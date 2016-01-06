#pragma once

#include "event_manager.h"
#include "room.h"
#include "horrible.h"

#include <functional>
#include <list>
#include <map>
#include <set>

#include "df/coord.h"
#include "df/tiletype_shape_basic.h"

namespace df
{
    struct building;
    struct building_stockpilest;
    struct item;
}

class AI;

typedef std::vector<horrible_t> task;

class Plan
{
    AI *ai;
    OnupdateCallback *onupdate_handle;
    size_t nrdig;
    std::list<task *> tasks;
    std::list<task *>::iterator bg_idx;
    std::vector<room *> rooms;
    std::map<std::string, std::vector<room *>> room_category;
    std::vector<room *> corridors;
    std::set<std::string> cache_nofurnish;
    room *fort_entrance;
    std::map<int32_t, std::set<df::coord>> map_veins;
    std::vector<std::string> important_workshops;
    std::vector<std::string> important_workshops2;
    size_t checkroom_idx;
    bool allow_ice;
    bool past_initial_phase;

public:
    Plan(AI *ai);
    ~Plan();

    command_result startup(color_ostream & out);
    command_result onupdate_register(color_ostream & out);
    command_result onupdate_unregister(color_ostream & out);

    void update(color_ostream & out);

    task *is_digging();
    bool is_idle();

    void new_citizen(color_ostream & out, int32_t uid);
    void del_citizen(color_ostream & out, int32_t uid);

    bool checkidle(color_ostream & out);
    void idleidle(color_ostream & out);

    void checkrooms(color_ostream & out);
    void checkroom(color_ostream & out, room *r);

    void getbedroom(color_ostream & out, int32_t id);
    void getdiningroom(color_ostream & out, int32_t id);
    void attribute_noblerooms(color_ostream & out, const std::set<int32_t> & id_list);

    void getsoldierbarrack(color_ostream & out, int32_t id);
    void assign_barrack_squad(color_ostream & out, df::building *bld, int32_t squad_id);

    void getcoffin(color_ostream & out);

    void freebedroom(color_ostream & out, int32_t id);
    void freecommonrooms(color_ostream & out, int32_t id, std::string subtype);
    void freecommonrooms(color_ostream & out, int32_t id);
    void freesoldierbarrack(color_ostream & out, int32_t id);

    df::building *getpasture(color_ostream & out, int32_t pet_id);
    void freepasture(color_ostream & out, int32_t pet_id);

    void set_owner(color_ostream & out, room *r, int32_t uid);

    void wantdig(color_ostream & out, room *r);
    void digroom(color_ostream & out, room *r);
    bool construct_room(color_ostream & out, room *r);
    bool furnish_room(color_ostream & out, room *r);
    bool try_furnish(color_ostream & out, room *r, furniture *f);
    bool try_furnish_well(color_ostream & out, room *r, furniture *f, df::coord t);
    bool try_furnish_archerytarget(color_ostream & out, room *r, furniture *f, df::coord t);
    bool try_furnish_construction(color_ostream & out, room *r, furniture *f, df::coord t);
    bool try_furnish_windmill(color_ostream & out, room *r, furniture *f, df::coord t);
    bool try_furnish_roller(color_ostream & out, room *r, furniture *f, df::coord t);
    bool try_furnish_minecart_route(color_ostream & out, room *r, furniture *f, df::coord t);
    bool try_furnish_trap(color_ostream & out, room *r, furniture *f);

    bool try_construct_workshop(color_ostream & out, room *r);
    bool try_construct_stockpile(color_ostream & out, room *r);
    bool try_construct_activityzone(color_ostream & out, room *r);

    void setup_stockpile_settings(color_ostream & out, std::string subtype, df::building_stockpilest *bld, room *r = nullptr);

    bool construct_farmplot(color_ostream & out, room *r);

    void move_dininghall_fromtemp(color_ostream & out, room *r, room *t);

