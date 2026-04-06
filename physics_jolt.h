#pragma once

#include "physics_interface.h"

// Jolt Physics implementation header

// Auto-detect Jolt availability based on library existence
#if __has_include("Jolt/Jolt.h")
    #define MOTIVE_JOLT_AVAILABLE 1
#endif

#ifdef MOTIVE_JOLT_AVAILABLE

// Only include full implementation if Jolt is available
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>

namespace motive {

// Forward declaration
class JoltPhysicsWorld;

class JoltPhysicsBody : public IPhysicsBody {
public:
    JoltPhysicsBody(JoltPhysicsWorld* world, Model* model);
    ~JoltPhysicsBody() override;
    
    // IPhysicsBody interface
    void syncTransformFromPhysics() override;
    void syncTransformToPhysics() override;
    
    void applyForce(const glm::vec3& force, const glm::vec3& localPoint = glm::vec3(0.0f)) override;
    void applyImpulse(const glm::vec3& impulse, const glm::vec3& localPoint = glm::vec3(0.0f)) override;
    void applyCentralForce(const glm::vec3& force) override;
    void applyCentralImpulse(const glm::vec3& impulse) override;
    
    void setLinearVelocity(const glm::vec3& velocity) override;
    void setAngularVelocity(const glm::vec3& velocity) override;
    glm::vec3 getLinearVelocity() const override;
    glm::vec3 getAngularVelocity() const override;
    
    void activate() override;
    bool isActive() const override;
    void setActivationState(int state) override;
    
    void setKinematicTarget(const glm::vec3& position, const glm::quat& rotation) override;
    
    void setMass(float mass) override;
    float getMass() const override;
    void setRestitution(float restitution) override;
    void setFriction(float friction) override;
    
    // Gravity control
    void setUseGravity(bool useGravity) override;
    bool getUseGravity() const override;
    void setCustomGravity(const glm::vec3& gravity) override;
    glm::vec3 getCustomGravity() const override;
    
    Model* getModel() const override { return model_; }
    void* getNativeHandle() override { return bodyID_.IsInvalid() ? nullptr : &bodyID_; }
    
    // Internal
    bool initialize(const PhysicsBodyConfig& config);
    void shutdown();
    void setBodyID(JPH::BodyID id) { bodyID_ = id; }
    JPH::BodyID getBodyID() const { return bodyID_; }
    
private:
    JoltPhysicsWorld* world_ = nullptr;
    Model* model_ = nullptr;
    JPH::BodyID bodyID_;
    float mass_ = 1.0f;
    bool useGravity_ = true;
    glm::vec3 customGravity_ = glm::vec3(0.0f);
};

class JoltPhysicsWorld : public IPhysicsWorld {
public:
    JoltPhysicsWorld();
    ~JoltPhysicsWorld() override;
    
    bool initialize() override;
    void shutdown() override;
    bool isInitialized() const override { return initialized_; }
    
    void stepSimulation(float deltaTime, int maxSubSteps = 1) override;
    void syncAllTransforms() override;
    
    IPhysicsBody* createPhysicsBody(Model* model, const PhysicsBodyConfig& config) override;
    void removePhysicsBody(IPhysicsBody* body) override;
    void removePhysicsBodyForModel(Model* model) override;
    IPhysicsBody* getBodyForModel(Model* model) override;
    
    RaycastHit raycast(const glm::vec3& from, const glm::vec3& to) const override;
    std::vector<RaycastHit> raycastAll(const glm::vec3& from, const glm::vec3& to) const override;
    
    void setGravity(const glm::vec3& gravity) override;
    glm::vec3 getGravity() const override;
    
    PhysicsStats getStats() const override;
    PhysicsEngineType getType() const override { return PhysicsEngineType::Jolt; }
    const char* getBackendName() const override { return "Jolt Physics"; }
    
    // Jolt-specific accessors
    JPH::PhysicsSystem* getPhysicsSystem() const { return physicsSystem_.get(); }
    JPH::TempAllocator* getTempAllocator() const { return tempAllocator_.get(); }
    JPH::JobSystem* getJobSystem() const { return jobSystem_.get(); }
    
private:
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator_;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem_;
    std::unique_ptr<JPH::PhysicsSystem> physicsSystem_;
    
    std::vector<std::unique_ptr<JoltPhysicsBody>> bodies_;
    std::unordered_map<Model*, JoltPhysicsBody*> modelToBodyMap_;
    
    bool initialized_ = false;
    mutable PhysicsStats lastStats_;
    
    void updateStats() const;
};

} // namespace motive

#else // !MOTIVE_JOLT_AVAILABLE

// Stub implementation when Jolt is not available
namespace motive {

class JoltPhysicsWorld : public IPhysicsWorld {
public:
    bool initialize() override { 
        std::cerr << "[Physics] Jolt not available. Jolt headers not found." << std::endl;
        return false; 
    }
    void shutdown() override {}
    bool isInitialized() const override { return false; }
    
    void stepSimulation(float deltaTime, int maxSubSteps = 1) override {}
    void syncAllTransforms() override {}
    
    IPhysicsBody* createPhysicsBody(Model* model, const PhysicsBodyConfig& config) override { return nullptr; }
    void removePhysicsBody(IPhysicsBody* body) override {}
    void removePhysicsBodyForModel(Model* model) override {}
    IPhysicsBody* getBodyForModel(Model* model) override { return nullptr; }
    
    RaycastHit raycast(const glm::vec3& from, const glm::vec3& to) const override { return RaycastHit{}; }
    std::vector<RaycastHit> raycastAll(const glm::vec3& from, const glm::vec3& to) const override { return {}; }
    
    void setGravity(const glm::vec3& gravity) override {}
    glm::vec3 getGravity() const override { return glm::vec3(0, -9.81f, 0); }
    
    PhysicsStats getStats() const override { return PhysicsStats{}; }
    PhysicsEngineType getType() const override { return PhysicsEngineType::Jolt; }
    const char* getBackendName() const override { return "Jolt Physics (Not Available)"; }
};

} // namespace motive

#endif // MOTIVE_JOLT_AVAILABLE
