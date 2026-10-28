/* VR: the headset's flight model flies the player's aircraft. The app runs JSBSim at 120 Hz (VrFlight.cs) and draws the eye from it every
 * headset frame; each simulation frame the game takes its latest state through the outside-model hook of 1E9E (flight/fl.h FlExt), in place of
 * its own stick, lift and turn. Back go the game's throttle, gear, flaps, brakes, fuel, autopilot and the ground's height, and a new start
 * whenever the game puts the aircraft somewhere itself (a new mission, a jump). Runs on the engine thread inside sim_input_flight. */
#include "vr/vr_fdm.h"

#include <math.h>
#include <pthread.h>
#include <string.h>

#include "mem.h"
#include "flight/fl.h"

static pthread_mutex_t fdm_lock = PTHREAD_MUTEX_INITIALIZER;
static VrFdmState st;
static bool have_state, enabled;
static VrFdmGame game;
static bool applied;                 /* the last frame was the outside model's */
static s32 wrote_x, wrote_y;         /* what it put in BA10 / C02A */
static u16 last_b052;

static u16 angle(float rad)
{
    double t = fmod((double)rad / 6.283185307179586, 1.0);
    if (t < 0) t += 1.0;
    return (u16)(s32)(t * 65536.0 + 0.5);
}

static s16 sat16(double v) { return (s16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

static bool hook(FlExt *s)
{
    pthread_mutex_lock(&fdm_lock);
    game.frame++;
    /* something other than the outside model moved the aircraft (a new mission's place, the Alt-I/J/K/L jumps): start over there */
    if (applied && ((s32)DSL(0xBA10) != wrote_x || (s32)DSL(0xC02A) != wrote_y)) game.start++;
    if (DSW(0xB052) == 0x546 && last_b052 != 0x546) game.catapult++;   /* flight_keys: the brakes off at full throttle on a deck */
    game.x = (double)(s32)DSL(0xBA10);
    game.y = (double)(s32)DSL(0xC02A);
    game.alt = DSW(0x2DF6);
    game.ground = DSW(0xC0A2);
    game.heading = DSW(0x2DEE);
    game.pitch = DSW(0x2DF0);
    game.roll = DSW(0x2DF2);
    game.speed = (s16)DSW(0xB168);
    game.throttle = DSW(0x2DFE);
    game.fuel = (s16)DSW(0x3634);
    game.ap_alt = DSW(0x3652);
    game.ap_heading = DSW(0xC078);
    game.gear_down = !(DSB(0x9B04) & 1);
    game.brakes = (DSB(0x9B04) & 8) != 0;
    game.flaps = (DSB(0x9B04) & 2) != 0;
    game.ejecting = DSW(0xC06E) != 0;
    game.time_accel = (u8)DSW(0x3D7A);
    bool use = enabled && have_state && st.start == game.start;
    VrFdmState k = st;
    pthread_mutex_unlock(&fdm_lock);

    applied = use;
    if (!use) { last_b052 = DSW(0xB052); return false; }
    s->x = (s32)llround(k.x);
    s->y = (s32)llround(k.y);
    double alt = k.alt_ft < 0 ? 0 : k.alt_ft > 0xEA60 ? 0xEA60 : k.alt_ft;
    s->alt = (u16)(alt + 0.5);
    s->heading = angle(k.heading);
    s->pitch = angle(k.pitch);
    s->roll = angle(k.roll);
    s->speed = sat16(k.kts + 0.5);
    s->g16 = sat16(k.nz * 16.0);
    s->vs = sat16(k.vs_fps);
    s->aoa = (s16)angle(k.aoa);
    s->stall = sat16(k.stall_kts);
    s->on_ground = k.on_ground != 0;
    s->stick = k.stick != 0;
    wrote_x = s->x;
    wrote_y = s->y;
    last_b052 = (u16)(s->speed > 0 ? s->speed : 0) * 27;
    return true;
}

void vr_fdm_enable(int on)
{
    pthread_mutex_lock(&fdm_lock);
    if (on && !enabled) game.start++;            /* switched on: start from where the game has it now */
    enabled = on != 0;
    fl_ext_fm = hook;
    pthread_mutex_unlock(&fdm_lock);
}

void vr_fdm_set(const VrFdmState *s)
{
    pthread_mutex_lock(&fdm_lock);
    st = *s;
    have_state = true;
    pthread_mutex_unlock(&fdm_lock);
}

void vr_fdm_game(VrFdmGame *g)
{
    pthread_mutex_lock(&fdm_lock);
    *g = game;
    pthread_mutex_unlock(&fdm_lock);
}
