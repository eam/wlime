#include "wlime.h"
#include "engine.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>

// --- Color palette (arcade neon) ---
struct Color { double r, g, b, a; };

static const Color BG          = {0.02, 0.02, 0.06, 0.75};
static const Color SCANLINE    = {0.0,  0.0,  0.0,  0.08};
static const Color PINYIN      = {0.4,  0.85, 1.0,  1.0};
static const Color PINYIN_GLOW = {0.2,  0.6,  1.0,  0.3};
static const Color GOLD        = {1.0,  0.85, 0.1,  1.0};
static const Color GOLD_GLOW   = {1.0,  0.7,  0.0,  0.25};
static const Color CAND_TEXT   = {0.92, 0.92, 0.96, 0.95};
static const Color CAND_DIM    = {0.45, 0.45, 0.55, 0.7};
static const Color TITLE       = {0.6,  0.4,  1.0,  0.6};
static const Color DIVIDER     = {0.3,  0.5,  0.8,  0.3};
static const Color IDLE_GLOW   = {0.5,  0.3,  1.0,  0.15};

static Color fade(Color c, double opacity) {
    return {c.r, c.g, c.b, c.a * opacity};
}

// HSV to RGB (hue 0-360, s/v 0-1)
static Color hsv(double h, double s, double v, double a) {
    double c = v * s;
    double x = c * (1.0 - fabs(fmod(h / 60.0, 2.0) - 1.0));
    double m = v - c;
    double r, g, b;
    if (h < 60)       { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else               { r = c; g = 0; b = x; }
    return {r + m, g + m, b + m, a};
}

static double randf() { return (double)rand() / RAND_MAX; }
static double randf_range(double lo, double hi) { return lo + randf() * (hi - lo); }

// --- Particle system ---

static void particle_spawn(Overlay *ov, double x, double y,
                            double vx, double vy, double size,
                            double hue, double life, int style) {
    if (ov->particle_count >= MAX_PARTICLES) return;
    Particle &p = ov->particles[ov->particle_count++];
    p.x = x; p.y = y;
    p.vx = vx; p.vy = vy;
    p.size = size;
    p.hue = hue;
    p.life = life;
    p.decay = randf_range(0.01, 0.04);
    p.style = style;
}

static void particles_update(Overlay *ov) {
    int alive = 0;
    for (int i = 0; i < ov->particle_count; i++) {
        Particle &p = ov->particles[i];
        p.life -= p.decay;
        if (p.life <= 0) continue;

        p.x += p.vx;
        p.y += p.vy;
        p.vy += 0.15; // gravity
        p.vx *= 0.98; // drag
        p.size *= 0.995;

        ov->particles[alive++] = p;
    }
    ov->particle_count = alive;
}

static void draw_star(cairo_t *cr, double cx, double cy, double size, int points) {
    double inner = size * 0.4;
    cairo_new_path(cr);
    for (int i = 0; i < points * 2; i++) {
        double angle = (i * M_PI / points) - M_PI / 2.0;
        double r = (i % 2 == 0) ? size : inner;
        double x = cx + cos(angle) * r;
        double y = cy + sin(angle) * r;
        if (i == 0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_close_path(cr);
    cairo_fill(cr);
}

static void draw_sparkle(cairo_t *cr, double cx, double cy, double size) {
    // 4-pointed sparkle with thin spikes
    cairo_new_path(cr);
    double thin = size * 0.15;
    // Vertical spike
    cairo_move_to(cr, cx, cy - size);
    cairo_line_to(cr, cx + thin, cy);
    cairo_line_to(cr, cx, cy + size);
    cairo_line_to(cr, cx - thin, cy);
    cairo_close_path(cr);
    cairo_fill(cr);
    // Horizontal spike
    cairo_new_path(cr);
    cairo_move_to(cr, cx - size, cy);
    cairo_line_to(cr, cx, cy + thin);
    cairo_line_to(cr, cx + size, cy);
    cairo_line_to(cr, cx, cy - thin);
    cairo_close_path(cr);
    cairo_fill(cr);
}

static void particles_draw(cairo_t *cr, Overlay *ov, double opacity) {
    for (int i = 0; i < ov->particle_count; i++) {
        Particle &p = ov->particles[i];
        Color c = hsv(p.hue, 0.8, 1.0, p.life * opacity);

        // Glow
        cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a * 0.3);
        cairo_arc(cr, p.x, p.y, p.size * 2.5, 0, 2 * M_PI);
        cairo_fill(cr);

        // Core
        cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
        switch (p.style) {
        case 0: // dot
            cairo_arc(cr, p.x, p.y, p.size, 0, 2 * M_PI);
            cairo_fill(cr);
            break;
        case 1: // star
            draw_star(cr, p.x, p.y, p.size, 5);
            break;
        case 2: // sparkle
            draw_sparkle(cr, p.x, p.y, p.size);
            break;
        }
    }
}

// Ambient sparkles that drift around
static void spawn_ambient(Overlay *ov, int w, int h) {
    if (ov->particle_count > MAX_PARTICLES - 5) return;
    if (randf() > 0.3) return; // don't spam every frame

    double x = randf_range(w * 0.15, w * 0.85);
    double y = randf_range(h * 0.15, h * 0.85);
    double hue = fmod(ov->global_hue + randf_range(-30, 30), 360);
    int style = (int)(randf() * 3);
    double size = randf_range(2, 6);
    particle_spawn(ov, x, y,
                   randf_range(-0.5, 0.5), randf_range(-1.5, -0.3),
                   size, hue, randf_range(0.5, 1.0), style);
}

void overlay_burst(Overlay *ov, double x, double y, int count) {
    if (!ov->config || !ov->config->sparkle_enabled) return;
    for (int i = 0; i < count && ov->particle_count < MAX_PARTICLES; i++) {
        double angle = randf() * 2 * M_PI;
        double speed = randf_range(2, 8);
        double hue = fmod(ov->global_hue + randf_range(-60, 60), 360);
        int style = (int)(randf() * 3);
        double size = randf_range(3, 8);
        particle_spawn(ov, x, y,
                       cos(angle) * speed, sin(angle) * speed - 2,
                       size, hue, randf_range(0.6, 1.0), style);
    }
}

void overlay_burst_keystroke(Overlay *ov) {
    if (!ov->config || !ov->config->sparkle_enabled) return;
    int w = gtk_widget_get_allocated_width(ov->drawing_area);
    int h = gtk_widget_get_allocated_height(ov->drawing_area);
    if (w <= 0 || h <= 0) return;

    // Burst from the right end of the pinyin text area
    // Approximate: text is centered at ~28% height, cursor at the right edge
    double cx = w * 0.5 + ov->display_text.size() * 14.0; // rough char width
    double cy = h * 0.28;

    // Punchy directional blast — mostly upward and outward, like impact sparks
    int count = 15 + (int)(randf() * 10);
    for (int i = 0; i < count && ov->particle_count < MAX_PARTICLES; i++) {
        double angle = randf_range(-M_PI * 0.8, -M_PI * 0.2); // mostly upward
        double speed = randf_range(4, 14);
        double hue = fmod(ov->global_hue + randf_range(-40, 40), 360);
        int style = (int)(randf() * 3);
        double size = randf_range(3, 9);
        particle_spawn(ov, cx + randf_range(-8, 8), cy + randf_range(-5, 5),
                       cos(angle) * speed * randf_range(0.5, 1.5),
                       sin(angle) * speed - 2,
                       size, hue, randf_range(0.5, 1.0), style);
    }

    // A few big slow sparkles for drama
    for (int i = 0; i < 3 && ov->particle_count < MAX_PARTICLES; i++) {
        double angle = randf_range(-M_PI * 0.7, -M_PI * 0.3);
        double hue = fmod(ov->global_hue + 180 + randf_range(-20, 20), 360);
        particle_spawn(ov, cx, cy,
                       cos(angle) * 3, sin(angle) * 3 - 1,
                       randf_range(8, 15), hue, randf_range(0.8, 1.0), 2); // sparkle style
    }
}

void overlay_burst_commit(Overlay *ov) {
    if (!ov->config || !ov->config->sparkle_enabled) return;
    int w = gtk_widget_get_allocated_width(ov->drawing_area);
    int h = gtk_widget_get_allocated_height(ov->drawing_area);

    // Massive rainbow burst from center
    double cx = w / 2.0;
    double cy = h * 0.4;
    for (int i = 0; i < 60 && ov->particle_count < MAX_PARTICLES; i++) {
        double angle = randf() * 2 * M_PI;
        double speed = randf_range(3, 15);
        double hue = fmod(i * 6.0, 360);
        int style = (int)(randf() * 3);
        double size = randf_range(4, 12);
        particle_spawn(ov, cx + randf_range(-30, 30), cy + randf_range(-20, 20),
                       cos(angle) * speed, sin(angle) * speed - 3,
                       size, hue, randf_range(0.7, 1.0), style);
    }
}

// Draw text with a soft glow behind it
static void draw_glowing_text(cairo_t *cr, double x, double y,
                               const char *text, Color col, Color glow,
                               double font_size) {
    for (int pass = 3; pass >= 1; pass--) {
        double spread = pass * 2.5;
        double alpha = glow.a / pass;
        cairo_set_source_rgba(cr, glow.r, glow.g, glow.b, alpha);
        cairo_set_font_size(cr, font_size);
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx == 0 && dy == 0) continue;
                cairo_move_to(cr, x + dx * spread, y + dy * spread);
                cairo_show_text(cr, text);
            }
        }
    }
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    cairo_set_font_size(cr, font_size);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, text);
}

