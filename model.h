#pragma once

#include <array>
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>

#include <QImage>
#include <QString>
#include <GLFW/glfw3.h>
#include <../tinygltf/tiny_gltf.h>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "object_transform.h"
#include "animation.h"
#include "physics_interface.h"

class Engine;
class Model;
class Mesh;
class Primitive;
class Texture;

constexpr uint32_t kMaxPrimitiveSkinJoints = 256;

struct SkinMatrixPalette
{
    std::array<glm::mat4, kMaxPrimitiveSkinJoints> joints{};
};

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::uvec4 joints = glm::uvec4(0u);
    glm::vec4 weights = glm::vec4(0.0f);

    static VkVertexInputBindingDescription getBindingDescription();
    static std::array<VkVertexInputAttributeDescription, 5> getAttributeDescriptions();
    static std::array<VkVertexInputAttributeDescription, 3> getNonSkinnedAttributeDescriptions();
};

struct GltfCombinedPrimitiveData {
    int materialIndex = -1;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

struct SharedTextureResources {
    Engine* engine = nullptr;
    VkImage textureImage = VK_NULL_HANDLE;
    VkDeviceMemory textureImageMemory = VK_NULL_HANDLE;
    VkImageView textureImageView = VK_NULL_HANDLE;
    VkSampler textureSampler = VK_NULL_HANDLE;
    uint32_t textureWidth = 0;
    uint32_t textureHeight = 0;
    VkFormat textureFormat = VK_FORMAT_UNDEFINED;

    ~SharedTextureResources();
};

#include "primitive.h"

class Mesh {
public:
    Mesh(Engine* engine, Model* model, const std::vector<Vertex>& vertices, bool initializeTextureResources = true);
    Mesh(Engine* engine, Model* model, tinygltf::Mesh);
    Mesh(Engine* engine, Model* model, const std::vector<GltfCombinedPrimitiveData>& combinedPrimitives);
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;
    ~Mesh();

    Model* model = nullptr;
    std::vector<std::unique_ptr<Primitive>> primitives;
    Engine* engine = nullptr;
};

class Model {
public:
    struct AnimationClipInfo
    {
        std::string name;
    };

    // Character controller for arcade-style physics
    struct CharacterController {
        glm::vec3 velocity = glm::vec3(0.0f);
        glm::vec3 inputDir = glm::vec3(0.0f);  // WASD input direction
        float moveSpeed = 3.0f;
        float sprintSpeedMultiplier = 1.5f;
        float gravity = -9.8f;
        float jumpSpeed = 5.0f;
        float groundHeight = 0.0f;
        bool isGrounded = false;
        bool isControllable = false;  // Set true for player character
        bool jumpRequested = false;     // Set true to trigger jump
        
        // Directional key states for animation selection
        bool keyW = false;
        bool keyA = false;
        bool keyS = false;
        bool keyD = false;
        bool keyQ = false;
        bool keyShift = false;
        bool phaseThroughWalls = false;
        
        // Animation state
        enum class AnimState { Idle, ComeToRest, WalkForward, WalkBackward, WalkLeft, WalkRight, Run, Jump };
        enum class JumpPhase { None, Start, Apex, Fall, Land };
        AnimState currentAnimState = AnimState::Idle;
        JumpPhase jumpPhase = JumpPhase::None;
        float walkSpeedThreshold = 0.1f;
        float runSpeedThreshold = 4.0f;
        float turnResponsiveness = 3.0f; // Lower = wider turning radius, more gradual
        
        // Standard Human Animation Set
        // These are common animation names used in humanoid characters
        // The system will try these names in order when selecting animations
        
        // Idle - standing still
        std::string animIdle = "idle";
        
        // Walking animations (directional)
        std::string animWalkForward = "walk_forward";   // or "walk_fwd", "walking", "walk"
        std::string animWalkBackward = "walk_backward"; // or "walk_back", "walk_bwd"
        std::string animWalkLeft = "walk_left";         // or "strafe_left"
        std::string animWalkRight = "walk_right";       // or "strafe_right"
        
        // Running animations
        std::string animRun = "run";                    // or "run_forward", "running", "sprint"
        std::string animRunBackward = "run_backward";   // or "run_back"
        std::string animRunLeft = "run_left";           // or "strafe_run_left"
        std::string animRunRight = "run_right";         // or "strafe_run_right"
        
        // Vertical movement
        std::string animJump = "jump";                  // or "jump_up", "jumping"
        std::string animFall = "fall";                  // or "falling", "fall_loop"
        std::string animLand = "land";                  // or "landing"
        std::string animComeToRest = "stop";            // or "brake", "halt", "run_to_idle"
        
