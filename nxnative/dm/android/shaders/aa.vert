#version 450
// A quad over the whole anti-aliasing target. uv (0, 0) is texel (0, 0): Vulkan's framebuffer y=0 is the top, and NDC y=-1 lands there.
layout(location = 0) out vec2 vTex;
void main() {
    const vec2 corner[6] = vec2[6](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(1, 0), vec2(1, 1), vec2(0, 1));
    vec2 q = corner[gl_VertexIndex];
    gl_Position = vec4(q.x * 2.0 - 1.0, q.y * 2.0 - 1.0, 0.0, 1.0);
    vTex = q;
}
