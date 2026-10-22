"""S3.6: true night on the golden scenes (ctest `r3d_hr_night`, DESIGN.md 2.12).

  python night_scenes.py <f117-enhanced.exe> <Game dir> <work dir> <golden work dir> [N] [--png DIR]

The render3d golden scenes (port/notes/s56/r3d_golden.py; its snapshot s1 and scenario list from the golden work dir) run in the port only (--render-test) at
--render-scale N with --hr-dump and F117_HRD_RGB=1 (the canvas as composed from the live DAC, DIR/<id>.rgb; the night plane DIR/<id>.nd):
  day     every scene with `poke w 0x3658 0` + `call pal_flight_load` first (a day mission, palette C), without and with --true-night: every canvas identical,
          the night plane empty;
  night   every scene with `poke w 0x3658 1` + `call pal_flight_load` first (a night mission, palette B), without / with --true-night, and the same pair with
          --smooth-shading, and the pair on a wide canvas (--wide-test, 16:9-like). Per scene, option on against option off:
    kept      every canvas pixel whose night factor is 0 is identical (HUD, MFDs, cockpit art, the TV camera and the map scenes, sprites, lights);
    scaled    every pixel with a factor is the option-off colour times nd / 256 exactly, or (art over the 3D layer) the option-off colour;
    lights    no layer pixel in a light colour (00h..0Fh, 50h..5Fh, 90h..97h) has a factor;
    curve     ground / object factors within [K_FAR, K_NEAR], sky factors within [SKY_TOP, SKY_HORIZON] (x 256);
    monotonic down every column, a run of ground-band (70h..7Fh) or sky-band (80h..8Fh) pixels never gets darker (nearer ground and the sky nearer the horizon
              are brighter), tolerance 1;
    darker    the mean brightness of the darkened pixels and of the whole 3D window against option off.
With --png DIR: DIR/scene_<id>_{off,on,soff,son}.png of a few scenes and DIR/contact_scenes.png (+ wide/). Exit 77 = no game files.
"""
import os, shutil, subprocess, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'e23'))
from hrd import Hrd

K_NEAR, K_FAR, SKY_H, SKY_T = 128, 15, 77, 26           # x 256, rounded (hr_night.c)
MONO_TOL = 1
PNG_SCENES = ('runway0', 'runway_full', 'low_d3_0', 'low_d3_1', 'high0', 'close3', 'ext0', 'frames')
EXCLUDED = ('tv', 'map', 'hud_map')                     # scene families with no main 3D window: must stay identical


def run_port(exe, game, base, args):
    env = dict(os.environ)
    env.pop('F117_CLASSIC', None)
    env['F117_ENH_ARGS'] = args
    env['F117_HRD_RGB'] = '1'
    return subprocess.run([exe, '--render-test', base, '--game-dir', game], env=env, capture_output=True, text=True, timeout=1800)


def read_img(path, dt):
    b = open(path, 'rb').read()
    lw, lh = (int(v) for v in np.frombuffer(b, '<i4', 2))
    return np.frombuffer(b, dt, lw * lh, 8).reshape(lh, lw)


def scaled(c, nd):
    nd = nd.astype(np.uint32)
    out = np.zeros(c.shape, np.uint32)
    for sh in (0, 8, 16):
        out |= ((((c >> sh) & 255) * nd + 128) >> 8) << sh
    return out


def lum(c):
    return 0.299 * ((c >> 16) & 255) + 0.587 * ((c >> 8) & 255) + 0.114 * (c & 255)


def lit(idx):
    return (idx < 0x10) | ((idx >= 0x50) & (idx <= 0x5F)) | ((idx >= 0x90) & (idx <= 0x97))


