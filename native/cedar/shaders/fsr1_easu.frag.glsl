#version 450
#extension GL_GOOGLE_include_directive : require
// SPDX-License-Identifier: MPL-2.0
// Adapted from PE1's public SDL_GPU FSR1 wrapper; AMD algorithm headers MIT.
layout(set=2,binding=0) uniform sampler2D source_image;
layout(set=3,binding=0,std140) uniform Constants { uvec4 c0; uvec4 c1; uvec4 c2; uvec4 c3; };
layout(location=0) out vec4 result_color;
#define A_GPU 1
#define A_GLSL 1
#include "ffx_a.h"
#define FSR_EASU_F 1
AF4 FsrEasuRF(AF2 p) { return textureGather(source_image,p,0); }
AF4 FsrEasuGF(AF2 p) { return textureGather(source_image,p,1); }
AF4 FsrEasuBF(AF2 p) { return textureGather(source_image,p,2); }
#include "ffx_fsr1.h"
void main() {
    AF3 color;
    FsrEasuF(color, AU2(gl_FragCoord.xy), c0,c1,c2,c3);
    result_color=vec4(color,1.0);
}
