#!/usr/bin/env python3
"""
Test script to verify camera context menu functionality.
This script checks that the shell.cpp file has been updated correctly
to handle right-click context menus for camera items in the hierarchy.
"""

import os
import re

def check_camera_context_menu():
    """Check if shell.cpp has camera context menu handling."""
    shell_cpp_path = os.path.join(os.path.dirname(__file__), 'shell.cpp')
    
    if not os.path.exists(shell_cpp_path):
        print(f"❌ shell.cpp not found at {shell_cpp_path}")
        return False
    
    with open(shell_cpp_path, 'r') as f:
        content = f.read()
    
    # Check for the key components of our implementation
    checks = [
        # Check for camera data extraction
        (r'const int nodeType = item \? item->data\(0, Qt::UserRole \+ 3\)\.toInt\(\) : -1;', 
         "Camera node type extraction"),
        
        (r'const int cameraIndex = item \? item->data\(0, Qt::UserRole \+ 5\)\.toInt\(\) : -1;',
         "Camera index extraction"),
        
        (r'const QString cameraId = item \? item->data\(0, Qt::UserRole \+ 6\)\.toString\(\) : QString\(\);',
         "Camera ID extraction"),
        
        # Check for camera type detection
        (r'const bool isCamera = \(nodeType == static_cast<int>\(ViewportHostWidget::HierarchyNode::Type::Camera\)\);',
         "Camera type detection"),
        
        # Check for camera context menu section
        (r'if \(isCamera\)\s*{\s*// Camera context menu',
         "Camera context menu section"),
        
        # Check for camera menu actions
        (r'QAction\* renameAction = menu\.addAction\(QStringLiteral\("Rename"\)\);',
         "Camera rename action"),
        
        (r'QAction\* deleteAction = menu\.addAction\(QStringLiteral\("Delete"\)\);',
         "Camera delete action"),
    ]
    
    all_passed = True
    for pattern, description in checks:
        if re.search(pattern, content):
            print(f"✅ {description}")
        else:
            print(f"❌ {description} not found")
            all_passed = False
    
    # Check that we handle both cameraIndex and cameraId
    if 'cameraIndex >= 0 && cameraIndex < configs.size()' in content:
        print("✅ Camera index validation")
    else:
        print("❌ Camera index validation not found")
        all_passed = False
    
    if 'configs[i].id == cameraId' in content:
        print("✅ Camera ID lookup")
    else:
        print("❌ Camera ID lookup not found")
        all_passed = False
    
    return all_passed

def main():
    print("🔍 Testing Camera Context Menu Implementation")
    print("=" * 50)
    
    if check_camera_context_menu():
        print("\n✅ All checks passed! Camera context menu should work correctly.")
        print("\nTo test manually:")
        print("1. Run ./motive_editor")
        print("2. Load a scene with models")
        print("3. Create a follow camera (right-click on a model → Follow → Create Follow Cam)")
        print("4. Right-click on the camera in the hierarchy tree")
        print("5. Verify 'Rename' and 'Delete' options appear")
    else:
        print("\n❌ Some checks failed. Camera context menu may not work correctly.")
        return 1
    
    return 0

if __name__ == '__main__':
    exit(main())