// Rainbow-shifted version of draw_glowing_text
static void draw_rainbow_text(cairo_t *cr, double x, double y,
                               const char *text, double hue_base,
                               double font_size, double opacity) {
    Color glow = hsv(hue_base, 0.7, 1.0, 0.2 * opacity);
    for (int pass = 3; pass >= 1; pass--) {
        double spread = pass * 3.0;
        double alpha = glow.a / pass;
        cairo_set_source_rgba(cr, glow.r, glow.g, glow.b, alpha);
        cairo_set_font_size(cr, font_size);
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx == 0 && dy == 0) continue;
                cairo_move_to(cr, x + dx * spread, y + dy * spread);
                cairo_show_text(cr, text);
            }
        }
    }

    // Per-character rainbow (approximate with full string for now)
    Color c = hsv(hue_base, 0.8, 1.0, opacity);
    cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
    cairo_set_font_size(cr, font_size);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, text);
}

static void draw_scanlines(cairo_t *cr, int w, int h, double opacity) {
    cairo_set_source_rgba(cr, SCANLINE.r, SCANLINE.g, SCANLINE.b, SCANLINE.a * opacity);
    for (int y = 0; y < h; y += 3) {
        cairo_rectangle(cr, 0, y, w, 1);
    }
    cairo_fill(cr);
}

