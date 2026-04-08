#include "engine.h"
#include <pinyin.h>
#include <cstdio>

#define LIBPINYIN_DATADIR "/usr/lib/libpinyin/data"

class PinyinEngine : public InputEngine {
    pinyin_context_t *ctx = nullptr;
    pinyin_instance_t *inst = nullptr;
    std::string buffer;
    std::vector<Candidate> candidates_;
    // Keep raw pointers alive for pinyin_choose_candidate
    std::vector<lookup_candidate_t *> raw_candidates;

public:
    PinyinEngine() {
        ctx = pinyin_init(LIBPINYIN_DATADIR, nullptr);
        if (!ctx) {
            fprintf(stderr, "[wlime] failed to init libpinyin (datadir=%s)\n", LIBPINYIN_DATADIR);
            return;
        }

        pinyin_option_t options = USE_TONE | PINYIN_INCOMPLETE | DYNAMIC_ADJUST;
        pinyin_set_options(ctx, options);

        inst = pinyin_alloc_instance(ctx);
        if (!inst) {
            fprintf(stderr, "[wlime] failed to alloc pinyin instance\n");
            pinyin_fini(ctx);
            ctx = nullptr;
            return;
        }

        fprintf(stderr, "[wlime] pinyin engine initialized\n");
    }

    ~PinyinEngine() override {
        if (inst)
            pinyin_free_instance(inst);
        if (ctx) {
            pinyin_save(ctx);
            pinyin_fini(ctx);
        }
    }

    bool ok() const { return ctx && inst; }

    bool feed_key(xkb_keysym_t /*sym*/, const char *utf8) override {
        if (!utf8 || utf8[0] < 'a' || utf8[0] > 'z')
            return false;

        buffer += utf8[0];
        query();
        return true;
    }

    bool backspace() override {
        if (buffer.empty())
            return false;
        buffer.pop_back();
        if (buffer.empty()) {
            candidates_.clear();
            raw_candidates.clear();
            pinyin_reset(inst);
        } else {
            query();
        }
        return true;
    }

    std::string get_preedit() const override {
        return buffer;
    }

    const std::vector<Candidate> &get_candidates() const override {
        return candidates_;
    }

    std::string select(int index) override {
        if (index < 0 || index >= (int)candidates_.size())
            return "";

        std::string text = candidates_[index].text;

        pinyin_choose_candidate(inst, 0, raw_candidates[index]);
        pinyin_train(inst, 0);
        pinyin_save(ctx);

        buffer.clear();
        candidates_.clear();
        raw_candidates.clear();
        pinyin_reset(inst);
        return text;
    }

    void reset() override {
        buffer.clear();
        candidates_.clear();
        raw_candidates.clear();
        if (inst)
            pinyin_reset(inst);
    }

    bool empty() const override {
        return buffer.empty();
    }

    const char *display_name() const override { return "pinyin"; }
    const char *cjk_font() const override { return "Noto Sans CJK SC"; }

private:
    void query() {
        candidates_.clear();
        raw_candidates.clear();

        size_t parsed = pinyin_parse_more_full_pinyins(inst, buffer.c_str());
        if (parsed == 0)
            return;

        pinyin_guess_sentence(inst);
        pinyin_guess_candidates(inst, 0,
                                SORT_BY_PHRASE_LENGTH_AND_PINYIN_LENGTH_AND_FREQUENCY);

        guint n = 0;
        pinyin_get_n_candidate(inst, &n);

        int count = (int)n < 9 ? (int)n : 9;
        for (int i = 0; i < count; i++) {
            lookup_candidate_t *cand = nullptr;
            if (!pinyin_get_candidate(inst, i, &cand))
                continue;

            const gchar *str = nullptr;
            if (!pinyin_get_candidate_string(inst, cand, &str))
                continue;

            candidates_.push_back({std::string(str)});
            raw_candidates.push_back(cand);
        }
    }
};

InputEngine *create_pinyin_engine() {
    auto *e = new PinyinEngine();
    if (!e->ok()) {
        delete e;
        return nullptr;
    }
    return e;
}
