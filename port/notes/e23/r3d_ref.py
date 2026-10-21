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
        self.sprites = self.dots = self.vlines = self.tints = 0
        self.vval = None                                       # the vector layer's expected values (S3.1), allocated on the first vector line
        self.hole = None                                       # S3.4: (x0, x1, y) of the open wide pass
        self.holes = []                                        # S3.4: the holes of the closed wide passes

    def unhole(self):
        """S3.4: the primitives of a wide pass inside its hole are not drawn: their pixels are not judged (a later pass, the TV camera, judges its own)"""
        if self.hole:
            x0, x1, y = self.hole
            self.val[y:, x0:x1 + 1] = np.where(self.val[y:, x0:x1 + 1] == -1, -1, -2)
            self.holes.append(self.hole)
            self.hole = None

    def stamp_block(self, x0, y0, w, hh, colours):
        """S3.1: layer pixels (x0 + i, y0 + j) = colours[j, i] where >= 0 (0 = transparent when colours < 0 is not used), clipped to the pass window"""
        wx0, wy0, wx1, wy1 = self.win
        for j in range(hh):
            y = y0 + j
            if y < wy0 or y > wy1:
                continue
            xs = np.arange(x0, x0 + w)
            ok = (xs >= wx0) & (xs <= wx1) & (colours[j] >= 0)
            self.val[y, xs[ok]] = colours[j][ok]

    def sprite(self, a):
        """S3.1: the sprite scaled to dw x dh, source column floor(i sw / dw), row floor(j sh / dh) (the DDA of MGRAPHIC sprite_scaled), 0 = transparent"""
        x0, y0, dw, dh, sw, sh = [int(v) for v in a[:6]]
        src = np.asarray(a[6:6 + sw * sh], np.int64).reshape(sh, sw)
        cols = (np.arange(dw, dtype=np.int64) * sw) // dw
        rows = (np.arange(dh, dtype=np.int64) * sh) // dh
        img = src[rows][:, cols]
        self.stamp_block(x0, y0, dw, dh, np.where(img != 0, img, -1))
        self.sprites += 1

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
            if ty == 14:                                       # S3.4: the wide main pass never writes the frame below its 3D window (the panel)
                self.hole = tuple(int(v) for v in a)
                continue
            if ty == 0:
                self.unhole()
                self.win = tuple(int(v) for v in a[:4])
                x0, y0, x1, y1 = self.win
                if not int(a[4]):                              # S3.2: a pass that continues the previous one (the TV close view) keeps its pixels
                    self.val[y0:y1 + 1, x0:x1 + 1] = -1
                edges = []
            elif ty == 10:                                     # S3.2: the FLIR tint recoloured the window through the green ramp 98h-9Fh
                x0, y0, x1, y1 = [int(v) for v in a]
                blk = self.val[y0:y1 + 1, x0:x1 + 1]
                self.val[y0:y1 + 1, x0:x1 + 1] = np.where(blk >= 0, ((blk & 0x0E) >> 1) | 0x98, blk)
                self.tints += 1
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
            elif ty == 7 and self.win:
                self.sprite(a)
            elif ty == 8 and self.win:
                x, y, n, c = [int(v) for v in a]
                self.stamp_block(x, y, n, n, np.full((n, n), c))
                self.dots += 1
            elif ty == 9:
                if self.vval is None:
                    self.vval = np.full((h.lh, h.lw), -1, np.int16)
                v = [int(q) for q in a]
                self.line(v[0], v[1], v[2], v[3], v[4], v[5], v[6], target=self.vval, bounds=(v[7], v[8], v[9], v[10]))
                self.vlines += 1
            elif ty == 11:                                     # S3.3: a polygon of the vector layer (the ADI): a layer pixel is in when its centre is inside or on an edge
                if self.vval is None:
                    self.vval = np.full((h.lh, h.lw), -1, np.int16)
                v = [int(q) for q in a]
                col, m, wx0, wy0, wx1, wy1 = v[:6]
                vx, vy = np.array(v[6::2][:m], np.int64), np.array(v[7::2][:m], np.int64)
                ys, xs = np.mgrid[wy0:wy1 + 1, wx0:wx1 + 1]
                px, py = 2 * xs + 1, 2 * ys + 1
                area = int((vx * np.roll(vy, -1) - np.roll(vx, -1) * vy).sum())
                if area:
                    sg = 1 if area > 0 else -1
                    ins = np.ones(xs.shape, bool)
                    for k in range(m):
                        j = (k + 1) % m
                        ins &= sg * ((vx[j] - vx[k]) * (py - vy[k]) - (vy[j] - vy[k]) * (px - vx[k])) >= 0
                    self.vval[ys[ins], xs[ins]] = col
                self.vlines += 1
            elif ty == 13:                                     # S3.3: the map window is saved to the backup page: its vector layer is kept
                x0, y0, x1, y1 = [int(q) for q in a]
                self.vsave = None if self.vval is None else self.vval[y0:y1 + 1, x0:x1 + 1].copy()
            elif ty == 12:                                     # S3.3: the window is restored from the backup: the lines drawn over it are gone, the saved ones come back
                if self.vval is not None:
                    x0, y0, x1, y1 = [int(q) for q in a]
                    vs = getattr(self, 'vsave', None)
                    self.vval[y0:y1 + 1, x0:x1 + 1] = -1 if vs is None else vs
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

    def line(self, x0, y0, x1, y1, c, w, o, hz=False, target=None, bounds=None):
        if self.win is None and bounds is None:
            return
        wx0, wy0, wx1, wy1 = bounds if bounds is not None else self.win
        val = self.val if target is None else target
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
                    val[py[ok], px[ok]] = c if strict else -2
        self.lines += 1


