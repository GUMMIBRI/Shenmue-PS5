#pragma once
#include "cedar/platform.hpp"

namespace cedar {
enum class ProcessStage : std::uint8_t { none, user_service, pad, audio };
struct ProcessStatus {
    Status status{Status::ok};
    ProcessStage stage{ProcessStage::none};
    int native_error{};
    explicit operator bool() const noexcept { return status == Status::ok; }
};
// One instance, main-thread use, before opening devices. The process loader MUST
// already make the public user/pad/audio imports available. This does not load
// modules, select/enumerate users, open a controller or boot a game.
// Successful stages are retained for a bounded explicit retry after failure.
// There is no teardown: these are process-wide services and no termination ABI
// is assumed. Do not create a second initializer or initialize via SDL joystick/
// audio paths as well. Publish readiness only after every actual call succeeded.
class Ps5ProcessApis final {
public:
    Ps5ProcessApis() noexcept = default;
    Ps5ProcessApis(const Ps5ProcessApis&) = delete;
    Ps5ProcessApis& operator=(const Ps5ProcessApis&) = delete;
    ProcessStatus initialize_after_module_load() noexcept;
    bool ready() const noexcept { return user_ready_ && pad_ready_ && audio_ready_; }
    bool borrowed_user_service() const noexcept { return user_borrowed_; }
private:
    bool user_ready_{}, user_borrowed_{}, pad_ready_{}, audio_ready_{};
};
} // namespace cedar
