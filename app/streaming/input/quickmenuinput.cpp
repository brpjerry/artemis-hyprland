// Artemis: input routing for the in-stream quick menu (streaming/quickmenu.h)

#include "streaming/session.h"
#include "streaming/quickmenu.h"

#include <Limelight.h>
#include "SDL_compat.h"

void SdlInputHandler::setQuickMenuOpen(bool open)
{
    if (m_QuickMenuOpen == open) {
        return;
    }

    if (open) {
        // Release everything the host has down. Keys and buttons the user
        // keeps holding, the toggle combo's Ctrl+Alt+Shift included, then go
        // to the menu until they're released, so nothing sticks on the host.
        raiseAllKeys();

        for (int i = 0; i < MAX_GAMEPADS; i++) {
            GamepadState* state = &m_GamepadState[i];
            if (state->controller == nullptr) {
                continue;
            }

            if (state->mouseEmulationTimer != 0) {
                // In mouse emulation, these buttons hold mouse buttons down
                static const struct {
                    int buttonFlag;
                    int mouseButton;
                } k_EmulatedMouseButtons[] = {
                    { A_FLAG, BUTTON_LEFT },
                    { B_FLAG, BUTTON_RIGHT },
                    { X_FLAG, BUTTON_MIDDLE },
                    { LB_FLAG, BUTTON_X1 },
                    { RB_FLAG, BUTTON_X2 },
                };
                for (const auto& button : k_EmulatedMouseButtons) {
                    if (state->buttons & button.buttonFlag) {
                        LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, button.mouseButton);
                    }
                }
            }

            LiSendMultiControllerEvent(state->index, m_GamepadMask, 0, 0, 0, 0, 0, 0, 0);
        }

        m_QuickMenuOpen = true;
    }
    else {
        m_QuickMenuOpen = false;

        for (int i = 0; i < MAX_GAMEPADS; i++) {
            GamepadState* state = &m_GamepadState[i];
            if (state->controller == nullptr) {
                continue;
            }

            // Buttons still down were held when the menu opened (the host saw
            // them released) or pressed for the menu, like the A or B that
            // closed it. The host gets them on their next press. Sticks and
            // triggers resume where they are.
            state->buttons = 0;
            if (state->mouseEmulationTimer == 0) {
                sendGamepadState(state);
            }
        }
    }
}

bool SdlInputHandler::handleQuickMenuKeyEvent(SDL_KeyboardEvent* event)
{
    const int scancode = event->keysym.scancode;

    if (!m_QuickMenuOpen) {
        // A key the menu took can still be down after it closed. The host
        // never saw it pressed, so it doesn't get the release or repeats.
        if (!m_QuickMenuKeysDown.contains(scancode)) {
            return false;
        }
        if (event->state == SDL_RELEASED) {
            m_QuickMenuKeysDown.remove(scancode);
            return true;
        }
        if (event->repeat) {
            return true;
        }

        // A new press means the release went missing (say, with focus), so
        // this press and its release are the host's
        m_QuickMenuKeysDown.remove(scancode);
        return false;
    }

    if (event->state == SDL_RELEASED) {
        m_QuickMenuKeysDown.remove(scancode);
        return true;
    }
    m_QuickMenuKeysDown.insert(scancode);

    QuickMenu* quickMenu = Session::get()->m_QuickMenu;
    const bool repeat = event->repeat != 0;

    // The toggle combo closes the menu, as in performSpecialKeyCombo()
    const auto& toggleCombo = m_SpecialKeyCombos[KeyComboToggleQuickMenu];
    if (!repeat && toggleCombo.enabled &&
            (event->keysym.mod & KMOD_CTRL) &&
            (event->keysym.mod & KMOD_ALT) &&
            (event->keysym.mod & KMOD_SHIFT) &&
            (event->keysym.sym == toggleCombo.keyCode || event->keysym.scancode == toggleCombo.scanCode)) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Detected quick menu toggle combo");
        quickMenu->close();
        return true;
    }

    switch (event->keysym.scancode) {
    case SDL_SCANCODE_UP:
        quickMenu->handleCommand(QuickMenu::Command::Up, repeat);
        break;
    case SDL_SCANCODE_DOWN:
        quickMenu->handleCommand(QuickMenu::Command::Down, repeat);
        break;
    case SDL_SCANCODE_PAGEUP:
        quickMenu->handleCommand(QuickMenu::Command::PageUp, repeat);
        break;
    case SDL_SCANCODE_PAGEDOWN:
        quickMenu->handleCommand(QuickMenu::Command::PageDown, repeat);
        break;
    case SDL_SCANCODE_HOME:
        quickMenu->handleCommand(QuickMenu::Command::Home, repeat);
        break;
    case SDL_SCANCODE_END:
        quickMenu->handleCommand(QuickMenu::Command::End, repeat);
        break;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        quickMenu->handleCommand(QuickMenu::Command::Activate, repeat);
        break;
    case SDL_SCANCODE_ESCAPE:
    case SDL_SCANCODE_BACKSPACE:
        quickMenu->handleCommand(QuickMenu::Command::Back, repeat);
        break;
    default:
        // Every other key is swallowed while the menu is open
        break;
    }

    return true;
}

bool SdlInputHandler::handleQuickMenuButtonEvent(GamepadState* state, SDL_ControllerButtonEvent* event)
{
    if (!m_QuickMenuOpen) {
        return false;
    }

    // Track the buttons without sending them. setQuickMenuOpen(false) clears
    // what's still down once the menu closes.
    if (event->state != SDL_PRESSED) {
        state->buttons &= ~k_ButtonMap[event->button];
        return true;
    }
    state->buttons |= k_ButtonMap[event->button];

    QuickMenu* quickMenu = Session::get()->m_QuickMenu;

    // The toggle combo (Select+L1+R1+Y) closes the menu, as in
    // handleControllerButtonEvent()
    if (state->buttons == (BACK_FLAG | LB_FLAG | RB_FLAG | Y_FLAG)) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected quick menu toggle gamepad combo");
        quickMenu->close();
        return true;
    }

    switch (event->button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        quickMenu->handleCommand(QuickMenu::Command::Up, false);
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        quickMenu->handleCommand(QuickMenu::Command::Down, false);
        break;
    case SDL_CONTROLLER_BUTTON_A:
        quickMenu->handleCommand(QuickMenu::Command::Activate, false);
        break;
    case SDL_CONTROLLER_BUTTON_B:
        quickMenu->handleCommand(QuickMenu::Command::Back, false);
        break;
    default:
        break;
    }

    return true;
}
