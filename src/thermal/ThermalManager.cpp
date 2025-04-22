#include "ThermalManager.h"
#include <stdio.h>

float ThermalManager::GetCurrentTemperatureC() const {
    // For Android: parse /sys/class/thermal or use BatteryManager NDK/JNI
    FILE* file = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!file) return 0.0f;
    float temp = 0.0f;
    fscanf(file, "%f", &temp);
    fclose(file);
    return temp / 1000.0f; // (may vary by device)
}

void ThermalManager::Update() {
    float temp = GetCurrentTemperatureC();
    if (temp >= threshold && !throttled) {
        ThrottleEmulation();
        throttled = true;
    } else if (temp < threshold - 5.0f && throttled) { // hysteresis
        UnthrottleEmulation();
        throttled = false;
    }
}

void ThermalManager::ThrottleEmulation() {
    // Reduce FPS cap, resolution, or process priority
}

void ThermalManager::UnthrottleEmulation() {
    // Restore normal emulation state
}

bool ThermalManager::IsThrottled() const { return throttled; }