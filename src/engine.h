#pragma once

#include <pinyin.h>
#include <string>
#include <vector>

struct Engine {
    pinyin_context_t *ctx;
    pinyin_instance_t *inst;
};

struct Candidate {
    std::string text;           // the Chinese string
    lookup_candidate_t *raw;    // libpinyin's opaque pointer (valid until next query)
};

// Initialize the pinyin engine. Returns false on failure.
bool engine_init(Engine *eng);
void engine_destroy(Engine *eng);

// Feed a raw pinyin string (e.g. "nihao") and get candidates back.
// Returns the number of candidates (up to max_candidates).
int engine_query(Engine *eng, const char *pinyin,
                 std::vector<Candidate> &candidates, int max_candidates = 9);

// Tell the engine the user chose this candidate (for learning).
void engine_select(Engine *eng, int index, const std::vector<Candidate> &candidates);

// Reset the engine state for a new input session.
void engine_reset(Engine *eng);
