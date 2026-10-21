"""S3.4: the wide hr scenes (ctest `r3d_hr_wide`): the render3d golden scenes with every main 3D pass drawn as a wide pass on a canvas wider and taller than the frame.

  python wide_scenes.py <f117-enhanced.exe> <Game dir> <work dir> <golden work dir> [N]

The golden scenes (port/notes/s56/r3d_golden.py, cached references) run once as usual at --render-scale N with --hr-dump (the baseline: the frame-sized hr layer;
the golden pages must stay byte for byte), then once per canvas with `--wide-test WxH` (a 16:9-like and a 21:9-like canvas): each scene's main 3D window
(x 0..319 from row 0: view_frame's, or a poked r3d_scene_draw) is drawn with the culling, the frustum, the terrain cells and B85D widened to the canvas, the hr
window the canvas less the frame below the 3D window (the panel). Per canvas:
  reference   the independent rasteriser of port/notes/e23/r3d_ref.py on the whole layer (gap / wrong / stray, the same limits as r3d_hr_ref);
  unwritten   layer pixels of a wide window (outside the hole and outside the later passes' windows) that no hr primitive wrote: a gap in the extended world;
  hole        layer pixels written inside the hole by the wide pass (must be 0);
  inside      inside the 3D window of the frame, the wide layer against the baseline layer: the share of equal pixels (they differ only where the side
              cells / objects the wide pass adds overlap into the frame);
  seams       pixel pairs across the frame border (left, right, top; both written) that differ, against the same count across block boundaries inside the
              frame (columns N 80, N 160, N 240): the extended world continues the inside.
Exit 77 = no game files.
"""
import os, shutil, subprocess, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'e23'))
from hrd import Hrd
import r3d_ref as REF
GOLDEN = os.path.join(HERE, '..', 's56', 'r3d_golden.py')

CANVASES = {'16:9': (2.0 * 480, 480), '21:9': (2.0 * 640, 480)}     # at N = 2 (frame 640 x 400): extension 80 / 160 px left and right, 40 px on top (1x px: 40 / 80, 20)
UNWRITTEN_MAX = 0.0001
INSIDE_MIN = 0.98
SEAM_SLACK = 0.03


def run_port(exe, game, work, args):
    env = dict(os.environ)
    env.pop('F117_CLASSIC', None)
    env['F117_ENH_ARGS'] = args
    return subprocess.run([exe, '--render-test', work, '--game-dir', game], env=env, capture_output=True, text=True, timeout=1800)


def wide_windows(h):
    """[(x0, y0, x1, y1, hole)] of the wide passes and [(x0, y0, x1, y1)] of the other passes, in trace order"""
    passes = []                                            # [window, hole]
    for ty, a in h.events():
        if ty == 0:
            passes.append([tuple(int(v) for v in a[:4]), None])
        elif ty == 14 and passes:
            passes[-1][1] = tuple(int(v) for v in a)
    is_wide = lambda w: w[0] == 0 and w[2] == h.lw - 1 and (h.lw > 320 * h.n or h.lh > 200 * h.n)
    wide = [w + (hole,) for w, hole in passes if is_wide(w)]
    other = [w for w, hole in passes if not is_wide(w)]
    return wide, other


