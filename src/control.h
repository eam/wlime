#pragma once

#include <string>

struct IME;

// Start the control socket server at $XDG_RUNTIME_DIR/wlime.sock.
// Integrates into the GLib main loop. Call after ime_init().
void control_init(IME *ime);

// Clean up the socket file and fd.
void control_destroy();

// Client mode: connect to a running wlime, send a command, print the response.
// Returns 0 on success, 1 on error.
int control_send(const char *command);
