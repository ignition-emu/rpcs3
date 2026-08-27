// The ABI Ignition drives RPCS3 through. C, opaque, versioned -- the only
// surface the host links, so RPCS3's C++ and its GPL-2.0-only code stay behind
// it and never combine with anything on the host side. Implemented by the
// Qt-free ignition frontend that links rpcs3_emu; consumed by the Ignition host
// binding. See docs/ps3-rpcs3-embed.md in the Ignition repo.
#ifndef IGNITION_PS3_H
#define IGNITION_PS3_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// The build compiles with -fvisibility=hidden, so each ABI entry point is
// marked default-visible or the host could not dlsym it.
#if defined(_WIN32)
#define IGNITION_PS3_API __declspec(dllexport)
#else
#define IGNITION_PS3_API __attribute__((visibility("default")))
#endif

// Bumped whenever this header changes shape. The host refuses a module whose
// version it does not know, the same guard libretro's API version gives.
#define IGNITION_PS3_ABI_VERSION 1
IGNITION_PS3_API uint32_t ignition_ps3_abi_version(void);

typedef struct ignition_ps3 ignition_ps3;

// Mirrors RPCS3's system_state, narrowed to what the host acts on.
typedef enum {
    IGNITION_PS3_STOPPED = 0,
    IGNITION_PS3_LOADING = 1,
    IGNITION_PS3_RUNNING = 2,
    IGNITION_PS3_PAUSED  = 3,
} ignition_ps3_state;

// Zero is success; non-zero mirrors game_boot_result so the host can report why.
typedef int32_t ignition_ps3_boot_result;

// Where RPCS3 keeps the state it insists on owning. Set once before boot.
typedef struct {
    const char* config_dir; // g_cfg / per-title config
    const char* cache_dir;  // shader and PPU/SPU caches
    const char* fw_dir;     // installed firmware (dev_flash)
    const char* hdd_dir;    // dev_hdd0, saves
} ignition_ps3_dirs;

// One video frame as RSX presented it, owned by the module until the next take.
typedef struct {
    const uint8_t* data;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    int32_t  is_bgra; // RSX presents either order; the host converts.
} ignition_ps3_frame;

// One controller, as the host sampled it. Buttons are a bitmask in RPCS3's own
// cell-pad order; sticks are signed, centred at zero.
typedef struct {
    uint32_t buttons;
    int16_t  lx, ly, rx, ry;
} ignition_ps3_pad;

// --- lifecycle -------------------------------------------------------------

// Builds Emu and installs the ignition EmuCallbacks. Null on failure.
IGNITION_PS3_API ignition_ps3* ignition_ps3_create(const ignition_ps3_dirs* dirs);
IGNITION_PS3_API void          ignition_ps3_destroy(ignition_ps3*);

// Boots a title (disc dir, ELF, or PKG path). Emulation threads start on their
// own; the host drives them with the pump below.
IGNITION_PS3_API ignition_ps3_boot_result ignition_ps3_boot(ignition_ps3*, const char* game_path);

IGNITION_PS3_API ignition_ps3_state ignition_ps3_state_of(const ignition_ps3*);
IGNITION_PS3_API void ignition_ps3_pause(ignition_ps3*);
IGNITION_PS3_API void ignition_ps3_resume(ignition_ps3*);
IGNITION_PS3_API void ignition_ps3_stop(ignition_ps3*);

// --- the tick --------------------------------------------------------------

// Services one batch of call_from_main_thread work. Called once per host tick;
// this is the frame gate's stand-in, since RPCS3 has no frame-step. Returns the
// number of work items run, for diagnostics.
IGNITION_PS3_API uint32_t ignition_ps3_pump(ignition_ps3*);

// Takes the newest presented frame, if one arrived since the last take. Returns
// 1 and fills `out` (valid until the next take), or 0 if nothing new.
IGNITION_PS3_API int32_t ignition_ps3_take_frame(ignition_ps3*, ignition_ps3_frame* out);

// The pad the emulator reads until replaced. Set once per tick, like the
// libretro host's input snapshot.
IGNITION_PS3_API void ignition_ps3_set_pad(ignition_ps3*, uint32_t port, const ignition_ps3_pad*);

// Drains queued audio into `dst` (interleaved stereo s16), up to `max_frames`;
// returns frames written. RPCS3 fills it from its own audio thread.
IGNITION_PS3_API size_t ignition_ps3_read_audio(ignition_ps3*, int16_t* dst, size_t max_frames);

// The core's declared audio rate, once known (0 before boot completes).
IGNITION_PS3_API uint32_t ignition_ps3_audio_rate(const ignition_ps3*);

// --- state -----------------------------------------------------------------

// RPCS3 states are stop-serialize-reboot, not a step; both block. Zero on
// success.
IGNITION_PS3_API int32_t ignition_ps3_save_state(ignition_ps3*, const char* path);
IGNITION_PS3_API int32_t ignition_ps3_load_state(ignition_ps3*, const char* path);

#ifdef __cplusplus
}
#endif

#endif // IGNITION_PS3_H