        // Crouching
        std::string animCrouchIdle = "crouch_idle";
        std::string animCrouchWalk = "crouch_walk";
        
        // Additional actions
        std::string animTurnLeft = "turn_left";
        std::string animTurnRight = "turn_right";
        
        // Animation synthesis options
        bool enableProceduralIdle = true;        // Generate idle when no clip exists
        bool enableTimeWarp = true;              // Speed up/slow down animations
        bool enableAnimationMirroring = true;    // Mirror animations for opposite directions
        
        // Time warp multipliers (1.0 = normal speed)
        float idleAnimSpeed = 1.0f;
        float walkAnimSpeed = 1.0f;
        float runAnimSpeed = 1.5f;               // 1.5x speed makes walk look like run
        float backwardAnimSpeed = 1.0f;
        float jumpAnimSpeed = 1.0f;
        
        // Procedural idle parameters
        float idleBobFrequency = 2.0f;           // Breathing cycles per second
        float idleBobAmplitude = 0.02f;          // How much the chest moves up/down
        float idleSwayFrequency = 1.0f;          // Sway cycles per second
        float idleSwayAmplitude = 0.01f;         // How much the body sways
        
        // Animation playback state
        float currentAnimSpeed = 1.0f;
        bool isUsingMirroredAnim = false;
        bool isUsingProceduralAnim = false;
        
        // For smooth animation blending
        float currentAnimWeight = 0.0f;  // 0=idle, 1=walk
        float animBlendSpeed = 5.0f;     // How fast to blend between states
        float comeToRestDuration = 0.20f;
        float comeToRestTimer = 0.0f;
        float moveIntentGraceDuration = 0.10f;
        float moveIntentGraceTimer = 0.0f;
        float jumpStartMinDuration = 0.08f;
        float jumpLandMinDuration = 0.12f;
        float jumpFallVelocityThreshold = -0.5f;
        float jumpApexVelocityThreshold = 0.5f;
        float jumpPhaseTimer = 0.0f;
        float airborneTimer = 0.0f;
        float lastAirborneVerticalVelocity = 0.0f;
        bool jumpStartedFromInput = false;
        float proceduralIdleTime = 0.0f;
        float proceduralJumpTime = 0.0f;
        bool wasGroundedLastFrame = true;
        AnimState previousAnimState = AnimState::Idle;
        bool hadMoveKeyIntentLastFrame = false;
        bool pendingRestPointLatch = false;
        bool enableRestPointOnMoveRelease = true;
        float restPointNormalizedOnMoveRelease = 1.0f;
    };

    struct AnimationRuntimeState
    {
        enum class Source
        {
            Manual,
            CharacterController
        };

        Source source = Source::Manual;
        CharacterController::AnimState semanticState = CharacterController::AnimState::Idle;
        CharacterController::JumpPhase semanticJumpPhase = CharacterController::JumpPhase::None;
        std::string resolvedClipName;
        bool resolvedPlaying = false;
        bool resolvedLoop = false;
        float resolvedSpeed = 1.0f;
        bool resolvedProcedural = false;
        bool resolvedMirrored = false;
    };

    Model(const std::string& gltfPath, Engine* engine, bool consolidateMeshes = true);
    Model(const std::vector<Vertex>& vertices, Engine* engine);
    ~Model();

    void scaleToUnitBox();
    void resizeToUnitBox();
    void scale(const glm::vec3& factors);
    void translate(const glm::vec3& offset);
    void rotate(float angleRadians, const glm::vec3& axis);
    void rotate(float xDegrees, float yDegrees, float zDegrees);
    void setSceneTransform(const glm::vec3& translation, const glm::vec3& rotationDegrees, const glm::vec3& scaleFactors);
    void setWorldTransform(const glm::mat4& transform);
    void setPaintOverride(bool enabled, const glm::vec3& color);
    void setAnimationPlaybackState(const std::string& clipName, bool playing, bool loop, float speed);
    void setAnimationProcessingOptions(bool centroidNormalizationEnabled,
                                       float trimStartNormalized,
                                       float trimEndNormalized);
    void updateAnimation(double deltaSeconds);
    void recomputeBounds();
    
