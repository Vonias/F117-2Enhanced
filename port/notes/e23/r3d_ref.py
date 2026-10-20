"""S2.3: independent reference rasteriser against the hr 3D layer (ctest `r3d_hr_ref`).

  python r3d_ref.py <f117-enhanced.exe> <Game dir> <work dir> <golden work dir> N [N ...] [--dither grid,fine] [--keep]

Runs the render3d golden scenes (port/notes/s56/r3d_golden.py, cached references) with the enhancements on and `--hr-dump DIR --render-scale N`;
the port writes per scenario DIR/<id>.hrd: every primitive the hr back end rasterised (clipped edges, walls, fills with their colour word, lines
with their brush, 1001 polygons) at N x, plus the resulting 3D layer. This script fills the SAME primitives again with a textbook rule, written
independently of the DDA (no integer tables, no sentinels, no gap rows):

  a polygon is its closed set of segments; vertices sit at pixel centres (integer layer coordinates); row y is cut at its exact (float)
  intersections with every segment (a horizontal segment contributes both ends), the polygon covers the row from the smallest to the largest cut.
  strict coverage = pixels whose centre lies inside [min, max]  (ceil(min) .. floor(max)),
  wide coverage   = pixels the span touches                      (floor(min) .. ceil(max)).
  The original fill convention (inclusive Bresenham ends, which round to the nearest pixel) lies between the two, so a pixel is judged only when
  its last painter covers it strictly; a line is the same on its major axis (exact minor position; floor and ceil are the wide coverage, the
  rounded position the strict one unless the fraction is within 0.05 of one half), brush w x w at offset o. Colours: two-colour dither (high
  colour on odd phase; phase = original-pixel grid or layer pixel, --dither), solid, stipple FCh; AND / OR (FEh / FDh) and hz_draw's half
  plane make a pixel "unknown" (not judged). Painting order is the trace order; a pass (B) clears its window.

Per scenario every layer pixel is classified:
  gap     a strict-covered pixel (by the last primitive that wrote it) is not written by the hr back end: a crack or a hole
  wrong   it is written with another colour (dither phase, shade, painter order)
  stray   no primitive's wide coverage reaches the pixel but the back end wrote it (wrong N placement, 16-bit wrap, overflow)
Stamped pixels (1x writes copied into their N x N block) are not judged. Tolerances: LIMITS below. Exit 77 = no game files.
"""
import os, shutil, subprocess, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from hrd import Hrd
GOLDEN = os.path.join(HERE, '..', 's56', 'r3d_golden.py')

# allowed fraction of the judged layer pixels (per scenario with >= 500 judged, and overall), set from the measurements
# measured 2026-10-20 (N = 2 / 4, grid / fine): gap 0, stray 0, wrong 0.001 % overall (a pixel of a 1001 sky-band edge, the worst scenario 0.14 %)
LIMITS_TOTAL = {'gap': 0.00001, 'wrong': 0.0001, 'stray': 0.00001}
LIMITS = {'gap': 0.0005, 'wrong': 0.003, 'stray': 0.0005}
MIN_DITHER = 50                      # the scenes must really contain two-colour fills


