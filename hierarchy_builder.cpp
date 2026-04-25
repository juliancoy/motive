#include "hierarchy_builder.h"

#include "viewport_internal_utils.h"
#include "viewport_runtime.h"
#include "scene_controller.h"

#include "camera.h"
#include "display.h"
#include "engine.h"
#include "model.h"
#include "object_transform.h"

#include <QHash>
#include <QJsonObject>
#include <vulkan/vulkan.h>

namespace motive::ui {
namespace {

QJsonObject hierarchyNodeToJson(const ViewportHostWidget::HierarchyNode& node)
{
    QJsonArray children;
    for (const auto& child : node.children)
    {
        children.append(hierarchyNodeToJson(child));
    }

    QString typeString = QStringLiteral("scene_item");
    switch (node.type)
    {
    case ViewportHostWidget::HierarchyNode::Type::Camera:
        typeString = QStringLiteral("camera");
        break;
    case ViewportHostWidget::HierarchyNode::Type::Light:
        typeString = QStringLiteral("light");
        break;
    case ViewportHostWidget::HierarchyNode::Type::SceneItem:
        typeString = QStringLiteral("scene_item");
        break;
    case ViewportHostWidget::HierarchyNode::Type::Mesh:
        typeString = QStringLiteral("mesh");
        break;
    case ViewportHostWidget::HierarchyNode::Type::Primitive:
        typeString = QStringLiteral("primitive");
        break;
    case ViewportHostWidget::HierarchyNode::Type::Material:
        typeString = QStringLiteral("material");
        break;
    case ViewportHostWidget::HierarchyNode::Type::Texture:
        typeString = QStringLiteral("texture");
        break;
    case ViewportHostWidget::HierarchyNode::Type::AnimationGroup:
        typeString = QStringLiteral("animation_group");
        break;
    case ViewportHostWidget::HierarchyNode::Type::AnimationClip:
        typeString = QStringLiteral("animation_clip");
        break;
    case ViewportHostWidget::HierarchyNode::Type::PendingSceneItem:
        typeString = QStringLiteral("pending_scene_item");
        break;
    }

    QJsonObject obj{
        {QStringLiteral("label"), node.label},
        {QStringLiteral("type"), typeString},
        {QStringLiteral("sceneIndex"), node.sceneIndex},
        {QStringLiteral("meshIndex"), node.meshIndex},
        {QStringLiteral("primitiveIndex"), node.primitiveIndex},
        {QStringLiteral("clipName"), node.clipName},
        {QStringLiteral("children"), children}
    };
    
    // Include cameraIndex for camera nodes
    if (node.type == ViewportHostWidget::HierarchyNode::Type::Camera && node.cameraIndex >= 0) {
        obj.insert(QStringLiteral("cameraIndex"), node.cameraIndex);
    }
    
    return obj;
}

}  // namespace

ViewportHierarchyBuilder::ViewportHierarchyBuilder(ViewportRuntime& runtime,
                                                   const ViewportSceneController& sceneController,
                                                   const ViewportHostWidget::SceneLight& sceneLight)
    : m_runtime(runtime)
    , m_sceneController(sceneController)
    , m_sceneLight(sceneLight)
{
}

QList<ViewportHostWidget::HierarchyNode> ViewportHierarchyBuilder::hierarchyItems() const
{
    QList<ViewportHostWidget::HierarchyNode> items;
    QHash<int, QList<ViewportHostWidget::HierarchyNode>> followCameraNodesByScene;
    bool hasAnyRealCamera = false;
    
    // Add all cameras from display
    if (m_runtime.display()) {
        const auto& cameras = m_runtime.display()->cameras;
        
        for (size_t i = 0; i < cameras.size(); ++i) {
            Camera* camera = cameras[i];
            if (!camera) continue;
            hasAnyRealCamera = true;
            const QString cameraName = QString::fromStdString(camera->getCameraName());
            
            QString label = cameraName;
            if (label.isEmpty()) {
                label = (i == 0) ? QStringLiteral("Camera") : QStringLiteral("Camera %1").arg(i);
            }
            
            ViewportHostWidget::HierarchyNode cameraNode{
                label,
                ViewportHostWidget::HierarchyNode::Type::Camera,
                detail::kHierarchyCameraIndex - static_cast<int>(i),  // sceneIndex: unique negative index for UI
                -1,                                                   // meshIndex
                static_cast<int>(i),                                  // cameraIndex: actual index in Display::cameras
                QString::fromStdString(camera->getCameraId()),        // cameraId
                -1,                                                   // primitiveIndex
                QString(),                                            // clipName
                {}                                                    // children
            };

            if (camera->isFollowModeEnabled() && camera->getFollowSceneIndex() >= 0) {
                followCameraNodesByScene[camera->getFollowSceneIndex()].push_back(cameraNode);
                continue;
            }

            items.push_back(cameraNode);
        }
    }
    
    // Ensure at least one camera entry exists if no display or no real cameras.
    if (!hasAnyRealCamera) {
        items.push_front(ViewportHostWidget::HierarchyNode{
            QStringLiteral("Camera"),
            ViewportHostWidget::HierarchyNode::Type::Camera,
            detail::kHierarchyCameraIndex,  // sceneIndex
            -1,                             // meshIndex
            0,                              // cameraIndex: default to first camera
            QString(),                      // cameraId
            -1,                             // primitiveIndex
            QString(),                      // clipName
            {}                              // children
        });
    }

    if (m_sceneLight.exists)
    {
        items.push_back(ViewportHostWidget::HierarchyNode{
            QStringLiteral("Directional Light"),
            ViewportHostWidget::HierarchyNode::Type::Light,
            -1,   // sceneIndex
            -1,   // meshIndex
            -1,   // cameraIndex
            QString(),
            -1,   // primitiveIndex
            QString(),
            {}
        });
    }

    const auto& sceneEntries = m_sceneController.loadedEntries();
    const int loadedCount = m_runtime.engine() ? static_cast<int>(m_runtime.engine()->models.size()) : 0;

    for (int i = 0; i < sceneEntries.size(); ++i)
    {
        const auto& entry = sceneEntries[i];
        ViewportHostWidget::HierarchyNode sceneNode;
        sceneNode.label = entry.name;
        sceneNode.type = ViewportHostWidget::HierarchyNode::Type::SceneItem;
        sceneNode.sceneIndex = i;

        const auto followIt = followCameraNodesByScene.constFind(i);
        if (followIt != followCameraNodesByScene.cend())
        {
            for (const auto& cameraNode : followIt.value())
            {
                sceneNode.children.push_back(cameraNode);
            }
            followCameraNodesByScene.remove(i);
        }

        if (i < loadedCount && m_runtime.engine() && m_runtime.engine()->models[static_cast<size_t>(i)])
        {
            const auto& model = m_runtime.engine()->models[static_cast<size_t>(i)];
            for (size_t meshIndex = 0; meshIndex < model->meshes.size(); ++meshIndex)
            {
                const auto& mesh = model->meshes[meshIndex];
                ViewportHostWidget::HierarchyNode meshNode;
                meshNode.label = QStringLiteral("Mesh %1").arg(meshIndex);
                meshNode.type = ViewportHostWidget::HierarchyNode::Type::Mesh;
                meshNode.sceneIndex = i;
                meshNode.meshIndex = static_cast<int>(meshIndex);

                for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex)
                {
                    const auto& primitive = mesh.primitives[primitiveIndex];
                    ViewportHostWidget::HierarchyNode primitiveNode;
                    primitiveNode.label = QStringLiteral("Primitive %1 (%2 verts, %3 indices)")
                                              .arg(primitiveIndex)
                                              .arg(primitive ? primitive->vertexCount : 0)
                                              .arg(primitive ? primitive->indexCount : 0);
                    primitiveNode.type = ViewportHostWidget::HierarchyNode::Type::Primitive;
                    primitiveNode.sceneIndex = i;
                    primitiveNode.meshIndex = static_cast<int>(meshIndex);
                    primitiveNode.primitiveIndex = static_cast<int>(primitiveIndex);

                    if (primitive)
                    {
                        ViewportHostWidget::HierarchyNode materialNode;
                        const QString materialName = primitive->sourceMaterialName.isEmpty()
                            ? (primitive->sourceMaterialIndex >= 0
                                ? QStringLiteral("material %1").arg(primitive->sourceMaterialIndex)
                                : QStringLiteral("material"))
                            : primitive->sourceMaterialName;
                        materialNode.label = QStringLiteral("Material (%1, %2, cull=%3)")
                                                 .arg(materialName)
                                                 .arg([&]() {
                                                     switch (primitive->alphaMode)
                                                     {
                                                     case PrimitiveAlphaMode::Opaque: return QStringLiteral("opaque");
                                                     case PrimitiveAlphaMode::Mask: return QStringLiteral("mask");
                                                     case PrimitiveAlphaMode::Blend: return QStringLiteral("blend");
                                                     }
                                                     return QStringLiteral("unknown");
                                                 }())
                                                 .arg([&]() {
                                                     switch (primitive->cullMode)
                                                     {
                                                     case PrimitiveCullMode::Back: return QStringLiteral("back");
                                                     case PrimitiveCullMode::Disabled: return QStringLiteral("none");
                                                     case PrimitiveCullMode::Front: return QStringLiteral("front");
                                                     }
                                                     return QStringLiteral("unknown");
                                                 }());
                        materialNode.type = ViewportHostWidget::HierarchyNode::Type::Material;
                        materialNode.sceneIndex = i;
                        materialNode.meshIndex = static_cast<int>(meshIndex);
                        materialNode.primitiveIndex = static_cast<int>(primitiveIndex);

                        if (primitive->sourceHasOpacityTexture || primitive->sourceOpacityScalar < 0.999f)
                        {
                            ViewportHostWidget::HierarchyNode opacityNode;
                            opacityNode.label = primitive->sourceOpacityTextureLabel.isEmpty()
                                ? QStringLiteral("Opacity (scalar=%1%2)")
                                      .arg(QString::number(primitive->sourceOpacityScalar, 'f', 3))
                                      .arg(primitive->sourceOpacityInverted ? QStringLiteral(", inverted") : QString())
                                : QStringLiteral("Opacity (%1, scalar=%2%3)")
                                      .arg(primitive->sourceOpacityTextureLabel)
                                      .arg(QString::number(primitive->sourceOpacityScalar, 'f', 3))
                                      .arg(primitive->sourceOpacityInverted ? QStringLiteral(", inverted") : QString());
                            opacityNode.type = ViewportHostWidget::HierarchyNode::Type::Texture;
                            opacityNode.sceneIndex = i;
                            opacityNode.meshIndex = static_cast<int>(meshIndex);
                            opacityNode.primitiveIndex = static_cast<int>(primitiveIndex);
                            materialNode.children.push_back(opacityNode);
                        }

                        ViewportHostWidget::HierarchyNode textureNode;
                        if (!primitive->texturePreviewImage.isNull() || primitive->textureWidth > 0 || primitive->textureHeight > 0)
                        {
                            textureNode.label = primitive->sourceTextureLabel.isEmpty()
                                ? QStringLiteral("Texture (%1x%2)")
                                      .arg(primitive->textureWidth)
                                      .arg(primitive->textureHeight)
                                : QStringLiteral("Texture (%1, %2x%3)")
                                      .arg(primitive->sourceTextureLabel)
                                      .arg(primitive->textureWidth)
                                      .arg(primitive->textureHeight);
                        }
                        else
                        {
                            textureNode.label = QStringLiteral("Texture (none)");
                        }
                        textureNode.type = ViewportHostWidget::HierarchyNode::Type::Texture;
                        textureNode.sceneIndex = i;
                        textureNode.meshIndex = static_cast<int>(meshIndex);
                        textureNode.primitiveIndex = static_cast<int>(primitiveIndex);
                        materialNode.children.push_back(textureNode);

                        primitiveNode.children.push_back(materialNode);
                    }

                    meshNode.children.push_back(primitiveNode);
                }

                sceneNode.children.push_back(meshNode);
            }

            if (!model->animationClips.empty())
            {
                ViewportHostWidget::HierarchyNode animationGroupNode;
                animationGroupNode.label = QStringLiteral("Animations");
                animationGroupNode.type = ViewportHostWidget::HierarchyNode::Type::AnimationGroup;
                animationGroupNode.sceneIndex = i;

                for (const auto& clip : model->animationClips)
                {
                    ViewportHostWidget::HierarchyNode clipNode;
                    clipNode.label = QString::fromStdString(clip.name);
                    clipNode.type = ViewportHostWidget::HierarchyNode::Type::AnimationClip;
                    clipNode.sceneIndex = i;
                    clipNode.clipName = clipNode.label;
                    animationGroupNode.children.push_back(clipNode);
                }

                sceneNode.children.push_back(animationGroupNode);
            }
        }

        items.push_back(sceneNode);
    }

