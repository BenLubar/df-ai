#pragma once

#include "event_manager.h"
#include "room.h"

#include <map>
#include <set>
#include <tuple>

#include "df/items_other_id.h"
#include "df/job_material_category.h"
#include "df/job_skill.h"
#include "df/job_type.h"

namespace df
{
    struct manager_order;
    struct manager_order_template;
}

class AI;
struct room;

class Stocks
{
    AI *ai;
public:
    std::map<std::string, int32_t> count;
private:
    OnupdateCallback *onupdate_handle;
    std::vector<std::string> updating;
    std::vector<std::string> updating_count;
    size_t lastupdating;
    std::map<std::pair<uint8_t, int32_t>, size_t> farmplots;
    std::map<int32_t, size_t> seeds;
    std::map<int32_t, size_t> plants;
    int32_t last_unforbidall_year;
    int32_t last_managerstall;
    df::job_type last_managerorder;
    bool updating_seeds;
    bool updating_plants;
    bool updating_corpses;
    bool updating_slabs;
    std::vector<room *> updating_farmplots;
public:
    // depends on raws.itemdefs, wait until a world is loaded
    std::map<std::string, int16_t> manager_subtype;
private:
    std::set<df::coord, std::function<bool(df::coord, df::coord)>> last_treelist;
    df::coord last_cutpos;
    std::time_t last_warn_food;

    std::map<int32_t, int16_t> drink_plants;
    std::map<int32_t, int16_t> drink_fruits;
    std::map<int32_t, int16_t> thread_plants;
    std::map<int32_t, int16_t> mill_plants;
    std::map<int32_t, int16_t> bag_plants;
    std::map<int32_t, int16_t> dye_plants;
    std::map<int32_t, int16_t> slurry_plants;
    std::map<int32_t, int16_t> grow_plants;
    std::map<int32_t, int16_t> milk_creatures;
    std::set<int32_t> clay_stones;
    std::map<int32_t, std::string> raw_coke;
    std::map<std::string, int32_t> raw_coke_inv;

    std::vector<int32_t> metal_digger_pref;
    std::vector<int32_t> metal_weapon_pref;
    std::vector<int32_t> metal_armor_pref;
    std::vector<int32_t> metal_anvil_pref;

    std::vector<std::set<int32_t>> simple_metal_ores;

    std::set<std::tuple<farm_type::type, bool, int8_t>> complained_about_no_plants;

public:
    Stocks(AI *ai);
    ~Stocks();

    void reset();
    command_result startup(color_ostream & out);
    command_result onupdate_register(color_ostream & out);
    command_result onupdate_unregister(color_ostream & out);

    std::string status();
    std::string report();

    void update(color_ostream & out);
    void update_kitchen(color_ostream & out);
    void update_plants(color_ostream & out);
    void count_seeds(color_ostream & out);
    void count_plants(color_ostream & out);
    void update_corpses(color_ostream & out);
    void update_slabs(color_ostream & out);

    int32_t num_needed(const std::string & key);
    void act(color_ostream & out, std::string key);
    int32_t count_stocks(color_ostream & out, std::string k);
    int32_t count_stocks_weapon(color_ostream & out, df::job_skill skill = job_skill::NONE, bool training = false);
    int32_t count_stocks_armor(color_ostream & out, df::items_other_id oidx);
    int32_t count_stocks_clothes(color_ostream & out, df::items_other_id oidx);

    void queue_need(color_ostream & out, std::string what, int32_t amount);
    void queue_need_weapon(color_ostream & out, int32_t needed, df::job_skill skill = job_skill::NONE, bool training = false);
    void queue_need_armor(color_ostream & out, df::items_other_id oidx);
    void queue_need_anvil(color_ostream & out);
    void queue_need_clothes(color_ostream & out, df::items_other_id oidx);
    void queue_need_coffin_bld(color_ostream & out, int32_t amount);
    void queue_use(color_ostream & out, std::string what, int32_t amount);
    void queue_use_gems(color_ostream & out, int32_t amount);
    void queue_use_metal_ore(color_ostream & out, int32_t amount);
    void queue_use_raw_coke(color_ostream & out, int32_t amount);

    std::set<df::coord, std::function<bool(df::coord, df::coord)>> tree_list();
    df::coord cuttrees(color_ostream & out, int32_t amount);

    static bool is_item_free(df::item *i, bool allow_nonempty = false);
    bool is_metal_ore(int32_t i);
    bool is_metal_ore(df::item *i);
    std::string is_raw_coke(int32_t i);
    std::string is_raw_coke(df::item *i);
    bool is_gypsum(int32_t i);
    bool is_gypsum(df::item *i);

    void update_simple_metal_ores(color_ostream & out);
    int32_t may_forge_bars(color_ostream & out, int32_t mat_index, int32_t div = 1);

    void init_manager_subtype();

    int32_t count_manager_orders_matcat(const df::job_material_category & matcat, df::job_type order = job_type::NONE);
    void legacy_add_manager_order(color_ostream & out, std::string order, int32_t amount = 1, int32_t maxmerge = 30);
    int32_t count_manager_orders(color_ostream & out, const df::manager_order_template & tmpl);
    void add_manager_order(color_ostream & out, const df::manager_order_template & tmpl, int32_t amount = 1);

    std::string furniture_order(std::string k);
    std::function<bool(df::item *)> furniture_find(std::string k);
    df::item *find_furniture_item(std::string itm);
    int32_t find_furniture_itemcount(std::string itm);

    void farmplot(color_ostream & out, room *r, bool initial = true);
    void queue_slab(color_ostream & out, int32_t histfig_id);

    bool need_more(std::string type);

    bool willing_to_trade_item(color_ostream & out, df::item *item);
    bool want_trader_item(color_ostream & out, df::item *item);
    bool want_trader_item_more(df::item *a, df::item *b);
};
