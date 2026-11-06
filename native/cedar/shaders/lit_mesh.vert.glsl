#version 450
// Original caller-world-normal Lambert facility. No inferred model transform.
layout(location=0) in vec3 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 color;
layout(location=3) in vec3 normal;
layout(set=1,binding=0,std140) uniform CameraBlock { mat4 view_projection; } camera;
layout(set=1,binding=1,std140) uniform VertexTransferBlock { vec4 flags; mat4 normal_rotation; } transfer;
layout(location=0) out vec2 texture_uv;
layout(location=1) out vec4 vertex_color;
layout(location=2) out vec3 world_normal;
void main() {
    gl_Position=camera.view_projection*vec4(position,1.0);
    vec3 rgb=color.rgb;
    if(transfer.flags.x>0.5)
        rgb=mix(rgb/12.92,pow((rgb+0.055)/1.055,vec3(2.4)),greaterThan(rgb,vec3(0.04045)));
    float scale=max(max(abs(normal.x),abs(normal.y)),abs(normal.z));
    vec3 n=scale>0.0?normal/scale:vec3(0.0);
    float length_squared=dot(n,n);
    n=length_squared>0.0?n*inversesqrt(length_squared):vec3(0.0);
    texture_uv=uv; vertex_color=vec4(rgb,color.a);
    world_normal=mat3(transfer.normal_rotation)*n;
}
