#pragma once

#include "event_manager.h"
#include "room.h"

#include "df/biome_type.h"
#include "df/items_other_id.h"
#include "df/job_material_category.h"
#include "df/job_skill.h"
#include "df/job_type.h"
#include "df/material_flags.h"
#include "df/tool_uses.h"

namespace df
{
    struct itemdef_toolst;
    struct manager_order;
    struct manager_order_template;
}

class AI;
struct room;

#define STOCKS_ENUMS \
BEGIN_ENUM(stock, item) \
    ENUM_ITEM(ammo) \
    ENUM_ITEM(anvil) \
    ENUM_ITEM(armor_feet) \
    ENUM_ITEM(armor_hands) \
    ENUM_ITEM(armor_head) \
    ENUM_ITEM(armor_legs) \
    ENUM_ITEM(armor_shield) \
    ENUM_ITEM(armor_stand) \
    ENUM_ITEM(armor_torso) \
    ENUM_ITEM(ash) \
    ENUM_ITEM(axe) \
    ENUM_ITEM(backpack) \
    ENUM_ITEM(bag) \
    ENUM_ITEM(bag_plant) \
    ENUM_ITEM(barrel) \
    ENUM_ITEM(bed) \
    ENUM_ITEM(bin) \
    ENUM_ITEM(block) \
    ENUM_ITEM(bone) \
    ENUM_ITEM(book_binding) \
    ENUM_ITEM(bookcase) \
    ENUM_ITEM(bucket) \
    ENUM_ITEM(cabinet) \
    ENUM_ITEM(cage) \
    ENUM_ITEM(cage_metal) \
    ENUM_ITEM(chair) \
    ENUM_ITEM(chest) \
    ENUM_ITEM(clay) \
    ENUM_ITEM(cloth) \
    ENUM_ITEM(cloth_nodye) \
    ENUM_ITEM(clothes_feet) \
    ENUM_ITEM(clothes_hands) \
    ENUM_ITEM(clothes_head) \
    ENUM_ITEM(clothes_legs) \
    ENUM_ITEM(clothes_torso) \
    ENUM_ITEM(coal) \
    ENUM_ITEM(coffin) \
    ENUM_ITEM(crutch) \
    ENUM_ITEM(door) \
    ENUM_ITEM(drink) \
    ENUM_ITEM(drink_fruit) \
    ENUM_ITEM(drink_plant) \
    ENUM_ITEM(dye) \
    ENUM_ITEM(dye_plant) \
    ENUM_ITEM(dye_seeds) \
    ENUM_ITEM(flask) \
    ENUM_ITEM(floodgate) \
    ENUM_ITEM(food_ingredients) \
    ENUM_ITEM(goblet) \
    ENUM_ITEM(gypsum) \
    ENUM_ITEM(hatch_cover) \
    ENUM_ITEM(hive) \
    ENUM_ITEM(honey) \
    ENUM_ITEM(honeycomb) \
    ENUM_ITEM(jug) \
    ENUM_ITEM(leather) \
    ENUM_ITEM(lye) \
    ENUM_ITEM(meal) \
    ENUM_ITEM(mechanism) \
    ENUM_ITEM(metal_ore) \
    ENUM_ITEM(metal_strand) \
    ENUM_ITEM(milk) \
    ENUM_ITEM(mill_plant) \
    ENUM_ITEM(minecart) \
    ENUM_ITEM(nest_box) \
    ENUM_ITEM(paper) \
    ENUM_ITEM(pick) \
    ENUM_ITEM(pipe_section) \
    ENUM_ITEM(plaster_powder) \
    ENUM_ITEM(quern) \
    ENUM_ITEM(quire) \
    ENUM_ITEM(quiver) \
    ENUM_ITEM(raw_coke) \
    ENUM_ITEM(raw_fish) \
    ENUM_ITEM(rock_pot) \
    ENUM_ITEM(rope) \
    ENUM_ITEM(rough_gem) \
    ENUM_ITEM(screw) \
    ENUM_ITEM(shell) \
    ENUM_ITEM(skull) \
    ENUM_ITEM(slab) \
    ENUM_ITEM(slurry) \
    ENUM_ITEM(slurry_plant) \
    ENUM_ITEM(soap) \
    ENUM_ITEM(splint) \
    ENUM_ITEM(statue) \
    ENUM_ITEM(stepladder) \
    ENUM_ITEM(stone) \
    ENUM_ITEM(table) \
    ENUM_ITEM(tallow) \
    ENUM_ITEM(thread) \
    ENUM_ITEM(thread_plant) \
    ENUM_ITEM(thread_seeds) \
    ENUM_ITEM(toy) \
    ENUM_ITEM(traction_bench) \
    ENUM_ITEM(weapon_melee) \
    ENUM_ITEM(weapon_rack) \
    ENUM_ITEM(weapon_ranged) \
    ENUM_ITEM(weapon_training) \
    ENUM_ITEM(wheelbarrow) \
    ENUM_ITEM(wood) \
    ENUM_ITEM(wool) \
    ENUM_ITEM(written_on_quire) \
