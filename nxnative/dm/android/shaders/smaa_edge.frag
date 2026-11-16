#version 450
// SMAA pass 1: morphological edge detection (the present's anti-aliasing, before the scale). quality 0..3 is Low, Medium, High,
// Ultra and selects the published luma threshold (0.15, 0.10, 0.10, 0.05). The edge is written .r against the left texel and
// .g against the top texel. See smaa.LICENSE.
layout(push_constant) uniform S { vec4 vp; float quality; } s;
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(location = 0) in vec2 vTex;
layout(location = 0) out vec4 outColor;

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main() {
    int q = int(clamp(s.quality + 0.5, 0.0, 3.0));
    const float thresh[4] = float[4](0.15, 0.10, 0.10, 0.05);
    float t = thresh[q];
    float lM = luma(textureLod(tex, vTex, 0.0).rgb);
    float lL = luma(textureLod(tex, vTex + vec2(-s.vp.x, 0.0), 0.0).rgb);
    float lT = luma(textureLod(tex, vTex + vec2(0.0, -s.vp.y), 0.0).rgb);
    float lR = luma(textureLod(tex, vTex + vec2(s.vp.x, 0.0), 0.0).rgb);
    float lB = luma(textureLod(tex, vTex + vec2(0.0, s.vp.y), 0.0).rgb);
    vec2 e = vec2(step(t, abs(lM - lL)), step(t, abs(lM - lT)));
    // A flat neighbourhood (the local contrast is below the threshold) is not an edge, so a texture's own grain is left alone.
    float local = max(max(abs(lM - lL), abs(lM - lT)), max(abs(lM - lR), abs(lM - lB)));
    if (local < t) e = vec2(0.0);
    outColor = vec4(e, 0.0, 1.0);
}
