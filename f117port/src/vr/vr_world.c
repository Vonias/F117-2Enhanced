/* VR: the 3D world for the headset's own renderer (Route 2). Instead of the 320x200 picture, the app draws the game's own 3DX
 * models on the GPU in stereo. This file turns VGAME's live data into flat arrays the app copies (f117vr.h):
 *   - meshes: a 3DX model (spec render3d.md Part 4, port r3d_object.c) as triangles in model axes (east, up, -north = StereoKit's
 *     x, y, z), colour words unresolved (the app maps them through the live DAC), conditions kept (dead / lights variables);
 *   - instances: every scenery record of every tile of the four terrain levels (r3d_draw_terrain 04DA), in world units;
 *   - the camera: the cockpit eye (view_frame 2F82 view 0) and its basis, from r3d_matrix_from_angles E26B as 03CE builds it.
 * Everything here only reads mem[]; it runs on the engine thread from host_pump, never in a display pass. */
#include "vr/vr_world.h"

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __ANDROID__
#include <android/log.h>
#define VLOG(...) __android_log_print(ANDROID_LOG_INFO, "F117A-VR", __VA_ARGS__)
#else
#include <stdio.h>
#define VLOG(...) (fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))
#endif

#include "mem.h"
#include "platform/vga.h"
#include "render3d/r3d.h"

/* ---------------------------------------------------------------- growable arrays */

typedef struct { void *p; int n, cap, size; } Vec;
static void *vec_push(Vec *v)
{
    if (v->n == v->cap) {
        int cap = v->cap ? v->cap * 2 : 256;
        void *q = realloc(v->p, (size_t)cap * (size_t)v->size);
        if (!q) return NULL;
        v->p = q; v->cap = cap;
    }
    return (char *)v->p + (size_t)v->size * (size_t)v->n++;
}
static void vec_clear(Vec *v) { v->n = 0; }

static Vec meshes = { 0, 0, 0, sizeof(VrMesh) }, tris = { 0, 0, 0, sizeof(VrTri) }, lines = { 0, 0, 0, sizeof(VrLine) },
           insts = { 0, 0, 0, sizeof(VrInst) };
static pthread_mutex_t world_lock = PTHREAD_MUTEX_INITIALIZER;
static int world_serial;
static volatile int world_requested;

/* ---------------------------------------------------------------- one 3DX model -> triangles */

#define EW(o) rd16(seg, (u16)(o))
#define ES(o) ((s16)rd16(seg, (u16)(o)))
#define EB(o) rd8(seg, (u16)(o))

typedef struct { float n[3]; u8 cv; s8 cval; bool has; } Plane;

static void vsub(const float *a, const float *b, float *o) { o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2]; }
static void vcross(const float *a, const float *b, float *o) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }
static float vdot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

/* A polygon is a set of edges (the rasteriser fills it by rows, so it is convex). Chains the edges into a vertex loop; when they do
 * not close (a stale or duplicated edge), orders the distinct vertices by angle around their centre in the polygon's plane. */
static int poly_loop(const u8 (*edge)[2], int nedges, const u8 *eidx, int n, u8 *loop)
{
    int k = 0;
    bool used[32] = { false };
    loop[k++] = edge[eidx[0]][0];
    u8 cur = edge[eidx[0]][1];
    used[0] = true;
    for (int step = 1; step < n; step++) {
        int found = -1;
        for (int j = 1; j < n; j++) {
            if (used[j] || eidx[j] >= nedges) continue;
            if (edge[eidx[j]][0] == cur || edge[eidx[j]][1] == cur) { found = j; break; }
        }
        if (found < 0) return -1;
        used[found] = true;
        loop[k++] = cur;
        cur = edge[eidx[found]][0] == cur ? edge[eidx[found]][1] : edge[eidx[found]][0];
    }
    return cur == loop[0] ? k : -1;
}

static int poly_sorted(const float (*vert)[3], const u8 (*edge)[2], const u8 *eidx, int n, const float *nrm, u8 *loop)
{
    int k = 0;
    for (int j = 0; j < n; j++)
        for (int e = 0; e < 2; e++) {
            u8 v = edge[eidx[j]][e];
            bool dup = false;
            for (int i = 0; i < k; i++) dup |= loop[i] == v;
            if (!dup && k < 32) loop[k++] = v;
        }
    if (k < 3) return k;
    float c[3] = { 0, 0, 0 }, u[3], w[3], ang[32];
    for (int i = 0; i < k; i++) for (int a = 0; a < 3; a++) c[a] += vert[loop[i]][a] / (float)k;
    vsub(vert[loop[0]], c, u);
    vcross(nrm, u, w);
    for (int i = 0; i < k; i++) {
        float d[3];
        vsub(vert[loop[i]], c, d);
        ang[i] = atan2f(vdot(d, w), vdot(d, u));
    }
    for (int i = 1; i < k; i++)                                           /* insertion sort by angle */
        for (int j = i; j > 0 && ang[j] < ang[j - 1]; j--) {
            float t = ang[j]; ang[j] = ang[j - 1]; ang[j - 1] = t;
            u8 s = loop[j]; loop[j] = loop[j - 1]; loop[j - 1] = s;
        }
    return k;
}

