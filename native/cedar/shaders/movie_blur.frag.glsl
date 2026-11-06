#version 450
// Original separable9-tap binomial spatial backdrop filter.
layout(set=2,binding=0) uniform sampler2D source_image;
layout(set=3,binding=0,std140) uniform Parameters {
    vec4 src_rect;
    vec4 sample_clamp;
    vec4 dst_rect;
    vec4 texel_mode;
    vec4 step_dim;
};
layout(location=0) out vec4 output_color;
vec4 sample_crop(vec2 uv) {
    return texture(source_image,clamp(uv,sample_clamp.xy,sample_clamp.zw));
}
void main() {
    vec2 p=(gl_FragCoord.xy-dst_rect.xy)/dst_rect.zw;
    vec2 uv=src_rect.xy+p*src_rect.zw;
    vec4 color=sample_crop(uv)*(70.0/256.0);
    color+=(sample_crop(uv-step_dim.xy)+sample_crop(uv+step_dim.xy))*(56.0/256.0);
    color+=(sample_crop(uv-step_dim.xy*2.0)+sample_crop(uv+step_dim.xy*2.0))*(28.0/256.0);
    color+=(sample_crop(uv-step_dim.xy*3.0)+sample_crop(uv+step_dim.xy*3.0))*(8.0/256.0);
    color+=(sample_crop(uv-step_dim.xy*4.0)+sample_crop(uv+step_dim.xy*4.0))*(1.0/256.0);
    output_color=vec4(color.rgb*step_dim.z,color.a);
}
