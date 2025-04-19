# --- Configuration ---
$buildDir = "build"
$sourceDir = "." # Relative path to source from where script is run
$cmakeGenerator = "Visual Studio 17 2022" # Example: Use the generator for your system
$cmakeBuildConfig = "Release" # Or Debug, RelWithDebInfo, MinSizeRel
# Add any specific CMake defines your project needs as a string array
$cmakeDefines = @(
    # "-DOPTION1=VALUE1",
    # "-DOPTION2=VALUE2"
)

# Add commands here if you use a package manager THAT needs running before CMake
# Example for vcpkg (uncomment and adjust path if needed):
Write-Host "--- Updating vcpkg packages ---" -ForegroundColor Cyan
try {
    & "D:\Projects\sumitrus\externals\vcpkg\vcpkg" install --triplet x64-windows # Adjust triplet as needed
    & "D:\Projects\sumitrus\externals\vcpkg\vcpkg" upgrade --no-dry-run
} catch {
    Write-Error "vcpkg update/upgrade failed: $($_.Exception.Message)"
    exit 1
}

Example for Conan (uncomment and adjust if needed):
Write-Host "--- Installing Conan packages ---" -ForegroundColor Cyan
try {
   # Ensure build dir exists for conan output if needed before cleaning
   if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir -Force | Out-Null }
   conan install . --output-folder=$buildDir --build=missing --update # Add other Conan options
   if ($LASTEXITCODE -ne 0) { throw "Conan install failed." }
} catch {
   Write-Error "Conan install failed: $($_.Exception.Message)"
   exit 1
}


# --- Clean Build Directory ---
Write-Host "Cleaning build directory: $buildDir" -ForegroundColor Cyan
if (Test-Path $buildDir) {
    try {
        Remove-Item -Recurse -Force -Path $buildDir -ErrorAction Stop
    } catch {
        Write-Error "Failed to remove build directory '$buildDir'. Please close programs using it. Error: $($_.Exception.Message)"
        exit 1
    }
}
try {
    New-Item -ItemType Directory -Path $buildDir -ErrorAction Stop | Out-Null
} catch {
    Write-Error "Failed to create build directory '$buildDir'. Error: $($_.Exception.Message)"
    exit 1
}

# --- Change to Build Directory ---
try {
    Set-Location $buildDir -ErrorAction Stop
} catch {
    Write-Error "Failed to change to build directory '$buildDir'. Error: $($_.Exception.Message)"
    exit 1
}

# --- Run CMake Configuration ---
Write-Host "Running CMake configuration..." -ForegroundColor Cyan
$cmakeArgs = @("..", "-G", $cmakeGenerator) + $cmakeDefines # Use '..' because we CD'd into buildDir
Write-Host "Executing: cmake $cmakeArgs"
try {
    cmake $cmakeArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration returned non-zero exit code: $LASTEXITCODE" }
} catch {
    Write-Error "CMake configuration failed: $($_.Exception.Message)"
    # Change back to original directory before exiting on failure
    Set-Location $sourceDir
    exit 1
}

# --- Run CMake Build ---
Write-Host "Building project ($cmakeBuildConfig)..." -ForegroundColor Cyan
$buildArgs = @("--build", ".", "--config", $cmakeBuildConfig)
Write-Host "Executing: cmake $buildArgs"
try {
    cmake $buildArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake build returned non-zero exit code: $LASTEXITCODE" }
} catch {
    Write-Error "CMake build failed: $($_.Exception.Message)"
    # Change back to original directory before exiting on failure
    Set-Location $sourceDir
    exit 1
}

Write-Host "--- Script finished successfully ---" -ForegroundColor Green
# Change back to original directory
Set-Location $sourceDir