def mono(lay, nd):
    """(runs, violations) of the band runs down the columns"""
    idx = (lay & 0xFF).astype(np.int32)
    ok = ((lay & 0x300) == 0x100) & (nd > 0)
    ramp = np.where(ok & (idx >= 0x80) & (idx <= 0x8F), 1, np.where(ok & (idx >= 0x70) & (idx <= 0x7F), 2, 0))
    same = (ramp[:-1] != 0) & (ramp[:-1] == ramp[1:])
    dn = nd[1:].astype(np.int32) - nd[:-1].astype(np.int32)
    bad_pair = same & (dn < -MONO_TOL)
    runs = int((same[1:] & ~same[:-1]).sum() + same[0].sum())
    return runs, int(bad_pair.sum())


def new_tot():
    return dict(scenes=0, missing=0, kept_bad=0, scaled_px=0, art_px=0, scaled_bad=0, lit_bad=0, lit_px=0, curve_bad=0, runs=0, mono_bad=0,
                excl_scenes=0, excl_bad=0, sum_on=0.0, sum_off=0.0, win_on=0.0, win_off=0.0, sky=0, ground=0, obj=0)


def compare(doff, don, tot, worst):
    for f in sorted(f for f in os.listdir(don) if f.endswith('.hrd')):
        sid = f[:-4]
        po, pn, pd = os.path.join(doff, sid + '.rgb'), os.path.join(don, sid + '.rgb'), os.path.join(don, sid + '.nd')
        if not (os.path.isfile(po) and os.path.isfile(pn) and os.path.isfile(pd)):
            tot['missing'] += 1
            continue
        tot['scenes'] += 1
        ra, rb, nd = read_img(po, '<u4'), read_img(pn, '<u4'), read_img(pd, 'u1')
        h = Hrd(os.path.join(don, f))
        lay = h.full_layer()
        if lay.shape != nd.shape:
            tot['missing'] += 1
            continue
        if sid.startswith(EXCLUDED):
            tot['excl_scenes'] += 1
            if (ra != rb).any() or nd.any():
                tot['excl_bad'] += 1
                worst.append('%s: an excluded scene changed' % sid)
        z = nd == 0
        kb = int((z & (ra != rb)).sum())
        tot['kept_bad'] += kb
        m = ~z
        exp = scaled(ra, nd)
        sc = m & (rb == exp)
        art = m & ~sc & (rb == ra)
        sb = int((m & ~sc & ~art).sum())
        tot['scaled_px'] += int(sc.sum()); tot['art_px'] += int(art.sum()); tot['scaled_bad'] += sb
        idx = (lay & 0xFF).astype(np.int32)
        wr = (lay & 0x100) != 0
        lb = int((wr & lit(idx) & m).sum())
        tot['lit_bad'] += lb
        tot['lit_px'] += int((wr & lit(idx) & z).sum())
        sky = m & (idx >= 0x80) & (idx <= 0x8F)
        gnd = m & ~sky
        cb = int((sky & ((nd < SKY_T - 1) | (nd > SKY_H + 1))).sum() + (gnd & ((nd < K_FAR - 1) | (nd > K_NEAR + 1))).sum())
        tot['curve_bad'] += cb
        tot['sky'] += int(sky.sum()); tot['ground'] += int((m & (idx >= 0x60) & (idx <= 0x7F)).sum()); tot['obj'] += int((m & (idx >= 0xA0)).sum())
        r, b = mono(lay, nd)
        tot['runs'] += r; tot['mono_bad'] += b
        tot['sum_on'] += float(lum(rb[sc]).sum()); tot['sum_off'] += float(lum(ra[sc]).sum())
        if h.has_win:
            w = np.zeros(nd.shape, bool)
            w[h.y0:h.y1 + 1, h.x0:h.x1 + 1] = True
            tot['win_on'] += float(lum(rb[w]).sum()); tot['win_off'] += float(lum(ra[w]).sum())
        if (kb or sb or lb or cb) and len(worst) < 8:
            worst.append('%s: kept %d, scaled %d, lights %d, curve %d' % (sid, kb, sb, lb, cb))


