#version 450
#extension GL_GOOGLE_include_directive : require
// SPDX-License-Identifier: MPL-2.0
// Adapted from PE1's public SDL_GPU FSR1 wrapper; AMD algorithm headers MIT.
layout(set=2,binding=0) uniform sampler2D source_image;
layout(set=3,binding=0,std140) uniform Constants { uvec4 c0; };
layout(location=0) out vec4 result_color;
#define A_GPU 1
#define A_GLSL 1
#include "ffx_a.h"
#define FSR_RCAS_F 1
AF4 FsrRcasLoadF(ASU2 p) {
    return texelFetch(source_image,clamp(p,ivec2(0),textureSize(source_image,0)-ivec2(1)),0);
}
void FsrRcasInputF(inout AF1 r,inout AF1 g,inout AF1 b) {}
#include "ffx_fsr1.h"
void main() {
    AF3 color;
    FsrRcasF(color.r,color.g,color.b,AU2(gl_FragCoord.xy),c0);
    result_color=vec4(color,1.0);
}
