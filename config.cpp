#include "config.h"

#include <fstream>

#include "jsoncpp.h"

Config config;

const static std::string config_name("dfhack-config/df-ai.json");

Config::Config() :
    random_embark(true),
    random_embark_world(""),
    debug(true),
    record_movie(false),
    no_quit(true)
{
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
            if (v.isMember("record_movie"))
            {
                record_movie = v["record_movie"].asBool();
            }
            if (v.isMember("no_quit"))
            {
                no_quit = v["no_quit"].asBool();
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
    v["record_movie"] = record_movie;
    v["no_quit"] = no_quit;

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
