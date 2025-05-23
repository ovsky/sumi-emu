<#
.SYNOPSIS
    Recursively finds CMakeLists.txt files and updates the cmake_minimum_required() directive.
.DESCRIPTION
    This script searches the current directory and all subdirectories for files named "CMakeLists.txt".
    In each found file, it looks for lines like "cmake_minimum_required(...)" (with any content
    inside the parentheses) and changes the entire content within the parentheses to
    "VERSION <TargetVersion>", resulting in "cmake_minimum_required(VERSION <TargetVersion>)".
    <TargetVersion> is a configurable variable in the script.
    It logs the path of modified files and the specific lines that were changed.
.NOTES
    Author: Gemini
    Version: 1.1
    Make sure to run this script from the root directory where you want the search to begin.
    It's always a good practice to back up your files before running scripts that modify them in bulk.
#>

# --- Configuration ---
[string]$TargetCMakeVersion = "3.10" # Change this to your desired CMake version (e.g., "3.12", "3.20")
# --- End Configuration ---

# --- Script Body ---
Write-Host "------------------------------"
Write-Host "STARTING CMakeLists.txt MODIFIER..."
Write-Host "------------------------------"
Write-Host "🚀 Starting CMake version update to '$TargetCMakeVersion'..."
Write-Host "📂 Script working directory: $(Get-Location)"

# Get all CMakeLists.txt files recursively
try {
    $FilesToProcess = Get-ChildItem -Path "." -Recurse -Filter "CMakeLists.txt" -File -ErrorAction Stop
}
catch {
    Write-Error "❌ Error finding CMakeLists.txt files: $($_.Exception.Message)"
    exit 1
}

if ($null -eq $FilesToProcess -or $FilesToProcess.Count -eq 0) {
    Write-Host "🤷 No CMakeLists.txt files found in the current directory or subdirectories."
    exit 0
}

Write-Host "🔍 Found $($FilesToProcess.Count) CMakeLists.txt file(s) to check."
Write-Host "---" # Separator

[int]$UpdatedFilesCount = 0
[int]$TotalLinesChangedCount = 0

# Regex to precisely match the cmake_minimum_required line.
# It looks for "cmake_minimum_required(...)" with any non-empty content in the parentheses.
$RegexPattern = '^\s*cmake_minimum_required\s*\([^)]+\)\s*$'

# The standardized replacement line format
$NewCMakeMinimumRequiredLine = "cmake_minimum_required(VERSION $TargetCMakeVersion)"

foreach ($FileItem in $FilesToProcess) {
    $FilePath = $FileItem.FullName
    Write-Verbose "Processing file: $FilePath"

    try {
        # Read content as an array of strings, ensuring it's always an array.
        # Explicitly use UTF8 encoding, common for CMake files.
        $OriginalLines = @(Get-Content -Path $FilePath -Encoding UTF8 -ErrorAction Stop)

        $NewLines = @()
        $FileHasBeenModified = $false

        if ($OriginalLines.Count -eq 0) {
            Write-Verbose "Skipping empty file: $FilePath"
            continue # Move to the next file
        }

        foreach ($CurrentLine in $OriginalLines) {
            $TrimmedCurrentLine = $CurrentLine.Trim() # Trim for reliable matching and comparison

            if ($TrimmedCurrentLine -match $RegexPattern) {
                # Line matches the pattern for cmake_minimum_required(...)
                if ($TrimmedCurrentLine -ne $NewCMakeMinimumRequiredLine) {
                    # Only replace if it's actually different from the target
                    if (-not $FileHasBeenModified) {
                        # Log file header only once per modified file
                        Write-Host "🔧 [MODIFIED] File: $FilePath"
                        $FileHasBeenModified = $true
                    }
                    Write-Host "  📝 Original: $TrimmedCurrentLine"
                    Write-Host "  ✨ New     : $NewCMakeMinimumRequiredLine"
                    $NewLines += $NewCMakeMinimumRequiredLine # Add the standardized new line
                    $TotalLinesChangedCount++
                } else {
                    # Line already matches the target, add original line (preserves exact original spacing if it was already correct)
                    $NewLines += $CurrentLine
                }
            } else {
                # Line does not match the pattern, add as is
                $NewLines += $CurrentLine
            }
        }

        if ($FileHasBeenModified) {
            # Write the modified content back to the file, preserving UTF8 encoding.
            Set-Content -Path $FilePath -Value $NewLines -Encoding UTF8 -ErrorAction Stop
            Write-Host "---" # Separator after a modified file's logs
            $UpdatedFilesCount++
        }
    }
    catch {
        Write-Warning "⚠️ Error processing file '$FilePath': $($_.Exception.Message)"
        Write-Host "---" # Separator even for errors
    }
}

Write-Host "🏁 Script finished."
Write-Host ""
Write-Host "  Total CMakeLists.txt files scanned: $($FilesToProcess.Count)"
Write-Host "  Files updated: $UpdatedFilesCount"
Write-Host "  Total lines changed: $TotalLinesChangedCount"
Write-Host "---"