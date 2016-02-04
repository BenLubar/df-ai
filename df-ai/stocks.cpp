#include "ai.h"
#include "stocks.h"
#include "plan.h"
#include "population.h"
#include "cache_hash.h"

#include "modules/Buildings.h"
#include "modules/Maps.h"
#include "modules/Materials.h"
#include "modules/Units.h"

#include "df/building_coffinst.h"
#include "df/building_farmplotst.h"
#include "df/building_trapst.h"
#include "df/buildings_other_id.h"
#include "df/corpse_material_type.h"
#include "df/creature_raw.h"
#include "df/entity_raw.h"
#include "df/general_ref_building_holderst.h"
#include "df/general_ref_contained_in_itemst.h"
#include "df/general_ref_contains_itemst.h"
#include "df/general_ref_contains_unitst.h"
#include "df/general_ref_unit_holderst.h"
#include "df/historical_entity.h"
#include "df/inorganic_raw.h"
#include "df/item.h"
#include "df/item_ammost.h"
#include "df/item_animaltrapst.h"
#include "df/item_armorst.h"
#include "df/item_barrelst.h"
#include "df/item_barst.h"
#include "df/item_binst.h"
#include "df/item_boulderst.h"
#include "df/item_boxst.h"
#include "df/item_bucketst.h"
#include "df/item_cagest.h"
#include "df/item_clothst.h"
#include "df/item_corpsepiecest.h"
#include "df/item_flaskst.h"
#include "df/item_foodst.h"
#include "df/item_glovesst.h"
#include "df/item_helmst.h"
#include "df/item_pantsst.h"
#include "df/item_plant_growthst.h"
#include "df/item_plantst.h"
#include "df/item_seedsst.h"
#include "df/item_shieldst.h"
#include "df/item_shoesst.h"
#include "df/item_slabst.h"
#include "df/item_toolst.h"
#include "df/item_trapcompst.h"
#include "df/item_trappartsst.h"
#include "df/item_weaponst.h"
#include "df/itemdef_ammost.h"
#include "df/itemdef_armorst.h"
#include "df/itemdef_glovesst.h"
#include "df/itemdef_helmst.h"
#include "df/itemdef_pantsst.h"
#include "df/itemdef_shieldst.h"
#include "df/itemdef_shoesst.h"
#include "df/itemdef_toolst.h"
#include "df/itemdef_trapcompst.h"
#include "df/itemdef_weaponst.h"
#include "df/itemimprovement.h"
#include "df/manager_order.h"
#include "df/material.h"
#include "df/plant.h"
#include "df/plant_growth.h"
#include "df/plant_raw.h"
#include "df/reaction.h"
#include "df/reaction_product.h"
#include "df/reaction_product_itemst.h"
#include "df/reaction_reagent.h"
#include "df/reaction_reagent_itemst.h"
#include "df/strain_type.h"
#include "df/ui.h"
#include "df/unit.h"
#include "df/unit_inventory_item.h"
#include "df/vehicle.h"
#include "df/world.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

const static std::map<std::string, int32_t> Needed
{
    {"door", 4}, {"bed", 4}, {"bin", 4}, {"barrel", 4},
    {"cabinet", 4}, {"chest", 4}, {"mechanism", 4},
    {"bag", 3}, {"table", 3}, {"chair", 3}, {"cage", 3},
    {"coffin", 2}, {"coffin_bld", 3}, {"coffin_bld_pet", 1},
    {"food", 20}, {"drink", 20}, {"goblet", 10}, {"wood", 16}, {"bucket", 2},
    {"thread_seeds", 10}, {"dye_seeds", 10}, {"dye", 10},
    {"weapon", 2}, {"armor_torso", 2}, {"clothes_torso", 2}, {"block", 6},
    {"quiver", 2}, {"flask", 2}, {"backpack", 2}, {"wheelbarrow", 1},
    {"splint", 1}, {"crutch", 1}, {"rope", 1}, {"weaponrack", 1},
    {"armorstand", 1}, {"floodgate", 1}, {"traction_bench", 1},
    {"soap", 1}, {"lye", 1}, {"ash", 1}, {"plasterpowder", 1},
    {"coal", 3}, {"raw_coke", 1}, {"gypsum", 1}, {"slab", 1},
    {"giant_corkscrew", 1}, {"pipe_section", 1}, {"anvil", 1},
    {"quern", 3}, {"minecart", 1}, {"nestbox", 1}, {"hive", 1},
    {"jug", 1}, {"stepladder", 2}, {"pick", 2}, {"axe", 2},
    {"armor_head", 2}, {"clothes_head", 2}, {"armor_legs", 2},
    {"clothes_legs", 2}, {"armor_hands", 2}, {"clothes_hands", 2},
    {"armor_feet", 2}, {"clothes_feet", 2}, {"armor_shield", 2},
};

const static std::map<std::string, int32_t> NeededPerDwarf
{
    // per 100 dwarves, actually
    {"food", 100}, {"drink", 200}, {"slab", 10}, {"soap", 20}, {"weapon", 5},
    {"cloth", 20}, {"clothes_torso", 20}, {"clothes_legs", 20},
    {"clothes_feet", 20}, {"clothes_hands", 20}, {"clothes_head", 20},
    {"armor_shield", 3}, {"armor_torso", 3}, {"armor_legs", 3},
    {"armor_feet", 3}, {"armor_hands", 3}, {"armor_head", 3},
};

const static std::map<std::string, int32_t> WatchStock
{
    {"roughgem", 6}, {"thread_plant", 10}, {"cloth_nodye", 10},
    {"mill_plant", 4}, {"bag_plant", 4}, {"milk", 1},
    {"metal_ore", 6}, {"raw_coke", 2}, {"raw_adamantine", 2},
    {"skull", 2}, {"bone", 8}, {"food_ingredients", 2},
    {"drink_plant", 5}, {"drink_fruit", 5}, {"honey", 1},
    {"honeycomb", 1}, {"wool", 1}, {"tallow", 1}, {"shell", 1},
    {"raw_fish", 1}, {"clay", 1},
};

const static std::set<std::string> AlsoCount
{
    "dye_plant", "cloth", "leather", "crossbow", "bonebolts", "stone",
    "dead_dwarf",
};

const static std::map<std::string, df::job_type> ManagerRealOrder
{
    {"BrewDrinkPlant", job_type::CustomReaction},
    {"BrewDrinkFruit", job_type::CustomReaction},
    {"BrewMead", job_type::CustomReaction},
    {"ProcessPlantsBag", job_type::CustomReaction},
    {"MakeSoap", job_type::CustomReaction},
    {"MakePlasterPowder", job_type::CustomReaction},
    {"PressHoneycomb", job_type::CustomReaction},
    {"MakeBag", job_type::ConstructChest},
    {"MakeRope", job_type::MakeChain},
    {"MakeWoodenWheelbarrow", job_type::MakeTool},
    {"MakeWoodenMinecart", job_type::MakeTool},
    {"MakeRockNestbox", job_type::MakeTool},
    {"MakeRockHive", job_type::MakeTool},
    {"MakeRockJug", job_type::MakeTool},
    {"MakeBoneBolt", job_type::MakeAmmo},
    {"MakeBoneCrossbow", job_type::MakeWeapon},
    {"MakeTrainingAxe", job_type::MakeWeapon},
    {"MakeTrainingShortSword", job_type::MakeWeapon},
    {"MakeTrainingSpear", job_type::MakeWeapon},
    {"MakeGiantCorkscrew", job_type::MakeTrapComponent},
    {"ConstructWoodenBlocks", job_type::ConstructBlocks},
    {"MakeWoodenStepladder", job_type::MakeTool},
    {"DecorateWithShell", job_type::DecorateWith},
    {"MakeClayStatue", job_type::CustomReaction},
};

const static std::map<std::string, std::string> ManagerMatCategory
{
    {"MakeRope", "cloth"}, {"MakeBag", "cloth"},
    {"ConstructBed", "wood"}, {"MakeBarrel", "wood"}, {"MakeBucket", "wood"}, {"ConstructBin", "wood"},
    {"MakeWoodenWheelbarrow", "wood"}, {"MakeWoodenMinecart", "wood"}, {"MakeTrainingAxe", "wood"},
    {"MakeTrainingShortSword", "wood"}, {"MakeTrainingSpear", "wood"},
    {"ConstructCrutch", "wood"}, {"ConstructSplint", "wood"}, {"MakeCage", "wood"},
    {"MakeGiantCorkscrew", "wood"}, {"MakePipeSection", "wood"}, {"ConstructWoodenBlocks", "wood"},
    {"MakeBoneBolt", "bone"}, {"MakeBoneCrossbow", "bone"},
    {"MakeQuiver", "leather"}, {"MakeFlask", "leather"}, {"MakeBackpack", "leather"},
    {"MakeWoodenStepladder", "wood"}, {"DecorateWithShell", "shell"},
};

// no MatCategory => mat_type = 0 (ie generic rock), unless specified here
const static std::map<std::string, int32_t> ManagerType
{
    {"ProcessPlants", -1}, {"ProcessPlantsBag", -1}, {"MillPlants", -1}, {"BrewDrinkPlant", -1},
    {"ConstructTractionBench", -1}, {"MakeSoap", -1}, {"MakeLye", -1}, {"MakeAsh", -1},
    {"MakeTotem", -1}, {"MakeCharcoal", -1}, {"MakePlasterPowder", -1}, {"PrepareMeal", 4},
    {"DyeCloth", -1}, {"MilkCreature", -1}, {"PressHoneycomb", -1}, {"BrewDrinkFruit", -1},
    {"BrewMead", -1}, {"MakeCheese", -1}, {"PrepareRawFish", -1}, {"MakeClayStatue", -1},
};

const static std::map<std::string, std::string> ManagerCustom
{
    {"ProcessPlantsBag", "PROCESS_PLANT_TO_BAG"},
    {"BrewDrinkPlant", "BREW_DRINK_FROM_PLANT"},
    {"BrewDrinkFruit", "BREW_DRINK_FROM_PLANT_GROWTH"},
    {"BrewMead", "MAKE_MEAD"},
    {"MakeSoap", "MAKE_SOAP_FROM_TALLOW"},
    {"MakePlasterPowder", "MAKE_PLASTER_POWDER"},
    {"PressHoneycomb", "PRESS_HONEYCOMB"},
    {"MakeClayStatue", "MAKE_CLAY_STATUE"},
};

Stocks::Stocks(AI *ai) :
    ai(ai),
    count(),
    onupdate_handle(nullptr),
    updating(),
    updating_count(),
    lastupdating(0),
    farmplots(),
    seeds(),
    plants(),
    last_unforbidall_year(*cur_year),
    last_managerstall(*cur_year_tick / 28 / 1200),
    last_managerorder(df::job_type(-1)),
    updating_seeds(false),
    updating_plants(false),
    updating_corpses(false),
    updating_farmplots(),
    manager_subtype(),
    last_treelist([ai](df::coord a, df::coord b) -> bool
            {
                df::coord fe = ai->plan->fort_entrance->pos();
                int16_t ascore = (a.x - fe.x) * (a.x - fe.x) + (a.y - fe.y) * (a.y - fe.y) + (a.z - fe.z) * (a.z - fe.z) * 16;
                int16_t bscore = (b.x - fe.x) * (b.x - fe.x) + (b.y - fe.y) * (b.y - fe.y) + (b.z - fe.z) * (b.z - fe.z) * 16;
                if (ascore < bscore)
                    return true;
                if (ascore > bscore)
                    return false;
                return a < b;
            }),
    last_cutpos(),
    last_warn_food(std::time(nullptr) - 610),
    drink_plants(),
    drink_fruits(),
    thread_plants(),
    mill_plants(),
    bag_plants(),
    dye_plants(),
    grow_plants(),
    milk_creatures(),
    clay_stones(),
    raw_coke(),
    raw_coke_inv(),
    metal_digger_pref(),
    metal_weapon_pref(),
    metal_armor_pref(),
    metal_anvil_pref(),
    complained_about_no_plants()
{
    last_cutpos.clear();
    events.onstatechange_register([this](color_ostream & out, state_change_event st)
            {
                if (st == SC_WORLD_LOADED)
                    init_manager_subtype();
            });
    if (!world->raws.itemdefs.ammo.empty())
        init_manager_subtype();
}

Stocks::~Stocks()
{
}

void Stocks::reset()
{
    updating.clear();
    lastupdating = 0;
    count.clear();
    farmplots.clear();
    seeds.clear();
    plants.clear();
}

command_result Stocks::startup(color_ostream & out)
{
    update_kitchen(out);
    update_plants(out);
    return CR_OK;
}

command_result Stocks::onupdate_register(color_ostream & out)
{
    reset();
    onupdate_handle = events.onupdate_register("df-ai stocks", 4800, 30, [this](color_ostream & out) { update(out); });
    return CR_OK;
}

command_result Stocks::onupdate_unregister(color_ostream & out)
{
    events.onupdate_unregister(onupdate_handle);
    return CR_OK;
}

std::string Stocks::status()
{
    std::ostringstream s;

    bool first = true;
    s << "need: ";
    for (auto n : Needed)
    {
        if (first)
            first = false;
        else
            s << ", ";

        int32_t want = num_needed(n.first);
        int32_t have = count[n.first];
        s << n.first << ": " << have << "/" << want;
        if (have < want)
            s << "!!!";
    }

    first = true;
    s << "\nwatch: ";
    for (auto w : WatchStock)
    {
        if (first)
            first = false;
        else
            s << ", ";

        int32_t want = w.second;
        int32_t have = count[w.first];
        s << w.first << ": " << have << "/" << want;
        if (have > want)
            s << "!!!";
    }

    first = true;
    s << "\nextra: ";
    for (auto c : AlsoCount)
    {
        if (first)
            first = false;
        else
            s << ", ";

        int32_t have = count[c];
        s << c << ": " << have;
    }

    return s.str();
}

