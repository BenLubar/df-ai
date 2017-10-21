#pragma once

#include "event_manager.h"
#include "room.h"

#include "df/biome_type.h"
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

namespace stock_item
{
    enum item
    {
        anvil,
        armor_feet,
        armor_hands,
        armor_head,
        armor_legs,
        armor_shield,
        armor_stand,
        armor_torso,
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
        bone_bolts,
        book_binding,
        bookcase,
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
        dead_dwarf,
        door,
        drink,
        drink_fruit,
        drink_plant,
        dye,
        dye_plant,
        dye_seeds,
        flask,
        floodgate,
        food_ingredients,
        giant_corkscrew,
        goblet,
        gypsum,
        hive,
        honey,
        honeycomb,
        jug,
        leather,
        lye,
        meal,
        mechanism,
        metal_ore,
        milk,
        mill_plant,
        minecart,
        nest_box,
        paper,
        pick,
        pipe_section,
        plaster_powder,
        quern,
        quire,
        quiver,
        raw_adamantine,
        raw_coke,
        raw_fish,
        rock_pot,
        rope,
        rough_gem,
        shell,
        skull,
        slab,
        slurry,
        slurry_plant,
        soap,
        splint,
        stepladder,
        stone,
        table,
        tallow,
        thread,
        thread_plant,
        thread_seeds,
        toy,
        traction_bench,
        training_weapon,
        weapon,
        weapon_rack,
        wheelbarrow,
        wood,
        wool,
        written_on_quire,

        _stock_item_count
    };
}

std::ostream & operator <<(std::ostream & stream, stock_item::item item);
namespace DFHack
{
    template<> inline bool find_enum_item<stock_item::item>(stock_item::item *var, const std::string & name) { return df_ai_find_enum_item(var, name, stock_item::_stock_item_count); }
}

class Stocks
{
    AI *ai;
public:
    std::map<stock_item::item, int32_t> count;
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
    std::vector<room *> updating_farmplots;
public:
    // depends on raws.itemdefs, wait until a world is loaded
    std::map<std::string, int16_t> manager_subtype;
private:
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

    std::vector<int32_t> metal_digger_pref;
    std::vector<int32_t> metal_weapon_pref;
    std::vector<int32_t> metal_armor_pref;
    std::vector<int32_t> metal_anvil_pref;

    std::vector<std::set<int32_t>> simple_metal_ores;

    std::set<std::tuple<farm_type::type, df::biome_type, int8_t>> complained_about_no_plants;

public:
    Stocks(AI *ai);
    ~Stocks();

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

    int32_t num_needed(stock_item::item key);
    void act(color_ostream & out, stock_item::item key);
    int32_t count_stocks(color_ostream & out, stock_item::item k);
    int32_t count_stocks_weapon(color_ostream & out, df::job_skill skill = job_skill::NONE, bool training = false);
    int32_t count_stocks_armor(color_ostream & out, df::items_other_id oidx);
    int32_t count_stocks_clothes(color_ostream & out, df::items_other_id oidx);

    void queue_need(color_ostream & out, stock_item::item what, int32_t amount);
    void queue_need_weapon(color_ostream & out, int32_t needed, df::job_skill skill = job_skill::NONE, bool training = false);
    void queue_need_armor(color_ostream & out, df::items_other_id oidx);
    void queue_need_anvil(color_ostream & out);
    void queue_need_clothes(color_ostream & out, df::items_other_id oidx);
    void queue_need_coffin_bld(color_ostream & out, int32_t amount);
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

    void init_manager_subtype();

    int32_t count_manager_orders_matcat(const df::job_material_category & matcat, df::job_type order = job_type::NONE);
    void legacy_add_manager_order(color_ostream & out, std::string order, int32_t amount = 1, int32_t maxmerge = 30);
    int32_t count_manager_orders(color_ostream & out, const df::manager_order_template & tmpl);
    void add_manager_order(color_ostream & out, const df::manager_order_template & tmpl, int32_t amount = 1);

    std::string furniture_order(stock_item::item k);
    std::function<bool(df::item *)> furniture_find(stock_item::item k);
    df::item *find_furniture_item(stock_item::item itm);
    int32_t find_furniture_itemcount(stock_item::item itm);

    void farmplot(color_ostream & out, room *r, bool initial = true);
    void queue_slab(color_ostream & out, int32_t histfig_id);

    bool need_more(stock_item::item type);

    bool willing_to_trade_item(color_ostream & out, df::item *item);
    bool want_trader_item(color_ostream & out, df::item *item);
    bool want_trader_item_more(df::item *a, df::item *b);
};
