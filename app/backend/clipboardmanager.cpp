#include "clipboardmanager.h"
#include "nvcomputer.h"
#include "nvhttp.h"
#include "settings/artemissettings.h"
#include "SDL_compat.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QGuiApplication>
#include <QMetaObject>
#include <QMutexLocker>
#include <QReadLocker>
#include <QThreadPool>

ClipboardManager* ClipboardManager::s_instance = nullptr;

ClipboardManager::ClipboardManager(QObject *parent)
    : QObject(parent)
    , m_computer(nullptr)
    , m_generation(0)
    , m_httpWorkerRunning(false)
    , m_shuttingDown(false)
    , m_wakePosted(false)
    , m_enabled(false)
    , m_bidirectionalSync(true)
    , m_maxClipboardSize(DEFAULT_MAX_SIZE)
    , m_textOnlyMode(true)
    , m_maxContentSizeMB(1)
{
    resetStreamState();
    loadSettings();

    if (QCoreApplication::instance()) {
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                this, &ClipboardManager::onAboutToQuit);
    }
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

void ClipboardManager::onAboutToQuit()
{
    // HTTP jobs run on the global thread pool, which main() waits on (with a
    // timeout) after the event loop exits. Don't start anything new from here
    // on. A request already running is aborted by NvHTTP's own aboutToQuit
    // handling, and is bounded by its request timeout regardless.
    QMutexLocker lock(&m_lock);
    m_shuttingDown = true;
    m_httpQueue.clear();
}

bool ClipboardManager::isConnected() const
{
    QMutexLocker lock(&m_lock);
    return m_computer != nullptr;
}

void ClipboardManager::resetStreamState()
{
    m_lastSyncedHash.clear();
    m_localUnconfirmed = false;
    m_textSetThroughSdl.clear();
    m_coveredClipboardUpdates = 0;
    m_uploadInFlight = false;
    m_pendingUploadText.clear();
    m_pendingUploadForced = false;
    m_fetchInFlight = false;
    m_fetchSuperseded = false;
    m_pendingFetch = PendingFetch::None;
}

void ClipboardManager::prepareForStream()
{
    // Once the stream runs, nothing dispatches Qt's display connection until
    // it ends, so Qt can't serve a selection it owns: every clipboard reader
    // on the system would block on it, and SDL's reads from the stream would
    // stall for SDL's read timeout. Qt owns the selection after
    // handOffLocalClipboard() at the end of the previous stream, or after a
    // copy in Artemis's own UI. Note its text while the Qt event loop still
    // runs, and let setConnection() take it over through SDL. If the stream
    // never gets that far, Qt simply keeps the selection.
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) == nullptr) {
        return;
    }

    // Keep the note current while the Qt event loop still runs during stream
    // startup, so a copy made elsewhere in the meantime isn't overwritten
    connect(QGuiApplication::clipboard(), &QClipboard::changed,
            this, &ClipboardManager::onQtClipboardChanged, Qt::UniqueConnection);
    onQtClipboardChanged(QClipboard::Clipboard);
}

void ClipboardManager::onQtClipboardChanged(QClipboard::Mode mode)
{
    if (mode != QClipboard::Clipboard) {
        return;
    }

    // QClipboard returns its own data without a round trip while it owns the
    // selection, and nothing is read otherwise
    QClipboard *clipboard = QGuiApplication::clipboard();
    const QString text = clipboard->ownsClipboard() ? clipboard->text() : QString();

    QMutexLocker lock(&m_lock);
    m_textFromQt = text;
}

void ClipboardManager::setConnection(NvComputer *computer)
{
    // Start the new session with fresh state before its SDL event loop runs
    resetStreamState();

    bool changed;
    QString textFromQt;
    {
        QMutexLocker lock(&m_lock);
        changed = (m_computer != nullptr) != (computer != nullptr);
        m_computer = computer;
        m_generation++;

        // Nothing queued so far belongs to this session. A request from an
        // older session that is already running finishes first, since the
        // HTTP queue runs one request at a time.
        m_pending = PendingWork();
        m_httpQueue.clear();

        textFromQt.swap(m_textFromQt);
    }

    // A wakeup still in flight from an older session may never be delivered
    m_wakePosted = false;

    if (!textFromQt.isEmpty()) {
        takeOverQtClipboard(textFromQt);
    }

    // Apollo's PERM bits (src/crypto.h): clipboard_set = 1 << 16,
    // clipboard_read = 1 << 17. Only Apollo reports a Permission mask in
    // serverinfo, so Sunshine and GFE hosts get no clipboard requests at all.
    constexpr quint32 kApolloPermClipboardSet = 1u << 16;
    constexpr quint32 kApolloPermClipboardRead = 1u << 17;
    m_hostAcceptsUpload = false;
    m_hostServesFetch = false;
    if (computer) {
        QReadLocker lock(&computer->lock);
        m_hostAcceptsUpload = (computer->serverPermissions & kApolloPermClipboardSet) != 0;
        m_hostServesFetch = (computer->serverPermissions & kApolloPermClipboardRead) != 0;
        qDebug() << "ClipboardManager: Connected to" << computer->name
                 << "- upload:" << m_hostAcceptsUpload << "fetch:" << m_hostServesFetch;
    }

    if (changed) {
        emit connectionChanged();
    }
}