void Stocks::update(color_ostream & out)
{
    if (!updating.empty() && lastupdating != updating.size() + updating_count.size())
    {
        // avoid stall if cb_bg crashed and was unregistered
        ai->debug(out, "not updating stocks");
        lastupdating = updating.size() + updating_count.size();
        return;
    }

    if (last_unforbidall_year != *cur_year)
    {
        last_unforbidall_year = *cur_year;
        for (df::item *i : world->items.all)
        {
            i->flags.bits.forbid = 0;
        }
    }

    // trim stalled manager orders once per month
    if (last_managerstall != *cur_year_tick / 28 / 1200)
    {
        last_managerstall = *cur_year_tick / 28 / 1200;
        if (!world->manager_orders.empty())
        {
            auto m = world->manager_orders.front();
            if (m->is_validated)
            {
                if (m->job_type == last_managerorder)
                {
                    if (m->amount_left > 3)
                    {
                        m->amount_left -= 3;
                    }
                    else
                    {
                        world->manager_orders.erase(world->manager_orders.begin());
                        delete m;
                    }
                }
                else
                {
                    last_managerorder = m->job_type;
                }
            }
        }
    }

    updating.clear();
    for (auto n : Needed)
    {
        updating.push_back(n.first);
    }
    for (auto w : WatchStock)
    {
        updating.push_back(w.first);
    }
    updating_count = updating;
    updating_count.insert(updating_count.end(), AlsoCount.begin(), AlsoCount.end());
    updating_seeds = true;
    updating_plants = true;
    updating_corpses = true;
    updating_farmplots.clear();

    ai->plan->find_room("farmplot", [this](room *r) -> bool
            {
                if (r->dfbuilding())
                {
                    updating_farmplots.push_back(r);
                }
                return false; // search all farm plots
            });

    ai->debug(out, "updating stocks");

    // do stocks accounting 'in the background' (ie one bit at a time)
    events.onupdate_register_once("df-ai stocks bg", 8, [this](color_ostream & out) -> bool
            {
                if (updating_seeds)
                {
                    count_seeds(out);
                    return false;
                }
                if (updating_plants)
                {
                    count_plants(out);
                    return false;
                }
                if (updating_corpses)
                {
                    update_corpses(out);
                    return false;
                }
                if (!updating_count.empty())
                {
                    std::string key = updating_count.back();
                    updating_count.pop_back();
                    count[key] = count_stocks(out, key);
                    return false;
                }
                if (!updating.empty())
                {
                    std::string key = updating.back();
                    updating.pop_back();
                    act(out, key);
                    return false;
                }
                if (!updating_farmplots.empty())
                {
                    room *r = updating_farmplots.back();
                    updating_farmplots.pop_back();
                    farmplot(out, r, false);
                    return false;
                }
                // finished, dismiss callback
                return true;
            });
}

static bool has_reaction_product(df::material *m, const std::string & product)
{
    for (std::string *p : m->reaction_product.id)
    {
        if (*p == product)
        {
            return true;
        }
    }
    return false;
}

void Stocks::update_kitchen(color_ostream & out)
{
    Core::getInstance().runCommand(out, "ban-cooking booze honey tallow seeds");
}

void Stocks::update_plants(color_ostream & out)
{
    drink_plants.clear();
    drink_fruits.clear();
    thread_plants.clear();
    mill_plants.clear();
    bag_plants.clear();
    dye_plants.clear();
    grow_plants.clear();
    milk_creatures.clear();
    clay_stones.clear();
    for (size_t i = 0; i < world->raws.plants.all.size(); i++)
    {
        df::plant_raw *p = world->raws.plants.all[i];
        for (size_t j = 0; j < p->material.size(); j++)
        {
            df::material *m = p->material[j];
            if (has_reaction_product(m, "DRINK_MAT"))
            {
                if (m->flags.is_set(material_flags::STRUCTURAL_PLANT_MAT))
                {
                    drink_plants[i] = j + MaterialInfo::PLANT_BASE;
                }
                else
                {
                    drink_fruits[i] = j + MaterialInfo::PLANT_BASE;
                }
                break;
            }
        }
        assert(int32_t(i) == p->material_defs.idx_basic_mat);
        MaterialInfo basic(p->material_defs.type_basic_mat, p->material_defs.idx_basic_mat);
        if (p->flags.is_set(plant_raw_flags::THREAD))
        {
            thread_plants[i] = basic.type;
        }
        if (p->flags.is_set(plant_raw_flags::MILL))
        {
            mill_plants[i] = basic.type;
            assert(int32_t(i) == p->material_defs.idx_mill);
            MaterialInfo mill(p->material_defs.type_mill, p->material_defs.idx_mill);
            if (mill.material->flags.is_set(material_flags::IS_DYE))
            {
                dye_plants[i] = mill.type;
            }
        }
        if (has_reaction_product(basic.material, "BAG_ITEM"))
        {
            bag_plants[i] = basic.type;
        }
        if (p->flags.is_set(plant_raw_flags::SEED) && p->flags.is_set(plant_raw_flags::BIOME_SUBTERRANEAN_WATER))
        {
            grow_plants[i] = basic.type;
        }
    }
    for (size_t i = 0; i < world->raws.creatures.all.size(); i++)
    {
        df::creature_raw *c = world->raws.creatures.all[i];
        for (size_t j = 0; j < c->material.size(); j++)
        {
            df::material *m = c->material[j];
            if (has_reaction_product(m, "CHEESE_MAT"))
            {
                milk_creatures[i] = j + MaterialInfo::CREATURE_BASE;
                break;
            }
        }
    }
    for (size_t i = 0; i < world->raws.inorganics.size(); i++)
    {
        if (has_reaction_product(&world->raws.inorganics[i]->material, "FIRED_MAT"))
        {
            clay_stones.insert(i);
        }
    }
}

void Stocks::count_seeds(color_ostream & out)
{
    farmplots.clear();
    ai->plan->find_room("farmplot", [this](room *r) -> bool
            {
                df::building_farmplotst *bld = virtual_cast<df::building_farmplotst>(r->dfbuilding());
                if (!bld)
                    return false;

                for (uint8_t season = 0; season < 4; season++)
                {
                    farmplots[std::make_pair(season, bld->plant_id[season])]++;
                }

                return false; // search all farm plots
            });
    seeds.clear();
    for (df::item *i : world->items.other[items_other_id::SEEDS])
    {
        df::item_seedsst *s = virtual_cast<df::item_seedsst>(i);
        if (s && is_item_free(s))
        {
            seeds[s->mat_index] += s->stack_size;
        }
    }
    updating_seeds = false;
}

void Stocks::count_plants(color_ostream & out)
{
    plants.clear();
    for (df::item *i : world->items.other[items_other_id::PLANT])
    {
        df::item_plantst *p = virtual_cast<df::item_plantst>(i);
        assert(p);
        if (p && is_item_free(p))
        {
            plants[p->mat_index] += p->stack_size;
        }
    }
    for (df::item *i : world->items.other[items_other_id::PLANT_GROWTH])
    {
        df::item_plant_growthst *p = virtual_cast<df::item_plant_growthst>(i);
        assert(p);
        if (p && is_item_free(p))
        {
            plants[p->mat_index] += p->stack_size;
        }
    }
    updating_plants = false;
}

void Stocks::update_corpses(color_ostream & out)
{
    room *r = ai->plan->find_room("garbagedump");
    if (!r)
        return;
    df::coord t = r->min - df::coord(0, 0, 1);

    for (df::item *i : world->items.other[items_other_id::ANY_CORPSE])
    {
        int16_t race = -1;
        int16_t caste = -1;
        df::historical_figure *hf = nullptr;
        df::unit *u = nullptr;
        i->getCorpseInfo(&race, &caste, &hf, &u);
        if (is_item_free(i) &&
                i->flags.bits.on_ground && // ignore corpses that are not on the ground
                !i->flags.bits.in_inventory && // ignore corpses in inventories even if they're being hauled
                !i->flags.bits.in_building && // ignore corpses in buildings even if they're not construction materials
                i->pos != t &&
                !(Maps::getTileOccupancy(i->pos)->bits.building == tile_building_occ::Passable && virtual_cast<df::building_stockpilest>(Buildings::findAtTile(i->pos))))
        {
            if (!i->flags.bits.dump && u)
            {
                ai->debug(out, "stocks: dump corpse of " + AI::describe_unit(u));
            }
            // dump corpses that aren't in a stockpile, a grave, or the dump.
            i->flags.bits.dump = true;
        }
        else if (i->pos == t)
        {
            if (i->flags.bits.forbid && u)
            {
                ai->debug(out, "stocks: unforbid corpse of " + AI::describe_unit(u));
            }
            // unforbid corpses in the dump so dwarves get buried before the next year.
            i->flags.bits.forbid = false;
        }
    }
    updating_corpses = false;
}

int32_t Stocks::num_needed(const std::string & key)
{
    int32_t amount = Needed.at(key);
    if (NeededPerDwarf.count(key))
    {
        amount += ai->pop->citizen.size() * NeededPerDwarf.at(key) / 100;
    }
    if (key == "coffin" && count.count("dead_dwarf") && count.count("coffin_bld"))
    {
        amount = std::max(amount, count.at("dead_dwarf") - count.at("coffin_bld"));
    }
    else if (key == "coffin_bld" && count.count("dead_dwarf"))
    {
        amount = std::max(amount, count.at("dead_dwarf"));
    }
    return amount;
}

void Stocks::act(color_ostream & out, std::string key)
{
    if (Needed.count(key))
    {
        int32_t amount = num_needed(key);
        if (count.at(key) < amount)
        {
            queue_need(out, key, amount * 3 / 2 - count.at(key));
        }
    }

    if (WatchStock.count(key))
    {
        int32_t amount = WatchStock.at(key);
        if (count.at(key) > amount)
        {
            queue_use(out, key, count.at(key) - amount);
        }
    }
}

