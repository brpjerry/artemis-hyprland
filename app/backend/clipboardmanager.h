#pragma once

#include "nvaddress.h"

#include <QObject>
#include <QByteArray>
#include <QClipboard>
#include <QMutex>
#include <QQmlEngine>
#include <QSslCertificate>
#include <QString>

#include <atomic>
#include <deque>

class NvComputer;

// Declare opaque pointers for Qt's meta-object system
#ifndef NVCOMPUTER_OPAQUE_DECLARED
#define NVCOMPUTER_OPAQUE_DECLARED
Q_DECLARE_OPAQUE_POINTER(NvComputer*)
#endif

// SDL_USEREVENT code that wakes the stream's SDL event loop to process
// clipboard work (HTTP results, manual requests). It carries no data: the work
// itself is stored in the ClipboardManager, so a lost or stale wakeup is
// harmless.
#define SDL_CODE_CLIPBOARD_WAKE 110

/**
 * @brief Synchronizes the clipboard between the client and an Apollo host
 *        through the host's /actions/clipboard HTTP endpoint.
 *
 * Threading model:
 *  - While streaming, the local clipboard is only accessed through SDL's
 *    clipboard API, on the thread that pumps the stream's SDL event loop (the
 *    "SDL thread" below). Between streams Qt owns it (see the handoffs below).
 *    That thread keeps dispatching the stream window's Wayland events, so SDL
 *    answers clipboard requests from other clients promptly. QClipboard must
 *    not be used during a stream: the Qt event loop doesn't run until the
 *    stream ends, so a QClipboard selection would never answer
 *    wl_data_source.send, which hangs every clipboard reader on the system.
 *  - setConnection() and disconnect() are called on the SDL thread right
 *    before and right after the stream's event loop. The SDL-thread state is
 *    only used between those two calls.
 *  - HTTP requests run one at a time, in order, on a QThreadPool worker. Their
 *    results, and manual requests from other threads, are stored here under a
 *    mutex and discarded by disconnect(). The SDL loop is woken with a
 *    pointer-free SDL_CODE_CLIPBOARD_WAKE event and also checks for stored
 *    work on clipboard and focus events, so a lost wakeup doesn't strand work.
 *  - The QML-facing properties and setters are used from the GUI thread.
 *
 * Sync behaviour while streaming (when enabled):
 *  - Client -> host: whenever SDL reports a clipboard update. Compositors send
 *    the current selection to the stream window when it gains focus, so this
 *    also uploads what was copied locally while the stream was in the
 *    background.
 *  - Host -> client: when the stream window loses focus (if bidirectional),
 *    but only if the host clipboard changed since the last sync, so a newer
 *    local copy is never overwritten with stale host content. A fetch waits
 *    for an upload in flight, and a fetch overtaken by a newer upload is
 *    dropped.
 *  - Manual upload/fetch requests (quick menu) force a sync.
 *
 * Clipboard ownership follows whichever event loop runs (regardless of the
 * sync settings): text that Qt owns when a stream starts is taken over by SDL
 * for the stream, and text set through SDL that is still on the clipboard when
 * the stream ends is handed over to QClipboard.
 */
class ClipboardManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isEnabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionChanged)
    Q_PROPERTY(bool textOnlyMode READ textOnlyMode WRITE setTextOnlyMode NOTIFY textOnlyModeChanged)
    Q_PROPERTY(int maxContentSizeMB READ maxContentSizeMB WRITE setMaxContentSizeMB NOTIFY maxContentSizeMBChanged)
    Q_PROPERTY(bool bidirectionalSync READ isBidirectionalSyncEnabled WRITE setBidirectionalSync NOTIFY bidirectionalSyncChanged)
    QML_ELEMENT

public:
    explicit ClipboardManager(QObject *parent = nullptr);
    ~ClipboardManager();

    // Process-wide instance shared by the QML settings page and the Session.
    // It is never deleted, because queued HTTP work refers to it.
    static ClipboardManager* instance();
    static ClipboardManager* create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    // Properties (GUI thread)
    bool isEnabled() const { return m_enabled; }
    bool isConnected() const;
    bool textOnlyMode() const { return m_textOnlyMode; }
    int maxContentSizeMB() const { return m_maxContentSizeMB; }
    bool isBidirectionalSyncEnabled() const { return m_bidirectionalSync; }

    void setEnabled(bool enabled);
    void setTextOnlyMode(bool textOnly);
    void setMaxContentSizeMB(int sizeMB);
    void setBidirectionalSync(bool enabled);

    // Session lifecycle, GUI thread: called when a stream is about to start,
    // while the Qt event loop still runs. Notes the text Qt owns on the
    // clipboard, for setConnection() to take over.
    void prepareForStream();

    // Session lifecycle, SDL thread. setConnection() is called right before
    // the stream's SDL event loop starts. disconnect() is called right after
    // it exits, while the SDL window and video subsystem are still alive: it
    // hands text set through SDL that is still on the local clipboard over to
    // QClipboard, then discards all of this session's queued requests and
    // results.
    void setConnection(NvComputer *computer);
    void disconnect();

    // Manual sync, callable from any thread (e.g. the quick menu). These only
    // queue a request for the SDL thread and return whether a stream is
    // running to take it.
    Q_INVOKABLE bool sendClipboard(bool force = true);
    Q_INVOKABLE bool getClipboard();

    // SDL thread: called from the stream's SDL event loop. They process stored
    // work (HTTP results, manual requests). With sync enabled, a clipboard
    // update and focus gain read the local offer first, so a newer copy can
    // supersede a completed fetch. A successful focus-gain read also covers the
    // clipboard updates that were already queued.
    void processPendingWork(bool uploadCurrentOffer = false); // SDL_CODE_CLIPBOARD_WAKE
    // Called on every SDL loop iteration: picks up work whose wakeup event
    // couldn't be posted (SDL's event queue was full). Cheap when there is none.
    void processStrandedWork();
    void onStreamClipboardUpdated(); // SDL_CLIPBOARDUPDATE
    void onStreamFocusGained(); // SDL_WINDOWEVENT_FOCUS_GAINED
    void onStreamFocusLost(); // SDL_WINDOWEVENT_FOCUS_LOST

