#pragma once

#include "Core.h"
#include <Console.h>
#include <Export.h>
#include <PluginManager.h>

#include "DataDefs.h"

#include <ctime>
#include <list>
#include <random>

#include "df/job_type.h"
#include "df/tiletype_shape_basic.h"
#include "df/unit_labor.h"

namespace df
{
    struct building;
    struct manager_order;
    struct report;
    struct unit;
}

using namespace DFHack;
using namespace df::enums;

extern std::vector<std::string> *plugin_globals;

class Population;
class Plan;
class Stocks;
class Camera;
class Embark;
class AI;

class Population
{
    AI *ai;
    std::set<int32_t> citizens;
    std::map<int32_t, int32_t> military;
    std::set<int32_t> workers;
    std::set<int32_t> idlers;
    std::map<df::unit_labor, int> labor_needmore;
    std::set<int32_t> medic;
    std::map<df::unit_labor, std::set<int32_t>> labor_worker;
    std::map<int32_t, std::set<df::unit_labor>> worker_labor;
    int32_t last_idle_year;
    union pet_flags
    {
        int32_t whole;
        struct
        {
            int milkable : 1;
            int shearable : 1;
            int hunts_vermin : 1;
            int grazer : 1;
        } bits;
    };
    std::map<int32_t, pet_flags> pets;
    int update_tick;

public:
    Population(color_ostream & out, AI *parent);
    ~Population();

    command_result status(color_ostream & out);
    command_result statechange(color_ostream & out, state_change_event event);
    command_result update(color_ostream & out);

    void assign_labor(color_ostream & out, df::unit *unit, df::unit_labor labor, std::string reason = "no reason given");
    void unassign_labor(color_ostream & out, df::unit *unit, df::unit_labor labor, std::string reason = "no reason given");

    bool has_military_duty(df::unit *unit);

private:
    void update_citizenlist(color_ostream & out);
    void update_nobles(color_ostream & out);
    void update_jobs(color_ostream & out);
    void update_military(color_ostream & out);
    void update_pets(color_ostream & out);
    void update_deads(color_ostream & out);
    void update_caged(color_ostream & out);
    void autolabors_workers(color_ostream & out);
    void autolabors_jobs(color_ostream & out);
    void autolabors_labors(color_ostream & out);
    void autolabors_commit(color_ostream & out);

    void set_up_trading(bool should_be_trading);
};

class Plan
{
    AI *ai;

public:
    Plan(color_ostream & out, AI *parent);
    ~Plan();

    enum class room_status
    {
        plan,
        dig,
        dug,
        finished,
    };
    enum class room_type
    {
        barracks,
        bedroom,
        cemetary,
        cistern_well,
        corridor,
        dining_hall,
        farm_plot,
        infirmary,
        noble_room,
        pasture,
        pit_cage,
        well,
        workshop,
    };
    enum class workshop_type
    {
        Carpenters,
        Fishery,
        Masons,
        TradeDepot,
    };
    enum class furniture_type
    {
        archery_target,
        armor_stand,
        bed,
        cabinet,
        chest,
        weapon_rack,
    };
    struct furniture
    {
        furniture_type type;
        df::coord2d pos;
        int32_t building_id;
        std::set<int32_t> users;
        bool ignore;
    };
    struct room
    {
        room_type type;
        room_status status;
        df::coord pos0, pos1;
        int32_t building_id;
        std::set<int32_t> users;
        std::vector<furniture> layout;
        union
        {
            struct
            {
                int32_t squad_id;
            } barracks;
            struct
            {
                workshop_type type;
            } workshop;
            struct
            {
                bool channeled;
            } cistern_well;
        } info;

        df::coord size() const
        {
            df::coord size = (pos1 - pos0);
            size.x++;
            size.y++;
            size.z++;
            return size;
        };
        df::coord pos() const
        {
            df::coord halfsize = size();
            halfsize.x /= 2;
            halfsize.y /= 2;
            halfsize.z /= 2;
            return pos0 + halfsize;
        };
    };
    enum class task_type
    {
        want_dig,
        dig_room,
        construct_workshop,
        construct_stockpile,
        construct_activity_zone,
        set_up_farm_plot,
        furnish,
        check_furnish,
        check_construct,
        dig_cistern,
        dig_garbage,
        check_idle,
        check_rooms,
        monitor_cistern,
    };
    struct task
    {
        task_type type;
        Plan::room *room;
        size_t index;

