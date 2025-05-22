@echo off
setlocal

REM --- Configuration ---
REM You can change the Visual Studio generator if needed.
REM Examples: "Visual Studio 16 2019", "Visual Studio 17 2022"
set CMAKE_GENERATOR="Visual Studio 17 2022"
set BUILD_CONFIG=Release
REM --- End Configuration ---

echo ==================================================
echo == Sumi Emulator Windows Build Script ==
echo ==================================================
echo.

REM Check if running from the root of the Sumi Emulator repository
if not exist "CMakeLists.txt" (
    echo ERROR: This script must be run from the root directory of the Sumi Emulator repository.
    echo Please 'cd' into the repository directory and re-run the script.
    goto :eof
)

REM Check for Git
git --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: Git is not found. Please install Git and ensure it's in your PATH.
    goto :eof
)

REM Check for CMake
cmake --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: CMake is not found. Please install CMake and ensure it's in your PATH.
    goto :eof
)

echo [*] Prerequisites:
echo    - Git for Windows (checked)
echo    - CMake (checked)
echo    - Visual Studio with C++ Desktop Development workload (ensure installed)
echo    - SDL2 Development Libraries (ensure downloaded and CMake can find them - e.g., via SDL2_DIR env var)
echo.
echo If SDL2 is not found by CMake, you might need to set the SDL2_DIR environment variable.
echo Example: set SDL2_DIR=C:\Libraries\SDL2-2.28.5
echo Or pass it to CMake: -DSDL2_DIR=C:/Libraries/SDL2-2.28.5 (use forward slashes)
echo.
pause

echo --- Step 1: Initializing and Updating Git Submodules ---
git submodule update --init --recursive
if errorlevel 1 (
    echo ERROR: Failed to update Git submodules.
    goto :eof
)
echo OK.
echo.

echo --- Step 2: Creating Build Directory ---
if not exist "build" (
    mkdir build
    if errorlevel 1 (
        echo ERROR: Failed to create 'build' directory.
        goto :eof
    )
    echo 'build' directory created.
) else (
    echo 'build' directory already exists.
)
echo OK.
echo.

echo --- Step 3: Configuring with CMake ---
echo Changing directory to 'build'...
cd build
if errorlevel 1 (
    echo ERROR: Failed to change directory to 'build'.
    goto :eof
)
echo.
echo Running CMake to generate project files (Generator: %CMAKE_GENERATOR%)...
echo This might take a moment.
cmake .. -G %CMAKE_GENERATOR%
REM Add -DSDL2_DIR=C:/path/to/your/SDL2/SDL2-2.x.x if CMake cannot find SDL2 automatically
if errorlevel 1 (
    echo ERROR: CMake configuration failed.
    echo Please check the output above for errors (e.g., missing SDL2, compiler issues).
    cd ..
    goto :eof
)
echo CMake configuration successful.
echo OK.
echo.

echo --- Step 4: Building the Project ---
echo Building Sumi Emulator (Configuration: %BUILD_CONFIG%)...
echo This will take some time depending on your system.
cmake --build . --config %BUILD_CONFIG%
if errorlevel 1 (
    echo ERROR: Build failed. Please check the output above for compilation errors.
    cd ..
    goto :eof
)
echo Build successful!
echo OK.
echo.

echo ==================================================
echo == Build Complete! ==
echo ==================================================
echo.
echo The Sumi Emulator executable should be located in:
echo   %cd%\src\%BUILD_CONFIG%\sumi-emu.exe
echo or
echo   %cd%\%BUILD_CONFIG%\sumi-emu.exe
echo (The exact path might vary slightly based on CMake project structure for executables)
echo.

cd ..
endlocal
pause