/* the face-list element stream of r3d_face_list (0CED), collecting instead of drawing */
static void face_list(u16 seg, u16 si, const float (*vert)[3], int nverts, const u8 (*edge)[2], int nedges,
                      const Plane *pl, VrMesh *m, u16 *order)
{
    u8 cv = 0xFF;
    s8 cval = 0;
    for (int guard = 0; guard < 4096; guard++) {
        u8 b = EB(si++);
        if (b == 0xFF) return;
        if (b & 0x80) { cv = b & 0x7F; cval = (s8)EB(si++); continue; }    /* the condition of the next element */
        if (b & 0x20) { si = (u16)(si + 1 + (b & 0x1F)); cv = 0xFF; continue; }
        if (b & 0x40) {
            u16 n = b & 0x1F;
            u16 col = EB(si++);
            u16 colour = DSW((u16)(DSW(0x7D50) + 2 * col));
            for (u16 i = 0; i < n; i++) {
                u8 e = EB(si++);
                if (e >= nedges || edge[e][0] >= nverts || edge[e][1] >= nverts) continue;
                VrLine *l = vec_push(&lines);
                if (!l) return;
                memcpy(l->p, vert[edge[e][0]], sizeof(float) * 3);
                memcpy(l->p + 3, vert[edge[e][1]], sizeof(float) * 3);
                l->colour = colour; l->cv = cv; l->cval = cval; l->pad = 0;
                m->nline++;
            }
            cv = 0xFF;
            continue;
        }
        u16 n = b;
        if (n == 0 || n > 31) return;                                     /* not a polygon head: a stream this reader does not know */
        u8 eidx[32];
        for (u16 i = 0; i < n; i++) eidx[i] = EB(si + i);
        u8 col = EB(si + n);
        si = (u16)(si + n + 1);
        if (col == 0xFF) { cv = 0xFF; continue; }                         /* 0D04: colour FFh is skipped */
        u16 colour = DSW((u16)(DSW(0x7D50) + 2 * col));
        bool bad = false;
        for (u16 i = 0; i < n; i++) bad |= eidx[i] >= nedges || edge[eidx[i]][0] >= nverts || edge[eidx[i]][1] >= nverts;
        if (bad) { cv = 0xFF; continue; }

        float nrm[3] = { 0, 1, 0 };
        if (pl && pl->has) memcpy(nrm, pl->n, sizeof nrm);
        u8 loop[32];
        int k = n >= 3 ? poly_loop(edge, nedges, eidx, n, loop) : -1;
        if (k < 3) k = poly_sorted(vert, edge, eidx, n, nrm, loop);
        if (k < 3) { cv = 0xFF; continue; }
        if (!(pl && pl->has)) {                                           /* no plane: the polygon's own normal (Newell) */
            nrm[0] = nrm[1] = nrm[2] = 0;
            for (int i = 0; i < k; i++) {
                const float *a = vert[loop[i]], *c = vert[loop[(i + 1) % k]];
                nrm[0] += (a[1] - c[1]) * (a[2] + c[2]);
                nrm[1] += (a[2] - c[2]) * (a[0] + c[0]);
                nrm[2] += (a[0] - c[0]) * (a[1] + c[1]);
            }
        }
        for (int i = 1; i + 1 < k; i++) {                                 /* fan */
            VrTri *t = vec_push(&tris);
            if (!t) return;
            const float *a = vert[loop[0]], *b2 = vert[loop[i]], *c = vert[loop[i + 1]];
            float e1[3], e2[3], tn[3];
            vsub(b2, a, e1); vsub(c, a, e2); vcross(e1, e2, tn);
            bool flip = vdot(tn, nrm) < 0;                                /* wind so the face looks along the plane normal */
            memcpy(t->p, a, sizeof(float) * 3);
            memcpy(t->p + 3, flip ? c : b2, sizeof(float) * 3);
            memcpy(t->p + 6, flip ? b2 : c, sizeof(float) * 3);
            t->colour = colour;
            t->order = (*order);
            t->flags = (pl && pl->has) ? 0 : VRTRI_DOUBLE;
            t->cv1 = cv; t->cval1 = cval;
            t->cv2 = pl ? pl->cv : 0xFF; t->cval2 = pl ? pl->cval : 0;
            t->pad[0] = t->pad[1] = t->pad[2] = 0;
            m->ntri++;
        }
        (*order)++;
        cv = 0xFF;
    }
}