signals:
    void enabledChanged();
    void connectionChanged();
    void textOnlyModeChanged();
    void maxContentSizeMBChanged();
    void bidirectionalSyncChanged();

private:
    // Connection parameters copied from the NvComputer when work is queued,
    // so workers never touch the NvComputer itself
    struct Endpoint
    {
        NvAddress address;
        uint16_t httpsPort = 0;
        QSslCertificate serverCert;
    };

    struct HttpJob
    {
        enum class Kind { Upload, Fetch };

        Kind kind = Kind::Upload;
        quint64 generation = 0;
        Endpoint endpoint;
        QString text; // Upload
        QByteArray hash; // Upload
        bool force = false; // Fetch
    };

    // Work waiting for the SDL thread, guarded by m_lock
    struct PendingWork
    {
        bool hasUploadResult = false;
        bool uploadSucceeded = false;
        QByteArray uploadHash;

        bool hasFetchResult = false;
        bool fetchForced = false;
        QString fetchedContent;

        bool manualUpload = false;
        bool manualUploadForced = false;
        bool manualFetch = false;
    };

    enum class PendingFetch { None, IfChanged, Forced };

    void loadSettings();
    void onAboutToQuit();
    void onQtClipboardChanged(QClipboard::Mode mode); // GUI thread

    // SDL thread
    void uploadLocalClipboard(bool force);
    void startUpload(const QString &text, bool force);
    void requestFetch(bool force);
    void startDeferredFetch();
    void handleUploadResult(bool success, const QByteArray &hash);
    void handleFetchResult(const QString &content, bool force);
    void applyFetchedClipboard(const QString &content, bool force);
    void takeOverQtClipboard(const QString &text);
    void handOffLocalClipboard();
    void resetStreamState();
    bool currentEndpoint(Endpoint *endpoint, quint64 *generation) const;
    void enqueueHttpJob(HttpJob job);

    // Any thread
    void wakeStream();

    // Worker thread
    void runHttpQueue();

    static QByteArray contentHash(const QByteArray &utf8);

    static constexpr int DEFAULT_MAX_SIZE = 1048576; // 1 MiB

    // Shared by the SDL thread, the HTTP worker and manual request callers
    mutable QMutex m_lock;
    NvComputer *m_computer; // Non-null between setConnection() and disconnect()
    quint64 m_generation; // Incremented by setConnection() and disconnect()
    PendingWork m_pending;
    std::deque<HttpJob> m_httpQueue;
    bool m_httpWorkerRunning;
    bool m_shuttingDown;
    QString m_textFromQt; // Text Qt owns, noted on the GUI thread, taken by setConnection()
    std::atomic<bool> m_wakePosted;
    std::atomic<bool> m_wakeFailed {false};

    // Settings
    std::atomic<bool> m_enabled;
    std::atomic<bool> m_bidirectionalSync;
    std::atomic<int> m_maxClipboardSize;
    bool m_textOnlyMode;
    int m_maxContentSizeMB;

    // SDL thread
    QByteArray m_lastSyncedHash; // Content both sides confirmed most recently
    bool m_localUnconfirmed = false; // An upload failed: the local copy is newer than the host's
    QByteArray m_textSetThroughSdl; // UTF-8 text this session last put on the local clipboard
    int m_coveredClipboardUpdates = 0; // Queued updates covered by the focus-gain read
    bool m_uploadInFlight;
    QString m_pendingUploadText; // Newest local copy made while an upload was in flight
    bool m_pendingUploadForced;
    bool m_fetchInFlight;
    bool m_fetchSuperseded; // An upload started while the fetch in flight was running
    PendingFetch m_pendingFetch; // Fetch to run once no request is in flight

    static ClipboardManager* s_instance;
};
