#version 450
// No guessed Shenmue lighting/material equation. This is explicit unlit texturing.
layout(location = 0) in vec2 texture_uv;
layout(location = 1) in vec4 vertex_color;
layout(set = 2, binding = 0) uniform sampler2D diffuse_texture;
layout(location = 0) out vec4 output_color;
void main() { output_color = texture(diffuse_texture, texture_uv) * vertex_color; }
