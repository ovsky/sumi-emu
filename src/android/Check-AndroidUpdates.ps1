Write-Host "--- Checking for dependency updates (requires gradle-versions-plugin) ---" -ForegroundColor Cyan
Write-Host "--- This only REPORTS updates, it does NOT apply them! ---" -ForegroundColor Yellow

# Ensure gradlew.bat exists

if (-not (Test-Path .\gradlew.bat)) {
Write-Error "gradlew.bat not found in current directory. Are you in the project root?"
exit 1
}

# Run the dependency check task

# We use -Drevision=release to favor stable releases

./gradlew.bat dependencyUpdates -Drevision=release

if ($LASTEXITCODE -ne 0) {
Write-Error "Dependency check command failed."
exit 1
}

Write-Host "--- Dependency check finished. Review the report above manually. ---" -ForegroundColor Green