void ClipboardManager::takeOverQtClipboard(const QString &text)
{
    // See prepareForStream(). Setting the same text through SDL moves the
    // selection to SDL's display connection, which this thread pumps for the
    // rest of the stream. disconnect() hands it back to Qt afterwards.
    const QByteArray utf8 = text.toUtf8();
    if (utf8.contains('\0')) {
        return;
    }

    if (SDL_SetClipboardText(utf8.constData()) != 0) {
        qWarning() << "ClipboardManager: Failed to take over the clipboard from Qt:" << SDL_GetError();
        return;
    }

    m_textSetThroughSdl = utf8;
    qDebug() << "ClipboardManager: Took over the clipboard from Qt for the stream";
}

void ClipboardManager::disconnect()
{
    // Must run before the stream's SDL window and video subsystem go away
    handOffLocalClipboard();
    resetStreamState();

    bool changed;
    {
        QMutexLocker lock(&m_lock);
        changed = m_computer != nullptr;
        m_computer = nullptr;
        m_generation++;

        // Drop this session's results, manual requests and queued HTTP jobs.
        // A request already running can't be recalled, but its result is
        // discarded because the generation changed.
        m_pending = PendingWork();
        m_httpQueue.clear();
    }

    m_wakePosted = false;

    qDebug() << "ClipboardManager: Disconnected";

    if (changed) {
        emit connectionChanged();
    }
}

void ClipboardManager::handOffLocalClipboard()
{
    // While SDL owns the selection with text we set (from the host, or taken
    // over from Qt), it answers other clients' requests for it
    // (wl_data_source.send on Wayland, SelectionRequest on X11) only while
    // something pumps SDL events. Once the stream's event loop exits, nothing
    // reliably does: the GUI pumps SDL only for gamepad navigation while its
    // own window is focused, and SDL video can stay initialized after the
    // stream. Pasting that text into another application would then block the
    // reader on a pipe nobody writes.
    //
    // So if the local clipboard still holds the text we set, give the same
    // text to QClipboard. The call is queued to the GUI thread and runs once
    // the Qt event loop resumes after the stream. Qt keeps pumping its own
    // display connection, so it can serve the selection for as long as it
    // owns it (prepareForStream() moves it back to SDL for the next stream).
    // If the user copied something else since, the clipboard isn't ours to
    // pass on and is left alone.
    if (m_textSetThroughSdl.isEmpty() || QCoreApplication::instance() == nullptr) {
        return;
    }

    // Returns our own copy without a round trip while SDL still owns the
    // selection
    char *rawText = SDL_GetClipboardText();
    const QByteArray current(rawText ? rawText : "");
    SDL_free(rawText);

    if (current != m_textSetThroughSdl) {
        qDebug() << "ClipboardManager: Local clipboard changed since it was set, not handing it over to Qt";
        return;
    }

    const QString text = QString::fromUtf8(current);
    QMetaObject::invokeMethod(QCoreApplication::instance(), [text]() {
        QGuiApplication::clipboard()->setText(text);
    }, Qt::QueuedConnection);

    qDebug() << "ClipboardManager: Handing the clipboard over to Qt";
}

bool ClipboardManager::currentEndpoint(Endpoint *endpoint, quint64 *generation) const
{
    NvComputer *computer;
    {
        QMutexLocker lock(&m_lock);
        computer = m_computer;
        *generation = m_generation;
    }
    if (computer == nullptr) {
        return false;
    }

    // The Session keeps its NvComputer alive until after disconnect(), which
    // runs on this thread, so it's safe to read here. Polling may update these
    // fields from another thread, so take the host's lock.
    QReadLocker lock(&computer->lock);
    endpoint->address = computer->activeAddress;
    endpoint->httpsPort = computer->activeHttpsPort;
    endpoint->serverCert = computer->serverCert;
    endpoint->useTrueUid = !computer->isNvidiaServerSoftware;

    if (endpoint->address.isNull()) {
        qWarning() << "ClipboardManager: Host has no active address";
        return false;
    }
    return true;
}

