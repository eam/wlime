# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What is wlime

wlime is a fullscreen arcade-style CJK input method editor (IME) for Hyprland. Instead of a traditional popup menu near the cursor, it renders a translucent fullscreen overlay with CRT scanlines, neon glow, and particle effects. It works in native Wayland apps, XWayland browsers, and fullscreen games.

## Build

```bash
meson setup build        # first time only
ninja -C build
```

The binary is `build/wlime`. There are no tests.

## Runtime dependencies

Requires a running Hyprland session with `input-method-v2` and `virtual-keyboard-v1` protocol support. Also needs `wl-copy` (wl-clipboard) for clipboard operations, `pw-play` (PipeWire) for sound effects, and optionally `ffmpeg` to generate sounds on first run.

## Architecture

wlime is a C++17 Wayland client (not a Hyprland plugin). It opens its **own** `wl_display` connection separate from GDK's to avoid stealing GDK's events, then integrates that fd into the GLib main loop via a custom `GSource` (`WaylandSource` in `ime.cpp`).

**Key modules:**

- **`main.cpp`** — Init sequence: config → sound → overlay → engine → IME → `gtk_main()`.
- **`ime.cpp`** — Core IME logic. Binds `zwp_input_method_v2` (keyboard grab, text commit) and `zwp_virtual_keyboard_v1` (key forwarding). Composition mode is toggled via `SIGUSR1` (`pkill -USR1 wlime`). Handles the full key dispatch: pinyin buffer editing, candidate selection (1-9), commit (Space/Enter), cancel (Escape). For XWayland apps, falls back to clipboard+paste via `wl-copy` and synthesized Ctrl+V.
- **`overlay.cpp`** — GTK3 + gtk-layer-shell fullscreen overlay. Renders via Cairo with CRT scanline effects, neon glow text, and a particle system (bursts on keystrokes and commits). Fade animations controlled by timers.
- **`engine.h` / `engine.cpp`** — Pinyin lookup via libpinyin. `engine_query()` takes raw pinyin and returns up to 9 `Candidate` structs. `engine_select()` feeds back user choices for learning.
- **`config.h` / `config.cpp`** — Loads `~/.config/wlime/config` (key=value format). Defaults applied if file missing.
- **`sound.h` / `sound.cpp`** — Sound effects via `pw-play`. Events: activate, deactivate, keystroke, commit, cancel, select.

**Wayland protocol flow:**

1. IME grabs keyboard via `input-method-v2` → all key events routed through `kb_key()`
2. Non-composing keys forwarded transparently via `virtual-keyboard-v1`
3. During composition: keys consumed, pinyin buffer built, candidates displayed on overlay
4. On commit: text sent via `zwp_input_method_v2_commit_string()` (Wayland) or clipboard+paste (XWayland)

**Protocol XML files** in `protocols/` are local copies; `xdg-shell` and `wlr-layer-shell` come from system `wayland-protocols` and `wlr-protocols` packages.
