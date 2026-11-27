#include "quickmenu.h"

#include "session.h"
#include "input/input.h"
#include "backend/clipboardmanager.h"
#include "backend/nvcomputer.h"
#include "backend/servercommandmanager.h"
#include "backend/serverpermissions.h"
#include "settings/streamingpreferences.h"

#include <QReadLocker>

namespace {

// How long an action's result stays up
constexpr Uint32 kStatusDurationMs = 4000;

// The menu's share of the window, and its largest font size in pixels
constexpr double kMaxAreaFraction = 0.9;
constexpr int kMaxFontSize = 48;
constexpr int kMinFontSize = 14;
constexpr int kWindowHeightPerFontPixel = 30;

void pushQuitEvent()
{
    SDL_Event event = {};
    event.type = SDL_QUIT;
    event.quit.timestamp = SDL_GetTicks();
    SDL_PushEvent(&event);
}

QString quoted(const QString& commandName)
{
    // Host names can be any length; 64 is plenty to elide to 24
    return QStringLiteral("'%1'").arg(QuickMenuModel::elide(QuickMenuModel::sanitize(commandName.left(64)), 24));
}

}

QuickMenu::QuickMenu(Overlay::OverlayManager& overlayManager)
    : m_OverlayManager(overlayManager),
      m_Window(nullptr),
      m_InputHandler(nullptr),
      m_Computer(nullptr),
      m_Preferences(nullptr),
      m_ServerCommandManager(nullptr),
      m_ClipboardManager(nullptr),
      m_StatusExpiry(0),
      m_Timer(0)
{
}

QuickMenu::~QuickMenu()
{
    detach();
}

void QuickMenu::attach(SDL_Window* window, SdlInputHandler* inputHandler, NvComputer* computer,
                       const StreamingPreferences* preferences,
                       ServerCommandManager* serverCommandManager, ClipboardManager* clipboardManager)
{
    detach();

    m_Window = window;
    m_InputHandler = inputHandler;
    m_Computer = computer;
    m_Preferences = preferences;
    m_ServerCommandManager = serverCommandManager;
    m_ClipboardManager = clipboardManager;

    // These are emitted on this thread: commands run synchronously, and
    // clipboard results arrive through the stream's event loop
    if (m_ServerCommandManager != nullptr) {
        m_Connections.append(QObject::connect(m_ServerCommandManager, &ServerCommandManager::commandExecuted,
                                              [this](const QString& commandId, bool success, const QString& result) {
            showStatus(success ? QStringLiteral("Sent %1 to the host").arg(quoted(commandId))
                               : QStringLiteral("%1 failed: %2").arg(quoted(commandId), result));
        }));
        m_Connections.append(QObject::connect(m_ServerCommandManager, &ServerCommandManager::commandFailed,
                                              [this](const QString& commandId, const QString& error) {
            showStatus(QStringLiteral("%1 failed: %2").arg(quoted(commandId), error));
        }));
    }
    if (m_ClipboardManager != nullptr) {
        m_Connections.append(QObject::connect(m_ClipboardManager, &ClipboardManager::manualRequestFinished,
                                              [this](bool, bool, const QString& message) {
            showStatus(message);
        }));
    }
}

void QuickMenu::detach()
{
    for (const QMetaObject::Connection& connection : std::as_const(m_Connections)) {
        QObject::disconnect(connection);
    }
    m_Connections.clear();

    cancelTimer();
    m_Status.clear();

    if (m_Model.isOpen()) {
        m_Model.close();
        if (m_InputHandler != nullptr) {
            m_InputHandler->setQuickMenuOpen(false);
        }
    }

    if (m_Window != nullptr) {
        m_OverlayManager.setOverlayState(Overlay::OverlayQuickMenu, false);
    }

    m_Window = nullptr;
    m_InputHandler = nullptr;
    m_Computer = nullptr;
    m_Preferences = nullptr;
    m_ServerCommandManager = nullptr;
    m_ClipboardManager = nullptr;
}

void QuickMenu::toggle()
{
    if (m_Model.isOpen()) {
        close();
    }
    else {
        open();
    }
}

void QuickMenu::open()
{
    if (m_Window == nullptr || m_Model.isOpen()) {
        return;
    }

    // A menu nobody can see would still take the keyboard and gamepads, and
    // the next Enter or A would disconnect the stream
    if (const char* reason = whyNotDisplayable()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Not opening the quick menu: %s",
                    reason);
        return;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Opening the quick menu");

    // Pick up the host's current command list, as the host indexes it
    if (m_ServerCommandManager != nullptr) {
        m_ServerCommandManager->refreshCommands();
    }

    m_Model.open(hostState());

    // Draw it first: if that fails, render() closes it, and the host keeps
    // whatever is held down
    render();

    // The menu takes the keyboard and gamepad buttons from here on, and
    // anything held down is released on the host. Nothing else runs on this
    // thread in between, so no input event can slip past it.
    if (m_Model.isOpen()) {
        m_InputHandler->setQuickMenuOpen(true);
    }
}