QByteArray ClipboardManager::contentHash(const QByteArray &utf8)
{
    return QCryptographicHash::hash(utf8, QCryptographicHash::Sha256);
}

void ClipboardManager::wakeStream()
{
    // One outstanding wakeup is enough: processPendingWork() takes everything
    if (m_wakePosted.exchange(true)) {
        return;
    }

    SDL_Event event = {};
    event.type = SDL_USEREVENT;
    event.user.code = SDL_CODE_CLIPBOARD_WAKE;

    if (SDL_PushEvent(&event) != 1) {
        // The work stays stored. The SDL loop checks for it on every
        // iteration (processStrandedWork()).
        m_wakePosted = false;
        m_wakeFailed = true;
    }
}

void ClipboardManager::processStrandedWork()
{
    if (m_wakeFailed.exchange(false)) {
        processPendingWork();
    }
}

bool ClipboardManager::sendClipboard(bool force)
{
    {
        QMutexLocker lock(&m_lock);
        if (m_computer == nullptr) {
            qWarning() << "ClipboardManager: No stream running, cannot upload clipboard";
            return false;
        }
        m_pending.manualUpload = true;
        m_pending.manualUploadForced |= force;
    }

    wakeStream();
    return true;
}

bool ClipboardManager::getClipboard()
{
    {
        QMutexLocker lock(&m_lock);
        if (m_computer == nullptr) {
            qWarning() << "ClipboardManager: No stream running, cannot fetch clipboard";
            return false;
        }
        m_pending.manualFetch = true;
    }

    wakeStream();
    return true;
}

void ClipboardManager::processPendingWork(bool uploadCurrentOffer)
{
    // Clear before taking the work, so anything stored after this posts a new
    // wakeup
    m_wakePosted = false;

    PendingWork work;
    {
        QMutexLocker lock(&m_lock);
        if (m_computer == nullptr) {
            return;
        }
        work = m_pending;
        m_pending = PendingWork();
    }

    // Read the current local offer before a completed fetch can replace it.
    // A pending manual upload is the same read, so it's folded in (with its
    // force flag) instead of POSTing the same text twice.
    if (uploadCurrentOffer) {
        uploadLocalClipboard(work.manualUpload && work.manualUploadForced);
        work.manualUpload = false;
    }

    // Upload results first: they record what the host confirmed, which a
    // fetch result is compared against
    if (work.hasUploadResult) {
        handleUploadResult(work.uploadSucceeded, work.uploadHash);
    }
    if (work.hasFetchResult) {
        handleFetchResult(work.fetchedContent, work.fetchForced);
    }
    if (work.manualUpload) {
        uploadLocalClipboard(work.manualUploadForced);
    }
    if (work.manualFetch) {
        requestFetch(true);
    }

    startDeferredFetch();
}

void ClipboardManager::onStreamClipboardUpdated()
{
    if (m_coveredClipboardUpdates > 0) {
        // Already read by the focus-gain read that preceded it
        --m_coveredClipboardUpdates;
        return;
    }
    processPendingWork(m_enabled);
}

void ClipboardManager::onStreamFocusGained()
{
    // A copy made while the stream was in the background is offered on focus
    // gain. Read the current offer once, before completions are processed.
    // SDL clipboard updates carry no text, so a successful read also covers
    // the ones already queued (but not any queued during the read); a failed
    // read resets this so they read again.
    m_coveredClipboardUpdates = m_enabled
        ? SDL_PeepEvents(nullptr, 0, SDL_PEEKEVENT, SDL_CLIPBOARDUPDATE, SDL_CLIPBOARDUPDATE)
        : 0;
    if (m_coveredClipboardUpdates < 0) {
        m_coveredClipboardUpdates = 0;
    }
    processPendingWork(m_enabled);
}

void ClipboardManager::onStreamFocusLost()
{
    processPendingWork();

    if (m_enabled && m_bidirectionalSync) {
        requestFetch(false);
    }
}

