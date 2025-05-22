@ECHO OFF
SETLOCAL ENABLEDELAYEDEXPANSION

REM #############################################################################
REM # Windows Batch equivalent of appimage-builder.sh
REM # This script orchestrates the packaging of a Windows application,
REM # focusing on using windeployqt for Qt dependencies and then creating
REM # a distributable package (e.g., a ZIP archive).
REM #
REM # Original script by: reg_server, 2024-01-09
REM #############################################################################

REM --- Preliminary Checks and Setup ---
REM Ensure Git is available for version information (optional, but in original script)
git --version >NUL 2>NUL
IF ERRORLEVEL 9009 (
    ECHO WARNING: Git not found in PATH. Commit information for package name will be missing.
)

REM Ensure curl is available (comes with Windows 10/11, used for downloads in original, might be needed for other tools)
curl --version >NUL 2>NUL
IF ERRORLEVEL 9009 (
    ECHO WARNING: curl not found in PATH.
)

REM Ensure a tool for SHA256 checksum is available (certutil is built-in)
certutil -? >NUL 2>NUL
IF ERRORLEVEL 9009 (
    ECHO ERROR: certutil.exe not found. This is needed for SHA256 checksums.
    GOTO :EOF
)

REM --- Argument Parsing and Validation ---
IF "%~2"=="" (
    ECHO ERROR: Invalid arguments!
    ECHO Usage: %~n0 yuzu^|sumi ^<build_dir^>
    EXIT /B 1
)

SET "BUILD_APP=%~1"
IF /I NOT "%BUILD_APP%"=="yuzu" IF /I NOT "%BUILD_APP%"=="sumi" (
    ECHO ERROR: Invalid arguments! Application must be 'yuzu' or 'sumi'.
    ECHO Usage: %~n0 yuzu^|sumi ^<build_dir^>
    EXIT /B 1
)

REM Resolve build directory to an absolute path
FOR %%F IN ("%~2") DO SET "BUILD_DIR=%%~fF"

IF NOT EXIST "%BUILD_DIR%\" (
    ECHO ERROR: Invalid arguments!
    ECHO '%~2' ('%BUILD_DIR%') is not a directory.
    EXIT /B 1
)
ECHO Build Directory: %BUILD_DIR%
ECHO Application Name: %BUILD_APP%

REM --- Path Definitions (Windows Style) ---
SET "DEPLOY_WINDOWS_FOLDER=%BUILD_DIR%\deploy-windows"
SET "DEPLOY_WINDOWS_APPDIR_FOLDER=%BUILD_DIR%\deploy-windows\AppDir"
SET "BIN_FOLDER=%BUILD_DIR%\bin"

REM The original script had a complex substitution ${BUILD_APP//sumi/sumi} which effectively
REM was a no-op or identity transformation for the given inputs.
REM We'll assume the executable is named %BUILD_APP%.exe
SET "BIN_EXE=%BIN_FOLDER%\%BUILD_APP%.exe"

REM --- CPU Architecture ---
REM %PROCESSOR_ARCHITECTURE% can be AMD64, IA64, x86, ARM64
SET "CPU_ARCH=%PROCESSOR_ARCHITECTURE%"
IF /I "%CPU_ARCH%"=="x86" (
    REM For consistency with common arch naming, you might want to set it to i686 or similar
    REM SET "CPU_ARCH=i686"
)
ECHO CPU Architecture: %CPU_ARCH%

REM --- Check for Main Executable ---
IF NOT EXIST "%BIN_EXE%" (
    ECHO ERROR: Invalid or missing main executable (%BIN_EXE%)!
    ECHO Ensure your Windows build has produced this executable.
    EXIT /B 1
)
ECHO Main executable found: %BIN_EXE%

REM --- Prepare Deployment Directories ---
ECHO Preparing deployment folder: %DEPLOY_WINDOWS_FOLDER%
IF EXIST "%DEPLOY_WINDOWS_FOLDER%\" (
    ECHO Cleaning up existing deploy folder: %DEPLOY_WINDOWS_APPDIR_FOLDER%
    RMDIR /S /Q "%DEPLOY_WINDOWS_APPDIR_FOLDER%" 2>NUL
)
MKDIR "%DEPLOY_WINDOWS_FOLDER%"
MKDIR "%DEPLOY_WINDOWS_APPDIR_FOLDER%"