    void smooth_room(color_ostream & out, room *r);
    void smooth_room_access(color_ostream & out, room *r);
    void smooth_cistern(color_ostream & out, room *r);
    bool construct_cistern(color_ostream & out, room *r);
    bool dump_items_access(color_ostream & out, room *r);
    void room_items(color_ostream & out, std::function<void(df::item *)>);
    void smooth_xyz(df::coord min, df::coord max);
    void smooth(std::set<df::coord> tiles);
    bool is_smooth(df::coord t);

    bool try_digcistern(color_ostream & out, room *r);
    void dig_garbagedump(color_ostream & out);
    bool try_diggarbage(color_ostream & out, room *r);
    bool try_setup_farmplot(color_ostream & out, room *r);
    bool try_endfurnish(color_ostream & out, room *r, furniture *f);

    bool setup_lever(color_ostream & out, room *r, furniture *f);
    bool link_lever(color_ostream & out, furniture *src, furniture *dst);
    bool pull_lever(color_ostream & out, furniture *f);

    void monitor_cistern(color_ostream & out);

    bool try_endconstruct(color_ostream & out, room *r);

    df::coord scan_river(color_ostream & out);

    command_result setup_blueprint(color_ostream & out);
    void make_map_walkable(color_ostream & out);
    void list_map_veins(color_ostream & out);

    size_t dig_vein(color_ostream & out, int32_t mat, size_t want_boulders = 1);
    size_t do_dig_vein(color_ostream & out, int32_t mat, int32_t bx, int32_t by, int32_t bz);

    static df::coord spiral_search(df::coord t, int32_t max, int32_t min, int32_t step, std::function<bool(df::coord)> b);
    static inline df::coord spiral_search(df::coord t, int32_t max, int32_t min, std::function<bool(df::coord)> b)
    {
        return spiral_search(t, max, min, 1, b);
    }
    static inline df::coord spiral_search(df::coord t, int32_t max, std::function<bool(df::coord)> b)
    {
        return spiral_search(t, max, 0, 1, b);
    }
    static inline df::coord spiral_search(df::coord t, std::function<bool(df::coord)> b)
    {
        return spiral_search(t, 100, 0, 1, b);
    }

    const static int32_t MIN_X, MIN_Y, MIN_Z;
    const static int32_t MAX_X, MAX_Y, MAX_Z;

    command_result scan_fort_entrance(color_ostream & out);
    command_result scan_fort_body(color_ostream & out);
    command_result setup_blueprint_rooms(color_ostream & out);
    command_result setup_blueprint_workshops(color_ostream & out, df::coord f, std::vector<room *> entr);
    command_result setup_blueprint_stockpiles(color_ostream & out, df::coord f, std::vector<room *> entr);
    command_result setup_blueprint_minecarts(color_ostream & out);
    command_result setup_blueprint_pitcage(color_ostream & out);
    command_result setup_blueprint_utilities(color_ostream & out, df::coord f, std::vector<room *> entr);
    command_result setup_blueprint_cistern_fromsource(color_ostream & out, df::coord src, df::coord f);
    command_result setup_blueprint_pastures(color_ostream & out);
    command_result setup_blueprint_outdoor_farms(color_ostream & out);
    command_result setup_blueprint_bedrooms(color_ostream & out, df::coord f, std::vector<room *> entr);
    command_result setup_outdoor_gathering_zones(color_ostream & out);
    command_result setup_blueprint_caverns(color_ostream & out);

    bool map_tile_in_rock(df::coord tile);
    bool map_tile_nocavern(df::coord tile);
    bool map_tile_cavernfloor(df::coord tile);

    std::vector<room *> find_corridor_tosurface(color_ostream & out, df::coord origin);

    df::coord surface_tile_at(int16_t tx, int16_t ty, bool allow_trees = false);

    std::string status();

    void categorize_all();

    std::string describe_room(room *r);

    room *find_room(std::string type);
    room *find_room(std::string type, std::function<bool(room *)> b);

    bool map_tile_intersects_room(df::coord t);

    static df::coord find_tree_base(df::coord t);
};

// vim: et:sw=4:ts=4
