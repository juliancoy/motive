param(
    [switch]$Asan,
    [switch]$NoValidation,
    [switch]$BootstrapDeps,
    [switch]$Clean,
    [switch]$Full,
    [int]$Jobs = 0,
    [string]$Generator = "",
    [string]$Arch = "x64",
    [string]$Toolset = "",
    [switch]$VerboseBuild,
    [Alias("h")]
    [switch]$Help
)

$ErrorActionPreference = "Stop"

function Resolve-CMakePath {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    $candidates = @(
        "C:\Program Files\CMake\bin\cmake.exe",
        "C:\Program Files (x86)\CMake\bin\cmake.exe",
        "$env:LOCALAPPDATA\Programs\CMake\bin\cmake.exe"
    )

    foreach ($candidate in $candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate)) {
            return $candidate
        }
    }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $vsInstallPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
        if (-not [string]::IsNullOrWhiteSpace($vsInstallPath)) {
            $vsCmake = Join-Path $vsInstallPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path -LiteralPath $vsCmake) {
                return $vsCmake
            }
        }
    }

    return $null
}

function Resolve-NinjaPath {
    param(
        [string]$CmakeExePath = ""
    )

    $cmd = Get-Command ninja -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    $candidates = @(
        "C:\Tools\ninja.exe",
        "C:\Program Files\CMake\bin\ninja.exe",
        "C:\Program Files (x86)\CMake\bin\ninja.exe",
        "$env:LOCALAPPDATA\Programs\CMake\bin\ninja.exe"
    )

    if (-not [string]::IsNullOrWhiteSpace($CmakeExePath)) {
        $cmakeBinDir = Split-Path -Parent $CmakeExePath
        if (-not [string]::IsNullOrWhiteSpace($cmakeBinDir)) {
            $candidates += (Join-Path $cmakeBinDir "ninja.exe")
        }
    }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $vsInstallPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
        if (-not [string]::IsNullOrWhiteSpace($vsInstallPath)) {
            $candidates += (Join-Path $vsInstallPath "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe")
        }
    }

    foreach ($candidate in $candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate)) {
            return $candidate
        }
    }

    return $null
}

function Test-VisualStudioAvailable {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) {
        return $false
    }

    $vsInstallPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
    return (-not [string]::IsNullOrWhiteSpace($vsInstallPath))
}

function Resolve-CompilerPair {
    $ccCmd = Get-Command clang -ErrorAction SilentlyContinue
    $cxxCmd = Get-Command clang++ -ErrorAction SilentlyContinue
    if ($ccCmd -and $cxxCmd) {
        return @{ C = $ccCmd.Source; CXX = $cxxCmd.Source; Family = "clang" }
    }

    $gccCmd = Get-Command gcc -ErrorAction SilentlyContinue
    $gxxCmd = Get-Command g++ -ErrorAction SilentlyContinue
    if ($gccCmd -and $gxxCmd) {
        return @{ C = $gccCmd.Source; CXX = $gxxCmd.Source; Family = "gcc" }
    }

    $commonPairs = @(
        @{ C = "C:\Program Files\LLVM\bin\clang.exe"; CXX = "C:\Program Files\LLVM\bin\clang++.exe"; Family = "clang" },
        @{ C = "$env:LOCALAPPDATA\Programs\LLVM\bin\clang.exe"; CXX = "$env:LOCALAPPDATA\Programs\LLVM\bin\clang++.exe"; Family = "clang" },
        @{ C = "C:\msys64\ucrt64\bin\gcc.exe"; CXX = "C:\msys64\ucrt64\bin\g++.exe"; Family = "gcc" },
        @{ C = "C:\msys64\mingw64\bin\gcc.exe"; CXX = "C:\msys64\mingw64\bin\g++.exe"; Family = "gcc" }
    )

    foreach ($pair in $commonPairs) {
        if (
            -not [string]::IsNullOrWhiteSpace($pair.C) -and
            -not [string]::IsNullOrWhiteSpace($pair.CXX) -and
            (Test-Path -LiteralPath $pair.C) -and
            (Test-Path -LiteralPath $pair.CXX)
        ) {
            return $pair
        }
    }

    $clCmd = Get-Command cl -ErrorAction SilentlyContinue
    if ($clCmd) {
        return @{ C = $clCmd.Source; CXX = $clCmd.Source; Family = "msvc" }
    }

    return $null
}