void QuickMenu::close()
{
    if (!m_Model.isOpen()) {
        return;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Closing the quick menu");

    m_Model.close();
    m_InputHandler->setQuickMenuOpen(false);

    // Hides the overlay, or leaves a status showing on its own
    render();
}

void QuickMenu::handleCommand(Command command, bool repeat)
{
    if (!m_Model.isOpen()) {
        return;
    }

    const QuickMenuModel::Page previousPage = m_Model.page();
    const int previousSelection = m_Model.selection();
    const int page = QuickMenuModel::kVisibleCommandRows;

    switch (command) {
    case Command::Up:
        m_Model.moveSelection(-1, !repeat);
        break;
    case Command::Down:
        m_Model.moveSelection(1, !repeat);
        break;
    case Command::PageUp:
        m_Model.moveSelection(-page, false);
        break;
    case Command::PageDown:
        m_Model.moveSelection(page, false);
        break;
    case Command::Home:
        m_Model.moveToFirst();
        break;
    case Command::End:
        m_Model.moveToLast();
        break;
    case Command::Activate:
        if (!repeat) {
            runActivation(m_Model.activate());
        }
        return;
    case Command::Back:
        if (repeat) {
            return;
        }
        if (m_Model.page() == QuickMenuModel::Page::Main) {
            close();
            return;
        }
        m_Model.back();
        break;
    case Command::Close:
        close();
        return;
    }

    // Moves stop at the ends of the list on key repeat: don't draw the same
    // panel again for those
    if (m_Model.page() != previousPage || m_Model.selection() != previousSelection) {
        render();
    }
}

void QuickMenu::runActivation(const QuickMenuModel::Activation& activation)
{
    switch (activation.action) {
    case QuickMenuModel::Action::None:
        // Moved to another page
        render();
        break;

    case QuickMenuModel::Action::Unavailable:
        showStatus(activation.message);
        break;

    case QuickMenuModel::Action::Disconnect:
        close();
        pushQuitEvent();
        break;

    case QuickMenuModel::Action::Quit:
        close();
        Session::get()->setShouldExit(true);
        pushQuitEvent();
        break;

    case QuickMenuModel::Action::UploadClipboard:
        close();
        if (m_ClipboardManager != nullptr && m_ClipboardManager->sendClipboard()) {
            // The result replaces this when it arrives
            showStatus(QStringLiteral("Uploading the clipboard to the host..."));
        }
        else {
            showStatus(QStringLiteral("Clipboard sync isn't running for this stream"));
        }
        break;

    case QuickMenuModel::Action::FetchClipboard:
        close();
        if (m_ClipboardManager != nullptr && m_ClipboardManager->getClipboard()) {
            showStatus(QStringLiteral("Fetching the host's clipboard..."));
        }
        else {
            showStatus(QStringLiteral("Clipboard sync isn't running for this stream"));
        }
        break;

    case QuickMenuModel::Action::ToggleStats:
        close();
        m_OverlayManager.setOverlayState(Overlay::OverlayDebug,
                                         !m_OverlayManager.isOverlayEnabled(Overlay::OverlayDebug));
        break;

    case QuickMenuModel::Action::RunServerCommand:
        close();
        // Reports through commandExecuted() or commandFailed() before it returns
        if (m_ServerCommandManager != nullptr) {
            m_ServerCommandManager->executeCommandAt(activation.commandIndex);
        }
        break;
    }
}

QuickMenuModel::HostState QuickMenu::hostState() const
{
    QuickMenuModel::HostState state;

    if (m_ServerCommandManager != nullptr) {
        state.serverCommands = m_ServerCommandManager->getAvailableCommands();
    }
    state.statsVisible = m_OverlayManager.isOverlayEnabled(Overlay::OverlayDebug);
    // The stream's cleanup quits the host app after a disconnect if set
    state.disconnectQuitsApp = m_Preferences != nullptr && m_Preferences->quitAppAfter;

    bool isApollo = false;
    quint32 permissions = 0;
    bool hostListsCommands = false;
    if (m_Computer != nullptr) {
        QReadLocker lock(&m_Computer->lock);
        isApollo = !m_Computer->apolloVersion.isEmpty();
        permissions = m_Computer->serverPermissions;
        hostListsCommands = !m_Computer->serverCommands.isEmpty();
    }

    // ServerCommandManager lists no commands without the permission
    if (state.serverCommands.isEmpty()) {
        state.serverCommandsUnavailable = (hostListsCommands && !(permissions & ServerPermissions::SERVER_COMMAND))
            ? QStringLiteral("The host doesn't let this client run its server commands")
            : QStringLiteral("No server commands on this host");
    }

    // The same permission bits ClipboardManager checks before each request.
    // Some Apollo builds don't report ApolloVersion, so a host that grants
    // permissions counts as Apollo too.
    if (!isApollo && permissions == 0) {
        state.uploadUnavailable = state.fetchUnavailable = QStringLiteral("Clipboard sync needs an Apollo host");
    }
    else {
        if (!(permissions & ServerPermissions::CLIPBOARD_SET)) {
            state.uploadUnavailable = QStringLiteral("The host doesn't let this client set its clipboard");
        }
        if (!(permissions & ServerPermissions::CLIPBOARD_READ)) {
            state.fetchUnavailable = QStringLiteral("The host doesn't let this client read its clipboard");
        }
    }

    return state;
}

void QuickMenu::showStatus(const QString& message)
{
    if (m_Window == nullptr) {
        return;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Quick menu: %s", qPrintable(message));

    m_Status = message;
    m_StatusExpiry = SDL_GetTicks() + kStatusDurationMs;
    if (!scheduleTimer(kStatusDurationMs)) {
        // Nothing would take it down again
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Not showing the quick menu status: SDL_AddTimer() failed: %s",
                    SDL_GetError());
        m_Status.clear();
    }
    render();
}

void QuickMenu::handleTimer()
{
    // May be stale (an earlier status's timer), so just draw what's current
    render();
}

void QuickMenu::refresh()
{
    // Minimized, hidden, or a recreated renderer that can't display it: give
    // the input back rather than keep it for a menu nobody can see. (With
    // background gamepad input, a minimized stream still gets button events.)
    if (m_Model.isOpen()) {
        if (const char* reason = whyNotDisplayable()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Closing the quick menu: %s",
                        reason);
            m_Status.clear();
            close();
            return;
        }
    }

    if (m_OverlayManager.isOverlayEnabled(Overlay::OverlayQuickMenu)) {
        render();
    }
}

