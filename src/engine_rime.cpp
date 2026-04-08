#include "engine.h"
#include <cstdio>
#include <string>

#ifdef HAVE_RIME
#include <rime_api.h>

static bool g_rime_initialized = false;

class RimeEngine : public InputEngine {
    RimeApi *api = nullptr;
    RimeSessionId session = 0;
    std::string schema_id;
    std::string schema_display_name;
    std::string font_name;
    std::vector<Candidate> candidates_;
    std::string preedit_;
    std::string raw_input_;

public:
    RimeEngine(const std::string &schema) : schema_id(schema) {
        api = rime_get_api();
        if (!api) {
            fprintf(stderr, "[wlime] failed to get rime API\n");
            return;
        }

        if (!g_rime_initialized) {
            RIME_STRUCT(RimeTraits, traits);
            traits.shared_data_dir = "/usr/share/rime-data";

            // User data dir for learned phrases, build cache, etc.
            static std::string user_dir;
            const char *home = getenv("HOME");
            if (home)
                user_dir = std::string(home) + "/.local/share/wlime/rime";
            else
                user_dir = "/tmp/wlime-rime";
            traits.user_data_dir = user_dir.c_str();

            traits.distribution_name = "wlime";
            traits.distribution_code_name = "wlime";
            traits.distribution_version = "0.1.0";
            traits.app_name = "rime.wlime";
            traits.min_log_level = 2; // ERROR only
            traits.log_dir = ""; // stderr only

            api->setup(&traits);
            api->initialize(&traits);

            // Deploy schemas (builds if needed, fast if already built)
            api->start_maintenance(False);
            api->join_maintenance_thread();

            g_rime_initialized = true;
            fprintf(stderr, "[wlime] rime initialized (version %s)\n",
                    api->get_version());
        }

        // Create session
        session = api->create_session();
        if (!session) {
            fprintf(stderr, "[wlime] failed to create rime session\n");
            return;
        }

        // Select the requested schema
        if (!api->select_schema(session, schema_id.c_str())) {
            fprintf(stderr, "[wlime] failed to select rime schema '%s'\n",
                    schema_id.c_str());
            // List available schemas
            RimeSchemaList list;
            if (api->get_schema_list(&list)) {
                fprintf(stderr, "[wlime] available schemas:\n");
                for (size_t i = 0; i < list.size; i++)
                    fprintf(stderr, "  - %s (%s)\n",
                            list.list[i].schema_id, list.list[i].name);
                api->free_schema_list(&list);
            }
            api->destroy_session(session);
            session = 0;
            return;
        }

        // Get schema display name
        RIME_STRUCT(RimeStatus, status);
        if (api->get_status(session, &status)) {
            if (status.schema_name)
                schema_display_name = status.schema_name;
            api->free_status(&status);
        }
        if (schema_display_name.empty())
            schema_display_name = schema_id;

        // Pick CJK font based on schema
        font_name = guess_font();

        fprintf(stderr, "[wlime] rime engine ready: %s (%s)\n",
                schema_id.c_str(), schema_display_name.c_str());
    }

    ~RimeEngine() override {
        if (session && api)
            api->destroy_session(session);
        // Don't finalize rime — other sessions may exist, and it's process-global
    }

    bool ok() const { return api && session; }

    bool feed_key(xkb_keysym_t sym, const char *utf8) override {
        (void)utf8;

        // Map xkb keysym directly — RIME uses X11 keysyms which are the same
        int keycode = (int)sym;
        int mask = 0;

        Bool handled = api->process_key(session, keycode, mask);

        sync_state();
        return handled;
    }

    bool backspace() override {
        Bool handled = api->process_key(session, 0xff08, 0); // XK_BackSpace
        sync_state();
        return handled;
    }

    std::string get_preedit() const override {
        return preedit_;
    }

    const std::vector<Candidate> &get_candidates() const override {
        return candidates_;
    }

    std::string select(int index) override {
        // RIME uses page-relative selection
        api->select_candidate_on_current_page(session, index);

        // Check if that triggered a commit
        RIME_STRUCT(RimeCommit, commit);
        std::string result;
        if (api->get_commit(session, &commit)) {
            if (commit.text)
                result = commit.text;
            api->free_commit(&commit);
        }

        // If no commit yet (multi-segment input), sync and keep going
        if (result.empty()) {
            sync_state();
            // If composition ended, collect whatever's there
            if (preedit_.empty() && candidates_.empty()) {
                // Nothing left — the commit happened via process_key
            }
        } else {
            // Committed — clear our state
            preedit_.clear();
            candidates_.clear();
        }

        return result;
    }

    void reset() override {
        if (session && api) {
            api->clear_composition(session);
            // Drain any pending commit
            RIME_STRUCT(RimeCommit, commit);
            if (api->get_commit(session, &commit))
                api->free_commit(&commit);
        }
        preedit_.clear();
        candidates_.clear();
    }

    bool empty() const override {
        return preedit_.empty();
    }

    const char *display_name() const override {
        return schema_display_name.c_str();
    }

    const char *cjk_font() const override {
        return font_name.c_str();
    }

private:
    void sync_state() {
        candidates_.clear();
        preedit_.clear();

        RIME_STRUCT(RimeContext, ctx);
        if (!api->get_context(session, &ctx))
            return;

        // Preedit: use raw input if available, fall back to composition preedit
        const char *raw = api->get_input(session);
        if (raw && raw[0])
            preedit_ = raw;
        else if (ctx.composition.preedit)
            preedit_ = ctx.composition.preedit;

        // Candidates from the current page
        if (ctx.menu.num_candidates > 0) {
            int count = ctx.menu.num_candidates < 9 ? ctx.menu.num_candidates : 9;
            for (int i = 0; i < count; i++) {
                if (ctx.menu.candidates[i].text)
                    candidates_.push_back({std::string(ctx.menu.candidates[i].text)});
            }
        }

        api->free_context(&ctx);
    }

    std::string guess_font() const {
        // Guess appropriate CJK font from schema name
        if (schema_id.find("jyut") != std::string::npos ||
            schema_id.find("cantonese") != std::string::npos ||
            schema_id.find("cangjie") != std::string::npos ||
            schema_id.find("pinyin") != std::string::npos ||
            schema_id.find("luna") != std::string::npos ||
            schema_id.find("wubi") != std::string::npos ||
            schema_id.find("bopomofo") != std::string::npos)
            return "Noto Sans CJK SC";

        if (schema_id.find("japanese") != std::string::npos ||
            schema_id.find("kana") != std::string::npos)
            return "Noto Sans CJK JP";

        if (schema_id.find("korean") != std::string::npos ||
            schema_id.find("hangul") != std::string::npos ||
            schema_id.find("hangeu") != std::string::npos)
            return "Noto Sans CJK KR";

        // Default to SC for CJK
        return "Noto Sans CJK SC";
    }
};

InputEngine *create_rime_engine(const std::string &schema) {
    auto *e = new RimeEngine(schema);
    if (!e->ok()) {
        delete e;
        return nullptr;
    }
    return e;
}

#else

InputEngine *create_rime_engine(const std::string &schema) {
    (void)schema;
    fprintf(stderr, "[wlime] rime support not compiled (librime not found)\n");
    return nullptr;
}

#endif
