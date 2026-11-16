// The present's anti-aliasing and scaling filter (gfx_vk_present.c present_window, the pause menu's Game tab).
// Both are integers on the settings path: settings.txt aa= / scale= / sharpness=, a profile's setting rows, and DM_VK_AA / DM_VK_SCALE / DM_VK_SHARP
// over those for one run. Anti-aliasing is a pass on the game's picture; the scaler then samples that image, or the picture itself when anti-aliasing is off.
// The default (an untouched settings file, both integers 0) is the bilinear blit and no anti-aliasing pass.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    PIC_AA_OFF = 0,
    PIC_AA_FXAA,   // one option: FXAA 3.11 preset 15, an 8-step luma-edge search
    PIC_AA_SMAA_LOW, PIC_AA_SMAA_MED, PIC_AA_SMAA_HIGH, PIC_AA_SMAA_ULTRA,
    PIC_AA_N
};
enum {
    PIC_SCALE_BILINEAR = 0,
    PIC_SCALE_NEAREST,
    PIC_SCALE_AREA,
    PIC_SCALE_FSR,
    PIC_SCALE_SGSR,
    PIC_SCALE_N
};

// FXAA is one option. Preset 15 of the published FXAA 3.11 algorithm searches the luma edge for 8 steps.
static inline int pic_fxaa_steps(int aa) { return aa == PIC_AA_FXAA ? 8 : 0; }
// SMAA presets: the morphological search (Low 4, Medium 8, High 16, Ultra 32).
static inline int pic_smaa_steps(int aa) {
    static const int s[4] = {4, 8, 16, 32};
    int q = aa - PIC_AA_SMAA_LOW; return q >= 0 && q < 4 ? s[q] : 0;
}
static inline int pic_clamp_aa(int v) { return v < 0 || v >= PIC_AA_N ? PIC_AA_OFF : v; }
static inline int pic_clamp_scale(int v) { return v < 0 || v >= PIC_SCALE_N ? PIC_SCALE_BILINEAR : v; }
static inline int pic_clamp_sharp(int v) { return v < 0 ? 0 : v > 100 ? 100 : v; }
static inline const char *pic_aa_name(int v) {
    static const char *const n[PIC_AA_N] = {"Off", "FXAA", "SMAA Low", "SMAA Medium", "SMAA High", "SMAA Ultra"};
    return n[pic_clamp_aa(v)];
}
static inline const char *pic_scale_name(int v) {
    static const char *const n[PIC_SCALE_N] = {"Bilinear", "Nearest", "Area", "FSR", "SGSR"};
    return n[pic_clamp_scale(v)];
}

typedef struct { int aa, scale, sharp; } pic_set;

// One settings.txt line. 1 when the line is aa=, scale= or sharpness= (the value is clamped into the struct).
static inline int pic_settings_take(pic_set *s, const char *line) {
    int v;
    if (sscanf(line, "aa=%d", &v) == 1) { s->aa = pic_clamp_aa(v); return 1; }
    if (sscanf(line, "scale=%d", &v) == 1) { s->scale = pic_clamp_scale(v); return 1; }
    if (sscanf(line, "sharpness=%d", &v) == 1) { s->sharp = pic_clamp_sharp(v); return 1; }
    return 0;
}
// The three lines settings_save writes beside the other Game keys.
static inline int pic_settings_write(char *dst, int cap, const pic_set *s) {
    return snprintf(dst, (size_t)cap, "aa=%d\nscale=%d\nsharpness=%d\n", pic_clamp_aa(s->aa), pic_clamp_scale(s->scale), pic_clamp_sharp(s->sharp));
}
// A set variable replaces the saved integer for this run. An empty or absent variable leaves it.
static inline pic_set pic_settings_env(pic_set s) {
    const char *e;
    if ((e = getenv("DM_VK_AA")) && *e) s.aa = pic_clamp_aa(atoi(e));
    if ((e = getenv("DM_VK_SCALE")) && *e) s.scale = pic_clamp_scale(atoi(e));
    if ((e = getenv("DM_VK_SHARP")) && *e) s.sharp = pic_clamp_sharp(atoi(e));
    return s;
}

// The upscaler needs the picture to grow by at least a sixteenth (PR 390: a 1920-wide picture on a wider window). Area is a box filter at any size.
static inline int pic_grows(int cw, int dw) { return dw > 0 && cw > 0 && dw * 16 >= cw * 17; }

typedef struct {
    int aa, scale, sharp;
    int cw, dw;     // the cropped picture's width and the rectangle it is fitted into
    int dyn;        // the device can begin a rendering scope without a render pass
    int single;     // a 2D, one-sample, one-layer picture
    int pipe;       // the view, the sampler and the pipeline were created
} pic_req;

typedef struct {
    int aa;          // PIC_AA_* that runs (off when the pass cannot be built)
    int aa_passes;   // 0, 1 (FXAA) or 3 (SMAA edge, blend weights, neighborhood)
    int fxaa_steps, smaa_steps;
    int scale;       // PIC_SCALE_* that runs
    int blit;        // 1: vkCmdBlitImage (bilinear or nearest)
    int nearest;     // the blit filter is nearest
    int sharp;       // 0..100, applied by FSR and SGSR
    const char *why; // set when a requested upscaler or pass fell back to bilinear; NULL on the plain path
} pic_plan;

// What present_window runs. Shader scales (Area, FSR, SGSR) and anti-aliasing need a pipeline; FSR and SGSR also need the picture to grow.
static inline pic_plan present_pic_plan(const pic_req *q) {
    pic_plan p; memset(&p, 0, sizeof p);
    int aa = pic_clamp_aa(q->aa), scale = pic_clamp_scale(q->scale);
    int shader = scale == PIC_SCALE_AREA || scale == PIC_SCALE_FSR || scale == PIC_SCALE_SGSR;
    int up = scale == PIC_SCALE_FSR || scale == PIC_SCALE_SGSR;
    const char *why = NULL;
    if (up && !pic_grows(q->cw, q->dw)) { scale = PIC_SCALE_BILINEAR; shader = 0; up = 0; why = "the picture does not grow enough for the upscaler; bilinear"; }
    int need = aa != PIC_AA_OFF || shader;
    if (need && !q->dyn) { why = "no dynamic rendering; bilinear"; aa = PIC_AA_OFF; if (shader) { scale = PIC_SCALE_BILINEAR; shader = 0; } }
    else if (need && !q->single) { why = "the picture is not a 2D single-sample texture; bilinear"; aa = PIC_AA_OFF; if (shader) { scale = PIC_SCALE_BILINEAR; shader = 0; } }
    else if (need && !q->pipe) { why = "the view, sampler or pipeline was refused; bilinear"; aa = PIC_AA_OFF; if (shader) { scale = PIC_SCALE_BILINEAR; shader = 0; } }
    p.aa = aa;
    p.aa_passes = aa == PIC_AA_OFF ? 0 : aa == PIC_AA_FXAA ? 1 : 3;
    p.fxaa_steps = p.aa_passes == 1 ? pic_fxaa_steps(aa) : 0;
    p.smaa_steps = p.aa_passes == 3 ? pic_smaa_steps(aa) : 0;
    p.scale = scale;
    p.blit = !shader;
    p.nearest = scale == PIC_SCALE_NEAREST;
    p.sharp = scale == PIC_SCALE_FSR || scale == PIC_SCALE_SGSR ? pic_clamp_sharp(q->sharp) : 0;
    p.why = why;
    return p;
}
