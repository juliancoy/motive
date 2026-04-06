#pragma once

#include "physics_interface.h"

// Built-in GPU Physics implementation header
// This uses Vulkan compute shaders for physics simulation
// Currently a stub - full implementation would require compute shader pipeline

#ifdef MOTIVE_BUILTIN_AVAILABLE

// Full implementation would be here
// This requires Vulkan compute pipeline setup

namespace motive {

// Compute shader-based physics world
class BuiltInPhysicsWorld : public IPhysicsWorld {
public:
    BuiltInPhysicsWorld();
    ~BuiltInPhysicsWorld() override;
    
    bool initialize() override;
    void shutdown() override;
    bool isInitialized() const override;
    
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
    PhysicsEngineType getType() const override { return PhysicsEngineType::BuiltIn; }
    const char* getBackendName() const override { return "Built-in GPU Physics"; }
    
private:
    bool initialized_ = false;
    // Vulkan compute resources would go here
};

} // namespace motive

#else // !MOTIVE_BUILTIN_AVAILABLE

// Stub implementation when built-in is not available
namespace motive {

class BuiltInPhysicsWorld : public IPhysicsWorld {
public:
    bool initialize() override { 
        std::cerr << "[Physics] Built-in GPU physics not compiled in. Rebuild with MOTIVE_BUILTIN_AVAILABLE." << std::endl;
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
    PhysicsEngineType getType() const override { return PhysicsEngineType::BuiltIn; }
    const char* getBackendName() const override { return "Built-in GPU Physics (Not Available)"; }
};

} // namespace motive

#endif // MOTIVE_BUILTIN_AVAILABLE
