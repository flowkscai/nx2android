"""The shipped present choice and the Game settings that feed it.

Compiles present_pic.h and the settings_load / settings_save in front_android.c, and reads the shaders build.py compiles.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from porter.upstream import android_profile  # noqa: E402


ANDROID = ROOT / 'nxnative/dm/android'
SHADERS = ANDROID / 'shaders'


def _show(text):
    """The unittest runner hides a passing test's stdout. The gate reads the process output."""
    sys.__stdout__.write(text)
    if not text.endswith('\n'):
        sys.__stdout__.write('\n')
    sys.__stdout__.flush()


class PresentPicture(unittest.TestCase):
    def test_shipped_selection_and_settings(self):
        cc = shutil.which('cc') or shutil.which('clang')
        if not cc:
            self.skipTest('a C compiler is needed for the present selection check')
        front = (ANDROID / 'front_android.c').read_text(encoding='utf-8')
        present = (ANDROID / 'gfx_vk_present.c').read_text(encoding='utf-8')
        load = front[front.index('static void settings_load(void)'):front.index('void dm_lan_android_start')]
        window = present[present.index('static void present_window('):present.index('// headless frame shots')]
        self.assertIn('pic_settings_take', load)
        self.assertIn('pic_settings_write', load)
        self.assertIn('present_pic_plan', window)
        self.assertIn('pic_aa(', window)
        self.assertIn('VK_FILTER_NEAREST', window)
        self.assertIn('VK_FILTER_LINEAR', window)
        for token in ('g_filt.area', 'g_filt.easu', 'g_filt.rcas', 'g_filt.sgsr'):
            self.assertIn(token, window)
        harness = textwrap.dedent(r'''
            #include <assert.h>
            #include <stdio.h>
            #include <stdlib.h>
            #include <string.h>
            #include <stdatomic.h>
            #include "overlay_layout.h"
            #include "present_pic.h"
            #define LOG(...) do { fprintf(stderr, "[dm] " __VA_ARGS__); fputc('\n', stderr); } while (0)
            static char g_root[512];
            static _Atomic int g_set_overlay = 1, g_set_rumble = 1, g_set_fps, g_set_opacity = 60, g_set_grid, g_set_labels = -1;
            static _Atomic int g_set_aa, g_set_scale, g_set_sharp;
            static _Atomic int g_cust[2][2][OV_N][4];
            static const char *const LAYOUT_NAMES[2] = {"tablet", "phone"};
            static int ctl_named(const char *n) { (void)n; return -1; }
            static int dm_opmode(void) { return 0; }
        ''') + load + textwrap.dedent(r'''
            static void show(const char *kind, int v, const char *name, int passes, int blit, int nearest, int sharp, int fxaa, int smaa) {
                printf("select %s %d %s passes %d blit %d nearest %d sharp %d fxaa %d smaa %d\n", kind, v, name, passes, blit, nearest, sharp, fxaa, smaa);
            }
            int main(int argc, char **argv) {
                assert(argc == 2);
                snprintf(g_root, sizeof g_root, "%s", argv[1]);
                pic_req q; memset(&q, 0, sizeof q);
                pic_plan def = present_pic_plan(&q);
                assert(def.blit && !def.nearest && def.aa_passes == 0 && def.scale == PIC_SCALE_BILINEAR && def.aa == PIC_AA_OFF && !def.why);
                printf("default bilinear anti-aliasing off\n");
                q.dyn = 1; q.single = 1; q.pipe = 1; q.cw = 1920; q.dw = 2400; q.sharp = 80;
                for (int aa = 0; aa < PIC_AA_N; aa++) {
                    q.aa = aa; q.scale = PIC_SCALE_BILINEAR;
                    pic_plan p = present_pic_plan(&q);
                    show("aa", aa, pic_aa_name(aa), p.aa_passes, p.blit, p.nearest, p.sharp, p.fxaa_steps, p.smaa_steps);
                    assert(p.aa == aa && p.scale == PIC_SCALE_BILINEAR && p.blit && !p.why);
                    if (aa == PIC_AA_OFF) assert(p.aa_passes == 0 && p.fxaa_steps == 0 && p.smaa_steps == 0);
                    else if (aa < PIC_AA_SMAA_LOW) assert(p.aa_passes == 1 && p.fxaa_steps == pic_fxaa_steps(aa) && p.smaa_steps == 0);
                    else assert(p.aa_passes == 3 && p.smaa_steps == pic_smaa_steps(aa) && p.fxaa_steps == 0);
                }
                q.aa = PIC_AA_OFF;
                for (int scale = 0; scale < PIC_SCALE_N; scale++) {
                    q.scale = scale;
                    pic_plan p = present_pic_plan(&q);
                    show("scale", scale, pic_scale_name(scale), p.aa_passes, p.blit, p.nearest, p.sharp, p.fxaa_steps, p.smaa_steps);
                    assert(p.scale == scale && p.aa_passes == 0 && !p.why);
                    assert(p.blit == (scale == PIC_SCALE_BILINEAR || scale == PIC_SCALE_NEAREST));
                    assert(p.nearest == (scale == PIC_SCALE_NEAREST));
                    assert(p.sharp == (scale == PIC_SCALE_FSR || scale == PIC_SCALE_SGSR ? 80 : 0));
                }
                q.scale = PIC_SCALE_FSR; q.dw = 1920;
                pic_plan small = present_pic_plan(&q);
                assert(small.blit && !small.nearest && small.scale == PIC_SCALE_BILINEAR && small.why);
                printf("fsr fallback: %s\n", small.why);
                q.scale = PIC_SCALE_SGSR;
                pic_plan sgsr = present_pic_plan(&q);
                assert(sgsr.blit && sgsr.scale == PIC_SCALE_BILINEAR && sgsr.why);
                printf("sgsr fallback: %s\n", sgsr.why);
                q.scale = PIC_SCALE_AREA; q.dw = 100;
                pic_plan area = present_pic_plan(&q);
                assert(!area.blit && area.scale == PIC_SCALE_AREA && !area.why);
                printf("area keeps its box without growth\n");
                q.scale = PIC_SCALE_SGSR; q.dw = 2400; q.pipe = 0;
                pic_plan refused = present_pic_plan(&q);
                assert(refused.blit && refused.scale == PIC_SCALE_BILINEAR && refused.why);
                printf("refused pipeline: %s\n", refused.why);
                q.pipe = 1; q.dyn = 0; q.aa = PIC_AA_FXAA;
                pic_plan nodyn = present_pic_plan(&q);
                assert(nodyn.blit && nodyn.aa_passes == 0 && nodyn.scale == PIC_SCALE_BILINEAR && nodyn.why);
                printf("no dynamic rendering: %s\n", nodyn.why);
                char path[600]; snprintf(path, sizeof path, "%s/settings.txt", g_root);
                FILE *f = fopen(path, "w"); assert(f); fputs("overlay=0\n", f); fclose(f);
                settings_load();
                assert(atomic_load(&g_set_aa) == PIC_AA_OFF && atomic_load(&g_set_scale) == PIC_SCALE_BILINEAR && atomic_load(&g_set_sharp) == 0);
                assert(atomic_load(&g_set_overlay) == 0);
                printf("untouched settings: bilinear anti-aliasing off\n");
                atomic_store(&g_set_aa, 0); atomic_store(&g_set_scale, 0); atomic_store(&g_set_sharp, 0); atomic_store(&g_set_overlay, 1);
                f = fopen(path, "w"); assert(f); fputs("aa=99\nscale=9\nsharpness=140\naa=5\nscale=4\nsharpness=80\n", f); fclose(f);
                settings_load();
                assert(atomic_load(&g_set_aa) == PIC_AA_SMAA_ULTRA);
                assert(atomic_load(&g_set_scale) == PIC_SCALE_SGSR);
                assert(atomic_load(&g_set_sharp) == 80);
                printf("settings load aa %s scale %s sharp %d\n", pic_aa_name(atomic_load(&g_set_aa)), pic_scale_name(atomic_load(&g_set_scale)), atomic_load(&g_set_sharp));
                atomic_store(&g_set_aa, PIC_AA_FXAA); atomic_store(&g_set_scale, PIC_SCALE_FSR); atomic_store(&g_set_sharp, 25);
                settings_save();
                atomic_store(&g_set_aa, 0); atomic_store(&g_set_scale, 0); atomic_store(&g_set_sharp, 0);
                settings_load();
                assert(atomic_load(&g_set_aa) == PIC_AA_FXAA && atomic_load(&g_set_scale) == PIC_SCALE_FSR && atomic_load(&g_set_sharp) == 25);
                char saved[256]; FILE *r = fopen(path, "r"); assert(r); size_t n = fread(saved, 1, sizeof saved - 1, r); saved[n] = 0; fclose(r);
                assert(strstr(saved, "aa=1\n")); assert(strstr(saved, "scale=3\n")); assert(strstr(saved, "sharpness=25\n"));
                printf("settings save round trip aa %s scale %s sharp %d\n", pic_aa_name(atomic_load(&g_set_aa)), pic_scale_name(atomic_load(&g_set_scale)), atomic_load(&g_set_sharp));
                setenv("DM_VK_AA", "3", 1); setenv("DM_VK_SCALE", "2", 1); setenv("DM_VK_SHARP", "40", 1);
                pic_set env = pic_settings_env((pic_set){atomic_load(&g_set_aa), atomic_load(&g_set_scale), atomic_load(&g_set_sharp)});
                assert(env.aa == PIC_AA_SMAA_MED && env.scale == PIC_SCALE_AREA && env.sharp == 40);
                printf("env override aa %s scale %s sharp %d\n", pic_aa_name(env.aa), pic_scale_name(env.scale), env.sharp);
                setenv("DM_VK_AA", "", 1); setenv("DM_VK_SCALE", "", 1); setenv("DM_VK_SHARP", "", 1);
                pic_set kept = pic_settings_env((pic_set){1, 3, 25});
                assert(kept.aa == 1 && kept.scale == 3 && kept.sharp == 25);
                printf("empty env keeps the saved values\n");
                return 0;
            }
        ''')
        # The placeholder fopen above is nonsense. Remove it before compiling.
        with tempfile.TemporaryDirectory(prefix='nx2a_present_pic_') as tmp:
            tmp = Path(tmp)
            (tmp / 'android_profile.h').write_text(android_profile.header(None), encoding='utf-8')
            src = tmp / 'present_pic_test.c'
            exe = tmp / 'present_pic_test'
            src.write_text(harness, encoding='utf-8')
            built = subprocess.run([cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', f'-I{tmp}', f'-I{ANDROID}',
                                    f'-I{ROOT / "nxnative/dm/host"}', str(src), '-lm', '-o', str(exe)],
                                    capture_output=True, text=True)
            if built.returncode != 0:
                self.fail(built.stderr)
            root = Path(tmp) / 'files'
            root.mkdir()
            run = subprocess.run([str(exe), str(root)], capture_output=True, text=True)
            _show(run.stdout)
            if run.returncode != 0:
                self.fail(run.stderr or run.stdout)
            out = run.stdout
            for name in ('Off', 'FXAA', 'SMAA Low', 'SMAA Medium', 'SMAA High', 'SMAA Ultra'):
                self.assertRegex(out, rf'select aa \d+ {re.escape(name)} ')
            self.assertNotIn('FXAA Low', out)
            self.assertRegex(out, r'select aa 1 FXAA passes 1 .* fxaa 8 ')
            prev = 0
            for level, steps in ((2, 4), (3, 8), (4, 16), (5, 32)):
                self.assertGreater(steps, prev)
                prev = steps
                self.assertRegex(out, rf'select aa {level} SMAA \w+ passes 3 .* smaa {steps}\b')
            draw = (ANDROID / 'present_draw.h').read_text(encoding='utf-8')
            self.assertIn('pic_gpl(&g_filt.blend', draw)
            self.assertIn('pic_img(&g_filt.weight[k]', draw)
            blend = draw[draw.index('pic_gpl(&g_filt.blend'):draw.index('pic_gpl(&g_filt.blend') + 400]
            weight = draw[draw.index('pic_img(&g_filt.weight[k]'):draw.index('pic_img(&g_filt.weight[k]') + 200]
            edge_img = draw[draw.index('pic_img(&g_filt.edges[k]'):draw.index('pic_img(&g_filt.weight[k]')]
            self.assertIn('VK_FORMAT_R16G16B16A16_SFLOAT', blend)
            self.assertIn('VK_FORMAT_R16G16B16A16_SFLOAT', weight)
            self.assertIn('VK_FORMAT_R8G8B8A8_UNORM', edge_img)
            self.assertNotIn('R8G8B8A8_UNORM', weight)
            due = present.index('ov_panel_due(menu)')
            keep = present.index('ov_panel_keep(menu)')
            self.assertLess(due, keep)
            self.assertIn('int64_t ov_panel_stamp', present)
            self.assertIn('externalMenuPicture', present)
            for name in ('Bilinear', 'Nearest', 'Area', 'FSR', 'SGSR'):
                self.assertRegex(out, rf'select scale \d+ {name} ')
            self.assertIn('default bilinear anti-aliasing off', out)
            self.assertIn('untouched settings: bilinear anti-aliasing off', out)
            self.assertRegex(out, r'fsr fallback: the picture does not grow enough')
            self.assertRegex(out, r'sgsr fallback: the picture does not grow enough')

    def test_pause_menu_redraws_picture_settings(self):
        """Left/Right and the sharpness slider redraw the shipped pause-menu raster.

        dm_menu_state does not carry anti-aliasing, the scaling filter or sharpness. The present compares ov_panel_stamp, then ov_panel_keep.
        """
        cc = shutil.which('cc') or shutil.which('clang')
        if not cc:
            self.skipTest('a C compiler is needed for the menu raster check')
        front = (ANDROID / 'front_android.c').read_text(encoding='utf-8')
        present = (ANDROID / 'gfx_vk_present.c').read_text(encoding='utf-8')
        state = front[front.index('int dm_menu_state(void)'):front.index('int dm_overlay_fps(')]
        pic = front[front.index('int dm_pic_aa(void)'):front.index('static void menu_open(')]
        sharp = front[front.index('static void menu_sharp('):front.index('static void menu_tab(')]
        key = front[front.index('static void menu_key('):front.index('// ---- the save manager')]
        shapes = present[present.index('typedef struct { uint8_t *px; uint32_t stride'):present.index('static void cv_box')]
        text = present[present.index('static float cv_text_w'):present.index('static const uint8_t C_WHITE')]
        white = present[present.index('static const uint8_t C_WHITE'):present.index('static void ov_menu_icon')]
        menu = present[present.index('static const uint8_t C_BG'):present.index('// The layout editor')]
        panel = present[present.index('static void ov_panel('):present.index('// The phone window')]
        self.assertIn('ov_panel_stamp', panel)
        self.assertIn('ov_panel_due', panel)
        self.assertIn('ov_panel_keep', panel)
        self.assertIn('dm_pic_aa()', panel)
        harness = textwrap.dedent(r'''
            #include <assert.h>
            #include <stdio.h>
            #include <stdlib.h>
            #include <string.h>
            #include <stdatomic.h>
            #include "overlay_layout.h"
            #include "pixel_font.h"
            #include "pixel_font_latin1.h"
            #include "present_pic.h"
            typedef struct { uint32_t width, height; } VkExtent2D;
            static VkExtent2D g_sext;
            static struct { uint8_t *px; uint32_t w, h, sh, pw, ph; int64_t panel; } g_ov;
            static _Atomic int g_set_overlay = 1, g_set_rumble, g_set_fps, g_set_opacity = 60;
            static _Atomic int g_set_aa, g_set_scale, g_set_sharp;
            static _Atomic int g_menu, g_menu_hi = -1, g_menu_tab, g_tx_on, g_tx_ver, g_edit, g_ed_st;
            static _Atomic int g_sp_on, g_sp_hi, g_sp_ver, g_sv_st, g_sv_n;
            static int saves, redraws, taps;
            static void settings_save(void) { saves++; }
            static void dm_haptics_android_tap(void) { taps++; }
            static void dm_overlay_redraw(void) { redraws++; }
            static int pad_labels(void) { return 0; }
            int dm_opmode(void) { return 0; }
            int dm_menu_audio_available(void) { return 1; }
            int dm_save_note(char *out, int cap) { if (cap > 0) out[0] = 0; return OVX_IDLE; }
            const uint8_t *dm_overlay_font(void) { return PF_LATIN1; }
            static void menu_opacity(int v) { (void)v; }
            static void menu_tab(int t) { (void)t; }
            static void menu_do(int item) { (void)item; }
            static void menu_close(void) {}
            static void ed_key(uint64_t bit) { (void)bit; }
            static void sp_key(uint64_t bit) { (void)bit; }
        ''') + state + pic + sharp + key + shapes + text + white + menu + textwrap.dedent(r'''
            static void ov_text_panel(const ov_canvas *cv) { (void)cv; assert(0); }
            static void ov_edit_panel(const ov_canvas *cv, int st, float W, float H) { (void)cv; (void)st; (void)W; (void)H; assert(0); }
            static void ov_save_panel(const ov_canvas *cv, int st) { (void)cv; (void)st; assert(0); }
        ''') + panel + textwrap.dedent(r'''
            static int panel_sync(int menu) {
                int due = ov_panel_due(menu);
                if (due) ov_panel_keep(menu);
                return due;
            }
            static int same(const uint8_t *a) {
                return memcmp(a, g_ov.px, (size_t)g_ov.w * g_ov.ph * 4) == 0;
            }
            int main(void) {
                assert(C_HOT[0] == 255);
                g_ov.w = g_ov.pw = 720; g_ov.ph = 520; g_ov.h = g_ov.sh = 0; g_ov.panel = -1;
                g_ov.px = calloc((size_t)g_ov.w * g_ov.ph, 4); assert(g_ov.px);
                atomic_store(&g_menu, 1); atomic_store(&g_menu_tab, 1); atomic_store(&g_menu_hi, 3);
                int menu = dm_menu_state();
                assert(menu >= 0 && ov_menu_item(1, 3) == OVM_AA && ov_menu_item(1, 4) == OVM_SCALE && ov_menu_item(1, 5) == OVM_EXPORT);
                assert(panel_sync(menu) == 1);
                uint8_t *off = malloc((size_t)g_ov.w * g_ov.ph * 4); assert(off); memcpy(off, g_ov.px, (size_t)g_ov.w * g_ov.ph * 4);
                assert(panel_sync(dm_menu_state()) == 0 && same(off));
                int state = dm_menu_state();
                menu_key(DM_BTN_RIGHT);
                assert(atomic_load(&g_set_aa) == PIC_AA_FXAA);
                assert(dm_menu_state() == state);
                assert(panel_sync(dm_menu_state()) == 1 && !same(off));
                printf("panel aa redraw %s\n", pic_aa_name(dm_pic_aa()));
                memcpy(off, g_ov.px, (size_t)g_ov.w * g_ov.ph * 4);
                assert(panel_sync(dm_menu_state()) == 0 && same(off));
                menu_key(DM_BTN_DOWN);
                assert(ov_menu_item(1, atomic_load(&g_menu_hi)) == OVM_SCALE);
                assert(panel_sync(dm_menu_state()) == 1);
                memcpy(off, g_ov.px, (size_t)g_ov.w * g_ov.ph * 4);
                state = dm_menu_state();
                menu_key(DM_BTN_RIGHT);
                assert(atomic_load(&g_set_scale) == PIC_SCALE_NEAREST && dm_menu_state() == state);
                assert(panel_sync(dm_menu_state()) == 1 && !same(off));
                printf("panel scale redraw %s\n", pic_scale_name(dm_pic_scale()));
                menu_key(DM_BTN_DOWN);
                assert(ov_menu_item(1, atomic_load(&g_menu_hi)) == OVM_EXPORT);
                menu_key(DM_BTN_UP);
                assert(ov_menu_item(1, atomic_load(&g_menu_hi)) == OVM_SCALE);
                menu_key(DM_BTN_RIGHT);
                menu_key(DM_BTN_RIGHT);
                assert(atomic_load(&g_set_scale) == PIC_SCALE_FSR && ov_menu_item(1, 5) == OVM_SHARP);
                assert(panel_sync(dm_menu_state()) == 1);
                menu_key(DM_BTN_DOWN);
                assert(ov_menu_item(1, atomic_load(&g_menu_hi)) == OVM_SHARP);
                assert(panel_sync(dm_menu_state()) == 1);
                memcpy(off, g_ov.px, (size_t)g_ov.w * g_ov.ph * 4);
                state = dm_menu_state();
                menu_key(DM_BTN_RIGHT);
                assert(atomic_load(&g_set_sharp) == 25 && dm_menu_state() == state);
                assert(panel_sync(dm_menu_state()) == 1 && !same(off));
                printf("panel sharp step %d\n", dm_pic_sharp());
                memcpy(off, g_ov.px, (size_t)g_ov.w * g_ov.ph * 4);
                menu_sharp(40);
                assert(atomic_load(&g_set_sharp) == 50 && dm_menu_state() == state);
                assert(panel_sync(dm_menu_state()) == 1 && !same(off));
                printf("panel sharp drag %d\n", dm_pic_sharp());
                memcpy(off, g_ov.px, (size_t)g_ov.w * g_ov.ph * 4);
                assert(panel_sync(dm_menu_state()) == 0 && same(off));
                atomic_store(&g_tx_on, 1);
                int tx = dm_menu_state();
                g_ov.panel = ov_panel_stamp(tx);
                atomic_store(&g_set_aa, PIC_AA_SMAA_ULTRA);
                assert(dm_menu_state() == tx && ov_panel_stamp(tx) == g_ov.panel && ov_panel_due(tx) == 0);
                printf("panel text entry ignores picture settings\n");
                printf("panel saves %d redraws %d taps %d\n", saves, redraws, taps);
                assert(saves >= 3 && redraws >= 3 && taps >= 3);
                free(off); free(g_ov.px);
                return 0;
            }
        ''')
        with tempfile.TemporaryDirectory(prefix='nx2a_panel_') as tmp:
            tmp = Path(tmp)
            (tmp / 'android_profile.h').write_text(android_profile.header(None), encoding='utf-8')
            src = tmp / 'panel_test.c'
            exe = tmp / 'panel_test'
            src.write_text(harness, encoding='utf-8')
            built = subprocess.run([cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', f'-I{tmp}', f'-I{ANDROID}',
                                    f'-I{ROOT / "nxnative/dm/host"}', str(src), '-lm', '-o', str(exe)],
                                    capture_output=True, text=True)
            if built.returncode != 0:
                self.fail(built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            _show(run.stdout)
            if run.returncode != 0:
                self.fail(run.stderr or run.stdout)
            self.assertIn('panel aa redraw FXAA', run.stdout)
            self.assertIn('panel scale redraw Nearest', run.stdout)
            self.assertIn('panel sharp step 25', run.stdout)
            self.assertIn('panel sharp drag 50', run.stdout)
            self.assertIn('panel text entry ignores picture settings', run.stdout)

    def test_shader_sources_the_build_compiles(self):
        build = (ANDROID / 'build.py').read_text(encoding='utf-8')
        found = re.search(r"p\.suffix in \(([^)]+)\)", build)
        self.assertIsNotNone(found, 'build.py must list shader suffixes, so a .LICENSE is not compiled')
        suffixes = tuple(s.strip().strip("'\"") for s in found.group(1).split(','))
        self.assertIn('.frag', suffixes)
        self.assertNotIn('.LICENSE', suffixes)
        sources = {p.name: p.read_text(encoding='utf-8') for p in SHADERS.iterdir() if p.suffix in suffixes}
        licences = {p.name: p.read_text(encoding='utf-8') for p in SHADERS.iterdir() if p.suffix == '.LICENSE'}
        fxaa = sources['fxaa.frag']
        self.assertIn('const int n = 8', fxaa)
        self.assertNotIn('int[4](3, 5, 8, 12)', fxaa)
        self.assertIn('luma', fxaa.lower())
        edge, blend, neigh = sources['smaa_edge.frag'], sources['smaa_blend.frag'], sources['smaa_neigh.frag']
        self.assertIn('0.15', edge)
        self.assertIn('int[4](4, 8, 16, 32)', blend)
        self.assertIn('sampler2D', neigh)
        easu, rcas = sources['fsr_easu.frag'], sources['fsr_rcas.frag']
        self.assertIn('textureGather', easu)
        self.assertIn('FsrEasuTap', easu)
        self.assertGreaterEqual(easu.count('FsrEasuTap('), 12)
        self.assertIn('FsrRcasLoad', rcas)
        self.assertIn('mix(2.0, 0.0', rcas)
        sgsr = sources['sr.frag']
        self.assertGreaterEqual(sgsr.count('textureGather'), 4)
        self.assertIn('mix(8.0, 2.0, s.sharp)', sgsr)
        self.assertIn('mix(EdgeSharpness, 8.0, s.sharp)', sgsr)
        self.assertIn('s.sharp * 0.35', sgsr)
        self.assertIn('mix(23.0, 70.0, s.sharp)', sgsr)
        self.assertIn('Timothy Lottes', licences['fxaa.LICENSE'])
        self.assertIn('NVIDIA', licences['fxaa.LICENSE'])
        self.assertIn('not a copy', licences['fxaa.LICENSE'])
        self.assertIn('Jorge Jimenez', licences['smaa.LICENSE'])
        self.assertIn('Permission is hereby granted', licences['smaa.LICENSE'])
        self.assertIn('Advanced Micro Devices', licences['fsr1.LICENSE'])
        self.assertIn('FsrEasuF', licences['fsr1.LICENSE'])
        self.assertIn('FsrRcasF', licences['fsr1.LICENSE'])
        self.assertIn('MIT', licences['fsr1.LICENSE'])
        self.assertIn('Qualcomm', licences['sgsr1.LICENSE'])
        self.assertIn('BSD', licences['sgsr1.LICENSE'])
        _show('shader FXAA SMAA FSR EASU RCAS SGSR textureGather licences ok')


if __name__ == '__main__':
    unittest.main()
