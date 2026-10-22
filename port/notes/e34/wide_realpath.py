"""S3.4: the wider view in the REAL game path (ctest `r3d_wide_realpath`): a scripted view sweep.

The scripted chain of debrief_chain_sea (START: a new pilot, a strike over Libya -> VGAME at the home airbase) runs headless on the virtual clock with the
enhancements on, `--render-scale 4 --wide` and a pretended window (F117_WINDOW) of 16:9 (1920 x 1080 -> canvas 1920 x 900 with the 4:3 pixel aspect) and of 21:9
(2560 x 1080 -> 2560 x 900); keys scripted into VGAME (F117_KEYS) step through every view: the first cockpit view, the second cockpit view (key c: [294B] = 1),
left, right, rear, the front view without cockpit, chase, delayed, side, target shoulder, target far, weapon, and back to the cockpit. After every page flip
`--mfd-probe` writes how the canvas is composed as presented (enh_canvas.c probe_wide): 3D world / art / vector / black pixels, the same for the area outside the
320 x 200 frame, the view class, whether the wide picture of the display pass is shown, and the seams (differing pixel pairs across the frame border where both
sides show the world, against the same count across block boundaries inside the frame).

Checked per settled view (the frames 4.. after a view change):
  first cockpit view, outside views: the wide picture is shown, >= 99.5 % of the area outside the frame is the 3D world, the frame sits bottom centre, the seam rate
    is not above the inside rate + 3 %;
  second cockpit view (Decisions 2026-10-21 b): the canvas is the frame's height (the host fits it to the window height) and wider, the frame top centre, the area
    beside it the 3D world (the same checks);
  left / right / rear: the canvas is the frame (the host scales it to the window height, black sides), no wide picture.
With --png DIR: DIR/wide_<aspect>_<view>.png (one frame per view as the window shows it: the canvas at the 4:3 pixel aspect, letterboxed into the
window) and DIR/contact_wide.png (16:9 left, 21:9 right).

usage: python wide_realpath.py <f117 executable> <game dir> <work dir> [--png DIR] [--aspect 16x9,21x9]      exit 77 without the game files
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'e32'))
sys.path.insert(0, os.path.join(HERE, '..', 'e24'))
import real_path as RP
import sandbox_hash as SH

N = 4
ASPECTS = {'16x9': '1920x1080', '21x9': '2560x1080'}
VIEW_KEYS = [  # (t in s, XT scan codes (2a = left shift held), label, C07A, [294B], expected class 1 FRONT / 2 FULL / 3 FIT)
    (None, None, 'cockpit', 0x00, 0, 1),
    (67, '2e', 'cockpit2', 0x00, 1, 4),
    (70, '2e', 'cockpit_again', 0x00, 0, 1),
    (73, '2a+32', 'left', 0x42, None, 3),
    (76, '2a+33', 'right', 0x43, None, 3),
    (79, '2a+34', 'rear', 0x41, None, 3),
    (82, '2a+35', 'front_clean', 0x44, None, 2),
    (85, '2a+3b', 'chase', 0x87, None, 2),
    (88, '2a+3c', 'delayed', 0x84, None, 2),
    (91, '2a+3d', 'side', 0x85, None, 2),
    (94, '2a+3f', 'target_shoulder', 0x88, None, 2),
    (97, '2a+40', 'target_far', 0x8B, None, 2),
    (100, '2a+3e', 'weapon', 0x89, None, 2),
    (103, '3b', 'cockpit_end', 0x00, 0, 1),
]
FRAMES = 460
SELECT = 50                                  # frame-loop iterations before the nearest site is selected (the target views need a target)
KEYS = '1:31,2:03,11:39,' + ','.join('%d:%s' % (t, k) for t, k, *_ in VIEW_KEYS if t)
EXT_MIN = 0.995
SEAM_SLACK = 0.03
CLASS = {1: 'front (extended)', 2: 'full (extended)', 3: 'fit (whole, height)', 4: 'side (height, L/R)', 0: 'menu'}


def parse(path):
    rows = []
    for l in open(path):
        f = l.split()
        if len(f) < 3 or f[0] != 'frame':
            continue
        d = {'frame': int(f[1])}
        for t in f:
            if '=' in t:
                k, v = t.split('=', 1)
                d[k] = v
        if 'lw' not in d:
            continue
        for k in ('lw', 'lh', 'n', 'fx', 'fy', 'vclass', 'held', 'world', 'world1', 'art', 'vec', 'black', 'eworld', 'eblack', 'evec', 'epx'):
            d[k] = int(d[k])
        d['view'] = int(d['view'], 16)
        d['style'] = int(d['style'], 16)
        for k in ('seaml', 'seamr', 'seamt', 'seamref'):
            a, b = d[k].split('/')
            d[k] = (int(a), int(b))
        rows.append(d)
    return rows


def segments(rows):
    """[(view, style, [rows])] runs of the same view and style; the first 3 frames of each run are dropped (the canvas follows at the next present)"""
    out = []
    for r in rows:
        key = (r['view'], r['style'] if r['view'] == 0 else None)
        if out and out[-1][0] == key:
            out[-1][1].append(r)
        else:
            out.append((key, [r]))
    return [(k[0], k[1], rr[3:]) for k, rr in out if len(rr) > 6]


def check(rows, aspect):
    bad, lines = 0, []
    segs = segments(rows)
    j = 0
    for t, key, label, view, style, cls in VIEW_KEYS:
        k = j
        while k < len(segs) and not (segs[k][0] == view and (style is None or segs[k][1] == style)):
            k += 1
        if k == len(segs):
            if label == 'weapon':                    # no weapon launched in the sweep: Shift-F4 keeps the previous view
                lines.append('  %-16s (no weapon in flight: not reached)' % label)
                continue
            lines.append('  %-16s NOT REACHED' % label)
            bad += 1
            continue
        cand = segs[k]
        j = k + 1
        rr = cand[2]
        r0 = rr[len(rr) // 2]
        epx = max(1, r0['epx'])
        tot = r0['lw'] * r0['lh']
        frac = lambda k: 100.0 * r0[k] / tot
        sd = sum(r[k][0] for r in rr for k in ('seaml', 'seamr', 'seamt'))
        sp = sum(r[k][1] for r in rr for k in ('seaml', 'seamr', 'seamt'))
        rd = sum(r['seamref'][0] for r in rr)
        rp = sum(r['seamref'][1] for r in rr)
        seam, ref = sd / max(1, sp), rd / max(1, rp)
        msg = '  %-16s view %02X style %d class %-20s frames %3d..%3d: world %5.1f %%, art %5.1f %%, vector %4.1f %%, black %5.1f %% of the canvas; outside the frame: world %5.1f %%, black %5.1f %%' % (
            label, r0['view'], r0['style'], CLASS.get(r0['vclass'], '?'), rr[0]['frame'], rr[-1]['frame'], frac('world') + frac('world1'), frac('art'), frac('vec'), frac('black'),
            100.0 * r0['eworld'] / epx, 100.0 * r0['eblack'] / epx)
        if cls in (1, 2, 4):
            msg += '; seams %d/%d (%.2f %%, inside %.2f %%)' % (sd, sp, 100 * seam, 100 * ref)
        probs = []
        if any(r['vclass'] != cls for r in rr):
            probs.append('class')
        if cls in (1, 2, 4):
            if any(not r['held'] for r in rr):
                probs.append('no wide picture in %d frames' % sum(1 for r in rr if not r['held']))
            if min(r['eworld'] / max(1, r['epx']) for r in rr) < EXT_MIN:
                probs.append('outside world below %.1f %%' % (100 * EXT_MIN))
            if any(r['fy'] != r['lh'] - 200 * r['n'] for r in rr):
                probs.append('frame not at the bottom')
            if cls == 4 and any(r['lh'] != 200 * r['n'] or r['lw'] <= 320 * r['n'] for r in rr):
                probs.append('not height-fitted and extended sideways')
            if sp < 100 or seam > ref + SEAM_SLACK:
                probs.append('seam')
        else:                                          # the canvas is the frame: the host scales it to the window height, black sides
            if any(r['held'] or r['epx'] or r['lw'] != 320 * r['n'] or r['lh'] != 200 * r['n'] for r in rr):
                probs.append('not the whole frame filling the height')
        if probs:
            bad += 1
            msg += '  BAD: ' + ', '.join(probs)
        lines.append(msg)
    return bad, lines


NIGHT_KEYS = '1:31,2:03,11:39,63:2a+0d,64:0b,64.5:0b,90:50p,93:50r'   # S3.4 fix: full throttle, brakes off, take off: the speed (stall-margin bar) and the VSI change
NIGHT_FRAMES = 160


def night_run(exe, game, work):
    """the night take-off (F117_TEST_NIGHT: the night flag, 7898's bars in colour 0x2A): every frame the bars beside the airspeed tape (x 72) and the altitude
    tape (x 247) must be shown on exactly the rows the 1x page has them (probe `bars`), and no hr HUD line may stay under a later 1x write (`vstale`)"""
    os.environ['F117_WINDOW'] = ASPECTS['16x9']
    os.environ['SEL'] = str(SELECT)
    os.environ['F117_TEST_NIGHT'] = '1'
    try:
        p, pre = RP.run(exe, game, work, 'wide_night', N, NIGHT_KEYS, '', frames=NIGHT_FRAMES, extra='--wide')
    finally:
        os.environ.pop('F117_TEST_NIGHT', None)
    mis = px = stale = n = 0
    spd = []
    for l in open(pre + '.txt'):
        m = re.search(r' bars=(\d+)/(\d+)', l)
        v = re.search(r' vstale=(\d+)', l)
        sp = re.search(r' spd=(-?\d+)', l)
        if not m:
            continue
        n += 1
        mis += int(m.group(1)); px += int(m.group(2)); stale = max(stale, int(v.group(1)) if v else 0)
        if sp:
            spd.append(int(sp.group(1)))
    ok = n >= NIGHT_FRAMES - 10 and px > 500 and mis == 0 and stale == 0 and spd and max(spd) > 100
    print('wide-realpath night take-off (16:9): %d frames, speed %d..%d; the 7898 bars beside the tapes: %d bar rows shown, %d differ from the 1x page; stale hr HUD pixels max %d%s' % (
        n, min(spd or [0]), max(spd or [0]), px, mis, stale, '' if ok else '  BAD'))
    return 0 if ok else 1


CVIEW_KEYS = NIGHT_KEYS.split(',90:')[0] + ',' + ','.join('%.1f:%s' % (66 + 0.7 * i, k) for i, k in enumerate(
    ['2e', '2e', '2e', '2a+32', '2e', '3b', '2e', '2a+3b', '3b', '2e', '2a+35', '3b', '2e', '2e', '2e', '2a+34', '2e', '3b', '2e', '2e']))


def cview_run(exe, game, work):
    """S3.4 fix (the crash on key c): in flight, the second cockpit view toggled and cycled with the other views (each switch resizes the canvas), with the host
    pumped inside the open main pass (F117_TEST_PUMP: as the real-time [262A] wait of r3d_begin_scene does); the run must end normally, the layers must never be
    reallocated inside a 3D pass, and the second cockpit view must show its wide picture"""
    os.environ['F117_WINDOW'] = ASPECTS['21x9']
    os.environ['SEL'] = str(SELECT)
    os.environ['F117_TEST_PUMP'] = '1'
    try:
        p, pre = RP.run(exe, game, work, 'wide_cview', N, CVIEW_KEYS, '', frames=200, extra='--wide')
    finally:
        os.environ.pop('F117_TEST_PUMP', None)
    rows = parse(pre + '.txt')
    side = sum(1 for r in rows if r['vclass'] == 4 and r['held'])
    switches = sum(1 for a, b in zip(rows, rows[1:]) if (a['vclass'], a['view']) != (b['vclass'], b['view']))
    ended = 'quit: START returned 0' in p.stdout
    inpass = p.stderr.count('reallocated inside a 3D pass')
    ok = ended and p.returncode == 0 and inpass == 0 and side >= 20 and switches >= 15
    print('wide-realpath c-view cycling (21:9, host pumped inside the pass): %d frames, %d view switches, %d second-cockpit frames with the wide picture, '
          'layers reallocated inside a pass %d, run ended %s%s' % (len(rows), switches, side, inpass, 'normally' if ended else 'ABNORMALLY (rc %d)' % p.returncode, '' if ok else '  BAD'))
    return 0 if ok else 1


def run(exe, game, work, aspect, frames_png='', extra='--wide', tag=None):
    os.environ['F117_WINDOW'] = ASPECTS[aspect]
    os.environ['SEL'] = str(SELECT)
    tag = tag or 'wide_' + aspect
    os.environ['F117_PRESENT_LOG'] = os.path.join(work, tag + '_present.txt')       # S3.4 fix 4: every presented picture's black share
    try:
        p, pre = RP.run(exe, game, work, tag, N, KEYS, frames_png, frames=FRAMES, extra=extra)
    finally:
        os.environ.pop('F117_PRESENT_LOG', None)
    return p, pre


def dark_ms(path):
    """per view switch of the sweep: how long (ms) the presented picture is darker (share of pure black pixels) than both the picture shown before the
    switch and the one shown 2 s after it, by more than 5 points (the black flash of a view change)"""
    rows = [tuple(float(v) for v in l.split()) for l in open(path)]
    def shown(t):
        v = rows[0]
        for r in rows:
            if r[0] > t:
                break
            v = r
        return v
    out = {}
    for t, k, label, *_ in VIEW_KEYS:
        if t is None:
            continue
        thr = max(shown(t - 0.01)[3], shown(t + 2.0)[3]) + 0.05
        seg = [r for r in rows if t <= r[0] < t + 2.0]
        out[label] = sum(((seg[i + 1][0] if i + 1 < len(seg) else t + 2.0) - r[0]) * 1000 for i, r in enumerate(seg) if r[3] > thr)
    return out


def flash_check(exe, game, work):
    """S3.4 fix 4: the black flash per view switch, enhanced (16:9, wide) against --classic (the original blanks the screen for one or two presents on most
    switches: view_frame's screen_off / screen_on): the enhanced build may not be dark longer than classic (2 ms slack)"""
    e = dark_ms(os.path.join(work, 'wide_16x9_present.txt'))
    run(exe, game, work, '16x9', extra='--classic', tag='classic_16x9')
    c = dark_ms(os.path.join(work, 'classic_16x9_present.txt'))
    worse = [k for k in e if e[k] > c.get(k, 0) + 2]
    print('wide-realpath view-switch black flash (ms dark, enhanced / classic): %s%s' % (', '.join('%s %.0f/%.0f' % (k, e[k], c.get(k, 0)) for k in e),
                                                                                 '' if not worse else '  BAD: longer than classic at ' + ', '.join(worse)))
    return 1 if worse else 0


def png_frames():
    """one frame per view, in the middle of its 3 s (the key at t s reaches the frame (t - 61) * 10)"""
    out = {}
    for t, key, label, *_ in VIEW_KEYS:
        out[label] = 40 if t is None else (t - 61) * 10 + 18
    return out


def present(rgb, aspect):
    """the canvas as the window shows it: the host stretches it to the 4:3 pixel aspect (x 1.2 vertically) and letterboxes it into the window (nearest)"""
    import numpy as np
    from PIL import Image
    ww, wh = (int(v) for v in ASPECTS[aspect].split('x'))
    ch, cw = rgb.shape[0] * 1.2, rgb.shape[1]
    k = min(ww / cw, wh / ch)
    w, h = max(1, round(cw * k)), max(1, round(ch * k))
    img = Image.fromarray(rgb).resize((w, h), Image.NEAREST)
    win = Image.new('RGB', (ww, wh), (0, 0, 0))
    win.paste(img, ((ww - w) // 2, (wh - h) // 2))
    return np.asarray(win)


def write_pngs(pre, out, aspect, rows):
    import numpy as np
    from PIL import Image
    imgs = {}
    byf = {r['frame']: r for r in rows}
    for label, fr in png_frames().items():
        raw = pre + '_%04d.raw' % fr
        if not os.path.isfile(raw):
            continue
        b = open(raw, 'rb').read()
        lw, lh = (int(v) for v in np.frombuffer(b, '<i4', 2))
        im = np.frombuffer(b, '<u4', lw * lh, 8).reshape(lh, lw)
        rgb = np.stack([(im >> 16) & 255, (im >> 8) & 255, im & 255], 2).astype(np.uint8)
        rgb = present(rgb, aspect)
        Image.fromarray(rgb).save(os.path.join(out, 'wide_%s_%s.png' % (aspect, label)))
        r = byf.get(fr)
        imgs[label] = (rgb, '%s  view %02X%s' % (label, r['view'], ' style %d' % r['style'] if r and r['view'] == 0 else '') if r else label)
    return imgs


def contact(out, per_aspect):
    from PIL import Image, ImageDraw
    labels = [v[2] for v in VIEW_KEYS]
    z = 4
    cols = [a for a in ASPECTS if a in per_aspect]
    cw = {a: max((im.shape[1] for im, _ in per_aspect[a].values()), default=0) // z for a in cols}
    rh = max((im.shape[0] for a in cols for im, _ in per_aspect[a].values()), default=0) // z + 20
    W = sum(cw.values()) + 8 * len(cols)
    sheet = Image.new('RGB', (W, rh * len(labels)), (40, 40, 48))
    for i, lab in enumerate(labels):
        x = 0
        for a in cols:
            if lab in per_aspect[a]:
                im, cap = per_aspect[a][lab]
                small = Image.fromarray(im).resize((im.shape[1] // z, im.shape[0] // z), Image.BOX)
                sheet.paste(small, (x, i * rh + 20))
                ImageDraw.Draw(sheet).text((x + 4, i * rh + 4), '%s  %s' % (a.replace('x', ':'), cap), fill=(235, 235, 235))
            x += cw[a] + 8
    path = os.path.join(out, 'contact_wide.png')
    sheet.save(path)
    print('wide-realpath: %s' % path)


def main():
    argv = sys.argv[1:]
    png = None
    if '--png' in argv:
        i = argv.index('--png'); png = argv[i + 1]; del argv[i:i + 2]
    aspects = list(ASPECTS)
    if '--aspect' in argv:
        i = argv.index('--aspect'); aspects = argv[i + 1].split(','); del argv[i:i + 2]
    exe, game, work = (os.path.abspath(a) for a in argv[:3])
    if not os.path.isfile(os.path.join(game, 'END.EXE')) or not os.path.isfile(os.path.join(SH.ROOT, 'work', 'unpacked', 'START.EXE')):
        print('no game files')
        return 77
    os.makedirs(work, exist_ok=True)
    bad = 0
    per = {}
    for a in aspects:
        pf = ','.join(str(v) for v in png_frames().values()) if png else ''
        p, pre = run(exe, game, work, a, pf)
        if not re.search(r'EXEC VGAME\.EXE', p.stdout) or not os.path.isfile(pre + '.txt'):
            print('wide-realpath FAILED (%s): VGAME did not run' % a, p.stdout[-1200:], p.stderr[-600:])
            return 1
        m = re.search(r'sandbox: (\d+) passes.*violations (\d+)', p.stdout)
        rows = parse(pre + '.txt')
        b, lines = check(rows, a)
        print('wide-realpath %s (window %s, N=%d, canvas %dx%d): %d VGAME frames, %s display passes, %s forbidden calls in a pass' % (
            a, ASPECTS[a], N, rows[0]['lw'] if rows else 0, rows[0]['lh'] if rows else 0, len(rows), m.group(1) if m else '0', m.group(2) if m else '?'))
        print('\n'.join(lines))
        if not m or int(m.group(2)) != 0 or len(rows) < FRAMES - 10:
            print('  passes / violations / frames wrong')
            b += 1
        bad += b
        if png:
            os.makedirs(png, exist_ok=True)
            per[a] = write_pngs(pre, png, a, rows)
        for f in os.listdir(os.path.join(work, 'wide_' + a)):
            if f.endswith('.raw'):
                os.remove(os.path.join(work, 'wide_' + a, f))
    if '16x9' in aspects:
        bad += flash_check(exe, game, work)
    bad += night_run(exe, game, work)
    bad += cview_run(exe, game, work)
    if png:
        contact(png, per)
    print('wide-realpath %s' % ('OK' if not bad else 'FAILED'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
