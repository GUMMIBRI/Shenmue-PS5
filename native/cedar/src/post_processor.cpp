#include "cedar/post_processor.hpp"
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_thread.h>
#include <cmath>

namespace cedar {
namespace {
bool rect(PixelRect r, Uint32 w, Uint32 h) noexcept {
    return w && h && w <= 8192 && h <= 8192 && r.width && r.height && r.x <= w && r.y <= h &&
        r.width <= w - r.x && r.height <= h - r.y;
}
bool same(const PostProcessorConfig& a, const PostProcessorConfig& b) noexcept {
    if (a.fsr.enabled != b.fsr.enabled || a.fsr.rcas_enabled != b.fsr.rcas_enabled ||
        a.fsr.sharpness_stops != b.fsr.sharpness_stops || a.display.mode != b.display.mode ||
        a.display.logical_rows != b.display.logical_rows || a.display.filter != b.display.filter ||
        a.display.scanline_strength != b.display.scanline_strength || a.display.mask_strength != b.display.mask_strength ||
        a.movie.cleanup != b.movie.cleanup || a.movie.blur_backdrop != b.movie.blur_backdrop ||
        a.movie.blur_radius_pixels != b.movie.blur_radius_pixels || a.movie.backdrop_brightness != b.movie.backdrop_brightness ||
        a.movie.pixel_aspect != b.movie.pixel_aspect) return false;
    for (unsigned i = 0; i < 3; ++i) if (a.display.mask_color[i] != b.display.mask_color[i]) return false;
    return true;
}
PixelRect fitted(const PostSource& source, float pixel_aspect, PixelRect viewport) noexcept {
    const double aspect = static_cast<double>(source.crop.width) * pixel_aspect / source.crop.height;
    Uint32 width = viewport.width, height = viewport.height;
    if (aspect > static_cast<double>(width) / height)
        height = static_cast<Uint32>(std::floor(width / aspect));
    else width = static_cast<Uint32>(std::floor(height * aspect));
    if (!width) width = 1;
    if (!height) height = 1;
    return {viewport.x + (viewport.width - width) / 2, viewport.y + (viewport.height - height) / 2, width, height};
}
PixelRect movie_fsr_extent(const PostSource& source, float pixel_aspect, PixelRect viewport) noexcept {
    const auto center = fitted(source, pixel_aspect, viewport);
    Uint32 a = source.crop.width, b = source.crop.height;
    while (b) { const auto remainder = a % b; a = b; b = remainder; }
    const auto unit_w = source.crop.width / a, unit_h = source.crop.height / a;
    const auto mx = center.width / unit_w, my = center.height / unit_h;
    const auto multiple = mx < my ? mx : my;
    return {0,0,unit_w * multiple,unit_h * multiple};
}
SDL_GPUTexture* texture(SDL_GPUDevice* device, Uint32 w, Uint32 h) noexcept {
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D; info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    info.width = w; info.height = h; info.layer_count_or_depth = 1; info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    return SDL_CreateGPUTexture(device, &info);
}
PostSource compact_source(SDL_GPUDevice* device, SDL_GPUTexture* tex, Uint32 w, Uint32 h,
                           const PostSource& original) noexcept {
    return {device, tex, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, w, h, {0,0,w,h},
            original.content, original.encoding, original.opaque};
}
}
PostProcessor::~PostProcessor() { shutdown(); }
bool PostProcessor::thread() const noexcept {
    return SDL_IsMainThread() && (!owner_thread_ || SDL_GetCurrentThreadID() == owner_thread_);
}
Status PostProcessor::fail(Status status, const char* text) noexcept {
    std::size_t i = 0;
    if (text) for (; i + 1 < sizeof(error_) && text[i]; ++i) error_[i] = text[i];
    error_[i] = '\0'; return status;
}
Status PostProcessor::initialize(SDL_GPUDevice* device, SDL_GPUTextureFormat format,
                                 OriginalPostPresentation original, void* context) noexcept {
    if (!thread() || device_) return fail(Status::invalid_state, "Post processor requires empty main-thread ownership");
    if (!device || format == SDL_GPU_TEXTUREFORMAT_INVALID || !original)
        return fail(Status::invalid_argument, "Actual device, target format and original presentation callback required");
    device_ = device; target_format_ = format; original_ = original; callback_context_ = context;
    owner_thread_ = SDL_GetCurrentThreadID(); error_[0] = '\0'; return Status::ok;
}
Status PostProcessor::configure(const PostProcessorConfig& config) noexcept {
    if (!thread() || !device_) return fail(Status::invalid_state, "Post processor not initialized on its creating thread");
    if (!std::isfinite(config.fsr.sharpness_stops) || config.fsr.sharpness_stops < 0 || config.fsr.sharpness_stops > 2 ||
        (config.display.mode != DisplayEffectMode::off && config.display.mode != DisplayEffectMode::scanlines && config.display.mode != DisplayEffectMode::crt) ||
        (config.movie.cleanup != MovieCleanup::off && config.movie.cleanup != MovieCleanup::gentle && config.movie.cleanup != MovieCleanup::strong) ||
        !std::isfinite(config.movie.pixel_aspect) || config.movie.pixel_aspect < 0.0625f || config.movie.pixel_aspect > 16 ||
        (config.display.mode != DisplayEffectMode::off && (!config.display.logical_rows || config.display.logical_rows > 8192 ||
         !(config.display.scanline_strength >= 0 && config.display.scanline_strength <= 0.25f) ||
         !(config.display.mask_strength >= 0 && config.display.mask_strength <= 0.15f) ||
         (config.display.filter != SDL_GPU_FILTER_NEAREST && config.display.filter != SDL_GPU_FILTER_LINEAR))) ||
        (config.movie.blur_backdrop && (!(config.movie.blur_radius_pixels >= 0.25f && config.movie.blur_radius_pixels <= 16) ||
         !(config.movie.backdrop_brightness >= 0 && config.movie.backdrop_brightness <= 1))))
        return fail(Status::invalid_argument, "Invalid explicit post options, logical rows or producer pixel aspect");
    for (float value : config.display.mask_color)
        if (!(value >= 0 && value <= 1)) return fail(Status::invalid_argument, "Display mask weights must be finite unit values");
    const bool changed = !same(config_, config);
    const auto configured = fsr_.configure(config.fsr);
    if (configured != Status::ok) return fail(configured, fsr_.last_error());
    config_ = config;
    if (changed) reset_failure();
    error_[0] = '\0'; return Status::ok;
}
void PostProcessor::reset_failure() noexcept {
    if (!thread()) return;
    fsr_failed_ = display_failed_ = movie_failed_ = targets_failed_ = false;
    for (auto& failure : failures_) failure = {};
    fsr_.reset_failure();
}
Status PostProcessor::begin_movie(std::uint64_t serial) noexcept {
    if (!thread() || !device_ || movie_active_) return fail(Status::invalid_state, "End the active movie lifetime before beginning another");
    if (!serial) return fail(Status::invalid_argument, "Movie producer serial must be nonzero");
    if (movie_ready_) {
        const auto status = movie_.begin_source(serial);
        if (status != Status::ok) return fail(status, movie_.last_error());
    }
    serial_ = serial; movie_active_ = true; error_[0] = '\0'; return Status::ok;
}
Status PostProcessor::end_movie(std::uint64_t serial) noexcept {
    if (!thread() || !movie_active_ || serial != serial_) return fail(Status::invalid_state, "Movie retirement serial does not match");
    if (movie_ready_) {
        const auto status = movie_.end_source(serial);
        if (status != Status::ok) return fail(status, movie_.last_error());
    }
    serial_ = 0; movie_active_ = false; release_targets(); error_[0] = '\0'; return Status::ok;
}
void PostProcessor::effect_error(PostProcessorRecord& result, Status status, const char* message, Uint32 bit) noexcept {
    if (result.first_effect_error == Status::ok) {
        result.first_effect_error = status;
        fail(status, message && *message ? message : "Requested effect was not completely recorded");
        std::size_t i = 0;
        for (; i + 1 < sizeof(result.first_effect_message) && error_[i]; ++i) result.first_effect_message[i] = error_[i];
        result.first_effect_message[i] = '\0';
    }
    result.latched_mask |= bit & result.requested_mask;
}
void PostProcessor::remember_failure(unsigned slot, Status status, const char* message) noexcept {
    auto& failure = failures_[slot];
    failure.status = status;
    std::size_t i = 0;
    if (message) for (; i + 1 < sizeof(failure.message) && message[i]; ++i) failure.message[i] = message[i];
    failure.message[i] = '\0';
}
Status PostProcessor::baseline(SDL_GPUCommandBuffer* commands, const PostSource& source,
                               const PostTarget& target, PostProcessorRecord& result) noexcept {
    result.last_valid_source = source;
    result.baseline_called = true;
    result.fallback_used = result.first_effect_error != Status::ok;
    result.baseline_status = original_(callback_context_, commands, source, target);
    result.baseline_recorded = result.baseline_status == Status::ok;
    result.presentation_recorded = result.baseline_recorded;
    if (!result.baseline_recorded && result.first_effect_error == Status::ok)
        fail(result.baseline_status, "Original presentation callback failed to record presentation");
    if (result.first_effect_error != Status::ok) fail(result.first_effect_error, result.first_effect_message);
    return result.baseline_status;
}
Status PostProcessor::ensure_movie() noexcept {
    if (movie_ready_) return Status::ok;
    const auto status = movie_.initialize(device_, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    if (status != Status::ok) {
        fail(status, movie_.last_error()); movie_failed_ = true; remember_failure(2, status, error_); return status;
    }
    const auto started = movie_.begin_source(serial_);
    if (started != Status::ok) {
        fail(started, movie_.last_error()); movie_.shutdown(); movie_failed_ = true;
        remember_failure(2, started, error_); return started;
    }
    movie_ready_ = true; return Status::ok;
}
Status PostProcessor::prepare_movie(Uint32 iw, Uint32 ih, Uint32 ow, Uint32 oh, bool cleanup) noexcept {
    const std::uint64_t bytes = std::uint64_t{iw} * ih * (cleanup ? 8u : 4u) + std::uint64_t{ow} * oh * 4u;
    if (bytes > max_owned_bytes) return fail(Status::unavailable, "Post processor movie intermediates exceed64MiB logical budget");
    if (crop_ && presentation_ && (!cleanup || clean_) && iw == iw_ && ih == ih_ && ow == ow_ && oh == oh_)
        return Status::ok;
    // Release old logical ownership before replacements; SDL separately defers
    // destruction of in-flight physical versions. Original caller input survives.
    release_targets();
    auto* crop = texture(device_, iw, ih);
    auto* clean = cleanup ? texture(device_, iw, ih) : nullptr;
    auto* presentation = texture(device_, ow, oh);
    if (!crop || (cleanup && !clean) || !presentation) {
        const auto status = fail(Status::backend_failure, SDL_GetError());
        if (crop) SDL_ReleaseGPUTexture(device_, crop);
        if (clean) SDL_ReleaseGPUTexture(device_, clean);
        if (presentation) SDL_ReleaseGPUTexture(device_, presentation);
        return status;
    }
    crop_ = crop; clean_ = clean; presentation_ = presentation;
    iw_ = iw; ih_ = ih; ow_ = ow; oh_ = oh; return Status::ok;
}

Status PostProcessor::record(SDL_GPUCommandBuffer* commands, const PostSource& input,
    const PostTarget& destination, MovieSourceReadiness readiness, PostProcessorRecord& result) noexcept {
    const auto original = input;
    const auto target = destination;
    result = {};
    error_[0] = '\0';
    if (!thread() || !device_ || !original_)
        return fail(Status::invalid_state, "Post processor requires initialized creating-thread ownership");
    if (!commands || !original.texture || !target.texture || original.device != device_ || target.device != device_ ||
        original.texture == target.texture || target.format != target_format_ ||
        !rect(original.crop, original.width, original.height) || !rect(target.viewport, target.width, target.height))
        return fail(Status::invalid_argument, "Actual distinct same-device post source/target and bounded crop/viewport required");
    if (original.content == FrameContent::native_ui)
        return fail(Status::unavailable, "Caller-native UI must be recorded after post-processing, outside this facility");
    if (original.content != FrameContent::game && original.content != FrameContent::movie)
        return fail(Status::invalid_argument, "Explicit game/movie content classification required");
    const bool is_movie = original.content == FrameContent::movie;
    if (is_movie && (!movie_active_ || !serial_ ||
        (readiness != MovieSourceReadiness::completed && readiness != MovieSourceReadiness::earlier_in_same_commands)))
        return fail(Status::unavailable, "Movie needs its actual active serial and producer-confirmed source readiness");
    if (config_.fsr.enabled) result.requested_mask |= post_fsr1 | (config_.fsr.rcas_enabled ? post_rcas : 0u);
    if (config_.display.mode != DisplayEffectMode::off) result.requested_mask |= post_display;
    if (is_movie && config_.movie.cleanup != MovieCleanup::off) result.requested_mask |= post_movie_cleanup;
    if (is_movie && config_.movie.blur_backdrop) result.requested_mask |= post_movie_backdrop;
    result.last_valid_source = original;
    if (original.texture == crop_ || original.texture == clean_ || original.texture == presentation_ ||
        target.texture == crop_ || target.texture == clean_ || target.texture == presentation_)
        return fail(Status::invalid_argument, "Caller source/target cannot alias this processor's intermediates");
    if (!result.requested_mask) return baseline(commands, original, target, result); // Preserve actual engine baseline.
    PostSource current = original;
    float pixel_aspect = config_.movie.pixel_aspect;
    bool composed = false;
    bool movie_work = is_movie;
    if (is_movie) {
        if (targets_failed_ || movie_failed_) {
            const auto& failure = failures_[targets_failed_ ? 3 : 2];
            effect_error(result, failure.status, failure.message,
                         post_movie_cleanup | post_movie_backdrop); movie_work = false;
        } else if (original.encoding != PostColorEncoding::perceptual_sdr_unorm ||
            (original.format != SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM && original.format != SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM)) {
            effect_error(result, Status::unavailable, "Active movie graph requires explicit perceptual SDR UNORM input"); movie_work = false;
        } else {
            const auto prepared = prepare_movie(original.crop.width, original.crop.height, target.viewport.width,
                                                 target.viewport.height, config_.movie.cleanup != MovieCleanup::off);
            if (prepared != Status::ok) {
                targets_failed_ = true;
                remember_failure(3, prepared, error_);
                effect_error(result, prepared, error_, post_movie_cleanup | post_movie_backdrop); movie_work = false;
            } else {
                SDL_GPUBlitInfo copy{};
                copy.source.texture = original.texture; copy.source.x = original.crop.x; copy.source.y = original.crop.y;
                copy.source.w = original.crop.width; copy.source.h = original.crop.height;
                copy.destination.texture = crop_; copy.destination.w = iw_; copy.destination.h = ih_;
                copy.load_op = SDL_GPU_LOADOP_DONT_CARE; copy.filter = SDL_GPU_FILTER_NEAREST; copy.cycle = true;
                SDL_BlitGPUTexture(commands, &copy); ++result.copies_recorded;
                current = compact_source(device_, crop_, iw_, ih_, original);
                if (config_.movie.cleanup != MovieCleanup::off) {
                    const auto initialized = ensure_movie();
                    if (initialized != Status::ok) {
                        effect_error(result, initialized, error_, post_movie_cleanup); movie_work = false;
                    } else {
                        auto cleanup = config_.movie; cleanup.pixel_aspect = 1; cleanup.blur_backdrop = false;
                        const PostTarget compact{device_, clean_, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, iw_, ih_, {0,0,iw_,ih_},
                                                 SDL_GPU_LOADOP_CLEAR, {0,0,0,0}};
                        MovieEffectRecord recorded;
                        const auto status = movie_.record(commands, serial_, current, compact,
                            MovieSourceReadiness::earlier_in_same_commands, cleanup, recorded);
                        result.passes_recorded += recorded.passes_recorded;
                        if (status == Status::ok && recorded.output_recorded && recorded.cleanup_recorded) {
                            current = compact_source(device_, clean_, iw_, ih_, original);
                            result.effective_mask |= post_movie_cleanup;
                        } else {
                            const auto actual = status == Status::ok ? Status::backend_failure : status;
                            movie_failed_ = actual == Status::backend_failure;
                            if (movie_failed_) remember_failure(2, actual, movie_.last_error());
                            effect_error(result, actual, movie_.last_error(),
                                         movie_failed_ ? post_movie_cleanup : 0u); movie_work = false;
                        }
                    }
                }
            }
        }
    }
    if (config_.fsr.enabled) {
        if (fsr_failed_) effect_error(result, failures_[0].status, failures_[0].message, post_fsr1 | post_rcas);
        else {
            auto status = Status::ok;
            if (!fsr_ready_) { status = fsr_.initialize(device_); fsr_ready_ = status == Status::ok; }
            Fsr1Record recorded;
            const auto fit = is_movie ? movie_fsr_extent(current, pixel_aspect, target.viewport) : target.viewport;
            bool extent_bypass = false;
            if (status == Status::ok && (!fit.width || !fit.height ||
                (is_movie && (fit.width < current.crop.width || fit.height < current.crop.height)))) {
                status = Status::unavailable; extent_bypass = true;
            }
            if (status == Status::ok) status = fsr_.record(commands, current, fit.width, fit.height, recorded);
            result.passes_recorded += recorded.passes_recorded;
            if (status == Status::ok && recorded.applied) {
                current = recorded.output; // Movie raster ratio preserved; actual producer PAR remains unchanged.
                result.effective_mask |= post_fsr1 | (config_.fsr.rcas_enabled ? post_rcas : 0u);
            } else {
                fsr_failed_ = status == Status::backend_failure;
                if (fsr_failed_) remember_failure(0, status, fsr_.last_error());
                effect_error(result, status == Status::ok ? Status::unavailable : status,
                    extent_bypass ? "Movie FSR cannot upscale at its exact raster ratio inside the supplied center" : fsr_.last_error(),
                    fsr_failed_ ? post_fsr1 | post_rcas : 0u);
            }
        }
    }
    if (config_.fsr.enabled && !(result.effective_mask & post_fsr1)) {
        auto fallback = target;
        if (is_movie && current.texture != original.texture) fallback.viewport = fitted(current, pixel_aspect, target.viewport);
        return baseline(commands, current, fallback, result); // Never pass a partial FSR result downstream.
    }
    if (is_movie && movie_work) {
        const auto initialized = ensure_movie();
        if (initialized != Status::ok) effect_error(result, initialized, error_, post_movie_cleanup | post_movie_backdrop);
        else {
            auto composition = config_.movie; composition.cleanup = MovieCleanup::off; composition.pixel_aspect = pixel_aspect;
            const PostTarget compact{device_, presentation_, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, ow_, oh_, {0,0,ow_,oh_},
                                     SDL_GPU_LOADOP_CLEAR, {0,0,0,1}};
            MovieEffectRecord recorded;
            const auto status = movie_.record(commands, serial_, current, compact,
                MovieSourceReadiness::earlier_in_same_commands, composition, recorded);
            result.passes_recorded += recorded.passes_recorded;
            if (status == Status::ok && recorded.output_recorded) {
                current = compact_source(device_, presentation_, ow_, oh_, original); composed = true;
                if (recorded.backdrop_recorded) result.effective_mask |= post_movie_backdrop;
            } else {
                const auto actual = status == Status::ok ? Status::backend_failure : status;
                movie_failed_ = actual == Status::backend_failure;
                if (movie_failed_) remember_failure(2, actual, movie_.last_error());
                effect_error(result, actual, movie_.last_error(),
                             movie_failed_ ? post_movie_backdrop : 0u);
            }
        }
    }
    result.last_valid_source = current;
    if (config_.display.mode != DisplayEffectMode::off && (!is_movie || composed)) {
        if (display_failed_) effect_error(result, failures_[1].status, failures_[1].message, post_display);
        else {
            auto status = Status::ok;
            if (!display_ready_) { status = display_.initialize(device_, target_format_); display_ready_ = status == Status::ok; }
            bool applied = false;
            if (status == Status::ok) status = display_.record(commands, current, target, config_.display, applied);
            if (status == Status::ok && applied) {
                ++result.passes_recorded; result.effective_mask |= post_display; result.presentation_recorded = true;
                if (result.first_effect_error != Status::ok) fail(result.first_effect_error, result.first_effect_message);
                return Status::ok;
            }
            display_failed_ = status == Status::backend_failure;
            if (display_failed_) remember_failure(1, status, display_.last_error());
            effect_error(result, status == Status::ok ? Status::unavailable : status, display_.last_error(), display_failed_ ? post_display : 0u);
        }
    } else if (config_.display.mode != DisplayEffectMode::off)
        effect_error(result, Status::unavailable, "Movie composition unavailable; display effect not recorded");
    auto fallback_target = target;
    if (is_movie && !composed && current.texture != original.texture)
        fallback_target.viewport = fitted(current, pixel_aspect, target.viewport);
    return baseline(commands, current, fallback_target, result);
}

void PostProcessor::release_targets() noexcept {
    if (device_) {
        if (crop_) SDL_ReleaseGPUTexture(device_, crop_);
        if (clean_) SDL_ReleaseGPUTexture(device_, clean_);
        if (presentation_) SDL_ReleaseGPUTexture(device_, presentation_);
    }
    crop_ = clean_ = presentation_ = nullptr; iw_ = ih_ = ow_ = oh_ = 0;
}
void PostProcessor::shutdown() noexcept {
    if (!thread()) return;
    release_targets(); fsr_.shutdown(); display_.shutdown(); movie_.shutdown();
    device_ = nullptr; target_format_ = SDL_GPU_TEXTUREFORMAT_INVALID; original_ = nullptr; callback_context_ = nullptr;
    owner_thread_ = serial_ = 0; fsr_ready_ = display_ready_ = movie_ready_ = movie_active_ = false;
    config_ = {}; reset_failure();
}
} // namespace cedar
