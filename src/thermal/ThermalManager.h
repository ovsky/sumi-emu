#pragma once

class ThermalManager {
public:
    void Update();
    bool IsThrottled() const;
private:
    float GetCurrentTemperatureC() const; // implement for Android NDK/Binder or /sys/class/thermal
    void ThrottleEmulation();
    void UnthrottleEmulation();
    bool throttled = false;
    const float threshold = 100.0f;
};