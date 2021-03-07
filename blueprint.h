#pragma once

#include "dfhack_shared.h"
#include "room.h"
#include "plan_priorities.h"
#include "stocks.h"
#include "variable_string.h"

#include "json/json.h"

#include <functional>

class AI;

struct room_blueprint;

struct room_base
{
    typedef size_t layoutindex_t;
    typedef size_t roomindex_t;
    typedef size_t placeholderindex_t;
    struct furniture_t
    {
        furniture_t();

        bool apply(Json::Value data, std::string & error, bool allow_placeholders = false);
        void shift(layoutindex_t layout_start, roomindex_t room_start);
        bool check_indexes(layoutindex_t layout_limit, roomindex_t room_limit, std::string & error) const;

        bool has_placeholder;
        placeholderindex_t placeholder;

        layout_type::type type;
        df::construction_type construction;
        df::tile_dig_designation dig;
        df::coord pos;

        bool has_target;
        layoutindex_t target;

        size_t has_users;
        bool ignore;
        bool makeroom;
        bool internal;
        bool stairs_special;

        variable_string comment;

        variable_string::context_t context;
    };
    struct room_t
    {
        room_t();

        bool apply(Json::Value data, std::string & error, bool allow_placeholders = false);
        void shift(layoutindex_t layout_start, roomindex_t room_start);
        bool check_indexes(layoutindex_t layout_limit, roomindex_t room_limit, std::string & error) const;

        bool has_placeholder;
        placeholderindex_t placeholder;

        room_type::type type;

        corridor_type::type corridor_type;
        farm_type::type farm_type;
        stockpile_type::type stockpile_type;
        nobleroom_type::type nobleroom_type;
        outpost_type::type outpost_type;
        location_type::type location_type;
        cistern_type::type cistern_type;
        df::workshop_type workshop_type;
        df::furnace_type furnace_type;

        variable_string raw_type;

        variable_string comment;

        df::coord min, max;

        std::vector<roomindex_t> accesspath;
        std::vector<layoutindex_t> layout;

        int32_t level;
        int32_t noblesuite;
        int32_t queue;

        bool has_workshop;
        roomindex_t workshop;

        std::set<df::stockpile_list> stock_disable;
        bool stock_specific1;
        bool stock_specific2;

        size_t has_users;
        bool temporary;
        bool outdoor;
        bool single_biome;

        bool require_walls;
        bool require_floor;
        int32_t require_grass;
        bool require_stone;
        bool in_corridor;
        bool remove_if_unused;
        bool build_when_accessible;
        std::map<df::coord, std::map<std::string, std::map<std::string, variable_string>>> exits;

        variable_string::context_t context;
        std::string blueprint;
    };

    ~room_base();

    bool apply(Json::Value data, std::string & error, bool allow_placeholders = false);

    std::vector<furniture_t *> layout;
    std::vector<room_t *> rooms;
};

struct room_template : public room_base
{
    room_template(const std::string &, const std::string & name) : name(name), min_placeholders() {}

    bool apply(Json::Value data, std::string & error);

    const std::string name;
    placeholderindex_t min_placeholders;
};

struct room_instance : public room_base
{
    room_instance(const std::string & type, const std::string & name) : type(type), name(name), blacklist(), placeholders() {}
    ~room_instance();

    bool apply(Json::Value data, std::string & error);

    const std::string type, name;
    std::set<std::string> blacklist;
    std::vector<Json::Value *> placeholders;
};

struct room_blueprint
{
    room_blueprint(const room_template *tmpl, const room_instance *inst);
    room_blueprint(const room_blueprint & rb);
    room_blueprint(const room_blueprint & rb, df::coord offset, const variable_string::context_t & context);
    ~room_blueprint();

    df::coord origin;
    const room_template *tmpl;
    const room_instance *inst;

    const std::string type, tmpl_name, name;

    std::vector<room_base::furniture_t *> layout;
    std::vector<room_base::room_t *> rooms;

    int32_t max_noblesuite;
    std::set<df::coord> corridor;
    std::set<df::coord> interior;
    std::set<df::coord> no_room;
    std::set<df::coord> no_corridor;

    bool apply(std::string & error);
    bool warn(std::string & error);
    void build_cache();
    void write_layout(std::ostream & f);
};

struct blueprint_plan_template
{
    blueprint_plan_template(const std::string &, const std::string & name) :
        name(name),
        max_retries(25),
        max_failures(100)
    {
    }

    const std::string name;
    size_t max_retries;
    size_t max_failures;
    std::string start;
    std::set<std::string> outdoor;
    std::map<std::string, std::set<std::string>> tags;
    std::map<std::string, std::map<std::string, size_t>> count_as;
    std::map<std::string, std::pair<size_t, size_t>> limits;
    std::map<std::string, std::map<std::string, std::pair<size_t, size_t>>> instance_limits;
    variable_string::context_t context;
    std::pair<int16_t, int16_t> padding_x;
    std::pair<int16_t, int16_t> padding_y;
    std::vector<plan_priority_t> priorities;
    struct Watch stock_goals;

    bool apply(Json::Value data, std::string & error);
};

class blueprints_t
{
public:
    blueprints_t(color_ostream & out);
    ~blueprints_t();

    bool is_valid;
    void write_rooms(std::ostream & f);

private:
    std::map<std::string, std::vector<room_blueprint *>> blueprints;
    std::map<std::string, blueprint_plan_template *> plans;
    friend class PlanSetup;
};
