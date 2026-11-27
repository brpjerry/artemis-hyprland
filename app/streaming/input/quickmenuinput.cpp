// Artemis: input routing for the in-stream quick menu (streaming/quickmenu.h).
// Which events are the menu's is decided in quickmenuinputfilter.h.

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
        // Every key and button held now is the menu's until it's released,
        // the toggle combo's included, and the host gets them all released.
        // Their releases then never reach the host a second time, and held
        // modifiers don't apply to keys after the menu closes.
        m_QuickMenuKeyFilter.menuOpened();
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

            QuickMenuButtonFilter::menuOpened(state->buttons, state->quickMenuButtons);
            LiSendMultiControllerEvent(state->index, m_GamepadMask, 0, 0, 0, 0, 0, 0, 0);
        }

        m_QuickMenuOpen = true;
    }
    else {
        m_QuickMenuOpen = false;

        // Sticks and triggers resume where they are. No pad has buttons down
        // for the host (the menu keeps what's held until it's released), so
        // single-controller merging sends none either.
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            GamepadState* state = &m_GamepadState[i];
            if (state->controller != nullptr && state->mouseEmulationTimer == 0) {
                sendGamepadState(state);
            }
        }
    }
}

bool SdlInputHandler::handleQuickMenuKeyEvent(SDL_KeyboardEvent* event)
{
    const bool pressed = event->state == SDL_PRESSED;
    const bool repeat = event->repeat != 0;

    // The toggle combo, matched as in handleKeyEvent()
    const auto& toggleCombo = m_SpecialKeyCombos[KeyComboToggleQuickMenu];
    auto isToggleCombo = [&]() {
        return pressed && !repeat && toggleCombo.enabled &&
               (event->keysym.mod & KMOD_CTRL) &&
               (event->keysym.mod & KMOD_ALT) &&
               (event->keysym.mod & KMOD_SHIFT) &&
               (event->keysym.sym == toggleCombo.keyCode || event->keysym.scancode == toggleCombo.scanCode);
    };

    // This also clears modifiers the menu still owns from events for the host
    switch (m_QuickMenuKeyFilter.route(event->keysym.scancode, pressed, repeat, m_QuickMenuOpen, &event->keysym.mod)) {
    case QuickMenuKeyFilter::Route::Host:
        // performSpecialKeyCombo() handles the combo, and the host never sees
        // its key pressed. Make the key the menu's so its release doesn't
        // reach the host either, even if the menu can't open.
        if (isToggleCombo()) {
            m_QuickMenuKeyFilter.claimKey(event->keysym.scancode);
        }
        return false;
    case QuickMenuKeyFilter::Route::Drop:
        return true;
    case QuickMenuKeyFilter::Route::Menu:
        break;
    }

    QuickMenu* quickMenu = Session::get()->m_QuickMenu;

    // The toggle combo closes the menu
    if (isToggleCombo()) {
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
    const bool pressed = event->state == SDL_PRESSED;

    // The menu's buttons stay out of state->buttons, mouse emulation and the
    // Start long press, even once it has closed, until they're released
    if (!QuickMenuButtonFilter::route(state->quickMenuButtons, k_ButtonMap[event->button], pressed, m_QuickMenuOpen)) {
        return false;
    }
    if (!m_QuickMenuOpen || !pressed) {
        return true;
    }

    QuickMenu* quickMenu = Session::get()->m_QuickMenu;

    // The toggle combo (Select+L1+R1+Y) closes the menu, as in
    // handleControllerButtonEvent()
    if (state->quickMenuButtons == (BACK_FLAG | LB_FLAG | RB_FLAG | Y_FLAG)) {
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
