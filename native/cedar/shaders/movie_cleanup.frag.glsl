#version 450
// SPDX-License-Identifier: MPL-2.0
// Source-informed adaptation of PE1 platform/movie_cleanup/movie_cleanup.frag.
// Reference individual file has an MIT marker; containing platform policy is
// MPL2.0, retained at third_party/licenses/PE1-platform-MPL2.txt. See provenance.
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
vec3 sample_logical(ivec2 at,ivec2 logical) {
    at=clamp(at,ivec2(0),logical-1);
    vec2 relative=(vec2(at)+0.5)/vec2(logical);
    vec2 uv=clamp(src_rect.xy+relative*src_rect.zw,sample_clamp.xy,sample_clamp.zw);
    ivec2 texture_extent=textureSize(source_image,0);
    ivec2 lo=clamp(ivec2(floor(sample_clamp.xy*vec2(texture_extent))),ivec2(0),texture_extent-1);
    ivec2 hi=clamp(ivec2(floor(sample_clamp.zw*vec2(texture_extent))),lo,texture_extent-1);
    ivec2 pixel=ivec2(floor(uv*vec2(texture_extent)));
    return texelFetch(source_image,clamp(pixel,lo,hi),0).rgb;
}
void main() {
    vec2 p=(gl_FragCoord.xy-dst_rect.xy)/dst_rect.zw;
    if(any(lessThan(p,vec2(0))) || any(greaterThanEqual(p,vec2(1)))) {
        if(step_dim.w>0.5) output_color=vec4(0,0,0,1); else discard;
        return;
    }
    vec2 uv=src_rect.xy+p*src_rect.zw;
    vec4 center=sample_crop(uv);
    vec3 color=center.rgb;
    if(texel_mode.z>0.0) {
        // Caller-defined decoded crop is the logical grid. No PS1 VRAM/native
        // scale flags or guessed codec block metadata are imported.
        ivec2 logical=max(ivec2(round(src_rect.zw/texel_mode.xy)),ivec2(1));
        ivec2 pixel=clamp(ivec2(floor(p*vec2(logical))),ivec2(0),logical-1);
        color=sample_logical(pixel,logical);
        vec3 sum=color*4.0;
        float total=4.0;
        for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
            if(x==0&&y==0)continue;
            vec3 tap=sample_logical(pixel+ivec2(x,y),logical);
            vec3 difference=abs(tap-color);
            float edge=max(difference.r,max(difference.g,difference.b));
            float weight=max(0.0,1.0-edge/texel_mode.w);
            weight=weight*weight*((x==0||y==0)?1.0:0.70710678);
            sum+=tap*weight;
            total+=weight;
        }
        // This is the reference's nominal8-pixel spatial-strength heuristic;
        // it is not detection of codec blocks or restoration of lost detail.
        bool boundary=((pixel.x&7)==0)||((pixel.x&7)==7)||((pixel.y&7)==0)||((pixel.y&7)==7);
        float amount=texel_mode.z*(boundary?1.0:0.35);
        color=clamp(mix(color,sum/total,amount),0.0,1.0);
        center.a=1.0; // Active RGB-only cleanup requires producer-confirmed opacity.
    }
    output_color=vec4(color*step_dim.z,center.a);
}
