#include "app/TimeFormat.h"

#include <cmath>
#include <cstdio>

namespace playeverything {

std::wstring FormatTimestamp(double seconds, bool forceHours) {
    if (!std::isfinite(seconds) || seconds < 0.0) {
        seconds = 0.0;
    }

    const long long totalSeconds = static_cast<long long>(std::floor(seconds + 1e-6));
    const long long hours = totalSeconds / 3600;
    const long long minutes = (totalSeconds % 3600) / 60;
    const long long secs = totalSeconds % 60;

    wchar_t buf[32]{};
    if (hours > 0 || forceHours) {
        std::swprintf(buf, std::size(buf), L"%lld:%02lld:%02lld", hours, minutes, secs);
    } else {
        std::swprintf(buf, std::size(buf), L"%02lld:%02lld", minutes, secs);
    }
    return std::wstring(buf);
}

double ClampSeekTarget(double targetSeconds, double durationSeconds) {
    if (!std::isfinite(targetSeconds) || targetSeconds < 0.0) {
        return 0.0;
    }
    if (std::isfinite(durationSeconds) && durationSeconds > 0.0 && targetSeconds > durationSeconds) {
        return durationSeconds;
    }
    return targetSeconds;
}

}  // namespace playeverything
