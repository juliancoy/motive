#pragma once

#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include <memory>

// Forward declarations for ImGui
struct ImGuiContext;

namespace motive {

/**
 * FilePair represents a matched video and brain data file.
 * Follows the naming convention: video.mp4 -> video_tribe.npy
 */
struct FilePair {
    std::string videoPath;
    std::string npyPath;
    std::string baseName;
    
    FilePair() = default;
    FilePair(const std::string& video, const std::string& npy);
    
    bool isComplete() const { return !videoPath.empty() && !npyPath.empty(); }
    std::string getStatus() const;
};

/**
 * FileSelectorUI - Dear ImGui-based file browser for selecting video/brain pairs.
 * 
 * Layout:
 *   +------------------+------------------+
 *   |  Directory Tree  |  File Pairs List |
 *   |  (Left Sidebar)  |  (Main Content)  |
 *   +------------------+------------------+
 *   |  Status: X pairs found              |
 *   +-------------------------------------+
 */
class FileSelectorUI {
public:
    using SelectionCallback = std::function<void(const FilePair&)>;
    
    FileSelectorUI();
    ~FileSelectorUI();
    
    // Set callback when a pair is selected
    void setCallback(SelectionCallback callback) { onSelect_ = callback; }
    
    // Set initial directory
    void setDirectory(const std::string& path);
    
    // Draw the file selector UI - call every frame
    void draw();
    
    // Returns true if a selection has been made
    bool hasSelection() const { return hasSelection_; }
    
    // Get the selected pair
    const FilePair& getSelectedPair() const { return selectedPair_; }
    
    // Reset selection state
    void resetSelection() { hasSelection_ = false; }
    
    // Show/hide the selector
    void show() { visible_ = true; }
    void hide() { visible_ = false; }
    bool isVisible() const { return visible_; }
    
private:
    void scanDirectory();
    void drawDirectoryTree();
    void drawFilePairs();
    void drawToolbar();
    
    std::filesystem::path currentDir_;
    std::vector<FilePair> filePairs_;
    std::vector<std::filesystem::path> subdirectories_;
    
    // UI state
    bool visible_ = true;
    bool hasSelection_ = false;
    FilePair selectedPair_;
    int selectedIndex_ = -1;
    
    // Filter
    char filterText_[256] = "";
    
    // Callback
    SelectionCallback onSelect_;
    
    // Constants
    static constexpr const char* VIDEO_EXTENSIONS[] = {".mp4", ".avi", ".mkv", ".mov", ".webm", ".m4v"};
    static constexpr const char* BRAIN_EXTENSIONS[] = {".npy"};
};

/**
 * Standalone file selector that creates its own window.
 * Returns the selected pair or empty if cancelled.
 */
FilePair selectFilesDialog(const std::string& initialDir = "");

} // namespace motive
