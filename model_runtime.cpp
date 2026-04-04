#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include "model.h"
#include "engine.h"

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

void Model::setCharacterAnimationNames(
    const std::string& idle,
    const std::string& walkForward,
    const std::string& walkBackward,
    const std::string& walkLeft,
    const std::string& walkRight,
    const std::string& run,
    const std::string& jump)
{
    if (!idle.empty()) character.animIdle = idle;
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
        // Apply mirroring if needed (by scaling root transform X by -1)
        if (character.isControllable && character.isUsingMirroredAnim)
        {
            // Store current speed
            float originalSpeed = fbxAnimationRuntime->speed;
            
            // Update animation
            motive::animation::updateFbxAnimation(*this, *fbxAnimationRuntime, deltaSeconds);
            
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
            motive::animation::updateFbxAnimation(*this, *fbxAnimationRuntime, deltaSeconds);
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
    for (auto &mesh : meshes)
    {
        for (auto &primitive : mesh.primitives)
        {
            if (!primitive)
            {
                continue;
            }

            primitive->transform = transform * primitive->transform;

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
        return;
    }
    glm::vec3 localCenter = (localMin + localMax) * 0.5f;
    float localRadius = glm::length(localMax - localMin) * 0.5f;

    boundsCenter = glm::vec3(worldTransform * glm::vec4(localCenter, 1.0f));

    glm::vec3 scale;
    scale.x = glm::length(glm::vec3(worldTransform[0]));
    scale.y = glm::length(glm::vec3(worldTransform[1]));
    scale.z = glm::length(glm::vec3(worldTransform[2]));
    boundsRadius = localRadius * glm::max(scale.x, glm::max(scale.y, scale.z));
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
    
    // Apply position to world transform
    worldTransform[3] = glm::vec4(currentPos, 1.0f);
    applyTransformToPrimitives(worldTransform);
    
    // Update animation state based on directional keys and speed
    const float horizontalSpeed = glm::length(glm::vec2(character.velocity.x, character.velocity.z));
    
    // Determine target animation state with directional awareness
    CharacterController::AnimState targetState;
    
    // Jump animation takes priority when not grounded
    if (!character.isGrounded)
    {
        targetState = CharacterController::AnimState::Jump;
    }
    else if (horizontalSpeed < character.walkSpeedThreshold)
    {
        targetState = CharacterController::AnimState::Idle;
    }
    else if (horizontalSpeed < character.runSpeedThreshold)
    {
        // Select directional walk animation based on which keys are pressed
        if (character.keyW && !character.keyS)
            targetState = CharacterController::AnimState::WalkForward;
        else if (character.keyS && !character.keyW)
            targetState = CharacterController::AnimState::WalkBackward;
        else if (character.keyA && !character.keyD)
            targetState = CharacterController::AnimState::WalkLeft;
        else if (character.keyD && !character.keyA)
            targetState = CharacterController::AnimState::WalkRight;
        else
            targetState = CharacterController::AnimState::WalkForward;  // Default
    }
    else
    {
        targetState = CharacterController::AnimState::Run;
    }
    
    // Smooth animation weight transition
    float targetWeight = 0.0f;
    switch (targetState)
    {
        case CharacterController::AnimState::Idle:
            targetWeight = 0.0f;
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
    character.currentAnimState = targetState;
    
    // Auto-select animation clips based on state
    // Note: This assumes clips are named "idle", "walk", "run" or similar
    if (animated && !animationClips.empty())
    {
        std::string targetClip;
        
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
                // Standard: jump, jump_up, jump_start, jumping
                targetClip = findClip({
                    character.animJump,
                    "jump_start", "jump_up", "jump_loop", "jump",
                    "Jumping", "JUMPING", "jumping", "Jump", "JUMP"
                });
                // Fallback to idle
                if (targetClip.empty())
                    targetClip = findClip({
                        character.animIdle,
                        "idle_loop", "idle", "Idle"
                    });
                break;
        }
        
        // Final fallback to first clip if nothing found
        if (targetClip.empty() && !animationClips.empty())
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
                setAnimationPlaybackState(targetClip, true, true, targetSpeed);
            }
            else
            {
                // Just update speed if same clip
                fbxAnimationRuntime->speed = targetSpeed;
            }
        }
        else if (useProcedural)
        {
            // Procedural idle - handled in updateAnimation
            fbxAnimationRuntime->playing = false;
        }
    }
}
