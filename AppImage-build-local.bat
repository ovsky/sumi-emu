@ECHO OFF
SETLOCAL

REM --- Configuration ---
REM Set the path to your compiled 'sumi' executable (Windows version)
SET "SUMI_EXECUTABLE_PATH=build\bin\sumi.exe"
REM If it's not an .exe, or in a different subfolder, adjust this path.
REM Example: SET "SUMI_EXECUTABLE_PATH=build\sumi.jar"

REM Set the expected output name from your Windows packaging script.
REM This will NOT be an AppImage. It could be a ZIP, an EXE installer, etc.
SET "WINDOWS_PACKAGE_NAME=sumi_windows_package.zip"
REM Example: SET "WINDOWS_PACKAGE_NAME=sumi_installer.exe"

REM --- Script Logic ---

ECHO Checking for Sumi executable: %SUMI_EXECUTABLE_PATH%
IF EXIST "%SUMI_EXECUTABLE_PATH%" (
    ECHO Found %SUMI_EXECUTABLE_PATH%.

    REM Remove any previously made package in the base sumi folder
    IF EXIST ".\%WINDOWS_PACKAGE_NAME%" (
        ECHO Deleting old package: .\%WINDOWS_PACKAGE_NAME%
        DEL ".\%WINDOWS_PACKAGE_NAME%"
    )

    REM Enter AppImageBuilder (or your Windows packaging utility) folder
    IF NOT EXIST "AppImageBuilder" (
        ECHO ERROR: The 'AppImageBuilder' directory does not exist.
        ECHO Please ensure this directory contains your Windows packaging scripts/tools.
        GOTO EndScript
    )
    PUSHD "AppImageBuilder"
    ECHO Changed directory to %CD%

    REM Run the Windows build/packaging script
    REM This script (e.g., build.bat) is something YOU need to create or adapt.
    REM It should take the files from "..\build" and produce "%WINDOWS_PACKAGE_NAME%".
    REM Usage example: build.bat [source sumi build folder] [destination package file]
    ECHO Looking for Windows build script (build.bat or build.cmd)
    IF EXIST "build.bat" (
        ECHO Executing: build.bat "..\build" ".\%WINDOWS_PACKAGE_NAME%"
        CALL build.bat "..\build" ".\%WINDOWS_PACKAGE_NAME%"
    ) ELSE (
        IF EXIST "build.cmd" (
            ECHO Executing: build.cmd "..\build" ".\%WINDOWS_PACKAGE_NAME%"
            CALL build.cmd "..\build" ".\%WINDOWS_PACKAGE_NAME%"
        ) ELSE (
            ECHO ERROR: Neither 'build.bat' nor 'build.cmd' found in 'AppImageBuilder' directory.
            ECHO You need to create a Windows-specific script to package your application.
            POPD
            GOTO EndScript
        )
    )

    SET "BUILT_PACKAGE_PATH=.\%WINDOWS_PACKAGE_NAME%"
    IF EXIST "%BUILT_PACKAGE_PATH%" (
        ECHO Windows package built successfully: %BUILT_PACKAGE_PATH%
        REM Move the package to the main sumi folder
        ECHO Moving %WINDOWS_PACKAGE_NAME% to ..\
        MOVE "%WINDOWS_PACKAGE_NAME%" ".."
        POPD
        ECHO Returned to %CD%

        REM Show contents of current folder
        ECHO.
        ECHO Contents of current folder:
        DIR
        REM Show the packaged file specifically
        ECHO.
        ECHO Listing specific package:
        DIR ".\%WINDOWS_PACKAGE_NAME%"
        ECHO.
        ECHO "'%WINDOWS_PACKAGE_NAME%' is now located in the current folder."
        ECHO.
    ) ELSE (
        POPD
        ECHO ERROR: Windows package was not built. Expected file not found: %BUILT_PACKAGE_PATH%
        ECHO Check the output of your build.bat/build.cmd script in AppImageBuilder.
    )
) ELSE (
    ECHO.
    ECHO ERROR: "%SUMI_EXECUTABLE_PATH%" does not exist.
    ECHO.
    ECHO "No sumi executable (Windows version) found in the specified path!"
    ECHO.
    ECHO "You must first build a native Windows version of sumi (e.g., sumi.exe)"
    ECHO "and ensure it's in '%SUMI_EXECUTABLE_PATH%' before running this script!"
    ECHO.
)

:EndScript  
ENDLOCAL
ECHO Script finished.