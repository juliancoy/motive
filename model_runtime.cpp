#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include "model.h"
#include "engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/component_wise.hpp>

void Model::scaleToUnitBox()
{
    glm::vec3 minBounds(std::numeric_limits<float>::max());
    glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
    bool foundPositions = false;

    if (tgltfModel)
    {
        for (const auto &meshData : tgltfModel->meshes)
        {
            for (const auto &primitiveData : meshData.primitives)
            {
                auto attrIt = primitiveData.attributes.find("POSITION");
                if (attrIt == primitiveData.attributes.end())
                {
                    continue;
                }

                const tinygltf::Accessor &accessor = tgltfModel->accessors[attrIt->second];
                const tinygltf::BufferView &bufferView = tgltfModel->bufferViews[accessor.bufferView];
                const tinygltf::Buffer &buffer = tgltfModel->buffers[bufferView.buffer];

                size_t stride = accessor.ByteStride(bufferView);
                if (stride == 0)
                {
                    stride = 3 * sizeof(float);
                }

                const uint8_t *dataPtr = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
                for (size_t i = 0; i < accessor.count; ++i)
                {
                    const float *position = reinterpret_cast<const float *>(dataPtr + i * stride);
                    glm::vec3 pos(position[0], position[1], position[2]);
                    minBounds = glm::min(minBounds, pos);
                    maxBounds = glm::max(maxBounds, pos);
                    foundPositions = true;
                }
            }
        }
    }
    else
    {
        foundPositions = computeProceduralBounds(minBounds, maxBounds);
        if (!foundPositions)
        {
            std::cerr << "[Warning] scaleToUnitBox: Unable to compute bounds for procedural model " << name << std::endl;
            return;
        }
    }

    if (!foundPositions)
    {
        std::cerr << "[Warning] scaleToUnitBox: No position data found on model " << name << std::endl;
        return;
    }

    glm::vec3 extent = maxBounds - minBounds;
    float maxExtent = glm::compMax(extent);
    if (maxExtent <= 0.0f || !std::isfinite(maxExtent))
    {
        std::cerr << "[Warning] scaleToUnitBox: Invalid extent for model " << name << std::endl;
        return;
    }

    glm::vec3 center = (minBounds + maxBounds) * 0.5f;
    float scale = 1.0f / maxExtent;

    glm::mat4 translation = glm::translate(glm::mat4(1.0f), -center);
    glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), glm::vec3(scale));
    glm::mat4 transform = scaleMat * translation;
    normalizedBaseTransform = transform;
    worldTransform = transform;

    for (auto &mesh : meshes)
    {
        for (auto &primitive : mesh.primitives)
        {
            if (primitive)
            {
                primitive->transform = transform;
                if (primitive->ObjectTransformUBOMapped)
                {
                    ObjectTransform updated = primitive->buildObjectTransformData();
                    memcpy(primitive->ObjectTransformUBOMapped, &updated, sizeof(updated));
                }
            }
        }
    }

    recomputeBounds();
    std::cout << "[Debug] Model " << name << " scaled to unit box (scale=" << scale << ")" << std::endl;
}

void Model::resizeToUnitBox()
{
    scaleToUnitBox();
}

void Model::scale(const glm::vec3 &factors)
{
    applyTransformToPrimitives(glm::scale(glm::mat4(1.0f), factors));
}

void Model::translate(const glm::vec3 &offset)
{
    applyTransformToPrimitives(glm::translate(glm::mat4(1.0f), offset));
}

void Model::rotate(float angleRadians, const glm::vec3 &axis)
{
    glm::vec3 normAxis = glm::length(axis) == 0.0f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::normalize(axis);
    applyTransformToPrimitives(glm::rotate(glm::mat4(1.0f), angleRadians, normAxis));
}

