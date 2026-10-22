"""S3.6: true night in the REAL game path (ctest `r3d_hr_night_realpath`).

1. The sim is untouched: the scripted chain of debrief_chain_sea (port/notes/e24/sandbox_hash.py: START -> VGAME flight, the dive into the sea -> END -> START)
   on the virtual clock with a wide canvas (window 1280 x 720, --wide), without and with --true-night, by day, at night (F117_TEST_NIGHT=2: the night flag and
   palette B from the first frame) and at night with --smooth-shading on both sides: the per-frame hash of the sim state (all but the canvas), the final comm
   block and ROSTER.FIL identical, no forbidden call in a display pass.
2. The view sweep of port/notes/e34/wide_realpath.py (16:9 window, N = 4, --wide) at night without / with the option (and with --smooth-shading without / with
   it) and by day without / with it: the runs end normally with the same number of frames and display passes with no forbidden call; per dumped frame (one per
   view) the share of canvas pixels the option changes and their brightness against option off; no pixel gets brighter than option off with --smooth-shading (true night blends what it darkens); by day no pixel changes.
With --png DIR: DIR/night_<view>_<off|on|son>.png (son = with --smooth-shading) and DIR/contact_night.png (rows: views, columns: off, on, on + smooth).

usage: python night_realpath.py <f117 executable> <game dir> <work dir> [--png DIR]      exit 77 without the game files
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'e34'))
sys.path.insert(0, os.path.join(HERE, '..', 'e32'))
sys.path.insert(0, os.path.join(HERE, '..', 'e24'))
import sandbox_hash as SH
import wide_realpath as WR

VIEWS = ('cockpit', 'cockpit2', 'rear', 'front_clean', 'chase', 'side', 'target_far')


def strip(l):
    return re.sub(r' canvas=\w+', '', l)


def hash_pair(exe, game, work, tag, night, base):
    if night:
        os.environ['F117_TEST_NIGHT'] = '2'
    try:
        a = SH.run(exe, game, work, 'hash_%s_off' % tag, ('--wide ' + base).strip(), window='1280x720')
        b = SH.run(exe, game, work, 'hash_%s_on' % tag, ('--wide --true-night ' + base).strip(), window='1280x720')
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
    same_end = fin(a['final']) == fin(b['final']) and a['roster'] == b['roster']
    if not same_end:
        bad = True
    m = re.search(r'passes=(\d+) violations=(\d+)', b['final'])
    passes, viol = (int(m.group(1)), int(m.group(2))) if m else (0, -1)
    if viol != 0 or passes < len(fb) // 2:
        bad = True
    canv = sum(1 for la, lb in zip(fa, fb) if la != lb)
    print('r3d-night-realpath hash (%s, 1280 x 720 wide): %d VGAME frames, sim state identical in %s, final comm block and roster %s; %d wide passes, '
          '%d forbidden calls; canvas hash differs in %d frames%s' % (tag, len(fb), 'all' if first is None else 'all before frame %d' % first,
                                                                     'identical' if same_end else 'DIFFERENT', passes, viol, canv, '' if not bad else '  BAD'))
    if not night and canv:
        print('  the canvas changed by day')
        bad = True
    return 1 if bad else 0


def load_raw(path):
    import numpy as np
    b = open(path, 'rb').read()
    lw, lh = (int(v) for v in np.frombuffer(b, '<i4', 2))
    return np.frombuffer(b, '<u4', lw * lh, 8).reshape(lh, lw)


def lum(c):
    return 0.299 * ((c >> 16) & 255) + 0.587 * ((c >> 8) & 255) + 0.114 * (c & 255)


def sweep_runs(exe, game, work, night, opts):
    tag = 'night' if night else 'day'
    frames = WR.png_frames()
    pf = ','.join(str(frames[v]) for v in VIEWS)
    if night:
        os.environ['F117_TEST_NIGHT'] = '2'
    out = {}
    try:
        for opt, extra in opts:
            p, pre = WR.run(exe, game, work, '16x9', pf, extra=extra, tag='sweep_%s_%s' % (tag, opt))
            rows = WR.parse(pre + '.txt') if os.path.isfile(pre + '.txt') else []
            m = re.search(r'sandbox: (\d+) passes.*violations (\d+)', p.stdout)
            ok = p.returncode == 0 and 'quit: START returned 0' in p.stdout and len(rows) >= WR.FRAMES - 10 and m and int(m.group(2)) == 0
            out[opt] = (ok, pre, len(rows), m.group(1) if m else '0')
    finally:
        os.environ.pop('F117_TEST_NIGHT', None)
    return out


def pair(out, a, b, name, day, ref=None):
    import numpy as np
    frames = WR.png_frames()
    bad = 0 if out[a][0] and out[b][0] and out[a][2] == out[b][2] else 1
    parts, imgs = [], {}
    for v in VIEWS:
        fa, fb = '%s_%04d.raw' % (out[a][1], frames[v]), '%s_%04d.raw' % (out[b][1], frames[v])
        if not (os.path.isfile(fa) and os.path.isfile(fb)):
            parts.append('%s missing' % v)
            bad += 1
            continue
        x, y = load_raw(fa), load_raw(fb)
        xr = load_raw('%s_%04d.raw' % (out[ref][1], frames[v])) if ref else x   # a night pass blends its colours (S3.5): no brighter than the blended picture
        d = x != y
        brighter = 0
        for sh in (0, 8, 16):
            brighter += int((((y >> sh) & 255) > ((xr >> sh) & 255)).sum())
        la, lb = float(lum(x).sum()), float(lum(y).sum())
        parts.append('%s %.0f %% changed, %.0f %% bright%s' % (v, 100 * float(d.mean()), 100 * lb / max(1.0, la), ', %d brighter' % brighter if brighter else ''))
        if brighter or (day and d.any()) or (not day and v in ('front_clean', 'chase') and lb > 0.75 * la):
            bad += 1
        imgs[v] = (x, y)
    print('r3d-night-realpath view sweep (%s, 16:9, N = %d): %s / %s frames, %s / %s display passes, runs end normally %s; %s%s' % (
        name, WR.N, out[a][2], out[b][2], out[a][3], out[b][3], 'yes' if out[a][0] and out[b][0] else 'NO', ', '.join(parts), '' if not bad else '  BAD'))
    return bad, imgs


def cleanup(out):
    for o in out.values():
        d = os.path.dirname(o[1])
        for f in os.listdir(d):
            if f.endswith('.raw'):
                os.remove(os.path.join(d, f))


def to_rgb(im):
    import numpy as np
    rgb = np.stack([(im >> 16) & 255, (im >> 8) & 255, im & 255], 2).astype(np.uint8)
    return WR.present(rgb, '16x9')


def write_pngs(png, per):
    from PIL import Image, ImageDraw
    os.makedirs(png, exist_ok=True)
    rows = [v for v in VIEWS + ('banked',) if v in per]
    for v in rows:
        for opt, im in per[v].items():
            Image.fromarray(to_rgb(im)).save(os.path.join(png, 'night_%s_%s.png' % (v, opt)))
    if not rows:
        return
    z = 3
    w, h = 1920 // z, 1080 // z
    cols = ('off', 'on', 'son')
    cap = {'off': 'night (original)', 'on': 'true night', 'son': 'true night + smooth shading'}
    sheet = Image.new('RGB', (len(cols) * (w + 8), (h + 20) * len(rows)), (40, 40, 48))
    for i, v in enumerate(rows):
        for j, opt in enumerate(cols):
            if opt not in per[v]:
                continue
            small = Image.fromarray(to_rgb(per[v][opt])).resize((w, h), Image.BOX)
            sheet.paste(small, (j * (w + 8), i * (h + 20) + 20))
            ImageDraw.Draw(sheet).text((j * (w + 8) + 4, i * (h + 20) + 4), '%s  %s' % (v, cap[opt]), fill=(235, 235, 235))
    path = os.path.join(png, 'contact_night.png')
    sheet.save(path)
    print('r3d-night-realpath: %s' % path)


POSE_WINDOWS = (('1167x821', 0), ('1459x1026', 4), ('1920x1080', 4))   # fix 2: non-integer fits (N = 3 auto; N = 4 wider than the window fits) and 16:9
POSE_FRAMES = ','.join(str(f) for f in range(24, 124, 8))


def load_probe(path):
    """the probe dump: the presented canvas, its classes (0 black, 1 art, 2 3D layer, 3 vector, 4 3D 1x) and per pixel the night value it got (test aid values:
    1 = a 3D record the page no longer matches, 2 = a page value under a vector record, 3 = a band colour shown from the page)"""
    import numpy as np
    b = open(path, 'rb').read()
    lw, lh = (int(v) for v in np.frombuffer(b, '<i4', 2)); px = lw * lh
    return (np.frombuffer(b, '<u4', px, 8).reshape(lh, lw), np.frombuffer(b, 'u1', px, 8 + 8 * px).reshape(lh, lw),
            np.frombuffer(b, 'u1', px, 8 + 9 * px).reshape(lh, lw))


def pose_run(exe, game, work, win, n, extra, tag):
    """fix 2: level, banked flight at 3000 ft over the sea (F117_TEST_POSE) at night, the first cockpit view, probe frames with classes and night values"""
    import subprocess
    d = os.path.join(work, 'pose_%s_%d_%s' % (win, n, tag))
    g = SH.fresh_game(game, os.path.join(d, 'game'))
    pre = os.path.join(d, 'probe').replace(os.sep, '/')
    env = dict(os.environ, F117_KEYS='1:31,2:03,11:39', F117_SHELL_SCRIPT=os.path.join(SH.S54, 'shell_chain_libya.txt'), F117_VGAME_FRAMES='160',
               F117_END_SCRIPT=os.path.join(SH.S58, 'end_chain.txt'), F117_VCLOCK='1', F117_WINDOW=win, F117_TEST_NIGHT='2', F117_PROBE_FRAMES=POSE_FRAMES,
               F117_TEST_POSE='20,0xB1C7', F117_ENH_ARGS='--render-scale %d --mfd-probe %s %s' % (n, pre, extra))
    env.pop('F117_CLASSIC', None)
    p = subprocess.run([exe, '--headless', '--trace', '--game-dir', g], env=env, capture_output=True, text=True, timeout=900)
    return p.returncode == 0 and 'quit: START returned 0' in p.stdout, pre


def pose_check(exe, game, work, png):
    """fix 2 (U1: dots and a dotted column in the black sky at the frame's right edge): per probe frame of the banked night flight, the 3D pixels of the night
    window shown without a factor, per source (3D layer / 1x 3D pixel / band colour from the page / under a vector record), the same in the columns at the
    frame's left and right edges (+-2N) and the rows at its top edge, and isolated bright 3D pixels (a 1x block brighter than the pixels N + 1 away)"""
    import numpy as np
    bad = 0
    imgs = {}
    for win, n in POSE_WINDOWS:
        runs = (('on', '--wide --true-night'), ('son', '--wide --true-night --smooth-shading'))
        if png and win == POSE_WINDOWS[0][0]:
            runs = (('off', '--wide'),) + runs                          # the picture of the original night for the contact sheet
        for tag, extra in runs:
            ok, pre = pose_run(exe, game, work, win, n, extra, tag)
            frames = sorted(f for f in os.listdir(os.path.dirname(pre)) if f.startswith('probe_') and f.endswith('.raw'))
            tot = dict(frames=len(frames), world=0, unset=0, edge=0, edge_unset=0, page_band=0, dots=0)
            for f in frames:
                img, cls, nd = load_probe(os.path.join(os.path.dirname(pre), f))
                lh, lw = img.shape
                N = min(lw // 320, lh // 200) if n == 0 else n
                fx, fy = (lw - 320 * N) // 2, lh - 200 * N
                win3 = np.zeros(img.shape, bool)
                win3[:fy + 90 * N, :] = True                             # the 3D window rows of the first cockpit view (and the area above the frame)
                world = win3 & ((cls == 2) | (cls == 4))
                unset = world & (nd == 0)
                edge = np.zeros(img.shape, bool)
                for x0 in (fx, fx + 320 * N - 1):
                    edge[:, max(0, x0 - 2 * N):min(lw, x0 + 2 * N + 1)] = True
                edge[max(0, fy - 2 * N):fy + 2 * N + 1, :] = True
                edge &= win3
                L = 0.299 * ((img >> 16) & 255) + 0.587 * ((img >> 8) & 255) + 0.114 * (img & 255)
                nb = np.full(L.shape, -1.0)
                k = N + 1
                for dy, dx in ((k, 0), (-k, 0), (0, k), (0, -k)):
                    nb = np.maximum(nb, np.roll(np.roll(L, dy, 0), dx, 1))
                dot = world & (L > nb + 3) & (nd != 255)
                dot[:k] = dot[-k:] = False; dot[:, :k] = dot[:, -k:] = False
                tot['world'] += int(world.sum()); tot['unset'] += int(unset.sum()); tot['edge'] += int((edge & world).sum())
                tot['edge_unset'] += int((edge & unset).sum()); tot['page_band'] += int((win3 & (nd == 3)).sum()); tot['dots'] += int(dot.sum())
                if win == POSE_WINDOWS[0][0] and f == frames[len(frames) // 2]:
                    imgs[tag] = img
            if tag == 'off':
                continue
            b = 0 if ok and tot['frames'] >= 10 and tot['world'] > 0 and not tot['unset'] and not tot['edge_unset'] and not tot['dots'] else 1
            print('r3d-night-realpath banked night flight (%s, N = %s, %s): %d frames, run ends normally %s; night-window 3D pixels %d, without a factor %d; at the '
                  'frame edges %d, without a factor %d; band colours shown from the page (darkened) %d; isolated bright 3D pixels %d%s' % (
                      win, n or 'auto', 'smooth' if tag == 'son' else 'no smooth', tot['frames'], 'yes' if ok else 'NO', tot['world'], tot['unset'], tot['edge'],
                      tot['edge_unset'], tot['page_band'], tot['dots'], '' if not b else '  BAD'))
            bad += b
            for f in frames:
                os.remove(os.path.join(os.path.dirname(pre), f))
    return bad, imgs


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
    bad += hash_pair(exe, game, work, 'day', False, '')
    bad += hash_pair(exe, game, work, 'night', True, '')
    bad += hash_pair(exe, game, work, 'night_smooth', True, '--smooth-shading')
    night = sweep_runs(exe, game, work, True, (('off', '--wide'), ('on', '--wide --true-night'), ('soff', '--wide --smooth-shading'),
                                                ('son', '--wide --smooth-shading --true-night')))
    b1, i1 = pair(night, 'off', 'on', 'night', False, 'soff')
    b2, i2 = pair(night, 'soff', 'son', 'night, smooth shading', False)
    bad += b1 + b2
    day = sweep_runs(exe, game, work, False, (('off', '--wide'), ('on', '--wide --true-night')))
    b3, _ = pair(day, 'off', 'on', 'day', True)
    bad += b3
    b4, i4 = pose_check(exe, game, work, png)
    bad += b4
    if png:
        per = {}
        for v in VIEWS:
            if v in i1:
                per[v] = {'off': i1[v][0], 'on': i1[v][1]}
                if v in i2:
                    per[v]['son'] = i2[v][1]
        if i4:
            per['banked'] = dict(i4)
        write_pngs(png, per)
    cleanup(night)
    cleanup(day)
    print('r3d-night-realpath %s' % ('OK' if not bad else 'FAILED'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