function Get-CMakeCacheValue {
    param(
        [string]$CacheText,
        [string]$Key
    )

    $escapedKey = [regex]::Escape($Key)
    if ($CacheText -match "(?m)^${escapedKey}:[^=]+=(.+)$") {
        return $Matches[1].Trim()
    }

    return $null
}

function Normalize-PathValue {
    param(
        [string]$PathValue
    )

    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return ""
    }

    return $PathValue.Replace('\', '/').ToLowerInvariant()
}

function Stop-BuildProcesses {
    $buildProcesses = Get-Process cmake, ninja -ErrorAction SilentlyContinue
    if ($buildProcesses) {
        $buildProcesses | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 200
    }
}

function Remove-BuildDirectory {
    param(
        [string]$Path
    )

    Stop-BuildProcesses

    for ($attempt = 1; $attempt -le 3; $attempt++) {
        try {
            Remove-Item -Recurse -Force -LiteralPath $Path -ErrorAction Stop
            return
        } catch {
            if ($attempt -eq 3) {
                throw
            }
            Start-Sleep -Milliseconds (500 * $attempt)
            Stop-BuildProcesses
        }
    }
}

function Resolve-VulkanSdk {
    $sdkRoot = $null

    if (-not [string]::IsNullOrWhiteSpace($env:VULKAN_SDK) -and (Test-Path -LiteralPath $env:VULKAN_SDK)) {
        $sdkRoot = $env:VULKAN_SDK
    }

    if (-not $sdkRoot -and (Test-Path -LiteralPath "C:\VulkanSDK")) {
        $versions = Get-ChildItem -Path "C:\VulkanSDK" -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending
        if ($versions -and $versions.Count -gt 0) {
            $sdkRoot = $versions[0].FullName
        }
    }

    if (-not $sdkRoot) {
        return $null
    }

    $validatorPath = Join-Path $sdkRoot "Bin\glslangValidator.exe"
    $includePath = Join-Path $sdkRoot "Include"
    $libPath = Join-Path $sdkRoot "Lib\vulkan-1.lib"

    if (-not (Test-Path -LiteralPath $validatorPath)) {
        $validatorPath = $null
    }
    if (-not (Test-Path -LiteralPath $includePath)) {
        $includePath = $null
    }
    if (-not (Test-Path -LiteralPath $libPath)) {
        $libPath = $null
    }

    return @{
        Root = $sdkRoot
        Validator = $validatorPath
        Include = $includePath
        VulkanLib = $libPath
    }
}

function Invoke-CheckedCommand {
    param(
        [string]$Exe,
        [string[]]$CommandArgs,
        [string]$WorkingDirectory = ""
    )

    if ([string]::IsNullOrWhiteSpace($WorkingDirectory)) {
        & $Exe @CommandArgs
    } else {
        Push-Location $WorkingDirectory
        try {
            & $Exe @CommandArgs
        } finally {
            Pop-Location
        }
    }

    if ($LASTEXITCODE -ne 0) {
        throw "Command failed: $Exe $($CommandArgs -join ' ')"
    }
}

