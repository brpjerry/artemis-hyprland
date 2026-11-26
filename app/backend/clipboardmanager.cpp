#include "clipboardmanager.h"
#include "nvcomputer.h"
#include "nvhttp.h"
#include "settings/artemissettings.h"
#include "SDL_compat.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QMutexLocker>
#include <QThreadPool>

#include <memory>

struct ClipboardManager::UploadResult
{
    quint64 generation;
    bool success;
    QByteArray hash;
};

struct ClipboardManager::FetchResult
{
    quint64 generation;
    bool force;
    QString content;
};

ClipboardManager* ClipboardManager::s_instance = nullptr;

ClipboardManager::ClipboardManager(QObject *parent)
    : QObject(parent)
    , m_computer(nullptr)
    , m_generation(0)
    , m_enabled(false)
    , m_bidirectionalSync(true)
    , m_maxClipboardSize(DEFAULT_MAX_SIZE)
    , m_textOnlyMode(true)
    , m_maxContentSizeMB(1)
    , m_showNotifications(true)
    , m_uploadInFlight(false)
    , m_uploadPending(false)
    , m_fetchInFlight(false)
{
    loadSettings();
}

ClipboardManager::~ClipboardManager()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

ClipboardManager* ClipboardManager::instance()
{
    if (!s_instance) {
        s_instance = new ClipboardManager();
    }
    return s_instance;
}

ClipboardManager* ClipboardManager::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)

    // The instance is shared with the Session, so QML must not take ownership of it
    ClipboardManager *manager = instance();
    QQmlEngine::setObjectOwnership(manager, QQmlEngine::CppOwnership);
    return manager;
}

void ClipboardManager::loadSettings()
{
    auto settings = ArtemisSettings::instance();

    m_enabled = settings->clipboardSyncEnabled();
    m_bidirectionalSync = settings->clipboardSyncBidirectional();
    m_maxClipboardSize = settings->clipboardSyncMaxSize();
    m_maxContentSizeMB = m_maxClipboardSize / (1024 * 1024);

    qDebug() << "ClipboardManager: Loaded settings - enabled:" << m_enabled
             << "bidirectional:" << m_bidirectionalSync
             << "maxSize:" << m_maxClipboardSize << "bytes";
}

bool ClipboardManager::isConnected() const
{
    QMutexLocker lock(&m_connectionLock);
    return m_computer != nullptr;
}

void ClipboardManager::setConnection(NvComputer *computer)
{
    bool changed;
    {
        QMutexLocker lock(&m_connectionLock);
        changed = (m_computer != nullptr) != (computer != nullptr);
        m_computer = computer;
        m_generation++;
    }

    // Called before the streaming thread starts, so resetting its state here is safe
    m_lastSyncedHash.clear();
    m_uploadInFlight = false;
    m_uploadPending = false;
    m_fetchInFlight = false;

    qDebug() << "ClipboardManager: Connected to" << (computer ? computer->name : QStringLiteral("null"));

    if (changed) {
        emit connectionChanged();
    }
}

void ClipboardManager::disconnect()
{
    bool changed;
    {
        QMutexLocker lock(&m_connectionLock);
        changed = m_computer != nullptr;
        m_computer = nullptr;
        m_generation++;
    }

    qDebug() << "ClipboardManager: Disconnected";

    if (changed) {
        emit connectionChanged();
    }
}

bool ClipboardManager::currentConnection(NvComputer **computer, quint64 *generation) const
{
    QMutexLocker lock(&m_connectionLock);
    *computer = m_computer;
    *generation = m_generation;
    return m_computer != nullptr;
}

QByteArray ClipboardManager::contentHash(const QString &content)
{
    return QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha256);
}

bool ClipboardManager::postStreamEvent(int code, void *data)
{
    SDL_Event event = {};
    event.type = SDL_USEREVENT;
    event.user.code = code;
    event.user.data1 = data;

    // Fails if no stream is running (SDL events not initialized)
    return SDL_PushEvent(&event) == 1;
}

