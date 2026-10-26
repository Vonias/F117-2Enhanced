# Changelog

## 0.1.0 (2026-10-26)

The first Enhanced release: the faithful port's game (its v0.1.0), drawn sharper and wider. The simulation, the
rules and the original art are unchanged. You need your own copy of the original game files (see README.md,
How to play).

### What is new

- **Higher resolution.** The 3D view, the HUD, the MFDs, the TV camera window and the map are drawn at N times
  the original 320x200 (the render scale; **Auto** picks it from the window size, or choose 1 to 8). The
  pictures, fonts, sprites, palettes and 3D models are still the original's.
- **Wider view.** On a wide window the first cockpit view and the outside views show more of the world beside
  the 4:3 picture instead of black bars. The side, rear and second cockpit views are fitted to the window
  height; the menus keep their 4:3 picture, centred.
- **Smooth distance shading** (option, off by default): the sky and ground colour bands and the depth shading
  of terrain and objects fade smoothly instead of in steps.
- **True night** (option, off by default): night missions are much darker, and terrain and objects fade into
  the dark sooner.
- **The launcher** (`f117-enhanced-launcher.exe`): the faithful launcher (game folder, window, speed, sound,
  key bindings) plus an **Enhanced** box with Render scale, Wide view, Smooth distance shading, True night
  and Classic. The settings are kept in `%APPDATA%\F-117A Enhanced\settings.ini`.
- **`--classic`**: every enhancement off; the game behaves as the faithful port.

### Command line

- `--render-scale N`, `--wide` / `--no-wide`, `--smooth-shading` / `--no-smooth-shading`,
  `--true-night` / `--no-true-night`, `--classic` for `f117-enhanced.exe` (the launcher passes them).
- The faithful options are unchanged: `--game-dir`, `--scale`, `--aspect`, `--fullscreen`, `--frame-ticks`,
  `--keys`, `--check`, `--version`.

### Known limits

- The frame rate is still the original's (one picture per game step, about 10 a second at the default Game
  speed). Smoother frames are the next stage.
- Wide view: the first frame after a change of view has black beside the picture; at very wide windows
  (about 21:9 and wider) a thin sliver of the sky band can show at the far edge in steep rolls; objects
  that the wide view adds at the sides can overlap into the 4:3 picture; the wide view makes each frame
  slower to draw. Portrait windows fit the picture to the width.
- Everything the faithful 0.1.0 listed as untested (mouse and joystick in the shell and the debrief) applies.

### How it was verified

- The faithful tests pass unchanged in `--classic`, and the enhanced drawing is checked against the original
  frames (the 3D scenes, the wide view across aspect ratios, and a scripted run through every view).
- Playtested.
