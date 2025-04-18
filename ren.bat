@ECHO OFF
SETLOCAL EnableDelayedExpansion

REM --- Configuration ---
REM Set the string to search for in filenames and directory names
SET "search_string=sumi"
REM Set the string to replace the search string with
SET "replace_string=sumi"
REM Set the starting folder for the recursive search. Use "." for the current folder.
SET "start_folder=."

REM --- Safety Check ---
ECHO ======================================================================
ECHO WARNING: This script will attempt to recursively rename files
ECHO          and directories containing "%search_string%" to
ECHO          "%replace_string%" within the folder:
ECHO          "%start_folder%"
ECHO          and all its subfolders.
ECHO ======================================================================
ECHO.
ECHO Search String : %search_string%
ECHO Replace String: %replace_string%
ECHO Starting Folder: %start_folder%
ECHO.
ECHO PLEASE BACK UP YOUR DATA BEFORE PROCEEDING!
ECHO.
SET /P "confirm=Are you absolutely sure you want to continue? (Y/N): "
IF /I NOT "%confirm%"=="Y" (
    ECHO Operation cancelled by user.
    GOTO :EOF
)
ECHO.

REM --- Step 1: Rename Files ---
ECHO Renaming files...
REM Loop recursively through the start folder
FOR /R "%start_folder%" %%F IN (*) DO (
    REM Check if the file exists (it might have been moved if a parent dir was renamed - though dirs are renamed later)
    IF EXIST "%%F" (
        REM Get just the filename and extension
        SET "filename=%%~nxF"
        REM Check if the filename contains the search string (case-insensitive check)
        ECHO "!filename!" | FINDSTR /I /C:"%search_string%" > NUL
        IF !ERRORLEVEL! EQU 0 (
            REM Create the new filename using string substitution
            SET "new_filename=!filename:%search_string%=%replace_string%!"
            REM Ensure the name actually changed to avoid unnecessary REN commands
            IF NOT "!filename!"=="!new_filename!" (
                ECHO Renaming File: "%%F" to "!new_filename!"
                REM Perform the rename operation
                REN "%%F" "!new_filename!"
                REM Check for errors during rename
                IF !ERRORLEVEL! NEQ 0 (
                    ECHO ERROR: Failed to rename "%%F"
                )
            )
        )
    )
)
ECHO File renaming complete.
ECHO.

REM --- Step 2: Rename Directories (Deepest First) ---
ECHO Renaming directories (attempting deepest first)...
REM Create a temporary file to store directory paths
SET "temp_dir_list=%TEMP%\dirlist_%RANDOM%.txt"
IF EXIST "%temp_dir_list%" DEL "%temp_dir_list%"

REM Get all directories (/A:D), recursively (/S), bare format (/B)
REM Sort reverse (/R) - This sorts by name reverse, but often puts longer paths (deeper) later
REM We want longest paths first, so we use `sort /R` on the output of DIR
DIR "%start_folder%" /S /B /A:D | SORT /R > "%temp_dir_list%"

REM Process the sorted list from the temporary file
FOR /F "usebackq delims=" %%D IN ("%temp_dir_list%") DO (
    REM Check if the directory path still exists (a parent might have been renamed)
    IF EXIST "%%D\" (
        REM Get just the directory name
        SET "dirname=%%~nxD"
        REM Check if the directory name contains the search string (case-insensitive check)
        ECHO "!dirname!" | FINDSTR /I /C:"%search_string%" > NUL
        IF !ERRORLEVEL! EQU 0 (
            REM Create the new directory name
            SET "new_dirname=!dirname:%search_string%=%replace_string%!"
            REM Ensure the name actually changed
            IF NOT "!dirname!"=="!new_dirname!" (
                ECHO Renaming Dir : "%%D" to "!new_dirname!"
                REM Perform the rename operation
                REN "%%D" "!new_dirname!"
                REM Check for errors during rename
                IF !ERRORLEVEL! NEQ 0 (
                    ECHO ERROR: Failed to rename "%%D". It might be in use or permissions issue.
                )
            )
        )
    ) ELSE (
        REM This can happen if a parent directory was renamed earlier in this loop
        ECHO Skipping already moved/renamed directory: "%%D"
    )
)

REM Clean up the temporary file
IF EXIST "%temp_dir_list%" DEL "%temp_dir_list%"
ECHO Directory renaming complete.
ECHO.

ECHO ======================================================================
ECHO Renaming process finished. Please check the results carefully.
ECHO ======================================================================

ENDLOCAL
GOTO :EOF
