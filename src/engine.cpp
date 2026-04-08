#include "engine.h"
#include <cstdio>
#include <utility>

// Engine factories (defined in their respective files)
InputEngine *create_pinyin_engine();
InputEngine *create_hangul_engine();
InputEngine *create_anthy_engine(int mode); // 0=kanji, 1=hiragana, 2=katakana
InputEngine *create_rime_engine(const std::string &schema);
void list_rime_schemas(std::vector<std::pair<std::string, std::string>> &out);

InputEngine *create_engine(const std::string &language) {
    if (language == "pinyin")
        return create_pinyin_engine();
    if (language == "korean")
        return create_hangul_engine();
    if (language == "japanese" || language == "japanese-kanji")
        return create_anthy_engine(0);
    if (language == "hiragana" || language == "japanese-hiragana")
        return create_anthy_engine(1);
    if (language == "katakana" || language == "japanese-katakana")
        return create_anthy_engine(2);

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
    fprintf(stderr, "[wlime] supported: pinyin, korean, japanese, hiragana, katakana, rime:<schema>\n");
    return nullptr;
}

void list_languages() {
    printf("pinyin\n");
#ifdef HAVE_HANGUL
    printf("korean\n");
#endif
#ifdef HAVE_ANTHY
    printf("japanese\n");
    printf("hiragana\n");
    printf("katakana\n");
#endif
#ifdef HAVE_RIME
    std::vector<std::pair<std::string, std::string>> schemas;
    list_rime_schemas(schemas);
    for (auto &s : schemas)
        printf("rime:%s\n", s.first.c_str());
#endif
}
