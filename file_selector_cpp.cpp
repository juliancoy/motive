#include "file_selector_cpp.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <set>

namespace motive {

// ============================================================================
// FilePair Implementation
// ============================================================================

FilePair::FilePair(const std::string& video, const std::string& npy) 
    : videoPath(video), npyPath(npy) {
    if (!video.empty()) {
        baseName = std::filesystem::path(video).stem().string();
    } else if (!npy.empty()) {
        baseName = std::filesystem::path(npy).stem().string();
        // Remove _tribe suffix if present
        if (baseName.size() > 6 && baseName.substr(baseName.size() - 6) == "_tribe") {
            baseName = baseName.substr(0, baseName.size() - 6);
        }
    }
}

std::string FilePair::getStatus() const {
    if (isComplete()) return "Complete";
    if (!videoPath.empty()) return "Missing NPY";
    if (!npyPath.empty()) return "Missing Video";
    return "Empty";
}

// ============================================================================
// FileSelectorUI Implementation
// ============================================================================

FileSelectorUI::FileSelectorUI() {
    currentDir_ = std::filesystem::current_path();
    scanDirectory();
}

FileSelectorUI::~FileSelectorUI() = default;

void FileSelectorUI::setDirectory(const std::string& path) {
    std::filesystem::path p(path);
    if (std::filesystem::exists(p) && std::filesystem::is_directory(p)) {
        currentDir_ = std::filesystem::canonical(p);
        scanDirectory();
    }
}

void FileSelectorUI::scanDirectory() {
    filePairs_.clear();
    subdirectories_.clear();
    
    if (!std::filesystem::exists(currentDir_)) {
        return;
    }
    
    // Collect video and brain files
    std::map<std::string, std::string> videos;
    std::map<std::string, std::string> brains;
    
    for (const auto& entry : std::filesystem::directory_iterator(currentDir_)) {
        if (!entry.is_regular_file()) {
            if (entry.is_directory()) {
                subdirectories_.push_back(entry.path());
            }
            continue;
        }
        
        const auto& path = entry.path();
        std::string ext = path.extension().string();
        std::string stem = path.stem().string();
        
        // Convert to lowercase for comparison
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        // Check if it's a video file
        for (const auto& vidExt : VIDEO_EXTENSIONS) {
            if (ext == vidExt) {
                videos[stem] = path.string();
                break;
            }
        }
        
        // Check if it's a brain data file
        for (const auto& brainExt : BRAIN_EXTENSIONS) {
            if (ext == brainExt) {
                // Remove _tribe suffix for matching
                std::string base = stem;
                if (base.size() > 6 && base.substr(base.size() - 6) == "_tribe") {
                    base = base.substr(0, base.size() - 6);
                }
                brains[base] = path.string();
                break;
            }
        }
    }
    
    // Match pairs
    std::set<std::string> allBases;
    for (const auto& [base, _] : videos) allBases.insert(base);
    for (const auto& [base, _] : brains) allBases.insert(base);
    
    for (const auto& base : allBases) {
        auto vit = videos.find(base);
        auto bit = brains.find(base);
        
        FilePair pair;
        pair.baseName = base;
        if (vit != videos.end()) pair.videoPath = vit->second;
        if (bit != brains.end()) pair.npyPath = bit->second;
        
        filePairs_.push_back(pair);
    }
    
    // Sort pairs by base name
    std::sort(filePairs_.begin(), filePairs_.end(), 
              [](const FilePair& a, const FilePair& b) {
                  return a.baseName < b.baseName;
              });
    
    // Sort subdirectories
    std::sort(subdirectories_.begin(), subdirectories_.end());
}

void FileSelectorUI::draw() {
    if (!visible_) return;
    
    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("TribeV2 Brain Viewer - File Selector", &visible_, 
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar)) {
        
        drawToolbar();
        
        // Main content area - split view
        ImGui::Columns(2, "FileSelectorColumns", true);
        
        // Left column: Directory tree
        ImGui::BeginChild("DirectoryTree", ImVec2(0, -30), true);
        drawDirectoryTree();
        ImGui::EndChild();
        
        ImGui::NextColumn();
        
        // Right column: File pairs
        ImGui::BeginChild("FilePairs", ImVec2(0, -30), true);
        drawFilePairs();
        ImGui::EndChild();
        
        ImGui::Columns(1);
        
        // Status bar
        int completeCount = 0;
        for (const auto& pair : filePairs_) {
            if (pair.isComplete()) completeCount++;
        }
        
        ImGui::Separator();
        ImGui::Text("%s | %d complete / %d total pairs", 
                    currentDir_.string().c_str(), 
                    completeCount, (int)filePairs_.size());
    }
    ImGui::End();
}

