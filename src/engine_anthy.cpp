#include "engine.h"
#include <cstdio>
#include <map>

#ifdef HAVE_ANTHY
#include <anthy/anthy.h>

// Romaji to hiragana conversion table
// Longest match wins, so "sha"→しゃ is checked before "sh" (which waits for more input)
static const std::map<std::string, std::string> ROMAJI_TABLE = {
    // Vowels
    {"a", "あ"}, {"i", "い"}, {"u", "う"}, {"e", "え"}, {"o", "お"},
    // K-row
    {"ka", "か"}, {"ki", "き"}, {"ku", "く"}, {"ke", "け"}, {"ko", "こ"},
    {"kya", "きゃ"}, {"kyi", "きぃ"}, {"kyu", "きゅ"}, {"kye", "きぇ"}, {"kyo", "きょ"},
    // S-row
    {"sa", "さ"}, {"si", "し"}, {"su", "す"}, {"se", "せ"}, {"so", "そ"},
    {"sha", "しゃ"}, {"shi", "し"}, {"shu", "しゅ"}, {"she", "しぇ"}, {"sho", "しょ"},
    {"sya", "しゃ"}, {"syu", "しゅ"}, {"syo", "しょ"},
    // T-row
    {"ta", "た"}, {"ti", "ち"}, {"tu", "つ"}, {"te", "て"}, {"to", "と"},
    {"chi", "ち"}, {"cha", "ちゃ"}, {"chu", "ちゅ"}, {"che", "ちぇ"}, {"cho", "ちょ"},
    {"tsu", "つ"}, {"tya", "ちゃ"}, {"tyu", "ちゅ"}, {"tyo", "ちょ"},
    // N-row
    {"na", "な"}, {"ni", "に"}, {"nu", "ぬ"}, {"ne", "ね"}, {"no", "の"},
    {"nya", "にゃ"}, {"nyi", "にぃ"}, {"nyu", "にゅ"}, {"nye", "にぇ"}, {"nyo", "にょ"},
    {"nn", "ん"},
    // H-row
    {"ha", "は"}, {"hi", "ひ"}, {"hu", "ふ"}, {"he", "へ"}, {"ho", "ほ"},
    {"fu", "ふ"},
    {"hya", "ひゃ"}, {"hyi", "ひぃ"}, {"hyu", "ひゅ"}, {"hye", "ひぇ"}, {"hyo", "ひょ"},
    // M-row
    {"ma", "ま"}, {"mi", "み"}, {"mu", "む"}, {"me", "め"}, {"mo", "も"},
    {"mya", "みゃ"}, {"myi", "みぃ"}, {"myu", "みゅ"}, {"mye", "みぇ"}, {"myo", "みょ"},
    // Y-row
    {"ya", "や"}, {"yu", "ゆ"}, {"yo", "よ"},
    // R-row
    {"ra", "ら"}, {"ri", "り"}, {"ru", "る"}, {"re", "れ"}, {"ro", "ろ"},
    {"rya", "りゃ"}, {"ryi", "りぃ"}, {"ryu", "りゅ"}, {"rye", "りぇ"}, {"ryo", "りょ"},
    // W-row
    {"wa", "わ"}, {"wi", "うぃ"}, {"we", "うぇ"}, {"wo", "を"},
    // G-row (dakuten)
    {"ga", "が"}, {"gi", "ぎ"}, {"gu", "ぐ"}, {"ge", "げ"}, {"go", "ご"},
    {"gya", "ぎゃ"}, {"gyi", "ぎぃ"}, {"gyu", "ぎゅ"}, {"gye", "ぎぇ"}, {"gyo", "ぎょ"},
    // Z-row
    {"za", "ざ"}, {"zi", "じ"}, {"zu", "ず"}, {"ze", "ぜ"}, {"zo", "ぞ"},
    {"ja", "じゃ"}, {"ji", "じ"}, {"ju", "じゅ"}, {"je", "じぇ"}, {"jo", "じょ"},
    {"jya", "じゃ"}, {"jyu", "じゅ"}, {"jyo", "じょ"},
    // D-row
    {"da", "だ"}, {"di", "ぢ"}, {"du", "づ"}, {"de", "で"}, {"do", "ど"},
    {"dya", "ぢゃ"}, {"dyi", "ぢぃ"}, {"dyu", "ぢゅ"}, {"dye", "ぢぇ"}, {"dyo", "ぢょ"},
    // B-row
    {"ba", "ば"}, {"bi", "び"}, {"bu", "ぶ"}, {"be", "べ"}, {"bo", "ぼ"},
    {"bya", "びゃ"}, {"byi", "びぃ"}, {"byu", "びゅ"}, {"bye", "びぇ"}, {"byo", "びょ"},
    // P-row (handakuten)
    {"pa", "ぱ"}, {"pi", "ぴ"}, {"pu", "ぷ"}, {"pe", "ぺ"}, {"po", "ぽ"},
    {"pya", "ぴゃ"}, {"pyi", "ぴぃ"}, {"pyu", "ぴゅ"}, {"pye", "ぴぇ"}, {"pyo", "ぴょ"},
    // Small kana
    {"xa", "ぁ"}, {"xi", "ぃ"}, {"xu", "ぅ"}, {"xe", "ぇ"}, {"xo", "ぉ"},
    {"xya", "ゃ"}, {"xyu", "ゅ"}, {"xyo", "ょ"}, {"xtu", "っ"}, {"xtsu", "っ"},
    // Punctuation
    {"-", "ー"},
};