// count unused stocks of one type of item
int32_t Stocks::count_stocks(color_ostream & out, std::string k)
{
    int32_t n = 0;
    auto add = [this, &n](df::item *i)
    {
        if (is_item_free(i))
        {
            n += virtual_cast<df::item_actual>(i)->stack_size;
        }
    };
    auto add_all = [add](df::items_other_id id, std::function<bool(df::item *)> pred = [](df::item *) -> bool { return true; })
    {
        for (df::item *i : world->items.other[id])
        {
            if (pred(i))
            {
                add(i);
            }
        }
    };
    if (k == "bin")
    {
        add_all(items_other_id::BIN, [](df::item *i) -> bool
                {
                    return virtual_cast<df::item_binst>(i)->stockpile.id == -1;
                });
    }
    else if (k == "barrel")
    {
        add_all(items_other_id::BARREL, [](df::item *i) -> bool
                {
                    return virtual_cast<df::item_barrelst>(i)->stockpile.id == -1;
                });
    }
    else if (k == "bag")
    {
        add_all(items_other_id::BOX, [](df::item *i) -> bool
                {
                    MaterialInfo mat(i);
                    return mat.isAnyCloth() || mat.material->flags.is_set(material_flags::LEATHER);
                });
    }
    else if (k == "rope")
    {
        add_all(items_other_id::CHAIN, [](df::item *i) -> bool
                {
                    MaterialInfo mat(i);
                    return mat.isAnyCloth();
                });
    }
    else if (k == "bucket")
    {
        add_all(items_other_id::BUCKET);
    }
    else if (k == "food")
    {
        add_all(items_other_id::ANY_GOOD_FOOD, [](df::item *i) -> bool
                {
                    return virtual_cast<df::item_foodst>(i);
                });
    }
    else if (k == "food_ingredients")
    {
        std::set<std::tuple<df::item_type, int16_t, int16_t, int32_t>> forbidden;
        for (size_t i = 0; i < ui->kitchen.item_types.size(); i++)
        {
            if ((ui->kitchen.exc_types[i] & 1) == 1)
            {
                forbidden.insert(std::make_tuple(ui->kitchen.item_types[i], ui->kitchen.item_subtypes[i], ui->kitchen.mat_types[i], ui->kitchen.mat_indices[i]));
            }
        }

        add_all(items_other_id::ANY_COOKABLE, [forbidden](df::item *i) -> bool
                {
                    if (virtual_cast<df::item_flaskst>(i))
                        return false;
                    if (virtual_cast<df::item_cagest>(i))
                        return false;
                    if (virtual_cast<df::item_barrelst>(i))
                        return false;
                    if (virtual_cast<df::item_bucketst>(i))
                        return false;
                    if (virtual_cast<df::item_animaltrapst>(i))
                        return false;
                    if (virtual_cast<df::item_boxst>(i))
                        return false;
                    if (virtual_cast<df::item_toolst>(i))
                        return false;
                    return !forbidden.count(std::make_tuple(i->getType(), i->getSubtype(), i->getMaterial(), i->getMaterialIndex()));
                });
    }
    else if (k == "drink")
    {
        add_all(items_other_id::DRINK);
    }
    else if (k == "goblet")
    {
        add_all(items_other_id::GOBLET);
    }
    else if (k == "soap" || k == "coal" || k == "ash")
    {
        std::string mat_id = k == "soap" ? "SOAP" : k == "coal" ? "COAL" : "ASH";
        add_all(items_other_id::BAR, [mat_id](df::item *i) -> bool
                {
                    MaterialInfo mat(i);
                    return mat.material && mat.material->id == mat_id;
                });
    }
    else if (k == "wood")
    {
        add_all(items_other_id::WOOD);
    }
    else if (k == "roughgem")
    {
        add_all(items_other_id::ROUGH, [](df::item *i) -> bool
                {
                    return i->getMaterial() == 0;
                });
    }
    else if (k == "metal_ore")
    {
        add_all(items_other_id::BOULDER, [this](df::item *i) -> bool
                {
                    return is_metal_ore(i);
                });
    }
    else if (k == "raw_coke")
    {
        add_all(items_other_id::BOULDER, [this](df::item *i) -> bool
                {
                    return !is_raw_coke(i).empty();
                });
    }
    else if (k == "gypsum")
    {
        add_all(items_other_id::BOULDER, [this](df::item *i) -> bool
                {
                    return is_gypsum(i);
                });
    }
    else if (k == "raw_adamantine")
    {
        MaterialInfo candy;
        if (candy.findInorganic("RAW_ADAMANTINE"))
            add_all(items_other_id::BOULDER, [candy](df::item *i) -> bool
                    {
                        return i->getMaterialIndex() == candy.index;
                    });
    }
    else if (k == "stone")
    {
        add_all(items_other_id::BOULDER, [](df::item *i) -> bool
                {
                    return !ui->economic_stone[i->getMaterialIndex()];
                });
    }
    else if (k == "raw_fish")
    {
        add_all(items_other_id::FISH_RAW);
    }
    else if (k == "splint")
    {
        add_all(items_other_id::SPLINT);
    }
    else if (k == "crutch")
    {
        add_all(items_other_id::CRUTCH);
    }
    else if (k == "crossbow")
    {
        if (manager_subtype.count("MakeBoneCrossbow"))
            add_all(items_other_id::WEAPON, [this](df::item *i) -> bool
                    {
                        return virtual_cast<df::item_weaponst>(i)->subtype->subtype == manager_subtype.at("MakeBoneCrossbow");
                    });
    }
    else if (k == "clay")
    {
        add_all(items_other_id::BOULDER, [this](df::item *i) -> bool
                {
                    return clay_stones.count(i->getMaterialIndex());
                });
    }
    else if (k == "drink_plant" || k == "thread_plant" || k == "mill_plant" || k == "bag_plant")
    {
        std::map<int32_t, int16_t> & plant = k == "drink_plant" ? drink_plants : k == "thread_plant" ? thread_plants : k == "mill_plant" ? mill_plants : bag_plants;
        add_all(items_other_id::PLANT, [plant](df::item *i) -> bool
                {
                    return plant.count(i->getMaterialIndex()) && plant.at(i->getMaterialIndex()) == i->getMaterial();
                });
    }
    else if (k == "drink_fruit")
    {
        add_all(items_other_id::PLANT_GROWTH, [this](df::item *i) -> bool
                {
                    return drink_fruits.count(i->getMaterialIndex()) && drink_fruits.at(i->getMaterialIndex()) == i->getMaterial();
                });
    }
    else if (k == "honey")
    {
        MaterialInfo honey;
        if (honey.findCreature("HONEY_BEE", "HONEY"))
            add_all(items_other_id::LIQUID_MISC, [honey](df::item *i) -> bool
                    {
                        return i->getMaterialIndex() == honey.index && i->getMaterial() == honey.type;
                    });
    }
    else if (k == "milk")
    {
        add_all(items_other_id::LIQUID_MISC, [this](df::item *i) -> bool
                {
                    return milk_creatures.count(i->getMaterialIndex()) && milk_creatures.at(i->getMaterialIndex()) == i->getMaterial();
                });
    }
    else if (k == "dye_plant")
    {
        add_all(items_other_id::PLANT, [this](df::item *i) -> bool
                {
                    return mill_plants.count(i->getMaterialIndex()) && mill_plants.at(i->getMaterialIndex()) == i->getMaterial() && dye_plants.count(i->getMaterialIndex());
                });
    }
    else if (k == "thread_seeds" || k == "dye_seeds")
    {
        std::map<int32_t, int16_t> & plant = k == "thread_seeds" ? thread_plants : dye_plants;
        add_all(items_other_id::SEEDS, [this, plant](df::item *i) -> bool
                {
                    return plant.count(i->getMaterialIndex()) && grow_plants.count(i->getMaterialIndex());
                });
    }
    else if (k == "dye")
    {
        add_all(items_other_id::POWDER_MISC, [this](df::item *i) -> bool
                {
                    return dye_plants.count(i->getMaterialIndex()) && dye_plants.at(i->getMaterialIndex()) == i->getMaterial();
                });
    }
    else if (k == "block")
    {
        add_all(items_other_id::BLOCKS);
    }
    else if (k == "skull")
    {
        // XXX exclude dwarf skulls ?
        add_all(items_other_id::CORPSEPIECE, [](df::item *item) -> bool
                {
                    df::item_corpsepiecest *i = virtual_cast<df::item_corpsepiecest>(item);
                    return i->corpse_flags.bits.skull && !i->corpse_flags.bits.unbutchered;
                });
    }
    else if (k == "bone")
    {
        for (df::item *item : world->items.other[items_other_id::CORPSEPIECE])
        {
            df::item_corpsepiecest *i = virtual_cast<df::item_corpsepiecest>(item);
            if (i->corpse_flags.bits.bone && !i->corpse_flags.bits.unbutchered)
            {
                n += i->material_amount[corpse_material_type::Bone];
            }
        }
    }
    else if (k == "shell")
    {
        for (df::item *item : world->items.other[items_other_id::CORPSEPIECE])
        {
            df::item_corpsepiecest *i = virtual_cast<df::item_corpsepiecest>(item);
            if (i->corpse_flags.bits.shell && !i->corpse_flags.bits.unbutchered)
            {
                n += i->material_amount[corpse_material_type::Shell];
            }
        }
    }
    else if (k == "wool")
    {
        // used for SpinThread which currently ignores the material_amount
        // note: if it didn't, use either HairWool or Yarn but not both
        add_all(items_other_id::CORPSEPIECE, [](df::item *item) -> bool
                {
                    df::item_corpsepiecest *i = virtual_cast<df::item_corpsepiecest>(item);
                    return i->corpse_flags.bits.hair_wool || i->corpse_flags.bits.yarn;
                });
    }
    else if (k == "bonebolts")
    {
        add_all(items_other_id::AMMO, [](df::item *i) -> bool
                {
                    return virtual_cast<df::item_ammost>(i)->skill_used == job_skill::BONECARVE;
                });
    }
    else if (k == "cloth")
    {
        add_all(items_other_id::CLOTH);
    }
    else if (k == "cloth_nodye")
    {
        add_all(items_other_id::CLOTH, [this](df::item *i) -> bool
                {
                    for (df::itemimprovement *imp : virtual_cast<df::item_clothst>(i)->improvements)
                    {
                        if (dye_plants.count(imp->mat_index) && dye_plants.at(imp->mat_index) == imp->mat_type)
                        {
                            return false;
                        }
                    }
                    return true;
                });
    }
    else if (k == "mechanism")
    {
        add_all(items_other_id::TRAPPARTS);
    }
    else if (k == "cage")
    {
        add_all(items_other_id::CAGE, [](df::item *i) -> bool
                {
                    for (auto ref : i->general_refs)
                    {
                        if (virtual_cast<df::general_ref_contains_unitst>(ref))
                            return false;
                        if (virtual_cast<df::general_ref_contains_itemst>(ref))
                            return false;
                        df::general_ref_building_holderst *bh = virtual_cast<df::general_ref_building_holderst>(ref);
                        if (bh && virtual_cast<df::building_trapst>(bh->getBuilding()))
                            return false;
                    }
                    return true;
                });
    }
    else if (k == "coffin_bld")
    {
        // count free constructed coffin buildings, not items
        for (df::building *bld : world->buildings.other[buildings_other_id::COFFIN])
        {
            if (!bld->owner)
            {
                n++;
            }
        }
    }
    else if (k == "coffin_bld_pet")
    {
        for (df::building *bld : world->buildings.other[buildings_other_id::COFFIN])
        {
            df::building_coffinst *coffin = virtual_cast<df::building_coffinst>(bld);
            if (coffin && !coffin->owner && !coffin->burial_mode.bits.no_pets)
            {
                n++;
            }
        }
    }
    else if (k == "weapon")
    {
        return count_stocks_weapon(out);
    }
    else if (k == "pick")
    {
        return count_stocks_weapon(out, job_skill::MINING);
    }
    else if (k == "axe")
    {
        return count_stocks_weapon(out, job_skill::AXE);
    }
    else if (k == "armor_torso")
    {
        return count_stocks_armor(out, items_other_id::ARMOR);
    }
    else if (k == "clothes_torso")
    {
        return count_stocks_clothes(out, items_other_id::ARMOR);
    }
    else if (k == "armor_legs")
    {
        return count_stocks_armor(out, items_other_id::PANTS);
    }
    else if (k == "clothes_legs")
    {
        return count_stocks_clothes(out, items_other_id::PANTS);
    }
    else if (k == "armor_head")
    {
        return count_stocks_armor(out, items_other_id::HELM);
    }
    else if (k == "clothes_head")
    {
        return count_stocks_clothes(out, items_other_id::HELM);
    }
    else if (k == "armor_hands")
    {
        return count_stocks_armor(out, items_other_id::GLOVES);
    }
    else if (k == "clothes_hands")
    {
        return count_stocks_clothes(out, items_other_id::GLOVES);
    }
    else if (k == "armor_feet")
    {
        return count_stocks_armor(out, items_other_id::SHOES);
    }
    else if (k == "clothes_feet")
    {
        return count_stocks_clothes(out, items_other_id::SHOES);
    }
    else if (k == "armor_shield")
    {
        return count_stocks_armor(out, items_other_id::SHIELD);
    }
    else if (k == "lye")
    {
        add_all(items_other_id::LIQUID_MISC, [](df::item *i) -> bool
                {
                    MaterialInfo mat(i);
                    return mat.material && mat.material->id == "LYE";
                    // TODO check container has no water
                });
    }
    else if (k == "plasterpowder")
    {
        add_all(items_other_id::POWDER_MISC, [](df::item *i) -> bool
                {
                    MaterialInfo mat(i);
                    return mat.material && mat.material->id == "PLASTER";
                });
    }
    else if (k == "wheelbarrow" || k == "minecart" || k == "nestbox" || k == "hive" || k == "jug" || k == "stepladder")
    {
        std::string ord = furniture_order(k);
        if (manager_subtype.count(ord))
            add_all(items_other_id::TOOL, [this, ord](df::item *item) -> bool
                    {
                        df::item_toolst *i = virtual_cast<df::item_toolst>(item);
                        return i->subtype->subtype == manager_subtype.at(ord) &&
                            i->stockpile.id == -1 &&
                            (i->vehicle_id == -1 || df::vehicle::find(i->vehicle_id)->route_id == -1);
                    });
    }
    else if (k == "honeycomb")
    {
        add_all(items_other_id::TOOL, [](df::item *i) -> bool
                {
                    return virtual_cast<df::item_toolst>(i)->subtype->id == "ITE_TOOL_HONEYCOMB";
                });
    }
    else if (k == "quiver")
    {
        add_all(items_other_id::QUIVER);
    }
    else if (k == "flask")
    {
        add_all(items_other_id::FLASK);
    }
    else if (k == "backpack")
    {
        add_all(items_other_id::BACKPACK);
    }
    else if (k == "leather")
    {
        add_all(items_other_id::SKIN_TANNED);
    }
    else if (k == "tallow")
    {
        add_all(items_other_id::GLOB, [](df::item *i) -> bool
                {
                    MaterialInfo mat(i);
                    return mat.material && mat.material->id == "TALLOW";
                });
    }
    else if (k == "giant_corkscrew")
    {
        if (manager_subtype.count("MakeGiantCorkscrew"))
            add_all(items_other_id::TRAPCOMP, [this](df::item *item) -> bool
                    {
                        df::item_trapcompst *i = virtual_cast<df::item_trapcompst>(item);
                        return i && i->subtype->subtype == manager_subtype.at("MakeGiantCorkscrew");
                    });
    }
    else if (k == "pipe_section")
    {
        add_all(items_other_id::PIPE_SECTION);
    }
    else if (k == "quern")
    {
        // include used in building
        return world->items.other[items_other_id::QUERN].size();
    }
    else if (k == "anvil")
    {
        add_all(items_other_id::ANVIL);
    }
    else if (k == "slab")
    {
        add_all(items_other_id::SLAB, [](df::item *i) -> bool { return i->getSlabEngravingType() == slab_engraving_type::Slab; });
    }
    else if (k == "dead_dwarf")
    {
        std::set<df::unit *> units;
        for (df::item *i : world->items.other[items_other_id::ANY_CORPSE])
        {
            if (!is_item_free(i))
            {
                continue;
            }
            int16_t race = -1;
            int16_t caste = -1;
            df::historical_figure *hf = nullptr;
            df::unit *u = nullptr;
            i->getCorpseInfo(&race, &caste, &hf, &u);
            if (u && Units::isCitizen(u) && Units::isDead(u))
            {
                units.insert(u);
            }
        }
        return units.size();
    }
    else
    {
        return find_furniture_itemcount(k);
    }

    return n;
}

// return the minimum of the number of free weapons for each subtype used by
// current civ
int32_t Stocks::count_stocks_weapon(color_ostream & out, df::job_skill skill)
{
    int32_t min = -1;
    auto search = [this, &min, skill](std::vector<int16_t> & idefs)
    {
        for (int16_t id : idefs)
        {
            df::itemdef_weaponst *idef = df::itemdef_weaponst::find(id);
            if (skill != df::job_skill(-1) && idef->skill_melee != skill)
            {
                continue;
            }
            if (idef->flags.is_set(weapon_flags::TRAINING))
            {
                continue;
            }
            int32_t count = 0;
            for (df::item *item : world->items.other[items_other_id::WEAPON])
            {
                df::item_weaponst *i = virtual_cast<df::item_weaponst>(item);
                if (i->subtype->subtype == idef->subtype &&
                        i->mat_type == 0 &&
                        is_item_free(i))
                {
                    count++;
                }
            }
            if (count < min || min == -1)
            {
                min = count;
            }
        }
    };
    auto & ue = ui->main.fortress_entity->entity_raw->equipment;
    search(ue.digger_id);
    search(ue.weapon_id);
    return min;
}

