#pragma once

#include "Core.h"
#include <Console.h>
#include <Export.h>
#include <PluginManager.h>

#include "DataDefs.h"

#include <random>
#include <ctime>

#include "df/job_type.h"
#include "df/unit_labor.h"

namespace df
{
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

    enum room_status
    {
        plan,
        finished,
    };
    enum room_type
    {
        infirmary,
        barracks,
        pitcage,
        workshop,
        cistern_well,
    };
    enum workshop_type
    {
        TradeDepot,
        Carpenters,
        Masons,
        Fishery,
    };
    struct room
    {
        room_type type;
        room_status status;
        df::coord pos;
        int32_t building_id;
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
    };

    command_result status(color_ostream & out);
    command_result statechange(color_ostream & out, state_change_event event);
    command_result update(color_ostream & out);

    template<class F>
    room *find_room(room_type type, F filter)
    {
        // TODO
        return nullptr;
    }
    room *find_room(room_type type)
    {
        return find_room(type, [](room *r) -> bool { return true; });
    }

    void attribute_noblerooms(color_ostream & out, std::set<int32_t> & ids);
    void idleidle(color_ostream & out);
    bool is_digging();
    bool past_initial_phase();

    void new_citizen(color_ostream & out, int32_t id);
    void del_citizen(color_ostream & out, int32_t id);
    void new_soldier(color_ostream & out, int32_t id);
    void del_soldier(color_ostream & out, int32_t id);
    room *new_grazer(color_ostream & out, int32_t id);
    void del_grazer(color_ostream & out, int32_t id);
};

class Stocks
{
    AI *ai;

public:
    Stocks(color_ostream & out, AI *parent);
    ~Stocks();

    enum good
    {
        food,
        drink,
        cloth,
    };

    command_result status(color_ostream & out);
    command_result statechange(color_ostream & out, state_change_event event);
    command_result update(color_ostream & out);

    std::vector<df::manager_order *> find_manager_orders(df::job_type type);
    void add_manager_order(df::job_type type, int count);
    void queue_slab(int32_t histfig);
    bool need_more(good g);
    bool is_cutting_trees();
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
