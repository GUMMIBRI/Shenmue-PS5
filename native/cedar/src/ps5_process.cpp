/*
  User-service declaration and documented already-initialized result adapted
  from public SDL PS5 driver ee4c47dc0d617b3bc8f35108f9956baf228a1322,
  retained SDL3 adaptation src/joystick/ps5/SDL_ps5pad.h / SDL_ps5joystick.c
  under tools/pe-ps5-sdl3/20261029T010754Z-68fad8/src.
  Altered source: original Cedar staged process-initialization wrapper.

  Copyright (C) 1997-2015 Sam Lantinga <slouken@libsdl.org>
  This software is provided 'as-is', without any express or implied
  warranty. In no event will the authors be held liable for any damages
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
#include "cedar/ps5_process.hpp"
#include "cedar/ps5_input.hpp"
#include "cedar/ps5_audio.hpp"
extern "C" int sceUserServiceInitialize(void*);

namespace cedar {
ProcessStatus Ps5ProcessApis::initialize_after_module_load() noexcept {
    if (!user_ready_) {
        const int result = sceUserServiceInitialize(nullptr);
        // The retained public SDL driver explicitly recognizes this one code.
        // No other native failure is interpreted as already-initialized success.
        constexpr std::uint32_t already_initialized = 0x80960003u;
        if (result != 0 && static_cast<std::uint32_t>(result) != already_initialized)
            return {Status::backend_failure, ProcessStage::user_service, result};
        user_borrowed_ = result != 0;
        user_ready_ = true;
    }
    if (!pad_ready_) {
        int native{};
        const auto result = Ps5Input::initialize_backend(native);
        if (result != Status::ok) return {result, ProcessStage::pad, native};
        pad_ready_ = true;
    }
    if (!audio_ready_) {
        int native{};
        const auto result = Ps5AudioOutput::initialize_api(native);
        if (result != Status::ok) return {result, ProcessStage::audio, native};
        audio_ready_ = true;
    }
    return {};
}
} // namespace cedar
