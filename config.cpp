#include "config.h"

#include <algorithm>
#include <fstream>

#include "jsoncpp.h"

Config config;

const static std::string config_name("dfhack-config/df-ai.json");

Config::Config() :
    random_embark(true),
    random_embark_world(""),
    debug(true),
    write_log(true),
    record_movie(false),
    no_quit(true),
    embark_options(),
    world_size(1),
    camera(true),
    fps_meter(true),
    manage_labors(true),
    manage_nobles(true)
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
            if (v.isMember("debug"))
            {
                debug = v["debug"].asBool();
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
    v["debug"] = debug;
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

    std::ofstream f(config_name, std::ofstream::trunc);
    f << v;
    if (f.fail())
    {
        out << "failed to write " << config_name << std::endl;
    }
}

void Config::set_random_embark_world(color_ostream & out, const std::string & value)
{
    if (random_embark_world == value)
    {
        return;
    }
    random_embark_world = value;
    save(out);
}