def verdict(name, tot, worst):
    ratio = tot['sum_on'] / max(1.0, tot['sum_off'])
    wratio = tot['win_on'] / max(1.0, tot['win_off'])
    print('r3d-night %s: %d scenes; pixels without a factor differing %d; %d darkened pixels (%d sky, %d ground faces 60h-7Fh, %d ramps A0h-FFh), %d under art, '
          'not the scaled colour %d; light-colour pixels kept %d, with a factor %d; factors off the curve %d; band runs %d, darker downwards %d; brightness of the '
          'darkened pixels %.1f %%, of the 3D windows %.1f %% of option off; excluded scenes (TV, map) %d, changed %d' % (
              name, tot['scenes'], tot['kept_bad'], tot['scaled_px'], tot['sky'], tot['ground'], tot['obj'], tot['art_px'], tot['scaled_bad'], tot['lit_px'],
              tot['lit_bad'], tot['curve_bad'], tot['runs'], tot['mono_bad'], 100 * ratio, 100 * wratio, tot['excl_scenes'], tot['excl_bad']))
    for w in worst[:8]:
        print('  ' + w)
    bad = 0
    if tot['scenes'] < 50 or tot['missing']:
        print('  too few scenes / missing dumps'); bad += 1
    if tot['kept_bad'] or tot['scaled_bad'] or tot['lit_bad'] or tot['curve_bad'] or tot['excl_bad']:
        bad += 1
    if tot['scaled_px'] < 100000 or tot['sky'] < 10000 or tot['obj'] < 1000 or tot['runs'] < 1000 or tot['excl_scenes'] < 5:
        print('  too little to judge'); bad += 1
    if tot['mono_bad'] > 0.001 * max(1, tot['runs']):
        print('  not monotonic'); bad += 1
    if ratio > 0.5 or wratio > 0.6:
        print('  not darker'); bad += 1
    return bad


def day_check(doff, don):
    scenes = differ = nd_px = missing = 0
    for f in sorted(f for f in os.listdir(don) if f.endswith('.hrd')):
        sid = f[:-4]
        po, pn = os.path.join(doff, sid + '.rgb'), os.path.join(don, sid + '.rgb')
        if not (os.path.isfile(po) and os.path.isfile(pn)):
            missing += 1
            continue
        scenes += 1
        if open(po, 'rb').read() != open(pn, 'rb').read():
            differ += 1
        pd = os.path.join(don, sid + '.nd')
        if os.path.isfile(pd):
            nd_px += int((read_img(pd, 'u1') != 0).sum())
    print('r3d-night day: %d scenes, canvas differing with the option %d, night-plane pixels %d' % (scenes, differ, nd_px))
    return 0 if scenes >= 50 and not missing and not differ and not nd_px else 1


def to_png(path, out_path=None):
    from PIL import Image
    im = read_img(path, '<u4')
    rgb = np.stack([(im >> 16) & 255, (im >> 8) & 255, im & 255], 2).astype(np.uint8)
    img = Image.fromarray(rgb)
    return img.resize((img.width, int(img.height * 1.2)), Image.NEAREST)


def write_pngs(dirs, tags, out, label):
    from PIL import Image, ImageDraw
    os.makedirs(out, exist_ok=True)
    rows = []
    for sid in PNG_SCENES:
        row = []
        for tag in tags:
            p = os.path.join(dirs[tag], sid + '.rgb')
            if not os.path.isfile(p):
                continue
            img = to_png(p)
            img.save(os.path.join(out, 'scene_%s_%s.png' % (sid, tag)))
            row.append((img, '%s  %s' % (sid, label[tag])))
        if row:
            rows.append(row)
    if not rows:
        return
    w = max(t[0].width for r in rows for t in r)
    h = max(t[0].height for r in rows for t in r) + 18
    cols = max(len(r) for r in rows)
    sheet = Image.new('RGB', (cols * (w + 8), h * len(rows)), (40, 40, 48))
    for i, r in enumerate(rows):
        for j, (img, cap) in enumerate(r):
            sheet.paste(img, (j * (w + 8), i * h + 18))
            ImageDraw.Draw(sheet).text((j * (w + 8) + 4, i * h + 3), cap, fill=(235, 235, 235))
    sheet.save(os.path.join(out, 'contact_scenes.png'))
    print('r3d-night: %s' % os.path.join(out, 'contact_scenes.png'))


