#pragma once

#include <string>

namespace playeverything {

std::wstring FormatTimestamp(double seconds, bool forceHours = false);
double ClampSeekTarget(double targetSeconds, double durationSeconds);

}  // namespace playeverything
