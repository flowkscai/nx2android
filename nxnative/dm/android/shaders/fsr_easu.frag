#version 450
// FidelityFX Super Resolution 1, the EASU pass (edge-adaptive spatial upsampling), for the Scaling Filter's FSR.
// Translated to Vulkan GLSL from AMD's ffx_fsr1.h (FsrEasuF): a 12-tap Lanczos kernel, the taps taken as textureGather
// reads, clamped to the 2x2 neighbourhood so the negative lobe does not ring. RCAS is the next pass (fsr_rcas.frag).
// vp is the source texture's texel size and size. vTex is this output pixel's source coordinate (sr.vert: the crop and the
// bottom-up flip). See fsr1.LICENSE.
//
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// SPDX-License-Identifier: MIT
layout(push_constant) uniform S { vec4 vp; vec4 dst; vec4 uv; float flip; float sharp; } s;
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(location = 0) in vec2 vTex;
layout(location = 0) out vec4 outColor;

vec4 FsrEasuR(vec2 p) { return textureGather(tex, p, 0); }
vec4 FsrEasuG(vec2 p) { return textureGather(tex, p, 1); }
vec4 FsrEasuB(vec2 p) { return textureGather(tex, p, 2); }

void FsrEasuTap(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len, float lob, float clp, vec3 c) {
    vec2 v = vec2(off.x * dir.x + off.y * dir.y, off.x * -dir.y + off.y * dir.x) * len;
    float d2 = min(dot(v, v), clp);
    float wB = (2.0 / 5.0) * d2 - 1.0;
    float wA = lob * d2 - 1.0;
    wB *= wB; wA *= wA;
    wB = (25.0 / 16.0) * wB - (25.0 / 16.0 - 1.0);
    float w = wB * wA;
    aC += c * w; aW += w;
}
void FsrEasuSet(inout vec2 dir, inout float len, float w, float lA, float lB, float lC, float lD, float lE) {
    float dc = lD - lC, cb = lC - lB;
    float lenX = 1.0 / max(max(abs(dc), abs(cb)), 1e-6);
    float dirX = lD - lB;
    dir.x += dirX * w;
    lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);
    len += lenX * lenX * w;
    float ec = lE - lC, ca = lC - lA;
    float lenY = 1.0 / max(max(abs(ec), abs(ca)), 1e-6);
    float dirY = lE - lA;
    dir.y += dirY * w;
    lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);
    len += lenY * lenY * w;
}

void main() {
    vec2 pp = vTex * s.vp.zw;           // FsrEasuF: the source pixel of this output pixel
    vec2 fp = floor(pp);
    pp -= fp;
    vec2 inv = s.vp.xy;
    // Gather centres. textureGather's .x/.y/.z/.w are the header's b/c (and the ijfe, klhg, zzon groups) for this layout.
    vec2 p0 = fp * inv + vec2(inv.x, -inv.y);
    vec2 p1 = p0 + vec2(-inv.x, 2.0 * inv.y);
    vec2 p2 = p0 + vec2(inv.x, 2.0 * inv.y);
    vec2 p3 = p0 + vec2(0.0, 4.0 * inv.y);
    vec4 bczzR = FsrEasuR(p0), bczzG = FsrEasuG(p0), bczzB = FsrEasuB(p0);
    vec4 ijfeR = FsrEasuR(p1), ijfeG = FsrEasuG(p1), ijfeB = FsrEasuB(p1);
    vec4 klhgR = FsrEasuR(p2), klhgG = FsrEasuG(p2), klhgB = FsrEasuB(p2);
    vec4 zzonR = FsrEasuR(p3), zzonG = FsrEasuG(p3), zzonB = FsrEasuB(p3);
    vec4 bczzL = bczzB * 0.5 + (bczzR * 0.5 + bczzG);
    vec4 ijfeL = ijfeB * 0.5 + (ijfeR * 0.5 + ijfeG);
    vec4 klhgL = klhgB * 0.5 + (klhgR * 0.5 + klhgG);
    vec4 zzonL = zzonB * 0.5 + (zzonR * 0.5 + zzonG);
    float bL = bczzL.x, cL = bczzL.y;
    float iL = ijfeL.x, jL = ijfeL.y, fL = ijfeL.z, eL = ijfeL.w;
    float kL = klhgL.x, lL = klhgL.y, hL = klhgL.z, gL = klhgL.w;
    float oL = zzonL.z, nL = zzonL.w;
    vec2 dir = vec2(0.0); float len = 0.0;
    FsrEasuSet(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);
    FsrEasuSet(dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);
    FsrEasuSet(dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);
    FsrEasuSet(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);
    float dirR = dot(dir, dir);
    bool zro = dirR < (1.0 / 32768.0);
    dirR = zro ? 1.0 : inversesqrt(dirR);
    dir = zro ? vec2(1.0, 0.0) : dir * dirR;
    len *= 0.5; len *= len;
    float stretch = dot(dir, dir) / max(abs(dir.x), abs(dir.y));
    vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    float clp = 1.0 / lob;
    vec3 fC = vec3(ijfeR.z, ijfeG.z, ijfeB.z);
    vec3 gC = vec3(klhgR.w, klhgG.w, klhgB.w);
    vec3 jC = vec3(ijfeR.y, ijfeG.y, ijfeB.y);
    vec3 kC = vec3(klhgR.x, klhgG.x, klhgB.x);
    vec3 mn = min(min(fC, gC), min(jC, kC));
    vec3 mx = max(max(fC, gC), max(jC, kC));
    vec3 aC = vec3(0.0); float aW = 0.0;
    FsrEasuTap(aC, aW, vec2(0.0, -1.0) - pp, dir, len2, lob, clp, vec3(bczzR.x, bczzG.x, bczzB.x));
    FsrEasuTap(aC, aW, vec2(1.0, -1.0) - pp, dir, len2, lob, clp, vec3(bczzR.y, bczzG.y, bczzB.y));
    FsrEasuTap(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, vec3(ijfeR.x, ijfeG.x, ijfeB.x));
    FsrEasuTap(aC, aW, vec2(0.0, 1.0) - pp, dir, len2, lob, clp, jC);
    FsrEasuTap(aC, aW, vec2(0.0, 0.0) - pp, dir, len2, lob, clp, fC);
    FsrEasuTap(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, vec3(ijfeR.w, ijfeG.w, ijfeB.w));
    FsrEasuTap(aC, aW, vec2(1.0, 1.0) - pp, dir, len2, lob, clp, kC);
    FsrEasuTap(aC, aW, vec2(2.0, 1.0) - pp, dir, len2, lob, clp, vec3(klhgR.y, klhgG.y, klhgB.y));
    FsrEasuTap(aC, aW, vec2(2.0, 0.0) - pp, dir, len2, lob, clp, vec3(klhgR.z, klhgG.z, klhgB.z));
    FsrEasuTap(aC, aW, vec2(1.0, 0.0) - pp, dir, len2, lob, clp, gC);
    FsrEasuTap(aC, aW, vec2(1.0, 2.0) - pp, dir, len2, lob, clp, vec3(zzonR.z, zzonG.z, zzonB.z));
    FsrEasuTap(aC, aW, vec2(0.0, 2.0) - pp, dir, len2, lob, clp, vec3(zzonR.w, zzonG.w, zzonB.w));
    vec3 pix = aW != 0.0 ? clamp(aC / aW, mn, mx) : fC;
    outColor = vec4(pix, 1.0);
}
