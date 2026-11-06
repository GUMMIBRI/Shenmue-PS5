// Original SDL conversion bridge; underlying codec provenance: SPSD-AICA.md.
#pragma once
#include "cedar/spsd_aica.hpp"
#include <SDL3/SDL_audio.h>
namespace cedar {
enum class SpsdVoiceError : std::uint8_t { none, invalid_state, invalid_argument,
    invalid_buffer, queue_full, decoder_failure, converter_failure, converter_contract, nonfinite_output };
struct SpsdVoiceResult {
    SpsdVoiceError error{SpsdVoiceError::none};
    SpsdAicaResult decoder{};
    std::size_t frames{}, converted_bytes{}, clipped_samples{};
    bool decoder_advanced{}, queue_acceptance_uncertain{}, converter_consumption_uncertain{};
    bool output_modified{}, output_valid{}, drained{};
    explicit operator bool() const noexcept { return error==SpsdVoiceError::none; }
};
class SpsdVoice final {
public:
    static constexpr std::size_t max_queued_bytes=1024u*1024u;
    SpsdVoice() noexcept=default;
    ~SpsdVoice();
    SpsdVoice(const SpsdVoice&)=delete;
    SpsdVoice& operator=(const SpsdVoice&)=delete;
    SpsdVoiceResult start(const SpsdView&,std::uint32_t output_channels) noexcept;
    SpsdVoiceResult pump(std::size_t frames,std::int16_t* pcm,std::size_t pcm_capacity,
        std::int16_t* mono,std::size_t mono_capacity) noexcept;
    SpsdVoiceResult read(float* output,std::size_t sample_capacity) noexcept;
    SpsdVoiceResult finish() noexcept;
    SpsdVoiceResult drain_status() noexcept;
    void stop() noexcept;
    bool faulted() const noexcept { return faulted_; }
    std::uint64_t frames_remaining() const noexcept { return decoder_.frames_remaining(); }
    const char* last_error() const noexcept { return error_; }
private:
    bool valid_buffer(const void*,std::size_t,std::size_t,std::size_t) const noexcept;
    SpsdVoiceResult native_failure(bool consumption=false) noexcept;
    SpsdAicaStream decoder_{};
    SDL_AudioStream* converter_{};
    const void* source_{};
    std::size_t source_bytes_{};
    std::uint32_t output_channels_{};
    bool flushed_{},faulted_{};
    char error_[256]{};
};
}
