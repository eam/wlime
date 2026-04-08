#pragma once

#include <wayland-client.h>
#include <gtk/gtk.h>
#include <gtk-layer-shell.h>
#include <xkbcommon/xkbcommon.h>
#include <cairo.h>

#include <string>
#include <vector>

#include "config.h"
#include "sound.h"

// Forward declaration
class InputEngine;
struct Candidate;

// Forward declarations for generated protocol types
struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;
struct zwp_input_method_manager_v2;
struct zwp_input_method_v2;
struct zwp_input_method_keyboard_grab_v2;
struct zwp_virtual_keyboard_manager_v1;
struct zwp_virtual_keyboard_v1;

// --- Overlay ---

// --- Particles ---

struct Particle {
    double x, y;
    double vx, vy;
    double life;        // 0.0 = dead, 1.0 = just born
    double decay;       // life lost per tick
    double size;
    double hue;         // 0-360 for rainbow
    int style;          // 0 = dot, 1 = star, 2 = sparkle
};

#define MAX_PARTICLES 256

struct Overlay {
    GtkWidget *window;
    GtkWidget *drawing_area;
    std::string display_text;
    std::vector<std::string> candidate_lines;
    bool visible;

    // Animation state
    double opacity;
    bool fading_in;
    bool fading_out;
    guint fade_timer;
    int fade_duration_ms;
    guint sparkle_timer;

    // Particle system
    Particle particles[MAX_PARTICLES];
    int particle_count;
    double global_hue;      // cycles for rainbow mode
    double time_counter;    // for animations

    // Language display
    std::string language_name;  // e.g. "pinyin", "한국어", "日本語"
    std::string cjk_font_name; // e.g. "Noto Sans CJK SC"

    Config *config;
};

// Particle burst events
void overlay_burst(Overlay *ov, double x, double y, int count);
void overlay_burst_keystroke(Overlay *ov); // punch burst on each letter
void overlay_burst_commit(Overlay *ov);    // big celebration burst

void overlay_init(Overlay *ov, Config *cfg);
void overlay_show(Overlay *ov, const std::string &text);
void overlay_hide(Overlay *ov);
void overlay_set_text(Overlay *ov, const std::string &text);
void overlay_set_candidates(Overlay *ov, const std::string &pinyin,
                            const std::vector<Candidate> &candidates);

// --- IME ---

struct IME {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_seat *seat;

    struct zwp_input_method_manager_v2 *im_manager;
    struct zwp_input_method_v2 *input_method;
    struct zwp_input_method_keyboard_grab_v2 *kb_grab;

    struct zwp_virtual_keyboard_manager_v1 *vk_manager;
    struct zwp_virtual_keyboard_v1 *virtual_keyboard;

    struct xkb_context *xkb_ctx;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;

    bool active;            // compositor says a text input is focused
    bool composing;         // user has toggled composition mode
    volatile bool toggle_requested; // set by SIGUSR1 handler

    InputEngine *engine;

    Config *config;
    Overlay *overlay;
};

void ime_init(IME *ime, Overlay *ov, Config *cfg);
void ime_destroy(IME *ime);
