cmake_minimum_required(VERSION 3.22)

project(Motive LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MOTIVE_ENABLE_ASAN "Build with AddressSanitizer" OFF)
option(MOTIVE_ENABLE_VALIDATION "Enable Vulkan validation layers" ON)
option(MOTIVE_ENABLE_TESTS "Configure and build tests in tests/" OFF)

if(MOTIVE_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address -fno-omit-frame-pointer)
endif()

if(NOT MOTIVE_ENABLE_VALIDATION)
    add_compile_definitions(MOTIVE_DISABLE_VALIDATION_LAYERS=1)
endif()

set(MOTIVE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(MOTIVE_FFMPEG_DIR "${MOTIVE_ROOT}/FFmpeg/.build/install")
set(MOTIVE_SHADER_DIR "${MOTIVE_ROOT}/shaders")

find_package(Threads REQUIRED)
find_package(OpenMP QUIET COMPONENTS CXX)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
find_program(GLSLANG_VALIDATOR NAMES glslangValidator REQUIRED)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/CPPMonetize/CMakeLists.txt")
    add_subdirectory(external/CPPMonetize EXCLUDE_FROM_ALL)
endif()

set(MOTIVE_AUTH_API_BASE_URL "https://jsynth.us/api" CACHE STRING "Authentication API base URL used by the editor login dialog")
add_compile_definitions("MOTIVE_AUTH_API_BASE_URL=\"${MOTIVE_AUTH_API_BASE_URL}\"")

function(motive_find_library out_var)
    set(options REQUIRED)
    set(oneValueArgs)
    set(multiValueArgs NAMES PATHS)
    cmake_parse_arguments(MFL "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    find_library(${out_var}
        NAMES ${MFL_NAMES}
        PATHS ${MFL_PATHS}
    )

    if(NOT ${out_var} AND MFL_REQUIRED)
        message(FATAL_ERROR "Missing required library: ${MFL_NAMES}")
    endif()
endfunction()

set(MOTIVE_INCLUDE_DIRS
    "${MOTIVE_ROOT}/Vulkan-Headers/include"
    "${MOTIVE_ROOT}/glfw/include"
    "${MOTIVE_ROOT}/tinygltf"
    "${MOTIVE_ROOT}/tinygltf/examples/common/imgui"
    "${MOTIVE_ROOT}/imgui"
    "${MOTIVE_ROOT}/glm"
    "${MOTIVE_FFMPEG_DIR}/include"
    "${MOTIVE_ROOT}/freetype/include"
    "${MOTIVE_ROOT}/freetype/build/include"
    "${MOTIVE_ROOT}/freetype/build/include/freetype2"
    "${MOTIVE_ROOT}/common_vv/include"
    "${MOTIVE_ROOT}/common_vv/libs"
    "${MOTIVE_ROOT}/vk_video_decoder/include"
    "${MOTIVE_ROOT}/vk_video_decoder/libs"
    "${MOTIVE_ROOT}/ufbx"
    "${MOTIVE_ROOT}/bullet3/src"
    "${MOTIVE_ROOT}/jolt"
    "${MOTIVE_ROOT}/jolt/Build/Linux_Release"
)

set(MOTIVE_LIBRARY_DIRS
    "${MOTIVE_ROOT}/Vulkan-Headers/lib"
    "${MOTIVE_ROOT}/glfw/build/src"
    "${MOTIVE_FFMPEG_DIR}/lib"
    "${MOTIVE_ROOT}/freetype/build"
    "${MOTIVE_ROOT}/ncnn/build/src"
    "${MOTIVE_ROOT}/ncnn/build/glslang/glslang"
    "${MOTIVE_ROOT}"
    "${MOTIVE_ROOT}/bullet3/build/lib"
    "${MOTIVE_ROOT}/jolt/Build/Linux_Release"
)

motive_find_library(MOTIVE_GLFW_LIBRARY REQUIRED
    NAMES glfw3
    PATHS "${MOTIVE_ROOT}/glfw/build/src"
)
motive_find_library(MOTIVE_TINYGLTF_LIBRARY REQUIRED
    NAMES tinygltf
    PATHS "${MOTIVE_ROOT}"
)
motive_find_library(MOTIVE_VULKAN_LIBRARY REQUIRED
    NAMES vulkan
    PATHS "${MOTIVE_ROOT}/Vulkan-Headers/lib"
)
motive_find_library(MOTIVE_AVFORMAT_LIBRARY REQUIRED NAMES avformat PATHS "${MOTIVE_FFMPEG_DIR}/lib")
motive_find_library(MOTIVE_AVCODEC_LIBRARY REQUIRED NAMES avcodec PATHS "${MOTIVE_FFMPEG_DIR}/lib")
motive_find_library(MOTIVE_SWSCALE_LIBRARY REQUIRED NAMES swscale PATHS "${MOTIVE_FFMPEG_DIR}/lib")
motive_find_library(MOTIVE_AVUTIL_LIBRARY REQUIRED NAMES avutil PATHS "${MOTIVE_FFMPEG_DIR}/lib")
motive_find_library(MOTIVE_SWRESAMPLE_LIBRARY REQUIRED NAMES swresample PATHS "${MOTIVE_FFMPEG_DIR}/lib")
motive_find_library(MOTIVE_FREETYPE_LIBRARY REQUIRED NAMES freetype PATHS "${MOTIVE_ROOT}/freetype/build")
motive_find_library(MOTIVE_NCNN_LIBRARY REQUIRED NAMES ncnn PATHS "${MOTIVE_ROOT}/ncnn/build/src")
motive_find_library(MOTIVE_GLSLANG_LIBRARY REQUIRED NAMES glslang PATHS "${MOTIVE_ROOT}/ncnn/build/glslang/glslang")
motive_find_library(MOTIVE_GLSLANG_LIMITS_LIBRARY REQUIRED NAMES glslang-default-resource-limits PATHS "${MOTIVE_ROOT}/ncnn/build/glslang/glslang")

# Bullet Physics libraries
motive_find_library(MOTIVE_BULLET_DYNAMICS_LIBRARY REQUIRED NAMES BulletDynamics 
    PATHS "${MOTIVE_ROOT}/bullet3/build/src/BulletDynamics")
motive_find_library(MOTIVE_BULLET_COLLISION_LIBRARY REQUIRED NAMES BulletCollision 
    PATHS "${MOTIVE_ROOT}/bullet3/build/src/BulletCollision")
motive_find_library(MOTIVE_BULLET_LINEAR_MATH_LIBRARY REQUIRED NAMES LinearMath 
    PATHS "${MOTIVE_ROOT}/bullet3/build/src/LinearMath")

motive_find_library(MOTIVE_PNG_LIBRARY NAMES png PATHS ${MOTIVE_LIBRARY_DIRS})
motive_find_library(MOTIVE_BROTLIDEC_LIBRARY NAMES brotlidec PATHS ${MOTIVE_LIBRARY_DIRS})
motive_find_library(MOTIVE_BROTLICOMMON_LIBRARY NAMES brotlicommon PATHS ${MOTIVE_LIBRARY_DIRS})
motive_find_library(MOTIVE_BZ2_LIBRARY NAMES bz2 PATHS ${MOTIVE_LIBRARY_DIRS})
motive_find_library(MOTIVE_LZMA_LIBRARY NAMES lzma PATHS ${MOTIVE_LIBRARY_DIRS})
motive_find_library(MOTIVE_DRM_LIBRARY NAMES drm PATHS ${MOTIVE_LIBRARY_DIRS})
motive_find_library(MOTIVE_Z_LIBRARY NAMES z PATHS ${MOTIVE_LIBRARY_DIRS})
motive_find_library(MOTIVE_OPENCL_LIBRARY NAMES OpenCL PATHS ${MOTIVE_LIBRARY_DIRS})
motive_find_library(MOTIVE_X11_LIBRARY NAMES X11 PATHS ${MOTIVE_LIBRARY_DIRS})
motive_find_library(MOTIVE_XEXT_LIBRARY NAMES Xext PATHS ${MOTIVE_LIBRARY_DIRS})
motive_find_library(MOTIVE_VDPAU_LIBRARY NAMES vdpau PATHS ${MOTIVE_LIBRARY_DIRS})

set(MOTIVE_COMMON_LINK_LIBS
    ${MOTIVE_GLFW_LIBRARY}
    ${MOTIVE_TINYGLTF_LIBRARY}
    ${MOTIVE_VULKAN_LIBRARY}
    ${MOTIVE_AVFORMAT_LIBRARY}
    ${MOTIVE_AVCODEC_LIBRARY}
    ${MOTIVE_SWSCALE_LIBRARY}
    ${MOTIVE_AVUTIL_LIBRARY}
    ${MOTIVE_SWRESAMPLE_LIBRARY}
    ${MOTIVE_FREETYPE_LIBRARY}
    ${MOTIVE_NCNN_LIBRARY}
    ${MOTIVE_GLSLANG_LIBRARY}
    ${MOTIVE_GLSLANG_LIMITS_LIBRARY}
    ${MOTIVE_BULLET_DYNAMICS_LIBRARY}
    ${MOTIVE_BULLET_COLLISION_LIBRARY}
    ${MOTIVE_BULLET_LINEAR_MATH_LIBRARY}
    Threads::Threads
    ${CMAKE_DL_LIBS}
    m
)

if(OpenMP_CXX_FOUND)
    list(APPEND MOTIVE_COMMON_LINK_LIBS OpenMP::OpenMP_CXX)
else()
    message(STATUS "OpenMP CXX not found, continuing without OpenMP support")
endif()

foreach(optional_lib
    MOTIVE_PNG_LIBRARY
    MOTIVE_BROTLIDEC_LIBRARY
    MOTIVE_BROTLICOMMON_LIBRARY
    MOTIVE_BZ2_LIBRARY
    MOTIVE_LZMA_LIBRARY
    MOTIVE_DRM_LIBRARY
    MOTIVE_Z_LIBRARY
    MOTIVE_OPENCL_LIBRARY
    MOTIVE_X11_LIBRARY
    MOTIVE_XEXT_LIBRARY
    MOTIVE_VDPAU_LIBRARY
)
    if(${optional_lib})
        list(APPEND MOTIVE_COMMON_LINK_LIBS ${${optional_lib}})
    endif()
endforeach()

# Jolt Physics library (optional)
motive_find_library(MOTIVE_JOLT_LIBRARY NAMES Jolt PATHS "${MOTIVE_ROOT}/jolt/Build/Linux_Release")

if(MOTIVE_JOLT_LIBRARY)
    message(STATUS "Jolt Physics library found: ${MOTIVE_JOLT_LIBRARY}")
    add_compile_definitions(MOTIVE_JOLT_AVAILABLE=1)
    list(APPEND MOTIVE_COMMON_LINK_LIBS ${MOTIVE_JOLT_LIBRARY})
else()
    message(STATUS "Jolt Physics library not found, Jolt backend will be unavailable")
endif()

file(GLOB MOTIVE_ROOT_CPP CONFIGURE_DEPENDS "${MOTIVE_ROOT}/*.cpp")
file(GLOB_RECURSE MOTIVE_COMMON_VV_CPP CONFIGURE_DEPENDS "${MOTIVE_ROOT}/common_vv/libs/*.cpp")
file(GLOB_RECURSE MOTIVE_DECODER_VV_CPP CONFIGURE_DEPENDS "${MOTIVE_ROOT}/vk_video_decoder/libs/*.cpp")

set(MOTIVE_MAIN_SOURCES
    "${MOTIVE_ROOT}/motive3d.cpp"
    "${MOTIVE_ROOT}/motive2d.cpp"
    "${MOTIVE_ROOT}/encode.cpp"
    "${MOTIVE_ROOT}/asset_load_profile.cpp"
    "${MOTIVE_ROOT}/main.cpp"
)
set(MOTIVE_EXCLUDED_ENGINE_SOURCES
    "${MOTIVE_ROOT}/brain_viewer_engine.cpp"
    "${MOTIVE_ROOT}/vulkan_video_bridge.cpp"
    "${MOTIVE_ROOT}/display_old.cpp"
    "${MOTIVE_ROOT}/display_old.h"
    "${MOTIVE_ROOT}/shell_old.cpp"
    "${MOTIVE_ROOT}/shell_old.h"
    "${MOTIVE_ROOT}/motive3d_app.cpp"
    "${MOTIVE_ROOT}/follow_target_tracker.cpp"
    "${MOTIVE_ROOT}/camera.cpp"
    "${MOTIVE_ROOT}/input_router.cpp"
    "${MOTIVE_ROOT}/orbit_follow_rig.cpp"
    "${MOTIVE_ROOT}/animation.cpp"
    "${MOTIVE_ROOT}/fbx_import.cpp"
    "${MOTIVE_ROOT}/gltf_import.cpp"
    "${MOTIVE_ROOT}/model.cpp"
    "${MOTIVE_ROOT}/model_import_shared.cpp"
    "${MOTIVE_ROOT}/model_runtime.cpp"
    "${MOTIVE_ROOT}/primitive.cpp"
    "${MOTIVE_ROOT}/texture.cpp"
)

list(REMOVE_ITEM MOTIVE_ROOT_CPP ${MOTIVE_MAIN_SOURCES})
list(REMOVE_ITEM MOTIVE_ROOT_CPP ${MOTIVE_EXCLUDED_ENGINE_SOURCES})

set(MOTIVE_ENGINE_SOURCES
    ${MOTIVE_ROOT_CPP}
    ${MOTIVE_COMMON_VV_CPP}
    ${MOTIVE_DECODER_VV_CPP}
)

set(MOTIVE_RUNTIME_CAMERA_SOURCES
    "${MOTIVE_ROOT}/follow_target_tracker.cpp"
    "${MOTIVE_ROOT}/camera.cpp"
    "${MOTIVE_ROOT}/input_router.cpp"
    "${MOTIVE_ROOT}/orbit_follow_rig.cpp"
)

set(MOTIVE_RUNTIME_SCENE_SOURCES
    "${MOTIVE_ROOT}/animation.cpp"
    "${MOTIVE_ROOT}/fbx_import.cpp"
    "${MOTIVE_ROOT}/gltf_import.cpp"
    "${MOTIVE_ROOT}/model.cpp"
    "${MOTIVE_ROOT}/model_import_shared.cpp"
    "${MOTIVE_ROOT}/model_runtime.cpp"
    "${MOTIVE_ROOT}/primitive.cpp"
    "${MOTIVE_ROOT}/texture.cpp"
)

add_library(motive_runtime_camera STATIC ${MOTIVE_RUNTIME_CAMERA_SOURCES})
target_include_directories(motive_runtime_camera PUBLIC ${MOTIVE_INCLUDE_DIRS})
target_link_libraries(motive_runtime_camera PUBLIC ${MOTIVE_COMMON_LINK_LIBS} Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)

add_library(motive_vendor_ufbx STATIC "${MOTIVE_ROOT}/ufbx/ufbx.c")
target_include_directories(motive_vendor_ufbx PUBLIC ${MOTIVE_INCLUDE_DIRS})

add_library(motive_runtime_scene STATIC ${MOTIVE_RUNTIME_SCENE_SOURCES})
target_include_directories(motive_runtime_scene PUBLIC ${MOTIVE_INCLUDE_DIRS})
target_link_libraries(motive_runtime_scene PUBLIC motive_vendor_ufbx ${MOTIVE_COMMON_LINK_LIBS} Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network motive_runtime_camera)

add_library(engine STATIC ${MOTIVE_ENGINE_SOURCES})
target_include_directories(engine PUBLIC ${MOTIVE_INCLUDE_DIRS})
target_link_libraries(engine PUBLIC motive_vendor_ufbx ${MOTIVE_COMMON_LINK_LIBS} Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network motive_runtime_camera motive_runtime_scene)
if(TARGET CPPMonetize::cppmonetize)
    target_link_libraries(engine PUBLIC CPPMonetize::cppmonetize)
endif()

foreach(source_file IN LISTS MOTIVE_ENGINE_SOURCES)
    get_filename_component(source_name "${source_file}" NAME)
    if(source_name MATCHES "detection|overlay_yolo|motive2d_yolo")
        set_source_files_properties("${source_file}" PROPERTIES COMPILE_DEFINITIONS "NCNN_AVAILABLE;NCNN_USE_VULKAN=0")
    endif()
endforeach()

set(MOTIVE_SHADER_OUTPUTS)
file(GLOB MOTIVE_SHADER_INPUTS CONFIGURE_DEPENDS
    "${MOTIVE_SHADER_DIR}/*.vert"
    "${MOTIVE_SHADER_DIR}/*.frag"
    "${MOTIVE_SHADER_DIR}/*.comp"
)
foreach(shader IN LISTS MOTIVE_SHADER_INPUTS)
    get_filename_component(shader_name "${shader}" NAME)
    set(shader_out "${MOTIVE_SHADER_DIR}/${shader_name}.spv")
    add_custom_command(
        OUTPUT "${shader_out}"
        COMMAND ${GLSLANG_VALIDATOR} -V "${shader}" -o "${shader_out}"
        DEPENDS "${shader}"
        VERBATIM
    )
    list(APPEND MOTIVE_SHADER_OUTPUTS "${shader_out}")
endforeach()
add_custom_target(compile_shaders ALL DEPENDS ${MOTIVE_SHADER_OUTPUTS})

function(motive_add_app target source_file)
    add_executable(${target} "${MOTIVE_ROOT}/${source_file}")
    target_include_directories(${target} PRIVATE ${MOTIVE_INCLUDE_DIRS})
    target_link_libraries(${target} PRIVATE engine)
    add_dependencies(${target} compile_shaders)
endfunction()

motive_add_app(motive3d main.cpp)
target_link_libraries(motive3d PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
motive_add_app(motive3d_runtime motive3d.cpp)
target_sources(motive3d_runtime PRIVATE "${MOTIVE_ROOT}/motive3d_app.cpp")
motive_add_app(motive2d motive2d.cpp)
motive_add_app(encode encode.cpp)
motive_add_app(motive_editor main.cpp)
motive_add_app(asset_load_profile asset_load_profile.cpp)
target_link_libraries(motive_editor PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)

if(MOTIVE_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
