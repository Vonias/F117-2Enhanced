"""Gameplay video of the Enhanced build (work/video/f117-enhanced-gameplay.mp4): real flights of the scripted chain, captured frame by frame.

Each run is the scripted chain of the real-path tests (port/notes/e24/sandbox_hash.py: a fresh game folder with a synthetic roster, START: a new pilot,
a strike over Libya -> VGAME at the home airbase -> END -> START), headless on the virtual clock with a pretended 1920 x 1080 window
(F117_WINDOW), `--render-scale 4 --wide --smooth-shading --frame-ticks 4` (the launcher's Game speed 4: 17.5 frames a second, the original's own
pacing cap; the flight model keeps its speed) and keys scripted into VGAME (F117_KEYS, XT scan codes at seconds of the host clock). The capture aid
F117_RECORD (f117port/src/enhanced/record.c) writes the presented canvas at most once per 1/30 s slot and the OPL chip rendered tick by tick.
No sim change: the attack runs start from the test pose of the real-path tests (F117_TEST_POSE: level at 600 ft, 380 kt, heading south, a few km
north of the SAM radar at Tripoli); the night run adds F117_TEST_NIGHT=2 (the night flag and palette from the first frame, as a night mission).

Runs and shots (seconds of the host clock; VGAME starts at about 61 s):
  takeoff  day: the first cockpit view on the runway, full throttle, brakes off; chase view at lift-off, pull up, gear up; side, fly-by (delayed),
           front without cockpit, the first cockpit view, the second cockpit view (c), and back.
  attack   day: the pose, gear up, look ahead (TV page, the SAM radar at Tripoli designated), F2 F2 air-to-ground, bay doors, the HARM fired at
           lock; weapon view to the impact and the smoke; side and chase views low over the coast.
  night    the same attack at night with --true-night: cockpit, the far target view of the impact, the cockpit again, a chase view.
Every frame of the video is a captured picture held for as long as the game showed it (30 fps output, about 17 pictures a second; no
interpolation). Hard cuts between runs; the sound is the game's own OPL output of the same span.

usage: python make_video.py [--exe F117] [--game DIR] [--work DIR] [--only RUN,...] [--keep]
needs: the Python used by the real-path tests (numpy, Pillow, unicorn for the synthetic roster), ffmpeg on PATH.
"""
import argparse, os, shutil, subprocess, sys, time
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, os.path.join(HERE, '..', 'e24'))
import sandbox_hash as SH

FPS = 30
OUT_W, OUT_H = 1920, 1080
BASE = '--render-scale 4 --wide --smooth-shading --frame-ticks 4'
START_KEYS = '1:31,2:03,11:39'                    # the chain's own keys before VGAME
ATTACK = '61.7:07,62:35,62.5:3c,63:3c,63.5:09,64.5:1c'   # gear up, look ahead (/), F2 F2, bay doors (8), Enter at lock
POSE = '20,0x8000,600,0'                          # VGAME frame 20: level at 600 ft heading south

RUNS = {
    'takeoff': dict(extra=BASE, frames=1100, rec=(62, 100), env={},
                    keys='63:0dp,66:0dr,67:0b,68:2a+0d,72.5:2a+3b,74:e050p,75.5:e050r,77:07,79:2a+3d,83:2a+3c,87:2a+35,90.5:3b,93.5:2e,97:2e'),
    'attack': dict(extra=BASE, frames=500, rec=(61.5, 82), env={'F117_TEST_POSE': POSE},
                   keys=ATTACK + ',64.8:2a+3e,69.6:2a+3d,74:2a+3b'),
    'night': dict(extra=BASE + ' --true-night', frames=500, rec=(61.5, 82), env={'F117_TEST_POSE': POSE, 'F117_TEST_NIGHT': '2'},
                  keys=ATTACK + ',64.8:2a+40,70:3b,76:2a+3b'),
}
SHOTS = [            # (run, from, to)
    ('takeoff', 64.0, 99.0),
    ('attack', 62.0, 80.0),
    ('night', 62.0, 78.5),
]


def run(name, cfg, exe, game, work):
    d = os.path.join(work, name)
    g = SH.fresh_game(game, os.path.join(d, 'game'))        # a copy of the game folder, a synthetic roster: Game/ is never written
    rec = os.path.join(d, 'rec')
    shutil.rmtree(rec, ignore_errors=True)
    os.makedirs(rec)
    env = dict(os.environ, F117_KEYS=START_KEYS + ',' + cfg['keys'], F117_SHELL_SCRIPT=os.path.join(SH.S54, 'shell_chain_libya.txt'),
               F117_VGAME_FRAMES=str(cfg['frames']), F117_END_SCRIPT=os.path.join(SH.S58, 'end_chain.txt'), F117_VCLOCK='1',
               F117_WINDOW='%dx%d' % (OUT_W, OUT_H), F117_RECORD=rec, F117_RECORD_FPS=str(FPS), F117_RECORD_FROM=str(cfg['rec'][0]),
               F117_RECORD_TO=str(cfg['rec'][1]), F117_ENH_ARGS=cfg['extra'], **cfg['env'])
    for k in ('F117_CLASSIC', 'F117_TEST_POSE', 'F117_TEST_NIGHT'):
        if k not in cfg['env']:
            env.pop(k, None)
    t = time.time()
    p = subprocess.run([exe, '--headless', '--trace', '--game-dir', g], env=env, capture_output=True, text=True, timeout=7200)
    open(os.path.join(d, 'stdout.txt'), 'w').write(p.stdout)
    open(os.path.join(d, 'stderr.txt'), 'w').write(p.stderr)
    n = sum(1 for _ in open(os.path.join(rec, 'frames.txt')))
    print('run %-8s rc %d, %d pictures, %.0f s' % (name, p.returncode, n, time.time() - t))
    if p.returncode != 0 or 'quit: START returned 0' not in p.stdout:
        sys.exit('run %s did not end normally (see %s)' % (name, d))
    return rec