// Check if any romaji table entry starts with the given prefix
static bool has_prefix(const std::string &prefix) {
    for (auto &kv : ROMAJI_TABLE) {
        if (kv.first.size() > prefix.size() &&
            kv.first.compare(0, prefix.size(), prefix) == 0)
            return true;
    }
    return false;
}

// Convert hiragana string to katakana (shift Unicode codepoints by 0x60)
static std::string hira_to_kata(const std::string &hira) {
    std::string result;
    for (size_t i = 0; i < hira.size(); ) {
        unsigned char c = hira[i];
        if ((c & 0xE0) == 0xC0 && i + 1 < hira.size()) {
            // 2-byte UTF-8
            result += hira[i];
            result += hira[i+1];
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < hira.size()) {
            // 3-byte UTF-8 — hiragana is U+3040-309F, katakana is U+30A0-30FF
            uint32_t cp = ((c & 0x0F) << 12) |
                          ((hira[i+1] & 0x3F) << 6) |
                          (hira[i+2] & 0x3F);
            if (cp >= 0x3041 && cp <= 0x3096) {
                cp += 0x60; // hiragana → katakana
            }
            result += (char)(0xE0 | (cp >> 12));
            result += (char)(0x80 | ((cp >> 6) & 0x3F));
            result += (char)(0x80 | (cp & 0x3F));
            i += 3;
        } else {
            result += hira[i];
            i++;
        }
    }
    return result;
}

class AnthyEngine : public InputEngine {
    anthy_context_t ctx = nullptr;
    std::string romaji;         // unconverted romaji tail (partial match)
    std::string raw_input;      // full romaji as typed (for preedit display)
    std::string kana;           // converted hiragana
    std::vector<Candidate> candidates_;

public:
    AnthyEngine() {
        if (anthy_init() < 0) {
            fprintf(stderr, "[wlime] failed to init anthy\n");
            return;
        }

        ctx = anthy_create_context();
        if (!ctx) {
            fprintf(stderr, "[wlime] failed to create anthy context\n");
            anthy_quit();
            return;
        }

        anthy_context_set_encoding(ctx, ANTHY_UTF8_ENCODING);
        fprintf(stderr, "[wlime] anthy engine initialized\n");
    }

    ~AnthyEngine() override {
        if (ctx)
            anthy_release_context(ctx);
        anthy_quit();
    }

    bool ok() const { return ctx != nullptr; }

    bool feed_key(xkb_keysym_t, const char *utf8) override {
        if (!utf8 || !utf8[0])
            return false;

        char c = utf8[0];

        // Accept lowercase, dash (for ー)
        if (c == '-') {
            kana += "ー";
            raw_input += c;
            update_candidates();
            return true;
        }

        if (c < 'a' || c > 'z')
            return false;

        romaji += c;
        raw_input += c;

        // Double consonant → っ + keep second consonant
        // e.g. "tt" → っ + "t", "kk" → っ + "k"
        if (romaji.size() >= 2 && romaji[romaji.size()-1] == romaji[romaji.size()-2]
            && c != 'a' && c != 'i' && c != 'u' && c != 'e' && c != 'o' && c != 'n') {
            kana += "っ";
            romaji = std::string(1, c);
            // Check if single char matches
            convert_romaji();
            update_candidates();
            return true;
        }

        // "n" before a consonant (not n, y, or vowel) → ん
        if (romaji.size() >= 2 && romaji[0] == 'n') {
            char next = romaji[1];
            if (next != 'a' && next != 'i' && next != 'u' && next != 'e' && next != 'o'
                && next != 'y' && next != 'n') {
                kana += "ん";
                romaji.erase(0, 1);
            }
        }

        convert_romaji();
        update_candidates();
        return true;
    }

