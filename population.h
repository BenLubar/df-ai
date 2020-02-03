#pragma once

#include "event_manager.h"

#include "df/entity_position.h"
#include "df/entity_position_responsibility.h"
#include "df/job_type.h"
#include "df/occupation_type.h"
#include "df/unit_labor.h"

namespace df
{
    struct abstract_building;
    struct building;
    struct building_civzonest;
    struct entity_position_assignment;
    struct squad;
    struct unit;
    struct viewscreen_tradegoodsst;
}

class AI;

class Population
{
    union pet_flags
    {
        uint32_t whole;
        struct
        {
            uint32_t milkable : 1;
            uint32_t shearable : 1;
            uint32_t hunts_vermin : 1;
            uint32_t grazer : 1;
        } bits;
    };

    AI & ai;
public:
    std::set<int32_t> citizen;
    std::map<int32_t, int32_t> military;
    std::map<int32_t, pet_flags> pet;
    std::set<int32_t> pet_check;
    std::set<int32_t> visitor;
    std::set<int32_t> resident;
private:
    size_t update_counter;
    OnupdateCallback *onupdate_handle;
    size_t seen_death;
    OnupdateCallback *deathwatch_handle;
    std::set<int32_t> medic;
    std::vector<int32_t> workers;
    std::set<df::job_type> seen_badwork;
    int32_t last_checked_crime_year, last_checked_crime_tick;
    bool did_trade;
    int32_t trade_start_x, trade_start_y, trade_start_z;

    struct squad_order_change
    {
        enum order_type
        {
            kill
        };

        order_type type;
        int32_t squad_id;
        int32_t unit_id;
        bool remove;
        std::string reason;
    };
    std::list<squad_order_change> squad_order_changes;
    friend class MilitarySquadAttackExclusive;

public:
    Population(AI & ai);
    ~Population();

    command_result startup(color_ostream & out);
    command_result onupdate_register(color_ostream & out);
    command_result onupdate_unregister(color_ostream & out);

    void update(color_ostream & out);
    void deathwatch(color_ostream & out);

    void new_citizen(color_ostream & out, int32_t id);
    void del_citizen(color_ostream & out, int32_t id);

    void update_trading(color_ostream & out);
    void update_citizenlist(color_ostream & out);
    void update_jobs(color_ostream & out);
    void update_deads(color_ostream & out);
    void update_caged(color_ostream & out);
    void update_military(color_ostream & out);
    void update_crimes(color_ostream & out);
    void update_locations(color_ostream & out);

    void assign_occupation(color_ostream & out, df::building *bld, df::abstract_building *loc, df::occupation_type occ);

    bool military_random_squad_attack_unit(color_ostream & out, df::unit *u, const std::string & reason);
    bool military_all_squads_attack_unit(color_ostream & out, df::unit *u, const std::string & reason);
    bool military_squad_attack_unit(color_ostream & out, df::squad *squad, df::unit *u, const std::string & reason);
    bool military_cancel_attack_order(color_ostream & out, df::unit *u, const std::string & reason);
    bool military_cancel_attack_order(color_ostream & out, df::squad *squad, df::unit *u, const std::string & reason);

    bool set_up_trading(color_ostream & out, bool should_be_trading, bool allow_any_dwarf = false);
    bool perform_trade(color_ostream & out);
    friend class PerformTradeExclusive;

    bool unit_hasmilitaryduty(df::unit *u);
    static int32_t unit_totalxp(const df::unit *u);

    void update_nobles(color_ostream & out);
    void check_noble_apartments(color_ostream & out);

    void update_pets(color_ostream & out);

    void assign_unit_to_zone(df::unit *u, df::building_civzonest *bld);

    std::string status();
    void report(std::ostream & out, bool html);

    static int32_t days_since(int32_t year, int32_t tick);
};

// vim: et:sw=4:ts=4