REM --- Deploy/Install Application to AppDir ---
ECHO Installing application to %DEPLOY_WINDOWS_APPDIR_FOLDER%...
PUSHD "%BUILD_DIR%"
REM This assumes your build system (e.g., CMake) supports an install step.
REM If using CMake:
cmake --install . --prefix "%DEPLOY_WINDOWS_APPDIR_FOLDER%"
REM If not using CMake, you'll need to manually copy the application binaries,
REM resources, and any known non-Qt dependencies here.
REM Example:
REM COPY "%BIN_EXE%" "%DEPLOY_WINDOWS_APPDIR_FOLDER%\bin\"
REM XCOPY /E /I /Y "%BUILD_DIR%\resources" "%DEPLOY_WINDOWS_APPDIR_FOLDER%\resources\"

IF ERRORLEVEL 1 (
    ECHO ERROR: Failed to install application to AppDir. Check your build/install commands.
    POPD
    EXIT /B 1
)
POPD
ECHO Application installed to AppDir.

REM --- Change to Deployment Folder ---
PUSHD "%DEPLOY_WINDOWS_FOLDER%"
ECHO Current directory: %CD%

REM --- Remove any unneeded command-line helper executables (if applicable) ---
REM Example: if there's a %BUILD_APP%-cmd.exe you don't want in the package
REM SET "CMD_EXE_TO_REMOVE=%DEPLOY_WINDOWS_APPDIR_FOLDER%\bin\%BUILD_APP%-cmd.exe"
REM IF EXIST "%CMD_EXE_TO_REMOVE%" (
REM     ECHO Removing: %CMD_EXE_TO_REMOVE%
REM     DEL /F /Q "%CMD_EXE_TO_REMOVE%"
REM )

REM #############################################################################
REM # Windows Equivalent of Dependency Deployment (using windeployqt)
REM # The original script used linuxdeploy and plugins. For Windows Qt apps,
REM # windeployqt is the standard tool.
REM #############################################################################

ECHO Running windeployqt to gather Qt dependencies...
REM Ensure windeployqt.exe is in your PATH or provide the full path to it.
REM Example: SET "WINDEPLOYQT_PATH=C:\Qt\6.5.0\msvc2019_64\bin\windeployqt.exe"
SET "WINDEPLOYQT_EXE=windeployqt"
SET "APP_EXE_IN_APPDIR=%DEPLOY_WINDOWS_APPDIR_FOLDER%\bin\%BUILD_APP%.exe"

REM Adjust windeployqt options as needed (e.g., --qmldir for QML apps, --no-translations, etc.)
REM Basic usage:
ECHO Calling: %WINDEPLOYQT_EXE% --dir "%DEPLOY_WINDOWS_APPDIR_FOLDER%" "%APP_EXE_IN_APPDIR%"
%WINDEPLOYQT_EXE% --dir "%DEPLOY_WINDOWS_APPDIR_FOLDER%" "%APP_EXE_IN_APPDIR%"
REM A more comprehensive call might look like:
REM %WINDEPLOYQT_EXE% --verbose 2 --dir "%DEPLOY_WINDOWS_APPDIR_FOLDER%" --plugindir "%DEPLOY_WINDOWS_APPDIR_FOLDER%\plugins" "%APP_EXE_IN_APPDIR%"

IF ERRORLEVEL 1 (
    ECHO ERROR: windeployqt failed. Ensure it's in PATH and your application executable is correct.
    ECHO Make sure all necessary DLLs for your app (non-Qt) are already alongside the EXE or in PATH during this step.
    POPD
    EXIT /B 1
)
ECHO windeployqt finished. Qt dependencies should be in %DEPLOY_WINDOWS_APPDIR_FOLDER%.

REM --- Remove any problematic or unnecessary libraries (Windows specific) ---
REM This is where you might remove files if you know they cause issues or are not needed.
REM Example:
REM IF EXIST "%DEPLOY_WINDOWS_APPDIR_FOLDER%\bin\some_unwanted_dll.dll" (
REM     DEL /F /Q "%DEPLOY_WINDOWS_APPDIR_FOLDER%\bin\some_unwanted_dll.dll"
REM )

REM #############################################################################
REM # Packaging the AppDir (e.g., into a ZIP file or an installer)
REM # The original script used appimagetool. Here, you'd use a Windows tool.
REM # Example: Creating a ZIP archive. You might need 7-Zip (7z.exe) or tar.
REM # For an installer, you'd call Inno Setup, NSIS, etc.
REM #############################################################################

ECHO Packaging the AppDir...
SET "PACKAGE_TYPE=zip" REM or "exe" for installer

REM --- Version and Naming Information ---
SET "COMM_COUNT=0"
SET "COMM_HASH=unknown"
SET "BUILD_DATE="