/* the model of bank / idx at its most detailed LOD; returns false when it is not a model this reader can walk */
static bool model_to_mesh(u16 bank, u16 idx, VrMesh *m)
{
    u16 seg = DSW((u16)(0xB75E + 2 * bank));
    if (seg == 0 || idx >= EW(0)) return false;
    u16 si = EW(2 * idx + 2);
    u16 mdl = EW(si + 2);                                                 /* the first LOD entry: the nearest, most detailed */
    si = mdl;
    m->radius = (float)EW(si);
    si = (u16)(si + 2);
    s16 nparts = ES(si);
    si = (u16)(si + 2);
    if (nparts > 1) si = (u16)(si + 2 * (nparts - 1) + 6 * (nparts - 1));   /* var/axis pairs, pivots (all angles 0: parts stay in place) */
    s16 nverts = ES(si);
    si = (u16)(si + 2);
    if (nverts < 0 || nverts > 255) return false;                         /* the shared-vertex path: count 0 in all shipped banks */

    s16 np = ES(si);
    si = (u16)(si + 2);
    u16 anp = (u16)(np < 0 ? -np : np);
    if (anp > 255) return false;
    Plane *pl = calloc(anp ? anp : 1, sizeof(Plane));
    if (!pl) return false;
    for (u16 p = 0; p < anp; p++) {
        pl[p].cv = 0xFF;
        for (int guard = 0; guard < 64; guard++) {
            u16 w = EW(si);
            si = (u16)(si + 2);
            if ((w >> 8) == 0x80) continue;                               /* part switch */
            if (w == 0x8100) { pl[p].cv = EB(si) & 0x1F; pl[p].cval = (s8)EB(si + 1); si = (u16)(si + 2); continue; }
            pl[p].n[0] = (float)(s16)w; pl[p].n[1] = (float)ES(si); pl[p].n[2] = (float)ES(si + 2);
            pl[p].has = true;
            si = (u16)(si + 8);
            break;
        }
    }
    for (u16 p = 0; p < anp; p++) si = (u16)(si + 1 + EB(si));            /* the vertex use lists */
    if (np < 0) si = (u16)(si + 2 * anp + 1);                              /* BSP pairs: the GPU's depth buffer replaces the order */

    float (*vert)[3] = calloc(nverts ? (size_t)nverts : 1, sizeof *vert);
    if (!vert) { free(pl); return false; }
    for (s16 i = 0; i < nverts; i++) {
        while (EW(si) >= 0x8000 && EW(si) < 0x8010) si = (u16)(si + 2);    /* part words */
        vert[i][0] = (float)ES(si); vert[i][1] = (float)ES(si + 2); vert[i][2] = (float)ES(si + 4);
        si = (u16)(si + 6);
    }
    u16 nedges = EW(si);
    si = (u16)(si + 2);
    if (nedges > 1024) { free(vert); free(pl); return false; }
    u8 (*edge)[2] = calloc(nedges ? nedges : 1, sizeof *edge);
    if (!edge) { free(vert); free(pl); return false; }
    for (u16 e = 0; e < nedges; e++) { edge[e][0] = EB(si); edge[e][1] = EB(si + 1); si = (u16)(si + 2); }

    m->first_tri = tris.n; m->ntri = 0; m->first_line = lines.n; m->nline = 0;
    u16 order = 0;
    if (np == 0) face_list(seg, si, (const float (*)[3])vert, nverts, (const u8 (*)[2])edge, nedges, NULL, m, &order);
    else for (u16 p = 0; p < anp; p++)
        face_list(seg, EW(si + 2 * p), (const float (*)[3])vert, nverts, (const u8 (*)[2])edge, nedges, &pl[p], m, &order);
    free(edge); free(vert); free(pl);
    return true;
}

/* ---------------------------------------------------------------- the terrain's scenery instances */

static int mesh_of[2][128];                                               /* (bank, idx) -> mesh index, -1 = none / unreadable */