static void draw_divider(cairo_t *cr, double x1, double x2, double y, double opacity) {
    Color d = fade(DIVIDER, opacity);
    cairo_set_source_rgba(cr, d.r, d.g, d.b, d.a);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, x1, y);
    cairo_line_to(cr, x2, y);
    cairo_stroke(cr);
    cairo_set_source_rgba(cr, d.r, d.g, d.b, d.a * 0.3);
    cairo_set_line_width(cr, 4.0);
    cairo_move_to(cr, x1, y);
    cairo_line_to(cr, x2, y);
    cairo_stroke(cr);
}

// --- Animation ---

static gboolean sparkle_tick(gpointer data) {
    auto *ov = static_cast<Overlay *>(data);
    if (!ov->visible) return TRUE;

    ov->global_hue = fmod(ov->global_hue + 2.0, 360.0);
    ov->time_counter += 0.016;

    if (ov->config && ov->config->sparkle_enabled) {
        int w = gtk_widget_get_allocated_width(ov->drawing_area);
        int h = gtk_widget_get_allocated_height(ov->drawing_area);
        if (w > 0 && h > 0) {
            particles_update(ov);
            spawn_ambient(ov, w, h);
        }
        gtk_widget_queue_draw(ov->drawing_area);
    }

    return TRUE;
}