def scene_stats(h, base):
    """the S3.4 counts of one scene (h = the wide .hrd, base = the baseline .hrd of the same scene or None)"""
    out = dict(wide=0, wpx=0, unwritten=0, hole=0, inside=0, inside_eq=0, sp=0, sd=0, rp=0, rd=0)
    wide, other = wide_windows(h)
    if not wide:
        return out
    out['wide'] = 1
    lay = h.full_layer()
    wr = (lay & 0x100) != 0
    n, fx, fy = h.n, h.fx, h.fy
    later = np.zeros(wr.shape, bool)
    for x0, y0, x1, y1 in other:
        later[y0:y1 + 1, x0:x1 + 1] = True
    x0, y0, x1, y1, hole = wide[-1]
    m = np.zeros(wr.shape, bool)
    m[y0:y1 + 1, x0:x1 + 1] = True
    hm = np.zeros(wr.shape, bool)
    if hole:
        hm[hole[2]:, hole[0]:hole[1] + 1] = True
    m &= ~hm & ~later
    out['wpx'] = int(m.sum())
    out['unwritten'] = int((m & ~wr).sum())
    out['hole'] = int((hm & ~later & wr).sum())
    rows = slice(fy, (hole[2] if hole else fy + 200 * n))
    if base is not None and base.has_win and (base.lw, base.lh) == (320 * n, 200 * n):
        bl = base.full_layer()
        if True:
            b = bl[0:rows.stop - fy, :]                    # the baseline canvas is the frame (fx = fy = 0)
            w = lay[rows, fx:fx + 320 * n]
            both = ((b & 0x100) != 0) & ((w & 0x100) != 0)
            out['inside'] = int(both.sum())
            out['inside_eq'] = int((both & ((b & 0xFF) == (w & 0xFF))).sum())
    v = (lay & 0xFF).astype(np.int16)
    def pairs(ya, xa, yb, xb):
        ok = wr[ya, xa] & wr[yb, xb]
        return int(ok.sum()), int((ok & (v[ya, xa] != v[yb, xb])).sum())
    ys = np.arange(rows.start, rows.stop)
    for xa in (fx - 1, fx + 320 * n - 1):
        if 0 <= xa and xa + 1 < h.lw:
            p, d = pairs(ys, np.full(len(ys), xa), ys, np.full(len(ys), xa + 1))
            out['sp'] += p; out['sd'] += d
    if fy > 0:
        xs = np.arange(fx, fx + 320 * n)
        p, d = pairs(np.full(len(xs), fy - 1), xs, np.full(len(xs), fy), xs)
        out['sp'] += p; out['sd'] += d
    for k in (80, 160, 240):
        xa = fx + n * k - 1
        p, d = pairs(ys, np.full(len(ys), xa), ys, np.full(len(ys), xa + 1))
        out['rp'] += p; out['rd'] += d
    return out