def picture(raw, row):
    """the captured canvas as the window shows it: scaled to its presentation size (the 4:3 pixel aspect), fitted into 1920 x 1080"""
    t, w, h, vw, vh, off = row
    raw.seek(off)
    a = np.frombuffer(raw.read(4 * w * h), '<u4').reshape(h, w)
    im = Image.fromarray(np.stack([(a >> 16) & 255, (a >> 8) & 255, a & 255], 2).astype(np.uint8))
    s = min(OUT_W / vw, OUT_H / vh)
    pw, ph = round(vw * s), round(vh * s)
    if (pw, ph) != (w, h):
        im = im.resize((pw, ph), Image.BICUBIC)
    if (pw, ph) == (OUT_W, OUT_H):
        return im.tobytes()
    out = Image.new('RGB', (OUT_W, OUT_H))
    out.paste(im, ((OUT_W - pw) // 2, (OUT_H - ph) // 2))
    return out.tobytes()


def encode_shot(rec, t0, t1, out):
    rows = [(float(r[0]), int(r[1]), int(r[2]), int(r[3]), int(r[4]), int(r[5])) for r in (l.split() for l in open(os.path.join(rec, 'frames.txt')))]
    n = int(round((t1 - t0) * FPS))
    cmd = ['ffmpeg', '-v', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', '%dx%d' % (OUT_W, OUT_H), '-r', str(FPS), '-i', '-',
           '-ss', '%.4f' % t0, '-t', '%.4f' % (t1 - t0), '-i', os.path.join(rec, 'audio.wav'),
           '-map', '0:v', '-map', '1:a', '-af', 'aresample=48000,afade=t=in:d=0.05,afade=t=out:st=%.3f:d=0.05' % (t1 - t0 - 0.05),
           '-c:v', 'libx264', '-preset', 'slow', '-crf', '12', '-pix_fmt', 'yuv420p', '-c:a', 'pcm_s16le', out]
    ff = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    j, cur, last = 0, None, None
    with open(os.path.join(rec, 'frames.raw'), 'rb') as raw:
        for k in range(n):
            t = t0 + k / FPS + 1e-6
            while j < len(rows) and rows[j][0] <= t:      # the picture the window shows at t: the last one presented by then
                j += 1
            if j - 1 != last:
                last = j - 1
                cur = picture(raw, rows[max(0, last)])
            ff.stdin.write(cur)
    ff.stdin.close()
    if ff.wait() != 0:
        sys.exit('ffmpeg failed on %s' % out)
    print('shot %s: %.1f s, %d frames' % (os.path.basename(out), t1 - t0, n))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', default=os.path.join(ROOT, 'f117port', 'build', 'f117-enhanced.exe'))
    ap.add_argument('--game', default=os.path.join(ROOT, 'Game'))
    ap.add_argument('--work', default=os.path.join(ROOT, 'work', 'video'))
    ap.add_argument('--only', default='', help='re-capture only these runs (the other shots are reused)')
    ap.add_argument('--keep', action='store_true', help='keep the raw captures (several GB per run)')
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    only = set(x for x in a.only.split(',') if x)
    parts = []
    for i, (name, t0, t1) in enumerate(SHOTS):
        part = os.path.join(a.work, 'shot%d_%s.mkv' % (i, name))
        if not only or name in only or not os.path.isfile(part):
            rec = run(name, RUNS[name], a.exe, a.game, a.work)
            encode_shot(rec, t0, t1, part)
            if not a.keep:
                os.remove(os.path.join(rec, 'frames.raw'))
        parts.append(part)
    out = os.path.join(a.work, 'f117-enhanced-gameplay.mp4')
    inputs = sum((['-i', p] for p in parts), [])
    fc = ''.join('[%d:v][%d:a]' % (i, i) for i in range(len(parts))) + 'concat=n=%d:v=1:a=1[v][a]' % len(parts)
    subprocess.run(['ffmpeg', '-v', 'error', '-y'] + inputs + ['-filter_complex', fc, '-map', '[v]', '-map', '[a]', '-c:v', 'libx264', '-preset', 'slow',
                    '-crf', '18', '-pix_fmt', 'yuv420p', '-r', str(FPS), '-c:a', 'aac', '-b:a', '192k', '-movflags', '+faststart', out], check=True)
    print('video: %s' % out)


if __name__ == '__main__':
    main()