static gboolean fade_tick(gpointer data) {
    auto *ov = static_cast<Overlay *>(data);
    double step = 16.0 / ov->fade_duration_ms;

    if (ov->fading_in) {
        ov->opacity += step;
        if (ov->opacity >= 1.0) {
            ov->opacity = 1.0;
            ov->fading_in = false;
            ov->fade_timer = 0;
            gtk_widget_queue_draw(ov->drawing_area);
            return FALSE;
        }
    } else if (ov->fading_out) {
        ov->opacity -= step;
        if (ov->opacity <= 0.0) {
            ov->opacity = 0.0;
            ov->fading_out = false;
            ov->visible = false;
            ov->fade_timer = 0;
            gtk_widget_hide(ov->window);
            return FALSE;
        }
    }

    gtk_widget_queue_draw(ov->drawing_area);
    return TRUE;
}

static void start_fade(Overlay *ov, bool in) {
    if (ov->fade_timer) {
        g_source_remove(ov->fade_timer);
        ov->fade_timer = 0;
    }

    if (!ov->config || !ov->config->animate_enabled) {
        if (in) {
            ov->opacity = 1.0;
            ov->fading_in = false;
            ov->fading_out = false;
            gtk_widget_show_all(ov->window);
        } else {
            ov->opacity = 0.0;
            ov->fading_in = false;
            ov->fading_out = false;
            ov->visible = false;
            gtk_widget_hide(ov->window);
        }
        return;
    }

    ov->fading_in = in;
    ov->fading_out = !in;

    if (in)
        gtk_widget_show_all(ov->window);

    ov->fade_timer = g_timeout_add(16, fade_tick, ov);
}

