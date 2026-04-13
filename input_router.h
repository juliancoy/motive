#ifndef INPUT_ROUTER_H
#define INPUT_ROUTER_H

#include <glm/glm.hpp>
#include <memory>
#include <vector>

#include "model.h"

struct InputState
{
    bool keysPressed[6] = {false, false, false, false, false, false}; // W,A,S,D,Q,E
    glm::vec3 inputDir = glm::vec3(0.0f);
    bool jumpRequested = false;
};

class InputRouter
{
public:
    InputRouter();
    ~InputRouter();

    void setCharacterTarget(Model* model);
    Model* getCharacterTarget() const { return m_characterTarget; }

    void handleKey(int key, int action);
    void update(float deltaTime, const glm::vec3& cameraRotation, glm::vec3& inout_cameraPos, bool isFollowMode, bool controlsEnabled, float moveSpeed);

    const InputState& getInputState() const { return m_inputState; }
    const bool* getKeysPressed() const { return m_inputState.keysPressed; }

    bool isCharacterInputActive() const { return m_isCharacterInputActive; }

    void clearInput();

private:
    InputState m_inputState;
    Model* m_characterTarget = nullptr;
    bool m_isCharacterInputActive = false;
};

#endif // INPUT_ROUTER_H