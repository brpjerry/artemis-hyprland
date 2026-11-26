#pragma once

#include <QObject>
#include <QByteArray>
#include <QMutex>
#include <QQmlEngine>
#include <QString>

#include <atomic>

class NvComputer;

// Declare opaque pointers for Qt's meta-object system
#ifndef NVCOMPUTER_OPAQUE_DECLARED
#define NVCOMPUTER_OPAQUE_DECLARED
Q_DECLARE_OPAQUE_POINTER(NvComputer*)
#endif

// SDL_USEREVENT codes for clipboard work. All clipboard access happens on the
// streaming (SDL event loop) thread; other threads post these events instead.
#define SDL_CODE_CLIPBOARD_UPLOAD_REQUEST 110
#define SDL_CODE_CLIPBOARD_FETCH_REQUEST  111
#define SDL_CODE_CLIPBOARD_UPLOAD_DONE    112
#define SDL_CODE_CLIPBOARD_FETCH_DONE     113

/**
 * @brief Synchronizes the clipboard between the client and an Apollo host
 *        through the host's /actions/clipboard HTTP endpoint.
 *
 * Threading model:
 *  - The local clipboard is only accessed through SDL's clipboard API, on the
 *    streaming thread that runs the SDL event loop. On Linux that thread owns
 *    the focused stream window and keeps dispatching Wayland events, so SDL
 *    answers clipboard requests from other clients promptly. QClipboard must
 *    not be used here: during a stream the Qt main thread is blocked in
 *    Session::exec(), so a QClipboard selection is never flushed and its
 *    wl_data_source.send requests are never answered, which hangs every
 *    clipboard reader on the system.
 *  - HTTP requests run on QThreadPool workers with their own NvHTTP and report
 *    back to the streaming thread with SDL user events, so the event loop
 *    never blocks on the network.
 *  - The QML-facing properties and setters are used from the GUI thread
 *    outside of streams.
 *
 * Sync behaviour while streaming (when enabled):
 *  - Client -> host: whenever SDL reports a clipboard update. Compositors send
 *    the current selection to the stream window when it gains focus, so this
 *    also uploads what was copied locally while the stream was in the
 *    background.
 *  - Host -> client: when the stream window loses focus (if bidirectional),
 *    but only if the host clipboard changed since the last sync, so a newer
 *    local copy is never overwritten with stale host content.
 *  - Manual upload/fetch requests (quick menu) force a sync.
 */
class ClipboardManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isEnabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionChanged)
    Q_PROPERTY(bool textOnlyMode READ textOnlyMode WRITE setTextOnlyMode NOTIFY textOnlyModeChanged)
    Q_PROPERTY(int maxContentSizeMB READ maxContentSizeMB WRITE setMaxContentSizeMB NOTIFY maxContentSizeMBChanged)
    Q_PROPERTY(bool showNotifications READ showNotifications WRITE setShowNotifications NOTIFY showNotificationsChanged)
    Q_PROPERTY(bool bidirectionalSync READ isBidirectionalSyncEnabled WRITE setBidirectionalSync NOTIFY bidirectionalSyncChanged)
    QML_ELEMENT

public:
    explicit ClipboardManager(QObject *parent = nullptr);
    ~ClipboardManager();

    // Process-wide instance shared by the QML settings page and the Session.
    // The Session must not delete it.
    static ClipboardManager* instance();
    static ClipboardManager* create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    // Properties (GUI thread)
    bool isEnabled() const { return m_enabled; }
    bool isConnected() const;
    bool textOnlyMode() const { return m_textOnlyMode; }
    int maxContentSizeMB() const { return m_maxContentSizeMB; }
    bool showNotifications() const { return m_showNotifications; }
    bool isBidirectionalSyncEnabled() const { return m_bidirectionalSync; }

    void setEnabled(bool enabled);
    void setTextOnlyMode(bool textOnly);
    void setMaxContentSizeMB(int sizeMB);
    void setShowNotifications(bool show);
    void setBidirectionalSync(bool enabled);

    // Session lifecycle. setConnection() is called before the stream starts,
    // disconnect() when the Session is torn down. Results of requests still in
    // flight at disconnect() are discarded.
    void setConnection(NvComputer *computer);
    void disconnect();

    // Manual sync, callable from any thread (e.g. the quick menu). These only
    // post a request to the streaming thread and return whether it was queued.
    Q_INVOKABLE bool sendClipboard(bool force = true);
    Q_INVOKABLE bool getClipboard();

    // Streaming thread only: called from Session's SDL event loop.
    void onStreamClipboardUpdated();
    void onStreamFocusLost();
    void handleStreamEvent(int code, void *data);

signals:
    void enabledChanged();
    void connectionChanged();
    void textOnlyModeChanged();
    void maxContentSizeMBChanged();
    void showNotificationsChanged();
    void bidirectionalSyncChanged();

private:
    struct UploadResult;
    struct FetchResult;

    void loadSettings();

    // Streaming thread
    void uploadLocalClipboard(bool force);
    void requestFetch(bool force);
    void applyFetchedClipboard(const FetchResult &result);

    bool currentConnection(NvComputer **computer, quint64 *generation) const;
    static QByteArray contentHash(const QString &content);
    static bool postStreamEvent(int code, void *data);

    static constexpr int DEFAULT_MAX_SIZE = 1048576; // 1 MiB

    // Connection state, written on the GUI thread, read on the streaming thread
    mutable QMutex m_connectionLock;
    NvComputer *m_computer;
    quint64 m_generation;

    // Settings
    std::atomic<bool> m_enabled;
    std::atomic<bool> m_bidirectionalSync;
    std::atomic<int> m_maxClipboardSize;
    bool m_textOnlyMode;
    int m_maxContentSizeMB;
    bool m_showNotifications;

    // Streaming-thread state
    QByteArray m_lastSyncedHash; // content both sides agreed on most recently
    bool m_uploadInFlight;
    bool m_uploadPending;
    bool m_fetchInFlight;

    static ClipboardManager* s_instance;
};