// --- Draw ---

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    auto *ov = static_cast<Overlay *>(data);
    double op = ov->opacity;
    bool sparkle = ov->config && ov->config->sparkle_enabled;

    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);

    // --- Background ---
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_pattern_t *bg_grad = cairo_pattern_create_linear(0, 0, 0, h);
    cairo_pattern_add_color_stop_rgba(bg_grad, 0.0, 0.01, 0.01, 0.04, BG.a * op);
    cairo_pattern_add_color_stop_rgba(bg_grad, 0.4, BG.r, BG.g, BG.b, BG.a * 0.9 * op);
    cairo_pattern_add_color_stop_rgba(bg_grad, 0.6, BG.r, BG.g, BG.b, BG.a * 0.9 * op);
    cairo_pattern_add_color_stop_rgba(bg_grad, 1.0, 0.01, 0.01, 0.04, BG.a * op);
    cairo_set_source(cr, bg_grad);
    cairo_paint(cr);
    cairo_pattern_destroy(bg_grad);

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    draw_scanlines(cr, w, h, op);

    // Particles behind text
    if (sparkle)
        particles_draw(cr, ov, op);

    if (ov->display_text.empty())
        return FALSE;

    bool idle = ov->candidate_lines.empty() && ov->display_text == "wlime";

    // --- Idle mode ---
    if (idle) {
        cairo_select_font_face(cr, "monospace",
                               CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_BOLD);

        double title_size = 140.0;
        cairo_set_font_size(cr, title_size);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, "wlime", &ext);
        double tx = (w - ext.width) / 2.0 - ext.x_bearing;
        double ty = (h - ext.height) / 2.0 - ext.y_bearing;

        if (sparkle) {
            draw_rainbow_text(cr, tx, ty, "wlime", ov->global_hue, title_size, op);
        } else {
            draw_glowing_text(cr, tx, ty, "wlime",
                              fade(TITLE, op), fade(IDLE_GLOW, op), title_size);
        }

        std::string subtitle = "[ " + ov->language_name + " input active ]";
        cairo_select_font_face(cr, ov->cjk_font_name.c_str(),
                               CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 22.0);
        cairo_text_extents(cr, subtitle.c_str(), &ext);
        double sx = (w - ext.width) / 2.0 - ext.x_bearing;
        Color sub = fade(CAND_DIM, op);
        cairo_set_source_rgba(cr, sub.r, sub.g, sub.b, sub.a * 0.7);
        cairo_move_to(cr, sx, ty + 60);
        cairo_show_text(cr, subtitle.c_str());

        return FALSE;
    }

    // --- Composing mode ---

    cairo_select_font_face(cr, ov->cjk_font_name.c_str(),
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    double pin_size = 44.0;
    cairo_set_font_size(cr, pin_size);

    std::string pinyin_display = ov->display_text + "_";

    cairo_text_extents_t pin_ext;
    cairo_text_extents(cr, pinyin_display.c_str(), &pin_ext);
    double pin_x = (w - pin_ext.width) / 2.0 - pin_ext.x_bearing;
    double pin_y = h * 0.28;

    if (sparkle) {
        draw_rainbow_text(cr, pin_x, pin_y, pinyin_display.c_str(),
                          ov->global_hue + 180, pin_size, op);
    } else {
        draw_glowing_text(cr, pin_x, pin_y, pinyin_display.c_str(),
                          fade(PINYIN, op), fade(PINYIN_GLOW, op), pin_size);
    }

    double div_margin = w * 0.3;
    draw_divider(cr, div_margin, w - div_margin, pin_y + 25, op);

    if (ov->candidate_lines.empty())
        return FALSE;

    // --- Candidates ---
    cairo_select_font_face(cr, ov->cjk_font_name.c_str(),
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);

    double cand_start_y = pin_y + 75;

    // First candidate — BIG, centered
    {
        double big_size = 96.0;
        cairo_set_font_size(cr, big_size);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, ov->candidate_lines[0].c_str(), &ext);
        double cx = (w - ext.width) / 2.0 - ext.x_bearing;

        cairo_select_font_face(cr, "monospace",
                               CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 40.0);
        Color g_dim = fade(GOLD, op);
        g_dim.a *= 0.5;
        cairo_set_source_rgba(cr, g_dim.r, g_dim.g, g_dim.b, g_dim.a);
        cairo_text_extents_t nxt;
        cairo_text_extents(cr, "1.", &nxt);
        cairo_move_to(cr, cx - nxt.width - 15, cand_start_y);
        cairo_show_text(cr, "1.");

        cairo_select_font_face(cr, ov->cjk_font_name.c_str(),
                               CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_BOLD);

        if (sparkle) {
            // Rainbow shimmer on first candidate
            double shimmer = fmod(ov->global_hue + 60, 360);
            Color rc = hsv(shimmer, 0.6, 1.0, op);
            Color rg = hsv(shimmer, 0.7, 1.0, 0.2 * op);
            draw_glowing_text(cr, cx, cand_start_y,
                              ov->candidate_lines[0].c_str(), rc, rg, big_size);
        } else {
            draw_glowing_text(cr, cx, cand_start_y,
                              ov->candidate_lines[0].c_str(),
                              fade(GOLD, op), fade(GOLD_GLOW, op), big_size);
        }
    }

    // Remaining candidates — horizontal row
    if (ov->candidate_lines.size() > 1) {
        double row_y = cand_start_y + 90;
        double small_size = 42.0;
        double spacing = 50.0;

        cairo_set_font_size(cr, small_size);

        struct CandLayout { std::string label; double label_w; double text_w; };
        std::vector<CandLayout> layouts;
        double total_width = 0;

        for (size_t i = 1; i < ov->candidate_lines.size(); i++) {
            char label[4];
            snprintf(label, sizeof(label), "%zu.", i + 1);

            cairo_select_font_face(cr, "monospace",
                                   CAIRO_FONT_SLANT_NORMAL,
                                   CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, small_size * 0.7);
            cairo_text_extents_t lext;
            cairo_text_extents(cr, label, &lext);

            cairo_select_font_face(cr, ov->cjk_font_name.c_str(),
                                   CAIRO_FONT_SLANT_NORMAL,
                                   CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, small_size);
            cairo_text_extents_t text;
            cairo_text_extents(cr, ov->candidate_lines[i].c_str(), &text);

            layouts.push_back({std::string(label), lext.width, text.width});
            total_width += lext.width + 8 + text.width;
            if (i < ov->candidate_lines.size() - 1)
                total_width += spacing;
        }

        double rx = (w - total_width) / 2.0;

        for (size_t i = 0; i < layouts.size(); i++) {
            size_t ci = i + 1;

            cairo_select_font_face(cr, "monospace",
                                   CAIRO_FONT_SLANT_NORMAL,
                                   CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, small_size * 0.7);
            Color dim = fade(CAND_DIM, op);
            cairo_set_source_rgba(cr, dim.r, dim.g, dim.b, dim.a);
            cairo_move_to(cr, rx, row_y);
            cairo_show_text(cr, layouts[i].label.c_str());
            rx += layouts[i].label_w + 8;

            cairo_select_font_face(cr, ov->cjk_font_name.c_str(),
                                   CAIRO_FONT_SLANT_NORMAL,
                                   CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, small_size);

            if (sparkle) {
                Color rc = hsv(fmod(ov->global_hue + ci * 40, 360), 0.4, 1.0, op * 0.9);
                cairo_set_source_rgba(cr, rc.r, rc.g, rc.b, rc.a);
            } else {
                Color ct = fade(CAND_TEXT, op);
                cairo_set_source_rgba(cr, ct.r, ct.g, ct.b, ct.a);
            }
            cairo_move_to(cr, rx, row_y);
            cairo_show_text(cr, ov->candidate_lines[ci].c_str());
            rx += layouts[i].text_w + spacing;
        }
    }

    return FALSE;
}