END_ENUM(stock, item)

#define BEGIN_ENUM BEGIN_DECLARE_ENUM
#define ENUM_ITEM DECLARE_ENUM_ITEM
#define END_ENUM END_DECLARE_ENUM
STOCKS_ENUMS
#undef BEGIN_ENUM
#undef ENUM_ITEM
#undef END_ENUM

const struct Watch
{
    std::map<stock_item::item, int32_t> Needed;
    std::map<stock_item::item, int32_t> NeededPerDwarf; // per 100 dwarves, actually
    std::map<stock_item::item, int32_t> WatchStock;
    std::set<stock_item::item> AlsoCount;

    Watch();
} Watch;

class Stocks
{
    AI *ai;
public:
    std::map<stock_item::item, int32_t> count_free;
    std::map<stock_item::item, int32_t> count_total;
    std::map<stock_item::item, std::map<int16_t, std::pair<int32_t, int32_t>>> count_subtype;
    std::map<int32_t, int32_t> ingots;
private:
    OnupdateCallback *onupdate_handle;
    std::vector<stock_item::item> updating;
    std::vector<stock_item::item> updating_count;
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
    bool updating_ingots;
    std::vector<room *> updating_farmplots;
    std::set<df::coord, std::function<bool(df::coord, df::coord)>> last_treelist;
    df::coord last_cutpos;
    int32_t last_warn_food_year;

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

public:
    std::map<df::material_flags, std::set<int32_t>> metal_pref;

private:
    std::vector<std::set<int32_t>> simple_metal_ores;

    std::set<std::tuple<farm_type::type, df::biome_type, int8_t>> complained_about_no_plants;

public:
    Stocks(AI *ai);
    ~Stocks();

    struct find_item_info;

    void reset();
    command_result startup(color_ostream & out);
    command_result onupdate_register(color_ostream & out);
    command_result onupdate_unregister(color_ostream & out);

    std::string status();
    void report(std::ostream & out, bool html);

    void update(color_ostream & out);
    void update_kitchen(color_ostream & out);
    void update_plants(color_ostream & out);
    void count_seeds(color_ostream & out);
    void count_plants(color_ostream & out);
    void update_corpses(color_ostream & out);
    void update_slabs(color_ostream & out);
    void update_ingots(color_ostream & out);

    int32_t num_needed(stock_item::item key);
    int16_t min_subtype_for_item(stock_item::item what);
    void act(color_ostream & out, stock_item::item key);
    void count_stocks(color_ostream & out, stock_item::item k);
    void count_stocks_subtype(color_ostream & out, stock_item::item k, find_item_info & helper);

