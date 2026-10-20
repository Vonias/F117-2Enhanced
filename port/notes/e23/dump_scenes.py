"""S2.3: headless PNG and mask dumps of real render3d scenes for the user (DESIGN 5.3; the user looks, nobody else does).

  python dump_scenes.py <f117-enhanced.exe> <Game dir> <golden work dir> [OUT DIR] [--n 4] [--scenes a,b,c]

Needs the golden test's work directory (scn.txt and the snapshot s1.* from a ctest / r3d_golden.py run). Runs the chosen scenes three times
through `f117 --render-test` with `--hr-dump` at --render-scale N (default 4): once per dither mode; the classic 1x page is the same in every run.
Writes into OUT (default work/e23):
  <scene>_classic.png   the faithful 320x200 page scaled N x N nearest
  <scene>_grid.png      the hr frame with --dither grid (the default: the dither keeps the original's pixel size)
  <scene>_fine.png      the hr frame with --dither fine  (a checkerboard of layer pixels)
  <scene>_mask.png      the shadow plane: grey = original page pixel, green = hr 3D layer pixel, magenta = stamped 1x pixel (sprites, dots),
                        red = 3D write whose layer block is not written (should not exist)
  contact_dither.png    classic | grid | fine side by side for the scenes with two-colour / stipple faces, each with a zoomed crop of the
                        place where grid and fine differ most (the question of DESIGN Q2)
  contact_scenes.png    the same for the other scenes (runway, low terrain, targets close up, a cockpit frame, TV and map windows)
The render test draws only the 3D passes (and, for view_frame scenes, the cockpit art of the page); the HUD lines are not part of it.

S3.1: `--views` dumps the whole composed frame of the view_* scenes (every view mode: sprites, dots, tracers, target boxes, the keyed cockpit art) instead:
  <scene>_classic.png   the faithful 320x200 page scaled N x N nearest (the same scene run with the DAC of the flight palette)
  <scene>_hr.png        what the compositor shows with the 3D layer, the vector layer and the shadow plane (<id>.cmp of the port, hr_pass.c frame_check)
  contact_views.png     classic | hr for every view mode
S3.2: `--tvmap` cuts the TV camera and map MFD windows out of the frame (the tv* / map* scenes, N x): <scene>_classic.png, <scene>_hr.png and contact_tv_map.png.
The scenes run with `call pal_flight_load` first (the mission's flight palette as the game loads it; the golden scenes keep the start-up palette, S2.3 note).
"""
import os, shutil, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from hrd import Hrd

DITHER_SCENES = ['dither_target0', 'dither_close0', 'dither_close2', 'dither_runway', 'dither_low', 'dither_frame', 'stipple_close0', 'stipple_runway']
OTHER_SCENES = ['runway0', 'runway_full', 'low_d3_0', 'low_d0_0', 'high0', 'target0', 'target4', 'close4', 'inside2', 'unit0_0', 'frames', 'smoke0', 'ext0', 'tv0', 'map3']
MASK_RGB = {0: (60, 60, 60), 1: (40, 200, 70), 2: (230, 40, 200), 3: (230, 40, 40)}


def font(size):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:
        return ImageFont.load_default()


def to_rgb(idx, pal):
    return pal[idx]


def run_mode(exe, game, gwork, out, n, mode, scenes):
    wd = os.path.join(out, '_run_' + mode)
    shutil.rmtree(wd, ignore_errors=True)
    os.makedirs(wd)
    for f in os.listdir(gwork):
        if f.startswith('s1.'):
            shutil.copyfile(os.path.join(gwork, f), os.path.join(wd, f))
    keep, blocks, cur = set(scenes), [], None
    for line in open(os.path.join(gwork, 'scn.txt')).read().splitlines():
        if line.startswith('scn '):
            cur = [line] if line.split()[1] in keep else None
        elif cur is not None:
            cur.append(line)
            if line == 'end':
                blocks.append('\n'.join(cur))
                cur = None
    open(os.path.join(wd, 'scn.txt'), 'w').write('\n'.join(blocks) + '\n')
    env = dict(os.environ)
    env.pop('F117_CLASSIC', None)
    env['F117_ENH_ARGS'] = '--render-scale %d --dither %s --hr-dump %s' % (n, mode, os.path.abspath(wd).replace('\\', '/'))
    r = subprocess.run([exe, '--render-test', wd, '--game-dir', game], env=env, capture_output=True, text=True, timeout=900)
    if r.returncode != 0:
        raise SystemExit('the port failed (%s): %s' % (mode, r.stdout[-400:] + r.stderr[-400:]))
    return wd