template<typename D, typename I>
static int32_t count_stocks_armor_helper(df::items_other_id oidx, std::vector<int16_t> & idefs, std::function<bool(D *)> pred = [](D *d) -> bool { return d->props.flags.is_set(armor_general_flags::METAL); })
{
    int32_t min = -1;
    for (int16_t id : idefs)
    {
        int32_t count = 0;
        D *idef = D::find(id);
        if (!pred(idef))
        {
            continue;
        }
        for (df::item *item : world->items.other[oidx])
        {
            I *i = virtual_cast<I>(item);
            if (i->subtype->subtype == idef->subtype &&
                    i->mat_type == 0 &&
                    Stocks::is_item_free(i))
            {
                count++;
            }
        }
        if (count < min || min == -1)
        {
            min = count;
        }
    }
    return min;
}

// return the minimum count of free metal armor piece per subtype
int32_t Stocks::count_stocks_armor(color_ostream & out, df::items_other_id oidx)
{
    auto & ue = ui->main.fortress_entity->entity_raw->equipment;
    switch (oidx)
    {
        case items_other_id::ARMOR:
            return count_stocks_armor_helper<df::itemdef_armorst, df::item_armorst>(oidx, ue.armor_id);
        case items_other_id::SHIELD:
            return count_stocks_armor_helper<df::itemdef_shieldst, df::item_shieldst>(oidx, ue.shield_id, [](df::itemdef_shieldst *) -> bool { return true; });
        case items_other_id::HELM:
            return count_stocks_armor_helper<df::itemdef_helmst, df::item_helmst>(oidx, ue.helm_id);
        case items_other_id::PANTS:
            return count_stocks_armor_helper<df::itemdef_pantsst, df::item_pantsst>(oidx, ue.pants_id);
        case items_other_id::GLOVES:
            return count_stocks_armor_helper<df::itemdef_glovesst, df::item_glovesst>(oidx, ue.gloves_id) / 2;
        case items_other_id::SHOES:
            return count_stocks_armor_helper<df::itemdef_shoesst, df::item_shoesst>(oidx, ue.shoes_id) / 2;
        default:
            return 0;
    }
}

template<typename D, typename I>
static int32_t count_stocks_clothes_helper(df::items_other_id oidx, std::vector<int16_t> & idefs)
{
    int32_t min = -1;
    for (int16_t id : idefs)
    {
        int32_t count = 0;
        D *idef = D::find(id);
        if (!idef->props.flags.is_set(armor_general_flags::SOFT)) // XXX
        {
            continue;
        }
        for (df::item *item : world->items.other[oidx])
        {
            I *i = virtual_cast<I>(item);
            if (i->subtype->subtype == idef->subtype &&
                    i->mat_type != 0 && // XXX
                    i->wear == 0 &&
                    Stocks::is_item_free(i))
            {
                count++;
            }
        }
        if (count < min || min == -1)
        {
            min = count;
        }
    }
    return min;
}

int32_t Stocks::count_stocks_clothes(color_ostream & out, df::items_other_id oidx)
{
    auto & ue = ui->main.fortress_entity->entity_raw->equipment;
    switch (oidx)
    {
        case items_other_id::ARMOR:
            return count_stocks_clothes_helper<df::itemdef_armorst, df::item_armorst>(oidx, ue.armor_id);
        case items_other_id::HELM:
            return count_stocks_clothes_helper<df::itemdef_helmst, df::item_helmst>(oidx, ue.helm_id);
        case items_other_id::PANTS:
            return count_stocks_clothes_helper<df::itemdef_pantsst, df::item_pantsst>(oidx, ue.pants_id);
        case items_other_id::GLOVES:
            return count_stocks_clothes_helper<df::itemdef_glovesst, df::item_glovesst>(oidx, ue.gloves_id) / 2;
        case items_other_id::SHOES:
            return count_stocks_clothes_helper<df::itemdef_shoesst, df::item_shoesst>(oidx, ue.shoes_id) / 2;
        default:
            return 0;
    }
}

// make it so the stocks of 'what' rises by 'amount'
void Stocks::queue_need(color_ostream & out, std::string what, int32_t amount)
{
    if (amount <= 0)
        return;

    std::vector<std::string> input;
    std::string order;

    if (what == "weapon")
    {
        queue_need_weapon(out, Needed.at("weapon"));
        return;
    }
    else if (what == "pick")
    {
        queue_need_weapon(out, Needed.at("pick"), job_skill::MINING);
        return;
    }
    else if (what == "axe")
    {
        queue_need_weapon(out, Needed.at("axe"), job_skill::AXE);
        return;
    }
    else if (what == "armor_torso")
    {
        queue_need_armor(out, items_other_id::ARMOR);
        return;
    }
    else if (what == "clothes_torso")
    {
        queue_need_clothes(out, items_other_id::ARMOR);
        return;
    }
    else if (what == "armor_legs")
    {
        queue_need_armor(out, items_other_id::PANTS);
        return;
    }
    else if (what == "clothes_legs")
    {
        queue_need_clothes(out, items_other_id::PANTS);
        return;
    }
    else if (what == "armor_head")
    {
        queue_need_armor(out, items_other_id::HELM);
        return;
    }
    else if (what == "clothes_head")
    {
        queue_need_clothes(out, items_other_id::HELM);
        return;
    }
    else if (what == "armor_hands")
    {
        queue_need_armor(out, items_other_id::GLOVES);
        return;
    }
    else if (what == "clothes_hands")
    {
        queue_need_clothes(out, items_other_id::GLOVES);
        return;
    }
    else if (what == "armor_feet")
    {
        queue_need_armor(out, items_other_id::SHOES);
        return;
    }
    else if (what == "clothes_feet")
    {
        queue_need_clothes(out, items_other_id::SHOES);
        return;
    }
    else if (what == "armor_shield")
    {
        queue_need_armor(out, items_other_id::SHIELD);
        return;
    }
    else if (what == "anvil")
    {
        queue_need_anvil(out);
        return;
    }
    else if (what == "coffin_bld")
    {
        queue_need_coffin_bld(out, amount);
        return;
    }
    else if (what == "coffin_bld_pet")
    {
        if (count.at("coffin_bld") >= Needed.at("coffin_bld"))
        {
            for (df::building *bld : world->buildings.other[buildings_other_id::COFFIN])
            {
                df::building_coffinst *cof = virtual_cast<df::building_coffinst>(bld);
                if (!cof->owner && cof->burial_mode.bits.no_pets)
                {
                    cof->burial_mode.bits.no_pets = 0;
                    break;
                }
            }
        }
        return;
    }
    else if (what == "raw_coke")
    {
        if (ai->plan->past_initial_phase)
        {
            for (auto vein : ai->plan->map_veins)
            {
                if (!is_raw_coke(vein.first).empty())
                {
                    ai->plan->dig_vein(out, vein.first, amount);
                    break;
                }
            }
        }
        return;
    }
    else if (what == "gypsum")
    {
        if (ai->plan->past_initial_phase)
        {
            for (auto vein : ai->plan->map_veins)
            {
                if (is_gypsum(vein.first))
                {
                    ai->plan->dig_vein(out, vein.first, amount);
                    break;
                }
            }
        }
        return;
    }
    else if (what == "food")
    {
        // XXX fish/hunt/cook ?
        if (last_warn_food < std::time(nullptr) - 600) // warn every 10 minutes
        {
            ai->debug(out, stl_sprintf("need %d more food", amount));
            last_warn_food = std::time(nullptr);
        }
        return;
    }
    else if (what == "thread_seeds")
    {
        // only useful at game start, with low seeds stocks
        order = "ProcessPlants";
        input.push_back("thread_plant");
    }
    else if (what == "dye_seeds" || what == "dye")
    {
        order = "MillPlants";
        input.push_back("dye_plant");
        input.push_back("bag");
    }
    else if (what == "wood")
    {
        // dont bother if the last designated tree is not cut yet
        if (is_cutting_trees())
            return;

        amount *= 2;
        if (amount > 30)
            amount = 30;
        std::set<df::coord, std::function<bool(df::coord, df::coord)>> tl = tree_list();
        for (df::coord t : tl)
        {
            if (Maps::getTileDesignation(t)->bits.dig == tile_dig_designation::Default)
            {
                if (amount <= 6)
                {
                    return;
                }
                amount -= 6;
            }
        }
        last_cutpos = cuttrees(out, amount / 6 + 1, tl);
        return;
    }
    else if (what == "honey")
    {
        order = "PressHoneycomb";
        input.push_back("honeycomb");
        input.push_back("jug");
    }
    else if (what == "drink")
    {
        std::map<std::string, std::string> orders
        {
            {"drink_plant", "BrewDrinkPlant"},
            {"drink_fruit", "BrewDrinkFruit"},
            {"honey", "BrewMead"},
        };
        auto score = [this](std::pair<const std::string, std::string> i) -> int32_t
        {
            int32_t c = count.at(i.first);
            for (df::manager_order *o : find_manager_orders(i.second))
            {
                c -= o->amount_left;
            }
            return c;
        };
        auto max = std::max_element(orders.begin(), orders.end(), [score](std::pair<const std::string, std::string> a, std::pair<const std::string, std::string> b) -> bool { return score(a) < score(b); });
        order = max->second;
        input.push_back(max->first);
        input.push_back("barrel");
        amount = (amount + 4) / 5; // accounts for brewer yield, but not for input stack size
    }
    else if (what == "block")
    {
        amount = (amount + 3) / 4;
        // no stone => make wooden blocks (needed for pumps for aquifer handling)
        bool found = false;
        for (df::item *i : world->items.other[items_other_id::BOULDER])
        {
            if (is_item_free(i) && !ui->economic_stone[i->getMaterialIndex()])
            {
                // TODO check the boulders we find there are reachable..
                found = true;
                break;
            }
        }
        if (!found)
        {
            if (amount > 2)
                amount = 2;
            order = "ConstructWoodenBlocks";
        }
    }
    else if (what == "coal")
    {
        // dont use wood -> charcoal if we have bituminous coal
        // (except for bootstraping)
        if (amount > 2 - count.at("coal") && count.at("raw_coke") > WatchStock.at("raw_coke"))
        {
            amount = 2 - count.at("coal");
        }
    }
    else if (what == "ash")
    {
        input.push_back("wood");
    }
    else if (what == "lye")
    {
        input.push_back("ash");
        input.push_back("bucket");
    }
    else if (what == "soap")
    {
        input.push_back("lye");
        input.push_back("tallow");
    }
    else if (what == "plasterpowder")
    {
        input.push_back("gypsum");
        input.push_back("bag");
    }

    if (order.empty())
    {
        order = furniture_order(what);
    }
    ai->debug(out, stl_sprintf("stocks: need %d %s for %s", amount, order.c_str(), what.c_str()));

    if (!input.empty())
    {
        int32_t i_amount = amount;
        for (std::string i : input)
        {
            int32_t c = count.at(i);
            if (c < i_amount)
            {
                i_amount = c;
            }
            if (c < amount && Needed.count(i))
            {
                ai->debug(out, stl_sprintf("stocks: want %d more %s for %d/%d %s", amount - c, i.c_str(), i_amount, amount, order.c_str()));
                queue_need(out, i, amount - c);
            }
        }
        amount = i_amount;
    }

    if (ManagerMatCategory.count(order))
    {
        std::string matcat = ManagerMatCategory.at(order);
        df::job_type job = df::job_type(-1);
        find_enum_item(&job, order);
        int32_t i_amount = count.at(matcat) - count_manager_orders_matcat(matcat, job);
        if (i_amount < amount && Needed.count(matcat))
        {
            ai->debug(out, stl_sprintf("stocks: want %d more %s for %d/%d %s", amount - i_amount, matcat.c_str(), i_amount, amount, order.c_str()));
            queue_need(out, matcat, amount - i_amount);
        }
        if (amount > i_amount)
        {
            amount = i_amount;
        }
    }

    if (amount > 30)
        amount = 30;

    for (df::manager_order *o : find_manager_orders(order))
    {
        amount -= o->amount_total;
    }

    if (amount <= 0)
        return;

    add_manager_order(out, order, amount);
}

