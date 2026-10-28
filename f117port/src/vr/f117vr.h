#pragma once
/* VR: the native interface of libf117.so, called from the StereoKit app (C# P/Invoke, NativeEngine.cs).
 * The engine runs the original chain on its own thread (f117vr_start); the app polls the picture and the
 * sound and feeds keys, joystick and pointer. Everything here is safe to call from any thread. */
#include <stdint.h>

#if defined(_WIN32)
#define F117VR_API __declspec(dllexport)
#else
#define F117VR_API __attribute__((visibility("default")))
#endif

enum { F117VR_IDLE = 0, F117VR_RUNNING = 1, F117VR_FINISHED = 2, F117VR_FATAL = 3 };

/* Starts the chain on the engine thread. args: extra command-line options separated by spaces
 * (e.g. "--render-scale 4 --frame-ticks 4"). Returns 0, or -1 if it is already running. */
F117VR_API int f117vr_start(const char *game_dir, const char *args);
F117VR_API int f117vr_state(void);
F117VR_API const char *f117vr_error(void);     /* the host_fatal message once the state is F117VR_FATAL */
F117VR_API void f117vr_quit(void);              /* like closing the window: the chain stops at its next wait */
F117VR_API const char *f117vr_program(void);   /* the running program ("VGAME" in flight, "START", "END", ...) */

/* The size the canvas pretends the window has (render scale Auto and the wide view follow it). */
F117VR_API void f117vr_set_view_size(int w, int h);
/* In flight, the main 3D window's world pixels come out transparent (alpha 0): the headset draws the world (Route 2). */
F117VR_API void f117vr_set_world_mask(int on);

/* Copies the latest picture (RGBA8888, alpha 255) into dst if it holds cap pixels or more and returns its
 * serial number (0 = no picture yet). w, h: its size; aspect: the width / height it is meant to be shown at. */
F117VR_API int f117vr_frame(uint32_t *dst, int cap, int *w, int *h, float *aspect);
F117VR_API int f117vr_frame_serial(void);

/* An XT set-1 make code (bit 8 = grey key, sent with E0), pressed or released. */
F117VR_API void f117vr_key(int xt, int down);
/* The port 201h joystick: present, axes -32768..32767 (stick A X/Y, stick B X/Y), buttons bit0..3. */
F117VR_API void f117vr_joy(int present, int ax, int ay, int bx, int by, int buttons);
/* The pointer (START and END): u, v in 0..1 across the displayed picture, buttons bit0 left, bit1 right. */
F117VR_API void f117vr_mouse(float u, float v, int buttons);

/* Renders frames of mono sound at F117VR_AUDIO_RATE into out; returns frames. */
#define F117VR_AUDIO_RATE 48000
F117VR_API int f117vr_audio(float *out, int frames);

/* ---- Route 2: the game's own 3D world (vr_world.h has the structs). In VGAME the app asks for the world once per mission; the
 * engine builds it at its next pump (meshes of the 3DX models, the terrain's scenery instances) and bumps the serial. */
F117VR_API void f117vr_world_request(void);
F117VR_API int  f117vr_world_serial(void);
F117VR_API void f117vr_world_counts(int *nmesh, int *ntri, int *nline, int *ninst);
/* copies everything if the buffers hold the counts; returns the serial, 0 when they are too small */
F117VR_API int  f117vr_world_copy(void *meshes, int cm, void *tris, int ct, void *lines, int cl, void *insts, int ci);
F117VR_API void f117vr_camera(void *vr_camera);             /* VrCamera: the cockpit eye, refreshed while VGAME runs */
F117VR_API void f117vr_palette(uint8_t *rgb768);           /* the live DAC, 8-bit RGB */
F117VR_API int  f117vr_objects(void *vr_objs, int cap);     /* VrObj[]: the moving objects of the latest frame; returns how many */
/* VrInst[] again (all of them, the buffer must hold the count) when the scenery's state changed since `known` (something was
 * destroyed); returns the state's serial, 0 when nothing was copied */
F117VR_API int  f117vr_world_live(void *vr_insts, int ci, int known);
/* one simulation frame: the camera (its frame and time_ns say which, and when) and its objects together; returns how many objects */
F117VR_API int  f117vr_sim_frame(void *vr_camera, void *vr_objs, int cap);
F117VR_API long long f117vr_now_ns(void);                  /* the clock of VrCamera.time_ns */
/* ---- the headset's flight model (vr_fdm.h): on, the game takes the player's aircraft from f117vr_fdm_set each frame */
F117VR_API void f117vr_fdm_enable(int on);
F117VR_API void f117vr_fdm_set(const void *vr_fdm_state);   /* VrFdmState */
F117VR_API void f117vr_fdm_game(void *vr_fdm_game);         /* VrFdmGame: the game's side, each frame */
F117VR_API int  f117vr_events(void *vr_events, int cap, int after);   /* VrEvent[]: the explosions after id `after`; returns how many */