bool ClipboardManager::sendClipboard(bool force)
{
    if (!isConnected()) {
        qWarning() << "ClipboardManager: No stream running, cannot upload clipboard";
        return false;
    }
    return postStreamEvent(SDL_CODE_CLIPBOARD_UPLOAD_REQUEST, force ? reinterpret_cast<void*>(1) : nullptr);
}

bool ClipboardManager::getClipboard()
{
    if (!isConnected()) {
        qWarning() << "ClipboardManager: No stream running, cannot fetch clipboard";
        return false;
    }
    return postStreamEvent(SDL_CODE_CLIPBOARD_FETCH_REQUEST, nullptr);
}

void ClipboardManager::onStreamClipboardUpdated()
{
    if (m_enabled) {
        uploadLocalClipboard(false);
    }
}

void ClipboardManager::onStreamFocusLost()
{
    if (m_enabled && m_bidirectionalSync) {
        requestFetch(false);
    }
}

void ClipboardManager::handleStreamEvent(int code, void *data)
{
    NvComputer *computer;
    quint64 generation;
    const bool connected = currentConnection(&computer, &generation);

    switch (code) {
    case SDL_CODE_CLIPBOARD_UPLOAD_REQUEST:
        uploadLocalClipboard(data != nullptr);
        break;

    case SDL_CODE_CLIPBOARD_FETCH_REQUEST:
        requestFetch(true);
        break;

    case SDL_CODE_CLIPBOARD_UPLOAD_DONE: {
        std::unique_ptr<UploadResult> result(static_cast<UploadResult*>(data));
        if (!connected || result->generation != generation) {
            break; // Result from a previous session
        }

        m_uploadInFlight = false;
        if (result->success) {
            qInfo() << "ClipboardManager: Uploaded clipboard to host";
        }
        else {
            qWarning() << "ClipboardManager: Failed to upload clipboard to host";
            if (m_lastSyncedHash == result->hash) {
                // Let the next clipboard update retry this content
                m_lastSyncedHash.clear();
            }
        }

        if (m_uploadPending) {
            m_uploadPending = false;
            uploadLocalClipboard(false);
        }
        break;
    }

    case SDL_CODE_CLIPBOARD_FETCH_DONE: {
        std::unique_ptr<FetchResult> result(static_cast<FetchResult*>(data));
        if (!connected || result->generation != generation) {
            break; // Result from a previous session
        }

        m_fetchInFlight = false;
        applyFetchedClipboard(*result);
        break;
    }

    default:
        SDL_assert(false);
        break;
    }
}

void ClipboardManager::uploadLocalClipboard(bool force)
{
    NvComputer *computer;
    quint64 generation;
    if (!currentConnection(&computer, &generation)) {
        return;
    }

    if (m_uploadInFlight) {
        // Re-read the clipboard once the current upload finishes
        m_uploadPending = true;
        return;
    }

    if (!SDL_HasClipboardText()) {
        return;
    }

    // SDL reads other clients' selections with a timeout, so this can't hang
    char *rawText = SDL_GetClipboardText();
    if (rawText == nullptr) {
        return;
    }
    const QByteArray utf8(rawText);
    SDL_free(rawText);

    if (utf8.isEmpty()) {
        return;
    }
    if (utf8.size() > m_maxClipboardSize) {
        qWarning() << "ClipboardManager: Local clipboard too large to upload:" << utf8.size() << "bytes";
        return;
    }

    const QString text = QString::fromUtf8(utf8);
    const QByteArray hash = contentHash(text);
    if (!force && hash == m_lastSyncedHash) {
        // Already in sync (including our own SDL_SetClipboardText() echo)
        return;
    }

    // Optimistically mark as synced so repeated updates don't upload it again
    m_lastSyncedHash = hash;
    m_uploadInFlight = true;

    qDebug() << "ClipboardManager: Uploading local clipboard to host (" << utf8.size() << "bytes)";

    QThreadPool::globalInstance()->start([computer, generation, text, hash]() {
        NvHTTP http(computer);
        auto *result = new UploadResult{generation, http.sendClipboardContent(text), hash};
        if (!postStreamEvent(SDL_CODE_CLIPBOARD_UPLOAD_DONE, result)) {
            delete result;
        }
    });
}

