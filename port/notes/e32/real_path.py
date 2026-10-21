"""S3.2 fix / S3.3: the TV camera, the map MFD and the HUD in the REAL game path (ctest `r3d_hr_realpath`, `--tvmap-real` of dump_scenes.py uses its frames).

The scripted chain of debrief_chain_sea (START: a new pilot, a strike over Libya -> VGAME) runs headless on the virtual clock with the enhancements on and
`--render-scale N --mfd-probe PREFIX`; keys scripted into VGAME (F117_KEYS) switch the right MFD to the TV page (the look key `/`) and the left MFD to the map (F3).
After every page flip the port writes how many pixels of the TV window (x 169..245, y 110..169) and of the map window (x 74..150, y 110..169) of the shown page are
shown from the 3D layer (a 3D shadow record equal to the page) against the window size; F117_PROBE_FRAMES dumps the composed canvas of chosen frames.
S3.3 adds a run with scripted HUD keys (F4 HUD colour set, v HUD style 0/1/2, c small view, F8 navigation page = route lines on the map, F3 scope, Alt-N night) and measures per
frame how many of the HUD's vector records (lines, tick strips, ladder, reticle bars, frame; colour 0 excluded) and of the map window's show a vector-layer pixel (HUD_MIN, route).

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
    """-> a dict per probe line: frame, tv / map (shown from the 3D layer, window size), hudcol, hudlvl and the S3.3 vector records of the shown page per region (hudv, mapv, allv, tapel,
    taper, framel, framer, centre) as (those that show an hr vector pixel, all vector records)"""
    rows = []
    for l in open(path):
        f = l.split()
        if len(f) > 5 and f[0] == 'frame':
            d = {'frame': int(f[1])}
            i = 2
            while i < len(f):
                if '=' in f[i]:
                    k, v = f[i].split('=')
                    if k == 'hudcol':
                        d['hudcol'] = v
                    elif k == 'hudlvl':
                        d['hudlvl'] = int(v, 16)
                    elif k == 'vstale':                       # S3.4 fix: hr HUD line pixels left under a later classic line (must be 0)
                        d['vstale'] = int(v)
                    i += 1
                else:
                    a, b = f[i + 1].split('/')
                    d[f[i]] = (int(a), int(b))
                    i += 2
            rows.append(d)
    return rows


HUD_KEYS = KEYS + ',82:3e00,86:2f76,90:2f76,94:2f76,102:2e63,106:4200,110:3d00,114:3100'      # frames (t - 61) * 10: F4 -> 210, v -> 250 / 290 / 330, c -> 410, F8 -> 450, F3 -> 490, Alt-N -> 530
HUD_FRAMES = 620
HUD_PNG_FRAMES = {'day': 150, 'colour2': 235, 'style1': 275, 'style2': 315, 'small': 440, 'route': 470, 'scope': 520, 'night': 600}
HUD_MIN = 0.97                         # of the HUD's vector records (line pixels of the 1x footprint) that show an hr pixel; the rest would be a 1x line left behind
HUD_REGIONS = (('tapel', 20), ('taper', 20), ('framel', 30), ('framer', 30), ('centre', 50))      # (record name, minimum number of vector records the element must have)


def hud_states():
    """(label, first frame, last frame, elements that must exist) of the settled states of the HUD run (the frames just after a key are skipped)"""
    return (('HUD colour set 1, style 0', 20, 200, HUD_REGIONS), ('HUD colour set 2, style 0 (F4)', 225, 245, HUD_REGIONS), ('style 1 (v)', 262, 285, HUD_REGIONS),
            ('style 2 (v)', 302, 325, HUD_REGIONS[2:4]), ('style 0 again', 360, 405, HUD_REGIONS), ('small view (c)', 420, 445, HUD_REGIONS[4:]), ('small view, night (Alt-N)', 545, 615, HUD_REGIONS[4:]))


def check_hud(rows):
    bad = 0
    out = []
    for label, f0, f1, need in hud_states():
        rr = [r for r in rows if f0 <= r['frame'] <= f1]
        if not rr:
            out.append('  %s: no frames' % label)
            bad += 1
            continue
        hud = min(r['hudv'][0] / max(1, r['hudv'][1]) for r in rr)
        msg = '  %s: HUD vector records shown from the vector layer min %.1f %% (of %d)' % (label, 100 * hud, rr[-1]['hudv'][1])
        if hud < HUD_MIN or rr[-1]['hudv'][1] < 200:
            bad += 1
            msg += '  BELOW %.0f %% (or too few)' % (100 * HUD_MIN)
        for name, nmin in need:
            fr = min(r[name][0] / max(1, r[name][1]) for r in rr)
            cnt = min(r[name][1] for r in rr)
            msg += '; %s %.0f %% (%d)' % (name, 100 * fr, cnt)
            if fr < HUD_MIN or cnt < nmin:
                bad += 1
                msg += ' BAD'
        out.append(msg)
    stale = max((r.get('vstale', 0) for r in rows), default=0)
    out.append('  stale hr HUD line pixels (an hr line left under a later 1x line in the HUD rows): max %d per frame' % stale)
    if stale:
        bad += 1
        out.append('  an hr HUD line stays where the original has drawn over it')
    route = [r for r in rows if 458 <= r['frame'] <= 486]       # the navigation page: mfd_map_route draws the legs on both pages every step
    scope = [r for r in rows if 500 <= r['frame'] <= 525]       # the left MFD scope: range rings and sweep lines
    if route and scope:
        rmin = min(r['mapv'][0] / max(1, r['mapv'][1]) for r in route)
        rmax = max(r['mapv'][0] for r in route)
        smin = min(r['mapv'][0] / max(1, r['mapv'][1]) for r in scope)
        out.append('  map window with the route legs (F8): vector records shown from the vector layer min %.1f %% (up to %d records; the rest are the 2x2 and 1 pixel blips, which stay 1x pixels); '
                   'scope (F3): min %.1f %%' % (100 * rmin, rmax, 100 * smin))
        if rmin < 0.75 or rmax < 100 or smin < HUD_MIN or max(r['mapv'][1] for r in scope) < 300:
            bad += 1
            out.append('  the route legs or the scope are not drawn at N x')
    else:
        bad += 1
        out.append('  no route / scope frames')
    return bad, out


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


def hud_sheet(out, pre):
    """work/e33/: hud_<state>_<hud|mfd>_<classic|hr>.png (the HUD strip, the two MFD windows) and contact_hud.png (classic left, hr right) from the frames of the HUD run"""
    import numpy as np
    from PIL import Image, ImageDraw
    rows = []
    for tag, frame in HUD_PNG_FRAMES.items():
        raw = pre + '_%04d.raw' % frame
        if not os.path.isfile(raw):
            continue
        b = open(raw, 'rb').read()
        lw, lh = (int(v) for v in np.frombuffer(b, '<i4', 2))
        n = lw // 320
        imgs = [np.frombuffer(b, '<u4', lw * lh, 8 + 4 * lw * lh * k).reshape(lh, lw) for k in (0, 1)]
        rgb = lambda c: np.stack([(c >> 16) & 255, (c >> 8) & 255, c & 255], 2).astype(np.uint8)
        crops = {'hud': (44, 0, 232, 94), 'mfd': (74, 108, 178, 62)}              # x0, y0, w, h in frame pixels: the glass HUD strip, the two MFD windows side by side
        for name, (x0, y0, w, h) in crops.items():
            pair = [rgb(im[y0 * n:(y0 + h) * n, x0 * n:(x0 + w) * n]) for im in imgs]
            for k, a in zip(('hr', 'classic'), pair):
                Image.fromarray(a).save(os.path.join(out, 'hud_%s_%s_%s.png' % (tag, name, k)))
            if name == 'hud' or tag in ('route', 'scope', 'day'):
                rows.append(('%s: %s' % (tag, 'HUD' if name == 'hud' else 'left and right MFD'), pair[1], pair[0]))
    if not rows:
        return
    cw = max(r[1].shape[1] for r in rows)
    ch = max(r[1].shape[0] for r in rows) + 22
    sheet = Image.new('RGB', (2 * cw, len(rows) * ch), (20, 20, 24))
    for i, (label, c, h) in enumerate(rows):
        for j, (k, a) in enumerate((('classic', c), ('hr', h))):
            ImageDraw.Draw(sheet).text((j * cw + 6, i * ch + 3), '%s  %s' % (label, k), fill=(235, 235, 235))
            sheet.paste(Image.fromarray(a), (j * cw, i * ch + 22))
    sheet.save(os.path.join(out, 'contact_hud.png'))
    print('real-path: %s' % os.path.join(out, 'contact_hud.png'))


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
    tv = [r for r in rows if r['frame'] >= TV_FROM]
    mp = [r for r in rows if r['frame'] >= MAP_FROM]
    bad = 0
    if len(tv) < 20 or len(mp) < 100:
        print('r3d-realpath FAILED: only %d frames' % len(rows))
        return 1
    tmin = min(r['tv'][0] / r['tv'][1] for r in tv)
    mmin = min(r['map'][0] / r['map'][1] for r in mp)
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
    tv2 = [r for r in rows2 if r['frame'] >= TV_FROM]
    tmin2 = min((r['tv'][0] / r['tv'][1] for r in tv2), default=0)
    print('  with the FLIR tint (F6): TV window shown from the 3D layer: min %.1f %%' % (100 * tmin2))
    if tmin2 < TV_MIN:
        print('  the tinted TV window is below %.0f %%' % (100 * TV_MIN))
        bad += 1
    pf = ','.join(str(v) for v in HUD_PNG_FRAMES.values()) if png else ''
    p3, pre3 = run(exe, game, work, 'real_hud', n, HUD_KEYS, pf, frames=HUD_FRAMES)
    rows3 = parse(pre3 + '.txt')
    if len(rows3) < HUD_FRAMES:
        print('r3d-realpath FAILED: only %d frames of the HUD run' % len(rows3))
        return 1
    b3, lines3 = check_hud(rows3)
    print('r3d-realpath HUD run N=%d (%d frames):' % (n, len(rows3)))
    print(chr(10).join(lines3))
    bad += b3
    raw, raw2 = pre + '_%04d.raw' % (SELECT + 60), pre2 + '_%04d.raw' % (SELECT + 60)
    if png and os.path.isfile(raw) and os.path.isfile(raw2):
        os.makedirs(png, exist_ok=True)
        day, flir = write_pngs(raw, png, 'day'), write_pngs(raw2, png, 'flir')
        sheet(png, [('TV camera, airbase (day)', *day['tv']), ('TV camera, airbase (FLIR)', *flir['tv']), ('left MFD map', *day['map'])])
        hud_sheet(png, pre3)
    print('r3d-realpath %s' % ('OK' if not bad else 'FAILED'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
