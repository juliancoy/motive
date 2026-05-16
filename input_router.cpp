#include "input_router.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <cstdint>

#include "camera.h"
#include "display.h"
#include "input_mode_rules.h"
#include "model.h"

InputRouter::InputRouter()
    : m_characterTarget(nullptr),
      m_display(nullptr)
{
}

InputRouter::~InputRouter()
{
}

namespace
{
std::int64_t unixTimeMsNow()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string debugKeyName(int key)
{
    switch (key)
    {
        case GLFW_KEY_W: return "W";
        case GLFW_KEY_A: return "A";
        case GLFW_KEY_S: return "S";
        case GLFW_KEY_D: return "D";
        case GLFW_KEY_Q: return "Q";
        case GLFW_KEY_E: return "E";
        case GLFW_KEY_SPACE: return "Space";
        case GLFW_KEY_LEFT_SHIFT: return "LeftShift";
        case GLFW_KEY_RIGHT_SHIFT: return "RightShift";
        default: return "Unknown";
    }
}
}

void InputRouter::setDisplay(Display* display)
{
    m_display = display;
}

void InputRouter::setCharacterTarget(Model* model)
{
    m_characterTarget = model;
}

void InputRouter::handleKey(int key, int action)
{
    m_debugState.lastKey = key;
    m_debugState.lastAction = action;
    m_debugState.lastKeyName = debugKeyName(key);
    m_debugState.lastKeyEventUnixMs = unixTimeMsNow();

    const bool pressed = (action != GLFW_RELEASE);

    switch (key)
    {
        case GLFW_KEY_W:
            m_inputState.keysPressed[0] = pressed;
            break;
        case GLFW_KEY_A:
            m_inputState.keysPressed[1] = pressed;
            break;
        case GLFW_KEY_S:
            m_inputState.keysPressed[2] = pressed;
            break;
        case GLFW_KEY_D:
            m_inputState.keysPressed[3] = pressed;
            break;
        case GLFW_KEY_Q:
            m_inputState.keysPressed[4] = pressed;
            break;
        case GLFW_KEY_E:
            m_inputState.keysPressed[5] = pressed;
            break;
        case GLFW_KEY_SPACE:
            if (action == GLFW_PRESS && m_characterTarget && m_characterTarget->character.isGrounded)
            {
                m_inputState.jumpRequested = true;
            }
            break;
        case GLFW_KEY_LEFT_SHIFT:
        case GLFW_KEY_RIGHT_SHIFT:
            m_inputState.sprintRequested = pressed;
            break;
        default:
            break;
    }

    // Forward key state to character for animation
    if (m_characterTarget && m_characterTarget->character.isControllable)
    {
        m_characterTarget->character.keyW = m_inputState.keysPressed[0];
        m_characterTarget->character.keyA = m_inputState.keysPressed[1];
        m_characterTarget->character.keyS = m_inputState.keysPressed[2];
        m_characterTarget->character.keyD = m_inputState.keysPressed[3];
        m_characterTarget->character.keyQ = m_inputState.keysPressed[4];
        m_characterTarget->character.keyShift = m_inputState.sprintRequested;
        m_characterTarget->character.phaseThroughWalls = m_inputState.keysPressed[4];
    }

    std::cout << "[InputRouter] key=" << m_debugState.lastKeyName
              << " action=" << action
              << " targetControllable="
              << ((m_characterTarget && m_characterTarget->character.isControllable) ? "true" : "false")
              << std::endl;
}