void ClipboardManager::requestFetch(bool force)
{
    NvComputer *computer;
    quint64 generation;
    if (!currentConnection(&computer, &generation)) {
        return;
    }

    if (m_fetchInFlight) {
        return;
    }
    m_fetchInFlight = true;

    qDebug() << "ClipboardManager: Fetching host clipboard";

    QThreadPool::globalInstance()->start([computer, generation, force]() {
        NvHTTP http(computer);
        auto *result = new FetchResult{generation, force, http.getClipboardContent()};
        if (!postStreamEvent(SDL_CODE_CLIPBOARD_FETCH_DONE, result)) {
            delete result;
        }
    });
}

void ClipboardManager::applyFetchedClipboard(const FetchResult &result)
{
    if (result.content.isEmpty()) {
        qDebug() << "ClipboardManager: Host clipboard is empty or unavailable";
        return;
    }

    const QByteArray utf8 = result.content.toUtf8();
    if (utf8.size() > m_maxClipboardSize) {
        qWarning() << "ClipboardManager: Host clipboard too large:" << utf8.size() << "bytes";
        return;
    }

    const QByteArray hash = contentHash(result.content);
    if (!result.force && hash == m_lastSyncedHash) {
        // The host hasn't changed its clipboard since the last sync. Don't
        // replace anything copied locally since then with stale host content.
        qDebug() << "ClipboardManager: Host clipboard unchanged since last sync";
        return;
    }

    // SDL owns the selection on its own Wayland connection and answers
    // requests for it from the streaming thread's event loop.
    if (SDL_SetClipboardText(utf8.constData()) != 0) {
        qWarning() << "ClipboardManager: Failed to set local clipboard:" << SDL_GetError();
        return;
    }

    m_lastSyncedHash = hash;
    qInfo() << "ClipboardManager: Set local clipboard from host (" << utf8.size() << "bytes)";
}

void ClipboardManager::setEnabled(bool enabled)
{
    if (m_enabled != enabled) {
        m_enabled = enabled;

        auto settings = ArtemisSettings::instance();
        settings->setClipboardSyncEnabled(enabled);
        settings->save();

        emit enabledChanged();
        qDebug() << "ClipboardManager: Enabled changed to" << enabled;
    }
}

void ClipboardManager::setBidirectionalSync(bool enabled)
{
    if (m_bidirectionalSync != enabled) {
        m_bidirectionalSync = enabled;

        auto settings = ArtemisSettings::instance();
        settings->setClipboardSyncBidirectional(enabled);
        settings->save();

        emit bidirectionalSyncChanged();
        qDebug() << "ClipboardManager: Bidirectional sync changed to" << enabled;
    }
}

void ClipboardManager::setTextOnlyMode(bool textOnly)
{
    if (m_textOnlyMode != textOnly) {
        m_textOnlyMode = textOnly;
        emit textOnlyModeChanged();
        qDebug() << "ClipboardManager: Text-only mode changed to" << textOnly;
    }
}

void ClipboardManager::setMaxContentSizeMB(int sizeMB)
{
    if (m_maxContentSizeMB != sizeMB) {
        m_maxContentSizeMB = sizeMB;
        m_maxClipboardSize = sizeMB * 1024 * 1024;

        auto settings = ArtemisSettings::instance();
        settings->setClipboardSyncMaxSize(m_maxClipboardSize);
        settings->save();

        emit maxContentSizeMBChanged();
        qDebug() << "ClipboardManager: Max content size changed to" << sizeMB << "MB";
    }
}

void ClipboardManager::setShowNotifications(bool show)
{
    if (m_showNotifications != show) {
        m_showNotifications = show;
        emit showNotificationsChanged();
        qDebug() << "ClipboardManager: Show notifications changed to" << show;
    }
}
