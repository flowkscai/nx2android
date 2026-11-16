#version 450
// FXAA, one luma-edge blend, for the present's anti-aliasing pass (gfx_vk_present.c). An independent implementation of the
// technique published by Timothy Lottes (NVIDIA FXAA 3.11): not a copy of FXAA3_11.h. The one FXAA option uses published
// quality preset 15, an 8-step edge search. quality is the shared anti-aliasing push constant (SMAA reads it; FXAA does not).
// vp: the picture's texel size and size (1/w, 1/h, w, h). See fxaa.LICENSE.
layout(push_constant) uniform S { vec4 vp; float quality; } s;
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(location = 0) in vec2 vTex;
layout(location = 0) out vec4 outColor;

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
    vec2 r = s.vp.xy;
    vec3 rgbM = textureLod(tex, vTex, 0.0).rgb;
    float lM = luma(rgbM);
    float lN = luma(textureLod(tex, vTex + vec2(0.0, -r.y), 0.0).rgb);
    float lS = luma(textureLod(tex, vTex + vec2(0.0, r.y), 0.0).rgb);
    float lE = luma(textureLod(tex, vTex + vec2(r.x, 0.0), 0.0).rgb);
    float lW = luma(textureLod(tex, vTex + vec2(-r.x, 0.0), 0.0).rgb);
    float lMax = max(lM, max(max(lN, lS), max(lW, lE)));
    float lMin = min(lM, min(min(lN, lS), min(lW, lE)));
    float contrast = lMax - lMin;
    if (contrast < max(1.0 / 12.0, lMax * 0.125)) { outColor = vec4(rgbM, 1.0); return; }
    float lNW = luma(textureLod(tex, vTex + vec2(-r.x, -r.y), 0.0).rgb);
    float lNE = luma(textureLod(tex, vTex + vec2(r.x, -r.y), 0.0).rgb);
    float lSW = luma(textureLod(tex, vTex + vec2(-r.x, r.y), 0.0).rgb);
    float lSE = luma(textureLod(tex, vTex + vec2(r.x, r.y), 0.0).rgb);
    // Horizontal span (the edge runs left-right) versus a vertical span. The search then walks along that span.
    float edgeH = abs(lNW + lNE - 2.0 * lN) + abs(lW + lE - 2.0 * lM) * 2.0 + abs(lSW + lSE - 2.0 * lS);
    float edgeV = abs(lNW + lSW - 2.0 * lW) + abs(lN + lS - 2.0 * lM) * 2.0 + abs(lNE + lSE - 2.0 * lE);
    bool horz = edgeH >= edgeV;
    float lNeg = horz ? lN : lW, lPos = horz ? lS : lE;
    bool negSide = abs(lNeg - lM) >= abs(lPos - lM);
    float grad = max(abs(lNeg - lM), abs(lPos - lM)) * 0.25;
    float pair = 0.5 * (lM + (negSide ? lNeg : lPos));
    vec2 side = horz ? vec2(0.0, negSide ? -r.y : r.y) : vec2(negSide ? -r.x : r.x, 0.0);
    vec2 along = horz ? vec2(r.x, 0.0) : vec2(0.0, r.y);
    const int n = 8;   // preset 15: the one FXAA option's edge search
    vec2 posN = vTex, posP = vTex;
    bool doneN = false, doneP = false;
    for (int i = 0; i < n; i++) {
        if (!doneN) posN -= along;
        if (!doneP) posP += along;
        if (!doneN) doneN = abs(luma(textureLod(tex, posN + side * 0.5, 0.0).rgb) - pair) >= grad;
        if (!doneP) doneP = abs(luma(textureLod(tex, posP + side * 0.5, 0.0).rgb) - pair) >= grad;
        if (doneN && doneP) break;
    }
    float distN = horz ? (vTex.x - posN.x) : (vTex.y - posN.y);
    float distP = horz ? (posP.x - vTex.x) : (posP.y - vTex.y);
    float span = distN + distP;
    float off = span > 0.0 ? clamp(-min(distN, distP) / span + 0.5, 0.0, 0.5) : 0.0;
    float avg = (lN + lS + lW + lE) * 0.25;
    float sub = clamp(abs(avg - lM) / contrast, 0.0, 1.0);
    sub = sub * sub * (3.0 - 2.0 * sub);
    float blend = max(off, sub * 0.75);
    vec2 uv = vTex + side * blend;
    outColor = vec4(textureLod(tex, uv, 0.0).rgb, 1.0);
}
