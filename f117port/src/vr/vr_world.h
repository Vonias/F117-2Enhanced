#pragma once
/* VR: the 3D world for the headset's renderer (vr_world.c). The structs are copied as they are by the app (NativeEngine.cs mirrors
 * them field for field, packed). World units: the terrain's level-1 units (= the 3DX model units of bank 0 objects at lod 3). */
#include "types.h"

#pragma pack(push, 1)
typedef struct {
    s32 first_tri, ntri, first_line, nline;
    float radius;
    s32 key;                 /* bank << 8 | model index */
    s32 cls;                 /* the size class (type flags & 7, [497A]): the depth shade starts that much later */
} VrMesh;

#define VRTRI_DOUBLE 1       /* a model without planes: no back-face test in the original */
typedef struct {
    float p[9];              /* three vertices in model axes: x east, y up, z south (StereoKit's x, y, z) */
    u16 colour;              /* colour word: low byte DAC index, high byte fill (FFh solid / shade ramp, < FBh dither with it, FCh stipple, FDh OR, FEh AND, FBh none) */
    u16 order;               /* polygon number in the model's draw order (coplanar decals) */
    u8 flags;
    u8 cv1; s8 cval1;        /* element condition: drawn when variable cv1 == cval1 (cv1 FFh: always) */
    u8 cv2; s8 cval2;        /* plane condition, the same way */
    u8 pad[3];
} VrTri;

typedef struct {
    float p[6];
    u16 colour;
    u8 cv; s8 cval;
    u8 pad;
} VrLine;

typedef struct {
    double x, y, z;          /* world units: x east, y north, z up */
    float scale;             /* model units -> world units (4^(level-1)) */
    s32 mesh;
    u8 level;                /* terrain level 1..4 (the original draws each level only near the camera) */
    u8 dead;                 /* variable 1 (C030) set: the destroyed variant */
    u8 pad[2];
} VrInst;

typedef struct {
    double x, y, z;          /* the cockpit eye in world units */
    float right[3], up[3], fwd[3];   /* its axes in world (east, north, up) */
    u16 view;                /* [C07A] */
    u16 night;               /* [9874]: the lights variables */
    s32 serial;
    u16 throttle;            /* [2DFE]: the throttle, 0..100 (the panel's THRUST) */
    u16 alt_ft;              /* [2DF6]: the altitude in feet, as the HUD shows it */
    s16 speed;               /* [B168]: the airspeed, as the HUD shows it */
    u16 heading;             /* [2DEE]: the heading, 10000h = 360 degrees */
    u16 night_mission;       /* [3658]: a night mission (the palette is the night one; true night darkens with distance) */
    u16 pad2;
    s32 frame;               /* counts the simulation's frames: bumped when the eye or any object moved */
    s64 time_ns;             /* when that frame appeared (CLOCK_MONOTONIC, f117vr_now_ns) */
} VrCamera;
/* A moving object of the frame (r3d_draw_world_objects 9ACA): a unit (aircraft, SAM vehicle, ship, truck), a projectile or the
 * falling wreck, with the model, place and attitude the original draws it with. */
#define VROBJ_MAX 96
typedef struct {
    double x, y, z;          /* world units: x east, y north, z up */
    float rot[9];            /* model axes -> world model axes (east, up, south), rows = the images of the model's x, y, z */
    float scale;             /* the original's lod shift: model units -> world units */
    s32 mesh;                /* -1: no mesh */
    u8 kind;                 /* 0 unit, 1 projectile, 2 wreck */
    u8 wpn;                  /* a projectile's weapon type ([slot + 10h], the table 3372; 1Ch..1Eh the bombs) */
    u8 slot;                 /* which one, the same from frame to frame: a unit 0..63, a projectile 64 + its slot, the wreck 127 */
    u8 pad;
} VrObj;

/* An explosion of the original (its counter [9D0C] started): where, and the cause */
#define VREV_IMPACT 0        /* a weapon hit the ground */
#define VREV_HIT    1        /* a unit or a ground object destroyed (a ground object: z = 0) */
#define VREV_CRASH  2        /* an aircraft hit the ground */
typedef struct {
    double x, y, z;          /* world units: x east, y north, z up */
    s32 id;                  /* 1, 2, 3 ... in order */
    u8 kind;
    u8 pad[3];
} VrEvent;
#pragma pack(pop)

void vr_world_request(void);
void vr_world_build(void);           /* engine thread: builds when requested */
int  vr_world_serial(void);
void vr_world_counts(int *nmesh, int *ntri, int *nline, int *ninst);
int  vr_world_copy(VrMesh *m, int cm, VrTri *t, int ct, VrLine *l, int cl, VrInst *in, int ci);
void vr_camera_capture(void);        /* engine thread, in VGAME */
void vr_camera_get(VrCamera *out);
void vr_palette_get(u8 *rgb768);
int  vr_objects_get(VrObj *out, int cap);                /* the latest frame's objects; returns how many */
int  vr_frame_get(VrCamera *c, VrObj *out, int cap);     /* the camera and the objects of one frame together; returns how many objects */
int  vr_world_live(VrInst *in, int ci, int known);       /* the instances' live state (mesh, dead) when it is not `known`; returns its serial */
int  vr_events_get(VrEvent *out, int cap, int after);    /* the explosions after id `after` (the last 32 at most); returns how many */
