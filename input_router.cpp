#include "input_router.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <iostream>

InputRouter::InputRouter()
    : m_characterTarget(nullptr)
{
}

InputRouter::~InputRouter()
{
}

void InputRouter::setCharacterTarget(Model* model)
{
    m_characterTarget = model;
}

void InputRouter::handleKey(int key, int action)
{
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
    }
}

void InputRouter::update(float deltaTime, const glm::vec3& cameraRotation, 
                         glm::vec3& inout_cameraPos, bool isFollowMode, 
                         bool controlsEnabled, float moveSpeed)
{
    if (!controlsEnabled)
    {
        m_isCharacterInputActive = false;
        return;
    }

    // Character input: follow mode with target
    if (isFollowMode && m_characterTarget)
    {
        m_isCharacterInputActive = true;
        
        const float yaw = cameraRotation.x;
        const glm::vec3 forward(-sin(yaw), 0.0f, cos(yaw));
        const glm::vec3 right(cos(yaw), 0.0f, sin(yaw));

        glm::vec3 inputDir(0.0f);
        if (m_inputState.keysPressed[0]) inputDir += forward;
        if (m_inputState.keysPressed[1]) inputDir -= right;
        if (m_inputState.keysPressed[2]) inputDir -= forward;
        if (m_inputState.keysPressed[3]) inputDir += right;

        m_inputState.inputDir = inputDir;
        
        if (m_characterTarget->character.isControllable)
        {
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
        m_inputState.inputDir = glm::vec3(0.0f);
    }

    // Camera movement: free-fly mode (not follow)
    if (!isFollowMode && !m_inputState.inputDir.x && !m_inputState.inputDir.z)
    {
        const float yaw = cameraRotation.x;
        const float pitch = cameraRotation.y;

        glm::vec3 front;
        front.x = cos(pitch) * sin(yaw);
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
    
    if (m_characterTarget)
    {
        m_characterTarget->character.keyW = false;
        m_characterTarget->character.keyA = false;
        m_characterTarget->character.keyS = false;
        m_characterTarget->character.keyD = false;
    }
}