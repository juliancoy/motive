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

namespace
{
bool isLocomotionState(Model::CharacterController::AnimState state)
{
    using AnimState = Model::CharacterController::AnimState;
    return state == AnimState::WalkForward ||
           state == AnimState::WalkBackward ||
           state == AnimState::WalkLeft ||
           state == AnimState::WalkRight ||
           state == AnimState::Run;
}

glm::vec2 safeNormalize2(const glm::vec2& value)
{
    const float len = glm::length(value);
    if (len <= 1e-5f)
    {
        return glm::vec2(0.0f);
    }
    return value / len;
}

Model::CharacterController::AnimState locomotionStateFromIntent(const glm::vec2& localIntent,
                                                                Model::CharacterController::AnimState previousState,
                                                                float bias)
{
    using AnimState = Model::CharacterController::AnimState;

    const float forwardScore = std::max(0.0f, localIntent.y);
    const float backwardScore = std::max(0.0f, -localIntent.y);
    const float leftScore = std::max(0.0f, -localIntent.x);
    const float rightScore = std::max(0.0f, localIntent.x);

    AnimState bestState = AnimState::WalkForward;
    float bestScore = forwardScore;
    if (backwardScore > bestScore)
    {
        bestScore = backwardScore;
        bestState = AnimState::WalkBackward;
    }
    if (leftScore > bestScore)
    {
        bestScore = leftScore;
        bestState = AnimState::WalkLeft;
    }
    if (rightScore > bestScore)
    {
        bestScore = rightScore;
        bestState = AnimState::WalkRight;
    }

    if (isLocomotionState(previousState))
    {
        float previousScore = 0.0f;
        switch (previousState)
        {
            case AnimState::WalkForward:
            case AnimState::Run:
                previousScore = forwardScore;
                break;
            case AnimState::WalkBackward:
                previousScore = backwardScore;
                break;
            case AnimState::WalkLeft:
                previousScore = leftScore;
                break;
            case AnimState::WalkRight:
                previousScore = rightScore;
                break;
            default:
                break;
        }

        if (previousScore + bias >= bestScore)
        {
            return previousState == AnimState::Run ? AnimState::WalkForward : previousState;
        }
    }

    return bestState;
}
}