        explicit task(task_type type);
    };

    command_result status(color_ostream & out);
    command_result statechange(color_ostream & out, state_change_event event);
    command_result update(color_ostream & out);

    std::map<room_type, std::vector<room *>> rooms;
    template<class F>
    room *find_room(room_type type, F filter)
    {
        if (!rooms.count(type))
            return nullptr;
        for (room *r : rooms.at(type))
        {
            if (filter(r))
                return r;
        }
        return nullptr;
    }
    room *find_room(room_type type)
    {
        return find_room(type, [](room *r) -> bool { return true; });
    }

    std::list<task> tasks;
    std::list<task>::iterator task_cur;
    template<class F>
    bool have_task(task_type type, F filter)
    {
        for (const auto & t : tasks)
        {
            if (t.type == type && filter(t))
                return true;
        }
        return false;
    }
    bool have_task(task_type type)
    {
        return have_task(type, [](const task & t) -> bool { return true; });
    }

    std::list<room *> idleidle_todo;
    std::set<furniture_type> out_of_furniture;
    size_t dig_count;

    void attribute_noblerooms(color_ostream & out, std::set<int32_t> & ids);
    void idleidle(color_ostream & out);
    bool check_idle(color_ostream & out);
    bool check_rooms(color_ostream & out);
    bool monitor_cistern(color_ostream & out);
    bool is_digging();
    bool past_initial_phase();

    void new_citizen(color_ostream & out, int32_t id);
    void del_citizen(color_ostream & out, int32_t id);
    void new_soldier(color_ostream & out, int32_t id);
    void del_soldier(color_ostream & out, int32_t id);
    room *new_grazer(color_ostream & out, int32_t id);
    void del_grazer(color_ostream & out, int32_t id);

    void get_dining_room(color_ostream & out, int32_t id);
    void get_bedroom(color_ostream & out, int32_t id);
    void free_bedroom(color_ostream & out, int32_t id);
    void free_common_rooms(color_ostream & out, int32_t id);
    void free_common_rooms(color_ostream & out, int32_t id, room_type subtype);
    void assign_squad_to_barracks(color_ostream & out, df::building *bld, int32_t squad_id);

    bool is_dug(room *r, df::tiletype_shape_basic want = tiletype_shape_basic::None);

    bool want_dig(color_ostream & out, room *r);
    bool dig_room(color_ostream & out, room *r);
    bool try_construct_workshop(color_ostream & out, room *r);
    bool try_construct_stockpile(color_ostream & out, room *r);
    bool try_construct_activity_zone(color_ostream & out, room *r);
    bool try_furnish(color_ostream & out, room *r, furniture & f);
    bool try_end_furnish(color_ostream & out, room *r, furniture & f);
    bool try_end_construct(color_ostream & out, room *r);
    bool try_set_up_farm_plot(color_ostream & out, room *r);
    bool try_dig_cistern(color_ostream & out, room *r);
    bool try_dig_garbage(color_ostream & out, room *r);
    void construct_room(color_ostream & out, room *r);
    void furnish_room(color_ostream & out, room *r);
    void smooth_room(color_ostream & out, room *r);
};

class Stocks
{
    AI *ai;
    // plants that can be brewed in their basic form
    std::map<int32_t, int16_t> drink_plants;
    // plants that have a growth that can be brewed
    std::map<int32_t, int16_t> drink_fruits;
    // plants that can be made into thread
    std::map<int32_t, int16_t> thread_plants;
    // plants that can be milled
    std::map<int32_t, int16_t> mill_plants;
    // plants that can be processed into a bag
    std::map<int32_t, int16_t> bag_plants;
    // plants that can be milled that are able to dye cloth
    std::map<int32_t, int16_t> dye_plants;
    // plants that we can grow underground
    std::map<int32_t, int16_t> grow_plants;
    // creatures that have milk that can be turned into cheese
    std::map<int32_t, int16_t> milk_creatures;
    // inorganic materials that have a FIRED_MAT reaction product
    std::set<int32_t> clay_stones;

public:
    Stocks(color_ostream & out, AI *parent);
    ~Stocks();