static int mesh_index(u16 type)
{
    u16 bank = (u16)((type & 0x7FFF) >> 8), idx = type & 0x7F;
    if (bank > 1) return -1;
    if (mesh_of[bank][idx] != -2) return mesh_of[bank][idx];
    VrMesh m = { 0 };
    m.key = (s32)(bank << 8 | idx);
    m.cls = DSB((u16)(DSW((u16)(0x0A60 + 2 * bank)) + idx)) & 7;          /* r3d_place_object's class */
    if (!model_to_mesh(bank, idx, &m)) { mesh_of[bank][idx] = -1; return -1; }
    VrMesh *slot = vec_push(&meshes);
    if (!slot) return -1;
    *slot = m;
    return mesh_of[bank][idx] = meshes.n - 1;
}

/* Where each instance came from, so its state can be looked up again during the mission (live destruction) */
typedef struct { s16 L, tx, ty, j; u8 ob, pad; } InstSrc;
static Vec srcs = { 0, 0, 0, sizeof(InstSrc) };

/* A scenery record's object as r3d_draw_terrain resolves it: the type (r3d_scenery_id), and whether it is the destroyed variant */
static u16 scenery_resolve(s16 L, s16 j, s16 tx, s16 ty, u8 ob, bool *dead)
{
    u16 obj;
    *dead = false;
    if (!(ob & 0x80)) obj = ob;
    else {
        u16 ov = terr_scenery_override(L, j, tx, ty);
        if (ov == 0) obj = ob & 0x7F;
        else if (((u8)ov ^ ob) & 0x7F) obj = ov;
        else { obj = ov; *dead = true; }
    }
    u16 type = r3d_scenery_id((u16)((obj & 0x7FFF) | 0x0100));
    if (type & 0x8000) *dead = true;
    return type;
}

static void walk_terrain(void)
{
    u16 save94dc = DSW(0x94DC);                                            /* terr_scenery_override's loop variable */
    vec_clear(&srcs);
    for (s16 L = 1; L <= 4; L++) {
        if (DSW((u16)(0x04FA + 2 * L)) == 0) continue;
        s16 dim = DSS((u16)(0x0506 + 2 * L));
        double scale = (double)(1 << (2 * (L - 1)));                      /* level-L units in world units (terr_to_level) */
        s16 lo = L == 4 ? -2 : 0, hi = (s16)(L == 4 ? dim - 2 : dim);
        for (s16 ty = lo; ty < hi; ty++)
            for (s16 tx = lo; tx < hi; tx++) {
                u16 tile = terr_tile_type(L, tx, ty);
                if (tile == 0xFFFF) continue;
                u16 idx = (u16)(L * 32 + tile);
                u16 p = DSW((u16)(0xE304 + 2 * idx)), cnt = DSW((u16)(0x061E + 2 * idx));
                for (u16 j = 0; j < cnt && j < 512; j++, p = (u16)(p + 7)) {
                    u8 ob = DSB((u16)(p + 6));
                    bool dead;
                    u16 type = scenery_resolve(L, (s16)j, tx, ty, ob, &dead);
                    int mi = mesh_index(type);
                    if (mi < 0 && !(ob & 0x80)) continue;                  /* a destroyable one is kept: it can become drawable */
                    VrInst *in = vec_push(&insts);
                    if (!in) break;
                    InstSrc *src = vec_push(&srcs);
                    if (!src) { insts.n--; break; }
                    *src = (InstSrc){ L, tx, ty, (s16)j, ob, 0 };
                    in->x = ((double)tx * 4096.0 + 2048.0 + (double)DSS(p)) * scale;
                    in->y = ((double)ty * 4096.0 + 2048.0 + (double)DSS((u16)(p + 2))) * scale;
                    in->z = (double)DSS((u16)(p + 4)) * scale;
                    in->scale = (float)scale;
                    in->mesh = mi;
                    in->level = (u8)L;
                    in->dead = dead;
                    in->pad[0] = in->pad[1] = 0;
                }
            }
    }
    DSW(0x94DC) = save94dc;
}

/* ---------------------------------------------------------------- live destruction */

/* The scenery overrides (5-byte entries at B772, count [9902]: level, record, cell x, y, the new object) change when something is
 * destroyed (fl_damage.c terr_set_scenery_state); then every destroyable instance is resolved again. live_serial counts the changes
 * (high half the world serial, so a new world never matches an old state). */
static u32 live_hash;
static int live_serial;

static u32 override_hash(void)
{
    u16 n = DSW(0x9902);
    u32 h = 2166136261u ^ n;
    for (u16 i = 0; i < n && i < 512; i++)
        for (int b = 0; b < 5; b++) h = (h ^ DSB((u16)(0xB772 + 5 * i + b))) * 16777619u;
    return h;
}

