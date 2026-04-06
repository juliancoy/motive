#pragma once

#include "physics_interface.h"
#include <imgui.h>
#include <string>
#include <functional>

namespace motive {

// ============================================================================
// Physics Settings UI
// ============================================================================

class PhysicsSettingsPanel {
public:
    // Called to render the settings UI
    void render(PhysicsSettings& settings, IPhysicsWorld* world);
    
    // Set callback for when physics engine type changes
    void onPhysicsEngineChanged(std::function<void(PhysicsEngineType)> callback) {
        onEngineChanged_ = callback;
    }
    
    // Set callback for when settings are applied
    void onSettingsApplied(std::function<void(const PhysicsSettings&)> callback) {
        onSettingsApplied_ = callback;
    }
    
private:
    void renderEngineSelection(PhysicsSettings& settings);
    void renderGeneralSettings(PhysicsSettings& settings);
    void renderEngineSpecificSettings(PhysicsSettings& settings);
    void renderStats(IPhysicsWorld* world);
    void renderBodiesList(IPhysicsWorld* world);
    
    std::function<void(PhysicsEngineType)> onEngineChanged_;
    std::function<void(const PhysicsSettings&)> onSettingsApplied_;
    
    // UI state
    bool showStats_ = true;
    bool showBodies_ = false;
    int selectedBodyIndex_ = -1;
};

// ============================================================================
// Inline Implementation for UI
// ============================================================================

inline void PhysicsSettingsPanel::render(PhysicsSettings& settings, IPhysicsWorld* world) {
    ImGui::Begin("Physics Settings");
    
    if (ImGui::BeginTabBar("PhysicsTabs")) {
        // General Tab
        if (ImGui::BeginTabItem("General")) {
            renderEngineSelection(settings);
            ImGui::Separator();
            renderGeneralSettings(settings);
            ImGui::EndTabItem();
        }
        
        // Engine-specific settings
        if (ImGui::BeginTabItem("Engine")) {
            renderEngineSpecificSettings(settings);
            ImGui::EndTabItem();
        }
        
        // Stats tab
        if (ImGui::BeginTabItem("Stats")) {
            renderStats(world);
            ImGui::EndTabItem();
        }
        
        // Bodies tab
        if (ImGui::BeginTabItem("Bodies")) {
            renderBodiesList(world);
            ImGui::EndTabItem();
        }
        
        ImGui::EndTabBar();
    }
    
    ImGui::End();
}

inline void PhysicsSettingsPanel::renderEngineSelection(PhysicsSettings& settings) {
    ImGui::Text("Physics Engine");
    ImGui::Spacing();
    
    auto availableBackends = PhysicsFactory::getAvailableBackends();
    
    for (auto backend : availableBackends) {
        const char* name = PhysicsFactory::getBackendName(backend);
        const char* desc = PhysicsFactory::getBackendDescription(backend);
        
        bool isSelected = (settings.engineType == backend);
        
        if (ImGui::RadioButton(name, isSelected)) {
            if (settings.engineType != backend) {
                settings.engineType = backend;
                if (onEngineChanged_) {
                    onEngineChanged_(backend);
                }
            }
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", desc);
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    
    // Current engine info
    if (world) {
        ImGui::Text("Active: %s", world->getBackendName());
        ImGui::Text("Initialized: %s", world->isInitialized() ? "Yes" : "No");
    }
}

inline void PhysicsSettingsPanel::renderGeneralSettings(PhysicsSettings& settings) {
    ImGui::Text("Simulation Settings");
    ImGui::Spacing();
    
    // Gravity
    float gravity[3] = { settings.gravity.x, settings.gravity.y, settings.gravity.z };
    if (ImGui::DragFloat3("Gravity", gravity, 0.1f, -50.0f, 50.0f, "%.2f")) {
        settings.gravity = glm::vec3(gravity[0], gravity[1], gravity[2]);
        if (auto* world = dynamic_cast<IPhysicsWorld*>(ImGui::GetIO().UserData)) {
            // Note: This requires proper handling
        }
    }
    
    // Time step
    ImGui::SliderFloat("Fixed Time Step", &settings.fixedTimeStep, 1.0f/120.0f, 1.0f/30.0f, "%.4f");
    ImGui::SliderInt("Max Substeps", &settings.maxSubSteps, 1, 10);
    
    // Options
    ImGui::Checkbox("Auto Sync Transforms", &settings.autoSync);
    ImGui::Checkbox("Debug Draw", &settings.debugDraw);
    
    ImGui::Spacing();
    if (ImGui::Button("Apply Settings", ImVec2(120, 0))) {
        if (onSettingsApplied_) {
            onSettingsApplied_(settings);
        }
    }
}

inline void PhysicsSettingsPanel::renderEngineSpecificSettings(PhysicsSettings& settings) {
    switch (settings.engineType) {
        case PhysicsEngineType::Bullet:
            ImGui::Text("Bullet Physics Settings");
            ImGui::TextDisabled("No additional settings available.");
            break;
            
        case PhysicsEngineType::Jolt:
            ImGui::Text("Jolt Physics Settings");
            ImGui::InputInt("Max Bodies", &settings.joltMaxBodies);
            ImGui::InputInt("Max Body Pairs", &settings.joltMaxBodyPairs);
            ImGui::InputInt("Max Contact Constraints", &settings.joltMaxContactConstraints);
            break;
            
        case PhysicsEngineType::BuiltIn:
            ImGui::Text("Built-in GPU Physics Settings");
            ImGui::SliderInt("Solver Iterations", &settings.builtinSolverIterations, 1, 50);
            ImGui::SliderFloat("Collision Margin", &settings.builtinCollisionMargin, 0.0f, 0.1f, "%.4f");
            break;
            
        default:
            ImGui::Text("Unknown physics engine");
            break;
    }
}

inline void PhysicsSettingsPanel::renderStats(IPhysicsWorld* world) {
    if (!world || !world->isInitialized()) {
        ImGui::TextDisabled("Physics world not initialized");
        return;
    }
    
    auto stats = world->getStats();
    
    ImGui::Text("Performance");
    ImGui::Separator();
    ImGui::Text("Last Step Time: %.3f ms", stats.lastStepTimeMs);
    
    ImGui::Spacing();
    ImGui::Text("Bodies");
    ImGui::Separator();
    ImGui::Text("Total: %d", stats.totalBodyCount);
    ImGui::Text("Active: %d", stats.activeBodyCount);
    
    ImGui::Spacing();
    ImGui::Text("Collisions");
    ImGui::Separator();
    ImGui::Text("Collision Objects: %d", stats.collisionPairs);
}

inline void PhysicsSettingsPanel::renderBodiesList(IPhysicsWorld* world) {
    if (!world || !world->isInitialized()) {
        ImGui::TextDisabled("Physics world not initialized");
        return;
    }
    
    ImGui::Text("Physics Bodies");
    ImGui::Separator();
    
    // Placeholder for body list
    ImGui::TextDisabled("Body list not yet implemented");
    
    // This would iterate through all bodies and show them in a list
    // with details panel on selection
}

} // namespace motive
