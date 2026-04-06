#include "physics_jolt.h"
#include "model.h"
#include <iostream>
#include <memory>
#include <vector>
#include <unordered_map>

// Only compile if Jolt is available
#ifdef MOTIVE_JOLT_AVAILABLE

// Jolt assertion callback (required by Jolt library)
static bool MotiveAssertFailed(const char* inExpression, const char* inMessage, const char* inFile, JPH::uint inLine) {
    std::cerr << "[Jolt Assert] " << inFile << ":" << inLine << " - " << inExpression;
    if (inMessage) std::cerr << ": " << inMessage;
    std::cerr << std::endl;
    return true; // Return true to break into debugger
}

// Define the extern AssertFailed variable that Jolt expects
namespace JPH {
    AssertFailedFunction AssertFailed = MotiveAssertFailed;
}

namespace motive {

// JoltPhysicsBody implementation (stub)
JoltPhysicsBody::JoltPhysicsBody(JoltPhysicsWorld* world, Model* model)
    : world_(world), model_(model), bodyID_(), mass_(1.0f) {
}

JoltPhysicsBody::~JoltPhysicsBody() {
    // Body is cleaned up by world
}

bool JoltPhysicsBody::initialize(const PhysicsBodyConfig& config) {
    mass_ = config.mass;
    useGravity_ = config.useGravity;
    customGravity_ = config.customGravity;
    std::cerr << "[Jolt] JoltPhysicsBody::initialize not fully implemented" << std::endl;
    return false; // Return false to indicate stub behavior
}

void JoltPhysicsBody::shutdown() {
    bodyID_ = JPH::BodyID();
}

void JoltPhysicsBody::syncTransformFromPhysics() {
    std::cerr << "[Jolt] syncTransformFromPhysics not implemented" << std::endl;
}

void JoltPhysicsBody::syncTransformToPhysics() {
    std::cerr << "[Jolt] syncTransformToPhysics not implemented" << std::endl;
}

void JoltPhysicsBody::applyForce(const glm::vec3& force, const glm::vec3& localPoint) {
    std::cerr << "[Jolt] applyForce not implemented" << std::endl;
}

void JoltPhysicsBody::applyImpulse(const glm::vec3& impulse, const glm::vec3& localPoint) {
    std::cerr << "[Jolt] applyImpulse not implemented" << std::endl;
}

void JoltPhysicsBody::applyCentralForce(const glm::vec3& force) {
    std::cerr << "[Jolt] applyCentralForce not implemented" << std::endl;
}

void JoltPhysicsBody::applyCentralImpulse(const glm::vec3& impulse) {
    std::cerr << "[Jolt] applyCentralImpulse not implemented" << std::endl;
}

void JoltPhysicsBody::setLinearVelocity(const glm::vec3& velocity) {
    std::cerr << "[Jolt] setLinearVelocity not implemented" << std::endl;
}

void JoltPhysicsBody::setAngularVelocity(const glm::vec3& velocity) {
    std::cerr << "[Jolt] setAngularVelocity not implemented" << std::endl;
}

glm::vec3 JoltPhysicsBody::getLinearVelocity() const {
    std::cerr << "[Jolt] getLinearVelocity not implemented" << std::endl;
    return glm::vec3(0);
}

glm::vec3 JoltPhysicsBody::getAngularVelocity() const {
    std::cerr << "[Jolt] getAngularVelocity not implemented" << std::endl;
    return glm::vec3(0);
}

void JoltPhysicsBody::activate() {}

bool JoltPhysicsBody::isActive() const { 
    return false; 
}

void JoltPhysicsBody::setActivationState(int state) {}

void JoltPhysicsBody::setKinematicTarget(const glm::vec3& position, const glm::quat& rotation) {
    std::cerr << "[Jolt] setKinematicTarget not implemented" << std::endl;
}

void JoltPhysicsBody::setMass(float mass) { 
    mass_ = mass; 
}

float JoltPhysicsBody::getMass() const { 
    return mass_; 
}

void JoltPhysicsBody::setRestitution(float restitution) {
    std::cerr << "[Jolt] setRestitution not implemented" << std::endl;
}

void JoltPhysicsBody::setFriction(float friction) {
    std::cerr << "[Jolt] setFriction not implemented" << std::endl;
}

void JoltPhysicsBody::setUseGravity(bool useGravity) {
    useGravity_ = useGravity;
    std::cerr << "[Jolt] setUseGravity: " << (useGravity ? "true" : "false") << " (not fully implemented)" << std::endl;
}

bool JoltPhysicsBody::getUseGravity() const {
    return useGravity_;
}

void JoltPhysicsBody::setCustomGravity(const glm::vec3& gravity) {
    customGravity_ = gravity;
    std::cerr << "[Jolt] setCustomGravity: (" << gravity.x << ", " << gravity.y << ", " << gravity.z << ") (not fully implemented)" << std::endl;
}

glm::vec3 JoltPhysicsBody::getCustomGravity() const {
    return customGravity_;
}

// JoltPhysicsWorld implementation (stub)
JoltPhysicsWorld::JoltPhysicsWorld() = default;

JoltPhysicsWorld::~JoltPhysicsWorld() { 
    shutdown(); 
}

bool JoltPhysicsWorld::initialize() {
    std::cerr << "[Jolt] Jolt Physics is available but not fully implemented yet." << std::endl;
    std::cerr << "[Jolt] Please implement physics_jolt.cpp or use Bullet Physics." << std::endl;
    initialized_ = true;
    return true;
}

void JoltPhysicsWorld::shutdown() {
    bodies_.clear();
    modelToBodyMap_.clear();
    initialized_ = false;
}

void JoltPhysicsWorld::stepSimulation(float deltaTime, int maxSubSteps) {
    // Stub - no simulation
    (void)deltaTime;
    (void)maxSubSteps;
}

void JoltPhysicsWorld::syncAllTransforms() {
    // Stub - no transforms to sync
}

IPhysicsBody* JoltPhysicsWorld::createPhysicsBody(Model* model, const PhysicsBodyConfig& config) {
    if (!model) return nullptr;
    
    auto body = std::make_unique<JoltPhysicsBody>(this, model);
    JoltPhysicsBody* bodyPtr = body.get();
    
    // Try to initialize - stub will return false
    if (!body->initialize(config)) {
        std::cerr << "[Jolt] Note: Jolt implementation is a stub. Falling back to no physics." << std::endl;
    }
    
    modelToBodyMap_[model] = bodyPtr;
    bodies_.push_back(std::move(body));
    return bodyPtr;
}

void JoltPhysicsWorld::removePhysicsBody(IPhysicsBody* body) {
    // Remove from map
    for (auto it = modelToBodyMap_.begin(); it != modelToBodyMap_.end(); ++it) {
        if (it->second == body) {
            modelToBodyMap_.erase(it);
            break;
        }
    }
    
    // Remove from bodies vector
    for (auto it = bodies_.begin(); it != bodies_.end(); ++it) {
        if (it->get() == body) {
            bodies_.erase(it);
            break;
        }
    }
}

void JoltPhysicsWorld::removePhysicsBodyForModel(Model* model) {
    auto it = modelToBodyMap_.find(model);
    if (it != modelToBodyMap_.end()) {
        IPhysicsBody* body = it->second;
        modelToBodyMap_.erase(it);
        
        // Remove from bodies vector
        for (auto vit = bodies_.begin(); vit != bodies_.end(); ++vit) {
            if (vit->get() == body) {
                bodies_.erase(vit);
                break;
            }
        }
    }
}

IPhysicsBody* JoltPhysicsWorld::getBodyForModel(Model* model) {
    auto it = modelToBodyMap_.find(model);
    return (it != modelToBodyMap_.end()) ? it->second : nullptr;
}

RaycastHit JoltPhysicsWorld::raycast(const glm::vec3& from, const glm::vec3& to) const {
    std::cerr << "[Jolt] raycast not implemented" << std::endl;
    (void)from;
    (void)to;
    return RaycastHit{};
}

std::vector<RaycastHit> JoltPhysicsWorld::raycastAll(const glm::vec3& from, const glm::vec3& to) const {
    std::cerr << "[Jolt] raycastAll not implemented" << std::endl;
    (void)from;
    (void)to;
    return {};
}

void JoltPhysicsWorld::setGravity(const glm::vec3& gravity) {
    (void)gravity;
}

glm::vec3 JoltPhysicsWorld::getGravity() const {
    return glm::vec3(0, -9.81f, 0);
}

PhysicsStats JoltPhysicsWorld::getStats() const {
    PhysicsStats stats;
    stats.backendName = "Jolt Physics (Stub)";
    stats.totalBodyCount = static_cast<int>(bodies_.size());
    return stats;
}

} // namespace motive

#endif // MOTIVE_JOLT_AVAILABLE