    void queue_need(color_ostream & out, stock_item::item what, int32_t amount);
    void queue_need_weapon(color_ostream & out, stock_item::item stock_item, int32_t needed, df::job_skill skill = job_skill::NONE, bool training = false, bool ranged = false, bool digger = false);
    void queue_need_armor(color_ostream & out, stock_item::item what, df::items_other_id oidx);
    void queue_need_ammo(color_ostream & out);
    void queue_need_anvil(color_ostream & out);
    void queue_need_cage(color_ostream & out);
    void queue_need_forge(color_ostream & out, df::material_flags preference, int32_t bars_per_item, stock_item::item item, df::job_type job, std::function<bool(const std::map<int32_t, int32_t> & bars, int32_t & chosen_type)> decide, df::item_type item_type = item_type::NONE, int16_t item_subtype = -1, int32_t items_created_per_job = 1);
    void queue_need_clothes(color_ostream & out, df::items_other_id oidx);
    void queue_use(color_ostream & out, stock_item::item what, int32_t amount);
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

    int32_t count_manager_orders_matcat(const df::job_material_category & matcat, df::job_type order = job_type::NONE);
    int32_t count_manager_orders(color_ostream & out, const df::manager_order_template & tmpl);
    void add_manager_order(color_ostream & out, const df::manager_order_template & tmpl, int32_t amount = 1);

    struct find_item_info
    {
        find_item_info(df::items_other_id oidx) : find_item_info(oidx, [](df::item *) -> bool { return true; })
        {
        }
        find_item_info(df::items_other_id oidx, std::function<bool(df::item *)> pred) : find_item_info(oidx, pred, &default_count)
        {
        }
        find_item_info(df::items_other_id oidx, std::function<bool(df::item *)> pred, std::function<int32_t(int32_t &, df::item *)> count) : find_item_info(oidx, pred, count, [](df::item *i) -> bool { return is_item_free(i); })
        {
        }
        find_item_info(df::items_other_id oidx, std::function<bool(df::item *)> pred, std::function<int32_t(int32_t &, df::item *)> count, std::function<bool(df::item *)> free) : oidx(oidx), pred(pred), free(free), do_count(count), init_count(-1), subtypes(), count_min_subtype(false)
        {
        }
        find_item_info(df::items_other_id oidx, std::function<bool(df::item *)> pred, std::function<int32_t(int32_t &, df::item *)> count, std::function<bool(df::item *)> free, const std::set<int16_t> & subtypes, bool count_min_subtype = true) : oidx(oidx), pred(pred), free(free), do_count(count), init_count(-1), subtypes(subtypes), count_min_subtype(count_min_subtype)
        {
        }

        const df::items_other_id oidx;
        const std::function<bool(df::item *)> pred;
        int32_t count(df::item *i)
        {
            return do_count(init_count, i);
        }
        const std::function<bool(df::item *)> free;

        static int32_t default_count(int32_t &, df::item *);
    private:
        const std::function<int32_t(int32_t &, df::item *)> do_count;
        int32_t init_count;

    public:
        const std::set<int16_t> subtypes;
        const bool count_min_subtype;
    };
    df::item *find_free_item(stock_item::item k);
    find_item_info find_item_helper(stock_item::item k);
    find_item_info find_item_helper_weapon(df::job_skill skill = job_skill::NONE, bool training = false, bool ranged = false);
    find_item_info find_item_helper_digger(df::job_skill skill = job_skill::NONE, bool training = false);
    find_item_info find_item_helper_ammo(df::job_skill skill = job_skill::NONE, bool training = false);
    find_item_info find_item_helper_armor(df::items_other_id oidx);
    find_item_info find_item_helper_clothes(df::items_other_id oidx);
    find_item_info find_item_helper_tool(df::tool_uses use, std::function<bool(df::itemdef_toolst *)> pred = [](df::itemdef_toolst *) -> bool { return true; });

    void farmplot(color_ostream & out, room *r, bool initial = true);
    void queue_slab(color_ostream & out, int32_t histfig_id);

    bool need_more(stock_item::item type);

    bool willing_to_trade_item(color_ostream & out, df::item *item);
    bool want_trader_item(color_ostream & out, df::item *item);
    bool want_trader_item_more(df::item *a, df::item *b);
};