static void live_refresh(void)
{
    u32 h = override_hash();
    if (h == live_hash) return;
    live_hash = h;
    u16 save94dc = DSW(0x94DC);
    pthread_mutex_lock(&world_lock);
    int changed = 0;
    for (int i = 0; i < srcs.n; i++) {
        InstSrc *s = &((InstSrc *)srcs.p)[i];
        if (!(s->ob & 0x80)) continue;
        bool dead;
        u16 type = scenery_resolve(s->L, s->j, s->tx, s->ty, s->ob, &dead);
        u16 bank = (u16)((type & 0x7FFF) >> 8), idx = type & 0x7F;
        int mi = bank <= 1 && mesh_of[bank][idx] >= 0 ? mesh_of[bank][idx] : -1;   /* only meshes the app already has */
        VrInst *in = &((VrInst *)insts.p)[i];
        if (in->mesh != mi || in->dead != (u8)dead) { in->mesh = mi; in->dead = (u8)dead; changed++; }
    }
    live_serial++;
    pthread_mutex_unlock(&world_lock);
    DSW(0x94DC) = save94dc;
    VLOG("live: %d overrides, %d instances changed", DSW(0x9902), changed);
}

int vr_world_live(VrInst *in, int ci, int known)
{
    pthread_mutex_lock(&world_lock);
    int s = live_serial;
    if (s != known && ci >= insts.n && insts.n > 0) memcpy(in, insts.p, sizeof(VrInst) * (size_t)insts.n);
    else if (s != known) s = 0;
    pthread_mutex_unlock(&world_lock);
    return s;
}

/* ---------------------------------------------------------------- explosions */

/* The original's one explosion (sim_objovl.c S3): a place C0A0 / C0AA / C0B0 (site units, the altitude in feet) and a counter
 * [9D0C] of frames, its sign the cause: FFFAh a weapon's impact, FFF0h a crash, 10h a hit (a unit or an object destroyed; a ground
 * object with the altitude 0). A new one starts when the counter appears or grows. */
#define VREV_RING 32
static VrEvent events[VREV_RING];
static int event_id;
static s16 last_count;

static void events_capture(void)
{
    s16 c = (s16)DSW(0x9D0C);
    int mag = c < 0 ? -c : c, last = last_count < 0 ? -last_count : last_count;
    last_count = c;
    if (c == 0 || mag <= last) return;
    pthread_mutex_lock(&world_lock);
    VrEvent *e = &events[event_id % VREV_RING];
    e->x = (double)((u32)DSW(0xC0A0) << 5);
    e->y = (double)(s32)(0x01000000u - ((u32)DSW(0xC0AA) << 5));
    e->z = (double)(s16)DSW(0xC0B0);
    e->kind = c == (s16)0xFFFA ? VREV_IMPACT : c < 0 ? VREV_CRASH : VREV_HIT;
    e->pad[0] = e->pad[1] = e->pad[2] = 0;
    e->id = ++event_id;
    pthread_mutex_unlock(&world_lock);
}

int vr_events_get(VrEvent *out, int cap, int after)
{
    pthread_mutex_lock(&world_lock);
    int n = 0;
    int first = event_id - VREV_RING + 1 > after + 1 ? event_id - VREV_RING + 1 : after + 1;
    for (int id = first; id <= event_id && n < cap; id++) out[n++] = events[(id - 1) % VREV_RING];
    pthread_mutex_unlock(&world_lock);
    return n;
}

void vr_world_request(void) { world_requested = 1; }