def main():
    argv = sys.argv[1:]
    if len(argv) < 4:
        print(__doc__)
        return 2
    exe, game, work, gwork = (os.path.abspath(a) for a in argv[:4])
    n = int(argv[4]) if len(argv) > 4 else 2
    os.makedirs(work, exist_ok=True)
    for fn in ('refs.bin', 'stamp.txt'):
        src = os.path.join(gwork, fn)
        if os.path.exists(src):
            shutil.copyfile(src, os.path.join(work, fn))
    bdir = os.path.join(work, 'base')
    shutil.rmtree(bdir, ignore_errors=True)
    os.makedirs(bdir)
    env = dict(os.environ)
    env.pop('F117_CLASSIC', None)
    env['F117_ENH_ARGS'] = '--hr-dump %s --render-scale %d' % (bdir.replace('\\', '/'), n)
    r = subprocess.run([sys.executable, GOLDEN, exe, game, work], env=env, capture_output=True, text=True, timeout=1800)
    if r.returncode == 77:
        print('r3d-wide: no game files (skipped)')
        return 77
    if r.returncode != 0:
        print('r3d-wide: the golden scenes differ with the enhancements on:', r.stdout[-800:], r.stderr[-400:])
        return 1
    bad = 0
    for name, (cw, ch) in CANVASES.items():
        cw, ch = int(cw / 2 * n), int(ch / 2 * n)
        wdir = os.path.join(work, 'wide')
        shutil.rmtree(wdir, ignore_errors=True)
        os.makedirs(wdir)
        p = run_port(exe, game, work, '--hr-dump %s --render-scale %d --wide-test %dx%d' % (wdir.replace('\\', '/'), n, cw, ch))
        if p.returncode != 0:
            print('r3d-wide %s: the port failed' % name, p.stdout[-600:], p.stderr[-600:])
            return 1
        files = sorted(f for f in os.listdir(wdir) if f.endswith('.hrd'))
        tot = dict(judged=0, gap=0, wrong=0, stray=0, scenes=0, wide=0, wpx=0, unwritten=0, hole=0, inside=0, inside_eq=0, sp=0, sd=0, rp=0, rd=0)
        worst = []
        for f in files:
            h = Hrd(os.path.join(wdir, f))
            bf = os.path.join(bdir, f)
            base = Hrd(bf) if os.path.isfile(bf) else None
            s = scene_stats(h, base)
            tot['scenes'] += 1
            if not s['wide']:
                continue
            c = REF.check(h)
            for k in ('judged', 'gap', 'wrong', 'stray'):
                tot[k] += c[k]
            for k in s:
                tot[k] += s[k]
            j = max(1, c['judged'])
            frac = {k: c[k] / j for k in ('gap', 'wrong', 'stray')}
            worst.append((max(frac.values()), f[:-4], c, s))
            for k, lim in REF.LIMITS.items():
                if c['judged'] >= 500 and frac[k] > lim:
                    print('  %s scenario %s: %s %.4f %% (%d of %d judged)' % (name, f[:-4], k, 100 * frac[k], c[k], c['judged']))
                    bad += 1
            if s['wpx'] and s['unwritten'] / s['wpx'] > 0.001:
                print('  %s scenario %s: %d of %d wide-window pixels not written' % (name, f[:-4], s['unwritten'], s['wpx']))
                bad += 1
        j = max(1, tot['judged'])
        seam, ref = tot['sd'] / max(1, tot['sp']), tot['rd'] / max(1, tot['rp'])
        print('r3d-wide %s N=%d canvas %dx%d: %d scenarios, %d with a wide pass; reference: %d layer px judged, gap %.4f %%, wrong %.4f %%, stray %.4f %%; '
              'wide windows %d px, unwritten %d (%.4f %%), written in the hole %d; inside the frame %.2f %% equal to the frame-only render (%d px); '
              'seams %d / %d pairs differ (%.2f %%; inside the frame %.2f %%)' % (
                  name, n, cw, ch, tot['scenes'], tot['wide'], tot['judged'], 100 * tot['gap'] / j, 100 * tot['wrong'] / j, 100 * tot['stray'] / j,
                  tot['wpx'], tot['unwritten'], 100 * tot['unwritten'] / max(1, tot['wpx']), tot['hole'], 100 * tot['inside_eq'] / max(1, tot['inside']), tot['inside'],
                  tot['sd'], tot['sp'], 100 * seam, 100 * ref))
        for fr, nm, c, s in sorted(worst, key=lambda t: t[0], reverse=True)[:3]:
            print('  worst: %s gap %d wrong %d stray %d of %d judged' % (nm, c['gap'], c['wrong'], c['stray'], c['judged']))
        if tot['wide'] < 100 or tot['judged'] == 0:
            print('  too few wide scenes'); bad += 1
        for k, lim in REF.LIMITS_TOTAL.items():
            if tot[k] / j > lim:
                print('  total %s above %.4f %%' % (k, 100 * lim)); bad += 1
        if tot['unwritten'] / max(1, tot['wpx']) > UNWRITTEN_MAX:
            print('  the extended world has gaps'); bad += 1
        if tot['hole']:
            print('  the wide pass wrote into the panel hole'); bad += 1
        if tot['inside_eq'] / max(1, tot['inside']) < INSIDE_MIN:
            print('  the frame differs from the frame-only render'); bad += 1
        if tot['sp'] < 1000 or seam > ref + SEAM_SLACK:
            print('  seams at the frame border'); bad += 1
        shutil.rmtree(wdir, ignore_errors=True)
    shutil.rmtree(bdir, ignore_errors=True)
    print('r3d-wide %s' % ('OK' if not bad else 'FAILED'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
