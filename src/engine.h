#pragma once

#include <string>
#include <vector>
#include <xkbcommon/xkbcommon.h>

struct Candidate {
    std::string text;
};

class InputEngine {
public:
    virtual ~InputEngine() = default;

    // Process a keystroke. Returns true if consumed (added to composition).
    virtual bool feed_key(xkb_keysym_t sym, const char *utf8) = 0;

    // Handle backspace. Returns true if there was something to delete.
    virtual bool backspace() = 0;

    // Get the current preedit string (shown in overlay as the input buffer).
    virtual std::string get_preedit() const = 0;

    // Get current candidate list.
    virtual const std::vector<Candidate> &get_candidates() const = 0;

    // Select candidate by index. Returns the text to commit to the app.
    virtual std::string select(int index) = 0;

    // Reset all composition state.
    virtual void reset() = 0;

    // Whether the composition buffer is empty.
    virtual bool empty() const = 0;

    // Drain any text committed internally by the engine (e.g. RIME process_key).
    // Returns empty string if nothing was committed.
    virtual std::string check_commit() { return ""; }

    // Display name for the overlay idle screen (e.g. "pinyin", "한국어", "日本語").
    virtual const char *display_name() const = 0;

    // CJK font family for candidate rendering.
    virtual const char *cjk_font() const = 0;
};

// Factory: creates engine based on language config string.
// Returns nullptr on failure (logs the reason).
InputEngine *create_engine(const std::string &language);