void vr_world_build(void)
{
    if (!world_requested) return;
    world_requested = 0;
    u16 save7d50 = DSW(0x7D50);
    DSW(0x7D50) = DSW(0x98DE) == 3 ? 0x7F60 : 0x8360;                     /* the colour table 03CE draws the main view with */
    pthread_mutex_lock(&world_lock);
    vec_clear(&meshes); vec_clear(&tris); vec_clear(&lines); vec_clear(&insts);
    for (int b = 0; b < 2; b++) for (int i = 0; i < 128; i++) mesh_of[b][i] = -2;
    walk_terrain();
    for (int i = 0; i < 128; i++) mesh_index((u16)i);                      /* bank 0 too: the aircraft, vehicles and weapons (step 5) */
    for (int i = 0; i < 128; i++) mesh_index((u16)(0x100 | i));            /* and all of bank 1: the wrecks a destruction swaps in */
    world_serial++;
    live_hash = override_hash();
    live_serial = world_serial << 16;
    pthread_mutex_unlock(&world_lock);
    DSW(0x7D50) = save7d50;
    float rmin = 1e9f, rmax = 0;
    for (int i = 0; i < meshes.n; i++) { float r = ((VrMesh *)meshes.p)[i].radius; if (r < rmin) rmin = r; if (r > rmax) rmax = r; }
    VLOG("world %d: %d meshes (radius %.0f..%.0f), %d triangles, %d lines, %d instances", world_serial, meshes.n, rmin, rmax,
         tris.n, lines.n, insts.n);
    int per[5] = { 0 };
    for (int i = 0; i < insts.n; i++) per[((VrInst *)insts.p)[i].level]++;
    VLOG("instances per level: L1 %d, L2 %d, L3 %d, L4 %d", per[1], per[2], per[3], per[4]);
    {   /* diagnostics: the fill modes and the most used colour words, and the backdrop band colours */
        int hist[6] = { 0 };            /* solid, ramp, dither, stipple FC, OR FD / AND FE, none FB */
        u16 words[64]; int counts[64], nw = 0;
        for (int i = 0; i < tris.n; i++) {
            u16 c = ((VrTri *)tris.p)[i].colour;
            u8 hi = (u8)(c >> 8), lo = (u8)c;
            hist[hi == 0xFF ? (lo >= 0xA0 ? 1 : 0) : hi < 0xFB ? 2 : hi == 0xFC ? 3 : hi == 0xFB ? 5 : 4]++;
            int k = 0;
            while (k < nw && words[k] != c) k++;
            if (k == nw && nw < 64) { words[nw] = c; counts[nw++] = 0; }
            if (k < 64) counts[k]++;
        }
        VLOG("fills: solid %d, ramp %d, dither %d, stipple %d, or/and %d, none %d", hist[0], hist[1], hist[2], hist[3], hist[4], hist[5]);
        char line[512]; int at = 0;
        for (int k = 0; k < nw && at < 480; k++) if (counts[k] > 40 || (words[k] >> 8) >= 0xFB && (words[k] >> 8) != 0xFF) at += snprintf(line + at, sizeof line - at, " %04X:%d", words[k], counts[k]);
        line[at] = 0;
        VLOG("colour words (>40 tris):%s", line);
        u8 pal[768]; vr_palette_get(pal);
        at = 0;
        for (int i = 0x70; i < 0x90; i += 3) at += snprintf(line + at, sizeof line - at, " %02X=%02X%02X%02X", i, pal[3*i], pal[3*i+1], pal[3*i+2]);
        for (int i = 0xF0; i < 0x100; i += 3) at += snprintf(line + at, sizeof line - at, " %02X=%02X%02X%02X", i, pal[3*i], pal[3*i+1], pal[3*i+2]);
        VLOG("bands:%s", line);
        VLOG("detail [98DE]=%d", DSS(0x98DE));
    }
    int m18 = mesh_of[0][0x18];
    if (m18 >= 0) VLOG("bank 0 model 18h (the player's aircraft): radius %.0f, %d triangles", ((VrMesh *)meshes.p)[m18].radius,
                       ((VrMesh *)meshes.p)[m18].ntri);
}

int vr_world_serial(void) { return world_serial; }

void vr_world_counts(int *nmesh, int *ntri, int *nline, int *ninst)
{
    pthread_mutex_lock(&world_lock);
    *nmesh = meshes.n; *ntri = tris.n; *nline = lines.n; *ninst = insts.n;
    pthread_mutex_unlock(&world_lock);
}

int vr_world_copy(VrMesh *m, int cm, VrTri *t, int ct, VrLine *l, int cl, VrInst *in, int ci)
{
    pthread_mutex_lock(&world_lock);
    int ok = cm >= meshes.n && ct >= tris.n && cl >= lines.n && ci >= insts.n;
    if (ok) {
        if (meshes.n) memcpy(m, meshes.p, sizeof(VrMesh) * (size_t)meshes.n);
        if (tris.n) memcpy(t, tris.p, sizeof(VrTri) * (size_t)tris.n);
        if (lines.n) memcpy(l, lines.p, sizeof(VrLine) * (size_t)lines.n);
        if (insts.n) memcpy(in, insts.p, sizeof(VrInst) * (size_t)insts.n);
    }
    int s = world_serial;
    pthread_mutex_unlock(&world_lock);
    return ok ? s : 0;
}

/* ---------------------------------------------------------------- camera, palette, variables */

static VrCamera cam;
static int objects_capture(void);
static VrObj objs[VROBJ_MAX], next_objs[VROBJ_MAX];   /* the published frame's objects, and the one being captured */
static int nobjs, nnext;
static pthread_mutex_t cam_lock = PTHREAD_MUTEX_INITIALIZER;

static double ang(u16 a) { return (double)a * (6.283185307179586 / 65536.0); }

