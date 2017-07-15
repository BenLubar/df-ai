#pragma once

#include "dfhack_shared.h"
#include "room.h"

#include "jsoncpp.h"

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

        bool has_users;
        bool ignore;
        bool makeroom;
        bool internal;

        std::string comment;
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

        std::string raw_type;

        std::string comment;

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

        bool has_users;
        bool temporary;
        bool outdoor;

        bool in_corridor;
    };

    ~room_base();

    bool apply(Json::Value data, std::string & error, bool allow_placeholders = false);

    std::vector<furniture_t *> layout;
    std::vector<room_t *> rooms;
};

struct room_template : public room_base
{
    bool apply(Json::Value data, std::string & error);

    placeholderindex_t min_placeholders;
};

struct room_instance : public room_base
{
    ~room_instance();

    bool apply(Json::Value data, std::string & error);

    std::vector<Json::Value *> placeholders;
};

struct room_blueprint
{
    room_blueprint(const room_template *tmpl, const room_instance *inst);
    room_blueprint(const room_blueprint & rb, df::coord offset = df::coord(0, 0, 0));
    ~room_blueprint();

    df::coord origin;
    const room_template *tmpl;
    const room_instance *inst;

    std::vector<room_base::furniture_t *> layout;
    std::vector<room_base::room_t *> rooms;

    std::set<df::coord> interior;
    std::set<df::coord> no_room;
    std::set<df::coord> no_corridor;

    bool apply(std::string & error);
    void build_cache();
};

struct blueprint_plan
{
    ~blueprint_plan();

    std::vector<room_base::furniture_t *> layout;
    std::vector<room_base::room_t *> rooms;

    std::map<df::coord, room_base::roomindex_t> corridor_connect;
    std::set<df::coord> corridor;
    std::set<df::coord> interior;
    std::set<df::coord> no_room;
    std::set<df::coord> no_corridor;

    bool add(const room_blueprint & rb, std::string & error);
    bool build_corridor_to(const room_blueprint & rb, room_base::roomindex_t & parent, std::string & error);
    void create(std::vector<room *> & real_corridors, std::vector<room *> & real_rooms) const;
};

class blueprints_t
{
public:
    blueprints_t(color_ostream & out);
    ~blueprints_t();

    std::vector<const room_blueprint *> operator[](const std::string & type) const;

private:
    std::map<std::string, std::vector<room_blueprint *>> blueprints;
};
