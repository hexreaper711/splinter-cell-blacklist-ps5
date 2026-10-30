#pragma once
/**
 * [ps5 port] DualSense controllers through scePad: one Xbox 360 pad per
 * logged-in PS5 user that has a controller.
 */
#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

#include <rex/input/input_driver.h>
#include <rex/input/sony_feedback.h>

namespace rex::input::ps5 {

class Ps5InputDriver final : public InputDriver {
 public:
  explicit Ps5InputDriver(rex::ui::Window* window, size_t window_z_order);
  ~Ps5InputDriver() override;

  X_STATUS Setup() override;

  // ReXGlue 0.10.0's device interface: a device is one connection of a pad (its id is the
  // connection's generation, never reused); InputSystem assigns devices to the game's users.
  void EnumerateDevices(std::vector<DeviceInfo>& out) override;
  X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) override;
  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t flags,
                                 X_INPUT_CAPABILITIES* out_caps) override;
  X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) override;
  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t flags,
                              X_INPUT_KEYSTROKE* out_keystroke) override;

  // The state for the host's own menus and the system dialogs: like GetDeviceState, but read
  // even while the game's input is held back. By pad slot (0..3).
  X_RESULT GetStateUi(uint32_t user_index, X_INPUT_STATE* out_state);

 private:
  // gta4-ps5's per-user calls (0.9.0's interface), by pad slot (0..3).
  X_RESULT GetCapabilities(uint32_t user_index, uint32_t flags, X_INPUT_CAPABILITIES* out_caps);
  X_RESULT GetState(uint32_t user_index, X_INPUT_STATE* out_state);
  X_RESULT SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration);
  X_RESULT GetKeystroke(uint32_t user_index, uint32_t flags, X_INPUT_KEYSTROKE* out_keystroke);
  // A device's pad slot, or -1 when that connection is gone.
  int SlotFor(DeviceId id);

  static constexpr size_t kMaxPads = 4;
  struct Pad {
    int32_t user_id = -1;
    int32_t handle = -1;
    uint32_t packet_number = 0;
    X_INPUT_GAMEPAD last_gamepad{};
    uint32_t keystroke_buttons = 0;  // the buttons the keystroke events last reported
    // DualSense features (rex::input::sony, the PC port's): the game's own
    // rumble, and what was last sent to the controller.
    uint64_t generation = 0;  // (its connection to the feature service)
    uint16_t native_low = 0, native_high = 0;
    uint16_t pulse_low = 0, pulse_high = 0;
    uint8_t sent_large = 0, sent_small = 0;
    bool light_set = false;
    std::array<uint8_t, 3> color{};
    bool triggers_set = false;
    std::array<uint8_t, 120> trigger_param{};  // (the last ScePadTriggerEffectParam sent)
    int trigger_failures = 0;
    uint64_t next_feedback_ms = 0;
  };
  // A pad by the game's user index (0..3: the PS5 users in login order).
  Pad* PadFor(uint32_t user_index);
  void RefreshPads();
  X_RESULT ReadState(uint32_t user_index, bool active, X_INPUT_STATE* out_state);
  // The light bar, trigger effects and added rumble, sent with the game's own
  // reads (every 8 ms at most; a controller is told only what changed): every
  // scePad call on a controller is made from the thread that reads it, under
  // mutex_, never from two threads at once.
  void ApplyFeedback(uint32_t user_index, Pad& pad);
  void SendVibration(Pad& pad);  // (the game's rumble with the added pulses)

  std::mutex mutex_;
  std::array<Pad, kMaxPads> pads_{};
  uint64_t last_refresh_ms_ = 0;
  uint64_t next_generation_ = 1;
};

}  // namespace rex::input::ps5
