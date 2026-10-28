#pragma once
/* VR: the headset's flight model (the app's JSBSim, VrFlight.cs) flying the player's aircraft (vr_fdm.c, flight/fl.h FlExt). The structs are
 * copied as they are by the app (NativeEngine.cs mirrors them, packed). World units: feet, x east, y north, as BA10 / C02A. */
#include "types.h"

#pragma pack(push, 1)
/* app -> engine: the aircraft as the flight model has it */
typedef struct {
    double x, y;             /* world units (feet), x east, y north */
    double alt_ft;           /* true altitude (as 2DF6): the ground's height [C0A2] when on the wheels */
    float heading, pitch, roll;   /* radians: JSBSim's psi, theta, phi (heading clockwise from north, nose up, right wing down) */
    float kts;               /* airspeed, as the HUD shows it */
    float nz;                /* load factor, G */
    float vs_fps;            /* climb rate (ft/s, + up); at a touch-down the sink just before it */
    float aoa;               /* radians: the flight path below the nose (theta - gamma) */
    float stall_kts;
    u8 on_ground, stick;     /* on the wheels; the pilot is flying (the stick off centre: the autopilot lets go) */
    u8 pad[2];
    s32 start;               /* the VrFdmGame.start it was set up from: an older one is not applied */
} VrFdmState;

/* engine -> app: what the game says about the aircraft, each frame */
typedef struct {
    double x, y;             /* BA10 / C02A */
    u16 alt;                 /* [2DF6] true altitude */
    u16 ground;              /* [C0A2] the ground's height under it (a carrier's deck) */
    u16 heading, pitch, roll;   /* [2DEE] [2DF0] [2DF2], 10000h a turn */
    s16 speed;               /* [B168] */
    u16 throttle;            /* [2DFE] 0..100 */
    s16 fuel;                /* [3634] */
    u16 ap_alt, ap_heading;  /* [3652] the autopilot's altitude (0 = off), [C078] its heading */
    u8 gear_down, brakes, flaps, ejecting;
    u8 time_accel;           /* [3D7A]: 2 while time runs fast */
    u8 pad[3];
    s32 start;               /* bumped when the flight model must start over here: a new mission, a jump, switched on */
    s32 catapult;            /* bumped by a catapult launch */
    s32 frame;               /* counts the simulation's frames (it stands still while the game is paused) */
} VrFdmGame;
#pragma pack(pop)

void vr_fdm_enable(int on);
void vr_fdm_set(const VrFdmState *s);
void vr_fdm_game(VrFdmGame *g);