void InputRouter::update(float deltaTime, const glm::vec3& cameraRotation, glm::vec3& inout_cameraPos)
{
    // Query camera state from Display
    Camera* camera = m_display ? m_display->getActiveCamera() : nullptr;
    if (!camera)
    {
        m_isCharacterInputActive = false;
        m_debugState.lastInputSource = "no_camera";
        m_debugState.characterInputActive = false;
        m_debugState.lastUpdateUnixMs = unixTimeMsNow();
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (m_simulatedInputActive && now >= m_simulatedInputExpiry)
    {
        clearSimulatedInput();
    }

    // Keep key state in sync with real keyboard state to avoid stale "stuck key"
    // behavior when release events are missed due to focus/input capture transitions.
    if (m_simulatedInputActive)
    {
        m_debugState.lastInputSource = "simulated";
        for (int i = 0; i < 6; ++i)
        {
            m_inputState.keysPressed[i] = m_simulatedKeys[static_cast<size_t>(i)];
        }
        if (m_simulatedJumpRequested)
        {
            m_inputState.jumpRequested = true;
            m_simulatedJumpRequested = false;
        }
        m_inputState.sprintRequested = m_simulatedSprintRequested;
    }
    else if (m_display && m_display->window)
    {
        const bool nativeFocused = (glfwGetWindowAttrib(m_display->window, GLFW_FOCUSED) == GLFW_TRUE);
        m_debugState.nativeWindowFocused = nativeFocused;
        if (nativeFocused)
        {
            m_debugState.lastInputSource = "nativeKeyboard";
            m_inputState.keysPressed[0] = (glfwGetKey(m_display->window, GLFW_KEY_W) == GLFW_PRESS);
            m_inputState.keysPressed[1] = (glfwGetKey(m_display->window, GLFW_KEY_A) == GLFW_PRESS);
            m_inputState.keysPressed[2] = (glfwGetKey(m_display->window, GLFW_KEY_S) == GLFW_PRESS);
            m_inputState.keysPressed[3] = (glfwGetKey(m_display->window, GLFW_KEY_D) == GLFW_PRESS);
            m_inputState.keysPressed[4] = (glfwGetKey(m_display->window, GLFW_KEY_Q) == GLFW_PRESS);
            m_inputState.keysPressed[5] = (glfwGetKey(m_display->window, GLFW_KEY_E) == GLFW_PRESS);
            m_inputState.sprintRequested =
                (glfwGetKey(m_display->window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ||
                (glfwGetKey(m_display->window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
        }
        else
        {
            m_debugState.lastInputSource = "windowUnfocused";
        }
    }

    for (int i = 0; i < 6; ++i)
    {
        m_debugState.effectiveKeys[static_cast<size_t>(i)] = m_inputState.keysPressed[i];
    }
    m_debugState.jumpRequested = m_inputState.jumpRequested;
    m_debugState.sprintRequested = m_inputState.sprintRequested;
    m_debugState.simulatedInputActive = m_simulatedInputActive;
    m_debugState.characterTargetPresent = (m_characterTarget != nullptr);
    m_debugState.characterTargetControllable =
        (m_characterTarget && m_characterTarget->character.isControllable);
    m_debugState.lastUpdateUnixMs = unixTimeMsNow();

    updateForMode(deltaTime,
                  cameraRotation,
                  inout_cameraPos,
                  camera->getMode(),
                  camera->getControlsEnabled(),
                  camera->moveSpeed);
}

void InputRouter::setSimulatedInput(const std::array<bool, 6>& keys,
                                    bool jumpRequested,
                                    bool sprintRequested,
                                    float durationSeconds)
{
    m_simulatedKeys = keys;
    m_simulatedInputActive = true;
    m_simulatedJumpRequested = jumpRequested;
    m_simulatedSprintRequested = sprintRequested;
    const float clampedDuration = std::max(0.0f, durationSeconds);
    m_simulatedInputExpiry = std::chrono::steady_clock::now() + std::chrono::milliseconds(
        static_cast<int>(clampedDuration * 1000.0f));
    m_debugState.simulatedInputActive = true;
}

void InputRouter::clearSimulatedInput()
{
    m_simulatedKeys = {false, false, false, false, false, false};
    m_simulatedInputActive = false;
    m_simulatedJumpRequested = false;
    m_simulatedSprintRequested = false;
    m_simulatedInputExpiry = std::chrono::steady_clock::time_point::min();
    for (int i = 0; i < 6; ++i)
    {
        m_inputState.keysPressed[i] = false;
    }
    m_inputState.inputDir = glm::vec3(0.0f);
    m_inputState.sprintRequested = false;
    m_debugState.simulatedInputActive = false;
    if (m_characterTarget && m_characterTarget->character.isControllable)
    {
        m_characterTarget->character.keyW = false;
        m_characterTarget->character.keyA = false;
        m_characterTarget->character.keyS = false;
        m_characterTarget->character.keyD = false;
        m_characterTarget->character.keyQ = false;
        m_characterTarget->character.keyShift = false;
        m_characterTarget->character.phaseThroughWalls = false;
        m_characterTarget->setCharacterInput(glm::vec3(0.0f));
    }
}

void InputRouter::updateForMode(float deltaTime,
                                const glm::vec3& cameraRotation,
                                glm::vec3& inout_cameraPos,
                                CameraMode mode,
                                bool controlsEnabled,
                                float moveSpeed)
{
    (void)deltaTime;

    const bool routeCharacterInput = input_mode_rules::shouldRouteCharacterInput(mode, m_characterTarget != nullptr);
    const bool moveCamera = input_mode_rules::shouldMoveCamera(mode, controlsEnabled);

    // Character input is tied to CharacterFollow mode, even if direct camera controls are disabled.
    if (routeCharacterInput)
    {
        m_isCharacterInputActive = true;
        m_debugState.characterInputActive = true;
        
        const float yaw = cameraRotation.x;
        // Match camera forward convention used by rendering:
        // front = (sin(yaw), 0, -cos(yaw)) when pitch is ignored for ground motion.
        glm::vec3 forward(-std::sin(yaw), 0.0f, -std::cos(yaw));
        if (glm::length(forward) > 0.0f)
        {
            forward = glm::normalize(forward);
        }
        const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        glm::vec3 right = glm::cross(forward, worldUp);
        if (glm::length(right) > 0.0f)
        {
            right = glm::normalize(right);
        }

        glm::vec3 inputDir(0.0f);
        if (m_inputState.keysPressed[0]) inputDir += forward;
        if (m_inputState.keysPressed[1]) inputDir -= right;
        if (m_inputState.keysPressed[2]) inputDir -= forward;
        if (m_inputState.keysPressed[3]) inputDir += right;

        m_inputState.inputDir = inputDir;
        
        if (m_characterTarget->character.isControllable)
        {
            m_characterTarget->character.keyW = m_inputState.keysPressed[0];
            m_characterTarget->character.keyA = m_inputState.keysPressed[1];
            m_characterTarget->character.keyS = m_inputState.keysPressed[2];
            m_characterTarget->character.keyD = m_inputState.keysPressed[3];
            m_characterTarget->character.keyQ = m_inputState.keysPressed[4];
            m_characterTarget->character.keyShift = m_inputState.sprintRequested;
            m_characterTarget->character.phaseThroughWalls = m_inputState.keysPressed[4];
            m_characterTarget->setCharacterInput(inputDir);

            // Handle jump
            if (m_inputState.jumpRequested && m_characterTarget->character.isGrounded)
            {
                m_characterTarget->character.jumpRequested = true;
                m_inputState.jumpRequested = false;
            }
        }
    }
    else
    {
        m_isCharacterInputActive = false;
        m_debugState.characterInputActive = false;
        m_inputState.inputDir = glm::vec3(0.0f);
        m_inputState.jumpRequested = false;
        m_inputState.sprintRequested = false;
        if (m_characterTarget && m_characterTarget->character.isControllable)
        {
            m_characterTarget->setCharacterInput(glm::vec3(0.0f));
            m_characterTarget->character.keyW = false;
            m_characterTarget->character.keyA = false;
            m_characterTarget->character.keyS = false;
            m_characterTarget->character.keyD = false;
            m_characterTarget->character.keyQ = false;
            m_characterTarget->character.keyShift = false;
            m_characterTarget->character.phaseThroughWalls = false;
        }
    }

    // Camera movement is only active in FreeFly mode with controls enabled.
    if (moveCamera)
    {
        const float yaw = cameraRotation.x;
        const float pitch = cameraRotation.y;

        glm::vec3 front;
        front.x = -cos(pitch) * sin(yaw);
        front.y = sin(pitch);
        front.z = -cos(pitch) * cos(yaw);
        front = glm::normalize(front);

        glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        glm::vec3 right = glm::normalize(glm::cross(front, worldUp));
        glm::vec3 up = glm::normalize(glm::cross(right, front));

        glm::vec3 moveDir(0.0f);
        if (m_inputState.keysPressed[0]) moveDir += front;
        if (m_inputState.keysPressed[1]) moveDir -= right;
        if (m_inputState.keysPressed[2]) moveDir -= front;
        if (m_inputState.keysPressed[3]) moveDir += right;
        if (m_inputState.keysPressed[4]) moveDir -= up;
        if (m_inputState.keysPressed[5]) moveDir += up;

        if (glm::length(moveDir) > 0.0f)
        {
            inout_cameraPos += glm::normalize(moveDir) * moveSpeed;
        }
    }
}

void InputRouter::clearInput()
{
    for (int i = 0; i < 6; ++i)
    {
        m_inputState.keysPressed[i] = false;
    }
    m_inputState.inputDir = glm::vec3(0.0f);
    m_inputState.jumpRequested = false;
    m_inputState.sprintRequested = false;
    m_debugState.lastInputSource = "cleared";
    m_debugState.jumpRequested = false;
    m_debugState.sprintRequested = false;
    m_debugState.characterInputActive = false;
    m_debugState.lastUpdateUnixMs = unixTimeMsNow();
    for (bool& key : m_debugState.effectiveKeys)
    {
        key = false;
    }
    clearSimulatedInput();
    
    if (m_characterTarget)
    {
        m_characterTarget->character.keyW = false;
        m_characterTarget->character.keyA = false;
        m_characterTarget->character.keyS = false;
        m_characterTarget->character.keyD = false;
        m_characterTarget->character.keyQ = false;
        m_characterTarget->character.keyShift = false;
        m_characterTarget->character.phaseThroughWalls = false;
    }
}