    // Character controller methods
    void updateCharacterPhysics(float deltaSeconds);
    void setCharacterInput(const glm::vec3& moveDir);  // Called from input handler
    void applyProceduralIdleAnimation(double deltaSeconds);  // Generate idle when no clip exists
    void applyProceduralJumpAnimation(double deltaSeconds);  // Generate jump when no clip exists
    glm::vec3 getCharacterPosition() const { return glm::vec3(worldTransform[3]); }
    glm::vec3 getFollowAnchorPosition() const;
    
    // Set standard human animation names for character controller
    // Use this to configure which animations play for each movement direction
    void setCharacterAnimationNames(
        const std::string& idle = "",
        const std::string& comeToRest = "",
        const std::string& walkForward = "",
        const std::string& walkBackward = "",
        const std::string& walkLeft = "",
        const std::string& walkRight = "",
        const std::string& run = "",
        const std::string& jump = ""
    );

    tinygltf::Model* tgltfModel = nullptr;

    std::string name = "Added Vertices";
    std::vector<Mesh> meshes;
    Engine* engine = nullptr;
    GLFWwindow* window = nullptr;
    std::vector<Texture*> textures;
    bool visible = true;
    bool animated = false;
    glm::mat4 normalizedBaseTransform = glm::mat4(1.0f);
    glm::mat4 worldTransform = glm::mat4(1.0f);
    glm::vec3 boundsCenter = glm::vec3(0.0f);
    float boundsRadius = 0.0f;
    glm::vec3 boundsMinWorld = glm::vec3(0.0f);
    glm::vec3 boundsMaxWorld = glm::vec3(0.0f);
    glm::vec3 boundsMinLocal = glm::vec3(0.0f);
    glm::vec3 boundsMaxLocal = glm::vec3(0.0f);
    bool boundsLocalValid = false;
    // Stable follow-anchor reference captured from mesh-local bounds.
    // Used for controllable characters to avoid animation AABB jitter/root-loop snaps.
    glm::vec3 followAnchorLocalCenter = glm::vec3(0.0f);
    bool followAnchorLocalCenterInitialized = false;
    bool meshConsolidationEnabled = true;
    std::vector<AnimationClipInfo> animationClips;
    std::unique_ptr<motive::animation::FbxRuntime> fbxAnimationRuntime;
    AnimationRuntimeState animationRuntimeState;
    std::vector<std::vector<Vertex>> proceduralIdleBaseVertices;
    bool proceduralIdleBaseVerticesValid = false;
    std::vector<std::vector<Vertex>> proceduralJumpBaseVertices;
    bool proceduralJumpBaseVerticesValid = false;
    bool animationPreprocessedFrameValid = false;
    uint64_t animationPreprocessedFrameCounter = 0;
    
    // Character controller (for player-controlled models)
    CharacterController character;
    
    // Cached animation clip indices for common states
    int idleClipIndex = -1;
    int walkClipIndex = -1;
    int runClipIndex = -1;
    
    // Physics body (if physics is enabled for this model)
    motive::IPhysicsBody* physicsBody = nullptr;
    
    // Enable physics for this model
    void enablePhysics(motive::IPhysicsWorld& world, const motive::PhysicsBodyConfig& config = motive::PhysicsBodyConfig());
    void disablePhysics(motive::IPhysicsWorld& world);
    bool hasPhysics() const { return physicsBody != nullptr; }
    motive::IPhysicsBody* getPhysicsBody() const { return physicsBody; }
    
    // Update character physics using physics engine
    void updateCharacterPhysics(float deltaTime, motive::IPhysicsWorld& world);
    
    // Animation-Physics Coupling
    std::string animationPhysicsCoupling = "AnimationOnly";  // Default
    void setAnimationPhysicsCoupling(const std::string& coupling) { animationPhysicsCoupling = coupling; }
    const std::string& getAnimationPhysicsCoupling() const { return animationPhysicsCoupling; }
    
    // Per-object gravity settings
    bool useGravity = true;
    glm::vec3 customGravity = glm::vec3(0.0f);
    void setUseGravity(bool use) { useGravity = use; }
    bool getUseGravity() const { return useGravity; }
    void setCustomGravity(const glm::vec3& gravity) { customGravity = gravity; }
    const glm::vec3& getCustomGravity() const { return customGravity; }

private:
    void updateCharacterAnimationSemanticState(float dt);
    void applyTransformToPrimitives(const glm::mat4& transform);
    void syncWorldTransformToPrimitives();
    void updateWorldBoundsFromLocalBounds();
    bool computeProceduralBounds(glm::vec3& minBounds, glm::vec3& maxBounds) const;

    std::unordered_map<int, std::weak_ptr<SharedTextureResources>> gltfMaterialTextureCache;
    friend class Primitive;
};
