// SPDX-License-Identifier: MPL-2.0
// Source-informed restrained CRT behavior from PsyZ platform display_filter;
// its root license assigns platform code MPL-2.0. See DISPLAY-EFFECTS.md.
#version 450
layout(set = 2, binding = 0) uniform sampler2D source_image;
layout(set = 3, binding = 0, std140) uniform Parameters {
    vec4 source_crop;
    vec4 destination_viewport;
    vec4 effect; // logical rows, mode, scanline attenuation, phosphor attenuation
    vec4 mask_color; // per-channel attenuation weights; w reserved
} parameters;
layout(location = 0) out vec4 output_color;
void main() {
    vec2 p = clamp((gl_FragCoord.xy - parameters.destination_viewport.xy) /
                   parameters.destination_viewport.zw, vec2(0.0), vec2(1.0));
    vec2 first_center = parameters.source_crop.xy + vec2(0.5);
    vec2 last_center = parameters.source_crop.xy + parameters.source_crop.zw - vec2(0.5);
    vec2 pixel = clamp(parameters.source_crop.xy + p * parameters.source_crop.zw,
                       first_center, last_center);
    // The single sample is always clamped to visible crop texel centers, not
    // unrelated texture-page/letterbox pixels. No warped or offset samples.
    vec4 sampled = textureLod(source_image, pixel / vec2(textureSize(source_image, 0)), 0.0);
    vec3 color = sampled.rgb;
    if (parameters.effect.y > 0.5) {
        const float tau = 6.28318530718;
        float rows = clamp(parameters.effect.x, 1.0, 8192.0);
        float visibility = smoothstep(1.0, 2.0, parameters.destination_viewport.w / rows);
        float row_darkness = 0.5 * (1.0 - cos(tau * p.y * rows));
        float scan_strength = clamp(parameters.effect.z, 0.0, 0.25);
        color *= 1.0 - visibility * scan_strength * row_darkness;
        if (parameters.effect.y > 1.5) {
            float phase = tau * (gl_FragCoord.x - parameters.destination_viewport.x) / 3.0;
            vec3 phosphor = 0.5 + 0.5 * cos(vec3(phase, phase - tau / 3.0, phase - 2.0 * tau / 3.0));
            vec3 weights = clamp(parameters.mask_color.rgb, vec3(0.0), vec3(1.0));
            float mask_strength = clamp(parameters.effect.w, 0.0, 0.15);
            color *= vec3(1.0) - visibility * mask_strength * weights * (vec3(1.0) - phosphor);
        }
    }
    // Producer-declared perceptual SDR path only: no manual gamma/transfer
    // conversion, alpha replacement, bloom, history, warp or invented detail.
    output_color = vec4(color, sampled.a);
}