    bool backspace() override {
        if (raw_input.empty())
            return false;

        raw_input.pop_back();

        // Re-derive kana from raw_input by replaying conversion
        romaji.clear();
        kana.clear();
        for (char c : raw_input) {
            romaji += c;
            // Double consonant
            if (romaji.size() >= 2 && romaji[romaji.size()-1] == romaji[romaji.size()-2]
                && c != 'a' && c != 'i' && c != 'u' && c != 'e' && c != 'o' && c != 'n') {
                kana += "っ";
                romaji = std::string(1, c);
            }
            // "n" before consonant
            if (romaji.size() >= 2 && romaji[0] == 'n') {
                char next = romaji[1];
                if (next != 'a' && next != 'i' && next != 'u' && next != 'e' && next != 'o'
                    && next != 'y' && next != 'n') {
                    kana += "ん";
                    romaji.erase(0, 1);
                }
            }
            convert_romaji();
        }
        update_candidates();
        return true;
    }

    std::string get_preedit() const override {
        return raw_input;
    }

    const std::vector<Candidate> &get_candidates() const override {
        return candidates_;
    }

    std::string select(int index) override {
        if (index < 0 || index >= (int)candidates_.size()) {
            // Commit kana as-is
            std::string text = kana + romaji;
            kana.clear();
            romaji.clear();
            raw_input.clear();
            candidates_.clear();
            anthy_reset_context(ctx);
            return text;
        }

        std::string text = candidates_[index].text;
        kana.clear();
        romaji.clear();
        raw_input.clear();
        candidates_.clear();
        anthy_reset_context(ctx);
        return text;
    }

    void reset() override {
        kana.clear();
        romaji.clear();
        raw_input.clear();
        candidates_.clear();
        if (ctx)
            anthy_reset_context(ctx);
    }

    bool empty() const override {
        return raw_input.empty();
    }

    const char *display_name() const override { return "日本語"; }
    const char *cjk_font() const override { return "Noto Sans CJK JP"; }

private:
    void convert_romaji() {
        // Try to match the romaji buffer against the table
        // Longest match first
        while (!romaji.empty()) {
            bool matched = false;
            // Try longest possible match (up to 4 chars)
            for (int len = std::min((int)romaji.size(), 4); len >= 1; len--) {
                std::string sub = romaji.substr(0, len);
                auto it = ROMAJI_TABLE.find(sub);
                if (it != ROMAJI_TABLE.end()) {
                    kana += it->second;
                    romaji.erase(0, len);
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                // Check if current buffer could be a prefix of a valid entry
                if (has_prefix(romaji))
                    break; // wait for more input
                // Invalid romaji — keep first char as-is and retry
                kana += romaji[0];
                romaji.erase(0, 1);
            }
        }
    }

    void update_candidates() {
        candidates_.clear();

        // Flush any pending "n" at end for candidate lookup
        std::string lookup_kana = kana;
        if (romaji == "n")
            lookup_kana += "ん";

        if (lookup_kana.empty())
            return;

        // First candidate: hiragana as-is
        candidates_.push_back({lookup_kana});

        // Second candidate: katakana
        std::string kata = hira_to_kata(lookup_kana);
        if (kata != lookup_kana)
            candidates_.push_back({kata});

        // Remaining candidates: kanji from anthy
        anthy_set_string(ctx, lookup_kana.c_str());

        struct anthy_conv_stat stat;
        if (anthy_get_stat(ctx, &stat) < 0)
            return;

        if (stat.nr_segment < 1)
            return;

        struct anthy_segment_stat seg;
        if (anthy_get_segment_stat(ctx, 0, &seg) < 0)
            return;

        int max_kanji = 9 - (int)candidates_.size();
        int count = seg.nr_candidate < max_kanji ? seg.nr_candidate : max_kanji;
        char buf[512];
        for (int i = 0; i < count; i++) {
            int len = anthy_get_segment(ctx, 0, i, buf, sizeof(buf));
            if (len > 0) {
                std::string s(buf);
                // Skip duplicates (anthy may return hiragana/katakana too)
                if (s != lookup_kana && s != kata)
                    candidates_.push_back({s});
            }
        }
    }
};

InputEngine *create_anthy_engine() {
    auto *e = new AnthyEngine();
    if (!e->ok()) {
        delete e;
        return nullptr;
    }
    return e;
}

#else

InputEngine *create_anthy_engine() {
    fprintf(stderr, "[wlime] japanese support not compiled (anthy not found)\n");
    return nullptr;
}

#endif