// forge weapons
void Stocks::queue_need_weapon(color_ostream & out, int32_t needed, df::job_skill skill)
{
    if (skill == df::job_skill(-1) && (count.at("pick") == 0 || count.at("axe") == 0))
        return;

    std::map<int32_t, int32_t> bars;
    int32_t coal_bars = count.at("coal");
    if (!world->buildings.other[buildings_other_id::FURNACE_SMELTER_MAGMA].empty())
        coal_bars = 50000;

    for (df::item *item : world->items.other[items_other_id::BAR])
    {
        df::item_actual *i = virtual_cast<df::item_actual>(item);
        if (i->getMaterial() == 0)
        {
            bars[i->getMaterialIndex()] += i->stack_size;
        }
    }

    // rough account of already queued jobs consumption
    for (df::manager_order *mo : world->manager_orders)
    {
        if (mo->mat_type == 0)
        {
            bars[mo->mat_index] -= 4 * mo->amount_total;
            coal_bars -= mo->amount_total;
        }
    }

    if (metal_digger_pref.empty())
    {
        for (size_t mi = 0; mi < world->raws.inorganics.size(); mi++)
        {
            if (world->raws.inorganics[mi]->material.flags.is_set(material_flags::ITEMS_DIGGER))
            {
                metal_digger_pref.push_back(mi);
            }
        }
        std::sort(metal_digger_pref.begin(), metal_digger_pref.end(), [](int32_t a, int32_t b) -> bool
                {
                    // should roughly order metals by effectiveness
                    return world->raws.inorganics[a]->material.strength.yield[strain_type::IMPACT] > world->raws.inorganics[b]->material.strength.yield[strain_type::IMPACT];
                });
    }

    if (metal_weapon_pref.empty())
    {
        for (size_t mi = 0; mi < world->raws.inorganics.size(); mi++)
        {
            if (world->raws.inorganics[mi]->material.flags.is_set(material_flags::ITEMS_WEAPON))
            {
                metal_weapon_pref.push_back(mi);
            }
        }
        std::sort(metal_weapon_pref.begin(), metal_weapon_pref.end(), [](int32_t a, int32_t b) -> bool
                {
                    return world->raws.inorganics[a]->material.strength.yield[strain_type::IMPACT] > world->raws.inorganics[b]->material.strength.yield[strain_type::IMPACT];
                });
    }

    cache_hash<int32_t, int32_t> may_forge_cache([this, &out](int32_t mi) -> int32_t { return may_forge_bars(out, mi); });

    auto search = [this, &out, needed, skill, &bars, &coal_bars, &may_forge_cache](std::vector<int16_t> & idefs, std::vector<int32_t> & pref)
    {
        for (int16_t id : idefs)
        {
            df::itemdef_weaponst *idef = df::itemdef_weaponst::find(id);
            if (skill != df::job_skill(-1) && idef->skill_melee != skill)
                continue;
            if (idef->flags.is_set(weapon_flags::TRAINING))
                continue;

            int32_t cnt = needed;
            for (df::item *item : world->items.other[items_other_id::WEAPON])
            {
                df::item_weaponst *i = virtual_cast<df::item_weaponst>(item);
                if (i->subtype->subtype == idef->subtype && is_item_free(i))
                {
                    cnt--;
                }
            }
            for (df::manager_order *mo : world->manager_orders)
            {
                if (mo->job_type == job_type::MakeWeapon && mo->item_subtype == idef->subtype)
                {
                    cnt -= mo->amount_total;
                }
            }
            if (cnt <= 0)
                continue;

            int32_t need_bars = idef->material_size / 3; // need this many bars to forge one idef item
            if (need_bars < 1)
                need_bars = 1;

            for (int32_t mi : pref)
            {
                if (bars[mi] < need_bars && may_forge_cache[mi] != -1)
                    break;
                int32_t nw = bars[mi] / need_bars;
                if (nw > coal_bars)
                    nw = coal_bars;
                if (nw > cnt)
                    nw = cnt;
                if (nw <= 0)
                    continue;

                ai->debug(out, stl_sprintf("stocks: queue %d MakeWeapon %s %s", nw, world->raws.inorganics[mi]->id.c_str(), idef->id.c_str()));
                df::manager_order *mo = df::allocate<df::manager_order>();
                mo->job_type = job_type::MakeWeapon;
                mo->item_type = item_type::NONE;
                mo->item_subtype = idef->subtype;
                mo->mat_type = 0;
                mo->mat_index = mi;
                mo->amount_left = nw;
                mo->amount_total = nw;
                world->manager_orders.push_back(mo);
                bars[mi] -= nw * need_bars;
                coal_bars -= nw;
                cnt -= nw;
                if (may_forge_cache[mi] != -1) // dont use lesser metal
                    break;
            }
        }
    };
    auto & ue = ui->main.fortress_entity->entity_raw->equipment;
    search(ue.digger_id, metal_digger_pref);
    search(ue.weapon_id, metal_weapon_pref);
}

template<typename D, typename I>
static void queue_need_armor_helper(AI *ai, std::vector<int32_t> & metal_armor_pref, color_ostream & out, df::items_other_id oidx, std::vector<int16_t> & idefs, df::job_type job, std::map<int32_t, int32_t> & bars, int32_t & coal_bars, cache_hash<int32_t, int32_t> & may_forge_cache, int32_t div = 1, std::function<bool(D *)> pred = [](D *d) -> bool { return d->props.flags.is_set(armor_general_flags::METAL); })
{
    for (int16_t id : idefs)
    {
        D *idef = D::find(id);

        if (!pred(idef))
        {
            continue;
        }

        const static std::map<df::items_other_id, std::string> needed
        {
            {items_other_id::ARMOR, "armor_torso"},
            {items_other_id::SHIELD, "armor_shield"},
            {items_other_id::HELM, "armor_head"},
            {items_other_id::PANTS, "armor_legs"},
            {items_other_id::GLOVES, "armor_hands"},
            {items_other_id::SHOES, "armor_feet"},
        };
        int32_t cnt = Needed.at(needed.at(oidx));
        int32_t have = 0;
        for (df::item *item : world->items.other[oidx])
        {
            I *i = virtual_cast<I>(item);
            if (i->subtype->subtype == idef->subtype && i->mat_type == 0 && ai->stocks->is_item_free(i))
            {
                have++;
            }
        }
        cnt -= have / div;

        for (df::manager_order *mo : world->manager_orders)
        {
            if (mo->job_type == job && mo->item_subtype == idef->subtype)
            {
                cnt -= mo->amount_total;
            }
        }
        if (cnt <= 0)
        {
            continue;
        }

        int32_t need_bars = idef->material_size / 3; // need this many bars to forge one idef item
        if (need_bars < 1)
            need_bars = 1;

        for (int32_t mi : metal_armor_pref)
        {
            if (bars[mi] < need_bars && may_forge_cache[mi] != -1)
                break;
            int32_t nw = bars[mi] / need_bars;
            if (nw > coal_bars)
                nw = coal_bars;
            if (nw > cnt)
                nw = cnt;
            if (nw <= 0)
                continue;

            ai->debug(out, stl_sprintf("stocks: queue %d %s %s %s", nw, ENUM_KEY_STR(job_type, job).c_str(), world->raws.inorganics[mi]->id.c_str(), idef->id.c_str()));
            df::manager_order *mo = df::allocate<df::manager_order>();
            mo->job_type = job;
            mo->item_type = item_type::NONE;
            mo->item_subtype = idef->subtype;
            mo->mat_type = 0;
            mo->mat_index = mi;
            mo->amount_left = nw;
            mo->amount_total = nw;
            world->manager_orders.push_back(mo);
            bars[mi] -= nw * need_bars;
            coal_bars -= nw;
            cnt -= nw;
            if (may_forge_cache[mi] != -1)
                break;
        }
    }
}

// forge armor pieces
void Stocks::queue_need_armor(color_ostream & out, df::items_other_id oidx)
{
    std::map<int32_t, int32_t> bars;
    int32_t coal_bars = count.at("coal");
    if (!world->buildings.other[buildings_other_id::FURNACE_SMELTER_MAGMA].empty())
        coal_bars = 50000;

    for (df::item *item : world->items.other[items_other_id::BAR])
    {
        df::item_actual *i = virtual_cast<df::item_actual>(item);
        if (i->getMaterial() == 0)
        {
            bars[i->getMaterialIndex()] += i->stack_size;
        }
    }

    // rough account of already queued jobs consumption
    for (df::manager_order *mo : world->manager_orders)
    {
        if (mo->mat_type == 0)
        {
            bars[mo->mat_index] -= 4 * mo->amount_total;
            coal_bars -= mo->amount_total;
        }
    }

    if (metal_armor_pref.empty())
    {
        for (size_t mi = 0; mi < world->raws.inorganics.size(); mi++)
        {
            if (world->raws.inorganics[mi]->material.flags.is_set(material_flags::ITEMS_ARMOR))
            {
                metal_armor_pref.push_back(mi);
            }
        }
        std::sort(metal_armor_pref.begin(), metal_armor_pref.end(), [](int32_t a, int32_t b) -> bool
                {
                    return world->raws.inorganics[a]->material.strength.yield[strain_type::IMPACT] > world->raws.inorganics[b]->material.strength.yield[strain_type::IMPACT];
                });
    }

    cache_hash<int32_t, int32_t> may_forge_cache([this, &out](int32_t mi) -> int32_t { return may_forge_bars(out, mi); });

    auto & ue = ui->main.fortress_entity->entity_raw->equipment;

    switch (oidx)
    {
        case items_other_id::ARMOR:
            queue_need_armor_helper<df::itemdef_armorst, df::item_armorst>(ai, metal_armor_pref, out, oidx, ue.armor_id, job_type::MakeArmor, bars, coal_bars, may_forge_cache);
            return;
        case items_other_id::SHIELD:
            queue_need_armor_helper<df::itemdef_shieldst, df::item_shieldst>(ai, metal_armor_pref, out, oidx, ue.shield_id, job_type::MakeShield, bars, coal_bars, may_forge_cache, 1, [](df::itemdef_shieldst *) -> bool { return true; });
            return;
        case items_other_id::HELM:
            queue_need_armor_helper<df::itemdef_helmst, df::item_helmst>(ai, metal_armor_pref, out, oidx, ue.helm_id, job_type::MakeHelm, bars, coal_bars, may_forge_cache);
            return;
        case items_other_id::PANTS:
            queue_need_armor_helper<df::itemdef_pantsst, df::item_pantsst>(ai, metal_armor_pref, out, oidx, ue.pants_id, job_type::MakePants, bars, coal_bars, may_forge_cache);
            return;
        case items_other_id::GLOVES:
            queue_need_armor_helper<df::itemdef_glovesst, df::item_glovesst>(ai, metal_armor_pref, out, oidx, ue.gloves_id, job_type::MakeGloves, bars, coal_bars, may_forge_cache, 2);
            return;
        case items_other_id::SHOES:
            queue_need_armor_helper<df::itemdef_shoesst, df::item_shoesst>(ai, metal_armor_pref, out, oidx, ue.shoes_id, job_type::MakeGloves, bars, coal_bars, may_forge_cache, 2);
            return;
        default:
            return;
    }
}

void Stocks::queue_need_anvil(color_ostream & out)
{
    std::map<int32_t, int32_t> bars;
    int32_t coal_bars = count.at("coal");
    if (!world->buildings.other[buildings_other_id::FURNACE_SMELTER_MAGMA].empty())
        coal_bars = 50000;

    for (df::item *item : world->items.other[items_other_id::BAR])
    {
        df::item_actual *i = virtual_cast<df::item_actual>(item);
        if (i->getMaterial() == 0)
        {
            bars[i->getMaterialIndex()] += i->stack_size;
        }
    }

    // rough account of already queued jobs consumption
    for (df::manager_order *mo : world->manager_orders)
    {
        if (mo->mat_type == 0)
        {
            bars[mo->mat_index] -= 4 * mo->amount_total;
            coal_bars -= mo->amount_total;
        }
    }

    if (metal_anvil_pref.empty())
    {
        for (size_t mi = 0; mi < world->raws.inorganics.size(); mi++)
        {
            if (world->raws.inorganics[mi]->material.flags.is_set(material_flags::ITEMS_ANVIL))
            {
                metal_anvil_pref.push_back(mi);
            }
        }
    }

    cache_hash<int32_t, int32_t> may_forge_cache([this, &out](int32_t mi) -> int32_t { return may_forge_bars(out, mi); });

    int32_t cnt = Needed.at("anvil");
    cnt -= count.at("anvil");

    for (df::manager_order *mo : world->manager_orders)
    {
        if (mo->job_type == job_type::ForgeAnvil)
        {
            cnt -= mo->amount_total;
        }
    }
    if (cnt <= 0)
        return;

    int32_t need_bars = 1;

    for (int32_t mi : metal_anvil_pref)
    {
        if (bars[mi] < need_bars && may_forge_cache[mi] != -1)
            break;
        int32_t nw = bars[mi] / need_bars;
        if (nw > coal_bars)
            nw = coal_bars;
        if (nw > cnt)
            nw = cnt;
        if (nw <= 0)
            continue;

        ai->debug(out, stl_sprintf("stocks: queue %d ForgeAnvil %s", nw, world->raws.inorganics[mi]->id.c_str()));
        df::manager_order *mo = df::allocate<df::manager_order>();
        mo->job_type = job_type::ForgeAnvil;
        mo->item_type = item_type::NONE;
        mo->item_subtype = -1;
        mo->mat_type = 0;
        mo->mat_index = mi;
        mo->amount_left = nw;
        mo->amount_total = nw;
        world->manager_orders.push_back(mo);
        bars[mi] -= nw * need_bars;
        coal_bars -= nw;
        cnt -= nw;
        if (may_forge_cache[mi] != -1)
            break;
    }
}

template<typename D, typename I>
static void queue_need_clothes_helper(AI *ai, color_ostream & out, df::items_other_id oidx, std::vector<int16_t> & idefs, int32_t & available_cloth, df::job_type job, int32_t needed, int32_t div = 1)
{
    for (int16_t id : idefs)
    {
        D *idef = D::find(id);
        if (!idef->props.flags.is_set(armor_general_flags::SOFT)) // XXX
            continue;

        int32_t cnt = needed;
        int32_t have = 0;
        for (df::item *item : world->items.other[oidx])
        {
            I *i = virtual_cast<I>(item);
            if (i->subtype->subtype == idef->subtype &&
                    i->mat_type != 0 &&
                    i->wear == 0 &&
                    ai->stocks->is_item_free(i))
            {
                have++;
            }
        }
        cnt -= have / div;

        for (df::manager_order *mo : world->manager_orders)
        {
            if (mo->job_type == job && mo->item_subtype == idef->subtype)
            {
                cnt -= mo->amount_total;
            }
            // TODO subtract available_cloth too
        }
        if (cnt > available_cloth)
            cnt = available_cloth;
        if (cnt <= 0)
            continue;

        ai->debug(out, stl_sprintf("stocks: queue %d %s cloth %s", cnt, ENUM_KEY_STR(job_type, job).c_str(), idef->id.c_str()));
        df::manager_order *mo = df::allocate<df::manager_order>();
        mo->job_type = job;
        mo->item_type = item_type::NONE;
        mo->item_subtype = idef->subtype;
        mo->mat_type = -1;
        mo->mat_index = -1;
        mo->material_category.bits.cloth = 1;
        mo->amount_left = cnt;
        mo->amount_total = cnt;
        world->manager_orders.push_back(mo);

        available_cloth -= cnt;
    }
}