def main():
    argv = sys.argv[1:]
    png = None
    if '--png' in argv:
        i = argv.index('--png'); png = os.path.abspath(argv[i + 1]); del argv[i:i + 2]
    if len(argv) < 4:
        print(__doc__)
        return 2
    exe, game, work, gwork = (os.path.abspath(a) for a in argv[:4])
    n = int(argv[4]) if len(argv) > 4 else 2
    if not os.path.isfile(os.path.join(game, 'END.EXE')):
        print('r3d-night: no game files (skipped)')
        return 77
    snap = ['s1.mem', 's1.a000', 's1.dac', 's1.meta', 'scn.txt']
    if not all(os.path.isfile(os.path.join(gwork, f)) for f in snap):
        print('r3d-night: the golden work dir has no snapshot (run render3d_golden first)')
        return 1
    os.makedirs(work, exist_ok=True)
    bases = {}
    for kind in ('day', 'night'):
        b = os.path.join(work, kind)
        shutil.rmtree(b, ignore_errors=True)
        os.makedirs(b)
        for f in snap[:-1]:
            shutil.copyfile(os.path.join(gwork, f), os.path.join(b, f))
        txt = open(os.path.join(gwork, 'scn.txt')).read().splitlines()
        out = []
        for l in txt:                                                           # the golden mission is a night one (its DAC pre-stepped toward C): set the
            out.append(l)                                                       # flag and load the palette as VGAME's start-up does (day C, night B)
            if l.startswith('scn '):
                out += ['poke w 0x3658 %d' % (kind == 'night'), 'call pal_flight_load']
        txt = out
        open(os.path.join(b, 'scn.txt'), 'w').write('\n'.join(txt) + '\n')
        bases[kind] = b
    cw, ch = 480 * n, 240 * n
    runs = [('day', 'off', ''), ('day', 'on', '--true-night'),
            ('night', 'off', ''), ('night', 'on', '--true-night'), ('night', 'soff', '--smooth-shading'), ('night', 'son', '--smooth-shading --true-night'),
            ('night', 'woff', '--wide-test %dx%d' % (cw, ch)), ('night', 'won', '--wide-test %dx%d --true-night' % (cw, ch))]
    dirs = {}
    for kind, tag, extra in runs:
        d = os.path.join(bases[kind], tag)
        os.makedirs(d)
        r = run_port(exe, game, bases[kind], ('--hr-dump %s --render-scale %d %s' % (d.replace('\\', '/'), n, extra)).strip())
        if r.returncode == 77:
            print('r3d-night: no game files (skipped)')
            return 77
        if r.returncode != 0:
            print('r3d-night: the %s %s run failed:' % (kind, tag), r.stdout[-600:], r.stderr[-600:])
            return 1
        dirs[kind + tag] = d
    bad = day_check(dirs['dayoff'], dirs['dayon'])
    for name, a, b in (('night N=%d' % n, 'nightoff', 'nighton'), ('night N=%d smooth shading' % n, 'nightsoff', 'nightson'),
                       ('night wide %dx%d N=%d' % (cw, ch, n), 'nightwoff', 'nightwon')):
        tot, worst = new_tot(), []
        compare(dirs[a], dirs[b], tot, worst)
        bad += verdict(name, tot, worst)
    if png:
        lab = {'nightoff': 'night', 'nighton': 'true night', 'nightsoff': 'night, smooth', 'nightson': 'true night, smooth', 'nightwoff': 'night wide',
               'nightwon': 'true night wide'}
        write_pngs(dirs, ('nightoff', 'nighton', 'nightsoff', 'nightson'), png, lab)
        write_pngs(dirs, ('nightwoff', 'nightwon'), os.path.join(png, 'wide'), lab)
    for b in bases.values():
        shutil.rmtree(b, ignore_errors=True)
    print('r3d-night %s' % ('OK' if not bad else 'FAILED'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
