cd src\android
gradlew --no-daemon --stacktrace --build-cache --parallel --configure-on-demand assembleMainlineRelease

cd app\build\outputs\apk\
explorer .

pause
exit