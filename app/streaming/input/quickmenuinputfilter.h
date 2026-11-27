#pragma once

// Artemis: which keyboard and gamepad events belong to the in-stream quick
// menu (streaming/quickmenu.h), kept free of the input handler so it can be
// tested on its own. SdlInputHandler applies it in quickmenuinput.cpp.

#include <QSet>

#include "SDL_compat.h"

/**
 * @brief Decides where each key event goes while the quick menu is, or was
 * recently, open.
 *
 * Keys the menu owns never reach the host: every key held when the menu
 * opens (the host gets those released when it opens) and every key pressed
 * while it's open. A key stays the menu's until it's released, even after the
 * menu closes, so neither its release nor its repeats reach the host, and a
 * modifier it owns is masked out of other keys' events. Otherwise closing
 * with Ctrl+Alt+Shift+\ and pressing Q before letting go would send the host
 * a Ctrl+Alt+Shift+Q and run the quit-stream shortcut.
 */
class QuickMenuKeyFilter
{
public:
    enum class Route {
        Host, // Handle the event as usual
        Menu, // A press (or repeat) for the menu
        Drop, // Nobody gets it
    };

    // The menu opened: the keys held right now are its own
    void menuOpened()
    {
        m_MenuKeys.unite(m_PhysicalKeys);
    }

    // Call for every key event, before anything else looks at it. For Host,
    // *mod has the modifiers the menu owns cleared.
    Route route(int scancode, bool pressed, bool repeat, bool menuOpen, Uint16* mod)
    {
        if (!pressed) {
            m_PhysicalKeys.remove(scancode);
        }
        else if (!repeat) {
            m_PhysicalKeys.insert(scancode);
        }

        if (menuOpen) {
            if (pressed) {
                m_MenuKeys.insert(scancode);
                return Route::Menu;
            }
            m_MenuKeys.remove(scancode);
            return Route::Drop;
        }

        if (m_MenuKeys.contains(scancode)) {
            if (!pressed) {
                m_MenuKeys.remove(scancode);
                return Route::Drop;
            }
            if (repeat) {
                return Route::Drop;
            }

            // A fresh press means the release went missing (say, with focus),
            // so this press and its release are the host's
            m_MenuKeys.remove(scancode);
        }

        if (!m_MenuKeys.isEmpty()) {
            *mod &= ~menuModifiers();
        }
        return Route::Host;
    }

    bool ownsKey(int scancode) const
    {
        return m_MenuKeys.contains(scancode);
    }

    bool isPhysicallyDown(int scancode) const
    {
        return m_PhysicalKeys.contains(scancode);
    }

private:
    Uint16 menuModifiers() const
    {
        static const struct {
            SDL_Scancode scancode;
            Uint16 modifier;
        } k_Modifiers[] = {
            { SDL_SCANCODE_LCTRL, KMOD_LCTRL },
            { SDL_SCANCODE_RCTRL, KMOD_RCTRL },
            { SDL_SCANCODE_LSHIFT, KMOD_LSHIFT },
            { SDL_SCANCODE_RSHIFT, KMOD_RSHIFT },
            { SDL_SCANCODE_LALT, KMOD_LALT },
            { SDL_SCANCODE_RALT, KMOD_RALT },
            { SDL_SCANCODE_LGUI, KMOD_LGUI },
            { SDL_SCANCODE_RGUI, KMOD_RGUI },
        };

        Uint16 modifiers = 0;
        for (const auto& modifier : k_Modifiers) {
            if (m_MenuKeys.contains(modifier.scancode)) {
                modifiers |= modifier.modifier;
            }
        }
        return modifiers;
    }

    QSet<int> m_PhysicalKeys; // Down, as far as this window has seen
    QSet<int> m_MenuKeys; // The menu's until released
};

/**
 * @brief The same for one gamepad's buttons, as k_ButtonMap flags.
 *
 * hostButtons is the GamepadState::buttons the host is sent; menuButtons are
 * the buttons the menu owns until they're released. Buttons never move back
 * from the menu to the host, so closing the menu with A or B doesn't send the
 * host that button, and in single-controller mode no pad sends another pad's
 * held buttons.
 */
namespace QuickMenuButtonFilter {

// The menu opened: the buttons held now are its own, and the host gets the
// pad released
inline void menuOpened(int& hostButtons, int& menuButtons)
{
    menuButtons |= hostButtons;
    hostButtons = 0;
}

// Returns true if the event is the menu's: the host's input handling must not
// see it
inline bool route(int& menuButtons, int buttonFlag, bool pressed, bool menuOpen)
{
    if (menuOpen) {
        if (pressed) {
            menuButtons |= buttonFlag;
        }
        else {
            menuButtons &= ~buttonFlag;
        }
        return true;
    }

    if (menuButtons & buttonFlag) {
        // Its release is the menu's. A press means the release went missing,
        // so this press is the host's.
        menuButtons &= ~buttonFlag;
        return !pressed;
    }
    return false;
}

}
