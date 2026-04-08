#include "wlime.h"
#include "engine.h"
#include "input-method-unstable-v2-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/mman.h>
#include <cerrno>
#include <cstdlib>

// Check if the currently focused window is an XWayland client
static bool is_xwayland_focused() {
    FILE *fp = popen("hyprctl activewindow -j 2>/dev/null", "r");
    if (!fp) return false;

    char buf[2048];
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = '\0';
    pclose(fp);

    // Quick and dirty: look for "xwayland": true
    return strstr(buf, "\"xwayland\": true") != nullptr;
}

// Copy text to clipboard via wl-copy
static void copy_to_clipboard(const char *text) {
    std::string escaped;
    for (const char *p = text; *p; p++) {
        if (*p == '\'')
            escaped += "'\\''";
        else
            escaped += *p;
    }
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "wl-copy '%s'", escaped.c_str());
    system(cmd);
}

// Synthesize Ctrl+V paste via virtual keyboard
static void synthesize_paste(IME *ime, uint32_t time) {
    uint32_t ctrl_key = 29; // KEY_LEFTCTRL
    uint32_t v_key = 47;    // KEY_V

    zwp_virtual_keyboard_v1_modifiers(ime->virtual_keyboard, (1 << 2), 0, 0, 0);
    zwp_virtual_keyboard_v1_key(ime->virtual_keyboard, time, ctrl_key, WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_key(ime->virtual_keyboard, time, v_key, WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_key(ime->virtual_keyboard, time, v_key, WL_KEYBOARD_KEY_STATE_RELEASED);
    zwp_virtual_keyboard_v1_key(ime->virtual_keyboard, time, ctrl_key, WL_KEYBOARD_KEY_STATE_RELEASED);
    zwp_virtual_keyboard_v1_modifiers(ime->virtual_keyboard, 0, 0, 0, 0);
}

// Commit text to the focused app
static void ime_commit_text(IME *ime, const char *text, uint32_t serial, uint32_t time) {
    bool xwayland = is_xwayland_focused();

    // Always try the protocol path for native Wayland apps
    if (!xwayland) {
        zwp_input_method_v2_commit_string(ime->input_method, text);
        zwp_input_method_v2_commit(ime->input_method, serial);
    }

    // Clipboard: always copy if configured, or paste for XWayland
    if (ime->config->clipboard_always || xwayland) {
        copy_to_clipboard(text);
        if (xwayland)
            synthesize_paste(ime, time);
    }

    fprintf(stderr, "[wlime] committed: %s (xwayland=%d clipboard=%d)\n",
            text, xwayland, ime->config->clipboard_always);
}

// --- Input method listeners ---

static void im_activate(void *data, struct zwp_input_method_v2 *im) {
    auto *ime = static_cast<IME *>(data);
    ime->active = true;
    fprintf(stderr, "[wlime] input method activated (text field focused)\n");
}

static void im_deactivate(void *data, struct zwp_input_method_v2 *im) {
    auto *ime = static_cast<IME *>(data);
    ime->active = false;
    ime->composing = false;
    ime->engine->reset();
    overlay_hide(ime->overlay);
    fprintf(stderr, "[wlime] input method deactivated\n");
}

static void im_surrounding_text(void *data, struct zwp_input_method_v2 *im,
                                 const char *text, uint32_t cursor, uint32_t anchor) {
    // We don't use surrounding text yet
}

static void im_text_change_cause(void *data, struct zwp_input_method_v2 *im,
                                  uint32_t cause) {
    // Ignored for now
}

static void im_content_type(void *data, struct zwp_input_method_v2 *im,
                             uint32_t hint, uint32_t purpose) {
    // Ignored for now
}

static void im_done(void *data, struct zwp_input_method_v2 *im) {
    // Compositor is done sending state updates for this cycle
    fprintf(stderr, "[wlime] input method done event\n");
}

static void im_unavailable(void *data, struct zwp_input_method_v2 *im) {
    fprintf(stderr, "[wlime] input method unavailable (another IME running?)\n");
}

static const struct zwp_input_method_v2_listener im_listener = {
    .activate = im_activate,
    .deactivate = im_deactivate,
    .surrounding_text = im_surrounding_text,
    .text_change_cause = im_text_change_cause,
    .content_type = im_content_type,
    .done = im_done,
    .unavailable = im_unavailable,
};

// --- Keyboard grab listeners ---

static void kb_keymap(void *data, struct zwp_input_method_keyboard_grab_v2 *kb,
                      uint32_t format, int32_t fd, uint32_t size) {
    auto *ime = static_cast<IME *>(data);

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    char *map_str = static_cast<char *>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (map_str == MAP_FAILED) {
        close(fd);
        return;
    }

    if (ime->xkb_keymap)
        xkb_keymap_unref(ime->xkb_keymap);
    if (ime->xkb_state)
        xkb_state_unref(ime->xkb_state);

    ime->xkb_keymap = xkb_keymap_new_from_string(ime->xkb_ctx, map_str,
                                                   XKB_KEYMAP_FORMAT_TEXT_V1,
                                                   XKB_KEYMAP_COMPILE_NO_FLAGS);
    ime->xkb_state = xkb_state_new(ime->xkb_keymap);

    // Set the same keymap on the virtual keyboard so we can forward keys
    zwp_virtual_keyboard_v1_keymap(ime->virtual_keyboard, format, fd, size);

    munmap(map_str, size);
    close(fd);
    fprintf(stderr, "[wlime] keyboard keymap loaded\n");
}


static void kb_key(void *data, struct zwp_input_method_keyboard_grab_v2 *kb,
                   uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    auto *ime = static_cast<IME *>(data);
    InputEngine *engine = ime->engine;

    // Forward key releases always
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
        zwp_virtual_keyboard_v1_key(ime->virtual_keyboard, time, key, state);
        return;
    }

    uint32_t keycode = key + 8; // evdev to xkb offset
    xkb_keysym_t sym = xkb_state_key_get_one_sym(ime->xkb_state, keycode);

    // Not composing — forward everything transparently
    if (!ime->composing) {
        zwp_virtual_keyboard_v1_key(ime->virtual_keyboard, time, key, state);
        return;
    }

    // --- Composing mode ---

    char utf8[8];
    xkb_state_key_get_utf8(ime->xkb_state, keycode, utf8, sizeof(utf8));

    // Escape cancels composition
    if (sym == XKB_KEY_Escape) {
        ime->composing = false;
        engine->reset();
        overlay_hide(ime->overlay);
        sound_play(SND_CANCEL);
        fprintf(stderr, "[wlime] composition cancelled\n");
        return;
    }

    // Try feeding the key to the engine first.
    // This lets engines like RIME handle space/enter/numbers internally.
    if (engine->feed_key(sym, utf8)) {
        // Check if the engine produced a commit (e.g. RIME conversion)
        std::string committed = engine->check_commit();
        if (!committed.empty()) {
            ime_commit_text(ime, committed.c_str(), serial, time);
            sound_play(SND_COMMIT);
            overlay_burst_commit(ime->overlay);
            fprintf(stderr, "[wlime] engine committed: %s\n", committed.c_str());

            // If composition is done, hide overlay
            if (engine->empty()) {
                ime->composing = false;
                overlay_hide(ime->overlay);
            } else {
                overlay_set_candidates(ime->overlay, engine->get_preedit(),
                                       engine->get_candidates());
            }
            return;
        }

        std::string preedit = engine->get_preedit();
        auto &candidates = engine->get_candidates();
        overlay_set_candidates(ime->overlay, preedit, candidates);
        sound_play(SND_KEYSTROKE);
        overlay_burst_keystroke(ime->overlay);

        // Set preedit so the app shows inline preview
        if (!candidates.empty()) {
            zwp_input_method_v2_set_preedit_string(ime->input_method,
                                                    candidates[0].text.c_str(),
                                                    0, candidates[0].text.size());
        } else {
            zwp_input_method_v2_set_preedit_string(ime->input_method,
                                                    preedit.c_str(),
                                                    0, preedit.size());
        }
        zwp_input_method_v2_commit(ime->input_method, serial);

        fprintf(stderr, "[wlime] input: %s → %d candidates\n",
                preedit.c_str(), (int)candidates.size());
        return;
    }

    // Engine didn't consume the key — use wlime's default handling

    // Number keys 1-9 select a candidate
    if (sym >= XKB_KEY_1 && sym <= XKB_KEY_9) {
        int idx = sym - XKB_KEY_1;
        auto &candidates = engine->get_candidates();
        if (idx < (int)candidates.size()) {
            std::string chosen = engine->select(idx);
            ime_commit_text(ime, chosen.c_str(), serial, time);
            sound_play(SND_SELECT);
            overlay_burst_commit(ime->overlay);
            fprintf(stderr, "[wlime] selected candidate %d: %s\n", idx + 1, chosen.c_str());

            ime->composing = false;
            overlay_hide(ime->overlay);
            return;
        }
    }

    // Space or Return commits first candidate, or preedit if no candidates
    if (sym == XKB_KEY_Return || sym == XKB_KEY_space) {
        auto &candidates = engine->get_candidates();
        if (!candidates.empty()) {
            std::string chosen = engine->select(0);
            ime_commit_text(ime, chosen.c_str(), serial, time);
            sound_play(SND_COMMIT);
            overlay_burst_commit(ime->overlay);
            fprintf(stderr, "[wlime] committed: %s\n", chosen.c_str());
        } else if (!engine->empty()) {
            std::string preedit = engine->get_preedit();
            ime_commit_text(ime, preedit.c_str(), serial, time);
            engine->reset();
            sound_play(SND_COMMIT);
            overlay_burst_commit(ime->overlay);
            fprintf(stderr, "[wlime] committed raw: %s\n", preedit.c_str());
        }
        ime->composing = false;
        overlay_hide(ime->overlay);
        return;
    }

    // Backspace
    if (sym == XKB_KEY_BackSpace) {
        if (engine->backspace()) {
            if (engine->empty()) {
                overlay_set_text(ime->overlay, "wlime");
            } else {
                overlay_set_candidates(ime->overlay, engine->get_preedit(),
                                       engine->get_candidates());
            }
        }
        return;
    }

    // Non-handled key — forward to app
    zwp_virtual_keyboard_v1_key(ime->virtual_keyboard, time, key, state);
}

static void kb_modifiers(void *data, struct zwp_input_method_keyboard_grab_v2 *kb,
                         uint32_t serial, uint32_t mods_depressed,
                         uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
    auto *ime = static_cast<IME *>(data);
    if (ime->xkb_state)
        xkb_state_update_mask(ime->xkb_state, mods_depressed, mods_latched,
                              mods_locked, 0, 0, group);

    // Forward modifiers to app
    zwp_virtual_keyboard_v1_modifiers(ime->virtual_keyboard,
                                       mods_depressed, mods_latched,
                                       mods_locked, group);
}

static void kb_repeat_info(void *data, struct zwp_input_method_keyboard_grab_v2 *kb,
                           int32_t rate, int32_t delay) {
    fprintf(stderr, "[wlime] key repeat: rate=%d delay=%d\n", rate, delay);
}

static const struct zwp_input_method_keyboard_grab_v2_listener kb_listener = {
    .keymap = kb_keymap,
    .key = kb_key,
    .modifiers = kb_modifiers,
    .repeat_info = kb_repeat_info,
};

// --- Registry ---

static void registry_global(void *data, struct wl_registry *registry,
                             uint32_t name, const char *interface, uint32_t version) {
    auto *ime = static_cast<IME *>(data);

    if (strcmp(interface, zwp_input_method_manager_v2_interface.name) == 0) {
        ime->im_manager = static_cast<struct zwp_input_method_manager_v2 *>(
            wl_registry_bind(registry, name, &zwp_input_method_manager_v2_interface, 1));
        fprintf(stderr, "[wlime] bound input-method-manager-v2\n");
    } else if (strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
        ime->vk_manager = static_cast<struct zwp_virtual_keyboard_manager_v1 *>(
            wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1));
        fprintf(stderr, "[wlime] bound virtual-keyboard-manager-v1\n");
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        ime->seat = static_cast<struct wl_seat *>(
            wl_registry_bind(registry, name, &wl_seat_interface, 1));
        fprintf(stderr, "[wlime] bound seat\n");
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                    uint32_t name) {
    // Not handled yet
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

// --- GLib integration for our separate wl_display ---

struct WaylandSource {
    GSource source;
    IME *ime;
    GPollFD pfd;
};

static gboolean wl_source_prepare(GSource *source, gint *timeout) {
    auto *ws = reinterpret_cast<WaylandSource *>(source);
    struct wl_display *dpy = ws->ime->display;

    // Flush outgoing requests
    while (wl_display_flush(dpy) == -1) {
        if (errno != EAGAIN) {
            fprintf(stderr, "[wlime] wl_display_flush failed: %s\n", strerror(errno));
            return FALSE;
        }
    }

    // Prepare to read — this must be called before poll()
    // If it fails, there are already events queued that need dispatching
    if (wl_display_prepare_read(dpy) != 0) {
        wl_display_dispatch_pending(dpy);
        *timeout = 0; // re-check immediately
        return FALSE;
    }

    *timeout = -1;
    ws->pfd.events = G_IO_IN | G_IO_ERR | G_IO_HUP;
    return FALSE;
}

static gboolean wl_source_check(GSource *source) {
    auto *ws = reinterpret_cast<WaylandSource *>(source);
    struct wl_display *dpy = ws->ime->display;

    if (ws->pfd.revents & G_IO_IN) {
        // Data available — read events from the fd
        if (wl_display_read_events(dpy) == -1) {
            fprintf(stderr, "[wlime] wl_display_read_events failed: %s (wl err=%d)\n",
                    strerror(errno), wl_display_get_error(dpy));
            return FALSE;
        }
        return TRUE;
    }

    // No data — cancel the read we prepared
    wl_display_cancel_read(dpy);

    if (ws->pfd.revents & (G_IO_ERR | G_IO_HUP)) {
        fprintf(stderr, "[wlime] wayland fd error (revents=0x%x)\n", ws->pfd.revents);
        return TRUE;
    }

    return FALSE;
}

static gboolean wl_source_dispatch(GSource *source, GSourceFunc, gpointer) {
    auto *ws = reinterpret_cast<WaylandSource *>(source);
    struct wl_display *dpy = ws->ime->display;

    int err = wl_display_get_error(dpy);
    if (err) {
        fprintf(stderr, "[wlime] wayland protocol error: %d (%s)\n", err, strerror(err));
        gtk_main_quit();
        return FALSE;
    }

    wl_display_dispatch_pending(dpy);
    return TRUE;
}

static GSourceFuncs wl_source_funcs = {
    .prepare = wl_source_prepare,
    .check = wl_source_check,
    .dispatch = wl_source_dispatch,
    .finalize = nullptr,
};

// --- Init / Destroy ---

void ime_init(IME *ime, Overlay *ov, Config *cfg) {
    ime->overlay = ov;
    ime->config = cfg;
    ime->active = false;
    ime->composing = false;
    ime->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    ime->xkb_keymap = nullptr;
    ime->xkb_state = nullptr;

    // Open our OWN wayland connection, separate from GDK's.
    // Sharing GDK's connection and calling roundtrip on it steals events
    // from GDK and causes protocol errors.
    ime->display = wl_display_connect(nullptr);
    if (!ime->display) {
        fprintf(stderr, "[wlime] failed to connect to wayland display\n");
        return;
    }

    ime->registry = wl_display_get_registry(ime->display);
    wl_registry_add_listener(ime->registry, &registry_listener, ime);
    wl_display_roundtrip(ime->display);

    if (!ime->im_manager) {
        fprintf(stderr, "[wlime] compositor does not support input-method-v2\n");
        return;
    }
    if (!ime->vk_manager) {
        fprintf(stderr, "[wlime] compositor does not support virtual-keyboard-v1\n");
        return;
    }

    // Create input method
    ime->input_method = zwp_input_method_manager_v2_get_input_method(ime->im_manager, ime->seat);
    zwp_input_method_v2_add_listener(ime->input_method, &im_listener, ime);

    // Grab keyboard
    ime->kb_grab = zwp_input_method_v2_grab_keyboard(ime->input_method);
    zwp_input_method_keyboard_grab_v2_add_listener(ime->kb_grab, &kb_listener, ime);

    // Create virtual keyboard for forwarding
    ime->virtual_keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
        ime->vk_manager, ime->seat);

    wl_display_roundtrip(ime->display);

    // Integrate our wl_display fd into the GLib main loop
    GSource *source = g_source_new(&wl_source_funcs, sizeof(WaylandSource));
    auto *ws = reinterpret_cast<WaylandSource *>(source);
    ws->ime = ime;
    ws->pfd.fd = wl_display_get_fd(ime->display);
    ws->pfd.events = G_IO_IN | G_IO_ERR | G_IO_HUP;
    g_source_add_poll(source, &ws->pfd);
    g_source_attach(source, nullptr);

    fprintf(stderr, "[wlime] IME initialized\n");
}

void ime_destroy(IME *ime) {
    if (ime->kb_grab)
        zwp_input_method_keyboard_grab_v2_release(ime->kb_grab);
    if (ime->input_method)
        zwp_input_method_v2_destroy(ime->input_method);
    if (ime->virtual_keyboard)
        zwp_virtual_keyboard_v1_destroy(ime->virtual_keyboard);
    if (ime->xkb_state)
        xkb_state_unref(ime->xkb_state);
    if (ime->xkb_keymap)
        xkb_keymap_unref(ime->xkb_keymap);
    if (ime->xkb_ctx)
        xkb_context_unref(ime->xkb_ctx);
}
