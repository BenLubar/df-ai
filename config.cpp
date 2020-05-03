#include "config.h"
#include "debug.h"

#include <algorithm>
#include <fstream>

#include "json/json.h"

Config config;
DebugCategoryConfig debug_category_config;

const static std::string config_name("dfhack-config/df-ai.json");

Config::Config() :
    random_embark(true),
    random_embark_world(""),
    write_console(true),
    write_log(true),
    record_movie(false),
    no_quit(true),
    embark_options(),
    world_size(1),
    camera(true),
    fps_meter(true),
    manage_labors("autolabor"),
    manage_nobles(true),
    cancel_announce(0),
    lockstep(false),
    allow_pause(true)
{
    for (int32_t & opt : embark_options)
    {
        opt = -1;
    }
    embark_options[embark_finder_option::DimensionX] = 3;
    embark_options[embark_finder_option::DimensionY] = 2;
    embark_options[embark_finder_option::AquiferLight] = 0;
    embark_options[embark_finder_option::AquiferHeavy] = 0;
    embark_options[embark_finder_option::Savagery] = 2;
}

void Config::load(color_ostream & out)
{
    std::ifstream f(config_name);
    if (f.good())
    {
        try
        {
            Json::Value v(Json::objectValue);
            f >> v;

            if (v.isMember("random_embark"))
            {
                random_embark = v["random_embark"].asBool();
            }
            if (v.isMember("random_embark_world"))
            {
                random_embark_world = v["random_embark_world"].asString();
            }
            if (v.isMember("write_console"))
            {
                write_console = v["write_console"].asBool();
            }
            else if (v.isMember("debug"))
            {
                write_console = v["debug"].asBool();
            }
            if (v.isMember("write_log"))
            {
                write_log = v["write_log"].asBool();
            }
            if (v.isMember("record_movie"))
            {
                record_movie = v["record_movie"].asBool();
            }
            if (v.isMember("no_quit"))
            {
                no_quit = v["no_quit"].asBool();
            }
            if (v.isMember("embark_options"))
            {
                auto & options = v["embark_options"];
                FOR_ENUM_ITEMS(embark_finder_option, o)
                {
                    auto name = ENUM_KEY_STR(embark_finder_option, o);
                    if (options.isMember(name))
                    {
                        embark_options[o] = options[name].asInt();
                    }
                }
            }
            if (v.isMember("world_size"))
            {
                world_size = std::min(std::max(int32_t(v["world_size"].asInt()), 0), 4);
            }
            if (v.isMember("camera"))
            {
                camera = v["camera"].asBool();
            }
            if (v.isMember("fps_meter"))
            {
                fps_meter = v["fps_meter"].asBool();
            }
            if (v.isMember("manage_labors"))
            {
                if (v["manage_labors"].isBool())
                {
                    if (v["manage_labors"].asBool())
                    {
                        manage_labors = "autolabor";
                    }
                    else
                    {
                        manage_labors = "";
                    }
                }
                else
                {
                    manage_labors = v["manage_labors"].asString();
                }
            }
            if (v.isMember("manage_nobles"))
            {
                manage_nobles = v["manage_nobles"].asBool();
            }
            if (v.isMember("cancel_announce"))
            {
                cancel_announce = v["cancel_announce"].asInt();
            }
            if (v.isMember("lockstep"))
            {
                lockstep = v["lockstep"].asBool();
            }
            if (v.isMember("allow_pause"))
            {
                allow_pause = v["allow_pause"].asBool();
            }
            if (v.isMember("plan_verbosity"))
            {
                debug_category_config.blueprint = v["plan_verbosity"].asInt();
                if (debug_category_config.blueprint < 0)
                {
                    debug_category_config.blueprint = 0;
                }
            }
#define DFAI_DEBUG_CATEGORY(x) \
            if (v.isMember(#x "_debug")) \
            { \
                debug_category_config.x = v[#x "_debug"].isBool() ? \
                    (v[#x "_debug"].asBool() ? 1 : 0) : \
                    v[#x "_debug"].asInt(); \
                if (debug_category_config.x < 0) \
                { \
                    out << "AI warning: debug levels cannot be negative (" #x << " level was set to " << debug_category_config.x << ")" << std::endl; \
                    debug_category_config.x = 0; \
                } \
                if (DFAI_IS_RELEASE && debug_category_config.x > 3) \
                { \
                    out << "AI warning: debug levels higher than 3 are not supported in release builds (" #x << " level is set to " << debug_category_config.x << ")" << std::endl; \
                } \
            } \
            else \
            { \
                debug_category_config.x = 0; \
            }
DFAI_DEBUG_CATEGORIES
#undef DFAI_DEBUG_CATEGORY
        }
        catch (Json::Exception & ex)
        {
            out << "AI: loading config failed: " << ex.what() << std::endl;
        }
    }
}

template<typename T>
inline void setComment(Json::Value & value, const T & t, const std::string & comment, Json::CommentPlacement placement = Json::commentBefore)
{
    value = t;
    value.setComment(comment, placement);
}

void Config::save(color_ostream & out)
{
    Json::Value v(Json::objectValue);
    setComment(v["random_embark"], random_embark, "// true or false: should the AI automatically pick an embark location?");
    setComment(v["random_embark_world"], random_embark_world, "// the name of the region to embark on, between quotes, or \"\" to generate a new world.");
    setComment(v["write_console"], write_console, "// true or false: should the AI say what it's thinking in the DFHack console?");
    setComment(v["write_log"], write_log, "// true or false: should the AI say what it's thinking in df-ai.log?");
    setComment(v["record_movie"], record_movie, "// true or false: should the AI automatically record CMV files while it plays?");
    setComment(v["no_quit"], no_quit, "// true or false: should the AI keep the game running after it loses?");

    Json::Value options(Json::objectValue);
    FOR_ENUM_ITEMS(embark_finder_option, o)
    {
        options[ENUM_KEY_STR(embark_finder_option, o)] = Json::Int(embark_options.at(o));
    }
    setComment(v["embark_options"], options, "// site finder options. -1 is \"N/A\", 0 is the first option, 1 is the second, and so on.");

    setComment(v["world_size"], Json::Int(world_size), "// 0: pocket, 1: smaller, 2: small, 3: medium, 4: large");
    setComment(v["camera"], camera, "// true or false: should the AI automatically follow dwarves it thinks are interesting?");
    setComment(v["fps_meter"], fps_meter, "// true or false: should the AI hide the FPS meter when in a menu? (the FPS meter is always hidden in lockstep mode)");
    setComment(v["manage_labors"], manage_labors, "// the name of a DFHack plugin that manages labors, in quotes, or \"\" for no automated labor management. \"autolabor\" and \"labormanger\" are specifically supported.");
    setComment(v["manage_nobles"], manage_nobles, "// true or false: should the AI assign administrators in the fortress?");
    setComment(v["cancel_announce"], Json::Int(cancel_announce), "// how many job cancellation notices to show. 0: none, 1: some, 2: most, 3: all");
    setComment(v["lockstep"], lockstep, "// true or false: should the AI make Dwarf Fortress think it's running at 100 simulation ticks, 50 graphical frames per second? this option is most useful when recording as lag will not affect animation speeds in the CMV files. the game will not accept input if this is set to true. does not work in TEXT mode.");
    setComment(v["allow_pause"], allow_pause, "// true or false: should df-ai allow the game to be paused?");

#define DFAI_DEBUG_CATEGORY(x) \
    if (!DFAI_IS_RELEASE || debug_category_config.x) \
        setComment(v[#x "_debug"], debug_category_config.x, "// hidden option; writes a LOT of information to df-ai-debug.log");
DFAI_DEBUG_CATEGORIES
#undef DFAI_DEBUG_CATEGORY

    std::ofstream f(config_name, std::ofstream::trunc);
    f << v;
    if (f.fail())
    {
        out << "failed to write " << config_name << std::endl;
    }
}
