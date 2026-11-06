#version 450
layout(location=0) in vec2 texture_uv;
layout(location=1) in vec4 vertex_color;
layout(location=2) in vec3 world_normal;
layout(set=2,binding=0) uniform sampler2D diffuse_texture;
// flags: unused, unused, output manual encode, unused.
layout(set=3,binding=0,std140) uniform LightBlock {
    vec4 direction; vec4 ambient; vec4 diffuse; vec4 flags;
} light;
layout(location=0) out vec4 output_color;
vec3 encode_srgb(vec3 v) {
    return mix(v*12.92,1.055*pow(v,vec3(1.0/2.4))-0.055,greaterThan(v,vec3(0.0031308)));
}
void main() {
    vec4 texel=texture(diffuse_texture,texture_uv);
    vec3 t=texel.rgb, c=vertex_color.rgb;
    // Rescale before normalization to avoid overflow/underflow at finite extremes.
    float scale=max(max(abs(world_normal.x),abs(world_normal.y)),abs(world_normal.z));
    vec3 n=scale>0.0?world_normal/scale:vec3(0.0);
    float len=dot(n,n);
    float lambert=len>0.0?max(dot(n*inversesqrt(len),light.direction.xyz),0.0):0.0;
    vec3 rgb=clamp(t*c*(light.ambient.rgb+light.diffuse.rgb*lambert),0.0,1.0);
    if(light.flags.z>0.5) rgb=encode_srgb(rgb);
    output_color=vec4(rgb,texel.a*vertex_color.a);
}
