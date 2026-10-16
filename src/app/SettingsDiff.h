#pragma once
#include "core/Settings.h"

namespace clipper {

// True when a change requires restarting the capture session (video, audio or buffer settings).
bool captureChanged(const Settings& a, const Settings& b);

}  // namespace clipper