function Get-MissingRequiredDeps {
    param(
        [hashtable]$VulkanSdkInfo
    )

    $missing = New-Object System.Collections.Generic.List[string]

    $sourceChecks = @(
        @{ Name = "glfw submodule source"; Path = "glfw/CMakeLists.txt" },
        @{ Name = "tinygltf submodule source"; Path = "tinygltf/tiny_gltf.cc" },
        @{ Name = "bullet3 submodule source"; Path = "bullet3/CMakeLists.txt" },
        @{ Name = "ncnn submodule source"; Path = "ncnn/CMakeLists.txt" }
    )

    foreach ($sourceCheck in $sourceChecks) {
        if (-not (Test-Path -LiteralPath $sourceCheck.Path)) {
            $missing.Add("$($sourceCheck.Name) (missing file: $($sourceCheck.Path))")
        }
    }

    $checks = @(
        @{ Name = "glfw3"; Path = "glfw/build/src"; Pattern = "libglfw3*.a" },
        @{ Name = "tinygltf"; Path = "."; Pattern = "libtinygltf.a" },
        @{ Name = "FFmpeg avformat"; Path = "FFmpeg/.build/install/lib"; Pattern = "libavformat*.a" },
        @{ Name = "FFmpeg avcodec"; Path = "FFmpeg/.build/install/lib"; Pattern = "libavcodec*.a" },
        @{ Name = "FFmpeg swscale"; Path = "FFmpeg/.build/install/lib"; Pattern = "libswscale*.a" },
        @{ Name = "FFmpeg avutil"; Path = "FFmpeg/.build/install/lib"; Pattern = "libavutil*.a" },
        @{ Name = "FFmpeg swresample"; Path = "FFmpeg/.build/install/lib"; Pattern = "libswresample*.a" },
        @{ Name = "freetype"; Path = "freetype/build"; Pattern = "libfreetype*.a" },
        @{ Name = "ncnn"; Path = "ncnn/build/src"; Pattern = "libncnn*.a" },
        @{ Name = "glslang"; Path = "ncnn/build/glslang/glslang"; Pattern = "libglslang*.a" },
        @{ Name = "glslang-default-resource-limits"; Path = "ncnn/build/glslang/glslang"; Pattern = "libglslang-default-resource-limits*.a" },
        @{ Name = "BulletDynamics"; Path = "bullet3/build/src/BulletDynamics"; Pattern = "libBulletDynamics*.a" },
        @{ Name = "BulletCollision"; Path = "bullet3/build/src/BulletCollision"; Pattern = "libBulletCollision*.a" },
        @{ Name = "LinearMath"; Path = "bullet3/build/src/LinearMath"; Pattern = "libLinearMath*.a" }
    )

    foreach ($check in $checks) {
        $targetPath = $check.Path
        if ($targetPath -eq ".") {
            $targetPath = (Get-Location).Path
        }

        if (-not (Test-Path -LiteralPath $targetPath)) {
            $missing.Add("$($check.Name) (missing path: $($check.Path))")
            continue
        }

        $found = Get-ChildItem -LiteralPath $targetPath -Filter $check.Pattern -File -ErrorAction SilentlyContinue
        if (-not $found) {
            $missing.Add("$($check.Name) (expected pattern: $($check.Path)/$($check.Pattern))")
        }
    }

    if (-not $VulkanSdkInfo -or [string]::IsNullOrWhiteSpace($VulkanSdkInfo.Validator)) {
        $missing.Add("glslangValidator.exe (install Vulkan SDK and/or set VULKAN_SDK)")
    }

    if (-not $VulkanSdkInfo -or [string]::IsNullOrWhiteSpace($VulkanSdkInfo.Include)) {
        $missing.Add("Vulkan headers include dir (install Vulkan SDK and/or set VULKAN_SDK)")
    }

    return $missing
}

