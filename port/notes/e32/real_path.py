"""S3.2 fix: the TV camera and the map MFD in the REAL game path (ctest `r3d_hr_realpath`, `--tvmap-real` of dump_scenes.py uses its frames).

The scripted chain of debrief_chain_sea (START: a new pilot, a strike over Libya -> VGAME) runs headless on the virtual clock with the enhancements on and
`--render-scale N --mfd-probe PREFIX`; keys scripted into VGAME (F117_KEYS) switch the right MFD to the TV page (the look key `/`) and the left MFD to the map (F3).
After every page flip the port writes how many pixels of the TV window (x 169..245, y 110..169) and of the map window (x 74..150, y 110..169) of the shown page are
shown from the 3D layer (a 3D shadow record equal to the page) against the window size; F117_PROBE_FRAMES dumps the composed canvas of chosen frames.

usage: python real_path.py <f117 executable> <game dir> <work dir> [N] [--frames a,b]      exit 77 without the game files
"""
import os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'e24'))
S54 = os.path.join(HERE, '..', 's54')
S58 = os.path.join(HERE, '..', 's58')
import sandbox_hash as SH


def run(exe, game, work, name, n, keys, probe_frames='', frames=300, extra=''):
    d = os.path.join(work, name)
    g = SH.fresh_game(game, os.path.join(d, 'game'))
    pre = os.path.join(d, 'probe')
    env = dict(os.environ, F117_KEYS=keys, F117_SHELL_SCRIPT=os.path.join(S54, 'shell_chain_libya.txt'), F117_VGAME_FRAMES=str(frames),
               F117_END_SCRIPT=os.path.join(S58, 'end_chain.txt'), F117_VCLOCK='1', F117_TRACE='1', F117_VGAME_SELECT=os.environ.get('SEL', '120'), F117_VGAME_SITE='0', F117_PROBE_FRAMES=probe_frames,
               F117_ENH_ARGS=('--render-scale %d --mfd-probe %s %s' % (n, pre.replace(os.sep, '/'), extra)).strip())
    env.pop('F117_CLASSIC', None)
    p = subprocess.run([exe, '--headless', '--trace', '--game-dir', g], env=env, capture_output=True, text=True, timeout=900)
    open(os.path.join(d, 'stdout.txt'), 'w').write(p.stdout)
    open(os.path.join(d, 'stderr.txt'), 'w').write(p.stderr)
    return p, pre


KEYS = '1:31,2:03,11:39,75:352f'      # site 0 = the home airbase (F117_VGAME_SITE)
KEYS_FLIR = KEYS + ',76:4000'          # F6: the FLIR tint on the TV page
#      # START's keys (the chain), then the look key `/` at 75 s: the right MFD shows the TV page (the left MFD shows the map from the start)
SELECT = 230                           # frame-loop iterations before the nearest site is selected (F117_VGAME_SELECT: after the key above has cleared the selection)
TV_FROM, MAP_FROM = 240, 100           # frames from which the TV / the map must be settled
TV_MIN, MAP_MIN = 0.90, 0.95           # of the 77 x 60 window shown from the 3D layer (the rest: the range / bearing / name labels, the aircraft marker)


def parse(path):
    rows = []
    for l in open(path):
        f = l.split()
        if len(f) > 5 and f[0] == 'frame':
            t, m = f[3].split('/'), f[5].split('/')
            rows.append((int(f[1]), int(t[0]), int(t[1]), int(m[0]), int(m[1])))
    return rows


def write_pngs(raw, out, tag):
    """the TV and map windows (and the whole frame) of a dumped frame: real_<tag>_<tv|map|frame>_<classic|hr>.png; returns {name: (classic, hr)} as RGB arrays"""
    import numpy as np
    from PIL import Image
    b = open(raw, 'rb').read()
    lw, lh = np.frombuffer(b, '<i4', 2)
    imgs = [np.frombuffer(b, '<u4', int(lw * lh), 8 + 4 * int(lw * lh) * k).reshape(lh, lw) for k in (0, 1)]    # composed (hr), then the classic 1x page scaled
    n = int(lw) // 320
    rgb = lambda c: np.stack([(c >> 16) & 255, (c >> 8) & 255, c & 255], 2).astype(np.uint8)
    res = {}
    for name, (x0, y0, w, h) in (('tv', (169, 110, 77, 61)), ('map', (74, 110, 77, 60)), ('frame', (0, 0, 320, 200))):
        pair = {}
        for k, im in zip(('hr', 'classic'), imgs):
            pair[k] = rgb(im[y0 * n:(y0 + h) * n, x0 * n:(x0 + w) * n])
            Image.fromarray(pair[k]).save(os.path.join(out, 'real_%s_%s_%s.png' % (tag, name, k)))
        res[name] = (pair['classic'], pair['hr'])
    return res


