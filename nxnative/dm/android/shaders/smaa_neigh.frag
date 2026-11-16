#version 450
// SMAA pass 3: neighbourhood blend. The blend-weight pass's offsets (in texels, signed, from a float weight image) slide the
// sample across the edge, so a stair becomes a blend of the two sides. Binding 0 is the game's picture, binding 1 the weights.
layout(push_constant) uniform S { vec4 vp; float quality; } s;
layout(set = 0, binding = 0) uniform sampler2D color;
layout(set = 0, binding = 1) uniform sampler2D blend;
layout(location = 0) in vec2 vTex;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 w = textureLod(blend, vTex, 0.0).xy;
    vec2 uv = vTex + vec2(w.y * s.vp.x, w.x * s.vp.y);
    outColor = vec4(textureLod(color, uv, 0.0).rgb, 1.0);
}
