#include "engine.h"
#include <cstdio>

#ifdef HAVE_HANGUL
#include <hangul.h>

// Convert UCS-4 string (from libhangul) to UTF-8
static std::string ucs4_to_utf8(const ucschar *ucs) {
    std::string result;
    if (!ucs) return result;

    for (; *ucs; ucs++) {
        uint32_t c = *ucs;
        if (c < 0x80) {
            result += (char)c;
        } else if (c < 0x800) {
            result += (char)(0xC0 | (c >> 6));
            result += (char)(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            result += (char)(0xE0 | (c >> 12));
            result += (char)(0x80 | ((c >> 6) & 0x3F));
            result += (char)(0x80 | (c & 0x3F));
        } else {
            result += (char)(0xF0 | (c >> 18));
            result += (char)(0x80 | ((c >> 12) & 0x3F));
            result += (char)(0x80 | ((c >> 6) & 0x3F));
            result += (char)(0x80 | (c & 0x3F));
        }
    }
    return result;
}

class HangulEngine : public InputEngine {
    HangulInputContext *hic = nullptr;
    std::string committed;      // accumulated committed syllables
    std::vector<Candidate> candidates_;

public:
    HangulEngine() {
        hic = hangul_ic_new("2"); // dubeolsik (standard 2-set) layout
        if (!hic) {
            fprintf(stderr, "[wlime] failed to create hangul input context\n");
            return;
        }

        fprintf(stderr, "[wlime] hangul engine initialized (dubeolsik)\n");
    }

    ~HangulEngine() override {
        if (hic)
            hangul_ic_delete(hic);
    }

    bool ok() const { return hic != nullptr; }

    bool feed_key(xkb_keysym_t /*sym*/, const char *utf8) override {
        if (!utf8 || !utf8[0])
            return false;

        int ascii = utf8[0];

        // Only feed printable ASCII that hangul can process
        if (ascii < 0x20 || ascii > 0x7e)
            return false;

        bool processed = hangul_ic_process(hic, ascii);

        // Collect any committed string from the IC
        const ucschar *commit = hangul_ic_get_commit_string(hic);
        if (commit && commit[0])
            committed += ucs4_to_utf8(commit);

        if (!processed && !hangul_ic_is_empty(hic)) {
            // Key wasn't processed but we have preedit — flush it
            const ucschar *flush = hangul_ic_flush(hic);
            if (flush && flush[0])
                committed += ucs4_to_utf8(flush);
        }

        return processed;
    }

    bool backspace() override {
        if (hangul_ic_backspace(hic))
            return true;

        // IC is empty, remove last committed character (UTF-8 aware)
        if (committed.empty())
            return false;

        // Remove last UTF-8 character
        size_t i = committed.size() - 1;
        while (i > 0 && (committed[i] & 0xC0) == 0x80)
            i--;
        committed.erase(i);
        return true;
    }

    std::string get_preedit() const override {
        std::string result = committed;
        const ucschar *preedit = hangul_ic_get_preedit_string(hic);
        if (preedit && preedit[0])
            result += ucs4_to_utf8(preedit);
        return result;
    }

    const std::vector<Candidate> &get_candidates() const override {
        return candidates_;
    }

    std::string select(int index) override {
        if (index >= 0 && index < (int)candidates_.size()) {
            std::string text = candidates_[index].text;
            committed.clear();
            candidates_.clear();
            hangul_ic_reset(hic);
            return text;
        }

        // No candidate selected — commit the preedit as-is
        std::string text = get_preedit();
        committed.clear();
        candidates_.clear();
        hangul_ic_reset(hic);
        return text;
    }

    void reset() override {
        committed.clear();
        candidates_.clear();
        hangul_ic_reset(hic);
    }

    bool empty() const override {
        return committed.empty() && hangul_ic_is_empty(hic);
    }

    const char *display_name() const override { return "한국어"; }
    const char *cjk_font() const override { return "Noto Sans CJK KR"; }
};

InputEngine *create_hangul_engine() {
    auto *e = new HangulEngine();
    if (!e->ok()) {
        delete e;
        return nullptr;
    }
    return e;
}

#else

InputEngine *create_hangul_engine() {
    fprintf(stderr, "[wlime] korean support not compiled (libhangul not found)\n");
    return nullptr;
}

#endif