class Ref:
    def __init__(self, h):
        self.h = h
        self.val = np.full((h.lh, h.lw), -1, np.int16)         # -1 untouched, -2 unknown / maybe written, >= 0 expected colour
        self.win = None
        self.polys = 0
        self.lines = 0
        self.fills = 0

    def phase(self, xs, y):
        h = self.h
        dx, dy = xs - h.fx, y - h.fy
        if h.fine:
            return (dx ^ dy) & 1
        return ((dx // h.n) ^ (dy // h.n)) & 1

    def span_arrays(self):
        rows = self.win[3] - self.win[1] + 1
        return [np.full(rows, np.inf), np.full(rows, -np.inf), np.full(rows, np.inf), np.full(rows, -np.inf)]   # strict min / max, wide min / max

    def add_segment(self, sp, x0, y0, x1, y1, dw=0.5 + 1e-6):
        """An edge is drawn as a line of pixels, so row y holds the part of the segment within half a pixel of y (the pixels the edge
        itself covers): strict = within 0.5 - d (certainly drawn), wide = within 0.5 + d (possibly, ties either way)."""
        wy0, wy1 = self.win[1], self.win[3]
        if y0 > y1:
            x0, y0, x1, y1 = x1, y1, x0, y0
        lo, hi = max(int(np.floor(y0 - dw - 0.01)), wy0), min(int(np.ceil(y1 + dw + 0.01)), wy1)
        if hi < lo:
            return
        ys = np.arange(lo, hi + 1)
        for k, d in ((0, 0.5 - 0.02), (2, dw)):
            ya, yb = np.maximum(ys - d, y0), np.minimum(ys + d, y1)
            ok = ya <= yb
            if not ok.any():
                continue
            if y1 == y0:
                xa = np.full(len(ys), float(min(x0, x1)))
                xb = np.full(len(ys), float(max(x0, x1)))
            else:
                t = (x1 - x0) / (y1 - y0)
                xa, xb = x0 + t * (ya - y0), x0 + t * (yb - y0)
                xa, xb = np.minimum(xa, xb), np.maximum(xa, xb)
            idx = (ys - wy0)[ok]
            sp[k][idx] = np.minimum(sp[k][idx], xa[ok])
            sp[k + 1][idx] = np.maximum(sp[k + 1][idx], xb[ok])

    def paint(self, sp, colour, mode, slack=0):
        wx0, wy0, wx1, _ = self.win
        mnS, mxS, mnW, mxW = sp
        rows = np.nonzero(np.isfinite(mnW))[0]
        if len(rows) == 0:
            return
        eps = 1e-6
        fx0 = np.floor(mnW[rows] + eps).astype(np.int64) - slack
        fx1 = np.ceil(mxW[rows] - eps).astype(np.int64) + slack
        has_s = np.isfinite(mnS[rows])
        sx0 = np.where(has_s, np.ceil(np.where(has_s, mnS[rows], 0) - eps), 1).astype(np.int64) + slack
        sx1 = np.where(has_s, np.floor(np.where(has_s, mxS[rows], 0) + eps), 0).astype(np.int64) - slack
        al, ah = colour & 255, (colour >> 8) & 255
        for k, r in enumerate(rows):
            y = wy0 + int(r)
            a0, a1 = max(int(fx0[k]), wx0), min(int(fx1[k]), wx1)
            if a1 < a0:
                continue
            row = self.val[y]
            row[a0:a1 + 1] = -2                                # every pixel the span touches is at least "maybe written"
            b0, b1 = max(int(sx0[k]), wx0), min(int(sx1[k]), wx1)
            if b1 < b0:
                continue
            xs = np.arange(b0, b1 + 1)
            if mode == 'dither':
                row[b0:b1 + 1] = np.where(self.phase(xs, y) == 1, ah, al)
            elif mode == 'solid':
                row[b0:b1 + 1] = al
            elif mode == 'stipple':
                ph = self.phase(xs, y) == 1
                cur = row[b0:b1 + 1]
                cur[ph] = al                                   # not in phase: the pixel stays unknown (it was -2 above)
            else:                                              # AND / OR: depends on what was there
                row[b0:b1 + 1] = -2

    def run(self):
        h = self.h
        edges = []
        hz = False
        for ty, a in h.events():
            if ty != 4:
                hz = False
            if ty == 0:
                self.win = tuple(int(v) for v in a)
                x0, y0, x1, y1 = self.win
                self.val[y0:y1 + 1, x0:x1 + 1] = -1
                edges = []
            elif ty == 1:
                edges.append((0, tuple(int(v) for v in a)))
            elif ty == 2:
                edges.append((1, tuple(int(v) for v in a)))
            elif ty == 3:
                if self.win is None:
                    continue
                sp = self.span_arrays()
                wy0, wy1 = self.win[1], self.win[3]
                for kind, v in edges:
                    if kind == 0:
                        self.add_segment(sp, *v)
                    else:                                      # wall: rows a..b at column x (hr_wall_fill)
                        ra, rb, x = v
                        if rb < ra:
                            ra, rb = rb, ra
                        if rb <= wy0 or ra >= wy1:
                            continue
                        ra, rb = max(ra, wy0), min(rb, wy1)
                        if rb == ra:
                            continue
                        i = slice(ra - wy0, rb - wy0 + 1)
                        for k in (0, 2):
                            sp[k][i] = np.minimum(sp[k][i], x)
                            sp[k + 1][i] = np.maximum(sp[k + 1][i], x)
                edges = []
                colour = int(a[0]) & 0xFFFF
                ah = colour >> 8
                if ah < 0xFB or ah == 0xFF:
                    if ah == 0xFF:
                        colour = (colour & 255) | ((colour & 255) << 8)
                    mode = 'solid' if (colour >> 8) == (colour & 255) else 'dither'
                elif ah == 0xFC:
                    mode = 'stipple'
                elif ah in (0xFD, 0xFE):
                    mode = 'rmw'
                else:
                    continue                                   # FBh: rows only
                self.paint(sp, colour, mode)
                self.fills += 1
            elif ty == 4:
                self.line(*[int(v) for v in a], hz=hz)
            elif ty == 5:
                self.poly(a)
            elif ty == 6 and self.win:
                x0, y0, x1, y1 = self.win
                self.val[y0:y1 + 1, x0:x1 + 1] = -2
                hz = True

    def poly(self, a):
        ox, oy, colour, n = int(a[0]), int(a[1]), int(a[2]) & 255, int(a[3])
        if self.win is None:
            return
        pts = [(ox + int(a[4 + 2 * i]), oy + int(a[5 + 2 * i])) for i in range(n)]
        sp = self.span_arrays()
        for i in range(n):
            x0, y0 = pts[i]
            x1, y1 = pts[(i + 1) % n]
            self.add_segment(sp, x0, y0, x1, y1, 1.0)
        self.paint(sp, colour | (colour << 8), 'solid', 1)
        self.polys += 1

    def line(self, x0, y0, x1, y1, c, w, o, hz=False):
        if self.win is None:
            return
        wx0, wy0, wx1, wy1 = self.win
        dx, dy = x1 - x0, y1 - y0
        xmajor = abs(dy) <= abs(dx)
        if xmajor:
            ts = np.arange(min(x0, x1), max(x0, x1) + 1)
            m = (y0 + dy * (ts - x0) / dx) if dx else np.full(len(ts), float(y0))
        else:
            ts = np.arange(min(y0, y1), max(y0, y1) + 1)
            m = x0 + dx * (ts - y0) / dy
        fl = np.floor(m + 1e-6)
        fr = m - fl
        exact = (np.abs(fr) < 1e-6) | (np.abs(fr - 1) < 1e-6)
        strict_ok = exact | (np.abs(fr - 0.5) > 0.05)
        rnd = np.floor(m + 0.5).astype(np.int64)
        lo, hi = fl.astype(np.int64), np.ceil(m - 1e-6).astype(np.int64)
        for ky in range(w):
            for kx in range(w):
                for minor, major, strict in ((lo, ts, False), (hi, ts, False), (rnd[strict_ok], ts[strict_ok], not hz)):
                    if xmajor:
                        px, py = major + o + kx, minor + o + ky
                    else:
                        px, py = minor + o + kx, major + o + ky
                    ok = (px >= wx0) & (px <= wx1) & (py >= wy0) & (py <= wy1)
                    self.val[py[ok], px[ok]] = c if strict else -2
        self.lines += 1


def check(h):
    """-> dict of counts for one scenario"""
    r = Ref(h)
    r.run()
    out = dict(judged=0, gap=0, wrong=0, stray=0, stamped=0, polys=r.polys, fills=r.fills, lines=r.lines, dith=0)
    if not h.has_win:
        return out
    lay = h.full_layer()
    written = (lay & 0x100) != 0
    stamped = (lay & 0x200) != 0
    val = r.val
    v = (lay & 0xFF).astype(np.int16)
    known = val >= 0
    out['judged'] = int((known & ~stamped).sum())
    out['stamped'] = int(stamped.sum())
    out['gap'] = int((known & ~stamped & ~written).sum())
    out['wrong'] = int((known & ~stamped & written & (v != val)).sum())
    out['stray'] = int(((val == -1) & written & ~stamped).sum())
    return out


def dithers(h):
    """number of dithered fills (two different colours) in the trace"""
    k = 0
    for ty, a in h.events():
        if ty == 3:
            c = int(a[0]) & 0xFFFF
            if (c >> 8) < 0xFB and (c >> 8) != (c & 255):
                k += 1
    return k


def main():
    argv = sys.argv[1:]
    dither = 'grid,fine'
    if '--dither' in argv:
        i = argv.index('--dither')
        dither = argv[i + 1]
        del argv[i:i + 2]
    keep = '--keep' in argv
    argv = [a for a in argv if a != '--keep']
    if len(argv) < 5:
        print(__doc__)
        return 2
    exe, game, work, gwork = argv[:4]
    ns = [int(a) for a in argv[4:]]
    os.makedirs(work, exist_ok=True)
    for fn in ('refs.bin', 'stamp.txt'):
        src = os.path.join(gwork, fn)
        if os.path.exists(src):
            shutil.copyfile(src, os.path.join(work, fn))
    bad = 0
    for n, dither in [(n, d) for d in dither.split(',') for n in ns]:
        for f in os.listdir(work):
            if f.endswith('.hrd'):
                os.remove(os.path.join(work, f))
        env = dict(os.environ)
        env.pop('F117_CLASSIC', None)
        env['F117_ENH_ARGS'] = '--hr-dump %s --render-scale %d --dither %s' % (os.path.abspath(work).replace('\\', '/'), n, dither)
        r = subprocess.run([sys.executable, GOLDEN, exe, game, work], env=env, capture_output=True, text=True, timeout=1800)
        if r.returncode == 77:
            print('r3d-ref: no game files (skipped)')
            return 77
        if r.returncode != 0:
            print('r3d-ref N=%d: the golden scenes differ with the enhancements on:' % n, r.stdout[-800:], r.stderr[-400:])
            bad += 1
            continue
        files = sorted(f for f in os.listdir(work) if f.endswith('.hrd'))
        tot = dict(judged=0, gap=0, wrong=0, stray=0, stamped=0, polys=0, fills=0, lines=0, dith=0)
        worst = []
        for f in files:
            h = Hrd(os.path.join(work, f))
            c = check(h)
            c['dith'] = dithers(h)
            for k in tot:
                tot[k] += c[k]
            j = max(1, c['judged'])
            frac = {k: c[k] / j for k in ('gap', 'wrong', 'stray')}
            worst.append((max(frac.values()), f[:-4], c))
            for k, lim in LIMITS.items():
                if c['judged'] >= 500 and frac[k] > lim:
                    print('  scenario %s: %s %.4f %% (%d of %d judged)' % (f[:-4], k, 100 * frac[k], c[k], c['judged']))
                    bad += 1
        j = max(1, tot['judged'])
        print('r3d-ref N=%d %s: %d scenarios, %d polygons, %d lines, %d dithered fills; %d layer px judged: gap %.4f %%, wrong %.4f %%, stray %d (%.4f %%), %d stamped skipped'
              % (n, dither, len(files), tot['fills'] + tot['polys'], tot['lines'], tot['dith'], tot['judged'], 100 * tot['gap'] / j,
                 100 * tot['wrong'] / j, tot['stray'], 100 * tot['stray'] / j, tot['stamped']))
        for fr, name, c in sorted(worst, reverse=True)[:3]:
            print('  worst: %s gap %d wrong %d stray %d of %d judged' % (name, c['gap'], c['wrong'], c['stray'], c['judged']))
        if tot['dith'] < MIN_DITHER:
            print('  only %d dithered fills in the scenes (need %d)' % (tot['dith'], MIN_DITHER))
            bad += 1
        if tot['judged'] == 0:
            print('  nothing judged')
            bad += 1
        for k, lim in LIMITS_TOTAL.items():
            if tot[k] / j > lim:
                print('  total %s above %.4f %%' % (k, 100 * lim))
                bad += 1
        if not keep:
            for f in files:
                os.remove(os.path.join(work, f))
    print('r3d-ref %s' % ('OK' if not bad else 'FAILED'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