function Bootstrap-WindowsDeps {
    param(
        [string]$CmakeExePath,
        [string]$NinjaExePath,
        [hashtable]$Compiler
    )

    if (-not $Compiler -or $Compiler.Family -ne "gcc") {
        throw "BootstrapDeps currently supports the MinGW/GCC toolchain only."
    }

    Write-Host "Bootstrapping Windows dependencies (glfw, tinygltf, bullet)..."

    if (-not (Test-Path -LiteralPath "glfw/CMakeLists.txt")) {
        throw "glfw submodule is not initialized (missing glfw/CMakeLists.txt). Run: git submodule update --init --recursive"
    }
    if (-not (Test-Path -LiteralPath "tinygltf/tiny_gltf.cc")) {
        throw "tinygltf submodule is not initialized (missing tinygltf/tiny_gltf.cc). Run: git submodule update --init --recursive"
    }
    if (-not (Test-Path -LiteralPath "bullet3/CMakeLists.txt")) {
        throw "bullet3 submodule is not initialized (missing bullet3/CMakeLists.txt). Run: git submodule update --init --recursive"
    }

    # GLFW
    if (-not (Test-Path -LiteralPath "glfw/build/src") -or -not (Get-ChildItem -LiteralPath "glfw/build/src" -Filter "libglfw3*.a" -File -ErrorAction SilentlyContinue)) {
        New-Item -ItemType Directory -Path "glfw/build" -Force | Out-Null
        $glfwConfigureArgs = @(
            "-S", "glfw",
            "-B", "glfw/build",
            "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DBUILD_SHARED_LIBS=OFF",
            "-DGLFW_BUILD_EXAMPLES=OFF",
            "-DGLFW_BUILD_TESTS=OFF",
            "-DGLFW_BUILD_DOCS=OFF",
            "-DCMAKE_MAKE_PROGRAM=$NinjaExePath",
            "-DCMAKE_C_COMPILER=$($Compiler.C)",
            "-DCMAKE_CXX_COMPILER=$($Compiler.CXX)"
        )
        Invoke-CheckedCommand -Exe $CmakeExePath -CommandArgs $glfwConfigureArgs
        Invoke-CheckedCommand -Exe $CmakeExePath -CommandArgs @("--build", "glfw/build", "--parallel")
    }

    # tinygltf
    if (-not (Test-Path -LiteralPath "libtinygltf.a")) {
        $compilerDir = Split-Path -Parent $Compiler.C
        $gppPath = Join-Path $compilerDir "g++.exe"
        $arPath = Join-Path $compilerDir "ar.exe"
        if (-not (Test-Path -LiteralPath $gppPath) -or -not (Test-Path -LiteralPath $arPath)) {
            throw "Could not find g++.exe/ar.exe for tinygltf bootstrap under $compilerDir"
        }
        Invoke-CheckedCommand -Exe $gppPath -CommandArgs @("-c", "-std=c++11", "-O2", "-fPIC", "tinygltf/tiny_gltf.cc", "-o", "tinygltf/tiny_gltf.o")
        Invoke-CheckedCommand -Exe $arPath -CommandArgs @("rcs", "libtinygltf.a", "tinygltf/tiny_gltf.o")
    }

    # Bullet
    if (-not (Test-Path -LiteralPath "bullet3/build/src/BulletDynamics") -or -not (Get-ChildItem -LiteralPath "bullet3/build/src/BulletDynamics" -Filter "libBulletDynamics*.a" -File -ErrorAction SilentlyContinue)) {
        New-Item -ItemType Directory -Path "bullet3/build" -Force | Out-Null
        $bulletConfigureArgs = @(
            "-S", "bullet3",
            "-B", "bullet3/build",
            "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DBUILD_SHARED_LIBS=OFF",
            "-DUSE_DOUBLE_PRECISION=OFF",
            "-DBUILD_CPU_DEMOS=OFF",
            "-DBUILD_OPENGL3_DEMOS=OFF",
            "-DBUILD_BULLET2_DEMOS=OFF",
            "-DBUILD_EXTRAS=OFF",
            "-DBUILD_UNIT_TESTS=OFF",
            "-DCMAKE_MAKE_PROGRAM=$NinjaExePath",
            "-DCMAKE_C_COMPILER=$($Compiler.C)",
            "-DCMAKE_CXX_COMPILER=$($Compiler.CXX)"
        )
        Invoke-CheckedCommand -Exe $CmakeExePath -CommandArgs $bulletConfigureArgs
        Invoke-CheckedCommand -Exe $CmakeExePath -CommandArgs @("--build", "bullet3/build", "--parallel")
    }
}

function Get-QtKitRootFromConfigPath {
    param(
        [string]$QtConfigPath
    )

    if ([string]::IsNullOrWhiteSpace($QtConfigPath)) {
        return $null
    }

    $path = Split-Path -Parent $QtConfigPath
    $path = Split-Path -Parent $path
    $path = Split-Path -Parent $path
    $path = Split-Path -Parent $path
    return $path
}

