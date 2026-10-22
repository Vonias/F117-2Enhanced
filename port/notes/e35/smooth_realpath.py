"""S3.5: smooth distance shading in the REAL game path (ctest `r3d_hr_smooth_realpath`).

1. The sim is untouched: the scripted chain of debrief_chain_sea (port/notes/e24/sandbox_hash.py: START -> VGAME flight, the dive into the sea -> END -> START)
   on the virtual clock with a wide canvas (window 1280 x 720, --wide), once without and once with --smooth-shading, by day and with F117_TEST_NIGHT=1: the
   per-frame hash of the sim state (all but the canvas), the final comm block and ROSTER.FIL identical, no forbidden call in a display pass.
2. The view sweep of port/notes/e34/wide_realpath.py (16:9 window, N = 4, --wide) without and with the option, by day and night: the run ends normally, the same
   number of frames, the wide display passes run with no forbidden call; per dumped frame the share of canvas pixels that differ between the two (the shaded colours
   only: small) is printed.
With --png DIR: DIR/smooth_<day|night>_<view>_<off|on>.png and DIR/contact_smooth.png (rows: view and time, columns: option off, on).

usage: python smooth_realpath.py <f117 executable> <game dir> <work dir> [--png DIR]      exit 77 without the game files
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'e34'))
sys.path.insert(0, os.path.join(HERE, '..', 'e32'))
sys.path.insert(0, os.path.join(HERE, '..', 'e24'))
import sandbox_hash as SH
import wide_realpath as WR

VIEWS = ('cockpit', 'front_clean', 'chase', 'side', 'target_far')
DIFF_MAX = 0.6           # at most 60 % of a frame's canvas pixels may differ (the sky and the ground are most of it; HUD, panel and art never)


def strip(l):
    return re.sub(r' canvas=\w+', '', l)


def hash_pair(exe, game, work, night):
    tag = 'night' if night else 'day'
    if night:
        os.environ['F117_TEST_NIGHT'] = '1'
    try:
        a = SH.run(exe, game, work, 'hash_%s_off' % tag, '--wide', window='1280x720')
        b = SH.run(exe, game, work, 'hash_%s_on' % tag, '--wide --smooth-shading', window='1280x720')
    finally:
        os.environ.pop('F117_TEST_NIGHT', None)
    if a is None or b is None:
        return 1
    fa, fb = a['frames'], b['frames']
    bad = len(fa) < 20 or len(fa) != len(fb)
    first = None
    for i, (la, lb) in enumerate(zip(fa, fb)):
        if strip(la) != strip(lb):
            first = i
            bad = True
            break
    fin = lambda f: re.sub(r' passes=\d+ violations=\d+', '', f)
    if fin(a['final']) != fin(b['final']) or a['roster'] != b['roster']:
        bad = True
    m = re.search(r'passes=(\d+) violations=(\d+)', b['final'])
    passes, viol = (int(m.group(1)), int(m.group(2))) if m else (0, -1)
    if viol != 0 or passes < len(fb) // 2:
        bad = True
    canv = sum(1 for la, lb in zip(fa, fb) if la != lb)
    print('r3d-smooth-realpath hash (%s, 1280 x 720 wide): %d VGAME frames, sim state identical in %s, final comm block and roster %s; %d wide passes, %d forbidden calls; '
          'canvas hash differs in %d frames%s' % (tag, len(fb), 'all' if first is None else 'all before frame %d' % first,
                                                  'identical' if fin(a['final']) == fin(b['final']) and a['roster'] == b['roster'] else 'DIFFERENT', passes, viol, canv,
                                                  '' if not bad else '  BAD'))
    return 1 if bad else 0


def load_raw(path):
    import numpy as np
    b = open(path, 'rb').read()
    lw, lh = (int(v) for v in np.frombuffer(b, '<i4', 2))
    return np.frombuffer(b, '<u4', lw * lh, 8).reshape(lh, lw)


def sweep(exe, game, work, night, png):
    tag = 'night' if night else 'day'
    frames = WR.png_frames()
    pf = ','.join(str(frames[v]) for v in VIEWS)
    if night:
        os.environ['F117_TEST_NIGHT'] = '1'
    out = {}
    try:
        for opt, extra in (('off', '--wide'), ('on', '--wide --smooth-shading')):
            p, pre = WR.run(exe, game, work, '16x9', pf, extra=extra, tag='sweep_%s_%s' % (tag, opt))
            rows = WR.parse(pre + '.txt') if os.path.isfile(pre + '.txt') else []
            m = re.search(r'sandbox: (\d+) passes.*violations (\d+)', p.stdout)
            ok = p.returncode == 0 and 'quit: START returned 0' in p.stdout and len(rows) >= WR.FRAMES - 10 and m and int(m.group(2)) == 0
            out[opt] = (ok, pre, len(rows), m.group(1) if m else '0')
    finally:
        os.environ.pop('F117_TEST_NIGHT', None)
    bad = 0 if out['off'][0] and out['on'][0] and out['off'][2] == out['on'][2] else 1
    parts, imgs = [], {}
    import numpy as np
    for v in VIEWS:
        fa, fb = '%s_%04d.raw' % (out['off'][1], frames[v]), '%s_%04d.raw' % (out['on'][1], frames[v])
        if not (os.path.isfile(fa) and os.path.isfile(fb)):
            parts.append('%s missing' % v)
            bad += 1
            continue
        a, b = load_raw(fa), load_raw(fb)
        d = a != b
        share = float(d.mean())
        mx = 0
        if d.any():
            for sh in (0, 8, 16):
                mx = max(mx, int(np.abs(((a >> sh) & 255).astype(int) - ((b >> sh) & 255).astype(int))[d].max()))
        parts.append('%s %.1f %% (max %d)' % (v, 100 * share, mx))
        if share > DIFF_MAX:
            bad += 1
        imgs[v] = (a, b)
    print('r3d-smooth-realpath view sweep (%s, 16:9, N = %d): %s / %s frames, %s / %s display passes, run ends normally %s; canvas pixels changed by the option: %s%s' % (
        tag, WR.N, out['off'][2], out['on'][2], out['off'][3], out['on'][3], 'yes' if out['off'][0] and out['on'][0] else 'NO', ', '.join(parts), '' if not bad else '  BAD'))
    if png:
        write_pngs(png, tag, imgs)
    for opt in ('off', 'on'):
        d = os.path.dirname(out[opt][1])
        for f in os.listdir(d):
            if f.endswith('.raw'):
                os.remove(os.path.join(d, f))
    return bad, imgs


def to_rgb(im):
    import numpy as np
    rgb = np.stack([(im >> 16) & 255, (im >> 8) & 255, im & 255], 2).astype(np.uint8)
    return WR.present(rgb, '16x9')


def write_pngs(png, tag, imgs):
    from PIL import Image
    os.makedirs(png, exist_ok=True)
    for v, (a, b) in imgs.items():
        for opt, im in (('off', a), ('on', b)):
            Image.fromarray(to_rgb(im)).save(os.path.join(png, 'smooth_%s_%s_%s.png' % (tag, v, opt)))


def contact(png, per):
    from PIL import Image, ImageDraw
    rows = [(tag, v) for tag in ('day', 'night') for v in VIEWS if v in per.get(tag, {})]
    if not rows:
        return
    z = 3
    w, h = 1920 // z, 1080 // z
    sheet = Image.new('RGB', (2 * w + 8, (h + 20) * len(rows)), (40, 40, 48))
    for i, (tag, v) in enumerate(rows):
        a, b = per[tag][v]
        for j, (opt, im) in enumerate((('off', a), ('on', b))):
            small = Image.fromarray(to_rgb(im)).resize((w, h), Image.BOX)
            sheet.paste(small, (j * (w + 8), i * (h + 20) + 20))
            ImageDraw.Draw(sheet).text((j * (w + 8) + 4, i * (h + 20) + 4), '%s  %s  smooth shading %s' % (tag, v, opt), fill=(235, 235, 235))
    path = os.path.join(png, 'contact_smooth.png')
    sheet.save(path)
    print('r3d-smooth-realpath: %s' % path)


def main():
    argv = sys.argv[1:]
    png = None
    if '--png' in argv:
        i = argv.index('--png'); png = os.path.abspath(argv[i + 1]); del argv[i:i + 2]
    if len(argv) < 3:
        print(__doc__)
        return 2
    exe, game, work = (os.path.abspath(a) for a in argv[:3])
    if not os.path.isfile(os.path.join(game, 'END.EXE')) or not os.path.isfile(os.path.join(SH.ROOT, 'work', 'unpacked', 'START.EXE')):
        print('no game files')
        return 77
    os.makedirs(work, exist_ok=True)
    bad = 0
    for night in (False, True):
        bad += hash_pair(exe, game, work, night)
    per = {}
    for night in (False, True):
        b, imgs = sweep(exe, game, work, night, png)
        bad += b
        per['night' if night else 'day'] = imgs
    if png:
        contact(png, per)
    print('r3d-smooth-realpath %s' % ('OK' if not bad else 'FAILED'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
