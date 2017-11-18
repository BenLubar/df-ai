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

    AI *ai;
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
    std::vector<size_t> trade_want_items;
    std::vector<size_t>::iterator trade_want_items_it;
    std::string trade_want_qty;
    int32_t trade_step;
    int32_t trade_offer_value, trade_request_value, trade_max_offer_value;
    int32_t trade_broker_item;
    std::string trade_broker_qty;
    int32_t trade_ten_percent;
    int32_t trade_remove_item;
    std::string trade_remove_qty;

public:
    Population(AI *ai);
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

    bool military_random_squad_attack_unit(color_ostream & out, df::unit *u);
    bool military_all_squads_attack_unit(color_ostream & out, df::unit *u);
    bool military_squad_attack_unit(color_ostream & out, df::squad *squad, df::unit *u);
    bool military_cancel_attack_order(color_ostream & out, df::unit *u);

    df::unit *military_find_new_soldier(color_ostream & out, const std::vector<df::unit *> & unitlist);
    int32_t military_find_free_squad();

    bool set_up_trading(color_ostream & out, bool should_be_trading, bool allow_any_dwarf = false);
    bool perform_trade(color_ostream & out);
    bool perform_trade(color_ostream & out, df::viewscreen_tradegoodsst *trade);
    bool perform_trade_step(color_ostream & out);

    bool unit_hasmilitaryduty(df::unit *u);
    int32_t unit_totalxp(df::unit *u);

    void update_nobles(color_ostream & out);
    void check_noble_appartments(color_ostream & out);

    df::entity_position_assignment *assign_new_noble(color_ostream & out, std::function<bool(df::entity_position *)> filter, df::unit *unit, const std::string & description, int32_t squad_id = -1);
    df::entity_position_assignment *assign_new_noble(color_ostream & out, df::entity_position_responsibility responsibility, df::unit *unit, int32_t squad_id = -1)
    {
        return assign_new_noble(out, [responsibility](df::entity_position *pos) -> bool
        {
            return pos->responsibilities[responsibility];
        }, unit, "position with responsibility " + enum_item_key(responsibility), squad_id);
    }

    void update_pets(color_ostream & out);

    void assign_unit_to_zone(df::unit *u, df::building_civzonest *bld);

    std::string status();
    void report(std::ostream & out, bool html);
};

// vim: et:sw=4:ts=4
