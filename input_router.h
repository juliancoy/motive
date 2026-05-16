#ifndef INPUT_ROUTER_H
#define INPUT_ROUTER_H

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <vector>
#include <array>
#include <chrono>
#include <string>

#include "camera_mode.h"

class Display;
class Model;

struct InputState
{
    bool keysPressed[6] = {false, false, false, false, false, false}; // W,A,S,D,Q,E
    glm::vec3 inputDir = glm::vec3(0.0f);
    bool jumpRequested = false;
    bool sprintRequested = false;
};

class InputRouter
{
public:
    struct DebugState
    {
        std::array<bool, 6> effectiveKeys = {false, false, false, false, false, false};
        std::string lastInputSource = "none";
        std::string lastKeyName = "Unknown";
        bool jumpRequested = false;
        bool sprintRequested = false;
        bool characterInputActive = false;
        bool simulatedInputActive = false;
        bool nativeWindowFocused = false;
        bool characterTargetPresent = false;
        bool characterTargetControllable = false;
        std::int64_t lastKeyEventUnixMs = 0;
        std::int64_t lastUpdateUnixMs = 0;
        int lastKey = -1;
        int lastAction = -1;
    };

    InputRouter();
    ~InputRouter();

    void setDisplay(Display* display);

    void setCharacterTarget(Model* model);
    Model* getCharacterTarget() const { return m_characterTarget; }

    void handleKey(int key, int action);
    void update(float deltaTime, const glm::vec3& cameraRotation, glm::vec3& inout_cameraPos);
    void updateForMode(float deltaTime,
                       const glm::vec3& cameraRotation,
                       glm::vec3& inout_cameraPos,
                       CameraMode mode,
                       bool controlsEnabled,
                       float moveSpeed);

    const InputState& getInputState() const { return m_inputState; }
    const bool* getKeysPressed() const { return m_inputState.keysPressed; }
    const DebugState& getDebugState() const { return m_debugState; }

    bool isCharacterInputActive() const { return m_isCharacterInputActive; }

    void setSimulatedInput(const std::array<bool, 6>& keys,
                           bool jumpRequested,
                           bool sprintRequested,
                           float durationSeconds);
    void clearSimulatedInput();

    void clearInput();

private:
    InputState m_inputState;
    Model* m_characterTarget = nullptr;
    bool m_isCharacterInputActive = false;
    Display* m_display = nullptr;
    std::array<bool, 6> m_simulatedKeys = {false, false, false, false, false, false};
    bool m_simulatedInputActive = false;
    bool m_simulatedJumpRequested = false;
    bool m_simulatedSprintRequested = false;
    std::chrono::steady_clock::time_point m_simulatedInputExpiry = std::chrono::steady_clock::time_point::min();
    DebugState m_debugState;
};

#endif // INPUT_ROUTER_H