    // Any remaining follow cameras point at scene indices that do not currently exist.
    // Keep them visible at root so they remain discoverable and editable.
    for (auto it = followCameraNodesByScene.cbegin(); it != followCameraNodesByScene.cend(); ++it)
    {
        for (const auto& cameraNode : it.value())
        {
            items.push_back(cameraNode);
        }
    }

    for (int i = 0; i < m_sceneController.pendingEntries().size(); ++i)
    {
        const auto& entry = m_sceneController.pendingEntries()[i];
        ViewportHostWidget::HierarchyNode pendingNode;
        pendingNode.label = entry.name + QStringLiteral(" (pending)");
        pendingNode.type = ViewportHostWidget::HierarchyNode::Type::PendingSceneItem;
        pendingNode.sceneIndex = m_sceneController.loadedEntries().size() + i;
        items.push_back(pendingNode);
    }

    return items;
}

QJsonArray ViewportHierarchyBuilder::hierarchyJson() const
{
    QJsonArray array;
    for (const auto& node : hierarchyItems())
    {
        array.append(hierarchyNodeToJson(node));
    }
    return array;
}

QJsonArray ViewportHierarchyBuilder::sceneProfileJson() const
{
    auto alphaModeName = [](PrimitiveAlphaMode mode) -> QString
    {
        switch (mode)
        {
        case PrimitiveAlphaMode::Opaque:
            return QStringLiteral("opaque");
        case PrimitiveAlphaMode::Mask:
            return QStringLiteral("mask");
        case PrimitiveAlphaMode::Blend:
            return QStringLiteral("blend");
        }
        return QStringLiteral("unknown");
    };
    auto cullModeName = [](PrimitiveCullMode mode) -> QString
    {
        switch (mode)
        {
        case PrimitiveCullMode::Back:
            return QStringLiteral("back");
        case PrimitiveCullMode::Disabled:
            return QStringLiteral("none");
        case PrimitiveCullMode::Front:
            return QStringLiteral("front");
        }
        return QStringLiteral("unknown");
    };
    auto animStateName = [](Model::CharacterController::AnimState state) -> QString
    {
        using AnimState = Model::CharacterController::AnimState;
        switch (state)
        {
        case AnimState::Idle:
            return QStringLiteral("Idle");
        case AnimState::ComeToRest:
            return QStringLiteral("ComeToRest");
        case AnimState::WalkForward:
            return QStringLiteral("WalkForward");
        case AnimState::WalkBackward:
            return QStringLiteral("WalkBackward");
        case AnimState::WalkLeft:
            return QStringLiteral("WalkLeft");
        case AnimState::WalkRight:
            return QStringLiteral("WalkRight");
        case AnimState::Run:
            return QStringLiteral("Run");
        case AnimState::Jump:
            return QStringLiteral("Jump");
        }
        return QStringLiteral("Unknown");
    };
    auto jumpPhaseName = [](Model::CharacterController::JumpPhase phase) -> QString
    {
        using JumpPhase = Model::CharacterController::JumpPhase;
        switch (phase)
        {
        case JumpPhase::None:
            return QStringLiteral("None");
        case JumpPhase::Start:
            return QStringLiteral("Start");
        case JumpPhase::Apex:
            return QStringLiteral("Apex");
        case JumpPhase::Fall:
            return QStringLiteral("Fall");
        case JumpPhase::Land:
            return QStringLiteral("Land");
        }
        return QStringLiteral("Unknown");
    };

    QJsonArray sceneItems;
    const auto& entries = m_sceneController.loadedEntries();

    for (int sceneIndex = 0; sceneIndex < entries.size(); ++sceneIndex)
    {
        const auto& entry = entries[sceneIndex];
        QJsonObject sceneItem{
            {QStringLiteral("name"), entry.name},
            {QStringLiteral("sourcePath"), entry.sourcePath},
            {QStringLiteral("translation"), QJsonArray{entry.translation.x(), entry.translation.y(), entry.translation.z()}},
            {QStringLiteral("rotation"), QJsonArray{entry.rotation.x(), entry.rotation.y(), entry.rotation.z()}},
            {QStringLiteral("scale"), QJsonArray{entry.scale.x(), entry.scale.y(), entry.scale.z()}},
            {QStringLiteral("paintOverrideEnabled"), entry.paintOverrideEnabled},
            {QStringLiteral("paintOverrideColor"), QJsonArray{entry.paintOverrideColor.x(), entry.paintOverrideColor.y(), entry.paintOverrideColor.z()}},
            {QStringLiteral("visible"), entry.visible},
            {QStringLiteral("activeAnimationClip"), entry.activeAnimationClip},
            {QStringLiteral("animationPlaying"), entry.animationPlaying},
            {QStringLiteral("animationLoop"), entry.animationLoop},
            {QStringLiteral("animationSpeed"), entry.animationSpeed},
            {QStringLiteral("focusPointOffset"), QJsonArray{entry.focusPointOffset.x(), entry.focusPointOffset.y(), entry.focusPointOffset.z()}},
            {QStringLiteral("focusDistance"), entry.focusDistance},
            {QStringLiteral("focusCameraOffset"), QJsonArray{entry.focusCameraOffset.x(), entry.focusCameraOffset.y(), entry.focusCameraOffset.z()}},
            {QStringLiteral("focusCameraOffsetValid"), entry.focusCameraOffsetValid}
        };

        QJsonArray meshArray;
        if (m_runtime.engine() &&
            sceneIndex < static_cast<int>(m_runtime.engine()->models.size()) &&
            m_runtime.engine()->models[static_cast<size_t>(sceneIndex)])
        {
            const auto& model = m_runtime.engine()->models[static_cast<size_t>(sceneIndex)];
            sceneItem.insert(QStringLiteral("boundsCenter"), QJsonArray{model->boundsCenter.x, model->boundsCenter.y, model->boundsCenter.z});
            sceneItem.insert(QStringLiteral("boundsRadius"), model->boundsRadius);
            sceneItem.insert(QStringLiteral("boundsMin"), QJsonArray{model->boundsMinWorld.x, model->boundsMinWorld.y, model->boundsMinWorld.z});
            sceneItem.insert(QStringLiteral("boundsMax"), QJsonArray{model->boundsMaxWorld.x, model->boundsMaxWorld.y, model->boundsMaxWorld.z});
            const glm::vec3 boundsSize = glm::max(model->boundsMaxWorld - model->boundsMinWorld, glm::vec3(0.0f));
            sceneItem.insert(QStringLiteral("boundsSize"), QJsonArray{boundsSize.x, boundsSize.y, boundsSize.z});
            const auto& character = model->character;
            sceneItem.insert(QStringLiteral("isControllable"), character.isControllable);
            sceneItem.insert(QStringLiteral("isGrounded"), character.isGrounded);
            sceneItem.insert(QStringLiteral("jumpRequested"), character.jumpRequested);
            sceneItem.insert(QStringLiteral("keyW"), character.keyW);
            sceneItem.insert(QStringLiteral("keyA"), character.keyA);
            sceneItem.insert(QStringLiteral("keyS"), character.keyS);
            sceneItem.insert(QStringLiteral("keyD"), character.keyD);
            sceneItem.insert(QStringLiteral("inputDir"), QJsonArray{character.inputDir.x, character.inputDir.y, character.inputDir.z});
            sceneItem.insert(QStringLiteral("velocity"), QJsonArray{character.velocity.x, character.velocity.y, character.velocity.z});
            sceneItem.insert(QStringLiteral("currentAnimState"), animStateName(character.currentAnimState));
            sceneItem.insert(QStringLiteral("jumpPhase"), jumpPhaseName(character.jumpPhase));
            sceneItem.insert(QStringLiteral("currentAnimWeight"), character.currentAnimWeight);
            sceneItem.insert(QStringLiteral("currentAnimSpeed"), character.currentAnimSpeed);
            sceneItem.insert(QStringLiteral("animationPreprocessedFrameValid"), model->animationPreprocessedFrameValid);
            sceneItem.insert(QStringLiteral("animationPreprocessedFrameCounter"), static_cast<qint64>(model->animationPreprocessedFrameCounter));
            sceneItem.insert(QStringLiteral("runtimeAnimationPlaying"), false);
            sceneItem.insert(QStringLiteral("runtimeAnimationLoop"), false);
            sceneItem.insert(QStringLiteral("runtimeAnimationSpeed"), character.currentAnimSpeed);
            sceneItem.insert(QStringLiteral("runtimeActiveClip"), QString());

            QString followAnchorMode = QStringLiteral("worldTransform");
            bool followAnchorReferencesPreprocessedFrames = false;
            if (character.isControllable && model->followAnchorLocalCenterInitialized)
            {
                followAnchorMode = QStringLiteral("stableLocalAnchor");
            }
            else if (model->boundsRadius > 0.0f)
            {
                followAnchorMode = QStringLiteral("preprocessedAnimatedBounds");
                followAnchorReferencesPreprocessedFrames = true;
            }
            sceneItem.insert(QStringLiteral("followAnchorMode"), followAnchorMode);
            sceneItem.insert(QStringLiteral("followAnchorReferencesPreprocessedFrames"), followAnchorReferencesPreprocessedFrames);
            if (model->fbxAnimationRuntime)
            {
                sceneItem.insert(QStringLiteral("runtimeAnimationPlaying"), model->fbxAnimationRuntime->playing);
                sceneItem.insert(QStringLiteral("runtimeAnimationLoop"), model->fbxAnimationRuntime->loop);
                sceneItem.insert(QStringLiteral("runtimeAnimationSpeed"), model->fbxAnimationRuntime->speed);
                QString runtimeClip;
                const int activeClipIndex = model->fbxAnimationRuntime->activeClipIndex;
                if (activeClipIndex >= 0 &&
                    activeClipIndex < static_cast<int>(model->fbxAnimationRuntime->clips.size()))
                {
                    runtimeClip = QString::fromStdString(model->fbxAnimationRuntime->clips[activeClipIndex].name);
                }
                sceneItem.insert(QStringLiteral("runtimeActiveClip"), runtimeClip);
            }

            // Publish object-owned follow camera settings for REST/UI validation.
            QJsonObject followCamera{
                {QStringLiteral("exists"), false}
            };
            if (m_runtime.display())
            {
                for (Camera* camera : m_runtime.display()->cameras)
                {
                    if (!camera || !camera->isFollowModeEnabled())
                    {
                        continue;
                    }
                    if (camera->getFollowSceneIndex() != sceneIndex)
                    {
                        continue;
                    }

                    const FollowSettings& fs = camera->getFollowSettings();
                    followCamera = QJsonObject{
                        {QStringLiteral("exists"), true},
                        {QStringLiteral("cameraId"), QString::fromStdString(camera->getCameraId())},
                        {QStringLiteral("cameraName"), QString::fromStdString(camera->getCameraName())},
                        {QStringLiteral("targetSceneIndex"), sceneIndex},
                        {QStringLiteral("distance"), fs.distance},
                        {QStringLiteral("yawDeg"), glm::degrees(fs.relativeYaw)},
                        {QStringLiteral("pitchDeg"), glm::degrees(fs.relativePitch)},
                        {QStringLiteral("smoothSpeed"), fs.smoothSpeed},
                        {QStringLiteral("targetOffset"), QJsonArray{fs.targetOffset.x, fs.targetOffset.y, fs.targetOffset.z}}
                    };
                    break;
                }
            }
            sceneItem.insert(QStringLiteral("followCamera"), followCamera);

            for (int meshIndex = 0; meshIndex < static_cast<int>(model->meshes.size()); ++meshIndex)
            {
                const auto& mesh = model->meshes[static_cast<size_t>(meshIndex)];
                QJsonObject meshObject{{QStringLiteral("meshIndex"), meshIndex}};
                QJsonArray primitiveArray;
                for (int primitiveIndex = 0; primitiveIndex < static_cast<int>(mesh.primitives.size()); ++primitiveIndex)
                {
                    const auto& primitive = mesh.primitives[static_cast<size_t>(primitiveIndex)];
                    if (!primitive)
                    {
                        continue;
                    }
                    primitiveArray.append(QJsonObject{
                        {QStringLiteral("primitiveIndex"), primitiveIndex},
                        {QStringLiteral("vertexCount"), static_cast<int>(primitive->vertexCount)},
                        {QStringLiteral("indexCount"), static_cast<int>(primitive->indexCount)},
                        {QStringLiteral("alphaMode"), alphaModeName(primitive->alphaMode)},
                        {QStringLiteral("cullMode"), cullModeName(primitive->cullMode)},
                        {QStringLiteral("alphaCutoff"), primitive->alphaCutoff},
                        {QStringLiteral("sourceMaterialIndex"), primitive->sourceMaterialIndex},
                        {QStringLiteral("sourceMaterialName"), primitive->sourceMaterialName},
                        {QStringLiteral("sourceTextureLabel"), primitive->sourceTextureLabel},
                        {QStringLiteral("sourceOpacityScalar"), primitive->sourceOpacityScalar},
                        {QStringLiteral("sourceHasOpacityTexture"), primitive->sourceHasOpacityTexture},
                        {QStringLiteral("sourceOpacityInverted"), primitive->sourceOpacityInverted},
                        {QStringLiteral("sourceOpacityTextureLabel"), primitive->sourceOpacityTextureLabel},
                        {QStringLiteral("textureWidth"), static_cast<int>(primitive->textureWidth)},
                        {QStringLiteral("textureHeight"), static_cast<int>(primitive->textureHeight)},
                        {QStringLiteral("hasTexturePreview"), !primitive->texturePreviewImage.isNull()},
                        {QStringLiteral("usesYuvTexture"), primitive->usesYuvTexture},
                        {QStringLiteral("descriptorAllocated"), primitive->primitiveDescriptorSet != VK_NULL_HANDLE}
                    });
                }
                meshObject.insert(QStringLiteral("primitives"), primitiveArray);
                meshArray.append(meshObject);
            }
        }

        sceneItem.insert(QStringLiteral("meshes"), meshArray);
        sceneItems.append(sceneItem);
    }

    return sceneItems;
}

}  // namespace motive::ui
