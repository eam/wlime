#include "wlime.h"
#include <cstdio>

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    fprintf(stderr, "[wlime] starting up...\n");

    Config config = {};
    config_init(&config);

    sound_init(&config);

    Overlay overlay = {};
    overlay_init(&overlay, &config);

    IME ime = {};
    if (!engine_init(&ime.engine)) {
        fprintf(stderr, "[wlime] failed to initialize pinyin engine, exiting\n");
        return 1;
    }
    ime_init(&ime, &overlay, &config);

    gtk_main();

    ime_destroy(&ime);
    engine_destroy(&ime.engine);
    return 0;
}
