#pragma once

#include "physics_interface.h"
#include <memory>
#include <vector>

namespace motive {

// ============================================================================
// Physics Factory
// ============================================================================

class PhysicsFactory {
public:
    // Create physics world for specified backend
    static std::unique_ptr<IPhysicsWorld> createWorld(PhysicsEngineType type);
    
    // Check if backend is available/compiled
    static bool isBackendAvailable(PhysicsEngineType type);
    
    // Get list of available backends
    static std::vector<PhysicsEngineType> getAvailableBackends();
    
    // Get backend info
    static const char* getBackendName(PhysicsEngineType type);
    static const char* getBackendDescription(PhysicsEngineType type);
};

} // namespace motive
