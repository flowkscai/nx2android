#version 450
// Area scaling filter: a box over the source texels this output pixel covers. The footprint is the source rectangle the
// rasterizer mapped onto the pixel (the crop and the flip are already in vTex). Up to 8 by 8 samples; a larger shrink still
// covers the centre of the footprint. The same push constant block as sr.vert.
layout(push_constant) uniform S { vec4 vp; vec4 dst; vec4 uv; float flip; float sharp; } s;
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(location = 0) in vec2 vTex;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 fw = max(abs(dFdx(vTex)) * s.vp.zw, vec2(1e-4));
    vec2 fh = max(abs(dFdy(vTex)) * s.vp.zw, vec2(1e-4));
    vec2 foot = vec2(max(fw.x, fh.x), max(fw.y, fh.y));   // the footprint, in source texels
    vec2 p = vTex * s.vp.zw;
    vec2 lo = p - 0.5 * foot, hi = p + 0.5 * foot;
    int x0 = int(floor(lo.x)), y0 = int(floor(lo.y));
    int nx = int(min(ceil(hi.x) - float(x0), 8.0));
    int ny = int(min(ceil(hi.y) - float(y0), 8.0));
    nx = max(nx, 1); ny = max(ny, 1);
    vec3 acc = vec3(0.0); float w = 0.0;
    for (int y = 0; y < 8; y++) {
        if (y >= ny) break;
        for (int x = 0; x < 8; x++) {
            if (x >= nx) break;
            float tx0 = float(x0 + x), ty0 = float(y0 + y);
            float tx1 = tx0 + 1.0, ty1 = ty0 + 1.0;
            float cov = max(0.0, min(hi.x, tx1) - max(lo.x, tx0)) * max(0.0, min(hi.y, ty1) - max(lo.y, ty0));
            if (cov <= 0.0) continue;
            acc += textureLod(tex, (vec2(tx0, ty0) + 0.5) * s.vp.xy, 0.0).rgb * cov;
            w += cov;
        }
    }
    outColor = vec4(w > 0.0 ? acc / w : textureLod(tex, vTex, 0.0).rgb, 1.0);
}
