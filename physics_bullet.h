#pragma once

#include "physics_interface.h"
#include <btBulletDynamicsCommon.h>

namespace motive {

// Forward declaration
class BulletPhysicsWorld;

// ============================================================================
// Bullet Physics Body Implementation
// ============================================================================

class BulletPhysicsBody : public IPhysicsBody {
public:
    BulletPhysicsBody(BulletPhysicsWorld* world, Model* model);
    ~BulletPhysicsBody() override;
    
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
    void* getNativeHandle() override { return rigidBody_.get(); }
    
    // Internal initialization
    bool initialize(const PhysicsBodyConfig& config);
    void shutdown();
    
    // Accessors for internal use
    btRigidBody* getRigidBody() const { return rigidBody_.get(); }
    BulletPhysicsWorld* getWorld() const { return world_; }
    
private:
    BulletPhysicsWorld* world_ = nullptr;
    Model* model_ = nullptr;
    
    std::unique_ptr<btCollisionShape> shape_;
    std::unique_ptr<btRigidBody> rigidBody_;
    std::unique_ptr<btMotionState> motionState_;
    
    float mass_ = 1.0f;
    bool isKinematic_ = false;
    bool initialized_ = false;
    bool useGravity_ = true;
    glm::vec3 customGravity_ = glm::vec3(0.0f);
    
    std::unique_ptr<btCollisionShape> createShape(const PhysicsBodyConfig& config);
};

// ============================================================================
// Bullet Physics World Implementation
// ============================================================================

class BulletPhysicsWorld : public IPhysicsWorld {
public:
    BulletPhysicsWorld();
    ~BulletPhysicsWorld() override;
    
    // IPhysicsWorld interface
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
    PhysicsEngineType getType() const override { return PhysicsEngineType::Bullet; }
    const char* getBackendName() const override { return "Bullet Physics"; }
    
    // Bullet-specific accessors
    btDiscreteDynamicsWorld* getDynamicsWorld() const { return dynamicsWorld_.get(); }
    
private:
    std::unique_ptr<btDefaultCollisionConfiguration> collisionConfig_;
    std::unique_ptr<btCollisionDispatcher> dispatcher_;
    std::unique_ptr<btBroadphaseInterface> broadphase_;
    std::unique_ptr<btSequentialImpulseConstraintSolver> solver_;
    std::unique_ptr<btDiscreteDynamicsWorld> dynamicsWorld_;
    
    std::vector<std::unique_ptr<BulletPhysicsBody>> bodies_;
    std::unordered_map<Model*, BulletPhysicsBody*> modelToBodyMap_;
    
    bool initialized_ = false;
    mutable PhysicsStats lastStats_;
    
    // Helpers
    void updateStats() const;
};

// Helper functions for glm <-> Bullet conversion
inline btVector3 toBtVector3(const glm::vec3& v) {
    return btVector3(v.x, v.y, v.z);
}

inline glm::vec3 toGlmVec3(const btVector3& v) {
    return glm::vec3(v.x(), v.y(), v.z());
}

inline btQuaternion toBtQuaternion(const glm::quat& q) {
    return btQuaternion(q.x, q.y, q.z, q.w);
}

inline glm::quat toGlmQuat(const btQuaternion& q) {
    return glm::quat(q.w(), q.x(), q.y(), q.z());
}

inline btTransform toBtTransform(const glm::mat4& m) {
    glm::vec3 position(m[3]);
    glm::quat rotation = glm::quat(glm::mat3(m));
    return btTransform(toBtQuaternion(rotation), toBtVector3(position));
}

inline glm::mat4 btTransformToGlmMat4(const btTransform& t) {
    btQuaternion rot = t.getRotation();
    btVector3 pos = t.getOrigin();
    
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), toGlmVec3(pos));
    glm::mat4 rotation = glm::mat4_cast(toGlmQuat(rot));
    return translation * rotation;
}

} // namespace motive
