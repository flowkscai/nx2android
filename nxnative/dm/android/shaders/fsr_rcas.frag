#version 450
// FidelityFX Super Resolution 1, the RCAS pass (robust contrast-adaptive sharpening), run on EASU's output.
// Translated from AMD's FsrRcasF. sharp is the menu's 0..1: 0 leaves two stops of the range (a mild sharpen, the picture
// stays close to EASU) and 1 is the published maximum (FsrRcasCon's sharpness of 0). outSz is (width, height, origin x, origin y)
// of EASU's image in the window. See fsr1.LICENSE.
//
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// SPDX-License-Identifier: MIT
layout(push_constant) uniform S { vec4 vp; vec4 dst; vec4 uv; float flip; float sharp; vec4 outSz; } s;
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(location = 0) out vec4 outColor;

vec3 FsrRcasLoad(vec2 p) { return textureLod(tex, (p + 0.5) / s.outSz.xy, 0.0).rgb; }

void main() {
    vec2 ip = gl_FragCoord.xy - s.outSz.zw;
    vec3 b = FsrRcasLoad(ip + vec2(0.0, -1.0));
    vec3 d = FsrRcasLoad(ip + vec2(-1.0, 0.0));
    vec3 e = FsrRcasLoad(ip);
    vec3 f = FsrRcasLoad(ip + vec2(1.0, 0.0));
    vec3 h = FsrRcasLoad(ip + vec2(0.0, 1.0));
    float bL = b.b * 0.5 + (b.r * 0.5 + b.g);
    float dL = d.b * 0.5 + (d.r * 0.5 + d.g);
    float eL = e.b * 0.5 + (e.r * 0.5 + e.g);
    float fL = f.b * 0.5 + (f.r * 0.5 + f.g);
    float hL = h.b * 0.5 + (h.r * 0.5 + h.g);
    vec3 mn = min(min(b, d), min(f, h));
    vec3 mx = max(max(b, d), max(f, h));
    float peakHi = 1.0, peakLo = -4.0;
    vec3 hitMin = min(mn, e) / max(4.0 * mx, vec3(1e-6));
    vec3 hitMax = (peakHi - max(mx, e)) / max(4.0 * mn + peakLo, vec3(1e-6));
    float lobe = max(-0.1875, min(max(max(max(-hitMin.r, hitMax.r), max(-hitMin.g, hitMax.g)), max(-hitMin.b, hitMax.b)), 0.0));
    // FsrRcasCon: the slider's 0 is two stops down from the maximum, 100 is the maximum (exp2(0) == 1).
    lobe *= exp2(-mix(2.0, 0.0, clamp(s.sharp, 0.0, 1.0)));
    float rcpL = 1.0 / (4.0 * lobe + 1.0);
    vec3 pix = (lobe * b + lobe * d + lobe * h + lobe * f + e) * rcpL;
    outColor = vec4(clamp(pix, 0.0, 1.0), 1.0);
}