void Model::scaleToUnitBox()
{
    glm::vec3 minBounds(std::numeric_limits<float>::max());
    glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
    const bool foundPositions = computeProceduralBounds(minBounds, maxBounds);

    if (!foundPositions)
    {
        std::cerr << "[Warning] scaleToUnitBox: Unable to compute bounds for model " << name << std::endl;
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
    updateWorldBoundsFromLocalBounds();
}

void Model::setWorldTransform(const glm::mat4& transform)
{
    worldTransform = transform;
    syncWorldTransformToPrimitives();
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

void Model::setInverseColorEnabled(bool enabled)
{
    for (auto& mesh : meshes)
    {
        for (auto& primitive : mesh.primitives)
        {
            if (!primitive)
            {
                continue;
            }
            primitive->invertColorEnabled = enabled;
        }
    }
}

void Model::setAnimationPlaybackState(const std::string& clipName,
                                      bool playing,
                                      bool loop,
                                      float speed,
                                      bool preserveNormalizedTime)
{
    animationRuntimeState.source = AnimationRuntimeState::Source::Manual;
    animationRuntimeState.resolvedClipName = clipName;
    animationRuntimeState.resolvedPlaying = playing;
    animationRuntimeState.resolvedLoop = loop;
    animationRuntimeState.resolvedSpeed = speed;
    animationRuntimeState.resolvedProcedural = false;
    animationRuntimeState.resolvedMirrored = false;

    if (fbxAnimationRuntime)
    {
        motive::animation::setFbxPlaybackState(*fbxAnimationRuntime,
                                               clipName,
                                               playing,
                                               loop,
                                               speed,
                                               preserveNormalizedTime);
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

void Model::updateCharacterAnimationSemanticState(float dt)
{
    CharacterController::AnimState targetState;
    const float horizontalSpeedForAnim = glm::length(glm::vec2(character.velocity.x, character.velocity.z));
    character.locomotionStateTimer = std::max(0.0f, character.locomotionStateTimer - dt);
    constexpr float kAirborneAnimationGrace = 0.12f;
    constexpr float kAirborneAnimationVelocityThreshold = 1.25f;

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
        character.moveIntentGraceTimer = character.moveIntentGraceDuration;
    }
    else
    {
        character.moveIntentGraceTimer = std::max(0.0f, character.moveIntentGraceTimer - dt);
    }
    const bool hasRecentMoveIntent = hasMoveKeyIntent || character.moveIntentGraceTimer > 0.0f;

    const bool shouldUseAirborneAnimation =
        character.jumpStartedFromInput ||
        (!character.isGrounded &&
         (character.airborneTimer >= kAirborneAnimationGrace ||
          std::abs(character.velocity.y) >= kAirborneAnimationVelocityThreshold));

    character.attackCooldownTimer = std::max(0.0f, character.attackCooldownTimer - dt);
    if (character.attackActive)
    {
        character.attackActiveTimer = std::max(0.0f, character.attackActiveTimer - dt);
        if (character.attackActiveTimer <= 0.0f)
        {
            character.attackActive = false;
            character.attackCooldownTimer = std::max(character.attackCooldownTimer,
                                                     character.attackCooldownDuration);
        }
    }

    if (character.attackRequested &&
        character.isAiDriven &&
        character.isGrounded &&
        !shouldUseAirborneAnimation &&
        !character.attackActive &&
        character.attackCooldownTimer <= 0.0f)
    {
        character.attackActive = true;
        character.attackActiveTimer = std::max(0.1f, character.attackDuration);
        character.attackCooldownTimer = std::max(character.attackCooldownTimer,
                                                 character.attackDuration);
        character.comeToRestTimer = 0.0f;
        character.pendingRestPointLatch = false;
    }

    if (shouldUseAirborneAnimation && !character.isGrounded)
    {
        if (character.jumpPhase == CharacterController::JumpPhase::None ||
            character.jumpPhase == CharacterController::JumpPhase::Land)
        {
            if (character.jumpStartedFromInput)
            {
                character.jumpPhase = CharacterController::JumpPhase::Start;
                character.jumpPhaseTimer = character.jumpStartMinDuration;
            }
            else if (character.velocity.y > character.jumpApexVelocityThreshold)
            {
                character.jumpPhase = CharacterController::JumpPhase::Apex;
                character.jumpPhaseTimer = 0.0f;
            }
            else
            {
                character.jumpPhase = CharacterController::JumpPhase::Fall;
                character.jumpPhaseTimer = 0.0f;
            }
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
        if (!shouldUseAirborneAnimation)
        {
            character.jumpPhase = CharacterController::JumpPhase::None;
            character.jumpPhaseTimer = 0.0f;
        }
        else if (!character.wasGroundedLastFrame &&
            character.jumpPhase != CharacterController::JumpPhase::None)
        {
            character.jumpPhase = CharacterController::JumpPhase::Land;
            const float impactSpeed = std::max(0.0f, -character.lastAirborneVerticalVelocity);
            const float landingDuration = character.jumpLandMinDuration +
                                          std::min(0.14f, impactSpeed * 0.02f);
            character.jumpPhaseTimer = landingDuration;
            character.jumpStartedFromInput = false;
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

        if (character.jumpPhase == CharacterController::JumpPhase::None)
        {
            character.jumpStartedFromInput = false;
        }
    }
    character.wasGroundedLastFrame = character.isGrounded;

    const bool wasLocomotion = isLocomotionState(character.currentAnimState);

    glm::vec2 localIntent(0.0f);
    if (hasMoveKeyIntent)
    {
        if (character.keyD) localIntent.x += 1.0f;
        if (character.keyA) localIntent.x -= 1.0f;
        if (character.keyW) localIntent.y += 1.0f;
        if (character.keyS) localIntent.y -= 1.0f;
        localIntent = safeNormalize2(localIntent);
    }
    else
    {
        const glm::vec3 planarInput(character.inputDir.x, 0.0f, character.inputDir.z);
        if (glm::length(planarInput) > 1e-4f)
        {
            glm::vec3 currentForward(worldTransform[2].x, 0.0f, worldTransform[2].z);
            if (glm::length(currentForward) < 1e-4f)
            {
                currentForward = glm::vec3(0.0f, 0.0f, 1.0f);
            }
            else
            {
                currentForward = glm::normalize(currentForward);
            }
            glm::vec3 currentRight = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), currentForward);
            if (glm::length(currentRight) > 1e-4f)
            {
                currentRight = glm::normalize(currentRight);
                const glm::vec3 normalizedInput = glm::normalize(planarInput);
                localIntent.x = glm::dot(normalizedInput, currentRight);
                localIntent.y = glm::dot(normalizedInput, currentForward);
            }
        }
        else if (horizontalSpeedForAnim > 1e-4f)
        {
            glm::vec3 currentForward(worldTransform[2].x, 0.0f, worldTransform[2].z);
            if (glm::length(currentForward) < 1e-4f)
            {
                currentForward = glm::vec3(0.0f, 0.0f, 1.0f);
            }
            else
            {
                currentForward = glm::normalize(currentForward);
            }
            glm::vec3 currentRight = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), currentForward);
            if (glm::length(currentRight) > 1e-4f)
            {
                currentRight = glm::normalize(currentRight);
                const glm::vec3 normalizedVelocity = glm::normalize(glm::vec3(character.velocity.x, 0.0f, character.velocity.z));
                localIntent.x = glm::dot(normalizedVelocity, currentRight);
                localIntent.y = glm::dot(normalizedVelocity, currentForward);
            }
        }
    }

    const float intentBlendT = std::clamp(
        1.0f - std::exp(-character.locomotionIntentSmoothing * std::max(dt, 0.0f)),
        0.0f,
        1.0f);
    character.smoothedLocalMoveIntent = glm::mix(character.smoothedLocalMoveIntent, localIntent, intentBlendT);
    if (!hasRecentMoveIntent && horizontalSpeedForAnim <= character.locomotionStopSpeedThreshold)
    {
        character.smoothedLocalMoveIntent = glm::mix(character.smoothedLocalMoveIntent,
                                                     glm::vec2(0.0f),
                                                     std::min(1.0f, intentBlendT * 1.35f));
    }

    if (character.attackActive && character.isAiDriven)
    {
        character.comeToRestTimer = 0.0f;
        character.pendingRestPointLatch = false;
        targetState = CharacterController::AnimState::Attack;
    }
    else if (shouldUseAirborneAnimation &&
        character.jumpPhase != CharacterController::JumpPhase::None)
    {
        character.comeToRestTimer = 0.0f;
        character.pendingRestPointLatch = false;
        targetState = CharacterController::AnimState::Jump;
    }
    else
    {
        const bool locomotionShouldContinue =
            hasRecentMoveIntent ||
            horizontalSpeedForAnim >= (wasLocomotion
                ? character.locomotionStopSpeedThreshold
                : character.locomotionStartSpeedThreshold);

        if (!locomotionShouldContinue)
        {
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
                targetState = CharacterController::AnimState::Idle;
            }
        }
        else
        {
            character.comeToRestTimer = 0.0f;
            CharacterController::AnimState locomotionState = locomotionStateFromIntent(
                safeNormalize2(character.smoothedLocalMoveIntent),
                character.currentAnimState,
                character.locomotionDirectionSwitchBias);

            if ((character.keyShift || horizontalSpeedForAnim >= character.runSpeedThreshold) &&
                locomotionState == CharacterController::AnimState::WalkForward)
            {
                locomotionState = CharacterController::AnimState::Run;
            }

            if (isLocomotionState(character.currentAnimState) &&
                locomotionState != character.currentAnimState &&
                character.locomotionStateTimer > 0.0f)
            {
                locomotionState = character.currentAnimState;
            }

            targetState = locomotionState;
            if (targetState != character.currentAnimState)
            {
                character.locomotionStateTimer = character.locomotionStateMinDuration;
            }
        }
    }

    character.previousAnimState = character.currentAnimState;
    character.currentAnimState = targetState;
    animationRuntimeState.source = AnimationRuntimeState::Source::CharacterController;
    animationRuntimeState.semanticState = targetState;
    animationRuntimeState.semanticJumpPhase = character.jumpPhase;
}