/* The cockpit eye of view 0 (view_frame 2F82): the aircraft at BA10 / C02A, the eye 18h above 2DF4, heading / pitch / roll 2DEE..2DF2.
 * 03CE builds the cull matrix 498E with r3d_matrix_from_angles(heading, -pitch, -roll); its rows are the camera axes in world
 * (east, north, up): right = (m0, m6, m3), up = (m1, m7, m4), forward = (m2, m8, m5) (r3d_cull_core E8B5). */
void vr_camera_capture(void)
{
    double a = ang(DSW(0x2DEE)), b = ang((u16)-DSW(0x2DF0)), c = ang((u16)-DSW(0x2DF2));
    double s1 = sin(a), c1 = cos(a), s2 = sin(b), c2 = cos(b), s3 = sin(c), c3 = cos(c);
    double m[9];
    m[0] = c1 * c3 + s1 * s2 * s3;
    m[1] = s1 * s2 * c3 - c1 * s3;
    m[2] = s1 * c2;
    m[3] = s3 * c2;
    m[4] = c3 * c2;
    m[5] = -s2;
    m[6] = c1 * s2 * s3 - s1 * c3;
    m[7] = c1 * s2 * c3 + s3 * s1;
    m[8] = c1 * c2;
    VrCamera k;
    k.x = (double)DSSL(0xBA10);
    k.y = (double)DSSL(0xC02A);
    k.z = (double)(s16)DSW(0x2DF4) + 24.0;
    k.right[0] = (float)m[0]; k.right[1] = (float)m[6]; k.right[2] = (float)m[3];
    k.up[0] = (float)m[1];    k.up[1] = (float)m[7];    k.up[2] = (float)m[4];
    k.fwd[0] = (float)m[2];   k.fwd[1] = (float)m[8];   k.fwd[2] = (float)m[5];
    k.view = DSW(0xC07A);
    k.night = DSW(0x9874);
    k.throttle = DSW(0x2DFE);
    k.alt_ft = DSW(0x2DF6);
    k.speed = DSS(0xB168);
    k.heading = DSW(0x2DEE);
    k.night_mission = DSW(0x3658);
    k.pad2 = 0;
    events_capture();
    int n = 0;
    if (world_serial) { n = objects_capture(); live_refresh(); }   /* the meshes exist once the world was built */
    /* A new frame of the simulation when anything moved (this runs at every pump, many times a frame): the app draws between
     * the last two by their times (interpolation), so the world moves smoothly however slowly the engine draws. */
    bool moved = n != nobjs || (n > 0 && memcmp(next_objs, objs, sizeof(VrObj) * (size_t)n) != 0) ||
                 k.x != cam.x || k.y != cam.y || k.z != cam.z || memcmp(k.right, cam.right, sizeof k.right) != 0 ||
                 memcmp(k.up, cam.up, sizeof k.up) != 0 || memcmp(k.fwd, cam.fwd, sizeof k.fwd) != 0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    pthread_mutex_lock(&world_lock);
    pthread_mutex_lock(&cam_lock);
    k.serial = cam.serial + 1;
    if (moved) {
        k.frame = cam.frame + 1;
        k.time_ns = (s64)ts.tv_sec * 1000000000ll + (s64)ts.tv_nsec;
        if (n > 0) memcpy(objs, next_objs, sizeof(VrObj) * (size_t)n);
        nobjs = n;
    } else {
        k.frame = cam.frame;
        k.time_ns = cam.time_ns;
    }
    cam = k;
    pthread_mutex_unlock(&cam_lock);
    pthread_mutex_unlock(&world_lock);
}

/* ---------------------------------------------------------------- the moving objects */

/* r3d_place_object's object matrix: the transpose of mat_from_euler(a, b, c) (r3d_math.c 1433) for the angles r3d_draw_model
 * gives it (heading, -pitch, roll), as floats; rows = the images of the model axes in (east, up, south). */
static void obj_matrix(u16 a, u16 b, u16 c, float *r)
{
    double SA = sin(ang(a)), CA = cos(ang(a)), SB = sin(ang(b)), CB = cos(ang(b)), SC = sin(ang(c)), CC = cos(ang(c));
    double m[9];
    m[0] = CC * CA - SC * SB * SA;  m[1] = CC * SB * SA + SC * CA;  m[2] = -CB * SA;
    m[3] = -SC * CB;                m[4] = CC * CB;                 m[5] = SB;
    m[6] = SC * SB * CA + CC * SA;  m[7] = SC * SA - CC * SB * CA;  m[8] = CB * CA;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) r[3 * i + j] = (float)m[3 * j + i];
}

