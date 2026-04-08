#include "wlime.h"
#include "engine.h"
#include "control.h"
#include <cstdio>
#include <cstring>

static void print_usage() {
    fprintf(stderr, "usage: wlime [command]\n");
    fprintf(stderr, "\nCommands:\n");
    fprintf(stderr, "  (none)              Start the IME daemon\n");
    fprintf(stderr, "  toggle              Toggle composition mode\n");
    fprintf(stderr, "  status              Print status as JSON\n");
    fprintf(stderr, "  switch <language>   Switch input language\n");
}

int main(int argc, char *argv[]) {
    // Client mode: subcommands talk to a running wlime via socket
    if (argc >= 2) {
        if (strcmp(argv[1], "toggle") == 0)
            return control_send("toggle");
        if (strcmp(argv[1], "status") == 0)
            return control_send("status");
        if (strcmp(argv[1], "switch") == 0) {
            if (argc < 3) {
                fprintf(stderr, "wlime switch: missing language argument\n");
                return 1;
            }
            std::string cmd = "switch " + std::string(argv[2]);
            return control_send(cmd.c_str());
        }
        print_usage();
        return 1;
    }

    // Daemon mode
    gtk_init(&argc, &argv);

    fprintf(stderr, "[wlime] starting up...\n");

    Config config = {};
    config_init(&config);

    sound_init(&config);

    InputEngine *engine = create_engine(config.language);
    if (!engine) {
        fprintf(stderr, "[wlime] failed to initialize engine for '%s', exiting\n",
                config.language.c_str());
        return 1;
    }

    Overlay overlay = {};
    overlay_init(&overlay, &config);
    overlay.language_name = engine->display_name();
    overlay.cjk_font_name = engine->cjk_font();

    IME ime = {};
    ime.engine = engine;
    ime_init(&ime, &overlay, &config);
    control_init(&ime);

    gtk_main();

    control_destroy();
    ime_destroy(&ime);
    delete engine;
    return 0;
}