void Model::updateAnimation(double deltaSeconds)
{
    if (!visible || !animated)
    {
        return;
    }

    if (isCharacterRuntimeDriven())
    {
        updateCharacterAnimationSemanticState(static_cast<float>(std::max(deltaSeconds, 0.0)));
        animationRuntimeState.source = AnimationRuntimeState::Source::CharacterController;
        animationRuntimeState.semanticState = character.currentAnimState;
        animationRuntimeState.semanticJumpPhase = character.jumpPhase;
    }
    else
    {
        animationRuntimeState.source = AnimationRuntimeState::Source::Manual;
    }
    
    // Handle procedural idle animation
    if (isCharacterRuntimeDriven() && character.isUsingProceduralAnim &&
        character.currentAnimState == CharacterController::AnimState::Idle)
    {
        applyProceduralIdleAnimation(deltaSeconds);
        return;
    }

    if (isCharacterRuntimeDriven() &&
        character.isUsingProceduralAnim &&
        character.currentAnimState == CharacterController::AnimState::Jump)
    {
        applyProceduralJumpAnimation(deltaSeconds);
        return;
    }

    if (isCharacterRuntimeDriven() &&
        character.isUsingProceduralAnim &&
        character.currentAnimState == CharacterController::AnimState::Attack)
    {
        applyProceduralAttackAnimation(deltaSeconds);
        return;
    }
    
    if (fbxAnimationRuntime && engine)
    {
        bool preprocessedUpdated = false;
        // Apply mirroring if needed (by scaling root transform X by -1)
        if (isCharacterRuntimeDriven() && character.isUsingMirroredAnim)
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
    character.proceduralIdleTime += static_cast<float>(deltaSeconds);

    if (!proceduralIdleBaseVerticesValid)
    {
        proceduralIdleBaseVertices.clear();
        proceduralIdleBaseVertices.reserve(meshes.size());
        for (const auto& mesh : meshes)
        {
            const Primitive* primitive = !mesh.primitives.empty() ? mesh.primitives.front().get() : nullptr;
            proceduralIdleBaseVertices.push_back(primitive ? primitive->cpuVertices : std::vector<Vertex>{});
        }
        proceduralIdleBaseVerticesValid = true;
    }

    glm::vec3 localMin(std::numeric_limits<float>::max());
    glm::vec3 localMax(std::numeric_limits<float>::lowest());
    bool found = false;
    for (const auto& vertices : proceduralIdleBaseVertices)
    {
        for (const Vertex& vertex : vertices)
        {
            localMin = glm::min(localMin, vertex.pos);
            localMax = glm::max(localMax, vertex.pos);
            found = true;
        }
    }
    if (!found)
    {
        return;
    }

    const glm::vec3 size = glm::max(localMax - localMin, glm::vec3(0.0001f));
    const glm::vec3 center = (localMin + localMax) * 0.5f;
    const float breathe = std::sin(character.proceduralIdleTime * character.idleBobFrequency * 2.0f * M_PI);
    const float sway = std::sin(character.proceduralIdleTime * character.idleSwayFrequency * 2.0f * M_PI);

    for (size_t meshIndex = 0; meshIndex < meshes.size() && meshIndex < proceduralIdleBaseVertices.size(); ++meshIndex)
    {
        if (meshes[meshIndex].primitives.empty() || !meshes[meshIndex].primitives.front())
        {
            continue;
        }

        std::vector<Vertex> posed = proceduralIdleBaseVertices[meshIndex];
        for (Vertex& vertex : posed)
        {
            const float y01 = std::clamp((vertex.pos.y - localMin.y) / size.y, 0.0f, 1.0f);
            const float xCentered = size.x > 1e-6f ? (vertex.pos.x - center.x) / size.x : 0.0f;
            const float torso = glm::smoothstep(0.35f, 0.78f, y01);
            const float chest = glm::smoothstep(0.52f, 0.86f, y01);
            const float head = glm::smoothstep(0.78f, 0.98f, y01);
            const float armBand = chest * glm::smoothstep(0.18f, 0.42f, std::abs(xCentered));

            vertex.pos.y += torso * (0.010f * size.y * breathe);
            vertex.pos.z += chest * (0.008f * size.z * breathe);
            vertex.pos.x += head * (0.010f * size.x * sway);
            vertex.pos.z += armBand * (0.006f * size.z * sway * (vertex.pos.x >= center.x ? 1.0f : -1.0f));
        }

        meshes[meshIndex].primitives.front()->updateVertexData(posed);
    }

    recomputeBounds();
    animationPreprocessedFrameValid = true;
    ++animationPreprocessedFrameCounter;
}

void Model::applyProceduralJumpAnimation(double deltaSeconds)
{
    character.proceduralJumpTime += static_cast<float>(deltaSeconds) *
                                    std::max(character.jumpAnimSpeed, 0.01f);

    if (!proceduralJumpBaseVerticesValid)
    {
        proceduralJumpBaseVertices.clear();
        proceduralJumpBaseVertices.reserve(meshes.size());
        for (const auto& mesh : meshes)
        {
            const Primitive* primitive = !mesh.primitives.empty() ? mesh.primitives.front().get() : nullptr;
            proceduralJumpBaseVertices.push_back(primitive ? primitive->cpuVertices : std::vector<Vertex>{});
        }
        proceduralJumpBaseVerticesValid = true;
    }

    glm::vec3 localMin(std::numeric_limits<float>::max());
    glm::vec3 localMax(std::numeric_limits<float>::lowest());
    bool found = false;
    for (const auto& vertices : proceduralJumpBaseVertices)
    {
        for (const Vertex& vertex : vertices)
        {
            localMin = glm::min(localMin, vertex.pos);
            localMax = glm::max(localMax, vertex.pos);
            found = true;
        }
    }
    if (!found)
    {
        return;
    }

    const glm::vec3 size = glm::max(localMax - localMin, glm::vec3(0.0001f));
    const glm::vec3 center = (localMin + localMax) * 0.5f;
    float crouch = 0.0f;
    float airborne = 0.0f;
    switch (character.jumpPhase)
    {
    case CharacterController::JumpPhase::Start:
        crouch = 0.75f;
        airborne = 0.25f;
        break;
    case CharacterController::JumpPhase::Apex:
        crouch = 0.05f;
        airborne = 1.0f;
        break;
    case CharacterController::JumpPhase::Fall:
        crouch = 0.25f;
        airborne = 0.75f;
        break;
    case CharacterController::JumpPhase::Land:
        crouch = 0.95f;
        airborne = 0.15f;
        break;
    case CharacterController::JumpPhase::None:
        crouch = 0.0f;
        airborne = 0.0f;
        break;
    }

    const float pulse = 0.5f + 0.5f * std::sin(character.proceduralJumpTime * 8.0f);
    const float lean = (airborne * 0.03f + crouch * 0.015f) * size.z;
    const float kneeTuck = (crouch * 0.05f + airborne * 0.025f) * size.y;
    const float armLift = (airborne * 0.06f + crouch * 0.025f) * size.y;

    for (size_t meshIndex = 0; meshIndex < meshes.size() && meshIndex < proceduralJumpBaseVertices.size(); ++meshIndex)
    {
        if (meshes[meshIndex].primitives.empty() || !meshes[meshIndex].primitives.front())
        {
            continue;
        }

        std::vector<Vertex> posed = proceduralJumpBaseVertices[meshIndex];
        for (Vertex& vertex : posed)
        {
            const float y01 = std::clamp((vertex.pos.y - localMin.y) / size.y, 0.0f, 1.0f);
            const float x01 = std::clamp(std::abs(vertex.pos.x - center.x) / (size.x * 0.5f), 0.0f, 1.0f);
            const float lowerBody = 1.0f - glm::smoothstep(0.18f, 0.58f, y01);
            const float torso = glm::smoothstep(0.42f, 0.82f, y01);
            const float armRegion = glm::smoothstep(0.52f, 0.78f, y01) *
                                    glm::smoothstep(0.22f, 0.65f, x01);
            const float footRegion = 1.0f - glm::smoothstep(0.0f, 0.18f, y01);

            vertex.pos.y -= crouch * lowerBody * size.y * 0.04f;
            vertex.pos.y += airborne * armRegion * armLift;
            vertex.pos.z += torso * lean;
            vertex.pos.z -= lowerBody * kneeTuck * (0.55f + 0.10f * pulse);
            vertex.pos.x += (vertex.pos.x >= center.x ? 1.0f : -1.0f) *
                            armRegion * airborne * size.x * 0.015f;
            vertex.pos.y -= footRegion * crouch * size.y * 0.015f;
        }

        meshes[meshIndex].primitives.front()->updateVertexData(posed);
    }

    recomputeBounds();
    animationPreprocessedFrameValid = true;
    ++animationPreprocessedFrameCounter;
}

void Model::applyProceduralAttackAnimation(double deltaSeconds)
{
    character.proceduralAttackTime += static_cast<float>(deltaSeconds) *
                                      std::max(character.attackAnimationSpeed, 0.01f);

    if (!proceduralAttackBaseVerticesValid)
    {
        proceduralAttackBaseVertices.clear();
        proceduralAttackBaseVertices.reserve(meshes.size());
        for (const auto& mesh : meshes)
        {
            const Primitive* primitive = !mesh.primitives.empty() ? mesh.primitives.front().get() : nullptr;
            proceduralAttackBaseVertices.push_back(primitive ? primitive->cpuVertices : std::vector<Vertex>{});
        }
        proceduralAttackBaseVerticesValid = true;
    }

    glm::vec3 localMin(std::numeric_limits<float>::max());
    glm::vec3 localMax(std::numeric_limits<float>::lowest());
    bool found = false;
    for (const auto& vertices : proceduralAttackBaseVertices)
    {
        for (const Vertex& vertex : vertices)
        {
            localMin = glm::min(localMin, vertex.pos);
            localMax = glm::max(localMax, vertex.pos);
            found = true;
        }
    }
    if (!found)
    {
        return;
    }

    const glm::vec3 size = glm::max(localMax - localMin, glm::vec3(0.0001f));
    const glm::vec3 center = (localMin + localMax) * 0.5f;
    const float cycle = std::clamp(1.0f - (character.attackActiveTimer / std::max(character.attackDuration, 0.001f)),
                                   0.0f,
                                   1.0f);
    const float windup = 1.0f - glm::smoothstep(0.22f, 0.46f, cycle);
    const float strike = glm::smoothstep(0.24f, 0.55f, cycle) *
                         (1.0f - glm::smoothstep(0.62f, 0.96f, cycle));
    const float recover = glm::smoothstep(0.68f, 1.0f, cycle);
    const float torsoLean = (-0.020f * windup + 0.070f * strike - 0.018f * recover) * size.z;
    const float shoulderLift = (0.030f * windup + 0.075f * strike - 0.020f * recover) * size.y;
    const float armReach = (-0.010f * windup + 0.085f * strike - 0.025f * recover) * size.z;
    const float armSpread = (0.008f * windup + 0.022f * strike) * size.x;
    const float bodyTwist = std::sin(cycle * static_cast<float>(M_PI)) * 0.020f * size.x;
    const float headDip = (0.015f * windup + 0.030f * strike) * size.y;

    for (size_t meshIndex = 0; meshIndex < meshes.size() && meshIndex < proceduralAttackBaseVertices.size(); ++meshIndex)
    {
        if (meshes[meshIndex].primitives.empty() || !meshes[meshIndex].primitives.front())
        {
            continue;
        }

        std::vector<Vertex> posed = proceduralAttackBaseVertices[meshIndex];
        for (Vertex& vertex : posed)
        {
            const float y01 = std::clamp((vertex.pos.y - localMin.y) / size.y, 0.0f, 1.0f);
            const float xCentered = size.x > 1e-6f ? (vertex.pos.x - center.x) / size.x : 0.0f;
            const float side = vertex.pos.x >= center.x ? 1.0f : -1.0f;
            const float torso = glm::smoothstep(0.36f, 0.82f, y01);
            const float chest = glm::smoothstep(0.52f, 0.88f, y01);
            const float head = glm::smoothstep(0.80f, 0.98f, y01);
            const float armRegion = chest * glm::smoothstep(0.12f, 0.32f, std::abs(xCentered));
            const float attackArm = armRegion * glm::smoothstep(0.02f, 0.22f, xCentered);
            const float supportArm = armRegion * glm::smoothstep(0.02f, 0.22f, -xCentered);
            const float lowerBody = 1.0f - glm::smoothstep(0.18f, 0.52f, y01);

            vertex.pos.z += torso * torsoLean;
            vertex.pos.x += torso * bodyTwist * side;
            vertex.pos.y -= head * headDip;
            vertex.pos.z += attackArm * armReach;
            vertex.pos.y += attackArm * shoulderLift;
            vertex.pos.x += attackArm * armSpread;
            vertex.pos.z += supportArm * (armReach * 0.32f);
            vertex.pos.y += supportArm * (shoulderLift * 0.35f);
            vertex.pos.x -= supportArm * (armSpread * 0.65f);
            vertex.pos.y -= lowerBody * strike * size.y * 0.012f;
        }

        meshes[meshIndex].primitives.front()->updateVertexData(posed);
    }

    recomputeBounds();
    animationPreprocessedFrameValid = true;
    ++animationPreprocessedFrameCounter;
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
    updateWorldBoundsFromLocalBounds();
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
        boundsMinLocal = glm::vec3(0.0f);
        boundsMaxLocal = glm::vec3(0.0f);
        boundsLocalValid = false;
        return;
    }
    boundsMinLocal = localMin;
    boundsMaxLocal = localMax;
    boundsLocalValid = true;
    if (!followAnchorLocalCenterInitialized)
    {
        followAnchorLocalCenter = (localMin + localMax) * 0.5f;
        followAnchorLocalCenterInitialized = true;
    }

    updateWorldBoundsFromLocalBounds();
}