/* r3d_draw_model's frame: x as it is, y counted from the top (the terrain's y = 1000000h - y), the lod shift lod - 3 */
static void obj_add(u8 kind, u8 slot, u8 wpn, u16 model, s32 x, s32 y, s16 alt, u16 hdg, u16 pitch, u16 roll, int lod)
{
    if (nnext >= VROBJ_MAX) return;
    u16 type = r3d_scenery_id(model);
    u16 bank = (u16)((type & 0x7FFF) >> 8), idx = type & 0x7F;
    VrObj *o = &next_objs[nnext++];
    o->x = (double)x;
    o->y = (double)(s32)(0x01000000u - (u32)y);
    o->z = (double)alt;
    obj_matrix(hdg, (u16)-pitch, roll, o->rot);
    o->scale = (float)pow(2.0, -(lod - (DSB(0x294B) == 0 ? 3 : 2)));
    o->mesh = bank <= 1 && mesh_of[bank][idx] >= 0 ? mesh_of[bank][idx] : -1;
    o->kind = kind;
    o->wpn = wpn;
    o->slot = slot;
    o->pad = 0;
}

static int objects_capture(void)
{
    nnext = 0;
    const bool cockpit = !(DSB(0xC07A) & 0x80);
    /* (D) units: active (+16h bit 1), the near model of the type table 2FDC, lod 4 */
    for (s16 i = 0; i < DSS(0xDED0) && i < 64; i++) {
        u16 U = (u16)(0xC140 + 0x24 * i);
        if (!(DSB((u16)(U + 0x16)) & 2)) continue;
        s16 alt = DSS((u16)(U + 4));
        if (cockpit) alt = (s16)(alt - 0x10);
        obj_add(0, (u8)i, 0, DSW((u16)(0x2FDC + 0x20 * DSW((u16)(U + 0x14)))), DSSL((u16)(U + 6)), DSSL((u16)(U + 0xA)), alt,
                DSW((u16)(U + 0xE)), DSW((u16)(U + 0x10)), DSW((u16)(U + 0x12)), 4);
    }
    /* (E) projectile slots: positions in 32-unit steps, the model of the weapon table 3372, lod 3 */
    for (int i = 0; i < 12; i++) {
        u16 W = (u16)(0x3C0C + 0x1C * i);
        if (DSW((u16)(W + 0xE)) == 0) continue;
        obj_add(1, (u8)(64 + i), (u8)DSW((u16)(W + 0x10)), DSW((u16)(0x3372 + 0x12 * DSW((u16)(W + 0x10)))), (s32)((u32)DSW(W) << 5), (s32)((u32)DSW((u16)(W + 2)) << 5),
                DSS((u16)(W + 4)), DSW((u16)(W + 8)), DSW((u16)(W + 0xA)), (u16)(DSW((u16)(W + 0xC)) + 0x2000), 3);
    }
    /* (F) the falling wreck */
    if (DSS(0xC602) > 0)
        obj_add(2, 127, 0, 0x0B, (s32)((u32)DSW(0xC0A6) << 5), (s32)((u32)DSW(0xC0B8) << 5), DSS(0xC602), 0, 0, 0, DSS(0xB756) > 0 ? 6 : 5);
    return nnext;
}

int vr_frame_get(VrCamera *c, VrObj *out, int cap)
{
    pthread_mutex_lock(&world_lock);
    pthread_mutex_lock(&cam_lock);
    *c = cam;
    int n = nobjs < cap ? nobjs : cap;
    if (n > 0) memcpy(out, objs, sizeof(VrObj) * (size_t)n);
    pthread_mutex_unlock(&cam_lock);
    pthread_mutex_unlock(&world_lock);
    return n;
}

int vr_objects_get(VrObj *out, int cap)
{
    pthread_mutex_lock(&world_lock);
    int n = nobjs < cap ? nobjs : cap;
    if (n > 0) memcpy(out, objs, sizeof(VrObj) * (size_t)n);
    pthread_mutex_unlock(&world_lock);
    return n;
}

void vr_camera_get(VrCamera *out)
{
    pthread_mutex_lock(&cam_lock);
    *out = cam;
    pthread_mutex_unlock(&cam_lock);
}

void vr_palette_get(u8 *rgb768)
{
    for (int i = 0; i < 256; i++) {
        u8 r, g, b;
        vga_dac_get((u8)i, &r, &g, &b);
        rgb768[3 * i] = (u8)(r << 2 | r >> 4);
        rgb768[3 * i + 1] = (u8)(g << 2 | g >> 4);
        rgb768[3 * i + 2] = (u8)(b << 2 | b >> 4);
    }
}
