#pragma once

#include "ai.h"
#include "blueprint.h"
#include "exclusive_callback.h"

#include "modules/Screen.h"

class PlanSetup;

class viewscreen_ai_plan_setupst : public dfhack_viewscreen
{
    PlanSetup & setup;

public:
    viewscreen_ai_plan_setupst(PlanSetup & setup) : setup(setup) {}

    std::string getFocusString();

    void render();

    static virtual_identity _identity;
};

class PlanSetup : public ExclusiveCallback
{
    AI & ai;
    int32_t next_noblesuite;

    std::vector<room_base::furniture_t *> layout;
    std::vector<room_base::room_t *> rooms;
    std::vector<plan_priority_t> priorities;

    std::map<df::coord, std::pair<room_base::roomindex_t, std::map<std::string, variable_string::context_t>>> room_connect;
    std::map<df::coord, std::string> corridor;
    std::map<df::coord, std::string> interior;
    std::map<df::coord, std::string> no_room;
    std::map<df::coord, std::string> no_corridor;

public:
    std::vector<std::pair<std::string, bool>> log;

    PlanSetup(AI &);
    ~PlanSetup();
    void Run(color_ostream &);

private:
    void Log(const std::string &);
    void LogQuiet(const std::string &);

    bool build_from_blueprint(const blueprints_t & blueprints);
    void create_from_blueprint(room * & fort_entrance, std::vector<room *> & real_rooms_and_corridors, std::vector<plan_priority_t> & real_priorities) const;

    typedef void (PlanSetup::*find_fn)(std::vector<const room_blueprint *> &, const std::map<std::string, size_t> &, const std::map<std::string, std::map<std::string, size_t>> &, const blueprints_t &, const blueprint_plan_template &);
    typedef bool (PlanSetup::*try_add_fn)(const room_blueprint &, std::map<std::string, size_t> &, std::map<std::string, std::map<std::string, size_t>> &, const blueprint_plan_template &);

    bool add(const room_blueprint & rb, std::string & error, df::coord exit_location = df::coord());
    bool add(const room_blueprint & rb, room_base::roomindex_t parent, std::string & error, df::coord exit_location = df::coord());
    bool build(const blueprints_t & blueprints, const blueprint_plan_template & plan);
    void place_rooms(std::map<std::string, size_t> & counts, std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprints_t & blueprints, const blueprint_plan_template & plan, find_fn find, try_add_fn try_add);
    void clear();
    void find_available_blueprints(std::vector<const room_blueprint *> & available_blueprints, const std::map<std::string, size_t> & counts, const std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprints_t & blueprints, const blueprint_plan_template & plan, const std::set<std::string> & available_tags_base, const std::function<bool(const room_blueprint &)> & check);
    void find_available_blueprints_start(std::vector<const room_blueprint *> & available_blueprints, const std::map<std::string, size_t> & counts, const std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprints_t & blueprints, const blueprint_plan_template & plan);
    void find_available_blueprints_outdoor(std::vector<const room_blueprint *> & available_blueprints, const std::map<std::string, size_t> & counts, const std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprints_t & blueprints, const blueprint_plan_template & plan);
    void find_available_blueprints_connect(std::vector<const room_blueprint *> & available_blueprints, const std::map<std::string, size_t> & counts, const std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprints_t & blueprints, const blueprint_plan_template & plan);
    bool can_add_room(const room_blueprint & rb, df::coord pos);
    bool try_add_room_start(const room_blueprint & rb, std::map<std::string, size_t> & counts, std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprint_plan_template & plan);
    bool try_add_room_outdoor(const room_blueprint & rb, std::map<std::string, size_t> & counts, std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprint_plan_template & plan);
    bool try_add_room_outdoor_shared(const room_blueprint & rb, std::map<std::string, size_t> & counts, std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprint_plan_template & plan, int16_t x, int16_t y);
    bool try_add_room_connect(const room_blueprint & rb, std::map<std::string, size_t> & counts, std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprint_plan_template & plan);
    bool have_minimum_requirements(std::map<std::string, size_t> & counts, std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprint_plan_template & plan);
    void remove_unused_rooms();
    void handle_stairs_special();
};
