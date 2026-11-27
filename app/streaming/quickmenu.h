#pragma once

#include "quickmenumodel.h"
#include "video/overlaymanager.h"

#include <QList>
#include <QMetaObject>

#include "SDL_compat.h"

class ClipboardManager;
class NvComputer;
class SdlInputHandler;
class ServerCommandManager;

// SDL_USEREVENT code that wakes the stream's event loop when the quick menu's
// status message expires
#define SDL_CODE_QUICK_MENU_TIMER 111

/**
 * @brief The quick menu, drawn into the stream as an overlay.
 *
 * Toggled with Ctrl+Alt+Shift+\ or Select+L1+R1+Y during a stream. It's part
 * of the stream window, so it works wherever the stream has focus (a separate
 * window never gets keyboard focus over a fullscreen stream on Wayland).
 *
 * Everything here runs on the thread that runs the stream's SDL event loop.
 * While the menu is open, SdlInputHandler hands it the keyboard and the
 * gamepads' buttons (see quickmenuinput.cpp) and the host has them released;
 * mouse, touch and motion input still go to the host.
 *
 * Results of actions (a command sent, a clipboard upload finishing) show in
 * the menu's detail area, or on their own in the same overlay for a few
 * seconds once the menu has closed.
 */
class QuickMenu
{
public:
    enum class Command {
        Up,
        Down,
        PageUp,
        PageDown,
        Home,
        End,
        Activate,
        Back,
        Close,
    };

    explicit QuickMenu(Overlay::OverlayManager& overlayManager);
    ~QuickMenu();

    QuickMenu(const QuickMenu&) = delete;
    QuickMenu& operator=(const QuickMenu&) = delete;

    // Called right before the stream's event loop starts and right after it
    // ends. The menu only opens in between, and only while the renderer can
    // display it.
    void attach(SDL_Window* window, SdlInputHandler* inputHandler, NvComputer* computer,
                ServerCommandManager* serverCommandManager, ClipboardManager* clipboardManager);
    void detach();

    bool isOpen() const { return m_Model.isOpen(); }
    void toggle();
    void open();
    void close();

    // A key or gamepad button, mapped by SdlInputHandler. repeat is set for
    // key repeats.
    void handleCommand(Command command, bool repeat);

    // SDL_CODE_QUICK_MENU_TIMER
    void handleTimer();

    // The window was resized or its renderer recreated: draw again for it, or
    // close if the new renderer can't display the menu
    void refresh();

private:
    void runActivation(const QuickMenuModel::Activation& activation);
    QuickMenuModel::HostState hostState() const;
    void showStatus(const QString& message);
    void render();
    void scheduleTimer(Uint32 delayMs);
    void cancelTimer();

    static Uint32 timerCallback(Uint32 interval, void* param);

    Overlay::OverlayManager& m_OverlayManager;
    QuickMenuModel m_Model;

    SDL_Window* m_Window;
    SdlInputHandler* m_InputHandler;
    NvComputer* m_Computer;
    ServerCommandManager* m_ServerCommandManager;
    ClipboardManager* m_ClipboardManager;
    QList<QMetaObject::Connection> m_Connections;

    QString m_Status;
    Uint32 m_StatusExpiry; // SDL_GetTicks() time
    SDL_TimerID m_Timer;
};
