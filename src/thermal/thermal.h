// app/src/main/cpp/core/thermal.h
#ifndef SUMI_THERMAL_H
#define SUMI_THERMAL_H

#include <atomic>

namespace Core {
    /**
     * Sets the current thermal throttling factor
     * @param factor Throttling factor (0.0 = stop, 1.0 = full speed)
     */
    void SetThrottleFactor(float factor);

    /**
     * Gets the current thermal throttling factor
     * @return Current throttling factor between 0.0 and 1.0
     */
    float GetThrottleFactor();
}

#endif // SUMI_THERMAL_H