void Stocks::queue_need_clothes(color_ostream & out, df::items_other_id oidx)
{
    // try to avoid cancel spam
    int32_t available_cloth = count.at("cloth") - 20;

    auto & ue = ui->main.fortress_entity->entity_raw->equipment;

    switch (oidx)
    {
        case items_other_id::ARMOR:
            queue_need_clothes_helper<df::itemdef_armorst, df::item_armorst>(ai, out, oidx, ue.armor_id, available_cloth, job_type::MakeArmor, Needed.at("clothes_torso"));
            return;
        case items_other_id::HELM:
            queue_need_clothes_helper<df::itemdef_helmst, df::item_helmst>(ai, out, oidx, ue.helm_id, available_cloth, job_type::MakeHelm, Needed.at("clothes_head"));
            return;
        case items_other_id::PANTS:
            queue_need_clothes_helper<df::itemdef_pantsst, df::item_pantsst>(ai, out, oidx, ue.pants_id, available_cloth, job_type::MakePants, Needed.at("clothes_legs"));
            return;
        case items_other_id::GLOVES:
            queue_need_clothes_helper<df::itemdef_glovesst, df::item_glovesst>(ai, out, oidx, ue.gloves_id, available_cloth, job_type::MakeGloves, Needed.at("clothes_hands"), 2);
            return;
        case items_other_id::SHOES:
            queue_need_clothes_helper<df::itemdef_shoesst, df::item_shoesst>(ai, out, oidx, ue.shoes_id, available_cloth, job_type::MakeShoes, Needed.at("clothes_feet"), 2);
            return;
        default:
            return;
    }
}

void Stocks::queue_need_coffin_bld(color_ostream & out, int32_t amount)
{
    // dont dig too early
    if (!ai->plan->find_room("cemetary", [](room *r) -> bool { return r->status != "plan"; }))
        return;

    // count actually allocated (plan wise) coffin buildings
    if (ai->plan->find_room("cemetary", [&amount](room *r) -> bool
                {
                    for (furniture *f : r->layout)
                    {
                        if (f->item == "coffin" && f->bld_id == -1 && !f->ignore)
                            amount--;
                    }
                    return amount <= 0;
                }))
        return;

    for (int32_t i = 0; i < amount; i++)
    {
        ai->plan->getcoffin(out);
    }
}

// make it so the stocks of 'what' decrease by 'amount'
void Stocks::queue_use(color_ostream & out, std::string what, int32_t amount)
{
    if (amount <= 0)
        return;

    std::vector<std::string> input;
    std::string order;

    if (what == "metal_ore")
    {
        queue_use_metal_ore(out, amount);
        return;
    }
    else if (what == "raw_coke")
    {
        queue_use_raw_coke(out, amount);
        return;
    }
    else if (what == "roughgem")
    {
        queue_use_gems(out, amount);
        return;
    }
    else if (what == "raw_adamantine")
    {
        order = "ExtractMetalStrands";
    }
    else if (what == "clay")
    {
        input.push_back("coal"); // TODO: handle magma kilns
        order = "MakeClayStatue";
    }
    else if (what == "drink_plant" || what == "drink_fruit")
    {
        order = what == "drink_plant" ? "BrewDrinkPlant" : "BrewDrinkFruit";
        // stuff may rot/be brewed before we can process it
        if (amount > 10)
            amount /= 2;
        if (amount > 4)
            amount /= 2;

        input.push_back("barrel");

        if (!need_more("drink"))
        {
            return;
        }
    }
    else if (what == "thread_plant")
    {
        order = "ProcessPlants";
        // stuff may rot/be brewed before we can process it
        if (amount > 10)
            amount /= 2;
        if (amount > 4)
            amount /= 2;
    }
    else if (what == "mill_plant" || what == "bag_plant")
    {
        order = what == "mill_plant" ? "MillPlants" : "ProcessPlantsBag";
        // stuff may rot/be brewed before we can process it
        if (amount > 10)
            amount /= 2;
        if (amount > 4)
            amount /= 2;
        input.push_back("bag");
    }
    else if (what == "food_ingredients")
    {
        order = "PrepareMeal";
        amount = (amount + 4) / 5;
        if (!need_more("food"))
        {
            return;
        }
    }
    else if (what == "skull")
    {
        order = "MakeTotem";
    }
    else if (what == "bone")
    {
        int32_t nhunters = 0;
        for (df::unit *u : world->units.active)
        {
            if (Units::isCitizen(u) && !Units::isDead(u) && u->status.labors[unit_labor::HUNT])
            {
                nhunters++;
            }
        }
        if (!nhunters)
        {
            return;
        }
        int32_t need_crossbow = nhunters + 1 - count.at("crossbow");
        if (need_crossbow > 0)
        {
            order = "MakeBoneCrossbow";
            if (amount > need_crossbow)
                amount = need_crossbow;
        }
        else
        {
            order = "MakeBoneBolt";
            int32_t stock = count.at("bonebolts");
            if (amount > 1000 - stock)
                amount = 1000 - stock;
            if (amount > 10)
                amount /= 2;
            if (amount > 4)
                amount /= 2;
        }
    }
    else if (what == "shell")
    {
        order = "DecorateWithShell";
    }
    else if (what == "wool")
    {
        order = "SpinThread";
    }
    else if (what == "cloth_nodye")
    {
        order = "DyeCloth";
        input.push_back("dye");
        if (amount > 10)
            amount /= 2;
        if (amount > 4)
            amount /= 2;
    }
    else if (what == "raw_fish")
    {
        order = "PrepareRawFish";
    }
    else if (what == "honeycomb")
    {
        order = "PressHoneycomb";
        input.push_back("jug");
    }
    else if (what == "honey")
    {
        order = "BrewMead";
        input.push_back("barrel");
    }
    else if (what == "milk")
    {
        order = "MakeCheese";
    }
    else if (what == "tallow")
    {
        order = "MakeSoap";
        input.push_back("lye");
        if (!need_more("soap"))
        {
            return;
        }
    }

    ai->debug(out, stl_sprintf("stocks: use %d %s for %s", amount, order.c_str(), what.c_str()));

    if (!input.empty())
    {
        int32_t i_amount = amount;
        for (std::string i : input)
        {
            int32_t c = count.at(i);
            if (i_amount > c)
                i_amount = c;
            if (c < amount && Needed.count(i))
            {
                ai->debug(out, stl_sprintf("stocks: want %d more %s for %d/%d %s", amount - c, i.c_str(), i_amount, amount, order.c_str()));
                queue_need(out, i, amount - c);
            }
        }
        amount = i_amount;
    }

    if (amount > 30)
        amount = 30;

    for (df::manager_order *mo : find_manager_orders(order))
    {
        amount -= mo->amount_total;
    }

    if (amount <= 0)
        return;

    add_manager_order(out, order, amount);
}

// cut gems
void Stocks::queue_use_gems(color_ostream & out, int32_t amount)
{
    for (df::manager_order *mo : world->manager_orders)
    {
        if (mo->job_type == job_type::CutGems)
        {
            return;
        }
    }
    df::item *base = nullptr;
    for (df::item *i : world->items.other[items_other_id::ROUGH])
    {
        if (i->getMaterial() == 0 && is_item_free(i))
        {
            base = i;
            break;
        }
    }
    if (!base)
    {
        return;
    }
    int32_t this_amount = 0;
    for (df::item *i : world->items.other[items_other_id::ROUGH])
    {
        if (i->getMaterial() == base->getMaterial() && i->getMaterialIndex() == base->getMaterialIndex() && is_item_free(i))
        {
            this_amount++;
        }
    }
    if (amount > this_amount)
        amount = this_amount;
    if (amount >= 10)
        amount = amount * 3 / 4;
    if (amount > 30)
        amount = 30;

    ai->debug(out, stl_sprintf("stocks: queue %d CutGems %s", amount, MaterialInfo(base).getToken().c_str()));
    df::manager_order *mo = df::allocate<df::manager_order>();
    mo->job_type = job_type::CutGems;
    mo->item_type = item_type::NONE;
    mo->item_subtype = -1;
    mo->mat_type = base->getMaterial();
    mo->mat_index = base->getMaterialIndex();
    mo->amount_left = amount;
    mo->amount_total = amount;
    world->manager_orders.push_back(mo);
}

// smelt metal ores
void Stocks::queue_use_metal_ore(color_ostream & out, int32_t amount)
{
    // make coke from bituminous coal has priority
    if (count.at("raw_coke") > WatchStock.at("raw_coke") && count.at("coal") < 100)
    {
        return;
    }
    for (df::manager_order *mo : world->manager_orders)
    {
        if (mo->job_type == job_type::SmeltOre)
        {
            return;
        }
    }

    df::item *base = nullptr;
    for (df::item *i : world->items.other[items_other_id::BOULDER])
    {
        if (is_metal_ore(i) && is_item_free(i))
        {
            base = i;
            break;
        }
    }
    if (!base)
    {
        return;
    }
    int32_t this_amount = 0;
    for (df::item *i : world->items.other[items_other_id::BOULDER])
    {
        if (i->getMaterial() == base->getMaterial() && i->getMaterialIndex() == base->getMaterialIndex() && is_item_free(i))
        {
            this_amount++;
        }
    }
    if (amount > this_amount)
        amount = this_amount;
    if (amount >= 10)
        amount = amount * 3 / 4;
    if (amount > 30)
        amount = 30;

    if (world->buildings.other[buildings_other_id::FURNACE_SMELTER_MAGMA].empty())
    {
        if (amount > count.at("coal"))
            amount = count.at("coal");
        if (amount <= 0)
            return;
    }

    ai->debug(out, stl_sprintf("stocks: queue %d SmeltOre %s", amount, MaterialInfo(base).getToken().c_str()));
    df::manager_order *mo = df::allocate<df::manager_order>();
    mo->job_type = job_type::SmeltOre;
    mo->item_type = item_type::NONE;
    mo->item_subtype = -1;
    mo->mat_type = base->getMaterial();
    mo->mat_index = base->getMaterialIndex();
    mo->amount_left = amount;
    mo->amount_total = amount;
    world->manager_orders.push_back(mo);
}

// bituminous_coal -> coke
void Stocks::queue_use_raw_coke(color_ostream & out, int32_t amount)
{
    is_raw_coke(0); // populate raw_coke_inv
    for (df::manager_order *mo : world->manager_orders)
    {
        if (mo->job_type == job_type::CustomReaction && raw_coke_inv.count(mo->reaction_name))
        {
            return;
        }
    }

    std::string reaction;
    df::item *base = nullptr;
    for (df::item *i : world->items.other[items_other_id::BOULDER])
    {
        reaction = is_raw_coke(i);
        if (!reaction.empty() && is_item_free(i))
        {
            base = i;
            break;
        }
    }
    if (!base)
    {
        return;
    }

    int32_t this_amount = 0;
    for (df::item *i : world->items.other[items_other_id::BOULDER])
    {
        if (i->getMaterial() == base->getMaterial() && i->getMaterialIndex() == base->getMaterialIndex() && is_item_free(i))
        {
            this_amount++;
        }
    }
    if (amount > this_amount)
        amount = this_amount;
    if (amount >= 10)
        amount = amount * 3 / 4;
    if (amount > 30)
        amount = 30;

    if (world->buildings.other[buildings_other_id::FURNACE_SMELTER_MAGMA].empty())
    {
        // need at least 1 unit of fuel to bootstrap
        if (count.at("coal") <= 0)
        {
            return;
        }
    }

    ai->debug(out, stl_sprintf("stocks: queue %d %s", amount, reaction.c_str()));
    df::manager_order *mo = df::allocate<df::manager_order>();
    mo->job_type = job_type::CustomReaction;
    mo->item_type = item_type::NONE;
    mo->item_subtype = -1;
    mo->mat_type = -1;
    mo->mat_index = -1;
    mo->amount_left = amount;
    mo->amount_total = amount;
    mo->reaction_name = reaction;
    world->manager_orders.push_back(mo);
}

// designate some trees for woodcutting
df::coord Stocks::cuttrees(color_ostream & out, int32_t amount, std::set<df::coord, std::function<bool(df::coord, df::coord)>> list)
{
    // return the bottom-rightest designated tree
    df::coord br;
    br.clear();
    for (df::coord tree : list)
    {
        if (ENUM_ATTR(tiletype, material, *Maps::getTileType(tree)) != tiletype_material::TREE)
            continue;
        if (Maps::getTileDesignation(tree)->bits.dig == tile_dig_designation::Default)
            continue;
        Plan::dig_tile(tree, tile_dig_designation::Default);
        if (!br.isValid() || (br.x & -16) < (tree.x & -16) || ((br.x & -16) == (tree.x & -16) && (br.y & -16) < (tree.y & -16)))
        {
            br = tree;
        }
        amount--;
        if (amount <= 0)
            break;
    }
    return br;
}

// return a list of trees on the map
// lists only visible trees, sorted by distance from the fort entrance
// expensive method, dont call often
std::set<df::coord, std::function<bool(df::coord, df::coord)>> Stocks::tree_list()
{
    last_treelist.clear();
    for (df::plant *p : world->plants.tree_dry)
    {
        if (ENUM_ATTR(tiletype, material, *Maps::getTileType(p->pos)) == tiletype_material::TREE && !Maps::getTileDesignation(p->pos)->bits.hidden)
        {
            last_treelist.insert(p->pos);
        }
    }
    for (df::plant *p : world->plants.tree_wet)
    {
        if (ENUM_ATTR(tiletype, material, *Maps::getTileType(p->pos)) == tiletype_material::TREE && !Maps::getTileDesignation(p->pos)->bits.hidden)
        {
            last_treelist.insert(p->pos);
        }
    }
    return last_treelist;
}