void FileSelectorUI::drawToolbar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open Directory...", "Ctrl+O")) {
                // TODO: Open native file dialog
            }
            if (ImGui::MenuItem("Refresh", "F5")) {
                scanDirectory();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Esc")) {
                visible_ = false;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    
    // Navigation bar
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));
    
    if (ImGui::Button("← Back")) {
        if (currentDir_.has_parent_path()) {
            setDirectory(currentDir_.parent_path().string());
        }
    }
    ImGui::SameLine();
    
    if (ImGui::Button("🔄 Refresh")) {
        scanDirectory();
    }
    ImGui::SameLine();
    
    // Path display
    char pathBuf[1024];
    strncpy(pathBuf, currentDir_.string().c_str(), sizeof(pathBuf) - 1);
    pathBuf[sizeof(pathBuf) - 1] = '\0';
    
    ImGui::PushItemWidth(-150);
    if (ImGui::InputText("##Path", pathBuf, sizeof(pathBuf), 
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        setDirectory(pathBuf);
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    
    // Filter
    ImGui::PushItemWidth(140);
    ImGui::InputText("##Filter", filterText_, sizeof(filterText_));
    ImGui::PopItemWidth();
    
    ImGui::PopStyleVar();
    ImGui::Separator();
}

void FileSelectorUI::drawDirectoryTree() {
    ImGui::TextUnformatted("Directories");
    ImGui::Separator();
    
    // Quick access locations
    auto drawLocation = [this](const char* name, const std::filesystem::path& path) {
        if (std::filesystem::exists(path)) {
            bool selected = (currentDir_ == path);
            if (ImGui::Selectable(name, selected)) {
                setDirectory(path.string());
            }
        }
    };
    
    drawLocation("🏠 Home", std::filesystem::path(getenv("HOME") ?: "/"));
    drawLocation("🖥️ Desktop", std::filesystem::path(getenv("HOME") ?: "/") / "Desktop");
    drawLocation("📁 Documents", std::filesystem::path(getenv("HOME") ?: "/") / "Documents");
    drawLocation("🎬 Videos", std::filesystem::path(getenv("HOME") ?: "/") / "Videos");
    
    ImGui::Separator();
    
    // Current directory subdirectories
    for (const auto& subdir : subdirectories_) {
        std::string name = "📁 " + subdir.filename().string();
        if (ImGui::Selectable(name.c_str())) {
            setDirectory(subdir.string());
        }
    }
}

void FileSelectorUI::drawFilePairs() {
    // Table header
    ImGui::TextUnformatted("Video + Brain Data Pairs");
    ImGui::Separator();
    
    // Column headers
    ImGui::Columns(4, "PairsColumns", false);
    ImGui::SetColumnWidth(0, 100);
    ImGui::SetColumnWidth(1, 250);
    ImGui::SetColumnWidth(2, 250);
    ImGui::SetColumnWidth(3, 80);
    
    ImGui::Text("Status"); ImGui::NextColumn();
    ImGui::Text("Video File"); ImGui::NextColumn();
    ImGui::Text("Brain Data"); ImGui::NextColumn();
    ImGui::Text("Size"); ImGui::NextColumn();
    ImGui::Separator();
    
    // Filter
    std::string filter = filterText_;
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
    
    // List file pairs
    for (size_t i = 0; i < filePairs_.size(); i++) {
        const auto& pair = filePairs_[i];
        
        // Apply filter
        if (!filter.empty()) {
            std::string searchText = pair.baseName;
            std::transform(searchText.begin(), searchText.end(), searchText.begin(), ::tolower);
            if (searchText.find(filter) == std::string::npos) {
                continue;
            }
        }
        
        // Status with color
        bool isComplete = pair.isComplete();
        if (isComplete) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
            ImGui::Text("✓ Complete");
        } else if (!pair.videoPath.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
            ImGui::Text("⚠ No NPY");
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
            ImGui::Text("✗ No Video");
        }
        ImGui::PopStyleColor();
        ImGui::NextColumn();
        
        // Video filename
        if (!pair.videoPath.empty()) {
            std::string vidName = std::filesystem::path(pair.videoPath).filename().string();
            ImGui::TextUnformatted(vidName.c_str());
        } else {
            ImGui::TextDisabled("-");
        }
        ImGui::NextColumn();
        
        // Brain filename
        if (!pair.npyPath.empty()) {
            std::string npyName = std::filesystem::path(pair.npyPath).filename().string();
            ImGui::TextUnformatted(npyName.c_str());
        } else {
            ImGui::TextDisabled("-");
        }
        ImGui::NextColumn();
        
        // File size (video)
        if (!pair.videoPath.empty()) {
            try {
                auto size = std::filesystem::file_size(pair.videoPath);
                float sizeMB = size / (1024.0f * 1024.0f);
                ImGui::Text("%.1f MB", sizeMB);
            } catch (...) {
                ImGui::Text("-");
            }
        } else {
            ImGui::Text("-");
        }
        ImGui::NextColumn();
        
        // Handle selection
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            if (isComplete) {
                selectedPair_ = pair;
                hasSelection_ = true;
                if (onSelect_) {
                    onSelect_(pair);
                }
            }
        }
        
        if (ImGui::IsItemClicked()) {
            selectedIndex_ = static_cast<int>(i);
            selectedPair_ = pair;
        }
        
        // Highlight selected row
        if (selectedIndex_ == static_cast<int>(i)) {
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 row_min = ImGui::GetItemRectMin();
            row_min.x = ImGui::GetWindowPos().x;
            ImVec2 row_max = ImVec2(row_min.x + ImGui::GetWindowWidth(), 
                                    row_min.y + ImGui::GetTextLineHeightWithSpacing());
            draw_list->AddRectFilled(row_min, row_max, 
                                     ImColor(ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]));
        }
    }
    
    ImGui::Columns(1);
    
    // Open button (if complete pair selected)
    if (selectedIndex_ >= 0 && selectedPair_.isComplete()) {
        ImGui::Separator();
        if (ImGui::Button("Open Selected", ImVec2(150, 30))) {
            hasSelection_ = true;
            if (onSelect_) {
                onSelect_(selectedPair_);
            }
        }
        ImGui::SameLine();
        ImGui::Text("Double-click to open");
    }
}

// ============================================================================
// Standalone Dialog
// ============================================================================

FilePair selectFilesDialog(const std::string& initialDir) {
    // This would require creating a GLFW window and running ImGui
    // For now, return empty pair - caller should use FileSelectorUI integrated
    // into their main loop
    return FilePair();
}

} // namespace motive
