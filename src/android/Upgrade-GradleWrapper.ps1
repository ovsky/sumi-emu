<#
.SYNOPSIS
Attempts to upgrade the Gradle wrapper version.
.WARNING
You MUST manually determine and set the target Gradle version compatible
with your Android Gradle Plugin version. Blindly upgrading WILL break builds.
#>

# --- !!! WARNING !!! ---
# --- Manually set the desired compatible Gradle version here ---
$targetGradleVersion = "8.13" # <-- CHANGE THIS MANUALLY!

Write-Host "--- Attempting to upgrade Gradle Wrapper to version $targetGradleVersion ---" -ForegroundColor Yellow

if (-not (Test-Path .\gradlew.bat)) {
    Write-Error "gradlew.bat not found. Are you in the project root?"
    exit 1
}

./gradlew.bat wrapper --gradle-version $targetGradleVersion --distribution-type all

if ($LASTEXITCODE -ne 0) {
    Write-Error "Gradle wrapper upgrade command failed."
    exit 1
}

Write-Host "--- Gradle wrapper upgrade attempted. Sync project in Android Studio and test thoroughly. ---" -ForegroundColor Green