void ClipboardManager::uploadLocalClipboard(bool force)
{
    if (!isConnected()) {
        return;
    }
    if (!m_hostAcceptsUpload) {
        // Not an Apollo host, or this client may not set its clipboard
        return;
    }

    // Read the offer now: SDL can lose it (e.g. on focus loss) before an
    // upload in flight finishes. SDL returns an empty string if there's no
    // text or reading it failed.
    char *rawText = SDL_GetClipboardText();
    const QByteArray utf8(rawText ? rawText : "");
    SDL_free(rawText);

    if (utf8.isEmpty()) {
        // The read may have failed: let queued clipboard updates read again
        m_coveredClipboardUpdates = 0;
        if (force) {
            qDebug() << "ClipboardManager: No text on the local clipboard to upload";
        }
        return;
    }

    if (!m_textSetThroughSdl.isEmpty() && utf8 != m_textSetThroughSdl) {
        // The user copied something else, so the text we set is no longer ours
        // to hand over when the stream ends
        m_textSetThroughSdl.clear();
    }

    if (utf8.size() > m_maxClipboardSize) {
        qWarning() << "ClipboardManager: Local clipboard too large to upload:" << utf8.size() << "bytes";
        return;
    }

    const QString text = QString::fromUtf8(utf8);
    if (m_uploadInFlight) {
        // Keep only the newest copy and send it once the current upload is done
        m_pendingUploadText = text;
        m_pendingUploadForced |= force;
        return;
    }

    startUpload(text, force);
}

void ClipboardManager::startUpload(const QString &text, bool force)
{
    const QByteArray hash = contentHash(text.toUtf8());
    // After a failed upload, resend even the last synced text: its
    // acknowledgement is what re-enables automatic fetches
    if (!force && !m_localUnconfirmed && hash == m_lastSyncedHash) {
        // Already on the host (including our own SDL_SetClipboardText() echo)
        return;
    }

    HttpJob job;
    if (!currentEndpoint(&job.endpoint, &job.generation)) {
        return;
    }
    job.kind = HttpJob::Kind::Upload;
    job.text = text;
    job.hash = hash;

    m_uploadInFlight = true;
    if (m_fetchInFlight) {
        // That fetch may return the host's text from before this upload
        m_fetchSuperseded = true;
    }

    qDebug() << "ClipboardManager: Uploading local clipboard to host (" << text.size() << "characters)";
    enqueueHttpJob(std::move(job));
}

void ClipboardManager::requestFetch(bool force)
{
    if (!m_hostServesFetch) {
        // Not an Apollo host, or this client may not read its clipboard
        return;
    }
    if (m_uploadInFlight || m_fetchInFlight) {
        // Fetch once the current request finishes, so the result reflects any
        // upload in flight. Coalesce with an earlier deferred fetch, keeping
        // it forced if either was.
        if (force || m_pendingFetch == PendingFetch::None) {
            m_pendingFetch = force ? PendingFetch::Forced : PendingFetch::IfChanged;
        }
        return;
    }

    if (!force && m_localUnconfirmed) {
        // An upload of a newer local copy failed (possibly the session's
        // first, with no synced hash yet), so the host's text is older. Don't
        // spend the serial HTTP worker on a result that would be discarded.
        qDebug() << "ClipboardManager: Keeping the local copy the host hasn't confirmed";
        return;
    }

    HttpJob job;
    if (!currentEndpoint(&job.endpoint, &job.generation)) {
        return;
    }
    job.kind = HttpJob::Kind::Fetch;
    job.force = force;

    m_fetchInFlight = true;
    m_fetchSuperseded = false;

    qDebug() << "ClipboardManager: Fetching host clipboard";
    enqueueHttpJob(std::move(job));
}

void ClipboardManager::startDeferredFetch()
{
    if (m_pendingFetch == PendingFetch::None || m_uploadInFlight || m_fetchInFlight) {
        return;
    }

    const bool force = m_pendingFetch == PendingFetch::Forced;
    m_pendingFetch = PendingFetch::None;
    requestFetch(force);
}

void ClipboardManager::handleUploadResult(bool success, const QByteArray &hash)
{
    m_uploadInFlight = false;

    if (success) {
        // Only now does the host hold this content
        m_lastSyncedHash = hash;
        m_localUnconfirmed = false;
        qInfo() << "ClipboardManager: Uploaded clipboard to host";
    }
    else {
        // Keep the last confirmed hash, so the next clipboard update retries
        // this content. Until an upload succeeds, the local copy is newer than
        // anything an automatic fetch returns.
        m_localUnconfirmed = true;
        qWarning() << "ClipboardManager: Failed to upload clipboard to host";
    }

    if (!m_pendingUploadText.isEmpty()) {
        QString text;
        text.swap(m_pendingUploadText);
        const bool force = m_pendingUploadForced;
        m_pendingUploadForced = false;
        startUpload(text, force);
    }
}

