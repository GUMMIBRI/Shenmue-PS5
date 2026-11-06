#include "cedar/post_blit.hpp"
#include <SDL3/SDL_init.h>
#include <cmath>

namespace cedar {
namespace {
bool valid_rect(PixelRect rect, Uint32 width, Uint32 height) noexcept {
    return width && height && width <= 8192 && height <= 8192 && rect.width && rect.height &&
        rect.x <= width && rect.width <= width - rect.x && rect.y <= height && rect.height <= height - rect.y;
}
bool perceptual_storage(SDL_GPUTextureFormat f) noexcept {
    return f == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM || f == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ||
        f == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
}
}
Status record_post_blit(void* context, SDL_GPUCommandBuffer* commands,
                        const PostSource& source, const PostTarget& target) noexcept {
    if (!SDL_IsMainThread()) return Status::invalid_state;
    if (!context || !commands || !source.device || source.device != target.device ||
        !source.texture || !target.texture || source.texture == target.texture ||
        !valid_rect(source.crop, source.width, source.height) || !valid_rect(target.viewport, target.width, target.height) ||
        (target.load_op != SDL_GPU_LOADOP_CLEAR && target.load_op != SDL_GPU_LOADOP_LOAD && target.load_op != SDL_GPU_LOADOP_DONT_CARE))
        return Status::invalid_argument;
    const auto options = *static_cast<const PostBlitOptions*>(context);
    if (options.filter != SDL_GPU_FILTER_NEAREST && options.filter != SDL_GPU_FILTER_LINEAR) return Status::invalid_argument;
    if (source.format == SDL_GPU_TEXTUREFORMAT_INVALID || target.format == SDL_GPU_TEXTUREFORMAT_INVALID) return Status::invalid_argument;
    if (source.format != target.format && !(options.allow_perceptual_format_conversion &&
        source.encoding == PostColorEncoding::perceptual_sdr_unorm && perceptual_storage(source.format) && perceptual_storage(target.format)))
        return Status::unavailable;
    if (!std::isfinite(target.clear_color.r) || !std::isfinite(target.clear_color.g) ||
        !std::isfinite(target.clear_color.b) || !std::isfinite(target.clear_color.a)) return Status::invalid_argument;
    SDL_GPUBlitInfo blit{};
    blit.source.texture = source.texture; blit.source.x = source.crop.x; blit.source.y = source.crop.y;
    blit.source.w = source.crop.width; blit.source.h = source.crop.height;
    blit.destination.texture = target.texture; blit.destination.x = target.viewport.x; blit.destination.y = target.viewport.y;
    blit.destination.w = target.viewport.width; blit.destination.h = target.viewport.height;
    blit.load_op = target.load_op; blit.clear_color = target.clear_color; blit.filter = options.filter;
    blit.cycle = target.load_op != SDL_GPU_LOADOP_LOAD;
    SDL_BlitGPUTexture(commands, &blit);
    return Status::ok;
}
} // namespace cedar
