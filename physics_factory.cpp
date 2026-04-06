#include "physics_interface.h"
#include "physics_factory.h"
#include "physics_bullet.h"

// Always include Jolt header (it auto-detects availability)
#include "physics_jolt.h"

#ifdef MOTIVE_BUILTIN_AVAILABLE
#include "physics_builtin.h"
#endif

#include <vector>
#include <cstring>
#include <iostream>
#include <filesystem>

namespace motive {

// ============================================================================
// Physics Engine Type Utilities
// ============================================================================

const char* physicsEngineTypeToString(PhysicsEngineType type) {
    switch (type) {
        case PhysicsEngineType::Bullet: return "Bullet";
        case PhysicsEngineType::Jolt: return "Jolt";
        case PhysicsEngineType::BuiltIn: return "BuiltIn";
        default: return "Unknown";
    }
}

PhysicsEngineType physicsEngineTypeFromString(const std::string& str) {
    if (str == "Bullet" || str == "bullet") return PhysicsEngineType::Bullet;
    if (str == "Jolt" || str == "jolt") return PhysicsEngineType::Jolt;
    if (str == "BuiltIn" || str == "builtin" || str == "Built-in" || str == "built-in") return PhysicsEngineType::BuiltIn;
    return PhysicsEngineType::Bullet;  // Default
}

// ============================================================================
// Animation-Physics Coupling Mode Utilities
// ============================================================================

const char* animationPhysicsCouplingModeToString(AnimationPhysicsCouplingMode mode) {
    switch (mode) {
        case AnimationPhysicsCouplingMode::AnimationOnly: return "AnimationOnly";
        case AnimationPhysicsCouplingMode::Kinematic: return "Kinematic";
        case AnimationPhysicsCouplingMode::RootMotionPhysics: return "RootMotionPhysics";
        case AnimationPhysicsCouplingMode::PhysicsDriven: return "PhysicsDriven";
        case AnimationPhysicsCouplingMode::Ragdoll: return "Ragdoll";
        case AnimationPhysicsCouplingMode::PartialRagdoll: return "PartialRagdoll";
        case AnimationPhysicsCouplingMode::ActiveRagdoll: return "ActiveRagdoll";
        default: return "Unknown";
    }
}

AnimationPhysicsCouplingMode animationPhysicsCouplingModeFromString(const std::string& str) {
    if (str == "AnimationOnly" || str == "Animation Only" || str == "animation_only") 
        return AnimationPhysicsCouplingMode::AnimationOnly;
    if (str == "Kinematic" || str == "kinematic") 
        return AnimationPhysicsCouplingMode::Kinematic;
    if (str == "RootMotionPhysics" || str == "Root Motion + Physics" || str == "root_motion_physics") 
        return AnimationPhysicsCouplingMode::RootMotionPhysics;
    if (str == "PhysicsDriven" || str == "Physics Driven" || str == "physics_driven") 
        return AnimationPhysicsCouplingMode::PhysicsDriven;
    if (str == "Ragdoll" || str == "ragdoll") 
        return AnimationPhysicsCouplingMode::Ragdoll;
    if (str == "PartialRagdoll" || str == "Partial Ragdoll" || str == "partial_ragdoll") 
        return AnimationPhysicsCouplingMode::PartialRagdoll;
    if (str == "ActiveRagdoll" || str == "Active Ragdoll" || str == "active_ragdoll") 
        return AnimationPhysicsCouplingMode::ActiveRagdoll;
    return AnimationPhysicsCouplingMode::AnimationOnly;  // Default
}

const char* animationPhysicsCouplingModeDisplayName(AnimationPhysicsCouplingMode mode) {
    switch (mode) {
        case AnimationPhysicsCouplingMode::AnimationOnly: return "Animation Only";
        case AnimationPhysicsCouplingMode::Kinematic: return "Kinematic";
        case AnimationPhysicsCouplingMode::RootMotionPhysics: return "Root Motion + Physics";
        case AnimationPhysicsCouplingMode::PhysicsDriven: return "Physics Driven";
        case AnimationPhysicsCouplingMode::Ragdoll: return "Ragdoll";
        case AnimationPhysicsCouplingMode::PartialRagdoll: return "Partial Ragdoll";
        case AnimationPhysicsCouplingMode::ActiveRagdoll: return "Active Ragdoll";
        default: return "Unknown";
    }
}

const char* animationPhysicsCouplingModeDescription(AnimationPhysicsCouplingMode mode) {
    switch (mode) {
        case AnimationPhysicsCouplingMode::AnimationOnly: 
            return "Animation drives transform, no physics interaction";
        case AnimationPhysicsCouplingMode::Kinematic: 
            return "Animation drives physics body, collides but not affected by forces";
        case AnimationPhysicsCouplingMode::RootMotionPhysics: 
            return "Root motion extracted from animation, physics handles details";
        case AnimationPhysicsCouplingMode::PhysicsDriven: 
            return "Physics drives transform, animation may provide visual pose";
        case AnimationPhysicsCouplingMode::Ragdoll: 
            return "Full physics simulation, animation disabled";
        case AnimationPhysicsCouplingMode::PartialRagdoll: 
            return "Some joints physics-driven, others animation-driven";
        case AnimationPhysicsCouplingMode::ActiveRagdoll: 
            return "Animation influences physics-driven body for natural reactions";
        default: return "";
    }
}

// ============================================================================
// Physics Factory Implementation
// ============================================================================

std::unique_ptr<IPhysicsWorld> PhysicsFactory::createWorld(PhysicsEngineType type) {
    switch (type) {
        case PhysicsEngineType::Bullet:
            return std::make_unique<BulletPhysicsWorld>();
            
        case PhysicsEngineType::Jolt:
            if (isBackendAvailable(PhysicsEngineType::Jolt)) {
                return std::make_unique<JoltPhysicsWorld>();
            } else {
                std::cerr << "[Physics] Jolt not available, falling back to Bullet" << std::endl;
                return std::make_unique<BulletPhysicsWorld>();
            }
            
        case PhysicsEngineType::BuiltIn:
#ifdef MOTIVE_BUILTIN_AVAILABLE
            return std::make_unique<BuiltInPhysicsWorld>();
#else
            std::cerr << "[Physics] Built-in not available, falling back to Bullet" << std::endl;
            return std::make_unique<BulletPhysicsWorld>();
#endif
            
        default:
            return std::make_unique<BulletPhysicsWorld>();
    }
}

bool PhysicsFactory::isBackendAvailable(PhysicsEngineType type) {
    switch (type) {
        case PhysicsEngineType::Bullet:
            return true;  // Always available
            
        case PhysicsEngineType::Jolt:
            // Check if Jolt library exists
            return std::filesystem::exists("jolt/Build/Linux_Release/libJolt.a") ||
                   std::filesystem::exists("../jolt/Build/Linux_Release/libJolt.a");
            
        case PhysicsEngineType::BuiltIn:
#ifdef MOTIVE_BUILTIN_AVAILABLE
            return true;
#else
            return false;
#endif
            
        default:
            return false;
    }
}

std::vector<PhysicsEngineType> PhysicsFactory::getAvailableBackends() {
    std::vector<PhysicsEngineType> backends;
    
    backends.push_back(PhysicsEngineType::Bullet);
    
    // Jolt is available if library was built
    if (isBackendAvailable(PhysicsEngineType::Jolt)) {
        backends.push_back(PhysicsEngineType::Jolt);
    }

#ifdef MOTIVE_BUILTIN_AVAILABLE
    backends.push_back(PhysicsEngineType::BuiltIn);
#endif
    
    return backends;
}

const char* PhysicsFactory::getBackendName(PhysicsEngineType type) {
    switch (type) {
        case PhysicsEngineType::Bullet: return "Bullet Physics";
        case PhysicsEngineType::Jolt: return "Jolt Physics";
        case PhysicsEngineType::BuiltIn: return "Built-in GPU Physics";
        default: return "Unknown";
    }
}

const char* PhysicsFactory::getBackendDescription(PhysicsEngineType type) {
    switch (type) {
        case PhysicsEngineType::Bullet:
            return "Mature, stable CPU physics. Best compatibility.";
        case PhysicsEngineType::Jolt:
            return "Modern, fast, multi-threaded CPU physics. Best performance.";
        case PhysicsEngineType::BuiltIn:
            return "GPU-native compute shader physics (experimental).";
        default:
            return "";
    }
}

} // namespace motive