def read_cmp(path):
    """DIR/<id>.cmp: s32 lw, lh; the DAC (6-bit); lh rows of lw palette indices -> (index image, 8-bit RGB palette)"""
    raw = open(path, 'rb').read()
    lw, lh = np.frombuffer(raw, '<i4', 2)
    dac = np.frombuffer(raw, np.uint8, 768, 8).reshape(256, 3).astype(np.uint16)
    idx = np.frombuffer(raw, np.uint8, int(lw * lh), 8 + 768).reshape(lh, lw)
    return idx, ((dac << 2) | (dac >> 4)).astype(np.uint8)


def run_views(exe, game, gwork, out, n, scenes):
    wd = os.path.join(out, '_run_views')
    shutil.rmtree(wd, ignore_errors=True)
    os.makedirs(wd)
    for f in os.listdir(gwork):
        if f.startswith('s1.'):
            shutil.copyfile(os.path.join(gwork, f), os.path.join(wd, f))
    keep, blocks, cur = set(scenes), [], None
    for line in open(os.path.join(gwork, 'scn.txt')).read().splitlines():
        if line.startswith('scn '):
            cur = [line, 'call pal_flight_load'] if line.split()[1] in keep else None
        elif cur is not None:
            cur.append(line)
            if line == 'end':
                blocks.append('\n'.join(cur))
                cur = None
    open(os.path.join(wd, 'scn.txt'), 'w').write('\n'.join(blocks) + '\n')
    env = dict(os.environ)
    env.pop('F117_CLASSIC', None)
    env['F117_ENH_ARGS'] = '--render-scale %d --hr-dump %s' % (n, os.path.abspath(wd).replace('\\', '/'))
    r = subprocess.run([exe, '--render-test', wd, '--game-dir', game], env=env, capture_output=True, text=True, timeout=900)
    if r.returncode != 0:
        raise SystemExit('the port failed (views): %s' % (r.stdout[-400:] + r.stderr[-400:]))
    return wd


def main_views(exe, game, gwork, out, n, scenes):
    if not scenes:
        scenes = [l.split()[1] for l in open(os.path.join(gwork, 'scn.txt')).read().splitlines() if l.startswith('scn view_')]
    wd = run_views(exe, game, gwork, out, n, scenes)
    rows = []
    for sc in scenes:
        pc, ph = os.path.join(wd, sc + '.cmp'), os.path.join(wd, sc + '.hrd')
        if not os.path.exists(pc):
            print('dump-scenes: scene %s is not in scn.txt' % sc)
            continue
        idx, pal = read_cmp(pc)
        page = Hrd(ph).page
        classic = pal[np.repeat(np.repeat(page, n, 0), n, 1)]
        hr = pal[idx]
        Image.fromarray(classic).save(os.path.join(out, sc + '_classic.png'))
        Image.fromarray(hr).save(os.path.join(out, sc + '_hr.png'))
        tw = 640
        th = tw * classic.shape[0] // classic.shape[1]
        rows.append([labelled(Image.fromarray(v).resize((tw, th), Image.BILINEAR), '%s  %s  (x%d, reduced to %d px wide)' % (sc, k, n, tw)) for k, v in (('classic', classic), ('hr', hr))])
    if rows:
        H = sum(r[0].height for r in rows)
        sheet = Image.new('RGB', (2 * 640, H), (20, 20, 24))
        y = 0
        for r in rows:
            for i, c in enumerate(r):
                sheet.paste(c, (i * 640, y))
            y += r[0].height
        sheet.save(os.path.join(out, 'contact_views.png'))
        print('dump-scenes: %s (%d scenes)' % (os.path.join(out, 'contact_views.png'), len(rows)))
    shutil.rmtree(wd, ignore_errors=True)
    return 0