function Resolve-QtToolchain {
    $searchPaths = @()

    if (-not [string]::IsNullOrWhiteSpace($env:Qt6_DIR)) {
        $searchPaths += $env:Qt6_DIR
    }

    if (-not [string]::IsNullOrWhiteSpace($env:CMAKE_PREFIX_PATH)) {
        $searchPaths += ($env:CMAKE_PREFIX_PATH -split ';')
    }

    $qtConfigPath = $null
    foreach ($path in $searchPaths) {
        if ([string]::IsNullOrWhiteSpace($path)) {
            continue
        }

        $candidate = $path
        if ((Test-Path -LiteralPath $candidate) -and (Get-Item -LiteralPath $candidate).PSIsContainer) {
            $candidate = Join-Path $candidate "Qt6Config.cmake"
        }

        if (Test-Path -LiteralPath $candidate) {
            if ($candidate -match "Qt6Config\.cmake$") {
                $qtConfigPath = $candidate
                break
            }
        }
    }

    if (-not $qtConfigPath) {
        $qtCandidates = Get-ChildItem -Path "C:\Qt" -Recurse -Filter "Qt6Config.cmake" -ErrorAction SilentlyContinue
        foreach ($candidate in $qtCandidates) {
            if ($candidate.FullName -match "Qt6Config\.cmake$") {
                $qtConfigPath = $candidate.FullName
                break
            }
        }
    }

    if (-not $qtConfigPath) {
        return $null
    }

    $qtCmakeDir = Split-Path -Parent $qtConfigPath
    $qtKitRoot = Get-QtKitRootFromConfigPath -QtConfigPath $qtConfigPath
    $qtKitName = Split-Path -Leaf $qtKitRoot

    $toolchainFamily = $null
    $preferredGenerator = $null
    $compilerPair = $null

    if ($qtKitName -match 'mingw') {
        $toolchainFamily = "mingw"
        $preferredGenerator = "Ninja"

        $mingwRoots = @(
            "C:\Qt\Tools\mingw1310_64",
            "C:\Qt\Tools\mingw"
        )

        foreach ($mingwRoot in $mingwRoots) {
            if ([string]::IsNullOrWhiteSpace($mingwRoot)) {
                continue
            }

            $gccPath = Join-Path $mingwRoot "bin\gcc.exe"
            $gxxPath = Join-Path $mingwRoot "bin\g++.exe"
            if ((Test-Path -LiteralPath $gccPath) -and (Test-Path -LiteralPath $gxxPath)) {
                $compilerPair = @{ C = $gccPath; CXX = $gxxPath; Family = "gcc" }
                break
            }
        }

        if (-not $compilerPair) {
            $gccCmd = Get-Command gcc -ErrorAction SilentlyContinue
            $gxxCmd = Get-Command g++ -ErrorAction SilentlyContinue
            if ($gccCmd -and $gxxCmd) {
                $compilerPair = @{ C = $gccCmd.Source; CXX = $gxxCmd.Source; Family = "gcc" }
            }
        }
    } elseif ($qtKitName -match 'msvc') {
        $toolchainFamily = "msvc"
        $preferredGenerator = "Visual Studio 17 2022"
    }

    return @{
        ConfigPath = $qtConfigPath
        CMakeDir = $qtCmakeDir
        KitRoot = $qtKitRoot
        KitName = $qtKitName
        ToolchainFamily = $toolchainFamily
        PreferredGenerator = $preferredGenerator
        CompilerPair = $compilerPair
    }
}

function Show-Help {
    @"
Motive 3D Engine Windows Build Script

Usage:
  ./build_windows.ps1 [options]

Options:
  -Asan             Enable AddressSanitizer (sets Debug build)
  -NoValidation     Disable Vulkan validation layers
  -BootstrapDeps    Build local Windows deps (glfw, tinygltf, bullet) before configure
  -Clean            Remove build-windows before configuring
  -Full             Build motive3d, motive2d, encode, and motive_editor
  -Jobs N           Number of parallel jobs
  -Generator NAME   CMake generator (default: Ninja, else Visual Studio 17 2022)
  -Arch NAME        Visual Studio architecture (default: x64)
  -Toolset NAME     Visual Studio toolset (example: clangcl)
  -VerboseBuild     Verbose build output
  -Help             Show this help message
"@
}

