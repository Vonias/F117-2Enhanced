# Gameplay video

`f117-enhanced-gameplay.mp4`: 1920 x 1080, H.264 (CRF 18, yuv420p), 30 fps, AAC sound. No overlays: every picture is the game's.

## Regenerate

```
cd f117port/build && ninja                                  # C:\msys64\mingw64\bin first on PATH
python port/notes/video/make_video.py                       # the Python of the real-path ctests (numpy, Pillow, unicorn); ffmpeg on PATH
python port/notes/video/make_video.py --only night          # re-capture one run, reuse the other shots
```

The script (`port/notes/video/make_video.py`, run list and key scripts at the top) runs the game three times headless on the virtual
clock: the scripted chain of the real-path tests (a fresh game folder under `work/video/<run>/game` with a synthetic roster, so `Game/`
and the real roster are never written), a pretended 1920 x 1080 window, `--render-scale 4 --wide --smooth-shading --frame-ticks 4`
(plus `--true-night` for the night run) and keys scripted into the flight (`F117_KEYS`). The capture aid `F117_RECORD=DIR`
(`f117port/src/enhanced/record.c`) writes the presented canvas once per 1/30 s slot to `DIR/frames.raw` + `frames.txt` and the OPL chip,
rendered tick by tick, to `DIR/audio.wav`. Each shot is encoded to `shot<N>_<run>.mkv` (pictures held for as long as the game showed
them, fitted into 1920 x 1080 at the 4:3 pixel aspect), then the shots are joined with hard cuts. The raw captures (several GB a run)
are deleted unless `--keep`.

Runs: `takeoff` (day: runway, lift-off, view switching), `attack` (day: HARM on the SAM radar at Tripoli from the real-path test pose,
`F117_TEST_POSE=20,0x8000,600,0`), `night` (the same attack with `F117_TEST_NIGHT=2` and `--true-night`).