void Model::rotate(float xDegrees, float yDegrees, float zDegrees)
{
    glm::mat4 rotationMat(1.0f);

    if (xDegrees != 0.0f)
    {
        rotationMat = glm::rotate(rotationMat, glm::radians(xDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
    }
    if (yDegrees != 0.0f)
    {
        rotationMat = glm::rotate(rotationMat, glm::radians(yDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    }
    if (zDegrees != 0.0f)
    {
        rotationMat = glm::rotate(rotationMat, glm::radians(zDegrees), glm::vec3(0.0f, 0.0f, 1.0f));
    }

    applyTransformToPrimitives(rotationMat);
}

void Model::setSceneTransform(const glm::vec3& translation, const glm::vec3& rotationDegrees, const glm::vec3& scaleFactors)
{
    glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), translation);
    modelMatrix = glm::rotate(modelMatrix, glm::radians(rotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
    modelMatrix = glm::rotate(modelMatrix, glm::radians(rotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
    modelMatrix = glm::rotate(modelMatrix, glm::radians(rotationDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
    modelMatrix = glm::scale(modelMatrix, scaleFactors);
    modelMatrix = modelMatrix * normalizedBaseTransform;

    worldTransform = modelMatrix;
    for (auto& mesh : meshes)
    {
        for (auto& primitive : mesh.primitives)
        {
            if (!primitive)
            {
                continue;
            }
            primitive->transform = modelMatrix;
            if (primitive->ObjectTransformUBOMapped)
            {
                ObjectTransform updated = primitive->buildObjectTransformData();
                memcpy(primitive->ObjectTransformUBOMapped, &updated, sizeof(updated));
            }
        }
    }
    recomputeBounds();
}

void Model::setPaintOverride(bool enabled, const glm::vec3& color)
{
    for (auto& mesh : meshes)
    {
        for (auto& primitive : mesh.primitives)
        {
            if (!primitive)
            {
                continue;
            }
            primitive->paintOverrideEnabled = enabled;
            primitive->paintOverrideColor = color;
            if (primitive->ObjectTransformUBOMapped)
            {
                ObjectTransform updated = primitive->buildObjectTransformData();
                memcpy(primitive->ObjectTransformUBOMapped, &updated, sizeof(updated));
            }
        }
    }
}

void Model::setAnimationPlaybackState(const std::string& clipName, bool playing, bool loop, float speed)
{
    if (fbxAnimationRuntime)
    {
        motive::animation::setFbxPlaybackState(*fbxAnimationRuntime, clipName, playing, loop, speed);
    }
}

void Model::setAnimationProcessingOptions(bool centroidNormalizationEnabled,
                                          float trimStartNormalized,
                                          float trimEndNormalized)
{
    if (fbxAnimationRuntime)
    {
        motive::animation::setFbxPlaybackOptions(*fbxAnimationRuntime,
                                                 centroidNormalizationEnabled,
                                                 trimStartNormalized,
                                                 trimEndNormalized);
    }
}

void Model::setCharacterAnimationNames(
    const std::string& idle,
    const std::string& comeToRest,
    const std::string& walkForward,
    const std::string& walkBackward,
    const std::string& walkLeft,
    const std::string& walkRight,
    const std::string& run,
    const std::string& jump)
{
    if (!idle.empty()) character.animIdle = idle;
    if (!comeToRest.empty()) character.animComeToRest = comeToRest;
    if (!walkForward.empty()) character.animWalkForward = walkForward;
    if (!walkBackward.empty()) character.animWalkBackward = walkBackward;
    if (!walkLeft.empty()) character.animWalkLeft = walkLeft;
    if (!walkRight.empty()) character.animWalkRight = walkRight;
    if (!run.empty()) character.animRun = run;
    if (!jump.empty()) character.animJump = jump;
}

void Model::updateAnimation(double deltaSeconds)
{
    if (!visible || !animated)
    {
        return;
    }
    
    // Handle procedural idle animation
    if (character.isControllable && character.isUsingProceduralAnim && 
        character.currentAnimState == CharacterController::AnimState::Idle)
    {
        applyProceduralIdleAnimation(deltaSeconds);
        return;
    }
    
    if (fbxAnimationRuntime && engine)
    {
        bool preprocessedUpdated = false;
        // Apply mirroring if needed (by scaling root transform X by -1)
        if (character.isControllable && character.isUsingMirroredAnim)
        {
            // Store current speed
            float originalSpeed = fbxAnimationRuntime->speed;
            
            // Update animation
            preprocessedUpdated = motive::animation::updateFbxAnimation(*this, *fbxAnimationRuntime, deltaSeconds);
            
            // Apply mirroring to root transform
            // This effectively mirrors the animation along the Z axis
            for (auto& mesh : meshes)
            {
                for (auto& primitive : mesh.primitives)
                {
                    if (primitive)
                    {
                        // Mirror the X component of translation to flip left/right
                        // This makes forward walk look like backward walk
                        glm::mat4& transform = primitive->transform;
                        transform[3][0] = -transform[3][0];  // Negate X translation
                        transform[3][2] = -transform[3][2];  // Negate Z translation (for backward)
                    }
                }
            }
            
            // Restore speed
            fbxAnimationRuntime->speed = originalSpeed;
        }
        else
        {
            // Normal animation update
            preprocessedUpdated = motive::animation::updateFbxAnimation(*this, *fbxAnimationRuntime, deltaSeconds);
        }

        if (preprocessedUpdated)
        {
            animationPreprocessedFrameValid = true;
            ++animationPreprocessedFrameCounter;
        }
    }
}

void Model::applyProceduralIdleAnimation(double deltaSeconds)
{
    // Generate procedural idle animation using sine waves for breathing/swaying
    static double timeAccumulator = 0.0;
    timeAccumulator += deltaSeconds;
    
    // Breathing - vertical chest movement
    float bobOffset = sin(timeAccumulator * character.idleBobFrequency * 2.0 * M_PI) * 
                      character.idleBobAmplitude;
    
    // Swaying - slight horizontal rotation
    float swayOffset = sin(timeAccumulator * character.idleSwayFrequency * 2.0 * M_PI) * 
                       character.idleSwayAmplitude;
    
    // Apply to all primitives (affects the whole character)
    for (auto& mesh : meshes)
    {
        for (auto& primitive : mesh.primitives)
        {
            if (primitive && primitive->ObjectTransformUBOMapped)
            {
                // Get current transform
                glm::mat4 baseTransform = primitive->transform;
                
                // Apply breathing (vertical offset)
                baseTransform[3][1] += bobOffset;
                
                // Apply swaying (subtle rotation around Y axis)
                glm::mat4 swayRotation = glm::rotate(glm::mat4(1.0f), swayOffset, glm::vec3(0.0f, 1.0f, 0.0f));
                baseTransform = swayRotation * baseTransform;
                
                // Update the UBO
                ObjectTransform ubo{};
                ubo.model = baseTransform;
                memcpy(primitive->ObjectTransformUBOMapped, &ubo, sizeof(ubo));
            }
        }
    }
}

void Model::applyTransformToPrimitives(const glm::mat4 &transform)
{
    worldTransform = transform * worldTransform;
    syncWorldTransformToPrimitives();
}

void Model::syncWorldTransformToPrimitives()
{
    for (auto &mesh : meshes)
    {
        for (auto &primitive : mesh.primitives)
        {
            if (!primitive)
            {
                continue;
            }

            primitive->transform = worldTransform;

            if (primitive->ObjectTransformUBOMapped)
            {
                ObjectTransform updated = primitive->buildObjectTransformData();
                memcpy(primitive->ObjectTransformUBOMapped, &updated, sizeof(updated));
            }
        }
    }
    recomputeBounds();
}

void Model::recomputeBounds()
{
    glm::vec3 localMin(std::numeric_limits<float>::max());
    glm::vec3 localMax(std::numeric_limits<float>::lowest());
    bool found = false;
    for (const auto &mesh : meshes)
    {
        for (const auto &primitive : mesh.primitives)
        {
            if (!primitive)
                continue;
            for (const auto &vertex : primitive->cpuVertices)
            {
                localMin = glm::min(localMin, vertex.pos);
                localMax = glm::max(localMax, vertex.pos);
                found = true;
            }
        }
    }
    if (!found)
    {
        boundsCenter = glm::vec3(0.0f);
        boundsRadius = 0.0f;
        boundsMinWorld = glm::vec3(0.0f);
        boundsMaxWorld = glm::vec3(0.0f);
        return;
    }
    glm::vec3 localCenter = (localMin + localMax) * 0.5f;
    float localRadius = glm::length(localMax - localMin) * 0.5f;
    if (!followAnchorLocalCenterInitialized)
    {
        followAnchorLocalCenter = localCenter;
        followAnchorLocalCenterInitialized = true;
    }

    boundsCenter = glm::vec3(worldTransform * glm::vec4(localCenter, 1.0f));

    glm::vec3 scale;
    scale.x = glm::length(glm::vec3(worldTransform[0]));
    scale.y = glm::length(glm::vec3(worldTransform[1]));
    scale.z = glm::length(glm::vec3(worldTransform[2]));
    boundsRadius = localRadius * glm::max(scale.x, glm::max(scale.y, scale.z));

    // Compute world-space AABB from transformed local AABB corners.
    const glm::vec3 corners[8] = {
        {localMin.x, localMin.y, localMin.z},
        {localMax.x, localMin.y, localMin.z},
        {localMin.x, localMax.y, localMin.z},
        {localMax.x, localMax.y, localMin.z},
        {localMin.x, localMin.y, localMax.z},
        {localMax.x, localMin.y, localMax.z},
        {localMin.x, localMax.y, localMax.z},
        {localMax.x, localMax.y, localMax.z}
    };
    glm::vec3 worldMin(std::numeric_limits<float>::max());
    glm::vec3 worldMax(std::numeric_limits<float>::lowest());
    for (const glm::vec3& corner : corners)
    {
        const glm::vec3 worldCorner = glm::vec3(worldTransform * glm::vec4(corner, 1.0f));
        worldMin = glm::min(worldMin, worldCorner);
        worldMax = glm::max(worldMax, worldCorner);
    }
    boundsMinWorld = worldMin;
    boundsMaxWorld = worldMax;
}

bool Model::computeProceduralBounds(glm::vec3 &minBounds, glm::vec3 &maxBounds) const
{
    bool found = false;
    for (const auto &mesh : meshes)
    {
        for (const auto &primitive : mesh.primitives)
        {
            if (!primitive)
            {
                continue;
            }

            for (const auto &vertex : primitive->cpuVertices)
            {
                minBounds = glm::min(minBounds, vertex.pos);
                maxBounds = glm::max(maxBounds, vertex.pos);
                found = true;
            }
        }
    }
    return found;
}


// ============================================================================
// Character Controller Implementation
// ============================================================================

void Model::setCharacterInput(const glm::vec3& moveDir)
{
    character.inputDir = moveDir;
}

glm::vec3 Model::getFollowAnchorPosition() const
{
    // Controllable characters: use stable world-space anchor derived from a
    // normalized local center (single-owner worldTransform). This avoids
    // per-frame animation AABB jitter and loop-end root snaps in follow camera.
    if (character.isControllable && followAnchorLocalCenterInitialized)
    {
        return glm::vec3(worldTransform * glm::vec4(followAnchorLocalCenter, 1.0f));
    }

    // Non-controllable assets keep animation-adjusted extents for framing.
    if (boundsRadius > 0.0f)
    {
        return boundsCenter;
    }
    return glm::vec3(worldTransform[3]);
}

void Model::updateCharacterPhysics(float deltaSeconds)
{
    if (!character.isControllable)
    {
        return;
    }
    
    // Clamp delta to prevent physics explosions on lag spikes
    const float dt = glm::min(deltaSeconds, 0.1f);
    
    // Debug: log input
    static int inputLogCounter = 0;
    if (++inputLogCounter % 60 == 0 && glm::length(character.inputDir) > 0.01f)
    {
        std::cout << "[CharacterPhysics] Input: " << character.inputDir.x << ", " 
                  << character.inputDir.y << ", " << character.inputDir.z << std::endl;
    }
    
    // Calculate target velocity from input
    glm::vec3 targetVelocity(0.0f);
    if (glm::length(character.inputDir) > 0.01f)
    {
        targetVelocity = glm::normalize(character.inputDir) * character.moveSpeed;
    }
    
    // Smoothly interpolate current velocity toward target (for arcade feel)
    const float accel = 10.0f;
    character.velocity.x = glm::mix(character.velocity.x, targetVelocity.x, glm::min(accel * dt, 1.0f));
    character.velocity.z = glm::mix(character.velocity.z, targetVelocity.z, glm::min(accel * dt, 1.0f));
    
    // Handle jump request
    if (character.jumpRequested && character.isGrounded)
    {
        character.velocity.y = character.jumpSpeed;
        character.isGrounded = false;
        character.jumpRequested = false;
        character.jumpPhase = CharacterController::JumpPhase::Start;
        character.jumpPhaseTimer = character.jumpStartMinDuration;
        character.currentAnimState = CharacterController::AnimState::Jump;
    }
    
    // Apply gravity
    character.velocity.y += character.gravity * dt;
    
    // Update position
    glm::vec3 currentPos = glm::vec3(worldTransform[3]);
    currentPos += character.velocity * dt;
    
    // Ground collision
    if (currentPos.y < character.groundHeight)
    {
        currentPos.y = character.groundHeight;
        character.velocity.y = 0.0f;
        character.isGrounded = true;
    }
    else
    {
        character.isGrounded = false;
    }

    auto normalizeAngle = [](float angle) -> float {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kTwoPi = 2.0f * kPi;
        while (angle > kPi) angle -= kTwoPi;
        while (angle < -kPi) angle += kTwoPi;
        return angle;
    };

    // Third-person facing: rotate character toward move direction with damping.
    // Only rotate when actually moving with meaningful velocity
    glm::vec3 facingDirection(0.0f);
    const glm::vec2 velocityPlanar(character.velocity.x, character.velocity.z);
    const float horizontalSpeed = glm::length(velocityPlanar);
    
    // Only face toward movement direction when actually moving
    if (horizontalSpeed > 0.1f)
    {
        facingDirection = glm::normalize(glm::vec3(character.velocity.x, 0.0f, character.velocity.z));
    }

    if (glm::length(facingDirection) > 0.001f)
    {
        glm::vec3 currentForward(worldTransform[2].x, 0.0f, worldTransform[2].z);
        if (glm::length(currentForward) < 0.001f)
        {
            currentForward = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            currentForward = glm::normalize(currentForward);
        }

        const float currentYaw = std::atan2(currentForward.x, currentForward.z);
        const float desiredYaw = std::atan2(facingDirection.x, facingDirection.z);
        const float yawDelta = normalizeAngle(desiredYaw - currentYaw);
        const float yawT = std::clamp(1.0f - std::exp(-character.turnResponsiveness * dt), 0.0f, 1.0f);
        const float newYaw = normalizeAngle(currentYaw + yawDelta * yawT);

        const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        const glm::vec3 forward(std::sin(newYaw), 0.0f, std::cos(newYaw));
        const glm::vec3 right = glm::normalize(glm::cross(worldUp, forward));
        const glm::vec3 up = glm::normalize(glm::cross(forward, right));

        const float scaleX = glm::max(glm::length(glm::vec3(worldTransform[0])), 0.0001f);
        const float scaleY = glm::max(glm::length(glm::vec3(worldTransform[1])), 0.0001f);
        const float scaleZ = glm::max(glm::length(glm::vec3(worldTransform[2])), 0.0001f);

        worldTransform[0] = glm::vec4(right * scaleX, 0.0f);
        worldTransform[1] = glm::vec4(up * scaleY, 0.0f);
        worldTransform[2] = glm::vec4(forward * scaleZ, 0.0f);
    }
    
    // Apply position to world transform
    worldTransform[3] = glm::vec4(currentPos, 1.0f);
    syncWorldTransformToPrimitives();
    
    // Update animation state based on directional keys and speed
    // Determine target animation state with directional awareness
    CharacterController::AnimState targetState;
    const float horizontalSpeedForAnim = glm::length(glm::vec2(character.velocity.x, character.velocity.z));
    
    const bool hasMoveKeyIntent =
        (character.keyW || character.keyA || character.keyS || character.keyD);
    const bool justReleasedMoveKeyIntent = !hasMoveKeyIntent && character.hadMoveKeyIntentLastFrame;
    character.hadMoveKeyIntentLastFrame = hasMoveKeyIntent;

    if (justReleasedMoveKeyIntent && character.enableRestPointOnMoveRelease)
    {
        character.pendingRestPointLatch = true;
    }
    if (hasMoveKeyIntent)
    {
        character.pendingRestPointLatch = false;
    }
    if (hasMoveKeyIntent)
    {
        character.moveIntentGraceTimer = character.moveIntentGraceDuration;
    }
    else
    {
        character.moveIntentGraceTimer = std::max(0.0f, character.moveIntentGraceTimer - dt);
    }
    const bool hasRecentMoveIntent = hasMoveKeyIntent || character.moveIntentGraceTimer > 0.0f;

    if (!character.isGrounded)
    {
        if (character.jumpPhase == CharacterController::JumpPhase::None ||
            character.jumpPhase == CharacterController::JumpPhase::Land)
        {
            character.jumpPhase = CharacterController::JumpPhase::Start;
            character.jumpPhaseTimer = character.jumpStartMinDuration;
        }

        if (character.jumpPhase == CharacterController::JumpPhase::Start)
        {
            character.jumpPhaseTimer = std::max(0.0f, character.jumpPhaseTimer - dt);
            if (character.jumpPhaseTimer <= 0.0f ||
                character.velocity.y <= character.jumpApexVelocityThreshold)
            {
                character.jumpPhase = CharacterController::JumpPhase::Apex;
            }
        }

        if ((character.jumpPhase == CharacterController::JumpPhase::Start ||
             character.jumpPhase == CharacterController::JumpPhase::Apex) &&
            character.velocity.y <= character.jumpFallVelocityThreshold)
        {
            character.jumpPhase = CharacterController::JumpPhase::Fall;
        }
    }
    else
    {
        if (!character.wasGroundedLastFrame &&
            character.jumpPhase != CharacterController::JumpPhase::None)
        {
            character.jumpPhase = CharacterController::JumpPhase::Land;
            character.jumpPhaseTimer = character.jumpLandMinDuration;
        }
        else if (character.jumpPhase == CharacterController::JumpPhase::Land)
        {
            character.jumpPhaseTimer = std::max(0.0f, character.jumpPhaseTimer - dt);
            if (character.jumpPhaseTimer <= 0.0f)
            {
                character.jumpPhase = CharacterController::JumpPhase::None;
            }
        }
        else if (character.jumpPhase != CharacterController::JumpPhase::None &&
                 character.jumpPhase != CharacterController::JumpPhase::Land)
        {
            character.jumpPhase = CharacterController::JumpPhase::None;
        }
    }
    character.wasGroundedLastFrame = character.isGrounded;

    const auto isLocomotionState = [](CharacterController::AnimState state) -> bool {
        return state == CharacterController::AnimState::WalkForward ||
               state == CharacterController::AnimState::WalkBackward ||
               state == CharacterController::AnimState::WalkLeft ||
               state == CharacterController::AnimState::WalkRight ||
               state == CharacterController::AnimState::Run;
    };
    const bool wasLocomotion = isLocomotionState(character.currentAnimState);

    // Jump animation takes priority while in any jump phase.
    if (character.jumpPhase != CharacterController::JumpPhase::None)
    {
        character.comeToRestTimer = 0.0f;
        character.pendingRestPointLatch = false;
        targetState = CharacterController::AnimState::Jump;
    }
    else if (!hasMoveKeyIntent)
    {
        // Trigger a short come-to-rest phase whenever locomotion input is released.
        if (character.comeToRestTimer <= 0.0f && (wasLocomotion || hasRecentMoveIntent))
        {
            character.comeToRestTimer = character.comeToRestDuration;
        }

        if (character.comeToRestTimer > 0.0f)
        {
            character.comeToRestTimer = std::max(0.0f, character.comeToRestTimer - dt);
            targetState = CharacterController::AnimState::ComeToRest;
        }
        else
        {
            // Strict rule: no locomotion animation when no key is held.
            targetState = CharacterController::AnimState::Idle;
        }
    }
    else if (horizontalSpeedForAnim >= character.runSpeedThreshold)
    {
        character.comeToRestTimer = 0.0f;
        targetState = CharacterController::AnimState::Run;
    }
    else
    {
        character.comeToRestTimer = 0.0f;
        // Select directional walk animation based on which keys are pressed.
        if (character.keyW && !character.keyS)
            targetState = CharacterController::AnimState::WalkForward;
        else if (character.keyS && !character.keyW)
            targetState = CharacterController::AnimState::WalkBackward;
        else if (character.keyA && !character.keyD)
            targetState = CharacterController::AnimState::WalkLeft;
        else if (character.keyD && !character.keyA)
            targetState = CharacterController::AnimState::WalkRight;
        else
            targetState = CharacterController::AnimState::WalkForward;
    }
    
    // Smooth animation weight transition
    float targetWeight = 0.0f;
    switch (targetState)
    {
        case CharacterController::AnimState::Idle:
            targetWeight = 0.0f;
            break;
        case CharacterController::AnimState::ComeToRest:
            targetWeight = 0.2f;
            break;
        case CharacterController::AnimState::WalkForward:
        case CharacterController::AnimState::WalkBackward:
        case CharacterController::AnimState::WalkLeft:
        case CharacterController::AnimState::WalkRight:
            targetWeight = 0.5f;
            break;
        case CharacterController::AnimState::Run:
            targetWeight = 1.0f;
            break;
        case CharacterController::AnimState::Jump:
            targetWeight = 0.5f;
            break;
    }
    
    character.currentAnimWeight = glm::mix(character.currentAnimWeight, targetWeight,
                                           glm::min(character.animBlendSpeed * dt, 1.0f));
    character.previousAnimState = character.currentAnimState;
    character.currentAnimState = targetState;
    
    // Auto-select animation clips based on state
    // Note: This assumes clips are named "idle", "walk", "run" or similar
    if (animated && !animationClips.empty())
    {
        std::string targetClip;
        bool targetLoop = true;
        bool supportsReleaseRestLatch = false;
        
        // Helper to find clip by substring
        auto findClip = [&](const std::vector<std::string>& searchTerms) -> std::string {
            for (const auto& term : searchTerms)
            {
                if (term.empty()) continue;
                for (size_t i = 0; i < animationClips.size(); ++i)
                {
                    const auto& name = animationClips[i].name;
                    if (name.find(term) != std::string::npos)
                        return name;
                }
            }
            return "";
        };
        
        switch (targetState)
        {
            case CharacterController::AnimState::Idle:
                // Standard: idle, idle_standing, idle_loop
                targetClip = findClip({
                    character.animIdle,
                    "idle_loop", "idle_standing", "idle_stand",
                    "Idle", "IDLE", "idle"
                });
                break;

            case CharacterController::AnimState::ComeToRest:
                targetClip = findClip({
                    character.animComeToRest,
                    "run_to_idle", "walk_to_idle", "stop", "brake", "halt",
                    "decelerate", "land_to_idle",
                    "Stop", "STOP"
                });
                if (targetClip.empty())
                {
                    targetClip = findClip({
                        character.animIdle,
                        "idle_loop", "idle_standing", "idle_stand",
                        "Idle", "IDLE", "idle"
                    });
                }
                if (!targetClip.empty())
                {
                    const bool isIdleFallback =
                        targetClip.find("idle") != std::string::npos ||
                        targetClip.find("Idle") != std::string::npos ||
                        targetClip.find("IDLE") != std::string::npos;
                    targetLoop = isIdleFallback;
                    supportsReleaseRestLatch = !isIdleFallback;
                }
                break;
                
            case CharacterController::AnimState::WalkForward:
                // Standard: walk_forward, walk_fwd, walking, walk
                targetClip = findClip({
                    character.animWalkForward,
                    "walk_fwd", "walk_forward", "walk_loop",
                    "Walking", "WALKING", "walking", "Walk", "walk"
                });
                break;
                
            case CharacterController::AnimState::WalkBackward:
                // Standard: walk_backward, walk_back, walk_bwd
                targetClip = findClip({
                    character.animWalkBackward,
                    "walk_bwd", "walk_back", "walk_backward",
                    "backward", "Backward", "BACKWARD"
                });
                // Fallback to forward walk (will be played in reverse if supported)
                if (targetClip.empty())
                    targetClip = findClip({
                        character.animWalkForward,
                        "walk_fwd", "walk_forward",
                        "Walking", "walking", "Walk", "walk"
                    });
                break;
                
            case CharacterController::AnimState::WalkLeft:
                // Standard: walk_left, strafe_left
                targetClip = findClip({
                    character.animWalkLeft,
                    "strafe_left", "walk_left", "walk_l",
                    "left", "Left", "LEFT"
                });
                // Fallback to forward walk
                if (targetClip.empty())
                    targetClip = findClip({
                        character.animWalkForward,
                        "walk_fwd", "walk_forward",
                        "Walking", "walking", "Walk", "walk"
                    });
                break;
                
            case CharacterController::AnimState::WalkRight:
                // Standard: walk_right, strafe_right
                targetClip = findClip({
                    character.animWalkRight,
                    "strafe_right", "walk_right", "walk_r",
                    "right", "Right", "RIGHT"
                });
                // Fallback to forward walk
                if (targetClip.empty())
                    targetClip = findClip({
                        character.animWalkForward,
                        "walk_fwd", "walk_forward",
                        "Walking", "walking", "Walk", "walk"
                    });
                break;
                
            case CharacterController::AnimState::Run:
                // Standard: run, run_forward, run_fwd, sprint
                targetClip = findClip({
                    character.animRun,
                    "run_forward", "run_fwd", "run_loop", "run",
                    "sprint", "Sprint", "SPRINT",
                    "Running", "RUNNING", "running", "Run", "RUN"
                });
                // Fallback to walk if no run animation
                if (targetClip.empty())
                    targetClip = findClip({
                        character.animWalkForward,
                        "walk_fwd", "walk_forward",
                        "Walking", "walking", "Walk", "walk"
                    });
                break;
                
            case CharacterController::AnimState::Jump:
                if (character.jumpPhase == CharacterController::JumpPhase::Start)
                {
                    targetClip = findClip({
                        character.animJump,
                        "jump_start", "jump_takeoff", "takeoff", "jump_up"
                    });
                    targetLoop = false;
                }
                else if (character.jumpPhase == CharacterController::JumpPhase::Land)
                {
                    targetClip = findClip({
                        character.animLand,
                        "jump_land", "landing", "land", "land_to_idle"
                    });
                    targetLoop = false;
                }
                else if (character.jumpPhase == CharacterController::JumpPhase::Fall)
                {
                    targetClip = findClip({
                        character.animFall,
                        "jump_fall", "fall_loop", "falling", "fall"
                    });
                    targetLoop = true;
                }
                else
                {
                    targetClip = findClip({
                        "jump_apex", "jump_loop", character.animJump, "jump",
                        "Jumping", "JUMPING", "jumping", "Jump", "JUMP"
                    });
                    targetLoop = true;
                }
                // Fallback to idle
                if (targetClip.empty())
                    targetClip = findClip({
                        character.animIdle,
                        "idle_loop", "idle", "Idle"
                    });
                break;
        }
        
        // Final fallback to first clip only for locomotion states.
        // For Idle, do not fall back to arbitrary clips (often walk cycles).
        if (targetClip.empty() &&
            targetState != CharacterController::AnimState::Idle &&
            targetState != CharacterController::AnimState::ComeToRest &&
            !animationClips.empty())
        {
            targetClip = animationClips[0].name;
        }
        
        // Determine animation speed based on state (time-warping)
        float targetSpeed = 1.0f;
        bool useMirroring = false;
        bool useProcedural = false;
        
        switch (targetState)
        {
            case CharacterController::AnimState::Idle:
                targetSpeed = character.enableTimeWarp ? character.idleAnimSpeed : 1.0f;
                // Check if we have an idle clip, otherwise use procedural
                if (targetClip.empty() && character.enableProceduralIdle)
                {
                    useProcedural = true;
                }
                break;

            case CharacterController::AnimState::ComeToRest:
                targetSpeed = character.enableTimeWarp ? character.walkAnimSpeed : 1.0f;
                break;
                
            case CharacterController::AnimState::WalkForward:
                targetSpeed = character.enableTimeWarp ? character.walkAnimSpeed : 1.0f;
                break;
                
            case CharacterController::AnimState::WalkBackward:
                targetSpeed = character.enableTimeWarp ? character.backwardAnimSpeed : 1.0f;
                // If we're using forward walk for backward, enable mirroring
                if (character.enableAnimationMirroring && 
                    targetClip.find("forward") != std::string::npos)
                {
                    useMirroring = true;
                }
                break;
                
            case CharacterController::AnimState::WalkLeft:
            case CharacterController::AnimState::WalkRight:
                targetSpeed = character.enableTimeWarp ? character.walkAnimSpeed : 1.0f;
                // Enable mirroring if we're using forward walk for strafing
                if (character.enableAnimationMirroring && 
                    targetClip.find("forward") != std::string::npos)
                {
                    useMirroring = true;
                }
                break;
                
            case CharacterController::AnimState::Run:
                targetSpeed = character.enableTimeWarp ? character.runAnimSpeed : 1.0f;
                break;
                
            case CharacterController::AnimState::Jump:
                targetSpeed = character.enableTimeWarp ? character.jumpAnimSpeed : 1.0f;
                break;
        }
        
        // Update animation playback state
        character.currentAnimSpeed = targetSpeed;
        character.isUsingMirroredAnim = useMirroring;
        character.isUsingProceduralAnim = useProcedural;

        if (character.pendingRestPointLatch &&
            (targetState != CharacterController::AnimState::ComeToRest || !supportsReleaseRestLatch))
        {
            character.pendingRestPointLatch = false;
        }

        if (character.pendingRestPointLatch &&
            targetState == CharacterController::AnimState::ComeToRest &&
            supportsReleaseRestLatch)
        {
            targetLoop = false;
        }
        
        // Switch animation if needed (only on state change)
        if (!targetClip.empty() && fbxAnimationRuntime)
        {
            // Check if we need to switch
            bool needSwitch = false;
            if (fbxAnimationRuntime->activeClipIndex >= 0 && 
                fbxAnimationRuntime->activeClipIndex < static_cast<int>(fbxAnimationRuntime->clips.size()))
            {
                const auto& currentClip = fbxAnimationRuntime->clips[fbxAnimationRuntime->activeClipIndex];
                if (currentClip.name != targetClip)
                {
                    needSwitch = true;
                }
            }
            else
            {
                needSwitch = true;  // No active clip yet
            }
            
            if (needSwitch)
            {
                setAnimationPlaybackState(targetClip, true, targetLoop, targetSpeed);
            }
            else
            {
                // Same clip: keep runtime explicitly in character-mode playback settings.
                fbxAnimationRuntime->playing = true;
                fbxAnimationRuntime->loop = targetLoop;
                fbxAnimationRuntime->speed = targetSpeed;
            }
        }
        else if (useProcedural)
        {
            // Procedural idle - handled in updateAnimation
            fbxAnimationRuntime->playing = false;
        }
        else if (targetState == CharacterController::AnimState::Idle && fbxAnimationRuntime)
        {
            // No valid idle clip: hold current pose instead of playing locomotion clips.
            fbxAnimationRuntime->playing = false;
        }

        if (character.pendingRestPointLatch &&
            supportsReleaseRestLatch &&
            targetState == CharacterController::AnimState::ComeToRest &&
            fbxAnimationRuntime)
        {
            const int activeClipIndex = fbxAnimationRuntime->activeClipIndex;
            if (activeClipIndex >= 0 &&
                activeClipIndex < static_cast<int>(fbxAnimationRuntime->clips.size()))
            {
                const auto& activeClip = fbxAnimationRuntime->clips[activeClipIndex];
                if (activeClip.name == targetClip)
                {
                    const double clipDuration = std::max(activeClip.timeEnd - activeClip.timeBegin, 0.0);
                    if (clipDuration > 0.0)
                    {
                        const double restNormalized = std::clamp(
                            static_cast<double>(character.restPointNormalizedOnMoveRelease), 0.0, 1.0);
                        const double restTime = activeClip.timeBegin + (restNormalized * clipDuration);
                        if (fbxAnimationRuntime->timeSeconds >= restTime - 1e-6)
                        {
                            fbxAnimationRuntime->timeSeconds = restTime;
                            fbxAnimationRuntime->playing = false;
                            fbxAnimationRuntime->loop = false;
                            character.pendingRestPointLatch = false;
                            character.comeToRestTimer = 0.0f;
                        }
                    }
                    else
                    {
                        character.pendingRestPointLatch = false;
                    }
                }
            }
        }
    }
}
