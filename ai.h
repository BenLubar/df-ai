#pragma once

#include "dfhack_shared.h"

#include <ctime>
#include <fstream>
#include <functional>
#include <list>
#include <random>

#include "df/coord.h"
#include "df/interface_key.h"
#include "df/language_name.h"

#include "jsoncpp.h"

namespace df
{
    struct item;
    struct report;
    struct unit;
    struct viewscreen;
}

const bool AI_RANDOM_EMBARK = true;
const bool DEBUG = true;
const bool RECORD_MOVIE = true;
const bool NO_QUIT = true;

struct OnupdateCallback;
class Population;
class Plan;
class Stocks;
class Camera;
class Embark;

class AI
{
public:
    std::mt19937 rng;
    std::ofstream logger;
    std::ofstream eventsJson;
    Population *pop;
    Plan *plan;
    Stocks *stocks;
    Camera *camera;
    Embark *embark;

    OnupdateCallback *status_onupdate;
    OnupdateCallback *pause_onupdate;
    OnupdateCallback *tag_enemies_onupdate;
    std::set<std::string> seen_cvname;
    bool skip_persist;

    AI();
    ~AI();

    static std::string timestamp(int32_t y, int32_t t);
    static std::string timestamp();

    static std::string describe_name(const df::language_name & name, bool in_english = false, bool only_last_part = false);
    static std::string describe_item(df::item *i);
    static std::string describe_unit(df::unit *u);

    static bool feed_key(df::viewscreen *view, df::interface_key key);
    static bool feed_key(df::interface_key key);

    static bool is_dwarfmode_viewscreen();

    static void write_df(std::ostream & out, const std::string & str, const std::string & newline = "\n", const std::string & suffix = "\n", std::function<std::string(const std::string &)> translate = DF2UTF);

    void debug(color_ostream & out, const std::string & str, df::coord announce);
    void debug(color_ostream & out, const std::string & str);

    void event(const std::string & name, const Json::Value & payload);

    command_result startup(color_ostream & out);

    static void unpause();
    void handle_pause_event(color_ostream & out, df::report *announce);
    void statechanged(color_ostream & out, state_change_event event);
    static void abandon(color_ostream & out);
    bool tag_enemies(color_ostream & out);

    void timeout_sameview(std::time_t delay, std::function<void(color_ostream &)> cb);
    void timeout_sameview(std::function<void(color_ostream &)> cb)
    {
        timeout_sameview(5, cb);
    }

    command_result onupdate_register(color_ostream & out);
    command_result onupdate_unregister(color_ostream & out);

    std::string status();

    command_result persist(color_ostream & out);
    command_result unpersist(color_ostream & out);
};

// vim: et:sw=4:ts=4