    enum class good
    {
        anvil,
        armor_feet,
        armor_hands,
        armor_head,
        armor_legs,
        armor_shield,
        armor_torso,
        armorstand,
        ash,
        axe,
        backpack,
        bag,
        bag_plant,
        barrel,
        bed,
        bin,
        block,
        bone,
        bonebolts,
        bucket,
        cabinet,
        cage,
        chair,
        chest,
        clay,
        cloth,
        cloth_nodye,
        clothes_feet,
        clothes_hands,
        clothes_head,
        clothes_legs,
        clothes_torso,
        coal,
        coffin,
        coffin_bld,
        coffin_bld_pet,
        crossbow,
        crutch,
        door,
        drink,
        drink_fruit,
        drink_plant,
        dye,
        dye_plant,
        dye_seeds,
        flask,
        floodgate,
        food,
        food_ingredients,
        giant_corkscrew,
        gypsum,
        hive,
        honey,
        honeycomb,
        jug,
        leather,
        lye,
        mechanism,
        metal_ore,
        milk,
        mill_plant,
        minecart,
        nestbox,
        pick,
        pipe_section,
        plasterpowder,
        quern,
        quiver,
        raw_adamantine,
        raw_coke,
        raw_fish,
        rope,
        roughgem,
        shell,
        skull,
        slab,
        soap,
        splint,
        stepladder,
        stone,
        table,
        tallow,
        thread_plant,
        thread_seeds,
        traction_bench,
        weapon,
        weaponrack,
        wheelbarrow,
        wood,
        wool,
    };

    command_result status(color_ostream & out);
    command_result statechange(color_ostream & out, state_change_event event);
    command_result update(color_ostream & out);

    std::vector<df::manager_order *> find_manager_orders(df::job_type type);
    void add_manager_order(df::job_type type, int count);
    void queue_slab(int32_t histfig);
    bool need_more(good g);
    bool is_cutting_trees();

private:
    void update_kitchen(color_ostream & out);
    void update_plants(color_ostream & out);
};

class Camera
{
    AI *ai;
    int32_t following;
    int32_t following_prev[3];
    int following_index;
    int update_after_ticks;

public:
    Camera(color_ostream & out, AI *parent);
    ~Camera();

    command_result status(color_ostream & out);
    command_result statechange(color_ostream & out, state_change_event event);
    command_result update(color_ostream & out);

    void start_recording(color_ostream & out);
    bool followed_previously(int32_t id);
};

class Embark
{
    AI *ai;
    bool embarking;
    std::string world_name;
    std::time_t timeout;

public:
    Embark(color_ostream & out, AI *parent);
    ~Embark();

    command_result status(color_ostream & out);
    command_result statechange(color_ostream & out, state_change_event event);
    command_result update(color_ostream & out);
};

class AI
{
protected:
    std::mt19937 rng;
    Population pop;
    friend class Population;
    Plan plan;
    friend class Plan;
    Stocks stocks;
    friend class Stocks;
    Camera camera;
    friend class Camera;
    Embark embark;
    friend class Embark;
    std::time_t unpause_delay;

public:
    AI(color_ostream & out);
    ~AI();

    command_result status(color_ostream & out);
    command_result statechange(color_ostream & out, state_change_event event);
    command_result update(color_ostream & out);

    void debug(color_ostream & out, const std::string & str);
    void unpause(color_ostream & out);
    void check_unpause(color_ostream & out, state_change_event event);
    void handle_pause_event(color_ostream & out, std::vector<df::report *>::reverse_iterator ann, std::vector<df::report *>::reverse_iterator end);
    std::string describe_unit(df::unit *unit);
};

// vim: et:sw=4:ts=4