// check if an item is free to use
bool Stocks::is_item_free(df::item *i, bool allow_nonempty)
{
    if (i->flags.bits.trader || // merchant's item
            i->flags.bits.in_job || // current job item
            i->flags.bits.construction ||
            i->flags.bits.encased ||
            i->flags.bits.removed || // deleted object
            i->flags.bits.forbid || // user forbidden (or dumped)
            i->flags.bits.dump ||
            i->flags.bits.in_chest) // in infirmary (XXX dwarf owned items ?)
    {
        return false;
    }

    if (i->flags.bits.container && !allow_nonempty)
    {
        // is empty
        for (auto ir : i->general_refs)
        {
            if (virtual_cast<df::general_ref_contains_itemst>(ir))
            {
                return false;
            }
        }
    }

    if (i->flags.bits.in_inventory)
    {
        // is not in a unit's inventory (ignore if it is simply hauled)
        for (auto ir : i->general_refs)
        {
            if (virtual_cast<df::general_ref_unit_holderst>(ir))
            {
                for (auto ii : ir->getUnit()->inventory)
                {
                   if (ii->item == i && ii->mode != df::unit_inventory_item::Hauled)
                   {
                       return false;
                   }
                }
            }
            if (virtual_cast<df::general_ref_contained_in_itemst>(ir) && !is_item_free(ir->getItem(), true))
            {
                return false;
            }
        }
    }

    if (i->flags.bits.in_building)
    {
        // is not part of a building construction materials
        for (auto ir : i->general_refs)
        {
            if (virtual_cast<df::general_ref_building_holderst>(ir))
            {
                for (auto bi : virtual_cast<df::building_actual>(ir->getBuilding())->contained_items)
                {
                    if (bi->use_mode == 2 && bi->item == i)
                    {
                        return false;
                    }
                }
            }
        }
    }

    df::coord pos = Items::getPosition(i);

    extern AI *dwarfAI; // XXX

    // If no dwarf can walk to it from the fort entrance, it's probably up in
    // a tree or down in the caverns.
    if (dwarfAI->plan->fort_entrance &&
            Plan::getTileWalkable(dwarfAI->plan->fort_entrance->max) !=
            Plan::getTileWalkable(pos))
    {
        return false;
    }

    df::tile_designation *td = Maps::getTileDesignation(pos);
    return td && !td->bits.hidden && td->bits.flow_size < 4;
}

bool Stocks::is_metal_ore(int32_t mi)
{
    return world->raws.inorganics[mi]->flags.is_set(inorganic_flags::METAL_ORE);
}

bool Stocks::is_metal_ore(df::item *i)
{
    if (virtual_cast<df::item_boulderst>(i) && i->getMaterial() == 0)
    {
        return is_metal_ore(i->getMaterialIndex());
    }
    return false;
}

