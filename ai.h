#pragma once

#include "dfhack_shared.h"
#include "config.h"
#include "room.h"

#include <ctime>
#include <fstream>
#include <functional>
#include <list>
#include <random>

#include "df/coord.h"
#include "df/interface_key.h"
#include "df/language_name.h"

#include "json/json.h"

namespace df
{
    struct activity_event_conflictst;
    struct history_event;
    struct item;
    struct job;
    struct manager_order;
    struct manager_order_template;
    struct report;
    struct unit;
    struct viewscreen;
}

namespace DFHack
{
    namespace Maps
    {
        uint16_t getTileWalkable(df::coord t);
    }
}

struct OnupdateCallback;
class Population;
class Plan;
class Stocks;
class Camera;
class Trade;

std::string html_escape(const std::string & str);
void ai_version(std::ostream & out, bool html = false);
class weblegends_handler_v1;
bool ai_weblegends_handler(weblegends_handler_v1 & out, const std::string & url);

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
    Trade *trade;

    OnupdateCallback *status_onupdate;
    OnupdateCallback *pause_onupdate;
    OnupdateCallback *tag_enemies_onupdate;
    std::set<std::string> seen_focus;
    std::set<std::string> seen_cvname;
    int32_t last_good_x, last_good_y, last_good_z;
    bool skip_persist;
    char lockstep_log_buffer[25][80];

    AI();
    ~AI();

    static std::string timestamp(int32_t y, int32_t t);
    static std::string timestamp();

    static std::string describe_name(const df::language_name & name, bool in_english = false, bool only_last_part = false);
    static std::string describe_item(df::item *i);
    static std::string describe_unit(df::unit *u, bool html = false);
    static std::string describe_job(const df::job *job);
    static std::string describe_job(const df::manager_order *job);
    static std::string describe_job(const df::manager_order_template *job);
    static std::string describe_job(const df::unit *u);
    static std::string describe_event(df::history_event *event);

    static bool is_dwarfmode_viewscreen();

    static void write_df(std::ostream & out, const std::string & str, const std::string & newline = "\n", const std::string & suffix = "\n", std::function<std::string(const std::string &)> translate = DF2UTF);
    void write_lockstep(std::string str);

    void debug(color_ostream & out, const std::string & str, df::coord announce);
    void debug(color_ostream & out, const std::string & str);

    void event(const std::string & name, const Json::Value & payload);

    command_result startup(color_ostream & out);

    void unpause();
    void handle_pause_event(color_ostream & out, df::report *announce);
    void statechanged(color_ostream & out, state_change_event event);
    static void abandon(color_ostream & out);
    bool tag_enemies(color_ostream & out);
    static df::unit *is_attacking_citizen(df::unit *u);
    static df::unit *is_hunting_target(df::unit *u);
    static bool is_in_conflict(df::unit *u, std::function<bool(df::activity_event_conflictst *)> filter = [](df::activity_event_conflictst *) -> bool { return true; });

    void timeout_sameview(int32_t seconds, std::function<void(color_ostream &)> cb);
    void timeout_sameview(std::function<void(color_ostream &)> cb)
    {
        timeout_sameview(5, cb);
    }
    void ignore_pause(int32_t x, int32_t y, int32_t z);

    static std::string describe_room(room *r, bool html = false);
    static std::string describe_furniture(furniture *f, bool html = false);

    static void dig_tile(df::coord t, df::tile_dig_designation dig = tile_dig_designation::Default);

    df::coord fort_entrance_pos();
    room *find_room(room_type::type type);
    room *find_room(room_type::type type, std::function<bool(room *)> b);
    room *find_room_at(df::coord t);
    inline bool map_tile_intersects_room(df::coord t)
    {
        return find_room_at(t) != nullptr;
    }

    static df::coord spiral_search(df::coord t, int16_t max, int16_t min, int16_t step, std::function<bool(df::coord)> b);
    static inline df::coord spiral_search(df::coord t, int16_t max, int16_t min, std::function<bool(df::coord)> b)
    {
        return spiral_search(t, max, min, 1, b);
    }
    static inline df::coord spiral_search(df::coord t, int16_t max, std::function<bool(df::coord)> b)
    {
        return spiral_search(t, max, 0, 1, b);
    }
    static inline df::coord spiral_search(df::coord t, std::function<bool(df::coord)> b)
    {
        return spiral_search(t, 100, 0, 1, b);
    }

    command_result onupdate_register(color_ostream & out);
    command_result onupdate_unregister(color_ostream & out);

    std::string status();
    std::string report(bool html = false);

    command_result persist(color_ostream & out);
    command_result unpersist(color_ostream & out);
};
