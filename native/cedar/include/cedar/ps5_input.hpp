// Original Cedar adapter interface; native ABI provenance is in ps5_input.cpp.
#pragma once
#include "cedar/platform.hpp"

namespace cedar {
class Ps5Input final {
public:
    Ps5Input() noexcept = default;
    ~Ps5Input();
    Ps5Input(const Ps5Input&) = delete;
    Ps5Input& operator=(const Ps5Input&) = delete;
    Ps5Input(Ps5Input&&) = delete;
    Ps5Input& operator=(Ps5Input&&) = delete;

    // Process owner calls once before opening pads. No user service or account
    // enumeration is performed. Return code is never treated as success by guess.
    static Status initialize_backend(int& native_error) noexcept;
    // Caller supplies an actual selected user ID after its own user-service setup.
    // Opens standard port index 0 only; does not borrow an already-open handle.
    Status open(int user_id) noexcept;
    // An explicit close reports failures. A failed close retains ownership so
    // the caller can retry; destructor makes a final best-effort close only.
    Status close() noexcept;
    Status poll(InputSample& sample) noexcept;
    int native_error() const noexcept { return error_; }
    bool is_open() const noexcept { return handle_ >= 0; }

    // Callback may be installed in Services only with context pointing to this
    // Ps5Input. Full platform composition must supply its other service methods.
    static Status poll_service(void* context, InputSample* sample) noexcept;

private:
    int handle_{-1};
    int error_{};
};
} // namespace cedar