// --- Public API ---

void overlay_init(Overlay *ov, Config *cfg) {
    ov->visible = false;
    ov->opacity = 0.0;
    ov->fading_in = false;
    ov->fading_out = false;
    ov->fade_timer = 0;
    ov->sparkle_timer = 0;
    ov->config = cfg;
    ov->fade_duration_ms = cfg->fade_duration_ms;
    ov->particle_count = 0;
    ov->global_hue = 0;
    ov->time_counter = 0;

    srand((unsigned)time(nullptr));

    ov->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    gtk_layer_init_for_window(GTK_WINDOW(ov->window));
    gtk_layer_set_layer(GTK_WINDOW(ov->window), GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_namespace(GTK_WINDOW(ov->window), "wlime");

    gtk_layer_set_anchor(GTK_WINDOW(ov->window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(ov->window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(ov->window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(ov->window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);

    gtk_layer_set_keyboard_mode(GTK_WINDOW(ov->window),
                                GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);

    GdkScreen *screen = gtk_widget_get_screen(ov->window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual)
        gtk_widget_set_visual(ov->window, visual);
    gtk_widget_set_app_paintable(ov->window, TRUE);

    gtk_layer_set_exclusive_zone(GTK_WINDOW(ov->window), -1);

    ov->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(ov->drawing_area, TRUE);
    gtk_widget_set_vexpand(ov->drawing_area, TRUE);
    gtk_container_add(GTK_CONTAINER(ov->window), ov->drawing_area);
    g_signal_connect(ov->drawing_area, "draw", G_CALLBACK(on_draw), ov);

    // Sparkle animation timer — always runs at 60fps when visible
    ov->sparkle_timer = g_timeout_add(16, sparkle_tick, ov);

    fprintf(stderr, "[wlime] overlay initialized (sparkle=%d)\n", cfg->sparkle_enabled);
}

void overlay_show(Overlay *ov, const std::string &text) {
    ov->display_text = text;
    ov->candidate_lines.clear();
    ov->visible = true;
    start_fade(ov, true);
    gtk_widget_queue_draw(ov->drawing_area);
}

void overlay_hide(Overlay *ov) {
    ov->display_text.clear();
    ov->candidate_lines.clear();
    start_fade(ov, false);
}

void overlay_set_text(Overlay *ov, const std::string &text) {
    ov->display_text = text;
    if (ov->visible)
        gtk_widget_queue_draw(ov->drawing_area);
}

void overlay_set_candidates(Overlay *ov, const std::string &pinyin,
                            const std::vector<Candidate> &candidates) {
    ov->display_text = pinyin;
    ov->candidate_lines.clear();
    for (const auto &c : candidates)
        ov->candidate_lines.push_back(c.text);

    if (!ov->visible) {
        ov->visible = true;
        start_fade(ov, true);
    }
    gtk_widget_queue_draw(ov->drawing_area);
}
