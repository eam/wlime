#include "engine.h"
#include <cstdio>

// Engine factories (defined in their respective files)
InputEngine *create_pinyin_engine();
InputEngine *create_hangul_engine();
InputEngine *create_anthy_engine();
InputEngine *create_rime_engine(const std::string &schema);

InputEngine *create_engine(const std::string &language) {
    if (language == "pinyin")
        return create_pinyin_engine();
    if (language == "korean")
        return create_hangul_engine();
    if (language == "japanese")
        return create_anthy_engine();

    // "rime:schema_id" — e.g. "rime:luna_pinyin", "rime:jyut6ping3"
    if (language.compare(0, 5, "rime:") == 0) {
        std::string schema = language.substr(5);
        if (schema.empty()) {
            fprintf(stderr, "[wlime] rime: no schema specified (use e.g. 'rime:luna_pinyin')\n");
            return nullptr;
        }
        return create_rime_engine(schema);
    }

    fprintf(stderr, "[wlime] unknown language '%s'\n", language.c_str());
    fprintf(stderr, "[wlime] supported: pinyin, korean, japanese, rime:<schema>\n");
    return nullptr;
}
