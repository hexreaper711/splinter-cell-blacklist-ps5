#include <rex/input/sony_feedback.h>
#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(sony_enabled, true, "Input/Sony Controller", "Enable supported Sony controller features");
REXCVAR_DEFINE_BOOL(sony_gestures, true, "Input/Sony Controller", "Enable controller touchpad gestures");
REXCVAR_DEFINE_BOOL(sony_lighting, true, "Input/Sony Controller", "Show episode and wanted state on the controller light");
REXCVAR_DEFINE_BOOL(sony_rumble, true, "Input/Sony Controller", "Enhance confirmed shot, damage and explosion rumble");
REXCVAR_DEFINE_BOOL(sony_triggers, true, "Input/Sony Controller", "Enable supported DualSense adaptive triggers");
REXCVAR_DEFINE_DOUBLE(sony_intensity, 0.5, "Input/Sony Controller", "Strength of added controller feedback").range(0.0, 1.0);

namespace rex::input::sony {
bool SetFeaturesEnabled(bool enabled) {
  // Hold the master off while changing the child switches. These are registered
  // in this translation unit, and a failed write leaves the bundle disabled.
  if (!rex::cvar::SetFlagByName("sony_enabled", "false")) return false;
  const char* value = enabled ? "true" : "false";
  for (const char* flag : {"sony_gestures", "sony_lighting",
                           "sony_rumble", "sony_triggers"}) {
    if (!rex::cvar::SetFlagByName(flag, value)) return false;
  }
  return rex::cvar::SetFlagByName("sony_enabled", value);
}

Options GetOptions() {
  // The SDL output worker and guest threads read while the frontend may write.
  // Registry queries take its lock; raw REXCVAR_GET references do not.
  using rex::cvar::Query;
  return {Query<bool>("sony_enabled"), Query<bool>("sony_gestures"),
          Query<bool>("sony_lighting"), Query<bool>("sony_rumble"),
          Query<bool>("sony_triggers"), static_cast<float>(Query<double>("sony_intensity"))};
}
}  // namespace rex::input::sony
