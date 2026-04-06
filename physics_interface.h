#pragma once

#include <memory>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Forward declarations
class Model;
class Engine;

namespace motive {

// ============================================================================
// Physics Engine Types
// ============================================================================

enum class PhysicsEngineType {
    Bullet,     // Bullet Physics (mature, stable)
    Jolt,       // Jolt Physics (modern, fast, multi-threaded)
    BuiltIn,    // Custom compute shader physics (GPU-native)
    
    Count
};

const char* physicsEngineTypeToString(PhysicsEngineType type);
PhysicsEngineType physicsEngineTypeFromString(const std::string& str);

// ============================================================================
// Animation-Physics Coupling Modes
// ============================================================================

enum class AnimationPhysicsCouplingMode {
    AnimationOnly,       // Animation drives transform, no physics
    Kinematic,           // Animation drives physics body (kinematic)
    RootMotionPhysics,   // Root motion from animation + physics for other aspects
    PhysicsDriven,       // Physics drives transform, animation may influence
    Ragdoll,             // Full physics simulation, animation disabled
    PartialRagdoll,      // Some joints physics, others animated
    ActiveRagdoll,       // Animation influences physics-driven body
    
    Count
};

const char* animationPhysicsCouplingModeToString(AnimationPhysicsCouplingMode mode);
AnimationPhysicsCouplingMode animationPhysicsCouplingModeFromString(const std::string& str);
const char* animationPhysicsCouplingModeDisplayName(AnimationPhysicsCouplingMode mode);
const char* animationPhysicsCouplingModeDescription(AnimationPhysicsCouplingMode mode);

// ============================================================================
// Common Physics Types (Engine Agnostic)
// ============================================================================

enum class CollisionShapeType {
    Box,
    Sphere,
    Capsule,
    Cylinder,
    ConvexHull,
    TriangleMesh,
    StaticPlane
};

struct PhysicsBodyConfig {
    CollisionShapeType shapeType = CollisionShapeType::Box;
    float mass = 1.0f;
    float restitution = 0.3f;
    float friction = 0.5f;
    float rollingFriction = 0.1f;
    bool isKinematic = false;
    bool useModelBounds = true;
    glm::vec3 shapeParams = glm::vec3(1.0f);
    
    // Per-object gravity control
    bool useGravity = true;
    glm::vec3 customGravity = glm::vec3(0.0f);  // Zero = use world gravity
    
    // For character controllers
    bool isCharacter = false;
    float characterMaxSlopeAngle = 45.0f;
};

struct RaycastHit {
    bool hit = false;
    glm::vec3 point;
    glm::vec3 normal;
    float distance = 0.0f;
    void* userData = nullptr;  // Backend-specific body pointer
};

struct PhysicsStats {
    float lastStepTimeMs = 0.0f;
    int activeBodyCount = 0;
    int totalBodyCount = 0;
    int collisionPairs = 0;
    std::string backendName;
};

// ============================================================================
// Physics Body Interface
// ============================================================================

class IPhysicsBody {
public:
    virtual ~IPhysicsBody() = default;
    
    // Transform synchronization
    virtual void syncTransformFromPhysics() = 0;
    virtual void syncTransformToPhysics() = 0;
    
    // Forces and impulses
    virtual void applyForce(const glm::vec3& force, const glm::vec3& localPoint = glm::vec3(0.0f)) = 0;
    virtual void applyImpulse(const glm::vec3& impulse, const glm::vec3& localPoint = glm::vec3(0.0f)) = 0;
    virtual void applyCentralForce(const glm::vec3& force) = 0;
    virtual void applyCentralImpulse(const glm::vec3& impulse) = 0;
    
    // Velocity
    virtual void setLinearVelocity(const glm::vec3& velocity) = 0;
    virtual void setAngularVelocity(const glm::vec3& velocity) = 0;
    virtual glm::vec3 getLinearVelocity() const = 0;
    virtual glm::vec3 getAngularVelocity() const = 0;
    
    // Activation
    virtual void activate() = 0;
    virtual bool isActive() const = 0;
    virtual void setActivationState(int state) = 0;
    
    // Kinematic control
    virtual void setKinematicTarget(const glm::vec3& position, const glm::quat& rotation) = 0;
    
    // Properties
    virtual void setMass(float mass) = 0;
    virtual float getMass() const = 0;
    virtual void setRestitution(float restitution) = 0;
    virtual void setFriction(float friction) = 0;
    
    // Gravity control (per-object)
    virtual void setUseGravity(bool useGravity) = 0;
    virtual bool getUseGravity() const = 0;
    virtual void setCustomGravity(const glm::vec3& gravity) = 0;  // Zero = use world gravity
    virtual glm::vec3 getCustomGravity() const = 0;
    
    // Accessors
    virtual Model* getModel() const = 0;
    virtual void* getNativeHandle() = 0;  // Backend-specific pointer
    
    // Character controller specific
    virtual bool isOnGround() const { return false; }
    virtual void setCharacterInput(const glm::vec3& inputDir) {}
};

// ============================================================================
// Physics World Interface
// ============================================================================

class IPhysicsWorld {
public:
    virtual ~IPhysicsWorld() = default;
    
    // Lifecycle
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual bool isInitialized() const = 0;
    
    // Simulation
    virtual void stepSimulation(float deltaTime, int maxSubSteps = 1) = 0;
    virtual void syncAllTransforms() = 0;
    
    // Body management
    virtual IPhysicsBody* createPhysicsBody(Model* model, const PhysicsBodyConfig& config) = 0;
    virtual void removePhysicsBody(IPhysicsBody* body) = 0;
    virtual void removePhysicsBodyForModel(Model* model) = 0;
    virtual IPhysicsBody* getBodyForModel(Model* model) = 0;
    
    // Queries
    virtual RaycastHit raycast(const glm::vec3& from, const glm::vec3& to) const = 0;
    virtual std::vector<RaycastHit> raycastAll(const glm::vec3& from, const glm::vec3& to) const = 0;
    
    // World properties
    virtual void setGravity(const glm::vec3& gravity) = 0;
    virtual glm::vec3 getGravity() const = 0;
    
    // Stats
    virtual PhysicsStats getStats() const = 0;
    virtual PhysicsEngineType getType() const = 0;
    virtual const char* getBackendName() const = 0;
    
    // Debug visualization (optional)
    virtual void setDebugDrawEnabled(bool enabled) {}
    virtual bool isDebugDrawEnabled() const { return false; }
    
    // Pause/resume simulation
    virtual void setPaused(bool paused) { paused_ = paused; }
    virtual bool isPaused() const { return paused_; }
    
protected:
    bool paused_ = false;
};

// Forward declaration of PhysicsFactory
class PhysicsFactory;

// ============================================================================
// Physics Settings (for UI/Serialization)
// ============================================================================

struct PhysicsSettings {
    PhysicsEngineType engineType = PhysicsEngineType::Bullet;
    glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    int maxSubSteps = 1;
    float fixedTimeStep = 1.0f / 60.0f;
    bool debugDraw = false;
    bool autoSync = true;  // Sync physics to models automatically
    
    // Engine-specific settings
    int joltMaxBodies = 10240;
    int joltMaxBodyPairs = 65536;
    int joltMaxContactConstraints = 10240;
    
    // Built-in settings
    int builtinSolverIterations = 10;
    float builtinCollisionMargin = 0.01f;
};

} // namespace motive
