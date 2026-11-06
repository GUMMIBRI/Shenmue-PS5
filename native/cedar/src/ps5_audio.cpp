/*
  Public audio ABI declarations/constants below adapted from Simple DirectMedia
  Layer's PS5 audio header, ps5-payload-dev/SDL revision
  ee4c47dc0d617b3bc8f35108f9956baf228a1322, retained SDL3 adaptation at
  tools/pe-ps5-sdl3/20261029T010754Z-68fad8/src/src/audio/ps5/SDL_ps5audio.h.
  Altered source: independent Cedar C++ output ownership and submission wrapper.

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
#include "cedar/ps5_audio.hpp"

extern "C" {
int sceAudioOutInit(void);
int sceAudioOutOpen(int user_id, int type, int index, unsigned int frames,
                    unsigned int frequency, unsigned int format);
int sceAudioOutOutput(int handle, const void* buffer);
int sceAudioOutClose(int handle);
}

namespace cedar {
Ps5AudioOutput::~Ps5AudioOutput() { close(); }
Status Ps5AudioOutput::initialize_api(int& native_error) noexcept {
    native_error = sceAudioOutInit();
    return native_error == 0 ? Status::ok : Status::backend_failure;
}
Status Ps5AudioOutput::open(int user, std::uint32_t channels,
                           std::uint32_t frames) noexcept {
    if (opened()) return Status::invalid_state;
    if (user < 0 || (channels != 1 && channels != 2) || frames < 256 ||
        frames > 2048 || frames % 256) return Status::invalid_argument;
    // Public SDL backend constants: main port=0, float mono=3/stereo=4.
    const int result = sceAudioOutOpen(user, 0, 0, frames, 48000,
                                       channels == 1 ? 3u : 4u);
    if (result < 1) {
        native_error_ = result;
        return Status::backend_failure;
    }
    handle_ = result;
    native_error_ = 0;
    channels_ = channels;
    frames_ = frames;
    return Status::ok;
}
Status Ps5AudioOutput::submit(const float* source, std::size_t frames) noexcept {
    if (!opened()) return Status::invalid_state;
    if (!source || frames != frames_) return Status::invalid_argument;
    const std::size_t count = frames * channels_;
    for (std::size_t i = 0; i < count; ++i)
        if (!(source[i] >= -1.0f && source[i] <= 1.0f))
            return Status::invalid_argument;
    for (std::size_t i = 0; i < count; ++i) staging_[i] = source[i];
    const int result = sceAudioOutOutput(handle_, staging_);
    native_error_ = result < 0 ? result : 0;
    return result < 0 ? Status::backend_failure : Status::ok;
}
Status Ps5AudioOutput::close() noexcept {
    if (!opened()) return Status::ok;
    const int result = sceAudioOutClose(handle_);
    native_error_ = result < 0 ? result : 0;
    if (result < 0) return Status::backend_failure; // Preserve handle for retry.
    handle_ = -1;
    channels_ = frames_ = 0;
    return Status::ok;
}
Status Ps5AudioOutput::submit_queued(AudioRing& queue) noexcept {
    if (!opened()) return Status::invalid_state;
    if (queue.sample_rate() != 48000 || queue.channels() != channels_)
        return Status::invalid_argument;
    const auto staged = queue.peek(staging_, frames_);
    if (staged != Status::ok) return staged;
    const auto result = submit(staging_, frames_);
    return result == Status::ok ? queue.discard(frames_) : result;
}
} // namespace cedar
