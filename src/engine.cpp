#include "engine.h"
#include <cstdio>

#define LIBPINYIN_DATADIR "/usr/lib/libpinyin/data"

bool engine_init(Engine *eng) {
    eng->ctx = pinyin_init(LIBPINYIN_DATADIR, nullptr);
    if (!eng->ctx) {
        fprintf(stderr, "[wlime] failed to init libpinyin (datadir=%s)\n", LIBPINYIN_DATADIR);
        return false;
    }

    // Enable full pinyin with common options
    pinyin_option_t options = USE_TONE | PINYIN_INCOMPLETE | DYNAMIC_ADJUST;
    pinyin_set_options(eng->ctx, options);

    eng->inst = pinyin_alloc_instance(eng->ctx);
    if (!eng->inst) {
        fprintf(stderr, "[wlime] failed to alloc pinyin instance\n");
        pinyin_fini(eng->ctx);
        eng->ctx = nullptr;
        return false;
    }

    fprintf(stderr, "[wlime] pinyin engine initialized\n");
    return true;
}

void engine_destroy(Engine *eng) {
    if (eng->inst)
        pinyin_free_instance(eng->inst);
    if (eng->ctx) {
        pinyin_save(eng->ctx);
        pinyin_fini(eng->ctx);
    }
}

int engine_query(Engine *eng, const char *pinyin,
                 std::vector<Candidate> &candidates, int max_candidates) {
    candidates.clear();

    // Parse the full pinyin string
    size_t parsed = pinyin_parse_more_full_pinyins(eng->inst, pinyin);
    if (parsed == 0)
        return 0;

    // Let libpinyin guess the best sentence match
    pinyin_guess_sentence(eng->inst);

    // Get candidates at offset 0
    pinyin_guess_candidates(eng->inst, 0,
                            SORT_BY_PHRASE_LENGTH_AND_PINYIN_LENGTH_AND_FREQUENCY);

    guint n = 0;
    pinyin_get_n_candidate(eng->inst, &n);

    int count = (int)n < max_candidates ? (int)n : max_candidates;
    for (int i = 0; i < count; i++) {
        lookup_candidate_t *cand = nullptr;
        if (!pinyin_get_candidate(eng->inst, i, &cand))
            continue;

        const gchar *str = nullptr;
        if (!pinyin_get_candidate_string(eng->inst, cand, &str))
            continue;

        candidates.push_back({std::string(str), cand});
    }

    return (int)candidates.size();
}

void engine_select(Engine *eng, int index, const std::vector<Candidate> &candidates) {
    if (index < 0 || index >= (int)candidates.size())
        return;

    pinyin_choose_candidate(eng->inst, 0, candidates[index].raw);
    pinyin_train(eng->inst, 0);
    pinyin_save(eng->ctx);
}

void engine_reset(Engine *eng) {
    pinyin_reset(eng->inst);
}
