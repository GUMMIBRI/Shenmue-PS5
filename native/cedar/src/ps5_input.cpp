/*
  Native pad ABI declarations below are an altered subset of SDL_ps5pad.h,
  public SDL PS5 source ee4c47dc0d617b3bc8f35108f9956baf228a1322, retained at
  tools/pe-ps5-sdl3/20261029T010754Z-68fad8/src/src/joystick/ps5/SDL_ps5pad.h.
  They are not Cedar-authored ABI definitions. Cedar class/mapping code is new.

  Simple DirectMedia Layer
  Copyright (C) 1997-2015 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "cedar/ps5_input.hpp"
#include <cstddef>
#include <cstdint>

#if !defined(CEDAR_PS5)
#error "ps5_input.cpp requires the actual PS5 target; no host success stub exists"
#endif

// Layout retained from the licensed public header; unused API declarations and
// vibration/color structures were omitted. Do not infer Sony SDK equivalence.
struct PS5_PadTouch {
    std::uint16_t x, y;
    std::uint8_t finger, pad[3];
};
struct PS5_PadTouchData {
    std::uint8_t fingers, pad1[3];
    std::uint32_t pad2;
    PS5_PadTouch touch[2];
};
struct PS5_PadData {
    std::uint32_t buttons;
    struct { std::uint8_t x, y; } leftStick, rightStick;
    struct { std::uint8_t l2, r2; } analogButtons;
    std::uint16_t padding;
    struct { float x, y, z, w; } quat;
    struct { float x, y, z; } vel, acell;
    PS5_PadTouchData touch;
    std::uint8_t connected;
    std::uint64_t timestamp;
    std::uint8_t ext[16], count, unknown[15];
};
static_assert(sizeof(PS5_PadData) == 120, "Public pad ABI size changed");
static_assert(offsetof(PS5_PadData, timestamp) == 80, "Public timestamp ABI changed");
static_assert(offsetof(PS5_PadData, connected) == 76, "Public connected ABI changed");
extern "C" {
int scePadInit(void);
int scePadOpen(int user_id, int type, int index, void* param);
int scePadReadState(int handle, PS5_PadData* data);
int scePadClose(int handle);
}

namespace cedar {
namespace {
float stick(std::uint8_t axis) noexcept {
    const int centered = static_cast<int>(axis) - 128;
    return static_cast<float>(centered) / (centered < 0 ? 128.0f : 127.0f);
}
void map_button(std::uint32_t source, std::uint32_t mask, Button target,
                InputSample& sample) noexcept {
    if (source & mask) sample.held |= std::uint32_t{1} << static_cast<unsigned>(target);
}
} // namespace

Ps5Input::~Ps5Input() { (void)close(); }

Status Ps5Input::initialize_backend(int& native_error) noexcept {
    native_error = scePadInit();
    return native_error == 0 ? Status::ok : Status::backend_failure;
}

Status Ps5Input::open(int user_id) noexcept {
    if (is_open()) return Status::invalid_state;
    if (user_id < 0) return Status::invalid_argument;
    const int opened = scePadOpen(user_id, 0 /* standard port */, 0, nullptr);
    if (opened < 0) {
        error_ = opened;
        return Status::backend_failure;
    }
    handle_ = opened;
    error_ = 0;
    return Status::ok;
}

Status Ps5Input::close() noexcept {
    if (!is_open()) return Status::ok;
    error_ = scePadClose(handle_);
    if (error_ != 0) return Status::backend_failure;
    handle_ = -1;
    return Status::ok;
}

Status Ps5Input::poll(InputSample& sample) noexcept {
    sample = {}; // Never replay stale held input after a read error/disconnect.
    if (!is_open()) return Status::invalid_state;
    PS5_PadData data{};
    error_ = scePadReadState(handle_, &data);
    if (error_ != 0) return Status::backend_failure;
    sample.connected = data.connected != 0;
    if (!sample.connected) return Status::ok;

    // Deliberate scaffold mapping, not a recovered Shenmue control scheme:
    // Cross confirm, Circle cancel, Square action, Options menu, D-pad and L1/R1.
    // Triangle is secondary_action; physical clicks/triggers retain separate bits.
    map_button(data.buttons, 0x4000, Button::confirm, sample);
    map_button(data.buttons, 0x2000, Button::cancel, sample);
    map_button(data.buttons, 0x8000, Button::action, sample);
    map_button(data.buttons, 0x0008, Button::menu, sample);
    map_button(data.buttons, 0x0010, Button::up, sample);
    map_button(data.buttons, 0x0040, Button::down, sample);
    map_button(data.buttons, 0x0080, Button::left, sample);
    map_button(data.buttons, 0x0020, Button::right, sample);
    map_button(data.buttons, 0x0400, Button::left_shoulder, sample);
    map_button(data.buttons, 0x0800, Button::right_shoulder, sample);
    map_button(data.buttons, 0x1000, Button::secondary_action, sample);
    map_button(data.buttons, 0x0100, Button::left_trigger_button, sample);
    map_button(data.buttons, 0x0200, Button::right_trigger_button, sample);
    map_button(data.buttons, 0x0002, Button::left_stick_button, sample);
    map_button(data.buttons, 0x0004, Button::right_stick_button, sample);
    map_button(data.buttons, 0x100000, Button::touchpad_button, sample);
    // Preserve SDK axis orientation: positive X right, positive Y down.
    // Exact center 128 maps to zero; endpoints map to -1/+1. No hidden deadzone.
    sample.left_x = stick(data.leftStick.x);
    sample.left_y = stick(data.leftStick.y);
    sample.right_x = stick(data.rightStick.x);
    sample.right_y = stick(data.rightStick.y);
    sample.left_trigger = static_cast<float>(data.analogButtons.l2) / 255.0f;
    sample.right_trigger = static_cast<float>(data.analogButtons.r2) / 255.0f;
    return Status::ok;
}

Status Ps5Input::poll_service(void* context, InputSample* sample) noexcept {
    if (!sample) return Status::invalid_argument;
    *sample = {};
    if (!context) return Status::invalid_argument;
    return static_cast<Ps5Input*>(context)->poll(*sample);
}
} // namespace cedar