TVMAP_SCENES = ['tvclose0', 'tvfire0', 'tvflir0', 'tvsmoke0', 'tvsimple_flir0', 'tvfire1', 'tvflir1', 'tvsmoke1', 'tvclose2',
                'map2', 'map4', 'map6', 'map9', 'mapbg6']


def main_tvmap(exe, game, gwork, out, n, scenes):
    """S3.2: the TV camera window (right MFD sensor page, x 169..245, y 110..169) and the map MFD window (left MFD, x 74..150, y 110..169) of the tv* / map* scenes:
    <scene>_classic.png / <scene>_hr.png are the window cut out of the frame at N x (the classic 1x page scaled N x N, and what the compositor shows with the 3D layer);
    contact_tv_map_scenes.png has both side by side at twice that size for every scene."""
    scenes = scenes or TVMAP_SCENES
    wd = run_views(exe, game, gwork, out, n, scenes)
    rows, bad = [], 0
    for sc in scenes:
        pc, ph = os.path.join(wd, sc + '.cmp'), os.path.join(wd, sc + '.hrd')
        if not os.path.exists(pc):
            print('dump-scenes: scene %s is not in scn.txt' % sc)
            continue
        idx, pal = read_cmp(pc)
        page = Hrd(ph).page
        x0, y0, w, h = (74, 110, 77, 60) if sc.startswith('map') else (169, 110, 77, 61)
        classic = pal[np.repeat(np.repeat(page, n, 0), n, 1)][y0 * n:(y0 + h) * n, x0 * n:(x0 + w) * n]
        hr = pal[idx][y0 * n:(y0 + h) * n, x0 * n:(x0 + w) * n]
        Image.fromarray(classic).save(os.path.join(out, sc + '_classic.png'))
        Image.fromarray(hr).save(os.path.join(out, sc + '_hr.png'))
        hh, ww = hr.shape[:2]                      # a window drawn at N x has blocks of N x N layer pixels that are not one colour (and differs from the scaled 1x page)
        blk = hr.reshape(hh // n, n, ww // n, n, 3).transpose(0, 2, 1, 3, 4).reshape(hh // n, ww // n, n * n, 3)
        if n > 1 and not (blk != blk[:, :, :1]).any() and not sc.startswith('tvsimple'):
            print('dump-scenes: %s: the window shows no N x detail' % sc)
            bad += 1
        z = 2
        rows.append([labelled(Image.fromarray(v).resize((v.shape[1] * z, v.shape[0] * z), Image.NEAREST), '%s  %s  (x%d, shown x%d)' % (sc, k, n, z)) for k, v in (('classic', classic), ('hr', hr))])
    if rows:
        cols = 2
        cw, ch = max(c.width for r in rows for c in r), max(c.height for r in rows for c in r)
        sheet = Image.new('RGB', (cols * cw, len(rows) * ch), (20, 20, 24))
        for i, r in enumerate(rows):
            for j, c in enumerate(r):
                sheet.paste(c, (j * cw, i * ch))
        sheet.save(os.path.join(out, 'contact_tv_map_scenes.png'))
        print('dump-scenes: %s (%d scenes)' % (os.path.join(out, 'contact_tv_map_scenes.png'), len(rows)))
    shutil.rmtree(wd, ignore_errors=True)
    return 1 if bad or not rows else 0


def labelled(img, text, h=22):
    im = Image.new('RGB', (img.width, img.height + h), (20, 20, 24))
    ImageDraw.Draw(im).text((6, 2), text, fill=(235, 235, 235), font=font(15))
    im.paste(img, (0, h))
    return im


def best_crop(a, b, cw, ch):
    """top-left of the cw x ch window where the two images differ most"""
    d = (a != b).astype(np.int32)
    H, W = d.shape
    ii = np.pad(d, ((1, 0), (1, 0))).cumsum(0).cumsum(1)
    best, pos = -1, (0, 0)
    for y in range(0, max(1, H - ch), 20):
        for x in range(0, max(1, W - cw), 20):
            s = ii[y + ch, x + cw] - ii[y, x + cw] - ii[y + ch, x] + ii[y, x]
            if s > best:
                best, pos = s, (x, y)
    return pos


def main():
    argv = sys.argv[1:]
    n, scenes = 4, None
    if '--n' in argv:
        i = argv.index('--n'); n = int(argv[i + 1]); del argv[i:i + 2]
    views = '--views' in argv
    if views:
        argv.remove('--views')
    tvmap = '--tvmap' in argv
    if tvmap:
        argv.remove('--tvmap')
    if '--scenes' in argv:
        i = argv.index('--scenes'); scenes = argv[i + 1].split(','); del argv[i:i + 2]
    if len(argv) < 3:
        print(__doc__)
        return 2
    exe, game, gwork = [os.path.abspath(a) for a in argv[:3]]
    out = os.path.abspath(argv[3]) if len(argv) > 3 else os.path.abspath(os.path.join(HERE, '..', '..', '..', 'work', 'e23'))
    if not os.path.exists(os.path.join(gwork, 'scn.txt')):
        print('dump-scenes: no scn.txt in %s (run the render3d_golden ctest first)' % gwork)
        return 77
    os.makedirs(out, exist_ok=True)
    if views:
        return main_views(exe, game, gwork, out, n, scenes)
    if tvmap:
        return main_tvmap(exe, game, gwork, out, n, scenes)
    groups = [('contact_dither.png', DITHER_SCENES), ('contact_scenes.png', OTHER_SCENES)]
    if scenes:
        groups = [('contact_selected.png', scenes)]
    allsc = [s for _, g in groups for s in g]
    runs = {m: run_mode(exe, game, gwork, out, n, m, allsc) for m in ('grid', 'fine')}
    for sheet, group in groups:
        rows = []
        for sc in group:
            pg = os.path.join(runs['grid'], sc + '.hrd')
            if not os.path.exists(pg):
                print('dump-scenes: scene %s is not in scn.txt' % sc)
                continue
            hg, hf = Hrd(pg), Hrd(os.path.join(runs['fine'], sc + '.hrd'))
            pal = hg.palette()
            classic = pal[np.repeat(np.repeat(hg.page, n, 0), n, 1)]
            ig, mg = hg.compose3d()
            if_, _ = hf.compose3d()
            imgs = {'classic': classic, 'grid': pal[ig], 'fine': pal[if_]}
            for k, v in imgs.items():
                Image.fromarray(v).save(os.path.join(out, '%s_%s.png' % (sc, k)))
            mask = np.zeros(mg.shape + (3,), np.uint8)
            for k, c in MASK_RGB.items():
                mask[mg == k] = c
            Image.fromarray(mask).save(os.path.join(out, sc + '_mask.png'))
            tw = 640
            th = tw * imgs['classic'].shape[0] // imgs['classic'].shape[1]
            row = [labelled(Image.fromarray(imgs[k]).resize((tw, th), Image.BILINEAR), '%s  %s  (x%d, reduced to %d px wide)' % (sc, k, n, tw)) for k in ('classic', 'grid', 'fine')]
            rows.append(row)
            if sc.startswith(('dither', 'stipple')):
                cw, ch = 40 * n, 25 * n
                x, y = best_crop(ig, if_, cw, ch)
                z = 3
                crop = [labelled(Image.fromarray(imgs[k][y:y + ch, x:x + cw]).resize((cw * z, ch * z), Image.NEAREST),
                                 '%s  %s  crop %dx%d at %d,%d, nearest x%d' % (sc, k, cw, ch, x, y, z)) for k in ('classic', 'grid', 'fine')]
                rows.append(crop)
        if not rows:
            continue
        W = 3 * 640
        H = sum(max(c.height for c in r) for r in rows)
        sheet_im = Image.new('RGB', (W, H), (20, 20, 24))
        y = 0
        for r in rows:
            for i, c in enumerate(r):
                sheet_im.paste(c, (i * 640, y))
            y += max(c.height for c in r)
        sheet_im.save(os.path.join(out, sheet))
        print('dump-scenes: %s (%d scenes)' % (os.path.join(out, sheet), len(group)))
    for m in runs.values():
        shutil.rmtree(m, ignore_errors=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