def sheet(out, rows):
    """contact_tv_map.png: for every (label, classic, hr) a row, classic left and hr right, each shown at twice its size"""
    from PIL import Image, ImageDraw
    z = 2
    cw = max(r[1].shape[1] for r in rows) * z
    ch = max(r[1].shape[0] for r in rows) * z + 22
    im = Image.new('RGB', (2 * cw, len(rows) * ch), (20, 20, 24))
    for i, (label, c, h) in enumerate(rows):
        for j, (k, a) in enumerate((('classic', c), ('hr', h))):
            ImageDraw.Draw(im).text((j * cw + 6, i * ch + 3), '%s  %s' % (label, k), fill=(235, 235, 235))
            im.paste(Image.fromarray(a).resize((a.shape[1] * z, a.shape[0] * z), Image.NEAREST), (j * cw, i * ch + 22))
    im.save(os.path.join(out, 'contact_tv_map.png'))
    print('real-path: %s' % os.path.join(out, 'contact_tv_map.png'))


def main():
    argv = [a for a in sys.argv[1:] if not a.startswith('--')]
    png = sys.argv[sys.argv.index('--png') + 1] if '--png' in sys.argv else None
    if png:
        argv.remove(png)
    exe, game, work = (os.path.abspath(a) for a in argv[:3])
    n = int(argv[3]) if len(argv) > 3 else 4
    if not os.path.isfile(os.path.join(game, 'END.EXE')) or not os.path.isfile(os.path.join(SH.ROOT, 'work', 'unpacked', 'START.EXE')):
        print('no game files')
        return 77
    os.environ['SEL'] = str(SELECT)
    os.makedirs(work, exist_ok=True)
    p, pre = run(exe, game, work, 'real', n, KEYS, '%d' % (SELECT + 60))
    want = [r'EXEC VGAME\.EXE', r'VGAME\.EXE exit code']
    pos = 0
    for w in want:
        m = re.compile(w).search(p.stdout, pos)
        if not m:
            print("r3d-realpath FAILED: %s not found" % w, p.stdout[-1500:], p.stderr[-600:])
            return 1
        pos = m.end()
    rows = parse(pre + '.txt')
    tv = [r for r in rows if r[0] >= TV_FROM]
    mp = [r for r in rows if r[0] >= MAP_FROM]
    bad = 0
    if len(tv) < 20 or len(mp) < 100:
        print('r3d-realpath FAILED: only %d frames' % len(rows))
        return 1
    tmin = min(r[1] / r[2] for r in tv)
    mmin = min(r[3] / r[4] for r in mp)
    print('r3d-realpath N=%d: %d VGAME frames; TV window shown from the 3D layer: min %.1f %% (frames %d..), map window: min %.1f %% (frames %d..)'
          % (n, len(rows), 100 * tmin, TV_FROM, 100 * mmin, MAP_FROM))
    if tmin < TV_MIN:
        print('  the TV window is below %.0f %%' % (100 * TV_MIN))
        bad += 1
    if mmin < MAP_MIN:
        print('  the map window is below %.0f %%' % (100 * MAP_MIN))
        bad += 1
    p2, pre2 = run(exe, game, work, 'real_flir', n, KEYS_FLIR, '%d' % (SELECT + 60))
    rows2 = parse(pre2 + '.txt')
    tv2 = [r for r in rows2 if r[0] >= TV_FROM]
    tmin2 = min((r[1] / r[2] for r in tv2), default=0)
    print('  with the FLIR tint (F6): TV window shown from the 3D layer: min %.1f %%' % (100 * tmin2))
    if tmin2 < TV_MIN:
        print('  the tinted TV window is below %.0f %%' % (100 * TV_MIN))
        bad += 1
    raw, raw2 = pre + '_%04d.raw' % (SELECT + 60), pre2 + '_%04d.raw' % (SELECT + 60)
    if png and os.path.isfile(raw) and os.path.isfile(raw2):
        os.makedirs(png, exist_ok=True)
        day, flir = write_pngs(raw, png, 'day'), write_pngs(raw2, png, 'flir')
        sheet(png, [('TV camera, airbase (day)', *day['tv']), ('TV camera, airbase (FLIR)', *flir['tv']), ('left MFD map', *day['map'])])
    print('r3d-realpath %s' % ('OK' if not bad else 'FAILED'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
