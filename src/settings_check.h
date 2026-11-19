#pragma once
// Stops the build with a plain-English message if panel_settings.h is missing.
#if __has_include("panel_settings.h")
#include "panel_settings.h"
#else
#error "panel_settings.h is missing: copy src/panel_settings.example.h to src/panel_settings.h and fill it in (README, step 5)"
#endif