if ($Help) {
    Show-Help
    exit 0
}

$buildType = "Release"
$enableAsan = "OFF"
$enableValidation = "ON"

if ($Asan) {
    $buildType = "Debug"
    $enableAsan = "ON"
}
if ($NoValidation) {
    $enableValidation = "OFF"
}

if ($Jobs -le 0) {
    if ($env:NUMBER_OF_PROCESSORS) {
        $Jobs = [int]$env:NUMBER_OF_PROCESSORS
    } else {
        $Jobs = 8
    }
}

$cmakeExe = Resolve-CMakePath
if (-not $cmakeExe) {
    throw "CMake was not found in PATH or common install locations. Install CMake or add it to PATH."
}
$ninjaExe = Resolve-NinjaPath -CmakeExePath $cmakeExe
$hasVisualStudio = Test-VisualStudioAvailable
$qtToolchain = Resolve-QtToolchain
$vulkanSdk = Resolve-VulkanSdk

if ($qtToolchain) {
    if ([string]::IsNullOrWhiteSpace($Generator)) {
        $Generator = $qtToolchain.PreferredGenerator
    }

    if ($qtToolchain.ToolchainFamily -eq "mingw") {
        if ($Generator -like "Visual Studio*") {
            throw "Qt kit '$($qtToolchain.KitName)' is MinGW-based, but generator '$Generator' requires an MSVC toolchain. Install a Qt MSVC kit or build with Ninja + MinGW."
        }
        if (-not $qtToolchain.CompilerPair) {
            throw "Qt kit '$($qtToolchain.KitName)' was found at '$($qtToolchain.ConfigPath)', but the matching MinGW compiler was not found. Install Qt's bundled MinGW toolchain or add gcc/g++ to PATH."
        }
        $compilerPair = $qtToolchain.CompilerPair
    } elseif ($qtToolchain.ToolchainFamily -eq "msvc") {
        if ($Generator -eq "Ninja") {
            $Generator = $qtToolchain.PreferredGenerator
        }
        if ($Generator -notlike "Visual Studio*") {
            throw "Qt kit '$($qtToolchain.KitName)' is MSVC-based, but generator '$Generator' is not compatible. Use Visual Studio 17 2022 or install a MinGW Qt kit."
        }
    }
}

if ([string]::IsNullOrWhiteSpace($Generator)) {
    if ($ninjaExe) {
        $Generator = "Ninja"
    } elseif ($hasVisualStudio) {
        $Generator = "Visual Studio 17 2022"
    } else {
        throw "No Ninja or Visual Studio installation found. Install Ninja, or provide -Generator with a valid installed generator."
    }
}

if ($Generator -like "Visual Studio*" -and -not $hasVisualStudio) {
    throw "Generator '$Generator' requested, but no Visual Studio installation was found."
}

if ($qtToolchain -and $qtToolchain.ToolchainFamily -eq "mingw") {
    $compilerPair = $qtToolchain.CompilerPair
} elseif ($Generator -eq "Ninja") {
    $compilerPair = Resolve-CompilerPair
    if (-not $compilerPair) {
        throw "Ninja generator selected, but no C/C++ compiler was found. Install LLVM (clang/clang++) or MinGW-w64 (gcc/g++), or run from a VS Developer prompt."
    }
}

if ($compilerPair -and $compilerPair.Family -eq "gcc") {
    $compilerBinDir = Split-Path -Parent $compilerPair.C
    if (-not [string]::IsNullOrWhiteSpace($compilerBinDir) -and (Test-Path -LiteralPath $compilerBinDir)) {
        $env:PATH = "$compilerBinDir;$env:PATH"
    }
}

if ($BootstrapDeps) {
    Bootstrap-WindowsDeps -CmakeExePath $cmakeExe -NinjaExePath $ninjaExe -Compiler $compilerPair
}

