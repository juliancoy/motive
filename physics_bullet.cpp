#include "physics_bullet.h"
#include "model.h"
#include <algorithm>
#include <iostream>
#include <chrono>

namespace motive {

// ============================================================================
// Bullet Physics Body Implementation
// ============================================================================

BulletPhysicsBody::BulletPhysicsBody(BulletPhysicsWorld* world, Model* model)
    : world_(world), model_(model) {}

BulletPhysicsBody::~BulletPhysicsBody() {
    shutdown();
}

bool BulletPhysicsBody::initialize(const PhysicsBodyConfig& config) {
    if (!world_ || !model_ || !world_->getDynamicsWorld()) return false;
    
    // Create collision shape
    shape_ = createShape(config);
    if (!shape_) {
        std::cerr << "[Bullet] Failed to create collision shape" << std::endl;
        return false;
    }
    
    mass_ = config.mass;
    isKinematic_ = config.isKinematic;
    
    // Calculate inertia
    btVector3 localInertia(0, 0, 0);
    if (mass_ > 0.0f && !isKinematic_) {
        shape_->calculateLocalInertia(mass_, localInertia);
    }
    
    // Create motion state
    btTransform startTransform = toBtTransform(model_->worldTransform);
    motionState_ = std::make_unique<btDefaultMotionState>(startTransform);
    
    // Create rigid body
    btRigidBody::btRigidBodyConstructionInfo rbInfo(
        mass_,
        motionState_.get(),
        shape_.get(),
        localInertia
    );
    
    rigidBody_ = std::make_unique<btRigidBody>(rbInfo);
    rigidBody_->setRestitution(config.restitution);
    rigidBody_->setFriction(config.friction);
    rigidBody_->setRollingFriction(config.rollingFriction);
    
    // Kinematic setup
    if (isKinematic_) {
        rigidBody_->setCollisionFlags(
            rigidBody_->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT
        );
        rigidBody_->setActivationState(DISABLE_DEACTIVATION);
    }
    
    // Gravity setup from config
    useGravity_ = config.useGravity;
    customGravity_ = config.customGravity;
    setUseGravity(useGravity_);
    
    // Add to world
    world_->getDynamicsWorld()->addRigidBody(rigidBody_.get());
    
    initialized_ = true;
    return true;
}

void BulletPhysicsBody::shutdown() {
    if (!initialized_ || !world_ || !world_->getDynamicsWorld()) return;
    
    if (rigidBody_) {
        world_->getDynamicsWorld()->removeRigidBody(rigidBody_.get());
        rigidBody_.reset();
    }
    
    motionState_.reset();
    shape_.reset();
    initialized_ = false;
}

std::unique_ptr<btCollisionShape> BulletPhysicsBody::createShape(const PhysicsBodyConfig& config) {
    if (!model_) return nullptr;
    
    glm::vec3 center = model_->boundsCenter;
    float radius = model_->boundsRadius;
    glm::vec3 halfExtents(radius * 0.5f);
    
    if (!config.useModelBounds) {
        halfExtents = config.shapeParams * 0.5f;
    }
    
    switch (config.shapeType) {
        case CollisionShapeType::Sphere: {
            float sphereRadius = glm::max(glm::max(halfExtents.x, halfExtents.y), halfExtents.z);
            return std::make_unique<btSphereShape>(sphereRadius);
        }
        
        case CollisionShapeType::Box: {
            return std::make_unique<btBoxShape>(toBtVector3(halfExtents));
        }
        
        case CollisionShapeType::Capsule: {
            float capsuleRadius = glm::max(halfExtents.x, halfExtents.z);
            float height = halfExtents.y * 2.0f - capsuleRadius * 2.0f;
            height = glm::max(height, 0.1f);
            return std::make_unique<btCapsuleShape>(capsuleRadius, height);
        }
        
        case CollisionShapeType::Cylinder: {
            return std::make_unique<btCylinderShape>(toBtVector3(halfExtents));
        }
        
        case CollisionShapeType::StaticPlane: {
            return std::make_unique<btStaticPlaneShape>(btVector3(0, 1, 0), 0);
        }
        
        case CollisionShapeType::ConvexHull:
        case CollisionShapeType::TriangleMesh:
        default:
            return std::make_unique<btBoxShape>(toBtVector3(halfExtents));
    }
}

void BulletPhysicsBody::syncTransformFromPhysics() {
    if (!rigidBody_ || !model_ || isKinematic_) return;
    
    btTransform transform;
    motionState_->getWorldTransform(transform);
    model_->worldTransform = btTransformToGlmMat4(transform);
}

void BulletPhysicsBody::syncTransformToPhysics() {
    if (!rigidBody_ || !model_) return;
    
    btTransform transform = toBtTransform(model_->worldTransform);
    rigidBody_->setWorldTransform(transform);
    motionState_->setWorldTransform(transform);
}

void BulletPhysicsBody::applyForce(const glm::vec3& force, const glm::vec3& localPoint) {
    if (rigidBody_) rigidBody_->applyForce(toBtVector3(force), toBtVector3(localPoint));
}

void BulletPhysicsBody::applyImpulse(const glm::vec3& impulse, const glm::vec3& localPoint) {
    if (rigidBody_) rigidBody_->applyImpulse(toBtVector3(impulse), toBtVector3(localPoint));
}

void BulletPhysicsBody::applyCentralForce(const glm::vec3& force) {
    if (rigidBody_) rigidBody_->applyCentralForce(toBtVector3(force));
}

void BulletPhysicsBody::applyCentralImpulse(const glm::vec3& impulse) {
    if (rigidBody_) rigidBody_->applyCentralImpulse(toBtVector3(impulse));
}

void BulletPhysicsBody::setLinearVelocity(const glm::vec3& velocity) {
    if (rigidBody_) rigidBody_->setLinearVelocity(toBtVector3(velocity));
}

void BulletPhysicsBody::setAngularVelocity(const glm::vec3& velocity) {
    if (rigidBody_) rigidBody_->setAngularVelocity(toBtVector3(velocity));
}

glm::vec3 BulletPhysicsBody::getLinearVelocity() const {
    if (!rigidBody_) return glm::vec3(0.0f);
    return toGlmVec3(rigidBody_->getLinearVelocity());
}

glm::vec3 BulletPhysicsBody::getAngularVelocity() const {
    if (!rigidBody_) return glm::vec3(0.0f);
    return toGlmVec3(rigidBody_->getAngularVelocity());
}

void BulletPhysicsBody::activate() {
    if (rigidBody_) rigidBody_->activate();
}

bool BulletPhysicsBody::isActive() const {
    return rigidBody_ ? rigidBody_->isActive() : false;
}

void BulletPhysicsBody::setActivationState(int state) {
    if (rigidBody_) rigidBody_->setActivationState(state);
}

void BulletPhysicsBody::setKinematicTarget(const glm::vec3& position, const glm::quat& rotation) {
    if (!rigidBody_ || !isKinematic_) return;
    
    btTransform transform;
    transform.setOrigin(toBtVector3(position));
    transform.setRotation(toBtQuaternion(rotation));
    motionState_->setWorldTransform(transform);
    rigidBody_->setWorldTransform(transform);
}

void BulletPhysicsBody::setMass(float mass) {
    mass_ = mass;
    if (rigidBody_ && shape_) {
        btVector3 inertia;
        shape_->calculateLocalInertia(mass_, inertia);
        rigidBody_->setMassProps(mass_, inertia);
    }
}

float BulletPhysicsBody::getMass() const {
    return mass_;
}

void BulletPhysicsBody::setRestitution(float restitution) {
    if (rigidBody_) rigidBody_->setRestitution(restitution);
}

void BulletPhysicsBody::setFriction(float friction) {
    if (rigidBody_) rigidBody_->setFriction(friction);
}

void BulletPhysicsBody::setUseGravity(bool useGravity) {
    useGravity_ = useGravity;
    if (!rigidBody_) return;
    
    if (useGravity) {
        // Clear the disable world gravity flag
        rigidBody_->setFlags(rigidBody_->getFlags() & ~BT_DISABLE_WORLD_GRAVITY);
        
        // If custom gravity is set, apply it; otherwise Bullet uses world gravity
        if (customGravity_.x != 0.0f || customGravity_.y != 0.0f || customGravity_.z != 0.0f) {
            rigidBody_->setGravity(toBtVector3(customGravity_));
        }
    } else {
        // Disable world gravity and set zero gravity
        rigidBody_->setFlags(rigidBody_->getFlags() | BT_DISABLE_WORLD_GRAVITY);
        rigidBody_->setGravity(btVector3(0, 0, 0));
    }
}

bool BulletPhysicsBody::getUseGravity() const {
    return useGravity_;
}

void BulletPhysicsBody::setCustomGravity(const glm::vec3& gravity) {
    customGravity_ = gravity;
    if (rigidBody_ && useGravity_) {
        if (gravity.x != 0.0f || gravity.y != 0.0f || gravity.z != 0.0f) {
            rigidBody_->setFlags(rigidBody_->getFlags() | BT_DISABLE_WORLD_GRAVITY);
            rigidBody_->setGravity(toBtVector3(gravity));
        } else {
            // Zero gravity means use world gravity
            rigidBody_->setFlags(rigidBody_->getFlags() & ~BT_DISABLE_WORLD_GRAVITY);
        }
    }
}

glm::vec3 BulletPhysicsBody::getCustomGravity() const {
    return customGravity_;
}

// ============================================================================
// Bullet Physics World Implementation
// ============================================================================

BulletPhysicsWorld::BulletPhysicsWorld() = default;

BulletPhysicsWorld::~BulletPhysicsWorld() {
    shutdown();
}

bool BulletPhysicsWorld::initialize() {
    if (initialized_) return true;
    
    collisionConfig_ = std::make_unique<btDefaultCollisionConfiguration>();
    dispatcher_ = std::make_unique<btCollisionDispatcher>(collisionConfig_.get());
    broadphase_ = std::make_unique<btDbvtBroadphase>();
    solver_ = std::make_unique<btSequentialImpulseConstraintSolver>();
    dynamicsWorld_ = std::make_unique<btDiscreteDynamicsWorld>(
        dispatcher_.get(), 
        broadphase_.get(), 
        solver_.get(), 
        collisionConfig_.get()
    );
    
    dynamicsWorld_->setGravity(btVector3(0, -9.81f, 0));
    
    initialized_ = true;
    std::cout << "[Bullet] Physics world initialized" << std::endl;
    return true;
}

void BulletPhysicsWorld::shutdown() {
    if (!initialized_) return;
    
    // Remove all bodies
    for (auto& body : bodies_) {
        if (body && body->getRigidBody()) {
            dynamicsWorld_->removeRigidBody(body->getRigidBody());
        }
    }
    
    bodies_.clear();
    modelToBodyMap_.clear();
    
    dynamicsWorld_.reset();
    solver_.reset();
    broadphase_.reset();
    dispatcher_.reset();
    collisionConfig_.reset();
    
    initialized_ = false;
    std::cout << "[Bullet] Physics world shutdown" << std::endl;
}

void BulletPhysicsWorld::stepSimulation(float deltaTime, int maxSubSteps) {
    if (!initialized_ || !dynamicsWorld_ || paused_) return;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    constexpr float fixedTimeStep = 1.0f / 60.0f;
    dynamicsWorld_->stepSimulation(deltaTime, maxSubSteps, fixedTimeStep);
    
    auto end = std::chrono::high_resolution_clock::now();
    lastStats_.lastStepTimeMs = std::chrono::duration<float, std::milli>(end - start).count();
    
    updateStats();
}

void BulletPhysicsWorld::syncAllTransforms() {
    for (auto& body : bodies_) {
        if (body) {
            body->syncTransformFromPhysics();
        }
    }
}

IPhysicsBody* BulletPhysicsWorld::createPhysicsBody(Model* model, const PhysicsBodyConfig& config) {
    if (!initialized_ || !model) return nullptr;
    
    // Remove existing body if any
    removePhysicsBodyForModel(model);
    
    auto body = std::make_unique<BulletPhysicsBody>(this, model);
    if (!body->initialize(config)) {
        return nullptr;
    }
    
    IPhysicsBody* bodyPtr = body.get();
    modelToBodyMap_[model] = body.get();
    bodies_.push_back(std::move(body));
    
    return bodyPtr;
}

void BulletPhysicsWorld::removePhysicsBody(IPhysicsBody* body) {
    if (!body) return;
    
    auto it = std::find_if(bodies_.begin(), bodies_.end(),
        [body](const std::unique_ptr<BulletPhysicsBody>& b) { 
            return b.get() == static_cast<BulletPhysicsBody*>(body); 
        });
    
    if (it != bodies_.end()) {
        if ((*it)->getModel()) {
            modelToBodyMap_.erase((*it)->getModel());
        }
        bodies_.erase(it);
    }
}

void BulletPhysicsWorld::removePhysicsBodyForModel(Model* model) {
    if (!model) return;
    
    auto it = modelToBodyMap_.find(model);
    if (it != modelToBodyMap_.end()) {
        removePhysicsBody(it->second);
    }
}

IPhysicsBody* BulletPhysicsWorld::getBodyForModel(Model* model) {
    if (!model) return nullptr;
    
    auto it = modelToBodyMap_.find(model);
    if (it != modelToBodyMap_.end()) {
        return it->second;
    }
    return nullptr;
}

RaycastHit BulletPhysicsWorld::raycast(const glm::vec3& from, const glm::vec3& to) const {
    RaycastHit result;
    result.hit = false;
    
    if (!initialized_ || !dynamicsWorld_) return result;
    
    btVector3 btFrom = toBtVector3(from);
    btVector3 btTo = toBtVector3(to);
    
    btCollisionWorld::ClosestRayResultCallback rayCallback(btFrom, btTo);
    dynamicsWorld_->rayTest(btFrom, btTo, rayCallback);
    
    if (rayCallback.hasHit()) {
        result.hit = true;
        result.point = toGlmVec3(rayCallback.m_hitPointWorld);
        result.normal = toGlmVec3(rayCallback.m_hitNormalWorld);
        result.distance = glm::length(to - from) * rayCallback.m_closestHitFraction;
        result.userData = const_cast<void*>(static_cast<const void*>(rayCallback.m_collisionObject));
    }
    
    return result;
}

std::vector<RaycastHit> BulletPhysicsWorld::raycastAll(const glm::vec3& from, const glm::vec3& to) const {
    std::vector<RaycastHit> results;
    
    if (!initialized_ || !dynamicsWorld_) return results;
    
    btVector3 btFrom = toBtVector3(from);
    btVector3 btTo = toBtVector3(to);
    
    btCollisionWorld::AllHitsRayResultCallback rayCallback(btFrom, btTo);
    dynamicsWorld_->rayTest(btFrom, btTo, rayCallback);
    
    for (int i = 0; i < rayCallback.m_collisionObjects.size(); ++i) {
        RaycastHit hit;
        hit.hit = true;
        hit.point = toGlmVec3(rayCallback.m_hitPointWorld[i]);
        hit.normal = toGlmVec3(rayCallback.m_hitNormalWorld[i]);
        hit.distance = glm::length(to - from) * rayCallback.m_hitFractions[i];
        hit.userData = const_cast<void*>(static_cast<const void*>(rayCallback.m_collisionObjects[i]));
        results.push_back(hit);
    }
    
    return results;
}

void BulletPhysicsWorld::setGravity(const glm::vec3& gravity) {
    if (dynamicsWorld_) {
        dynamicsWorld_->setGravity(toBtVector3(gravity));
    }
}

glm::vec3 BulletPhysicsWorld::getGravity() const {
    if (dynamicsWorld_) {
        return toGlmVec3(dynamicsWorld_->getGravity());
    }
    return glm::vec3(0.0f, -9.81f, 0.0f);
}

void BulletPhysicsWorld::updateStats() const {
    if (!dynamicsWorld_) return;
    
    lastStats_.totalBodyCount = static_cast<int>(bodies_.size());
    lastStats_.activeBodyCount = 0;
    for (const auto& body : bodies_) {
        if (body && body->isActive()) {
            lastStats_.activeBodyCount++;
        }
    }
    lastStats_.collisionPairs = dynamicsWorld_->getNumCollisionObjects();
    lastStats_.backendName = "Bullet Physics";
}

PhysicsStats BulletPhysicsWorld::getStats() const {
    return lastStats_;
}

} // namespace motive
