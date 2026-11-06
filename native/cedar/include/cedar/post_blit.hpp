#pragma once
#include "cedar/post_types.hpp"

namespace cedar {
struct PostBlitOptions {
    SDL_GPUFilter filter{SDL_GPU_FILTER_NEAREST}; // Actual selected baseline, not engine discovery.
    bool allow_perceptual_format_conversion{};
};
// Concrete baseline callback for Cedar's dedicated color images. It does not
// emulate an unknown engine's HDR/tonemap/overlay policy. Context must point to
// a live PostBlitOptions. Unknown transfer requires exact source/target format;
// explicit perceptual SDR may convert UNORM8/float16 storage when opted in.
// Returns recording status only; SDL's void blit has no execution/error result.
Status record_post_blit(void* options, SDL_GPUCommandBuffer* commands,
                        const PostSource& source, const PostTarget& target) noexcept;
} // namespace cedar
