#include "Platform/FInput.hpp"
#include "Platform/FWindow.hpp"

#include <GLFW/glfw3.h>
#include <cmath>

namespace Leon {

    namespace {

        GLFWwindow* GetInputWindow() {
            const FWindow* window = FWindow::GetMainWindow();
            return window ? static_cast<GLFWwindow*>(window->GetNativeHandle()) : nullptr;
        }

        bool GBlocked = false;
        bool GRebaseMouse = false;
        std::pair<float, float> GLastMouse{0.0f, 0.0f};
        std::pair<float, float> GMouseOffset{0.0f, 0.0f};

    } // namespace

    void FInput::SetBlocked(bool bInBlocked) {
        if (GBlocked && !bInBlocked)
            GRebaseMouse = true;
        GBlocked = bInBlocked;
    }

    bool FInput::IsBlocked() {
        return GBlocked;
    }

    bool FInput::IsKeyPressed(int InKeyCode) {
        GLFWwindow* window = GetInputWindow();
        if (!window || GBlocked)
            return false;

        auto state = glfwGetKey(window, InKeyCode);
        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    bool FInput::IsMouseButtonPressed(int InButton) {
        GLFWwindow* window = GetInputWindow();
        if (!window || GBlocked)
            return false;

        auto state = glfwGetMouseButton(window, InButton);
        return state == GLFW_PRESS;
    }

    std::pair<float, float> FInput::GetMousePosition() {
        GLFWwindow* window = GetInputWindow();
        if (!window)
            return {0.0f, 0.0f};

        if (GBlocked)
            return GLastMouse;

        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        std::pair<float, float> position{(float)xpos, (float)ypos};
        // A captured mouse has no place on screen, only movement: after a block it carries on from
        // where it was. A visible cursor is where it is.
        if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED) {
            if (GRebaseMouse) {
                GMouseOffset = {GLastMouse.first - position.first, GLastMouse.second - position.second};
                GRebaseMouse = false;
            }
            position.first += GMouseOffset.first;
            position.second += GMouseOffset.second;
        }
        GLastMouse = position;
        return position;
    }

    float FInput::GetMouseX() {
        auto [x, y] = GetMousePosition();
        return x;
    }

    float FInput::GetMouseY() {
        auto [x, y] = GetMousePosition();
        return y;
    }

    bool FInput::IsGamepadConnected(int InGamepadID) {
        int jid = GLFW_JOYSTICK_1 + InGamepadID;
        return glfwJoystickPresent(jid) && glfwJoystickIsGamepad(jid);
    }

    std::string FInput::GetGamepadName(int InGamepadID) {
        int jid = GLFW_JOYSTICK_1 + InGamepadID;
        if (glfwJoystickIsGamepad(jid)) {
            const char* name = glfwGetGamepadName(jid);
            return name ? std::string(name) : "Generic Gamepad";
        }
        return "Not Connected";
    }

    bool FInput::IsGamepadButtonPressed(int InButton, int InGamepadID) {
        int jid = GLFW_JOYSTICK_1 + InGamepadID;
        if (!glfwJoystickIsGamepad(jid))
            return false;

        GLFWgamepadstate state;
        if (glfwGetGamepadState(jid, &state)) {
            if (InButton >= 0 && InButton <= GLFW_GAMEPAD_BUTTON_LAST) {
                return state.buttons[InButton] == GLFW_PRESS;
            }
        }
        return false;
    }

    float FInput::GetGamepadAxis(int InAxis, int InGamepadID, float InDeadzone) {
        int jid = GLFW_JOYSTICK_1 + InGamepadID;
        if (!glfwJoystickIsGamepad(jid))
            return 0.0f;

        GLFWgamepadstate state;
        if (glfwGetGamepadState(jid, &state)) {
            if (InAxis >= 0 && InAxis <= GLFW_GAMEPAD_AXIS_LAST) {
                float value = state.axes[InAxis];
                if (std::abs(value) < InDeadzone)
                    return 0.0f;
                return value;
            }
        }
        return 0.0f;
    }

    float FInput::GetGamepadTrigger(int InAxis, int InGamepadID, float InThreshold) {
        // GLFW gamepad triggers rest at -1 and fully press at +1.
        const float raw = GetGamepadAxis(InAxis, InGamepadID, 0.0f);
        const float t = (raw + 1.0f) * 0.5f;
        return t >= InThreshold ? t : 0.0f;
    }

    std::pair<float, float> FInput::GetGamepadLeftStick(int InGamepadID, float InDeadzone) {
        float x = GetGamepadAxis(GamepadAxis::LeftX, InGamepadID, InDeadzone);
        float y = GetGamepadAxis(GamepadAxis::LeftY, InGamepadID, InDeadzone);
        return {x, y};
    }

    std::pair<float, float> FInput::GetGamepadRightStick(int InGamepadID, float InDeadzone) {
        float x = GetGamepadAxis(GamepadAxis::RightX, InGamepadID, InDeadzone);
        float y = GetGamepadAxis(GamepadAxis::RightY, InGamepadID, InDeadzone);
        return {x, y};
    }

} // namespace Leon