$missingDeps = Get-MissingRequiredDeps -VulkanSdkInfo $vulkanSdk
if ($missingDeps.Count -gt 0) {
    Write-Host ""
    Write-Host "Missing required dependencies:"
    foreach ($item in $missingDeps) {
        Write-Host "  - $item"
    }
    Write-Host ""
    Write-Host "Tip: run git submodule update --init --recursive first if source submodules are missing."
    Write-Host "Then run .\\build_windows.ps1 -BootstrapDeps to auto-build glfw/tinygltf/bullet."
    Write-Host "Remaining deps (FFmpeg, FreeType, NCNN/glslang) still need to be built/installed for Windows."
    exit 1
}

if ($vulkanSdk -and -not [string]::IsNullOrWhiteSpace($vulkanSdk.Root)) {
    $env:VULKAN_SDK = $vulkanSdk.Root
    $vulkanBinDir = Join-Path $vulkanSdk.Root "Bin"
    if (Test-Path -LiteralPath $vulkanBinDir) {
        $env:PATH = "$vulkanBinDir;$env:PATH"
    }
}

$isMultiConfig = $false
if ($Generator -like "Visual Studio*" -or $Generator -like "*Multi-Config*") {
    $isMultiConfig = $true
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $scriptDir "build-windows"

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Cleaning $buildDir"
    Remove-BuildDirectory -Path $buildDir
}

if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

$cachePath = Join-Path $buildDir "CMakeCache.txt"
if (Test-Path -LiteralPath $cachePath) {
    $cacheText = Get-Content -Raw -LiteralPath $cachePath
    $cachedGenerator = $null
    $cachedCCompiler = Get-CMakeCacheValue -CacheText $cacheText -Key "CMAKE_C_COMPILER"
    $cachedCxxCompiler = Get-CMakeCacheValue -CacheText $cacheText -Key "CMAKE_CXX_COMPILER"
    $cachedQtDir = Get-CMakeCacheValue -CacheText $cacheText -Key "Qt6_DIR"

    if ($cacheText -match "(?m)^CMAKE_GENERATOR:INTERNAL=(.+)$") {
        $cachedGenerator = $Matches[1].Trim()
    } elseif ($cacheText -match "(?m)^CMAKE_GENERATOR:STRING=(.+)$") {
        $cachedGenerator = $Matches[1].Trim()
    }

    if (-not [string]::IsNullOrWhiteSpace($cachedGenerator) -and $cachedGenerator -ne $Generator) {
        Write-Host "Detected generator change in existing build cache:"
        Write-Host "  Cached:   $cachedGenerator"
        Write-Host "  Current:  $Generator"
        Write-Host "Cleaning $buildDir to avoid CMake generator mismatch."
        Remove-BuildDirectory -Path $buildDir
        New-Item -ItemType Directory -Path $buildDir | Out-Null
    }

    if ($compilerPair) {
        $compilerMismatch = $false
        if (
            -not [string]::IsNullOrWhiteSpace($cachedCCompiler) -and
            (Normalize-PathValue $cachedCCompiler) -ne (Normalize-PathValue $compilerPair.C)
        ) {
            $compilerMismatch = $true
        }
        if (
            -not [string]::IsNullOrWhiteSpace($cachedCxxCompiler) -and
            (Normalize-PathValue $cachedCxxCompiler) -ne (Normalize-PathValue $compilerPair.CXX)
        ) {
            $compilerMismatch = $true
        }
        if (
            $qtToolchain -and
            -not [string]::IsNullOrWhiteSpace($cachedQtDir) -and
            (Normalize-PathValue $cachedQtDir) -ne (Normalize-PathValue $qtToolchain.CMakeDir)
        ) {
            $compilerMismatch = $true
        }

        if ($compilerMismatch) {
            Write-Host "Detected compiler or Qt kit change in existing build cache:"
            Write-Host "  Cached C compiler:   $cachedCCompiler"
            Write-Host "  Current C compiler:   $($compilerPair.C)"
            Write-Host "  Cached CXX compiler:  $cachedCxxCompiler"
            Write-Host "  Current CXX compiler: $($compilerPair.CXX)"
            if ($qtToolchain) {
            Write-Host "  Cached Qt6_DIR:      $cachedQtDir"
            Write-Host "  Current Qt6_DIR:     $($qtToolchain.CMakeDir)"
        }
        Write-Host "Cleaning $buildDir to avoid CMake cache/toolchain mismatch."
            Remove-BuildDirectory -Path $buildDir
            New-Item -ItemType Directory -Path $buildDir | Out-Null
        }
    }
}

