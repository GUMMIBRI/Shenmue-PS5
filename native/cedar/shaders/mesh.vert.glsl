#version 450
// Original unlit facility. Caller supplies world positions and a real camera.
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 color;
layout(set = 1, binding = 0, std140) uniform CameraBlock { mat4 view_projection; } camera;
layout(location = 0) out vec2 texture_uv;
layout(location = 1) out vec4 vertex_color;
void main() {
    gl_Position = camera.view_projection * vec4(position, 1.0);
    texture_uv = uv;
    vertex_color = color;
}
