#version 450
// SMAA pass 2: blend weights. From each morphological edge, search along it until the edge ends. quality 0..3 is Low, Medium,
// High, Ultra and the search takes 4, 8, 16 or 32 steps (the published SMAA_MAX_SEARCH_STEPS presets). The result is the
// signed offset, in texels, that the neighbourhood pass applies: .x across a horizontal edge, .y across a vertical edge.
// The colour target is VK_FORMAT_R16G16B16A16_SFLOAT, so a negative offset is kept. An unsigned target would store it as 0.
layout(push_constant) uniform S { vec4 vp; float quality; } s;
layout(set = 0, binding = 0) uniform sampler2D edge;
layout(location = 0) in vec2 vTex;
layout(location = 0) out vec4 outColor;

void main() {
    int q = int(clamp(s.quality + 0.5, 0.0, 3.0));
    const int steps[4] = int[4](4, 8, 16, 32);   // Low .. Ultra: the search lengthens
    int n = steps[q];
    vec2 e = textureLod(edge, vTex, 0.0).rg;
    float ox = 0.0, oy = 0.0;
    if (e.g > 0.5) {   // horizontal edge: search left and right
        float dL = 0.0, dR = 0.0;
        for (int i = 1; i <= 32; i++) {
            if (i > n) break;
            if (dL == 0.0 && textureLod(edge, vTex + vec2(-float(i) * s.vp.x, 0.0), 0.0).g < 0.5) dL = float(i);
            if (dR == 0.0 && textureLod(edge, vTex + vec2(float(i) * s.vp.x, 0.0), 0.0).g < 0.5) dR = float(i);
            if (dL > 0.0 && dR > 0.0) break;
        }
        if (dL == 0.0) dL = float(n);
        if (dR == 0.0) dR = float(n);
        oy = 0.5 * (dR - dL) / (dL + dR);   // toward the shorter end, at most half a texel
    }
    if (e.r > 0.5) {   // vertical edge: search up and down
        float dU = 0.0, dD = 0.0;
        for (int i = 1; i <= 32; i++) {
            if (i > n) break;
            if (dU == 0.0 && textureLod(edge, vTex + vec2(0.0, -float(i) * s.vp.y), 0.0).r < 0.5) dU = float(i);
            if (dD == 0.0 && textureLod(edge, vTex + vec2(0.0, float(i) * s.vp.y), 0.0).r < 0.5) dD = float(i);
            if (dU > 0.0 && dD > 0.0) break;
        }
        if (dU == 0.0) dU = float(n);
        if (dD == 0.0) dD = float(n);
        ox = 0.5 * (dD - dU) / (dU + dD);
    }
    outColor = vec4(oy, ox, 0.0, 1.0);
}