Write-Host "Motive Windows build"
Write-Host "  CMake:       $cmakeExe"
if ($ninjaExe) {
    Write-Host "  Ninja:       $ninjaExe"
}
Write-Host "  Generator:   $Generator"
Write-Host "  Build type:  $buildType"
Write-Host "  ASan:        $enableAsan"
Write-Host "  Validation:  $enableValidation"
Write-Host "  Full build:  $Full"
Write-Host "  Jobs:        $Jobs"
if ($compilerPair) {
    Write-Host "  C compiler:  $($compilerPair.C)"
    Write-Host "  CXX compiler:$($compilerPair.CXX)"
}
if ($qtToolchain) {
    Write-Host "  Qt kit:      $($qtToolchain.KitName)"
    Write-Host "  Qt6_DIR:     $($qtToolchain.CMakeDir)"
}
if ($vulkanSdk) {
    Write-Host "  Vulkan SDK:  $($vulkanSdk.Root)"
}

if ($enableAsan -eq "ON" -and $Generator -like "Visual Studio*" -and [string]::IsNullOrWhiteSpace($Toolset)) {
    Write-Warning "ASan with default MSVC toolset may fail. Use -Toolset clangcl if needed."
}

$configureArgs = @(
    "-S", $scriptDir,
    "-B", $buildDir,
    "-G", $Generator,
    "-DMOTIVE_ENABLE_ASAN=$enableAsan",
    "-DMOTIVE_ENABLE_VALIDATION=$enableValidation",
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
)

if (-not $isMultiConfig) {
    $configureArgs += @("-DCMAKE_BUILD_TYPE=$buildType")
}
if ($Generator -like "Visual Studio*") {
    $configureArgs += @("-A", $Arch)
}
if (-not [string]::IsNullOrWhiteSpace($Toolset)) {
    $configureArgs += @("-T", $Toolset)
}
if ($Generator -eq "Ninja" -and $ninjaExe) {
    $configureArgs += @("-DCMAKE_MAKE_PROGRAM=$ninjaExe")
}
if ($compilerPair) {
    $configureArgs += @("-DCMAKE_C_COMPILER=$($compilerPair.C)")
    $configureArgs += @("-DCMAKE_CXX_COMPILER=$($compilerPair.CXX)")
}
if ($qtToolchain) {
    $configureArgs += @("-DQt6_DIR=$($qtToolchain.CMakeDir)")
}
if ($vulkanSdk) {
    if ($vulkanSdk.Validator) {
        $configureArgs += @("-DGLSLANG_VALIDATOR=$($vulkanSdk.Validator)")
    }
    if ($vulkanSdk.Include) {
        $configureArgs += @("-DVulkan_INCLUDE_DIR=$($vulkanSdk.Include)")
    }
    if ($vulkanSdk.VulkanLib) {
        $configureArgs += @("-DVulkan_LIBRARY=$($vulkanSdk.VulkanLib)")
    }
}

& $cmakeExe @configureArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$buildArgs = @(
    "--build", $buildDir,
    "--parallel", "$Jobs"
)

if ($isMultiConfig) {
    $buildArgs += @("--config", $buildType)
}

if ($Full) {
    $buildArgs += @("--target", "motive3d", "motive2d", "encode", "motive_editor")
} else {
    $buildArgs += @("--target", "motive_editor")
}

if ($VerboseBuild) {
    $buildArgs += "--verbose"
}

& $cmakeExe @buildArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Build complete."