std::string Stocks::is_raw_coke(int32_t mi)
{
    // mat_index => custom reaction name
    if (raw_coke.empty())
    {
        for (df::reaction *r : world->raws.reactions)
        {
            if (r->reagents.size() != 1)
                continue;

            int32_t mat;

            bool found = false;
            for (df::reaction_reagent *rr : r->reagents)
            {
                df::reaction_reagent_itemst *rri = virtual_cast<df::reaction_reagent_itemst>(rr);
                if (rri && rri->item_type == item_type::BOULDER && rri->mat_type == 0)
                {
                    mat = rri->mat_index;
                    found = true;
                    break;
                }
            }
            if (!found)
                continue;

            found = false;
            for (df::reaction_product *rp : r->products)
            {
                df::reaction_product_itemst *rpi = virtual_cast<df::reaction_product_itemst>(rp);
                if (rpi && rpi->item_type == item_type::BAR && MaterialInfo(rpi).material->id == "COAL")
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                continue;

            // XXX check input size vs output size ?
            raw_coke[mat] = r->code;
            raw_coke_inv[r->code] = mat;
        }
    }
    return raw_coke.count(mi) ? raw_coke.at(mi) : "";
}

std::string Stocks::is_raw_coke(df::item *i)
{
    if (virtual_cast<df::item_boulderst>(i) && i->getMaterial() == 0)
    {
        return is_raw_coke(i->getMaterialIndex());
    }
    return "";
}

bool Stocks::is_gypsum(int32_t mi)
{
    for (std::string *c : world->raws.inorganics[mi]->material.reaction_class)
    {
        if (*c == "GYPSUM")
        {
            return true;
        }
    }
    return false;
}

bool Stocks::is_gypsum(df::item *i)
{
    if (virtual_cast<df::item_boulderst>(i) && i->getMaterial() == 0)
    {
        return is_gypsum(i->getMaterialIndex());
    }
    return false;
}

// determine if we may be able to generate metal bars for this metal
// may queue manager_jobs to do so
// recursive (eg steel need pig_iron)
// return the potential number of bars available (in dimensions, eg 1 bar => 150)
int32_t Stocks::may_forge_bars(color_ostream & out, int32_t mat_index, int32_t div)
{
    // simple metal ore
    cache_hash<int32_t, bool> moc([mat_index](int32_t mi) -> bool
            {
                for (int32_t i : world->raws.inorganics[mi]->metal_ore.mat_index)
                {
                    if (i == mat_index)
                    {
                        return true;
                    }
                }
                return false;
            });

    int32_t can_melt = 0;
    for (df::item *i : world->items.other[items_other_id::BOULDER])
    {
        if (is_metal_ore(i) && moc[i->getMaterialIndex()] && is_item_free(i))
        {
            can_melt++;
        }
    }

    if (can_melt < WatchStock.at("metal_ore") && ai->plan->past_initial_phase)
    {
        for (auto k : ai->plan->map_veins)
        {
            if (moc[k.first])
            {
                can_melt += ai->plan->dig_vein(out, k.first);
            }
        }
    }

    if (can_melt > WatchStock.at("metal_ore"))
    {
        return 4 * 150 * (can_melt - WatchStock.at("metal_ore"));
    }

    // "make <mi> bars" customreaction
    for (df::reaction *r : world->raws.reactions)
    {
        // XXX choose best reaction from all reactions
        int32_t prod_mult = -1;
        for (df::reaction_product *rp : r->products)
        {
            df::reaction_product_itemst *rpi = virtual_cast<df::reaction_product_itemst>(rp);
            if (rpi && rpi->item_type == item_type::BAR && rpi->mat_type == 0 && rpi->mat_index == mat_index)
            {
                prod_mult = rpi->product_dimension;
                break;
            }
        }
        if (prod_mult == -1)
            continue;

        bool all = true;
        int32_t can_reaction = 30;
        bool future = false;
        for (df::reaction_reagent *rr : r->reagents)
        {
            // XXX may queue forge reagents[1] even if we dont handle reagents[2]
            df::reaction_reagent_itemst *rri = virtual_cast<df::reaction_reagent_itemst>(rr);
            if (!rri || (rri->item_type != item_type::BAR && rri->item_type != item_type::BOULDER))
            {
                all = false;
                break;
            }
            int32_t has = 0;
            df::items_other_id oidx;
            if (!find_enum_item(&oidx, ENUM_KEY_STR(item_type, rri->item_type)))
            {
                ai->debug(out, "[ERROR] missing items_other_id::" + ENUM_KEY_STR(item_type, rri->item_type));
                all = false;
                break;
            }
            for (df::item *i : world->items.other[oidx])
            {
                if (rri->mat_type != -1 && i->getMaterial() != rri->mat_type)
                    continue;
                if (rri->mat_index != -1 && i->getMaterialIndex() != rri->mat_index)
                    continue;
                if (!is_item_free(i))
                    continue;
                if (!rri->reaction_class.empty())
                {
                    MaterialInfo mi(i);
                    bool found = false;
                    for (std::string *c : mi.material->reaction_class)
                    {
                        if (*c == rri->reaction_class)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        continue;
                }
                if (rri->metal_ore != -1 && i->getMaterial() == 0)
                {
                    bool found = false;
                    for (int32_t mi : world->raws.inorganics[i->getMaterialIndex()]->metal_ore.mat_index)
                    {
                        if (mi == rri->metal_ore)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        continue;
                }
                if (rri->item_type == item_type::BAR)
                {
                    has += virtual_cast<df::item_barst>(i)->dimension;
                }
                else
                {
                    has++;
                }
            }
            if (has <= 0 && rri->item_type == item_type::BOULDER && rri->mat_type == 0 && rri->mat_index != -1 && ai->plan->past_initial_phase)
            {
                has += ai->plan->dig_vein(out, rri->mat_index);
                if (has > 0)
                    future = true;
            }
            has /= rri->quantity;

            if (has <= 0 && rri->item_type == item_type::BAR && rri->mat_type == 0 && rri->mat_index != -1)
            {
                future = true;
                // 'div' tries to ensure that eg making pig iron wont consume all available iron
                // and leave some to make steel
                has = may_forge_bars(out, rri->mat_index, div + 1);
                if (has == -1)
                {
                    all = false;
                    break;
                }
            }

            if (can_reaction > has / div)
            {
                can_reaction = has / div;
            }
        }
        if (all)
        {
            if (can_reaction <= 0)
            {
                continue;
            }

            if (!future)
            {
                bool found = false;
                for (df::manager_order *mo : world->manager_orders)
                {
                    if (mo->job_type == job_type::CustomReaction && mo->reaction_name == r->code)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    ai->debug(out, stl_sprintf("stocks: queue %d %s", can_reaction, r->code.c_str()));
                    df::manager_order *mo = df::allocate<df::manager_order>();
                    mo->job_type = job_type::CustomReaction;
                    mo->item_type = item_type::NONE;
                    mo->item_subtype = -1;
                    mo->reaction_name = r->code;
                    mo->mat_type = -1;
                    mo->mat_index = -1;
                    mo->amount_left = can_reaction;
                    mo->amount_total = can_reaction;
                    world->manager_orders.push_back(mo);
                }
            }
            return prod_mult * can_reaction;
        }
    }
    return -1;
}

void Stocks::init_manager_subtype()
{
    manager_subtype.clear();
    for (df::itemdef_toolst *def : world->raws.itemdefs.tools)
    {
        for (df::tool_uses u : def->tool_use)
        {
            switch (u)
            {
                case tool_uses::HEAVY_OBJECT_HAULING:
                    manager_subtype["MakeWoodenWheelbarrow"] = def->subtype;
                    break;
                case tool_uses::TRACK_CART:
                    manager_subtype["MakeWoodenMinecart"] = def->subtype;
                    break;
                case tool_uses::NEST_BOX:
                    manager_subtype["MakeRockNestbox"] = def->subtype;
                    break;
                case tool_uses::HIVE:
                    manager_subtype["MakeRockHive"] = def->subtype;
                    break;
                case tool_uses::LIQUID_CONTAINER:
                    manager_subtype["MakeRockJug"] = def->subtype;
                    break;
                case tool_uses::STAND_AND_WORK_ABOVE:
                    manager_subtype["MakeWoodenStepladder"] = def->subtype;
                    break;
                default:
                    break;
            }
        }
    }
    for (df::itemdef_weaponst *def : world->raws.itemdefs.weapons)
    {
        if (def->id == "ITEM_WEAPON_AXE_TRAINING")
        {
            manager_subtype["MakeTrainingAxe"] = def->subtype;
        }
        else if (def->id == "ITEM_WEAPON_SWORD_SHORT_TRAINING")
        {
            manager_subtype["MakeTrainingShortSword"] = def->subtype;
        }
        else if (def->id == "ITEM_WEAPON_SPEAR_TRAINING")
        {
            manager_subtype["MakeTrainingSpear"] = def->subtype;
        }
        else if (def->id == "ITEM_WEAPON_CROSSBOW")
        {
            manager_subtype["MakeBoneCrossbow"] = def->subtype;
        }
    }
    for (df::itemdef_ammost *def : world->raws.itemdefs.ammo)
    {
        if (def->id == "ITEM_AMMO_BOLTS")
        {
            manager_subtype["MakeBoneBolt"] = def->subtype;
        }
    }
    for (df::itemdef_trapcompst *def : world->raws.itemdefs.trapcomps)
    {
        if (def->id == "ITEM_TRAPCOMP_ENORMOUSCORKSCREW")
        {
            manager_subtype["MakeGiantCorkscrew"] = def->subtype;
        }
    }
}

std::vector<df::manager_order *> Stocks::find_manager_orders(std::string order)
{
    df::job_type _order;
    if (ManagerRealOrder.count(order))
        _order = ManagerRealOrder.at(order);
    else if (!find_enum_item(&_order, order))
        return std::vector<df::manager_order *>();

    df::job_material_category matcat;
    if (ManagerMatCategory.count(order))
        parseJobMaterialCategory(&matcat, ManagerMatCategory.at(order));
    int32_t type = ManagerType.count(order) ? ManagerType.at(order) : ManagerMatCategory.count(order) ? -1 : 0;
    int32_t subtype = manager_subtype.count(order) ? manager_subtype.at(order) : -1;
    std::string custom = ManagerCustom.count(order) ? ManagerCustom.at(order) : "";

    std::vector<df::manager_order *> orders;
    for (df::manager_order *_o : world->manager_orders)
    {
        if (_o->job_type == _order &&
                _o->mat_type == type &&
                _o->material_category.whole == matcat.whole &&
                _o->item_subtype == subtype &&
                _o->reaction_name == custom)
        {
            orders.push_back(_o);
        }
    }
    return orders;
}

// return the number of current manager orders that share the same material (leather, cloth)
// ignore inorganics, ignore order
int32_t Stocks::count_manager_orders_matcat(std::string matcat, df::job_type order)
{
    df::job_material_category cat;
    if (!parseJobMaterialCategory(&cat, matcat))
        return 0;

    int32_t cnt = 0;
    for (df::manager_order *_o : world->manager_orders)
    {
        if (_o->material_category.whole == cat.whole && _o->job_type != order)
        {
            cnt += _o->amount_total;
        }
    }
    return cnt;
}

void Stocks::add_manager_order(color_ostream & out, std::string order, int32_t amount, int32_t maxmerge)
{
    ai->debug(out, stl_sprintf("add_manager %s %d", order.c_str(), amount));
    df::job_type _order;
    if (ManagerRealOrder.count(order))
    {
        _order = ManagerRealOrder.at(order);
    }
    else if (!find_enum_item(&_order, order))
    {
        ai->debug(out, "[ERROR] no such manager order: " + order);
        return;
    }

    df::job_material_category matcat;
    if (ManagerMatCategory.count(order))
        parseJobMaterialCategory(&matcat, ManagerMatCategory.at(order));
    int32_t type = ManagerType.count(order) ? ManagerType.at(order) : ManagerMatCategory.count(order) ? -1 : 0;
    int32_t subtype = manager_subtype.count(order) ? manager_subtype.at(order) : -1;
    std::string custom = ManagerCustom.count(order) ? ManagerCustom.at(order) : "";

    df::manager_order *o = nullptr;
    for (df::manager_order *_o : find_manager_orders(order))
    {
        if (_o->amount_total + amount <= maxmerge)
        {
            o = _o;
            break;
        }
    }
    if (!o)
    {
        // try to merge with last manager_order, upgrading maxmerge to 30
        o = world->manager_orders.empty() ? nullptr : world->manager_orders.back();
        if (o && o->job_type == _order &&
                o->amount_total + amount < 30 &&
                o->mat_type == type &&
                o->material_category.whole == matcat.whole &&
                o->item_subtype == subtype &&
                o->reaction_name == custom &&
                o->amount_total + amount < 30)
        {
            o->amount_total += amount;
            o->amount_left += amount;
        }
        else
        {
            o = df::allocate<df::manager_order>();
            o->job_type = _order;
            o->item_type = item_type::NONE;
            o->item_subtype = subtype;
            o->mat_type = type;
            o->mat_index = -1;
            o->amount_left = amount;
            o->amount_total = amount;
            o->material_category.whole = matcat.whole;
            o->reaction_name = custom;
            if (_order == job_type::ExtractMetalStrands)
            {
                MaterialInfo candy;
                candy.findInorganic("RAW_ADAMANTINE");
                o->mat_index = candy.index;
            }
            world->manager_orders.push_back(o);
        }
    }
    else
    {
        o->amount_total += amount;
        o->amount_left += amount;
    }
}

std::string Stocks::furniture_order(std::string k)
{
    const static std::map<std::string, std::string> diff
    {
        {"chair", "ConstructThrone"},
        {"traction_bench", "ConstructTractionBench"},
        {"weaponrack", "ConstructWeaponRack"},
        {"armorstand", "ConstructArmorStand"},
        {"bucket", "MakeBucket"},
        {"barrel", "MakeBarrel"},
        {"bin", "ConstructBin"},
        {"crutch", "ConstructCrutch"},
        {"splint", "ConstructSplint"},
        {"bag", "MakeBag"},
        {"block", "ConstructBlocks"},
        {"mechanism", "ConstructMechanisms"},
        {"trap", "ConstructMechanisms"},
        {"cage", "MakeCage"},
        {"soap", "MakeSoap"},
        {"rope", "MakeRope"},
        {"lye", "MakeLye"},
        {"ash", "MakeAsh"},
        {"plasterpowder", "MakePlasterPowder"},
        {"wheelbarrow", "MakeWoodenWheelbarrow"},
        {"minecart", "MakeWoodenMinecart"},
        {"nestbox", "MakeRockNestbox"},
        {"hive", "MakeRockHive"},
        {"jug", "MakeRockJug"},
        {"quiver", "MakeQuiver"},
        {"flask", "MakeFlask"},
        {"backpack", "MakeBackpack"},
        {"giant_corkscrew", "MakeGiantCorkscrew"},
        {"pipe_section", "MakePipeSection"},
        {"coal", "MakeCharcoal"},
        {"stepladder", "MakeWoodenStepladder"},
        {"goblet", "MakeGoblet"},
    };
    if (diff.count(k))
        return diff.at(k);

    k[0] += 'A' - 'a';
    return "Construct" + k;
}

std::function<bool(df::item *)> Stocks::furniture_find(std::string k)
{
    if (k == "chest")
    {
        return [](df::item *item) -> bool
        {
            df::item_boxst *i = virtual_cast<df::item_boxst>(item);
            return i && i->mat_type == 0;
        };
    }
    if (k == "hive" || k == "nestbox" || k == "stepladder")
    {
        if (!manager_subtype.count(furniture_order(k)))
            return [](df::item *i) -> bool { return false; };
        int32_t subtype = manager_subtype.at(furniture_order(k));
        return [subtype](df::item *item) -> bool
        {
            df::item_toolst *i = virtual_cast<df::item_toolst>(item);
            return i && i->subtype->subtype == subtype;
        };
    }
    if (k == "trap")
    {
        return [](df::item *i) -> bool
        {
            return virtual_cast<df::item_trappartsst>(i);
        };
    }

    virtual_identity *sym = virtual_identity::find("item_" + k + "st");
    return [sym](df::item *i) -> bool
    {
        return sym->is_instance(i);
    };
}

// find one item of this type (:bed, etc)
df::item *Stocks::find_furniture_item(std::string itm)
{
    std::function<bool(df::item *)> find = furniture_find(itm);
    std::string order = furniture_order(itm);
    df::items_other_id oidx = items_other_id::IN_PLAY;
    df::job_type job;
    if ((!find_enum_item(&job, order) || !find_enum_item(&oidx, ENUM_KEY_STR(item_type, ENUM_ATTR(job_type, item, job)))) && ManagerRealOrder.count(order))
    {
        order = ManagerRealOrder.at(order);
        if (find_enum_item(&job, order))
        {
            find_enum_item(&oidx, ENUM_KEY_STR(item_type, ENUM_ATTR(job_type, item, job)));
        }
    }
    for (df::item *i : world->items.other[oidx])
    {
        if (find(i) && is_item_free(i))
        {
            return i;
        }
    }
    return nullptr;
}

// return nr of free items of this type
int32_t Stocks::find_furniture_itemcount(std::string itm)
{
    std::function<bool(df::item *)> find = furniture_find(itm);
    std::string order = furniture_order(itm);
    df::items_other_id oidx = items_other_id::IN_PLAY;
    df::job_type job;
    if ((!find_enum_item(&job, order) || !find_enum_item(&oidx, ENUM_KEY_STR(item_type, ENUM_ATTR(job_type, item, job)))) && ManagerRealOrder.count(order))
    {
        order = ManagerRealOrder.at(order);
        if (find_enum_item(&job, order))
        {
            find_enum_item(&oidx, ENUM_KEY_STR(item_type, ENUM_ATTR(job_type, item, job)));
        }
    }
    int32_t n = 0;
    for (df::item *i : world->items.other[oidx])
    {
        if (find(i) && is_item_free(i))
        {
            n++;
        }
    }
    return n;
}

bool Stocks::is_cutting_trees()
{
    if (!last_cutpos.isValid())
    {
        return false;
    }
    if (Maps::getTileDesignation(last_cutpos)->bits.dig == tile_dig_designation::Default)
    {
        return true;
    }

    for (df::coord t : tree_list())
    {
        if (Maps::getTileDesignation(t)->bits.dig == tile_dig_designation::Default)
        {
            last_cutpos = t;
            return true;
        }
    }
    last_cutpos.clear();
    return false;
}

void Stocks::farmplot(color_ostream & out, room *r, bool initial)
{
    df::building_farmplotst *bld = virtual_cast<df::building_farmplotst>(r->dfbuilding());
    if (!bld)
        return;

    bool subterranean = Maps::getTileDesignation(r->pos())->bits.subterranean;

    std::vector<int32_t> may;
    for (int32_t i = 0; i < int32_t(world->raws.plants.all.size()); i++)
    {
        df::plant_raw *p = world->raws.plants.all[i];
        if (p->flags.is_set(plant_raw_flags::BIOME_SUBTERRANEAN_WATER) != subterranean)
            continue;
        if (p->flags.is_set(plant_raw_flags::TREE) || !p->flags.is_set(plant_raw_flags::SEED))
            continue;
        may.push_back(i);
    }

    // XXX 1st plot = the one with a door
    bool isfirst = !r->layout.empty();
    for (int8_t season = 0; season < 4; season++)
    {
        std::vector<int32_t> pids;
        if (r->subtype == "food")
        {
            for (int32_t i : may)
            {
                df::plant_raw *p = world->raws.plants.all[i];

                // season numbers are also the 1st 4 flags
                if (!p->flags.is_set(df::plant_raw_flags(season)))
                {
                    continue;
                }

                MaterialInfo pm(p->material_defs.type_basic_mat, p->material_defs.idx_basic_mat);
                if (isfirst)
                {
                    if (pm.material->flags.is_set(material_flags::EDIBLE_RAW) && p->flags.is_set(plant_raw_flags::DRINK))
                    {
                        pids.push_back(i);
                    }
                    continue;
                }
                if (pm.material->flags.is_set(material_flags::EDIBLE_RAW) || pm.material->flags.is_set(material_flags::EDIBLE_COOKED) || p->flags.is_set(plant_raw_flags::DRINK))
                {
                    pids.push_back(i);
                    continue;
                }
                if (p->flags.is_set(plant_raw_flags::MILL))
                {
                    MaterialInfo mm(p->material_defs.type_mill, p->material_defs.idx_mill);
                    if (mm.material->flags.is_set(material_flags::EDIBLE_RAW) || mm.material->flags.is_set(material_flags::EDIBLE_COOKED))
                    {
                        pids.push_back(i);
                        continue;
                    }
                }
                for (size_t bi = 0; bi < pm.material->reaction_product.id.size(); bi++)
                {
                    if (*pm.material->reaction_product.id[bi] == "BAG_ITEM")
                    {
                        MaterialInfo bm(pm.material->reaction_product.material.mat_type[bi], pm.material->reaction_product.material.mat_index[bi]);
                        if (bm.material->flags.is_set(material_flags::EDIBLE_RAW) || bm.material->flags.is_set(material_flags::EDIBLE_COOKED))
                        {
                            pids.push_back(i);
                            break;
                        }
                    }
                }
            }
        }
        else if (r->subtype == "cloth")
        {
            if (isfirst)
            {
                for (int32_t i : may)
                {
                    df::plant_raw *p = world->raws.plants.all[i];
                    if (p->flags.is_set(df::plant_raw_flags(season)) && thread_plants.count(i))
                    {
                        pids.push_back(i);
                    }
                }
            }
            // only grow dyes the first field if there is no cloth crop available
            if (pids.empty())
            {
                for (int32_t i : may)
                {
                    df::plant_raw *p = world->raws.plants.all[i];
                    if (p->flags.is_set(df::plant_raw_flags(season)) && (thread_plants.count(i) || dye_plants.count(i)))
                    {
                        pids.push_back(i);
                    }
                }
            }
        }
        std::sort(pids.begin(), pids.end(), [this, season](int32_t a, int32_t b) -> bool
                {
                    if (seeds.count(a) && !seeds.count(b))
                        return true;
                    if (!seeds.count(a) && seeds.count(b))
                        return false;
                    int32_t ascore = plants.count(a) ? plants.at(a) : 0;
                    int32_t bscore = plants.count(b) ? plants.at(b) : 0;
                    if (seeds.count(a))
                    {
                        ascore -= seeds.at(a);
                        bscore -= seeds.at(b);
                    }
                    ascore += farmplots.count(std::make_pair(season, a)) ? 3 * 3 * 2 * farmplots.at(std::make_pair(season, a)) : 0;
                    bscore += farmplots.count(std::make_pair(season, b)) ? 3 * 3 * 2 * farmplots.at(std::make_pair(season, b)) : 0;
                    return ascore < bscore;
                });

        if (pids.empty())
        {
            if (!isfirst && complained_about_no_plants.insert(std::make_tuple(r->subtype, subterranean, season)).second)
            {
                ai->debug(out, stl_sprintf("[ERROR] stocks: no legal plants for %s farm plot (%s) for season %d", r->subtype.c_str(), subterranean ? "underground" : "outdoor", season));
            }
        }
        else
        {
            if (!initial)
            {
                farmplots[std::make_pair(season, bld->plant_id[season])]--;
                farmplots[std::make_pair(season, pids[0])]++;
            }
            bld->plant_id[season] = pids[0];
        }
    }
}

void Stocks::queue_slab(color_ostream & out, int32_t histfig_id)
{
    for (df::manager_order *mo : world->manager_orders)
    {
        if (mo->job_type == job_type::EngraveSlab && mo->hist_figure_id == histfig_id)
        {
            return;
        }
    }
    for (df::item *item : world->items.other[items_other_id::SLAB])
    {
        df::item_slabst *sl = virtual_cast<df::item_slabst>(item);
        if (sl->engraving_type == slab_engraving_type::Memorial && sl->topic == histfig_id)
        {
            return;
        }
    }
    df::manager_order *o = df::allocate<df::manager_order>();
    o->job_type = job_type::EngraveSlab;
    o->item_type = item_type::NONE;
    o->item_subtype = -1;
    o->mat_type = 0;
    o->mat_index = -1;
    o->amount_left = 1;
    o->amount_total = 1;
    o->hist_figure_id = histfig_id;
    world->manager_orders.push_back(o);
    // XXX we need to build the slab somewhere to kill the ghost
}

bool Stocks::need_more(std::string type)
{
    int32_t want = Needed.count(type) ? num_needed(type) : WatchStock.count(type) ? WatchStock.at(type) : 10;
    if (NeededPerDwarf.count(type))
        want += NeededPerDwarf.at(type) * ai->pop->citizen.size() / 100 * 9;

    return (count.count(type) ? count.at(type) : 0) < want;
}

// vim: et:sw=4:ts=4