def check(h):
    """-> dict of counts for one scenario"""
    r = Ref(h)
    r.run()
    r.unhole()
    out = dict(judged=0, gap=0, wrong=0, stray=0, stamped=0, polys=r.polys, fills=r.fills, lines=r.lines, dith=0, sprites=r.sprites, dots=r.dots, vlines=r.vlines,
               vjudged=0, vgap=0, vwrong=0, vstray=0, tints=r.tints)
    if r.vval is not None and h.vec is not None:           # the vector layer: the clipped lines of the tracers / target boxes
        vw = (h.vec & 0x100) != 0
        vv = (h.vec & 0xFF).astype(np.int16)
        k = r.vval >= 0
        out['vjudged'] = int(k.sum())
        out['vgap'] = int((k & ~vw).sum())
        out['vwrong'] = int((k & vw & (vv != r.vval)).sum())
        out['vstray'] = int(((r.vval == -1) & vw).sum())
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
        tot = dict(judged=0, gap=0, wrong=0, stray=0, stamped=0, polys=0, fills=0, lines=0, dith=0, sprites=0, dots=0, vlines=0, vjudged=0, vgap=0, vwrong=0, vstray=0, tints=0)
        s32 = dict(judged=0, gap=0, wrong=0, stray=0, sprites=0, tints=0, n=0)       # S3.2: the TV camera (tv*) and map MFD (map*) scenes on their own
        s33 = dict(n=0, vlines=0, vjudged=0, vgap=0, vwrong=0, vstray=0)             # S3.3: the HUD / MFD scenes (hud_*): lines and the ADI polygon of the vector layer
        worst = []
        for f in files:
            h = Hrd(os.path.join(work, f))
            c = check(h)
            c['dith'] = dithers(h)
            for k in tot:
                tot[k] += c[k]
            if f.startswith('hud_'):
                s33['n'] += 1
                for k in ('vlines', 'vjudged', 'vgap', 'vwrong', 'vstray'):
                    s33[k] += c[k]
            if f.startswith(('tv', 'map')):
                s32['n'] += 1
                for k in ('judged', 'gap', 'wrong', 'stray', 'sprites', 'tints'):
                    s32[k] += c[k]
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
        print('  S3.1 objects: %d sprites, %d dots (judged with the polygons); %d vector lines: %d layer px judged, gap %d, wrong %d, stray %d'
              % (tot['sprites'], tot['dots'], tot['vlines'], tot['vjudged'], tot['vgap'], tot['vwrong'], tot['vstray']))
        print('  S3.2 TV camera and map MFD scenes: %d scenes, %d layer px judged, gap %d, wrong %d, stray %d; %d sprites, %d FLIR tints (judged through the ramp)'
              % (s32['n'], s32['judged'], s32['gap'], s32['wrong'], s32['stray'], s32['sprites'], s32['tints']))
        if s32['n'] < 30 or s32['judged'] < 90000 * n * n or s32['sprites'] < 60 or s32['tints'] < 6:
            print('  the TV / map scenes did not exercise two-pass windows, sprites, the FLIR tint or the map zooms')
            bad += 1
        if s32['gap'] + s32['stray'] or s32['wrong'] > s32['judged'] // 20000:
            print('  the TV / map windows differ from the reference rasteriser')
            bad += 1
        print('  S3.3 HUD / MFD scenes: %d scenes, %d vector lines and polygons, %d layer px judged: gap %d, wrong %d, stray %d' % (s33['n'], s33['vlines'], s33['vjudged'], s33['vgap'], s33['vwrong'], s33['vstray']))
        if s33['n'] < 12 or s33['vlines'] < 200 or s33['vjudged'] < 3000 * n:
            print('  the HUD / MFD scenes did not exercise the vector lines')
            bad += 1
        if tot['sprites'] < 50 or tot['dots'] < 8 or tot['vlines'] < 50 or tot['vjudged'] < 500:
            print('  the object scenes did not exercise sprites / dots / vector lines')
            bad += 1
        if tot['vgap'] or tot['vwrong'] or tot['vstray']:
            print('  the vector layer differs from the reference lines')
            bad += 1
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