void Model::updateWorldBoundsFromLocalBounds()
{
    if (!boundsLocalValid)
    {
        recomputeBounds();
        return;
    }

    const glm::vec3 localMin = boundsMinLocal;
    const glm::vec3 localMax = boundsMaxLocal;
    const glm::vec3 localCenter = (localMin + localMax) * 0.5f;
    const float localRadius = glm::length(localMax - localMin) * 0.5f;

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
    if (isCharacterRuntimeDriven() && followAnchorLocalCenterInitialized)
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
    if (!isCharacterRuntimeDriven())
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
        const float targetMoveSpeed = character.keyShift
            ? character.moveSpeed * character.sprintSpeedMultiplier
            : character.moveSpeed;
        targetVelocity = glm::normalize(character.inputDir) * targetMoveSpeed;
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
        character.jumpStartedFromInput = true;
        character.jumpPhase = CharacterController::JumpPhase::Start;
        character.jumpPhaseTimer = character.jumpStartMinDuration;
        character.currentAnimState = CharacterController::AnimState::Jump;
    }
    
    // Apply gravity only when the scene item opts into it. The TPS bootstrap
    // uses the arcade controller on a fixed ground plane.
    if (useGravity)
    {
        character.velocity.y += character.gravity * dt;
    }
    else
    {
        character.velocity.y = 0.0f;
    }
    
    // Update position
    glm::vec3 currentPos = glm::vec3(worldTransform[3]);
    currentPos += character.velocity * dt;
    
    // Ground collision
    if (currentPos.y <= character.groundHeight + 0.001f)
    {
        currentPos.y = character.groundHeight;
        character.velocity.y = 0.0f;
        character.isGrounded = true;
        character.airborneTimer = 0.0f;
    }
    else
    {
        character.isGrounded = false;
        character.airborneTimer += dt;
        character.lastAirborneVerticalVelocity = character.velocity.y;
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
    
    CharacterController::AnimState targetState = character.currentAnimState;
    
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
        case CharacterController::AnimState::Attack:
            targetWeight = 0.85f;
            break;
    }
    
    character.currentAnimWeight = glm::mix(character.currentAnimWeight, targetWeight,
                                           glm::min(character.animBlendSpeed * dt, 1.0f));
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

        auto lowerCopy = [](std::string value) -> std::string {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        };

        auto findClipExact = [&](const std::vector<std::string>& searchTerms) -> std::string {
            for (const auto& term : searchTerms)
            {
                if (term.empty())
                {
                    continue;
                }
                const std::string loweredTerm = lowerCopy(term);
                for (size_t i = 0; i < animationClips.size(); ++i)
                {
                    const auto& name = animationClips[i].name;
                    if (lowerCopy(name) == loweredTerm)
                    {
                        return name;
                    }
                }
            }
            return "";
        };

        auto findPhaseClip = [&](const std::vector<std::string>& exactTerms,
                                 const std::vector<std::string>& broadTerms) -> std::string {
            std::string clip = findClipExact(exactTerms);
            if (!clip.empty())
            {
                return clip;
            }
            return findClip(broadTerms);
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
                    targetClip = findPhaseClip({
                        "jump_takeoff", "jump_start", "takeoff", "jump_up"
                    }, {
                        "jump_takeoff", "jump_start", "takeoff", "jump_up",
                        character.animJump
                    });
                    targetLoop = false;
                }
                else if (character.jumpPhase == CharacterController::JumpPhase::Land)
                {
                    targetClip = findPhaseClip({
                        character.animLand,
                        "jump_land", "landing", "land_to_idle"
                    }, {
                        character.animLand,
                        "jump_land", "landing", "land", "land_to_idle"
                    });
                    targetLoop = false;
                }
                else if (character.jumpPhase == CharacterController::JumpPhase::Fall)
                {
                    targetClip = findPhaseClip({
                        character.animFall,
                        "jump_fall", "fall_loop", "falling"
                    }, {
                        character.animFall,
                        "jump_fall", "fall_loop", "falling", "fall"
                    });
                    targetLoop = true;
                }
                else
                {
                    targetClip = findPhaseClip({
                        "jump_apex", "jump_loop"
                    }, {
                        "jump_apex", "jump_loop"
                    });
                    if (targetClip.empty())
                    {
                        targetClip = findPhaseClip({
                            character.animFall,
                            "jump_fall", "fall_loop", "falling"
                        }, {
                            character.animFall,
                            "jump_fall", "fall_loop", "falling", "fall"
                        });
                    }
                    if (targetClip.empty())
                    {
                        targetClip = findClip({
                            character.animJump, "jump",
                            "Jumping", "JUMPING", "jumping", "Jump", "JUMP"
                        });
                    }
                    targetLoop = true;
                }
                break;

            case CharacterController::AnimState::Attack:
                targetClip = findClip({
                    character.animAttack,
                    "attack", "swipe", "slash", "melee", "hit", "punch"
                });
                targetLoop = false;
                break;
        }
        
        // Final fallback to first clip only for locomotion states.
        // For Idle/Jump, do not fall back to arbitrary clips (often walk cycles).
        if (targetClip.empty() &&
            targetState != CharacterController::AnimState::Idle &&
            targetState != CharacterController::AnimState::ComeToRest &&
            targetState != CharacterController::AnimState::Jump &&
            targetState != CharacterController::AnimState::Attack &&
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

            case CharacterController::AnimState::Attack:
                targetSpeed = std::max(0.01f, character.attackAnimationSpeed);
                if (targetClip.empty())
                {
                    useProcedural = true;
                }
                break;
        }

        if (targetState == CharacterController::AnimState::Jump && targetClip.empty())
        {
            std::string activeClipName;
            if (fbxAnimationRuntime &&
                fbxAnimationRuntime->activeClipIndex >= 0 &&
                fbxAnimationRuntime->activeClipIndex < static_cast<int>(fbxAnimationRuntime->clips.size()))
            {
                activeClipName = fbxAnimationRuntime->clips[fbxAnimationRuntime->activeClipIndex].name;
            }

            if (!activeClipName.empty())
            {
                targetClip = activeClipName;
                targetLoop = true;
            }
            else
            {
                targetClip = findClip({
                    character.animIdle,
                    "idle_loop", "idle_standing", "idle_stand",
                    "Idle", "IDLE", "idle",
                    character.animWalkForward,
                    "walk_fwd", "walk_forward", "walk_loop",
                    "Walking", "WALKING", "walking", "Walk", "walk"
                });
                targetLoop = true;
            }
        }
        
        // Update animation playback state
        character.currentAnimSpeed = targetSpeed;
        character.isUsingMirroredAnim = useMirroring;
        character.isUsingProceduralAnim = useProcedural;
        animationRuntimeState.resolvedClipName = targetClip;
        animationRuntimeState.resolvedLoop = targetLoop;
        animationRuntimeState.resolvedSpeed = targetSpeed;
        animationRuntimeState.resolvedProcedural = useProcedural;
        animationRuntimeState.resolvedMirrored = useMirroring;
        animationRuntimeState.resolvedPlaying = !useProcedural && !targetClip.empty();
        if (targetState != CharacterController::AnimState::Idle || !useProcedural)
        {
            proceduralIdleBaseVerticesValid = false;
            character.proceduralIdleTime = 0.0f;
        }
        if (targetState != CharacterController::AnimState::Jump || !useProcedural)
        {
            proceduralJumpBaseVerticesValid = false;
            character.proceduralJumpTime = 0.0f;
        }
        if (targetState != CharacterController::AnimState::Attack || !useProcedural)
        {
            proceduralAttackBaseVerticesValid = false;
            character.proceduralAttackTime = 0.0f;
        }

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
                const bool preserveNormalizedTime =
                    isLocomotionState(character.currentAnimState) &&
                    isLocomotionState(character.previousAnimState);
                setAnimationPlaybackState(targetClip,
                                          true,
                                          targetLoop,
                                          targetSpeed,
                                          preserveNormalizedTime);
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
            // Procedural idle/jump is handled in updateAnimation.
            if (fbxAnimationRuntime)
            {
                fbxAnimationRuntime->playing = false;
            }
        }
        else if (targetState == CharacterController::AnimState::Idle && fbxAnimationRuntime)
        {
            // No valid idle clip: hold current pose instead of playing locomotion clips.
            fbxAnimationRuntime->playing = false;
            animationRuntimeState.resolvedPlaying = false;
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