void QuickMenu::render()
{
    if (m_Window == nullptr) {
        return;
    }

    if (!m_Status.isEmpty() && SDL_TICKS_PASSED(SDL_GetTicks(), m_StatusExpiry)) {
        m_Status.clear();
    }

    const QString text = m_Model.isOpen() ? m_Model.text(m_Status) : QuickMenuModel::toastText(m_Status);
    if (text.isEmpty()) {
        m_OverlayManager.setOverlayState(Overlay::OverlayQuickMenu, false);
        return;
    }

    // Size the text to the window, in the pixels the renderers draw in
    int width, height;
#if SDL_VERSION_ATLEAST(2, 26, 0)
    SDL_GetWindowSizeInPixels(m_Window, &width, &height);
#else
    SDL_GetWindowSize(m_Window, &width, &height);
#endif
    m_OverlayManager.setOverlayFitArea(Overlay::OverlayQuickMenu,
                                       qMax(1, (int)(width * kMaxAreaFraction)),
                                       qMax(1, (int)(height * kMaxAreaFraction)),
                                       qBound(kMinFontSize, height / kWindowHeightPerFontPixel, kMaxFontSize));

    // The text is sanitized to ASCII and kept well under the overlay's limit
    const QByteArray utf8 = text.toUtf8();
    SDL_assert(utf8.size() < m_OverlayManager.getOverlayMaxTextLength());
    m_OverlayManager.updateOverlayText(Overlay::OverlayQuickMenu, utf8.constData());
    m_OverlayManager.setOverlayState(Overlay::OverlayQuickMenu, true);

    // If the panel couldn't be drawn, renderers keep showing the old one, or
    // nothing: don't let Enter or A act on a selection nobody can see
    if (m_OverlayManager.takeOverlayPanelFailure(Overlay::OverlayQuickMenu)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Closing the quick menu: its panel couldn't be drawn");
        m_Status.clear();
        m_OverlayManager.setOverlayState(Overlay::OverlayQuickMenu, false);
        if (m_Model.isOpen()) {
            m_Model.close();
            m_InputHandler->setQuickMenuOpen(false);
        }
    }
}

const char* QuickMenu::whyNotDisplayable() const
{
    if (m_Window == nullptr) {
        return "there's no stream window";
    }

    // A visible window on another display still works
    if (SDL_GetWindowFlags(m_Window) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) {
        return "the stream window is minimized or hidden";
    }

    if (!m_OverlayManager.canShowOverlay(Overlay::OverlayQuickMenu)) {
        return "the renderer can't display it";
    }

    return nullptr;
}

bool QuickMenu::scheduleTimer(Uint32 delayMs)
{
    cancelTimer();

    // A little late, so the status has expired when it fires
    m_Timer = SDL_AddTimer(delayMs + 50, timerCallback, nullptr);
    return m_Timer != 0;
}

void QuickMenu::cancelTimer()
{
    if (m_Timer != 0) {
        SDL_RemoveTimer(m_Timer);
        m_Timer = 0;
    }
}

Uint32 QuickMenu::timerCallback(Uint32, void*)
{
    // Runs on SDL's timer thread. The event carries nothing, so a late one is
    // harmless.
    SDL_Event event = {};
    event.type = SDL_USEREVENT;
    event.user.code = SDL_CODE_QUICK_MENU_TIMER;

    // One-shot, unless the event queue was full: then try again shortly, or
    // the status would stay up
    return SDL_PushEvent(&event) == 1 ? 0 : 100;
}
