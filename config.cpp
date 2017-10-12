#include "config.h"

#include <algorithm>
#include <fstream>

#include "jsoncpp.h"

Config config;

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
    manage_labors(true),
    manage_nobles(true),
    cancel_announce(0),
    lockstep(false),
    lockstep_debug(false),
    plan_verbosity(2),
    tick_debug(false)
{
    for (int32_t i = 0; i < embark_options_count; i++)
    {
        embark_options[i] = -1;
    }
    embark_options[embark_finder_option::DimensionX] = 3;
    embark_options[embark_finder_option::DimensionY] = 2;
    embark_options[embark_finder_option::Aquifer] = 0;
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
                manage_labors = v["manage_labors"].asBool();
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
            if (v.isMember("lockstep_debug"))
            {
                lockstep_debug = v["lockstep_debug"].asBool();
            }
            if (v.isMember("plan_verbosity"))
            {
                plan_verbosity = v["plan_verbosity"].asInt();
            }
            if (v.isMember("tick_debug"))
            {
                tick_debug = v["tick_debug"].asBool();
            }
        }
        catch (Json::Exception & ex)
        {
            out << "AI: loading config failed: " << ex.what() << std::endl;
        }
    }
}

void Config::save(color_ostream & out)
{
    Json::Value v(Json::objectValue);
    v["random_embark"] = random_embark;
    v["random_embark_world"] = random_embark_world;
    v["write_console"] = write_console;
    v["write_log"] = write_log;
    v["record_movie"] = record_movie;
    v["no_quit"] = no_quit;

    Json::Value options(Json::objectValue);
    FOR_ENUM_ITEMS(embark_finder_option, o)
    {
        options[ENUM_KEY_STR(embark_finder_option, o)] = Json::Int(embark_options[o]);
    }
    v["embark_options"] = options;

    v["world_size"] = Json::Int(world_size);
    v["camera"] = camera;
    v["fps_meter"] = fps_meter;
    v["manage_labors"] = manage_labors;
    v["manage_nobles"] = manage_nobles;
    v["cancel_announce"] = cancel_announce;
    v["lockstep"] = lockstep;
    if (lockstep_debug) // hide from config if not set
        v["lockstep_debug"] = true;
    v["plan_verbosity"] = plan_verbosity;
    if (tick_debug) // hide from config if not set
        v["tick_debug"] = true;

    std::ofstream f(config_name, std::ofstream::trunc);
    f << v;
    if (f.fail())
    {
        out << "failed to write " << config_name << std::endl;
    }
}