void ClipboardManager::handleFetchResult(const QString &content, bool force)
{
    m_fetchInFlight = false;

    if (m_fetchSuperseded) {
        m_fetchSuperseded = false;

        // A newer local copy was uploaded after this fetch started, so the
        // result may be stale. An explicit request runs again after the upload.
        qDebug() << "ClipboardManager: Dropping host clipboard fetched before a newer upload";
        if (force) {
            m_pendingFetch = PendingFetch::Forced;
        }
        return;
    }

    applyFetchedClipboard(content, force);
}

void ClipboardManager::applyFetchedClipboard(const QString &content, bool force)
{
    if (content.isEmpty()) {
        qDebug() << "ClipboardManager: Host clipboard is empty or unavailable";
        return;
    }

    const QByteArray utf8 = content.toUtf8();
    if (utf8.contains('\0')) {
        // SDL_SetClipboardText() takes a NUL-terminated string, so it would
        // only install the text before the NUL
        qWarning() << "ClipboardManager: Host clipboard contains an embedded NUL, not applying it";
        return;
    }
    if (utf8.size() > m_maxClipboardSize) {
        qWarning() << "ClipboardManager: Host clipboard too large:" << utf8.size() << "bytes";
        return;
    }

    const QByteArray hash = contentHash(utf8);
    if (!force && hash == m_lastSyncedHash) {
        // The host hasn't changed its clipboard since the last sync. Don't
        // replace anything copied locally since then with stale host content.
        qDebug() << "ClipboardManager: Host clipboard unchanged since last sync";
        return;
    }

    // SDL owns the selection on its own display connection and answers
    // requests for it from the stream's event loop. disconnect() hands it
    // over to Qt when that loop ends.
    if (SDL_SetClipboardText(utf8.constData()) != 0) {
        qWarning() << "ClipboardManager: Failed to set local clipboard:" << SDL_GetError();
        return;
    }

    m_lastSyncedHash = hash;
    m_localUnconfirmed = false;
    m_textSetThroughSdl = utf8;
    qInfo() << "ClipboardManager: Set local clipboard from host (" << utf8.size() << "bytes)";
}

void ClipboardManager::enqueueHttpJob(HttpJob job)
{
    bool startWorker;
    {
        QMutexLocker lock(&m_lock);
        if (m_shuttingDown) {
            return;
        }
        m_httpQueue.push_back(std::move(job));
        startWorker = !m_httpWorkerRunning;
        m_httpWorkerRunning = true;
    }

    if (startWorker) {
        // The global pool is waited on (with a timeout) at exit. The instance
        // is never deleted, so the worker can't outlive it.
        QThreadPool::globalInstance()->start([this]() {
            runHttpQueue();
        });
    }
}

void ClipboardManager::runHttpQueue()
{
    // Run requests one at a time, in the order they were queued, so an older
    // POST can never land on Apollo after a newer one
    for (;;) {
        HttpJob job;
        {
            QMutexLocker lock(&m_lock);

            // Skip requests whose session ended before they started
            while (!m_httpQueue.empty() &&
                   (m_shuttingDown || m_httpQueue.front().generation != m_generation)) {
                m_httpQueue.pop_front();
            }
            if (m_httpQueue.empty()) {
                m_httpWorkerRunning = false;
                return;
            }

            job = std::move(m_httpQueue.front());
            m_httpQueue.pop_front();
        }

        NvHTTP http(job.endpoint.address, job.endpoint.httpsPort, job.endpoint.serverCert, job.endpoint.useTrueUid);

        bool uploadSucceeded = false;
        QString fetchedContent;
        try {
            if (job.kind == HttpJob::Kind::Upload) {
                uploadSucceeded = http.sendClipboardContent(job.text);
            }
            else {
                fetchedContent = http.getClipboardContent();
            }
        }
        catch (...) {
            qWarning() << "ClipboardManager: Unexpected exception during clipboard request";
        }

        {
            QMutexLocker lock(&m_lock);
            if (m_shuttingDown || job.generation != m_generation) {
                continue; // The session ended while this request ran
            }

            if (job.kind == HttpJob::Kind::Upload) {
                m_pending.hasUploadResult = true;
                m_pending.uploadSucceeded = uploadSucceeded;
                m_pending.uploadHash = job.hash;
            }
            else {
                m_pending.hasFetchResult = true;
                m_pending.fetchForced = job.force;
                m_pending.fetchedContent = fetchedContent;
            }
        }

        wakeStream();
    }
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
