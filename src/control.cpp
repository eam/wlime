#include "control.h"
#include "wlime.h"
#include "engine.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <string>

static int server_fd = -1;
static std::string socket_path;
static IME *g_ctrl_ime = nullptr;

static std::string get_socket_path() {
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (!runtime)
        runtime = "/tmp";
    return std::string(runtime) + "/wlime.sock";
}

// --- Command handlers ---

static std::string handle_command(const std::string &cmd) {
    if (!g_ctrl_ime)
        return "error: not initialized\n";

    if (cmd == "toggle") {
        if (g_ctrl_ime->composing) {
            g_ctrl_ime->composing = false;
            g_ctrl_ime->engine->reset();
            overlay_hide(g_ctrl_ime->overlay);
            sound_play(SND_DEACTIVATE);
            fprintf(stderr, "[wlime] composition OFF\n");
        } else {
            g_ctrl_ime->composing = true;
            g_ctrl_ime->engine->reset();
            overlay_show(g_ctrl_ime->overlay, "wlime");
            sound_play(SND_ACTIVATE);
            fprintf(stderr, "[wlime] composition ON\n");
        }
        return "ok\n";
    }

    if (cmd == "status") {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "{\"composing\":%s,\"language\":\"%s\"}\n",
                 g_ctrl_ime->composing ? "true" : "false",
                 g_ctrl_ime->config->language.c_str());
        return buf;
    }

    if (cmd.compare(0, 7, "switch ") == 0) {
        std::string lang = cmd.substr(7);
        // Trim trailing whitespace
        while (!lang.empty() && (lang.back() == ' ' || lang.back() == '\t'))
            lang.pop_back();

        if (lang.empty())
            return "error: no language specified\n";

        InputEngine *new_engine = create_engine(lang);
        if (!new_engine)
            return "error: unknown language '" + lang + "'\n";

        // Tear down current state
        g_ctrl_ime->composing = false;
        g_ctrl_ime->engine->reset();
        overlay_hide(g_ctrl_ime->overlay);

        // Swap engine
        delete g_ctrl_ime->engine;
        g_ctrl_ime->engine = new_engine;
        g_ctrl_ime->overlay->language_name = new_engine->display_name();
        g_ctrl_ime->overlay->cjk_font_name = new_engine->cjk_font();
        g_ctrl_ime->config->language = lang;

        fprintf(stderr, "[wlime] switched to %s\n", lang.c_str());
        return "ok\n";
    }

    return "error: unknown command '" + cmd + "'\n";
}

// --- GLib socket integration ---

static gboolean on_client_data(GIOChannel *source, GIOCondition, gpointer) {
    int fd = g_io_channel_unix_get_fd(source);

    char buf[256];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        close(fd);
        return FALSE;
    }
    buf[n] = '\0';

    // Strip trailing newline/CR
    std::string cmd(buf);
    while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r'))
        cmd.pop_back();

    std::string response = handle_command(cmd);
    ssize_t written = write(fd, response.c_str(), response.size());
    (void)written;
    close(fd);
    return FALSE; // one-shot per connection
}

static gboolean on_new_connection(GIOChannel *, GIOCondition, gpointer) {
    int client_fd = accept(server_fd, nullptr, nullptr);
    if (client_fd < 0)
        return TRUE;

    GIOChannel *client_chan = g_io_channel_unix_new(client_fd);
    g_io_add_watch(client_chan, G_IO_IN, on_client_data, nullptr);
    g_io_channel_unref(client_chan);

    return TRUE; // keep listening
}

// --- Public API ---

void control_init(IME *ime) {
    g_ctrl_ime = ime;
    socket_path = get_socket_path();

    // Remove stale socket from a previous crash
    unlink(socket_path.c_str());

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[wlime] control socket");
        return;
    }

    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[wlime] control bind");
        close(server_fd);
        server_fd = -1;
        return;
    }

    if (listen(server_fd, 4) < 0) {
        perror("[wlime] control listen");
        close(server_fd);
        server_fd = -1;
        unlink(socket_path.c_str());
        return;
    }

    GIOChannel *chan = g_io_channel_unix_new(server_fd);
    g_io_add_watch(chan, G_IO_IN, on_new_connection, nullptr);
    g_io_channel_unref(chan);

    fprintf(stderr, "[wlime] control socket: %s\n", socket_path.c_str());
}

void control_destroy() {
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    if (!socket_path.empty()) {
        unlink(socket_path.c_str());
        socket_path.clear();
    }
}

int control_send(const char *command) {
    std::string path = get_socket_path();

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("wlime");
        return 1;
    }

    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "wlime: not running (can't connect to %s)\n", path.c_str());
        close(fd);
        return 1;
    }

    std::string msg = std::string(command) + "\n";
    ssize_t written = write(fd, msg.c_str(), msg.size());
    (void)written;

    // Read response
    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }

    close(fd);
    return 0;
}