REM Get date in YYYYMMDD format
FOR /F "tokens=1-3 delims=. " %%a IN ('DATE /T') DO (
    REM Date format is locale-dependent. This works for DD.MM.YYYY or DD MM YYYY
    REM Adjust if your system date format is different (e.g. MM/DD/YYYY)
    IF "%%c" GTR "1000" ( SET "BUILD_DATE=%%c%%b%%a" ) ELSE (powershell -Command "(Get-Date).ToString('yyyyMMdd')" > "%TEMP%\currentdate.tmp" && SET /P BUILD_DATE=<"%TEMP%\currentdate.tmp" && DEL "%TEMP%\currentdate.tmp")
)
IF "%BUILD_DATE%"=="" (
    powershell -Command "(Get-Date).ToString('yyyyMMdd')" > "%TEMP%\currentdate.tmp"
    SET /P BUILD_DATE=<"%TEMP%\currentdate.tmp"
    DEL "%TEMP%\currentdate.tmp"
)

PUSHD "%BUILD_DIR%"
git rev-list --count HEAD > "%TEMP%\gitcount.tmp" 2>NUL
IF EXIST "%TEMP%\gitcount.tmp" (
    SET /P COMM_COUNT=<"%TEMP%\gitcount.tmp"
    DEL "%TEMP%\gitcount.tmp"
    SET "COMM_COUNT=!COMM_COUNT: =!" REM Trim spaces
)

git rev-parse --short=9 HEAD > "%TEMP%\githash.tmp" 2>NUL
IF EXIST "%TEMP%\githash.tmp" (
    SET /P COMM_HASH=<"%TEMP%\githash.tmp"
    DEL "%TEMP%\githash.tmp"
    SET "COMM_HASH=!COMM_HASH: =!" REM Trim spaces
)
POPD

SET "WINDOWS_PACKAGE_BASENAME=%BUILD_APP%-nightly-%BUILD_DATE%-%COMM_COUNT%-%COMM_HASH%-%CPU_ARCH%"
SET "WINDOWS_PACKAGE_NAME=%WINDOWS_PACKAGE_BASENAME%.%PACKAGE_TYPE%"

ECHO Generated Package Name: %WINDOWS_PACKAGE_NAME%

REM --- Create the package (ZIP example using tar, which is available on modern Windows) ---
ECHO Creating package: %WINDOWS_PACKAGE_NAME%
REM Ensure you are in the DEPLOY_WINDOWS_FOLDER for correct relative paths in ZIP
REM The AppDir is inside DEPLOY_WINDOWS_FOLDER
REM We want to zip the contents of AppDir, not AppDir itself as a top-level folder in the zip.

PUSHD "%DEPLOY_WINDOWS_APPDIR_FOLDER%"
REM Using tar to create a zip. Ensure tar.exe is in PATH (usually with Git for Windows or built-in Win10+)
REM The command creates a zip file one level up (in DEPLOY_WINDOWS_FOLDER)
tar -acf "../%WINDOWS_PACKAGE_NAME%" *
REM For 7-Zip (if 7z.exe is in PATH):
REM "C:\Program Files\7-Zip\7z.exe" a -tzip "../%WINDOWS_PACKAGE_NAME%" ".\*" -mx=5
POPD

IF NOT EXIST "%WINDOWS_PACKAGE_NAME%" (
    ECHO ERROR: Failed to create the package %WINDOWS_PACKAGE_NAME%. Check tar/7zip command and paths.
    POPD
    EXIT /B 1
)
ECHO Package created: %CD%\%WINDOWS_PACKAGE_NAME%

REM --- Get Filesize and SHA256 Checksum ---
SET "FILESIZE="
FOR %%F IN ("%WINDOWS_PACKAGE_NAME%") DO SET "FILESIZE=%%~zF"

SET "SHA256SUM="
certutil -hashfile "%WINDOWS_PACKAGE_NAME%" SHA256 > "%TEMP%\sha256.tmp"
FOR /F "skip=1 tokens=1" %%h IN ('type "%TEMP%\sha256.tmp"') DO (
    IF NOT DEFINED SHA256SUM SET "SHA256SUM=%%h"
)
DEL "%TEMP%\sha256.tmp"

ECHO.
ECHO --- Build Summary ---
ECHO Final Package: %WINDOWS_PACKAGE_NAME%
ECHO Size: %FILESIZE% bytes
IF DEFINED SHA256SUM (
    ECHO SHA256: %SHA256SUM%
    ECHO Metadata Line: %SHA256SUM%;