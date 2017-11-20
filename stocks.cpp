#include "ai.h"
#include "stocks.h"
#include "plan.h"
#include "population.h"

#include "modules/Buildings.h"
#include "modules/Gui.h"
#include "modules/Maps.h"
#include "modules/Materials.h"
#include "modules/Units.h"

#include "df/building_coffinst.h"
#include "df/building_farmplotst.h"
#include "df/building_slabst.h"
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
#include "df/historical_figure.h"
#include "df/inorganic_raw.h"
#include "df/item.h"
#include "df/item_ammost.h"
#include "df/item_animaltrapst.h"
#include "df/item_armorst.h"
#include "df/item_armorstandst.h"
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
#include "df/item_globst.h"
#include "df/item_glovesst.h"
#include "df/item_helmst.h"
#include "df/item_pantsst.h"
#include "df/item_plant_growthst.h"
#include "df/item_plantst.h"
#include "df/item_seedsst.h"
#include "df/item_shieldst.h"
#include "df/item_shoesst.h"
#include "df/item_slabst.h"
#include "df/item_threadst.h"
#include "df/item_toolst.h"
#include "df/item_traction_benchst.h"
#include "df/item_trapcompst.h"
#include "df/item_trappartsst.h"
#include "df/item_weaponrackst.h"
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
#include "df/job.h"
#include "df/manager_order.h"
#include "df/manager_order_template.h"
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
#include "df/tile_occupancy.h"
#include "df/ui.h"
#include "df/unit.h"
#include "df/unit_inventory_item.h"
#include "df/vehicle.h"
#include "df/viewscreen_createquotast.h"
#include "df/viewscreen_overallstatusst.h"
#include "df/world.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

#define BEGIN_ENUM BEGIN_IMPLEMENT_ENUM
#define ENUM_ITEM IMPLEMENT_ENUM_ITEM
#define END_ENUM END_IMPLEMENT_ENUM
STOCKS_ENUMS
#undef BEGIN_ENUM
#undef ENUM_ITEM
#undef END_ENUM

const static struct Watch
{
    std::map<stock_item::item, int32_t> Needed;
    std::map<stock_item::item, int32_t> NeededPerDwarf; // per 100 dwarves, actually
    std::map<stock_item::item, int32_t> WatchStock;
    std::set<stock_item::item> AlsoCount;

    Watch()
    {
        Needed[stock_item::anvil] = 1;
        Needed[stock_item::armor_feet] = 2;
        Needed[stock_item::armor_hands] = 2;
        Needed[stock_item::armor_head] = 2;
        Needed[stock_item::armor_legs] = 2;
        Needed[stock_item::armor_shield] = 2;
        Needed[stock_item::armor_stand] = 1;
        Needed[stock_item::armor_torso] = 2;
        Needed[stock_item::ash] = 1;
        Needed[stock_item::axe] = 2;
        Needed[stock_item::backpack] = 2;
        Needed[stock_item::bag] = 3;
        Needed[stock_item::barrel] = 4;
        Needed[stock_item::bed] = 4;
        Needed[stock_item::bin] = 4;
        Needed[stock_item::block] = 6;
        Needed[stock_item::book_binding] = 5;
        Needed[stock_item::bookcase] = 1;
        Needed[stock_item::bucket] = 2;
        Needed[stock_item::cabinet] = 4;
        Needed[stock_item::cage] = 3;
        Needed[stock_item::cage_metal] = 1;
        Needed[stock_item::chair] = 3;
        Needed[stock_item::chest] = 4;
        Needed[stock_item::clothes_feet] = 2;
        Needed[stock_item::clothes_hands] = 2;
        Needed[stock_item::clothes_head] = 2;
        Needed[stock_item::clothes_legs] = 2;
        Needed[stock_item::clothes_torso] = 2;
        Needed[stock_item::coal] = 3;
        Needed[stock_item::coffin] = 2;
        Needed[stock_item::coffin_bld] = 3;
        Needed[stock_item::coffin_bld_pet] = 1;
        Needed[stock_item::crutch] = 1;
        Needed[stock_item::door] = 4;
        Needed[stock_item::drink] = 20;
        Needed[stock_item::dye] = 10;
        Needed[stock_item::dye_seeds] = 10;
        Needed[stock_item::flask] = 2;
        Needed[stock_item::floodgate] = 1;
        Needed[stock_item::giant_corkscrew] = 1;
        Needed[stock_item::goblet] = 10;
        Needed[stock_item::gypsum] = 1;
        Needed[stock_item::hatch_cover] = 2;
        Needed[stock_item::hive] = 1;
        Needed[stock_item::jug] = 1;
        Needed[stock_item::lye] = 1;
        Needed[stock_item::meal] = 20;
        Needed[stock_item::mechanism] = 4;
        Needed[stock_item::minecart] = 1;
        Needed[stock_item::nest_box] = 1;
        Needed[stock_item::paper] = 5;
        Needed[stock_item::pick] = 2;
        Needed[stock_item::pipe_section] = 1;
        Needed[stock_item::plaster_powder] = 1;
        Needed[stock_item::quern] = 3;
        Needed[stock_item::quire] = 5;
        Needed[stock_item::quiver] = 2;
        Needed[stock_item::raw_coke] = 1;
        Needed[stock_item::rock_pot] = 4;
        Needed[stock_item::rope] = 1;
        Needed[stock_item::slab] = 1;
        Needed[stock_item::slurry] = 5;
        Needed[stock_item::soap] = 1;
        Needed[stock_item::splint] = 1;
        Needed[stock_item::stepladder] = 2;
        Needed[stock_item::table] = 3;
        Needed[stock_item::thread_seeds] = 10;
        Needed[stock_item::toy] = 2;
        Needed[stock_item::traction_bench] = 1;
        Needed[stock_item::weapon] = 2;
        Needed[stock_item::weapon_rack] = 1;
        Needed[stock_item::wheelbarrow] = 1;
        Needed[stock_item::wood] = 16;

        NeededPerDwarf[stock_item::armor_feet] = 3;
        NeededPerDwarf[stock_item::armor_hands] = 3;
        NeededPerDwarf[stock_item::armor_head] = 3;
        NeededPerDwarf[stock_item::armor_legs] = 3;
        NeededPerDwarf[stock_item::armor_shield] = 3;
        NeededPerDwarf[stock_item::armor_torso] = 3;
        NeededPerDwarf[stock_item::cloth] = 20;
        NeededPerDwarf[stock_item::clothes_feet] = 20;
        NeededPerDwarf[stock_item::clothes_hands] = 20;
        NeededPerDwarf[stock_item::clothes_head] = 20;
        NeededPerDwarf[stock_item::clothes_legs] = 20;
        NeededPerDwarf[stock_item::clothes_torso] = 20;
        NeededPerDwarf[stock_item::drink] = 200;
        NeededPerDwarf[stock_item::meal] = 100;
        NeededPerDwarf[stock_item::slab] = 10;
        NeededPerDwarf[stock_item::soap] = 20;
        NeededPerDwarf[stock_item::toy] = 5;
        NeededPerDwarf[stock_item::weapon] = 5;

        WatchStock[stock_item::bag_plant] = 4;
        WatchStock[stock_item::bone] = 8;
        WatchStock[stock_item::clay] = 1;
        WatchStock[stock_item::cloth_nodye] = 10;
        WatchStock[stock_item::drink_fruit] = 5;
        WatchStock[stock_item::drink_plant] = 5;
        WatchStock[stock_item::food_ingredients] = 2;
        WatchStock[stock_item::honey] = 1;
        WatchStock[stock_item::honeycomb] = 1;
        WatchStock[stock_item::metal_ore] = 6;
        WatchStock[stock_item::milk] = 1;
        WatchStock[stock_item::mill_plant] = 4;
        WatchStock[stock_item::raw_adamantine] = 2;
        WatchStock[stock_item::raw_coke] = 2;
        WatchStock[stock_item::raw_fish] = 1;
        WatchStock[stock_item::rough_gem] = 6;
        WatchStock[stock_item::shell] = 1;
        WatchStock[stock_item::skull] = 2;
        WatchStock[stock_item::tallow] = 1;
        WatchStock[stock_item::thread_plant] = 10;
        WatchStock[stock_item::wool] = 1;
        WatchStock[stock_item::written_on_quire] = 1;

        AlsoCount.insert(stock_item::bone_bolts);
        AlsoCount.insert(stock_item::cloth);
        AlsoCount.insert(stock_item::crossbow);
        AlsoCount.insert(stock_item::dead_dwarf);
        AlsoCount.insert(stock_item::dye_plant);
        AlsoCount.insert(stock_item::leather);
        AlsoCount.insert(stock_item::slurry_plant);
        AlsoCount.insert(stock_item::statue);
        AlsoCount.insert(stock_item::stone);
        AlsoCount.insert(stock_item::thread);
    }
} Watch;

const static struct Manager
{
    std::map<std::string, df::job_type> RealOrder;
    std::map<std::string, df::job_material_category> MatCategory;
    // no MatCategory => mat_type = 0 (ie generic rock), unless specified here
    std::map<std::string, int32_t> Type;
    std::map<std::string, std::string> Custom;
    std::map<uint32_t, stock_item::item> MatCategoryItem;

    Manager()
    {
        RealOrder["BrewDrinkPlant"] = job_type::CustomReaction;
        RealOrder["BrewDrinkFruit"] = job_type::CustomReaction;
        RealOrder["BrewMead"] = job_type::CustomReaction;
        RealOrder["ProcessPlantsBag"] = job_type::CustomReaction;
        RealOrder["MakeSoap"] = job_type::CustomReaction;
        RealOrder["MakePlasterPowder"] = job_type::CustomReaction;
        RealOrder["PressHoneycomb"] = job_type::CustomReaction;
        RealOrder["MakeBag"] = job_type::ConstructChest;
        RealOrder["MakeRope"] = job_type::MakeChain;
        RealOrder["MakeWoodenWheelbarrow"] = job_type::MakeTool;
        RealOrder["MakeWoodenMinecart"] = job_type::MakeTool;
        RealOrder["MakeRockNestBox"] = job_type::MakeTool;
        RealOrder["MakeRockHive"] = job_type::MakeTool;
        RealOrder["MakeRockJug"] = job_type::MakeTool;
        RealOrder["MakeBoneBolt"] = job_type::MakeAmmo;
        RealOrder["MakeBoneCrossbow"] = job_type::MakeWeapon;
        RealOrder["MakeTrainingAxe"] = job_type::MakeWeapon;
        RealOrder["MakeTrainingShortSword"] = job_type::MakeWeapon;
        RealOrder["MakeTrainingSpear"] = job_type::MakeWeapon;
        RealOrder["MakeGiantCorkscrew"] = job_type::MakeTrapComponent;
        RealOrder["ConstructWoodenBlocks"] = job_type::ConstructBlocks;
        RealOrder["MakeWoodenStepladder"] = job_type::MakeTool;
        RealOrder["DecorateWithShell"] = job_type::DecorateWith;
        RealOrder["MakeClayStatue"] = job_type::CustomReaction;
        RealOrder["MakeRockBookcase"] = job_type::MakeTool;
        RealOrder["MakeSlurryFromPlant"] = job_type::CustomReaction;
        RealOrder["PressPlantPaper"] = job_type::CustomReaction;
        RealOrder["MakeQuire"] = job_type::CustomReaction;
        RealOrder["MakeRockPot"] = job_type::MakeTool;
        RealOrder["MakeBookBinding"] = job_type::MakeTool;
        RealOrder["BindBook"] = job_type::CustomReaction;

        MatCategory["MakeRope"].bits.cloth = 1;
        MatCategory["MakeBag"].bits.cloth = 1;
        MatCategory["ConstructBed"].bits.wood = 1;
        MatCategory["MakeBarrel"].bits.wood = 1;
        MatCategory["MakeBucket"].bits.wood = 1;
        MatCategory["ConstructBin"].bits.wood = 1;
        MatCategory["MakeWoodenWheelbarrow"].bits.wood = 1;
        MatCategory["MakeWoodenMinecart"].bits.wood = 1;
        MatCategory["MakeTrainingAxe"].bits.wood = 1;
        MatCategory["MakeTrainingShortSword"].bits.wood = 1;
        MatCategory["MakeTrainingSpear"].bits.wood = 1;
        MatCategory["ConstructCrutch"].bits.wood = 1;
        MatCategory["ConstructSplint"].bits.wood = 1;
        MatCategory["MakeCage"].bits.wood = 1;
        MatCategory["MakeGiantCorkscrew"].bits.wood = 1;
        MatCategory["MakePipeSection"].bits.wood = 1;
        MatCategory["ConstructWoodenBlocks"].bits.wood = 1;
        MatCategory["MakeBoneBolt"].bits.bone = 1;
        MatCategory["MakeBoneCrossbow"].bits.bone = 1;
        MatCategory["MakeQuiver"].bits.leather = 1;
        MatCategory["MakeFlask"].bits.leather = 1;
        MatCategory["MakeBackpack"].bits.leather = 1;
        MatCategory["MakeWoodenStepladder"].bits.wood = 1;
        MatCategory["DecorateWithShell"].bits.shell = 1;
        MatCategory["SpinThread"].bits.strand = 1;

        Type["ProcessPlants"] = -1;
        Type["ProcessPlantsBag"] = -1;
        Type["MillPlants"] = -1;
        Type["BrewDrinkPlant"] = -1;
        Type["ConstructTractionBench"] = -1;
        Type["MakeSoap"] = -1;
        Type["MakeLye"] = -1;
        Type["MakeAsh"] = -1;
        Type["MakeTotem"] = -1;
        Type["MakeCharcoal"] = -1;
        Type["MakePlasterPowder"] = -1;
        Type["PrepareMeal"] = 4;
        Type["DyeCloth"] = -1;
        Type["MilkCreature"] = -1;
        Type["PressHoneycomb"] = -1;
        Type["BrewDrinkFruit"] = -1;
        Type["BrewMead"] = -1;
        Type["MakeCheese"] = -1;
        Type["PrepareRawFish"] = -1;
        Type["MakeClayStatue"] = -1;
        Type["MakeSlurryFromPlant"] = -1;
        Type["PressPlantPaper"] = -1;
        Type["MakeQuire"] = -1;
        Type["BindBook"] = -1;

        Custom["ProcessPlantsBag"] = "PROCESS_PLANT_TO_BAG";
        Custom["BrewDrinkPlant"] = "BREW_DRINK_FROM_PLANT";
        Custom["BrewDrinkFruit"] = "BREW_DRINK_FROM_PLANT_GROWTH";
        Custom["BrewMead"] = "MAKE_MEAD";
        Custom["MakeSoap"] = "MAKE_SOAP_FROM_TALLOW";
        Custom["MakePlasterPowder"] = "MAKE_PLASTER_POWDER";
        Custom["PressHoneycomb"] = "PRESS_HONEYCOMB";
        Custom["MakeClayStatue"] = "MAKE_CLAY_STATUE";
        Custom["MakeSlurryFromPlant"] = "MAKE_SLURRY_FROM_PLANT";
        Custom["PressPlantPaper"] = "PRESS_PLANT_PAPER";
        Custom["MakeQuire"] = "MAKE_QUIRE";
        Custom["BindBook"] = "BIND_BOOK";

#define MAT_CAT(item) \
        { \
            df::job_material_category cat; \
            cat.bits.item = 1; \
            MatCategoryItem[cat.whole] = stock_item::item; \
        }

        MAT_CAT(wood);
        MAT_CAT(cloth);
        MAT_CAT(leather);
        MAT_CAT(bone);
        MAT_CAT(shell);
#undef MAT_CAT
    }
} Manager;

Stocks::Stocks(AI *ai) :
    ai(ai),
    count(),
    ingots(),
    onupdate_handle(nullptr),
    updating(),
    updating_count(),
    lastupdating(0),
    farmplots(),
    seeds(),
    plants(),
    last_unforbidall_year(*cur_year),
    last_managerstall(*cur_year_tick / 28 / 1200),
    last_managerorder(job_type::NONE),
    updating_seeds(false),
    updating_plants(false),
    updating_corpses(false),
    updating_slabs(false),
    updating_ingots(false),
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
    last_warn_food_year(-1),
    drink_plants(),
    drink_fruits(),
    thread_plants(),
    mill_plants(),
    bag_plants(),
    dye_plants(),
    slurry_plants(),
    grow_plants(),
    milk_creatures(),
    clay_stones(),
    raw_coke(),
    raw_coke_inv(),
    metal_pref(),
    simple_metal_ores(),
    complained_about_no_plants()
{
    last_cutpos.clear();
    events.onstatechange_register("init_manager_subtype", [this](color_ostream &, state_change_event st)
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
    update_simple_metal_ores(out);
    ui->stockpile.reserved_barrels = 5;
    return CR_OK;
}

command_result Stocks::onupdate_register(color_ostream &)
{
    reset();
    onupdate_handle = events.onupdate_register("df-ai stocks", 4800, 30, [this](color_ostream & out) { update(out); });
    return CR_OK;
}

command_result Stocks::onupdate_unregister(color_ostream &)
{
    events.onupdate_unregister(onupdate_handle);
    return CR_OK;
}

std::string Stocks::status()
{
    std::ostringstream s;

    bool first = true;
    s << "need: ";
    for (auto it = Watch.Needed.begin(); it != Watch.Needed.end(); it++)
    {
        int32_t want = num_needed(it->first);
        int32_t have = count[it->first];

        if (have >= want)
            continue;

        if (first)
            first = false;
        else
            s << ", ";

        s << it->first;
    }

    first = true;
    s << "; use: ";
    for (auto it = Watch.WatchStock.begin(); it != Watch.WatchStock.end(); it++)
    {
        int32_t want = it->second;
        int32_t have = count[it->first];
        if (have <= want)
            continue;

        if (first)
            first = false;
        else
            s << ", ";

        s << it->first;
    }

    return s.str();
}

void Stocks::report(std::ostream & out, bool html)
{
    if (html)
    {
        out << "<h2 id=\"Stocks_Need\">Need</h2><ul>";
    }
    else
    {
        out << "## Need\n";
    }
    for (auto n : Watch.Needed)
    {
        if (html)
        {
            out << "<li><b>" << n.first << ":</b> " << count[n.first] << " / " << num_needed(n.first) << "</li>";
        }
        else
        {
            out << "- " << n.first << ": " << count[n.first] << " / " << num_needed(n.first) << "\n";
        }
    }

    if (html)
    {
        out << "</ul><h2 id=\"Stocks_Watch\">Watch</h2><ul>";
    }
    else
    {
        out << "\n## Watch\n";
    }
    for (auto w : Watch.WatchStock)
    {
        if (html)
        {
            out << "<li><b>" << w.first << ":</b> " << count[w.first] << " / " << w.second << "</li>";
        }
        else
        {
            out << "- " << w.first << ": " << count[w.first] << " / " << w.second << "\n";
        }
    }

    if (html)
    {
        out << "</ul><h2 id=\"Stocks_Track\">Track</h2><ul>";
    }
    else
    {
        out << "\n## Track\n";
    }
    for (auto t : Watch.AlsoCount)
    {
        if (html)
        {
            out << "<li><b>" << t << ":</b> " << count[t] << "</li>";
        }
        else
        {
            out << "- " << t << ": " << count[t] << "\n";
        }
    }

    if (html)
    {
        out << "</ul><h2 id=\"Stocks_Ingots\">Ingots</h2><ul>";
    }
    else
    {
        out << "\n## Ingots\n";
    }
    for (auto t : ingots)
    {
        df::inorganic_raw *mat = df::inorganic_raw::find(t.first);

        if (html)
        {
            out << "<li><b>" << html_escape(mat->material.state_name[matter_state::Solid]) << ":</b> " << t.second << "</li>";
        }
        else
        {
            out << "- " << mat->material.state_name[matter_state::Solid] << ": " << t.second << "\n";
        }
    }

    if (html)
    {
        out << "</ul><h2 id=\"Stocks_Orders\">Orders</h2><ul>";
    }
    else
    {
        out << "\n## Orders\n";
    }
    for (auto mo : world->manager_orders)
    {
        if (html)
        {
            out << "<li>";
        }
        else
        {
            out << "- ";
        }
        out << stl_sprintf("% 4d /% 4d ", mo->amount_left, mo->amount_total);
        if (html)
        {
            out << html_escape(AI::describe_job(mo)) << "</li>";
        }
        else
        {
            out << AI::describe_job(mo) << "\n";
        }
    }
    if (html)
    {
        out << "</ul>";
    }
    else
    {
        out << "\n";
    }
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
        for (auto it = world->items.all.begin(); it != world->items.all.end(); it++)
        {
            (*it)->flags.bits.forbid = 0;
        }
    }

    // trim stalled manager orders once per month
    if (last_managerstall != *cur_year_tick / 28 / 1200)
    {
        last_managerstall = *cur_year_tick / 28 / 1200;
        if (!world->manager_orders.empty())
        {
            auto m = world->manager_orders.front();
            if (m->status.bits.validated)
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
    for (auto it = Watch.Needed.begin(); it != Watch.Needed.end(); it++)
    {
        updating.push_back(it->first);
    }
    for (auto it = Watch.WatchStock.begin(); it != Watch.WatchStock.end(); it++)
    {
        updating.push_back(it->first);
    }
    updating_count = updating;
    updating_count.insert(updating_count.end(), Watch.AlsoCount.begin(), Watch.AlsoCount.end());
    updating_seeds = true;
    updating_plants = true;
    updating_corpses = true;
    updating_slabs = true;
    updating_ingots = true;
    updating_farmplots.clear();

    ai->plan->find_room(room_type::farmplot, [this](room *r) -> bool
    {
        if (r->dfbuilding())
        {
            updating_farmplots.push_back(r);
        }
        return false; // search all farm plots
    });

    ai->debug(out, "updating stocks");

    if (ai->eventsJson.is_open())
    {
        // update wealth by opening the status screen
        AI::feed_key(interface_key::D_STATUS);
        if (auto view = strict_virtual_cast<df::viewscreen_overallstatusst>(Gui::getCurViewscreen(true)))
        {
            // only leave if we're on the status screen
            AI::feed_key(view, interface_key::LEAVESCREEN);
        }
        Json::Value payload(Json::objectValue);
        payload["total"] = Json::Int(ui->tasks.wealth.total);
        payload["weapons"] = Json::Int(ui->tasks.wealth.weapons);
        payload["armor"] = Json::Int(ui->tasks.wealth.armor);
        payload["furniture"] = Json::Int(ui->tasks.wealth.furniture);
        payload["other"] = Json::Int(ui->tasks.wealth.other);
        payload["architecture"] = Json::Int(ui->tasks.wealth.architecture);
        payload["displayed"] = Json::Int(ui->tasks.wealth.displayed);
        payload["held"] = Json::Int(ui->tasks.wealth.held);
        payload["imported"] = Json::Int(ui->tasks.wealth.imported);
        payload["exported"] = Json::Int(ui->tasks.wealth.exported);
        ai->event("wealth", payload);
    }

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
        if (updating_slabs)
        {
            update_slabs(out);
            return false;
        }
        if (updating_ingots)
        {
            update_ingots(out);
            return false;
        }
        if (!updating_count.empty())
        {
            stock_item::item key = updating_count.back();
            updating_count.pop_back();
            count[key] = count_stocks(out, key);
            return false;
        }
        if (!updating.empty())
        {
            stock_item::item key = updating.back();
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
        if (ai->eventsJson.is_open())
        {
            std::ostringstream stringify;
            Json::Value payload(Json::objectValue);
            for (auto it = Watch.Needed.begin(); it != Watch.Needed.end(); it++)
            {
                Json::Value needed(Json::arrayValue);
                needed.append(count.at(it->first));
                needed.append(num_needed(it->first));
                stringify.str(std::string());
                stringify.clear();
                stringify << it->first;
                payload[stringify.str()] = needed;
            }
            for (auto it = Watch.WatchStock.begin(); it != Watch.WatchStock.end(); it++)
            {
                Json::Value watch(Json::arrayValue);
                watch.append(count.at(it->first));
                watch.append(-it->second);
                stringify.str(std::string());
                stringify.clear();
                stringify << it->first;
                payload[stringify.str()] = watch;
            }
            for (auto it = Watch.AlsoCount.begin(); it != Watch.AlsoCount.end(); it++)
            {
                Json::Value also(Json::arrayValue);
                also.append(count.at(*it));
                also.append(0);
                stringify.str(std::string());
                stringify.clear();
                stringify << *it;
                payload[stringify.str()] = also;
            }
            ai->event("stocks update", payload);
        }
        // finished, dismiss callback
        return true;
    });
}

static bool has_reaction_product(df::material *m, const std::string & product)
{
    for (auto it = m->reaction_product.id.begin(); it != m->reaction_product.id.end(); it++)
    {
        if (**it == product)
        {
            return true;
        }
    }
    return false;
}

void Stocks::update_kitchen(color_ostream &)
{
    std::ofstream unopened; // unopened file output stream = no output
    color_ostream_wrapper discard(unopened);
    Core::getInstance().runCommand(discard, "ban-cooking booze honey tallow seeds");
}

void Stocks::update_plants(color_ostream &)
{
    drink_plants.clear();
    drink_fruits.clear();
    thread_plants.clear();
    mill_plants.clear();
    bag_plants.clear();
    dye_plants.clear();
    slurry_plants.clear();
    grow_plants.clear();
    milk_creatures.clear();
    clay_stones.clear();
    for (int32_t i = 0; i < int32_t(world->raws.plants.all.size()); i++)
    {
        df::plant_raw *p = world->raws.plants.all[i];
        for (int16_t j = 0; j < int16_t(p->material.size()); j++)
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
        if (has_reaction_product(basic.material, "PRESS_PAPER_MAT"))
        {
            slurry_plants[i] = basic.type;
        }
        if (p->flags.is_set(plant_raw_flags::SEED) && p->flags.is_set(plant_raw_flags::BIOME_SUBTERRANEAN_WATER))
        {
            grow_plants[i] = basic.type;
        }
    }
    for (int32_t i = 0; i < int32_t(world->raws.creatures.all.size()); i++)
    {
        df::creature_raw *c = world->raws.creatures.all[i];
        for (int16_t j = 0; j < int16_t(c->material.size()); j++)
        {
            df::material *m = c->material[j];
            if (has_reaction_product(m, "CHEESE_MAT"))
            {
                milk_creatures[i] = j + MaterialInfo::CREATURE_BASE;
                break;
            }
        }
    }
    for (int32_t i = 0; i < int32_t(world->raws.inorganics.size()); i++)
    {
        if (has_reaction_product(&world->raws.inorganics[i]->material, "FIRED_MAT"))
        {
            clay_stones.insert(i);
        }
    }
}

void Stocks::count_seeds(color_ostream &)
{
    farmplots.clear();
    ai->plan->find_room(room_type::farmplot, [this](room *r) -> bool
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
    for (auto it = world->items.other[items_other_id::SEEDS].begin(); it != world->items.other[items_other_id::SEEDS].end(); it++)
    {
        df::item_seedsst *s = virtual_cast<df::item_seedsst>(*it);
        if (s && is_item_free(s))
        {
            seeds[s->mat_index] += s->stack_size;
        }
    }
    updating_seeds = false;
}

void Stocks::count_plants(color_ostream &)
{
    plants.clear();
    for (auto it = world->items.other[items_other_id::PLANT].begin(); it != world->items.other[items_other_id::PLANT].end(); it++)
    {
        df::item_plantst *p = virtual_cast<df::item_plantst>(*it);
        if (p && is_item_free(p))
        {
            plants[p->mat_index] += p->stack_size;
        }
    }
    for (auto it = world->items.other[items_other_id::PLANT_GROWTH].begin(); it != world->items.other[items_other_id::PLANT_GROWTH].end(); it++)
    {
        df::item_plant_growthst *p = virtual_cast<df::item_plant_growthst>(*it);
        if (p && is_item_free(p))
        {
            plants[p->mat_index] += p->stack_size;
        }
    }
    updating_plants = false;
}

void Stocks::update_corpses(color_ostream & out)
{
    room *r = ai->plan->find_room(room_type::garbagedump);
    if (!r)
    {
        updating_corpses = false;
        return;
    }
    df::coord t = r->min - df::coord(0, 0, 1);

    for (auto it = world->items.other[items_other_id::ANY_CORPSE].begin(); it != world->items.other[items_other_id::ANY_CORPSE].end(); it++)
    {
        df::item *i = *it;
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
                ai->debug(out, "stocks: dump " + enum_item_key(i->getType()) + " of " + AI::describe_unit(u) + " (" + AI::describe_item(i) + ")");
            }
            // dump corpses that aren't in a stockpile, a grave, or the dump.
            i->flags.bits.dump = true;
        }
        else if (i->pos == t)
        {
            if (i->flags.bits.forbid && u)
            {
                ai->debug(out, "stocks: unforbid " + enum_item_key(i->getType()) + " of " + AI::describe_unit(u) + " (" + AI::describe_item(i) + ")");
            }
            // unforbid corpses in the dump so dwarves get buried before the next year.
            i->flags.bits.forbid = false;
        }
    }
    updating_corpses = false;
}

void Stocks::update_slabs(color_ostream & out)
{
    for (auto it = world->items.other[items_other_id::SLAB].begin(); it != world->items.other[items_other_id::SLAB].end(); it++)
    {
        df::item_slabst *i = virtual_cast<df::item_slabst>(*it);
        if (is_item_free(i) && i->engraving_type == slab_engraving_type::Memorial)
        {
            df::coord pos;
            pos.clear();

            ai->plan->find_room(room_type::cemetery, [&pos](room *r) -> bool
            {
                if (r->status == room_status::plan)
                    return false;
                for (int16_t x = r->min.x; x <= r->max.x; x++)
                {
                    for (int16_t y = r->min.y; y <= r->max.y; y++)
                    {
                        if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(x, y, r->min.z))) == tiletype_shape_basic::Floor && Maps::getTileOccupancy(x, y, r->min.z)->bits.building == tile_building_occ::None)
                        {
                            df::coord t(x, y, r->min.z);
                            bool any = false;
                            for (auto f : r->layout)
                            {
                                if (r->min + f->pos == t)
                                {
                                    any = true;
                                    break;
                                }
                            }
                            if (!any)
                            {
                                pos = t;
                                return true;
                            }
                        }
                    }
                }
                return false;
            });

            if (pos.isValid())
            {
                df::building_slabst *bld = virtual_cast<df::building_slabst>(Buildings::allocInstance(pos, building_type::Slab));
                Buildings::setSize(bld, df::coord(1, 1, 1));
                std::vector<df::item *> item;
                item.push_back(i);
                Buildings::constructWithItems(bld, item);
                ai->debug(out, "slabbing " + AI::describe_unit(df::unit::find(df::historical_figure::find(i->topic)->unit_id)) + ": " + i->description);
            }
        }
    }
    updating_slabs = false;
}

void Stocks::update_ingots(color_ostream &)
{
    // Set to 0 instead of clearing so metals stay in
    // the report after we use them all up.
    for (auto & i : ingots)
    {
        i.second = 0;
    }
    for (auto i : world->items.other[items_other_id::BAR])
    {
        if (i->getMaterial() == 0 && is_item_free(i))
        {
            ingots[i->getMaterialIndex()] += i->getStackSize();
        }
    }
    updating_ingots = false;
}

int32_t Stocks::num_needed(stock_item::item key)
{
    int32_t amount = Watch.Needed.at(key);
    if (Watch.NeededPerDwarf.count(key))
    {
        amount += int32_t(ai->pop->citizen.size()) * Watch.NeededPerDwarf.at(key) / 100;
    }

    if (key == stock_item::coffin && count.count(stock_item::dead_dwarf) && count.count(stock_item::coffin_bld))
    {
        amount = std::max(amount, count.at(stock_item::dead_dwarf) - count.at(stock_item::coffin_bld));
    }
    else if (key == stock_item::coffin_bld && count.count(stock_item::dead_dwarf))
    {
        amount = std::max(amount, count.at(stock_item::dead_dwarf));
    }
    else if (key == stock_item::barrel && need_more(stock_item::bed))
    {
        amount = 0;
    }
    return amount;
}

void Stocks::act(color_ostream & out, stock_item::item key)
{
    if (Watch.Needed.count(key))
    {
        int32_t amount = num_needed(key);
        if (count.at(key) < amount)
        {
            queue_need(out, key, amount * 3 / 2 - count.at(key));
        }
    }

    if (Watch.WatchStock.count(key))
    {
        int32_t amount = Watch.WatchStock.at(key);
        if (count.at(key) > amount)
        {
            queue_use(out, key, count.at(key) - amount);
        }
    }
}

// count unused stocks of one type of item
int32_t Stocks::count_stocks(color_ostream & out, stock_item::item k)
{
    int32_t n = 0;
    auto add = [this, &n](df::item *i)
    {
        if (is_item_free(i))
        {
            n += virtual_cast<df::item_actual>(i)->stack_size;
        }
    };
    auto yes_i_mean_all = [](df::item *) -> bool { return true; };
    auto add_all = [add](df::items_other_id id, std::function<bool(df::item *)> pred)
    {
        for (auto it = world->items.other[id].begin(); it != world->items.other[id].end(); it++)
        {
            if (pred(*it))
            {
                add(*it);
            }
        }
    };
    switch (k)
    {
    case stock_item::bin:
    {
        add_all(items_other_id::BIN, [](df::item *i) -> bool
        {
            return virtual_cast<df::item_binst>(i)->stockpile.id == -1;
        });
        break;
    }
    case stock_item::barrel:
    {
        add_all(items_other_id::BARREL, [](df::item *i) -> bool
        {
            return virtual_cast<df::item_barrelst>(i)->stockpile.id == -1;
        });
        break;
    }
    case stock_item::bag:
    {
        add_all(items_other_id::BOX, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.isAnyCloth() || mat.material->flags.is_set(material_flags::LEATHER);
        });
        break;
    }
    case stock_item::bucket:
    {
        add_all(items_other_id::BUCKET, yes_i_mean_all);
        break;
    }
    case stock_item::meal:
    {
        add_all(items_other_id::ANY_GOOD_FOOD, [](df::item *i) -> bool
        {
            return virtual_cast<df::item_foodst>(i);
        });
        break;
    }
    case stock_item::food_ingredients:
    {
        std::set<std::tuple<df::item_type, int16_t, int16_t, int32_t>> forbidden;
        for (size_t i = 0; i < ui->kitchen.item_types.size(); i++)
        {
            if ((ui->kitchen.exc_types[i] & 1) == 1)
            {
                forbidden.insert(std::make_tuple(ui->kitchen.item_types[i], ui->kitchen.item_subtypes[i], ui->kitchen.mat_types[i], ui->kitchen.mat_indices[i]));
            }
        }

        for (auto i : world->items.other[items_other_id::ANY_COOKABLE])
        {
            if (virtual_cast<df::item_flaskst>(i))
                continue;
            if (virtual_cast<df::item_cagest>(i))
                continue;
            if (virtual_cast<df::item_barrelst>(i))
                continue;
            if (virtual_cast<df::item_bucketst>(i))
                continue;
            if (virtual_cast<df::item_animaltrapst>(i))
                continue;
            if (virtual_cast<df::item_boxst>(i))
                continue;
            if (virtual_cast<df::item_toolst>(i))
                continue;
            if (!forbidden.count(std::make_tuple(i->getType(), i->getSubtype(), i->getMaterial(), i->getMaterialIndex())) && is_item_free(i))
                n++;
        }
        break;
    }
    case stock_item::drink:
    {
        add_all(items_other_id::DRINK, yes_i_mean_all);
        break;
    }
    case stock_item::goblet:
    {
        add_all(items_other_id::GOBLET, yes_i_mean_all);
        break;
    }
    case stock_item::soap:
    {
        add_all(items_other_id::BAR, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.material && mat.material->id == "SOAP";
        });
        break;
    }
    case stock_item::coal:
    {
        add_all(items_other_id::BAR, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.material && mat.material->id == "COAL";
        });
        break;
    }
    case stock_item::ash:
    {
        add_all(items_other_id::BAR, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.material && mat.material->id == "ASH";
        });
        break;
    }
    case stock_item::wood:
    {
        add_all(items_other_id::WOOD, yes_i_mean_all);
        break;
    }
    case stock_item::rough_gem:
    {
        add_all(items_other_id::ROUGH, [](df::item *i) -> bool
        {
            return i->getMaterial() == 0;
        });
        break;
    }
    case stock_item::metal_ore:
    {
        add_all(items_other_id::BOULDER, [this](df::item *i) -> bool
        {
            return is_metal_ore(i);
        });
        break;
    }
    case stock_item::raw_coke:
    {
        add_all(items_other_id::BOULDER, [this](df::item *i) -> bool
        {
            return !is_raw_coke(i).empty();
        });
        break;
    }
    case stock_item::gypsum:
    {
        add_all(items_other_id::BOULDER, [this](df::item *i) -> bool
        {
            return is_gypsum(i);
        });
        break;
    }
    case stock_item::raw_adamantine:
    {
        MaterialInfo candy;
        if (candy.findInorganic("RAW_ADAMANTINE"))
        {
            add_all(items_other_id::BOULDER, [candy](df::item *i) -> bool
            {
                return i->getMaterialIndex() == candy.index;
            });
        }
        break;
    }
    case stock_item::statue:
    {
        add_all(items_other_id::STATUE, yes_i_mean_all);
        break;
    }
    case stock_item::stone:
    {
        add_all(items_other_id::BOULDER, [](df::item *i) -> bool
        {
            return !ui->economic_stone[i->getMaterialIndex()];
        });
        break;
    }
    case stock_item::raw_fish:
    {
        add_all(items_other_id::FISH_RAW, yes_i_mean_all);
        break;
    }
    case stock_item::splint:
    {
        add_all(items_other_id::SPLINT, yes_i_mean_all);
        break;
    }
    case stock_item::crutch:
    {
        add_all(items_other_id::CRUTCH, yes_i_mean_all);
        break;
    }
    case stock_item::crossbow:
    {
        if (manager_subtype.count("MakeBoneCrossbow"))
        {
            add_all(items_other_id::WEAPON, [this](df::item *i) -> bool
            {
                return virtual_cast<df::item_weaponst>(i)->subtype->subtype == manager_subtype.at("MakeBoneCrossbow");
            });
        }
        break;
    }
    case stock_item::clay:
    {
        add_all(items_other_id::BOULDER, [this](df::item *i) -> bool
        {
            return clay_stones.count(i->getMaterialIndex());
        });
        break;
    }
    case stock_item::drink_plant:
    {
        add_all(items_other_id::PLANT, [this](df::item *i) -> bool
        {
            return drink_plants.count(i->getMaterialIndex()) && drink_plants.at(i->getMaterialIndex()) == i->getMaterial();
        });
        break;
    }
    case stock_item::thread_plant:
    {
        add_all(items_other_id::PLANT, [this](df::item *i) -> bool
        {
            return thread_plants.count(i->getMaterialIndex()) && thread_plants.at(i->getMaterialIndex()) == i->getMaterial();
        });
        break;
    }
    case stock_item::mill_plant:
    {
        add_all(items_other_id::PLANT, [this](df::item *i) -> bool
        {
            return mill_plants.count(i->getMaterialIndex()) && mill_plants.at(i->getMaterialIndex()) == i->getMaterial();
        });
        break;
    }
    case stock_item::bag_plant:
    {
        add_all(items_other_id::PLANT, [this](df::item *i) -> bool
        {
            return bag_plants.count(i->getMaterialIndex()) && bag_plants.at(i->getMaterialIndex()) == i->getMaterial();
        });
        break;
    }
    case stock_item::slurry_plant:
    {
        add_all(items_other_id::PLANT, [this](df::item *i) -> bool
        {
            return slurry_plants.count(i->getMaterialIndex()) && slurry_plants.at(i->getMaterialIndex()) == i->getMaterial();
        });
        break;
    }
    case stock_item::drink_fruit:
    {
        add_all(items_other_id::PLANT_GROWTH, [this](df::item *i) -> bool
        {
            return drink_fruits.count(i->getMaterialIndex()) && drink_fruits.at(i->getMaterialIndex()) == i->getMaterial();
        });
        break;
    }
    case stock_item::honey:
    {
        MaterialInfo honey;
        if (honey.findCreature("HONEY_BEE", "HONEY"))
        {
            add_all(items_other_id::LIQUID_MISC, [honey](df::item *i) -> bool
            {
                return i->getMaterialIndex() == honey.index && i->getMaterial() == honey.type;
            });
        }
        break;
    }
    case stock_item::milk:
    {
        add_all(items_other_id::LIQUID_MISC, [this](df::item *i) -> bool
        {
            return milk_creatures.count(i->getMaterialIndex()) && milk_creatures.at(i->getMaterialIndex()) == i->getMaterial();
        });
        break;
    }
    case stock_item::dye_plant:
    {
        add_all(items_other_id::PLANT, [this](df::item *i) -> bool
        {
            return mill_plants.count(i->getMaterialIndex()) && mill_plants.at(i->getMaterialIndex()) == i->getMaterial() && dye_plants.count(i->getMaterialIndex());
        });
        break;
    }
    case stock_item::thread_seeds:
    {
        add_all(items_other_id::SEEDS, [this](df::item *i) -> bool
        {
            return thread_plants.count(i->getMaterialIndex()) && grow_plants.count(i->getMaterialIndex());
        });
        break;
    }
    case stock_item::dye_seeds:
    {
        add_all(items_other_id::SEEDS, [this](df::item *i) -> bool
        {
            return dye_plants.count(i->getMaterialIndex()) && grow_plants.count(i->getMaterialIndex());
        });
        break;
    }
    case stock_item::dye:
    {
        add_all(items_other_id::POWDER_MISC, [this](df::item *i) -> bool
        {
            return dye_plants.count(i->getMaterialIndex()) && dye_plants.at(i->getMaterialIndex()) == i->getMaterial();
        });
        break;
    }
    case stock_item::block:
    {
        add_all(items_other_id::BLOCKS, yes_i_mean_all);
        break;
    }
    case stock_item::skull:
    {
        // XXX exclude dwarf skulls ?
        add_all(items_other_id::CORPSEPIECE, [](df::item *item) -> bool
        {
            df::item_corpsepiecest *i = virtual_cast<df::item_corpsepiecest>(item);
            return i->corpse_flags.bits.skull && !i->corpse_flags.bits.unbutchered;
        });
        break;
    }
    case stock_item::bone:
    {
        for (auto it = world->items.other[items_other_id::CORPSEPIECE].begin(); it != world->items.other[items_other_id::CORPSEPIECE].end(); it++)
        {
            df::item_corpsepiecest *i = virtual_cast<df::item_corpsepiecest>(*it);
            if (i->corpse_flags.bits.bone && !i->corpse_flags.bits.unbutchered)
            {
                n += i->material_amount[corpse_material_type::Bone];
            }
        }
        break;
    }
    case stock_item::shell:
    {
        for (auto it = world->items.other[items_other_id::CORPSEPIECE].begin(); it != world->items.other[items_other_id::CORPSEPIECE].end(); it++)
        {
            df::item_corpsepiecest *i = virtual_cast<df::item_corpsepiecest>(*it);
            if (i->corpse_flags.bits.shell && !i->corpse_flags.bits.unbutchered)
            {
                n += i->material_amount[corpse_material_type::Shell];
            }
        }
        break;
    }
    case stock_item::wool:
    {
        // used for SpinThread which currently ignores the material_amount
        // note: if it didn't, use either HairWool or Yarn but not both
        add_all(items_other_id::CORPSEPIECE, [](df::item *item) -> bool
        {
            df::item_corpsepiecest *i = virtual_cast<df::item_corpsepiecest>(item);
            return i->corpse_flags.bits.hair_wool || i->corpse_flags.bits.yarn;
        });
        break;
    }
    case stock_item::bone_bolts:
    {
        add_all(items_other_id::AMMO, [](df::item *i) -> bool
        {
            return virtual_cast<df::item_ammost>(i)->skill_used == job_skill::BONECARVE;
        });
        break;
    }
    case stock_item::cloth:
    {
        add_all(items_other_id::CLOTH, yes_i_mean_all);
        break;
    }
    case stock_item::cloth_nodye:
    {
        add_all(items_other_id::CLOTH, [this](df::item *i) -> bool
        {
            df::item_clothst *c = virtual_cast<df::item_clothst>(i);
            for (auto imp = c->improvements.begin(); imp != c->improvements.end(); imp++)
            {
                if (dye_plants.count((*imp)->mat_index) && dye_plants.at((*imp)->mat_index) == (*imp)->mat_type)
                {
                    return false;
                }
            }
            return true;
        });
        break;
    }
    case stock_item::mechanism:
    {
        add_all(items_other_id::TRAPPARTS, yes_i_mean_all);
        break;
    }
    case stock_item::coffin_bld:
    {
        // count free constructed coffin buildings, not items
        for (auto bld = world->buildings.other[buildings_other_id::COFFIN].begin(); bld != world->buildings.other[buildings_other_id::COFFIN].end(); bld++)
        {
            if (!(*bld)->owner)
            {
                n++;
            }
        }
        break;
    }
    case stock_item::coffin_bld_pet:
    {
        for (auto bld = world->buildings.other[buildings_other_id::COFFIN].begin(); bld != world->buildings.other[buildings_other_id::COFFIN].end(); bld++)
        {
            df::building_coffinst *coffin = virtual_cast<df::building_coffinst>(*bld);
            if (coffin && !coffin->owner && !coffin->burial_mode.bits.no_pets)
            {
                n++;
            }
        }
        break;
    }
    case stock_item::training_weapon:
    {
        return count_stocks_weapon(out, job_skill::NONE, true);
    }
    case stock_item::weapon:
    {
        return count_stocks_weapon(out);
    }
    case stock_item::pick:
    {
        return count_stocks_weapon(out, job_skill::MINING);
    }
    case stock_item::axe:
    {
        return count_stocks_weapon(out, job_skill::AXE);
    }
    case stock_item::armor_torso:
    {
        return count_stocks_armor(out, items_other_id::ARMOR);
    }
    case stock_item::clothes_torso:
    {
        return count_stocks_clothes(out, items_other_id::ARMOR);
    }
    case stock_item::armor_legs:
    {
        return count_stocks_armor(out, items_other_id::PANTS);
    }
    case stock_item::clothes_legs:
    {
        return count_stocks_clothes(out, items_other_id::PANTS);
    }
    case stock_item::armor_head:
    {
        return count_stocks_armor(out, items_other_id::HELM);
    }
    case stock_item::clothes_head:
    {
        return count_stocks_clothes(out, items_other_id::HELM);
    }
    case stock_item::armor_hands:
    {
        return count_stocks_armor(out, items_other_id::GLOVES);
    }
    case stock_item::clothes_hands:
    {
        return count_stocks_clothes(out, items_other_id::GLOVES);
    }
    case stock_item::armor_feet:
    {
        return count_stocks_armor(out, items_other_id::SHOES);
    }
    case stock_item::clothes_feet:
    {
        return count_stocks_clothes(out, items_other_id::SHOES);
    }
    case stock_item::armor_shield:
    {
        return count_stocks_armor(out, items_other_id::SHIELD);
    }
    case stock_item::lye:
    {
        add_all(items_other_id::LIQUID_MISC, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.material && mat.material->id == "LYE";
            // TODO check container has no water
        });
        break;
    }
    case stock_item::plaster_powder:
    {
        add_all(items_other_id::POWDER_MISC, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.material && mat.material->id == "PLASTER";
        });
        break;
    }
    case stock_item::wheelbarrow:
    case stock_item::minecart:
    case stock_item::nest_box:
    case stock_item::hive:
    case stock_item::jug:
    case stock_item::stepladder:
    case stock_item::bookcase:
    case stock_item::quire:
    case stock_item::rock_pot:
    case stock_item::book_binding:
    {
        std::string ord = furniture_order(k);
        if (manager_subtype.count(ord))
        {
            add_all(items_other_id::TOOL, [this, ord](df::item *item) -> bool
            {
                df::item_toolst *i = virtual_cast<df::item_toolst>(item);
                return i->subtype->subtype == manager_subtype.at(ord) &&
                    i->stockpile.id == -1 &&
                    (i->vehicle_id == -1 || df::vehicle::find(i->vehicle_id)->route_id == -1);
            });
        }
        break;
    }
    case stock_item::honeycomb:
    {
        add_all(items_other_id::TOOL, [](df::item *i) -> bool
        {
            return virtual_cast<df::item_toolst>(i)->subtype->id == "ITEM_TOOL_HONEYCOMB";
        });
        break;
    }
    case stock_item::quiver:
    {
        add_all(items_other_id::QUIVER, yes_i_mean_all);
        break;
    }
    case stock_item::flask:
    {
        add_all(items_other_id::FLASK, yes_i_mean_all);
        break;
    }
    case stock_item::backpack:
    {
        add_all(items_other_id::BACKPACK, yes_i_mean_all);
        break;
    }
    case stock_item::leather:
    {
        add_all(items_other_id::SKIN_TANNED, yes_i_mean_all);
        break;
    }
    case stock_item::tallow:
    {
        add_all(items_other_id::GLOB, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.material && mat.material->id == "TALLOW";
        });
        break;
    }
    case stock_item::giant_corkscrew:
    {
        if (manager_subtype.count("MakeGiantCorkscrew"))
        {
            add_all(items_other_id::TRAPCOMP, [this](df::item *item) -> bool
            {
                df::item_trapcompst *i = virtual_cast<df::item_trapcompst>(item);
                return i && i->subtype->subtype == manager_subtype.at("MakeGiantCorkscrew");
            });
        }
    }
    case stock_item::pipe_section:
    {
        add_all(items_other_id::PIPE_SECTION, yes_i_mean_all);
        break;
    }
    case stock_item::quern:
    {
        // include used in building
        return int32_t(world->items.other[items_other_id::QUERN].size());
    }
    case stock_item::anvil:
    {
        add_all(items_other_id::ANVIL, yes_i_mean_all);
        break;
    }
    case stock_item::slab:
    {
        add_all(items_other_id::SLAB, [](df::item *i) -> bool { return i->getSlabEngravingType() == slab_engraving_type::Slab; });
        break;
    }
    case stock_item::dead_dwarf:
    {
        std::set<df::unit *> units;
        for (auto i : world->items.other[items_other_id::ANY_CORPSE])
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
        return int32_t(units.size());
    }
    case stock_item::slurry:
    {
        add_all(items_other_id::GLOB, [](df::item *i) -> bool
        {
            if (!virtual_cast<df::item_globst>(i)->mat_state.bits.paste)
            {
                return false;
            }
            MaterialInfo mat(i);
            for (auto it : mat.material->reaction_class)
            {
                if (*it == "PAPER_SLURRY")
                {
                    return true;
                }
            }
            return false;
        });
        break;
    }
    case stock_item::paper:
    {
        add_all(items_other_id::SHEET, yes_i_mean_all);
        break;
    }
    case stock_item::toy:
    {
        add_all(items_other_id::TOY, yes_i_mean_all);
        break;
    }
    case stock_item::written_on_quire:
    {
        add_all(items_other_id::TOOL, [this](df::item *i) -> bool
        {
            return i->getSubtype() == manager_subtype.at("MakeQuire") && i->hasSpecificImprovements(improvement_type::WRITING);
        });
        break;
    }
    case stock_item::thread:
    {
        add_all(items_other_id::THREAD, [](df::item *i) -> bool
        {
            return !i->flags.bits.spider_web && virtual_cast<df::item_threadst>(i)->dimension == 15000;
        });
        break;
    }
    default:
    {
        return find_furniture_itemcount(k);
    }
    }

    return n;
}

// return the minimum of the number of free weapons for each subtype used by
// current civ
int32_t Stocks::count_stocks_weapon(color_ostream &, df::job_skill skill, bool training)
{
    int32_t min = -1;
    auto search = [this, &min, skill, training](const std::vector<int16_t> & idefs)
    {
        for (auto id = idefs.begin(); id != idefs.end(); id++)
        {
            df::itemdef_weaponst *idef = df::itemdef_weaponst::find(*id);
            if (skill != job_skill::NONE && idef->skill_melee != skill)
            {
                continue;
            }
            if (idef->flags.is_set(weapon_flags::TRAINING) != training)
            {
                continue;
            }
            int32_t count = 0;
            for (auto item = world->items.other[items_other_id::WEAPON].begin(); item != world->items.other[items_other_id::WEAPON].end(); item++)
            {
                df::item_weaponst *i = virtual_cast<df::item_weaponst>(*item);
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

template<typename D>
static bool is_armor_metal(D *d) { return d->props.flags.is_set(armor_general_flags::METAL); }

template<typename D, typename I>
static int32_t count_stocks_armor_helper(df::items_other_id oidx, const std::vector<int16_t> & idefs, std::function<bool(D *)> pred = is_armor_metal<D>)
{
    int32_t min = -1;
    for (auto id = idefs.begin(); id != idefs.end(); id++)
    {
        int32_t count = 0;
        D *idef = D::find(*id);
        if (!pred(idef))
        {
            continue;
        }
        for (auto item = world->items.other[oidx].begin(); item != world->items.other[oidx].end(); item++)
        {
            I *i = virtual_cast<I>(*item);
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
int32_t Stocks::count_stocks_armor(color_ostream &, df::items_other_id oidx)
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
static int32_t count_stocks_clothes_helper(df::items_other_id oidx, const std::vector<int16_t> & idefs)
{
    int32_t min = -1;
    for (auto id = idefs.begin(); id != idefs.end(); id++)
    {
        int32_t count = 0;
        D *idef = D::find(*id);
        if (!idef->props.flags.is_set(armor_general_flags::SOFT)) // XXX
        {
            continue;
        }
        for (auto item = world->items.other[oidx].begin(); item != world->items.other[oidx].end(); item++)
        {
            I *i = virtual_cast<I>(*item);
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

int32_t Stocks::count_stocks_clothes(color_ostream &, df::items_other_id oidx)
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
void Stocks::queue_need(color_ostream & out, stock_item::item what, int32_t amount)
{
    if (amount <= 0)
        return;

    std::vector<stock_item::item> input;
    std::string order;

    switch (what)
    {
    case stock_item::training_weapon:
    {
        queue_need_weapon(out, what, num_needed(what), job_skill::NONE, true);
        return;
    }
    case stock_item::weapon:
    {
        queue_need_weapon(out, what, num_needed(what));
        return;
    }
    case stock_item::pick:
    {
        queue_need_weapon(out, what, num_needed(what), job_skill::MINING);
        return;
    }
    case stock_item::axe:
    {
        queue_need_weapon(out, what, num_needed(what), job_skill::AXE);
        return;
    }
    case stock_item::armor_torso:
    {
        queue_need_armor(out, what, items_other_id::ARMOR);
        return;
    }
    case stock_item::clothes_torso:
    {
        queue_need_clothes(out, items_other_id::ARMOR);
        return;
    }
    case stock_item::armor_legs:
    {
        queue_need_armor(out, what, items_other_id::PANTS);
        return;
    }
    case stock_item::clothes_legs:
    {
        queue_need_clothes(out, items_other_id::PANTS);
        return;
    }
    case stock_item::armor_head:
    {
        queue_need_armor(out, what, items_other_id::HELM);
        return;
    }
    case stock_item::clothes_head:
    {
        queue_need_clothes(out, items_other_id::HELM);
        return;
    }
    case stock_item::armor_hands:
    {
        queue_need_armor(out, what, items_other_id::GLOVES);
        return;
    }
    case stock_item::clothes_hands:
    {
        queue_need_clothes(out, items_other_id::GLOVES);
        return;
    }
    case stock_item::armor_feet:
    {
        queue_need_armor(out, what, items_other_id::SHOES);
        return;
    }
    case stock_item::clothes_feet:
    {
        queue_need_clothes(out, items_other_id::SHOES);
        return;
    }
    case stock_item::armor_shield:
    {
        queue_need_armor(out, what, items_other_id::SHIELD);
        return;
    }
    case stock_item::anvil:
    {
        queue_need_anvil(out);
        return;
    }
    case stock_item::cage_metal:
    {
        queue_need_cage(out);
        return;
    }
    case stock_item::coffin_bld:
    {
        queue_need_coffin_bld(out, amount);
        return;
    }
    case stock_item::coffin_bld_pet:
    {
        if (count.at(stock_item::coffin_bld) >= Watch.Needed.at(stock_item::coffin_bld))
        {
            for (auto bld = world->buildings.other[buildings_other_id::COFFIN].begin(); bld != world->buildings.other[buildings_other_id::COFFIN].end(); bld++)
            {
                df::building_coffinst *cof = virtual_cast<df::building_coffinst>(*bld);
                if (!cof->owner && cof->burial_mode.bits.no_pets)
                {
                    cof->burial_mode.bits.no_pets = 0;
                    break;
                }
            }
        }
        return;
    }
    case stock_item::raw_coke:
    {
        if (ai->plan->should_search_for_metal)
        {
            for (auto vein = ai->plan->map_veins.begin(); vein != ai->plan->map_veins.end(); vein++)
            {
                if (!is_raw_coke(vein->first).empty())
                {
                    ai->plan->dig_vein(out, vein->first, amount);
                    break;
                }
            }
        }
        return;
    }
    case stock_item::gypsum:
    {
        if (ai->plan->should_search_for_metal)
        {
            for (auto vein = ai->plan->map_veins.begin(); vein != ai->plan->map_veins.end(); vein++)
            {
                if (is_gypsum(vein->first))
                {
                    ai->plan->dig_vein(out, vein->first, amount);
                    break;
                }
            }
        }
        return;
    }
    case stock_item::meal:
    {
        // XXX fish/hunt/cook ?
        if (last_warn_food_year != *cur_year)
        {
            ai->debug(out, stl_sprintf("need %d more food", amount));
            last_warn_food_year = *cur_year;
        }
        return;
    }
    case stock_item::thread_seeds:
    {
        // only useful at game start, with low seeds stocks
        order = "ProcessPlants";
        input.push_back(stock_item::thread_plant);
        break;
    }
    case stock_item::dye_seeds:
    case stock_item::dye:
    {
        order = "MillPlants";
        input.push_back(stock_item::dye_plant);
        input.push_back(stock_item::bag);
        break;
    }
    case stock_item::wood:
    {
        amount *= 2;
        if (amount > 30)
            amount = 30;

        last_cutpos = cuttrees(out, amount / 6 + 1);

        return;
    }
    case stock_item::honey:
    {
        order = "PressHoneycomb";
        input.push_back(stock_item::honeycomb);
        input.push_back(stock_item::jug);
        break;
    }
    case stock_item::drink:
    {
        std::map<stock_item::item, std::string> orders;
        orders[stock_item::drink_plant] = "BrewDrinkPlant";
        orders[stock_item::drink_fruit] = "BrewDrinkFruit";
        orders[stock_item::honey] = "BrewMead";
        auto score = [this, &out](std::pair<const stock_item::item, std::string> i) -> int32_t
        {
            int32_t c = count.at(i.first);
            df::manager_order_template tmpl;
            tmpl.job_type = job_type::CustomReaction;
            tmpl.reaction_name = Manager.Custom.at(i.second);
            tmpl.item_type = item_type::NONE;
            tmpl.item_subtype = -1;
            tmpl.mat_type = -1;
            tmpl.mat_index = -1;
            c -= count_manager_orders(out, tmpl);
            return c;
        };
        auto max = std::max_element(orders.begin(), orders.end(), [score](std::pair<const stock_item::item, std::string> a, std::pair<const stock_item::item, std::string> b) -> bool { return score(a) < score(b); });
        order = max->second;
        input.push_back(max->first);
        if (count[stock_item::barrel] > count[stock_item::rock_pot])
        {
            input.push_back(stock_item::barrel);
        }
        else
        {
            input.push_back(stock_item::rock_pot);
        }
        amount = (amount + 4) / 5; // accounts for brewer yield, but not for input stack size
        break;
    }
    case stock_item::block:
    {
        amount = (amount + 3) / 4;
        // no stone => make wooden blocks (needed for pumps for aquifer handling)
        bool found = false;
        for (auto i = world->items.other[items_other_id::BOULDER].begin(); i != world->items.other[items_other_id::BOULDER].end(); i++)
        {
            if (is_item_free(*i) && !ui->economic_stone[(*i)->getMaterialIndex()])
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
        break;
    }
    case stock_item::coal:
    {
        // dont use wood -> charcoal if we have bituminous coal
        // (except for bootstraping)
        if (amount > 2 - count.at(stock_item::coal) && count.at(stock_item::raw_coke) > Watch.WatchStock.at(stock_item::raw_coke))
        {
            amount = 2 - count.at(stock_item::coal);
        }
        break;
    }
    case stock_item::ash:
    {
        input.push_back(stock_item::wood);
        break;
    }
    case stock_item::lye:
    {
        input.push_back(stock_item::ash);
        input.push_back(stock_item::bucket);
        break;
    }
    case stock_item::soap:
    {
        input.push_back(stock_item::lye);
        input.push_back(stock_item::tallow);
        break;
    }
    case stock_item::plaster_powder:
    {
        input.push_back(stock_item::gypsum);
        input.push_back(stock_item::bag);
        break;
    }
    case stock_item::slurry:
    {
        order = "MakeSlurryFromPlant";
        input.push_back(stock_item::slurry_plant);
        break;
    }
    case stock_item::paper:
    {
        order = "PressPlantPaper";
        input.push_back(stock_item::slurry);
        break;
    }
    case stock_item::quire:
    {
        order = "MakeQuire";
        input.push_back(stock_item::paper);
        break;
    }
    case stock_item::toy:
    {
        order = "MakeToy";
        break;
    }
    case stock_item::book_binding:
    {
        order = "MakeBookBinding";
        break;
    }
    default:
        break; // TODO
    }

    if (order.empty())
    {
        order = furniture_order(what);
    }

    if (amount > 30)
        amount = 30;

    if (!input.empty())
    {
        int32_t i_amount = amount;
        for (auto i = input.begin(); i != input.end(); i++)
        {
            int32_t c = count.at(*i);
            if (c < i_amount)
            {
                i_amount = c;
            }
            if (c < amount && Watch.Needed.count(*i))
            {
                queue_need(out, *i, amount - c);
            }
        }
        amount = i_amount;
    }

    if (Manager.MatCategory.count(order))
    {
        df::job_material_category matcat = Manager.MatCategory.at(order);
        df::job_type job = job_type::NONE;
        find_enum_item(&job, order);
        stock_item::item matcat_item = Manager.MatCategoryItem.at(matcat.whole);
        int32_t i_amount = count.at(matcat_item) - count_manager_orders_matcat(matcat, job);
        if (i_amount < amount && Watch.Needed.count(matcat_item))
        {
            queue_need(out, matcat_item, amount - i_amount);
        }
        if (amount > i_amount)
        {
            amount = i_amount;
        }
    }

    legacy_add_manager_order(out, order, amount);
}

// forge weapons
void Stocks::queue_need_weapon(color_ostream & out, stock_item::item stock_item, int32_t needed, df::job_skill skill, bool training)
{
    if (skill == job_skill::NONE && !training && (count.at(stock_item::pick) == 0 || count.at(stock_item::axe) == 0))
        return;

    auto search = [this, &out, stock_item, needed, skill, training](const std::vector<int16_t> & idefs, df::material_flags pref)
    {
        for (auto id : idefs)
        {
            df::itemdef_weaponst *idef = df::itemdef_weaponst::find(id);
            if (skill != job_skill::NONE && idef->skill_melee != skill)
                continue;
            if (idef->flags.is_set(weapon_flags::TRAINING) != training)
                continue;

            int32_t cnt = needed;
            for (auto item = world->items.other[items_other_id::WEAPON].begin(); item != world->items.other[items_other_id::WEAPON].end(); item++)
            {
                df::item_weaponst *i = virtual_cast<df::item_weaponst>(*item);
                if (i->subtype->subtype == idef->subtype && is_item_free(i))
                {
                    cnt--;
                }
            }
            for (auto mo = world->manager_orders.begin(); mo != world->manager_orders.end(); mo++)
            {
                if ((*mo)->job_type == job_type::MakeWeapon && (*mo)->item_subtype == idef->subtype)
                {
                    cnt -= (*mo)->amount_total;
                }
            }
            if (cnt <= 0)
                continue;

            if (training)
            {
                df::manager_order_template tmpl;
                tmpl.job_type = job_type::MakeWeapon;
                tmpl.item_type = item_type::NONE;
                tmpl.item_subtype = idef->subtype;
                tmpl.mat_index = -1;
                tmpl.material_category.bits.wood = true;
                add_manager_order(out, tmpl, cnt);
                continue;
            }

            int32_t need_bars = idef->material_size / 3; // need this many bars to forge one idef item
            if (need_bars < 1)
                need_bars = 1;

            queue_need_forge(out, pref, need_bars, stock_item, job_type::MakeWeapon, [this, &out, pref, need_bars](const std::map<int32_t, int32_t> & bars, int32_t & chosen_type) -> bool
            {
                std::vector<int32_t> best;
                best.insert(best.end(), metal_pref.at(pref).begin(), metal_pref.at(pref).end());
                std::sort(best.begin(), best.end(), [need_bars](int32_t a, int32_t b) -> bool
                {
                    // should roughly order metals by effectiveness
                    return world->raws.inorganics[a]->material.strength.yield[strain_type::IMPACT] > world->raws.inorganics[b]->material.strength.yield[strain_type::IMPACT];
                });

                for (auto mi : best)
                {
                    if (bars.count(mi))
                    {
                        chosen_type = mi;
                        return true;
                    }
                    if (may_forge_bars(out, mi, need_bars) > 0)
                    {
                        return false;
                    }
                }
                return false;
            }, item_type::NONE, idef->subtype);
        }
    };
    auto & ue = ui->main.fortress_entity->entity_raw->equipment;
    search(ue.digger_id, material_flags::ITEMS_DIGGER);
    search(ue.weapon_id, material_flags::ITEMS_WEAPON);
}

template<typename D, typename I>
static void queue_need_armor_helper(AI *ai, color_ostream & out, stock_item::item what, df::items_other_id oidx, const std::vector<int16_t> & idefs, df::job_type job, int32_t div = 1, std::function<bool(D *)> pred = is_armor_metal<D>)
{
    for (auto id = idefs.begin(); id != idefs.end(); id++)
    {
        D *idef = D::find(*id);

        if (!pred(idef))
        {
            continue;
        }

        int32_t cnt = ai->stocks->num_needed(what);
        int32_t have = 0;
        for (auto item : world->items.other[oidx])
        {
            I *i = virtual_cast<I>(item);
            if (i && i->subtype->subtype == idef->subtype && i->mat_type == 0 && ai->stocks->is_item_free(i))
            {
                have++;
            }
        }
        cnt -= have / div;

        for (auto mo : world->manager_orders)
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

        ai->stocks->queue_need_forge(out, material_flags::ITEMS_ARMOR, need_bars, what, job, [ai, &out, need_bars](const std::map<int32_t, int32_t> & bars, int32_t & chosen_type) -> bool
        {
            std::vector<int32_t> best;
            const auto & pref = ai->stocks->metal_pref.at(material_flags::ITEMS_ARMOR);
            best.insert(best.end(), pref.begin(), pref.end());
            std::sort(best.begin(), best.end(), [](int32_t a, int32_t b) -> bool
            {
                // should roughly order metals by effectiveness
                return world->raws.inorganics[a]->material.strength.yield[strain_type::IMPACT] > world->raws.inorganics[b]->material.strength.yield[strain_type::IMPACT];
            });

            for (auto mi : best)
            {
                if (bars.count(mi))
                {
                    chosen_type = mi;
                    return true;
                }
                if (ai->stocks->may_forge_bars(out, mi, need_bars) > 0)
                {
                    return false;
                }
            }
            return false;
        }, item_type::NONE, idef->subtype);
    }
}

// forge armor pieces
void Stocks::queue_need_armor(color_ostream & out, stock_item::item what, df::items_other_id oidx)
{
    auto & ue = ui->main.fortress_entity->entity_raw->equipment;

    switch (oidx)
    {
    case items_other_id::ARMOR:
        queue_need_armor_helper<df::itemdef_armorst, df::item_armorst>(ai, out, what, oidx, ue.armor_id, job_type::MakeArmor);
        return;
    case items_other_id::SHIELD:
        queue_need_armor_helper<df::itemdef_shieldst, df::item_shieldst>(ai, out, what, oidx, ue.shield_id, job_type::MakeShield, 1, [](df::itemdef_shieldst *) -> bool { return true; });
        return;
    case items_other_id::HELM:
        queue_need_armor_helper<df::itemdef_helmst, df::item_helmst>(ai, out, what, oidx, ue.helm_id, job_type::MakeHelm);
        return;
    case items_other_id::PANTS:
        queue_need_armor_helper<df::itemdef_pantsst, df::item_pantsst>(ai, out, what, oidx, ue.pants_id, job_type::MakePants);
        return;
    case items_other_id::GLOVES:
        queue_need_armor_helper<df::itemdef_glovesst, df::item_glovesst>(ai, out, what, oidx, ue.gloves_id, job_type::MakeGloves, 2);
        return;
    case items_other_id::SHOES:
        queue_need_armor_helper<df::itemdef_shoesst, df::item_shoesst>(ai, out, what, oidx, ue.shoes_id, job_type::MakeGloves, 2);
        return;
    default:
        return;
    }
}

static bool select_most_abundant_metal(const std::map<int32_t, int32_t> & bars, int32_t & chosen_type)
{
    // pick the metal we have the most of
    chosen_type = std::max_element(bars.begin(), bars.end(), [](std::pair<const int32_t, int32_t> a, std::pair<const int32_t, int32_t> b) -> bool
    {
        return a.second < b.second;
    })->first;
    return true;
}

void Stocks::queue_need_anvil(color_ostream & out)
{
    queue_need_forge(out, material_flags::ITEMS_ANVIL, 3, stock_item::anvil, job_type::ForgeAnvil, &select_most_abundant_metal);
}

void Stocks::queue_need_cage(color_ostream & out)
{
    queue_need_forge(out, material_flags::ITEMS_METAL, 3, stock_item::cage_metal, job_type::MakeCage, &select_most_abundant_metal);
}

void Stocks::queue_need_forge(color_ostream & out, df::material_flags preference, int32_t bars_per_item, stock_item::item item, df::job_type job, std::function<bool(const std::map<int32_t, int32_t> & bars, int32_t & chosen_type)> decide, df::item_type item_type, int16_t item_subtype)
{
    int32_t coal_bars = count.at(stock_item::coal);
    if (!world->buildings.other[buildings_other_id::FURNACE_SMELTER_MAGMA].empty())
        coal_bars = 50000;

    if (!metal_pref.count(preference))
    {
        auto & pref = metal_pref[preference];
        for (size_t mi = 0; mi < world->raws.inorganics.size(); mi++)
        {
            if (world->raws.inorganics[mi]->material.flags.is_set(preference))
            {
                pref.insert(int32_t(mi));
            }
        }
    }
    const auto & pref = metal_pref.at(preference);

    int32_t cnt = Watch.Needed.at(item);
    cnt -= count.at(item);

    for (auto mo : world->manager_orders)
    {
        if (mo->job_type == job && mo->item_type == item_type && mo->item_subtype == item_subtype && mo->material_category.whole == 0)
        {
            cnt -= mo->amount_left;
        }
    }

    std::map<int32_t, int32_t> bars = ingots;

    // rough account of already queued jobs consumption
    for (auto mo : world->manager_orders)
    {
        if (mo->mat_type == 0 && bars.count(mo->mat_index))
        {
            bars[mo->mat_index] -= 4 * mo->amount_left;
            coal_bars -= mo->amount_left;
        }
    }

    std::map<int32_t, int32_t> potential_bars = bars;
    if (ai->plan->should_search_for_metal)
    {
        for (auto mi : pref)
        {
            potential_bars[mi] += may_forge_bars(out, mi);
        }
    }

    std::map<int32_t, int32_t> to_queue;

    while (cnt > 0)
    {
        if (coal_bars < 1)
        {
            break;
        }

        for (auto it = potential_bars.begin(); it != potential_bars.end(); )
        {
            if (!pref.count(it->first) || it->second < bars_per_item)
            {
                it = potential_bars.erase(it);
            }
            else
            {
                it++;
            }
        }

        if (potential_bars.empty())
        {
            break;
        }

        int32_t mat_index;
        if (!decide(potential_bars, mat_index))
        {
            break;
        }

        if (!bars.count(mat_index) || bars.at(mat_index) < bars_per_item)
        {
            break;
        }

        to_queue[mat_index]++;
        potential_bars[mat_index] -= bars_per_item;
        bars[mat_index] -= bars_per_item;
        coal_bars--;
    }

    for (auto q : to_queue)
    {
        df::manager_order_template tmpl;
        tmpl.job_type = job;
        tmpl.item_type = item_type;
        tmpl.item_subtype = item_subtype;
        tmpl.mat_type = 0;
        tmpl.mat_index = q.first;
        add_manager_order(out, tmpl, q.second);
    }
}

template<typename D, typename I>
static void queue_need_clothes_helper(AI *ai, color_ostream & out, df::items_other_id oidx, const std::vector<int16_t> & idefs, int32_t & available_cloth, df::job_type job, int32_t needed, int32_t div = 1)
{
    int32_t thread = 0, yarn = 0, silk = 0;

    for (auto & item : world->items.other[items_other_id::CLOTH])
    {
        MaterialInfo mat(item);
        if (mat.material)
        {
            if (mat.material->flags.is_set(material_flags::THREAD_PLANT))
            {
                thread++;
            }
            else if (mat.material->flags.is_set(material_flags::SILK))
            {
                silk++;
            }
            else if (mat.material->flags.is_set(material_flags::YARN))
            {
                yarn++;
            }
        }
    }

    for (auto id = idefs.begin(); id != idefs.end(); id++)
    {
        D *idef = D::find(*id);
        if (!idef->props.flags.is_set(armor_general_flags::SOFT)) // XXX
            continue;

        int32_t cnt = needed;
        int32_t have = 0;
        for (auto item = world->items.other[oidx].begin(); item != world->items.other[oidx].end(); item++)
        {
            I *i = virtual_cast<I>(*item);
            if (i->subtype->subtype == idef->subtype &&
                i->mat_type != 0 &&
                i->wear == 0 &&
                ai->stocks->is_item_free(i))
            {
                have++;
            }
        }
        cnt -= have / div;

        for (auto mo = world->manager_orders.begin(); mo != world->manager_orders.end(); mo++)
        {
            if ((*mo)->job_type == job && (*mo)->item_subtype == idef->subtype)
            {
                cnt -= (*mo)->amount_total;
            }
            // TODO subtract available_cloth too
        }
        if (cnt > available_cloth)
            cnt = available_cloth;
        if (cnt <= 0)
            continue;

        df::manager_order_template tmpl;
        tmpl.job_type = job;
        tmpl.item_type = item_type::NONE;
        tmpl.item_subtype = idef->subtype;
        tmpl.mat_type = -1;
        tmpl.mat_index = -1;
        if (thread >= yarn && thread >= silk)
        {
            tmpl.material_category.bits.cloth = 1;
            thread -= cnt;
        }
        else if (yarn >= thread && yarn >= silk)
        {
            tmpl.material_category.bits.yarn = 1;
            yarn -= cnt;
        }
        else if (silk >= thread && silk >= yarn)
        {
            tmpl.material_category.bits.silk = 1;
            silk -= cnt;
        }
        ai->stocks->add_manager_order(out, tmpl, cnt);

        available_cloth -= cnt;
    }
}

void Stocks::queue_need_clothes(color_ostream & out, df::items_other_id oidx)
{
    // try to avoid cancel spam
    int32_t available_cloth = count.at(stock_item::cloth) - 20;

    auto & ue = ui->main.fortress_entity->entity_raw->equipment;

    switch (oidx)
    {
    case items_other_id::ARMOR:
        queue_need_clothes_helper<df::itemdef_armorst, df::item_armorst>(ai, out, oidx, ue.armor_id, available_cloth, job_type::MakeArmor, num_needed(stock_item::clothes_torso));
        return;
    case items_other_id::HELM:
        queue_need_clothes_helper<df::itemdef_helmst, df::item_helmst>(ai, out, oidx, ue.helm_id, available_cloth, job_type::MakeHelm, num_needed(stock_item::clothes_head));
        return;
    case items_other_id::PANTS:
        queue_need_clothes_helper<df::itemdef_pantsst, df::item_pantsst>(ai, out, oidx, ue.pants_id, available_cloth, job_type::MakePants, num_needed(stock_item::clothes_legs));
        return;
    case items_other_id::GLOVES:
        queue_need_clothes_helper<df::itemdef_glovesst, df::item_glovesst>(ai, out, oidx, ue.gloves_id, available_cloth, job_type::MakeGloves, num_needed(stock_item::clothes_hands), 2);
        return;
    case items_other_id::SHOES:
        queue_need_clothes_helper<df::itemdef_shoesst, df::item_shoesst>(ai, out, oidx, ue.shoes_id, available_cloth, job_type::MakeShoes, num_needed(stock_item::clothes_feet), 2);
        return;
    default:
        return;
    }
}

void Stocks::queue_need_coffin_bld(color_ostream & out, int32_t amount)
{
    // dont dig too early
    if (!ai->plan->find_room(room_type::cemetery, [](room *r) -> bool { return r->status != room_status::plan; }))
        return;

    // count actually allocated (plan wise) coffin buildings
    if (ai->plan->find_room(room_type::cemetery, [&amount](room *r) -> bool
    {
        for (auto f : r->layout)
        {
            if (f->type == layout_type::coffin && f->bld_id == -1 && !f->ignore)
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
void Stocks::queue_use(color_ostream & out, stock_item::item what, int32_t amount)
{
    if (amount <= 0)
        return;

    std::vector<stock_item::item> input;
    std::string order;

    switch (what)
    {
    case stock_item::metal_ore:
    {
        queue_use_metal_ore(out, amount);
        return;
    }
    case stock_item::raw_coke:
    {
        queue_use_raw_coke(out, amount);
        return;
    }
    case stock_item::rough_gem:
    {
        queue_use_gems(out, amount);
        return;
    }
    case stock_item::raw_adamantine:
    {
        order = "ExtractMetalStrands";
        break;
    }
    case stock_item::clay:
    {
        input.push_back(stock_item::coal); // TODO: handle magma kilns
        order = "MakeClayStatue";
        break;
    }
    case stock_item::drink_plant:
    case stock_item::drink_fruit:
    {
        order = what == stock_item::drink_plant ? "BrewDrinkPlant" : "BrewDrinkFruit";
        // stuff may rot/be brewed before we can process it
        if (amount > 10)
            amount /= 2;
        if (amount > 4)
            amount /= 2;

        if (count[stock_item::barrel] > count[stock_item::rock_pot])
        {
            input.push_back(stock_item::barrel);
        }
        else
        {
            input.push_back(stock_item::rock_pot);
        }

        if (!need_more(stock_item::drink))
        {
            return;
        }
        break;
    }
    case stock_item::thread_plant:
    {
        order = "ProcessPlants";
        // stuff may rot/be brewed before we can process it
        if (amount > 10)
            amount /= 2;
        if (amount > 4)
            amount /= 2;
        break;
    }
    case stock_item::mill_plant:
    case stock_item::bag_plant:
    {
        order = what == stock_item::mill_plant ? "MillPlants" : "ProcessPlantsBag";
        // stuff may rot/be brewed before we can process it
        if (amount > 10)
            amount /= 2;
        if (amount > 4)
            amount /= 2;
        input.push_back(stock_item::bag);
        break;
    }
    case stock_item::food_ingredients:
    {
        order = "PrepareMeal";
        amount = (amount + 4) / 5;
        if (!need_more(stock_item::meal))
        {
            return;
        }
        break;
    }
    case stock_item::skull:
    {
        order = "MakeTotem";
        break;
    }
    case stock_item::bone:
    {
        int32_t nhunters = 0;
        for (auto u = world->units.active.begin(); u != world->units.active.end(); u++)
        {
            if (Units::isCitizen(*u) && !Units::isDead(*u) && (*u)->status.labors[unit_labor::HUNT])
            {
                nhunters++;
            }
        }
        if (!nhunters)
        {
            return;
        }
        int32_t need_crossbow = nhunters + 1 - count.at(stock_item::crossbow);
        if (need_crossbow > 0)
        {
            order = "MakeBoneCrossbow";
            if (amount > need_crossbow)
                amount = need_crossbow;
        }
        else
        {
            order = "MakeBoneBolt";
            int32_t stock = count.at(stock_item::bone_bolts);
            if (amount > 1000 - stock)
                amount = 1000 - stock;
            if (amount > 10)
                amount /= 2;
            if (amount > 4)
                amount /= 2;
        }
        break;
    }
    case stock_item::shell:
    {
        order = "DecorateWithShell";
        break;
    }
    case stock_item::wool:
    {
        order = "SpinThread";
        break;
    }
    case stock_item::cloth_nodye:
    {
        order = "DyeCloth";
        input.push_back(stock_item::dye);
        if (amount > 10)
            amount /= 2;
        if (amount > 4)
            amount /= 2;
        break;
    }
    case stock_item::raw_fish:
    {
        order = "PrepareRawFish";
        break;
    }
    case stock_item::honeycomb:
    {
        order = "PressHoneycomb";
        input.push_back(stock_item::jug);
        break;
    }
    case stock_item::honey:
    {
        order = "BrewMead";
        if (count[stock_item::barrel] > count[stock_item::rock_pot])
        {
            input.push_back(stock_item::barrel);
        }
        else
        {
            input.push_back(stock_item::rock_pot);
        }
        break;
    }
    case stock_item::milk:
    {
        order = "MakeCheese";
        break;
    }
    case stock_item::tallow:
    {
        order = "MakeSoap";
        input.push_back(stock_item::lye);
        if (!need_more(stock_item::soap))
        {
            return;
        }
        break;
    }
    case stock_item::written_on_quire:
    {
        input.push_back(stock_item::book_binding);
        input.push_back(stock_item::thread);
        order = "BindBook";
        break;
    }
    default:
        break; // TODO
    }

    if (amount > 30)
        amount = 30;

    if (!input.empty())
    {
        int32_t i_amount = amount;
        for (auto i = input.begin(); i != input.end(); i++)
        {
            int32_t c = count.at(*i);
            if (i_amount > c)
                i_amount = c;
            if (c < amount && Watch.Needed.count(*i))
            {
                queue_need(out, *i, amount - c);
            }
        }
        amount = i_amount;
    }

    legacy_add_manager_order(out, order, amount);
}

// cut gems
void Stocks::queue_use_gems(color_ostream & out, int32_t amount)
{
    for (auto mo = world->manager_orders.begin(); mo != world->manager_orders.end(); mo++)
    {
        if ((*mo)->job_type == job_type::CutGems)
        {
            return;
        }
    }
    df::item *base = nullptr;
    for (auto i = world->items.other[items_other_id::ROUGH].begin(); i != world->items.other[items_other_id::ROUGH].end(); i++)
    {
        if ((*i)->getMaterial() == 0 && is_item_free(*i))
        {
            base = *i;
            break;
        }
    }
    if (!base)
    {
        return;
    }
    int32_t this_amount = 0;
    for (auto i = world->items.other[items_other_id::ROUGH].begin(); i != world->items.other[items_other_id::ROUGH].end(); i++)
    {
        if ((*i)->getMaterial() == base->getMaterial() && (*i)->getMaterialIndex() == base->getMaterialIndex() && is_item_free(*i))
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

    df::manager_order_template tmpl;
    tmpl.job_type = job_type::CutGems;
    tmpl.item_type = item_type::NONE;
    tmpl.item_subtype = -1;
    tmpl.mat_type = base->getMaterial();
    tmpl.mat_index = base->getMaterialIndex();
    add_manager_order(out, tmpl, amount);
}

// smelt metal ores
void Stocks::queue_use_metal_ore(color_ostream & out, int32_t amount)
{
    // make coke from bituminous coal has priority
    if (count.at(stock_item::raw_coke) > Watch.WatchStock.at(stock_item::raw_coke) && count.at(stock_item::coal) < 100)
    {
        return;
    }
    for (auto mo = world->manager_orders.begin(); mo != world->manager_orders.end(); mo++)
    {
        if ((*mo)->job_type == job_type::SmeltOre)
        {
            return;
        }
    }

    df::item *base = nullptr;
    for (auto i = world->items.other[items_other_id::BOULDER].begin(); i != world->items.other[items_other_id::BOULDER].end(); i++)
    {
        if (is_metal_ore(*i) && is_item_free(*i))
        {
            base = *i;
            break;
        }
    }
    if (!base)
    {
        return;
    }
    int32_t this_amount = 0;
    for (auto i = world->items.other[items_other_id::BOULDER].begin(); i != world->items.other[items_other_id::BOULDER].end(); i++)
    {
        if ((*i)->getMaterial() == base->getMaterial() && (*i)->getMaterialIndex() == base->getMaterialIndex() && is_item_free(*i))
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
        if (amount > count.at(stock_item::coal))
            amount = count.at(stock_item::coal);
        if (amount <= 0)
            return;
    }

    df::manager_order_template tmpl;
    tmpl.job_type = job_type::SmeltOre;
    tmpl.item_type = item_type::NONE;
    tmpl.item_subtype = -1;
    tmpl.mat_type = base->getMaterial();
    tmpl.mat_index = base->getMaterialIndex();
    add_manager_order(out, tmpl, amount);
}

// bituminous_coal -> coke
void Stocks::queue_use_raw_coke(color_ostream & out, int32_t amount)
{
    is_raw_coke(0); // populate raw_coke_inv
    for (auto mo = world->manager_orders.begin(); mo != world->manager_orders.end(); mo++)
    {
        if ((*mo)->job_type == job_type::CustomReaction && raw_coke_inv.count((*mo)->reaction_name))
        {
            return;
        }
    }

    std::string reaction;
    df::item *base = nullptr;
    for (auto i = world->items.other[items_other_id::BOULDER].begin(); i != world->items.other[items_other_id::BOULDER].end(); i++)
    {
        reaction = is_raw_coke(*i);
        if (!reaction.empty() && is_item_free(*i))
        {
            base = *i;
            break;
        }
    }
    if (!base)
    {
        return;
    }

    int32_t this_amount = 0;
    for (auto i = world->items.other[items_other_id::BOULDER].begin(); i != world->items.other[items_other_id::BOULDER].end(); i++)
    {
        if ((*i)->getMaterial() == base->getMaterial() && (*i)->getMaterialIndex() == base->getMaterialIndex() && is_item_free(*i))
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
        if (count.at(stock_item::coal) <= 0)
        {
            return;
        }
    }

    df::manager_order_template tmpl;
    tmpl.job_type = job_type::CustomReaction;
    tmpl.reaction_name = reaction;
    tmpl.item_type = item_type::NONE;
    tmpl.item_subtype = -1;
    tmpl.mat_type = -1;
    tmpl.mat_index = -1;
    add_manager_order(out, tmpl, amount);
}

// designate some trees for woodcutting
df::coord Stocks::cuttrees(color_ostream &, int32_t amount)
{
    std::set<df::coord> jobs;

    for (auto job = world->job_list.next; job; job = job->next)
    {
        if (job->item->job_type == job_type::FellTree)
        {
            jobs.insert(job->item->pos);
        }
    }

    if (last_cutpos.isValid() && (Maps::getTileDesignation(last_cutpos)->bits.dig != tile_dig_designation::No || jobs.count(last_cutpos)))
    {
        // skip designating if we haven't cut the last tree yet
        return last_cutpos;
    }

    // return the bottom-rightest designated tree
    df::coord br;
    br.clear();

    auto list = tree_list();

    for (auto tree = list.begin(); tree != list.end(); tree++)
    {
        if (ENUM_ATTR(tiletype, material, *Maps::getTileType(*tree)) != tiletype_material::TREE)
        {
            continue;
        }

        if (!br.isValid() || (br.x & -16) < (tree->x & -16) || ((br.x & -16) == (tree->x & -16) && (br.y & -16) < (tree->y & -16)))
        {
            br = *tree;
        }

        if (Maps::getTileDesignation(*tree)->bits.dig == tile_dig_designation::No && !jobs.count(*tree))
        {
            Plan::dig_tile(*tree, tile_dig_designation::Default);
        }

        amount--;
        if (amount <= 0)
        {
            break;
        }
    }

    return br;
}

// return a list of trees on the map
// lists only visible trees, sorted by distance from the fort entrance
// expensive method, dont call often
std::set<df::coord, std::function<bool(df::coord, df::coord)>> Stocks::tree_list()
{
    uint16_t walkable = Plan::getTileWalkable(ai->plan->fort_entrance->max);

    auto is_walkable = [walkable](df::coord t) -> bool
    {
        return walkable == Plan::getTileWalkable(t);
    };

    auto add_from_vector = [this, is_walkable](std::vector<df::plant *> & trees)
    {
        for (auto it = trees.begin(); it != trees.end(); it++)
        {
            df::plant *p = *it;
            df::tiletype tt = *Maps::getTileType(p->pos);
            if (ENUM_ATTR(tiletype, material, tt) == tiletype_material::TREE &&
                ENUM_ATTR(tiletype, shape, tt) == tiletype_shape::WALL &&
                !Maps::getTileDesignation(p->pos)->bits.hidden &&
                !Plan::spiral_search(p->pos, 1, [](df::coord t) -> bool
            {
                df::tile_designation *td = Maps::getTileDesignation(t);
                return td && td->bits.flow_size > 0;
            }).isValid() &&
                Plan::spiral_search(p->pos, 1, is_walkable).isValid())
            {
                last_treelist.insert(p->pos);
            }
        }
    };

    last_treelist.clear();
    add_from_vector(world->plants.tree_dry);
    add_from_vector(world->plants.tree_wet);

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
        for (auto ir = i->general_refs.begin(); ir != i->general_refs.end(); ir++)
        {
            if (virtual_cast<df::general_ref_contains_itemst>(*ir))
            {
                return false;
            }
        }
    }

    if (i->flags.bits.in_inventory)
    {
        // is not in a unit's inventory (ignore if it is simply hauled)
        for (auto ir = i->general_refs.begin(); ir != i->general_refs.end(); ir++)
        {
            if (virtual_cast<df::general_ref_unit_holderst>(*ir))
            {
                auto & inv = (*ir)->getUnit()->inventory;
                for (auto ii = inv.begin(); ii != inv.end(); ii++)
                {
                    if ((*ii)->item == i && (*ii)->mode != df::unit_inventory_item::Hauled)
                    {
                        return false;
                    }
                }
            }
            if (virtual_cast<df::general_ref_contained_in_itemst>(*ir) && !is_item_free((*ir)->getItem(), true))
            {
                return false;
            }
        }
    }

    if (i->flags.bits.in_building)
    {
        // is not part of a building construction materials
        for (auto ir = i->general_refs.begin(); ir != i->general_refs.end(); ir++)
        {
            if (virtual_cast<df::general_ref_building_holderst>(*ir))
            {
                auto & inv = virtual_cast<df::building_actual>((*ir)->getBuilding())->contained_items;
                for (auto bi = inv.begin(); bi != inv.end(); bi++)
                {
                    if ((*bi)->use_mode == 2 && (*bi)->item == i)
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
        for (auto r = world->raws.reactions.begin(); r != world->raws.reactions.end(); r++)
        {
            if ((*r)->reagents.size() != 1)
                continue;

            int32_t mat;

            bool found = false;
            for (auto rr = (*r)->reagents.begin(); rr != (*r)->reagents.end(); rr++)
            {
                df::reaction_reagent_itemst *rri = virtual_cast<df::reaction_reagent_itemst>(*rr);
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
            for (auto rp = (*r)->products.begin(); rp != (*r)->products.end(); rp++)
            {
                df::reaction_product_itemst *rpi = virtual_cast<df::reaction_product_itemst>(*rp);
                if (rpi && rpi->item_type == item_type::BAR && MaterialInfo(rpi).material->id == "COAL")
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                continue;

            // XXX check input size vs output size ?
            raw_coke[mat] = (*r)->code;
            raw_coke_inv[(*r)->code] = mat;
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
    for (auto c = world->raws.inorganics[mi]->material.reaction_class.begin(); c != world->raws.inorganics[mi]->material.reaction_class.end(); c++)
    {
        if (**c == "GYPSUM")
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

void Stocks::update_simple_metal_ores(color_ostream &)
{
    simple_metal_ores.clear();
    simple_metal_ores.resize(world->raws.inorganics.size());
    for (auto it = world->raws.inorganics.begin(); it != world->raws.inorganics.end(); it++)
    {
        const auto & bars = (*it)->metal_ore.mat_index;
        for (auto bar = bars.begin(); bar != bars.end(); bar++)
        {
            simple_metal_ores.at(*bar).insert(it - world->raws.inorganics.begin());
        }
    }
}

// determine if we may be able to generate metal bars for this metal
// may queue manager_jobs to do so
// recursive (eg steel need pig_iron)
// return the potential number of bars available (in dimensions, eg 1 bar => 150)
int32_t Stocks::may_forge_bars(color_ostream & out, int32_t mat_index, int32_t div)
{
    int32_t can_melt = 0;
    for (auto i = world->items.other[items_other_id::BOULDER].begin(); i != world->items.other[items_other_id::BOULDER].end(); i++)
    {
        if (is_metal_ore(*i) && simple_metal_ores.at(mat_index).count((*i)->getMaterialIndex()) && is_item_free(*i))
        {
            can_melt++;
        }
    }

    if (can_melt < Watch.WatchStock.at(stock_item::metal_ore) && ai->plan->should_search_for_metal)
    {
        for (auto k = ai->plan->map_veins.begin(); k != ai->plan->map_veins.end(); k++)
        {
            if (simple_metal_ores.at(mat_index).count(k->first))
            {
                can_melt += ai->plan->dig_vein(out, k->first);
            }
        }
    }

    if (can_melt > Watch.WatchStock.at(stock_item::metal_ore))
    {
        return 4 * 150 * (can_melt - Watch.WatchStock.at(stock_item::metal_ore));
    }

    // "make <mi> bars" customreaction
    for (auto r = world->raws.reactions.begin(); r != world->raws.reactions.end(); r++)
    {
        // XXX choose best reaction from all reactions
        int32_t prod_mult = -1;
        for (auto rp = (*r)->products.begin(); rp != (*r)->products.end(); rp++)
        {
            df::reaction_product_itemst *rpi = virtual_cast<df::reaction_product_itemst>(*rp);
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
        for (auto rr = (*r)->reagents.begin(); rr != (*r)->reagents.end(); rr++)
        {
            // XXX may queue forge reagents[1] even if we dont handle reagents[2]
            df::reaction_reagent_itemst *rri = virtual_cast<df::reaction_reagent_itemst>(*rr);
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
            for (auto i = world->items.other[oidx].begin(); i != world->items.other[oidx].end(); i++)
            {
                if (rri->mat_type != -1 && (*i)->getMaterial() != rri->mat_type)
                    continue;
                if (rri->mat_index != -1 && (*i)->getMaterialIndex() != rri->mat_index)
                    continue;
                if (!is_item_free(*i))
                    continue;
                if (!rri->reaction_class.empty())
                {
                    MaterialInfo mi(*i);
                    bool found = false;
                    for (auto c = mi.material->reaction_class.begin(); c != mi.material->reaction_class.end(); c++)
                    {
                        if (**c == rri->reaction_class)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        continue;
                }
                if (rri->metal_ore != -1 && (*i)->getMaterial() == 0)
                {
                    bool found = false;
                    auto & mis = world->raws.inorganics[(*i)->getMaterialIndex()]->metal_ore.mat_index;
                    for (auto mi = mis.begin(); mi != mis.end(); mi++)
                    {
                        if (*mi == rri->metal_ore)
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
                    has += virtual_cast<df::item_barst>(*i)->dimension;
                }
                else
                {
                    has++;
                }
            }
            if (has <= 0 && rri->item_type == item_type::BOULDER && rri->mat_type == 0 && rri->mat_index != -1 && ai->plan->should_search_for_metal)
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
                for (auto mo = world->manager_orders.begin(); mo != world->manager_orders.end(); mo++)
                {
                    if ((*mo)->job_type == job_type::CustomReaction && (*mo)->reaction_name == (*r)->code)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    df::manager_order_template tmpl;
                    tmpl.job_type = job_type::CustomReaction;
                    tmpl.reaction_name = (*r)->code;
                    tmpl.item_type = item_type::NONE;
                    tmpl.item_subtype = -1;
                    tmpl.mat_type = -1;
                    tmpl.mat_index = -1;
                    add_manager_order(out, tmpl, can_reaction);
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

    if (!world->raws.itemdefs.tools_by_type[tool_uses::HEAVY_OBJECT_HAULING].empty())
        manager_subtype["MakeWoodenWheelbarrow"] = world->raws.itemdefs.tools_by_type[tool_uses::HEAVY_OBJECT_HAULING][0]->subtype;
    if (!world->raws.itemdefs.tools_by_type[tool_uses::TRACK_CART].empty())
        manager_subtype["MakeWoodenMinecart"] = world->raws.itemdefs.tools_by_type[tool_uses::TRACK_CART][0]->subtype;
    if (!world->raws.itemdefs.tools_by_type[tool_uses::NEST_BOX].empty())
        manager_subtype["MakeRockNestBox"] = world->raws.itemdefs.tools_by_type[tool_uses::NEST_BOX][0]->subtype;
    if (!world->raws.itemdefs.tools_by_type[tool_uses::HIVE].empty())
        manager_subtype["MakeRockHive"] = world->raws.itemdefs.tools_by_type[tool_uses::HIVE][0]->subtype;
    if (!world->raws.itemdefs.tools_by_type[tool_uses::LIQUID_CONTAINER].empty())
        manager_subtype["MakeRockJug"] = world->raws.itemdefs.tools_by_type[tool_uses::LIQUID_CONTAINER][0]->subtype;
    if (!world->raws.itemdefs.tools_by_type[tool_uses::STAND_AND_WORK_ABOVE].empty())
        manager_subtype["MakeWoodenStepladder"] = world->raws.itemdefs.tools_by_type[tool_uses::STAND_AND_WORK_ABOVE][0]->subtype;
    if (!world->raws.itemdefs.tools_by_type[tool_uses::BOOKCASE].empty())
        manager_subtype["MakeRockBookcase"] = world->raws.itemdefs.tools_by_type[tool_uses::BOOKCASE][0]->subtype;
    for (auto it = world->raws.itemdefs.tools_by_type[tool_uses::CONTAIN_WRITING].begin(); it != world->raws.itemdefs.tools_by_type[tool_uses::CONTAIN_WRITING].end(); it++)
    {
        if ((*it)->flags.is_set(tool_flags::INCOMPLETE_ITEM))
        {
            manager_subtype["MakeQuire"] = (*it)->subtype;
            break;
        }
    }
    if (!world->raws.itemdefs.tools_by_type[tool_uses::PROTECT_FOLDED_SHEETS].empty())
        manager_subtype["MakeBookBinding"] = world->raws.itemdefs.tools_by_type[tool_uses::PROTECT_FOLDED_SHEETS][0]->subtype;
    if (!world->raws.itemdefs.tools_by_type[tool_uses::FOOD_STORAGE].empty())
        manager_subtype["MakeRockPot"] = world->raws.itemdefs.tools_by_type[tool_uses::FOOD_STORAGE][0]->subtype;

    for (auto def = world->raws.itemdefs.weapons.begin(); def != world->raws.itemdefs.weapons.end(); def++)
    {
        if ((*def)->id == "ITEM_WEAPON_AXE_TRAINING")
        {
            manager_subtype["MakeTrainingAxe"] = (*def)->subtype;
        }
        else if ((*def)->id == "ITEM_WEAPON_SWORD_SHORT_TRAINING")
        {
            manager_subtype["MakeTrainingShortSword"] = (*def)->subtype;
        }
        else if ((*def)->id == "ITEM_WEAPON_SPEAR_TRAINING")
        {
            manager_subtype["MakeTrainingSpear"] = (*def)->subtype;
        }
        else if ((*def)->id == "ITEM_WEAPON_CROSSBOW")
        {
            manager_subtype["MakeBoneCrossbow"] = (*def)->subtype;
        }
    }
    for (auto def = world->raws.itemdefs.ammo.begin(); def != world->raws.itemdefs.ammo.end(); def++)
    {
        if ((*def)->id == "ITEM_AMMO_BOLTS")
        {
            manager_subtype["MakeBoneBolt"] = (*def)->subtype;
        }
    }
    for (auto def = world->raws.itemdefs.trapcomps.begin(); def != world->raws.itemdefs.trapcomps.end(); def++)
    {
        if ((*def)->id == "ITEM_TRAPCOMP_ENORMOUSCORKSCREW")
        {
            manager_subtype["MakeGiantCorkscrew"] = (*def)->subtype;
        }
    }
}

// return the number of current manager orders that share the same material (leather, cloth)
// ignore inorganics, ignore order
int32_t Stocks::count_manager_orders_matcat(const df::job_material_category & matcat, df::job_type order)
{
    int32_t cnt = 0;
    for (auto it = world->manager_orders.begin(); it != world->manager_orders.end(); it++)
    {
        if ((*it)->material_category.whole == matcat.whole && (*it)->job_type != order)
        {
            cnt += (*it)->amount_total;
        }
    }
    return cnt;
}

void Stocks::legacy_add_manager_order(color_ostream & out, std::string order, int32_t amount, int32_t)
{
    df::manager_order_template tmpl;
    if (Manager.RealOrder.count(order))
    {
        tmpl.job_type = Manager.RealOrder.at(order);
    }
    else if (!find_enum_item(&tmpl.job_type, order))
    {
        ai->debug(out, "[ERROR] no such manager order: " + order);
        return;
    }

    if (Manager.MatCategory.count(order))
    {
        tmpl.material_category = Manager.MatCategory.at(order);
    }
    tmpl.mat_type = Manager.Type.count(order) ? Manager.Type.at(order) : Manager.MatCategory.count(order) ? -1 : 0;
    tmpl.mat_index = -1;
    if (tmpl.job_type == job_type::ExtractMetalStrands)
    {
        MaterialInfo candy;
        candy.findInorganic("RAW_ADAMANTINE");
        tmpl.mat_index = candy.index;
    }
    tmpl.item_type = item_type::NONE;
    tmpl.item_subtype = manager_subtype.count(order) ? manager_subtype.at(order) : -1;
    tmpl.reaction_name = Manager.Custom.count(order) ? Manager.Custom.at(order) : "";

    add_manager_order(out, tmpl, amount);
}

template<typename T>
static bool template_equals(const T *a, const df::manager_order_template *b)
{
    if (a->job_type != b->job_type)
        return false;
    if (a->reaction_name != b->reaction_name)
        return false;
    if (a->item_type != b->item_type)
        return false;
    if (a->item_subtype != b->item_subtype)
        return false;
    if (a->mat_type != b->mat_type)
        return false;
    if (a->mat_index != b->mat_index)
        return false;
    if (a->item_category.whole != b->item_category.whole)
        return false;
    if (a->hist_figure_id != b->hist_figure_id)
        return false;
    if (a->material_category.whole != b->material_category.whole)
        return false;
    return true;
}

int32_t Stocks::count_manager_orders(color_ostream &, const df::manager_order_template & tmpl)
{
    int32_t amount = 0;

    for (auto it = world->manager_orders.begin(); it != world->manager_orders.end(); it++)
    {
        if (template_equals<df::manager_order>(*it, &tmpl))
        {
            amount += (*it)->amount_left;
        }
    }

    return amount;
}

void Stocks::add_manager_order(color_ostream & out, const df::manager_order_template & tmpl, int32_t amount)
{
    amount -= count_manager_orders(out, tmpl);
    if (amount <= 0)
    {
        return;
    }

    if (!ai->is_dwarfmode_viewscreen())
    {
        ai->debug(out, stl_sprintf("cannot add manager order for %s - not on main screen", tmpl.job_type == job_type::CustomReaction ? tmpl.reaction_name.c_str() : ENUM_ATTR(job_type, caption, tmpl.job_type)));
        return;
    }
    AI::feed_key(interface_key::D_JOBLIST);
    AI::feed_key(interface_key::UNITJOB_MANAGER);
    AI::feed_key(interface_key::MANAGER_NEW_ORDER);
    auto view = strict_virtual_cast<df::viewscreen_createquotast>(Gui::getCurViewscreen(true));
    if (!view)
    {
        ai->debug(out, stl_sprintf("[ERROR] viewscreen when queueing manager job is %s, not viewscreen_createquotast", virtual_identity::get(Gui::getCurViewscreen(true))->getName()));
        return;
    }
    int32_t idx = -1;
    df::manager_order_template *target = nullptr;
    for (auto it = view->orders.begin(); it != view->orders.end(); it++)
    {
        if (template_equals<df::manager_order_template>(*it, &tmpl))
        {
            idx = it - view->orders.begin();
            target = *it;
            break;
        }
    }
    if (!target)
    {
        target = df::allocate<df::manager_order_template>();
        *target = tmpl;
        idx = int32_t(view->orders.size());
        view->orders.push_back(target);
        view->all_orders.push_back(target);
    }

    while (view->sel_idx < idx)
    {
        AI::feed_key(interface_key::STANDARDSCROLL_PAGEDOWN);
    }
    while (view->sel_idx > idx)
    {
        AI::feed_key(interface_key::STANDARDSCROLL_UP);
    }
    AI::feed_key(interface_key::SELECT);

    if (amount >= 10000)
    {
        amount = 9999;
    }
    AI::feed_char('0' + char((amount / 1000) % 10));
    AI::feed_char('0' + char((amount / 100) % 10));
    AI::feed_char('0' + char((amount / 10) % 10));
    AI::feed_char('0' + char(amount % 10));
    AI::feed_key(interface_key::SELECT);
    AI::feed_key(interface_key::LEAVESCREEN);
    AI::feed_key(interface_key::LEAVESCREEN);
    ai->debug(out, stl_sprintf("add_manager_order(%d) %s", amount, AI::describe_job(world->manager_orders.back()).c_str()));
}

std::string Stocks::furniture_order(stock_item::item k)
{
    switch (k)
    {
    case stock_item::chair:
        return "ConstructThrone";
    case stock_item::traction_bench:
        return "ConstructTractionBench";
    case stock_item::weapon_rack:
        return "ConstructWeaponRack";
    case stock_item::armor_stand:
        return "ConstructArmorStand";
    case stock_item::bucket:
        return "MakeBucket";
    case stock_item::barrel:
        return "MakeBarrel";
    case stock_item::bin:
        return "ConstructBin";
    case stock_item::crutch:
        return "ConstructCrutch";
    case stock_item::splint:
        return "ConstructSplint";
    case stock_item::bag:
        return "MakeBag";
    case stock_item::block:
        return "ConstructBlocks";
    case stock_item::mechanism:
        return "ConstructMechanisms";
    case stock_item::cage:
        return "MakeCage";
    case stock_item::soap:
        return "MakeSoap";
    case stock_item::rope:
        return "MakeRope";
    case stock_item::lye:
        return "MakeLye";
    case stock_item::ash:
        return "MakeAsh";
    case stock_item::plaster_powder:
        return "MakePlasterPowder";
    case stock_item::wheelbarrow:
        return "MakeWoodenWheelbarrow";
    case stock_item::minecart:
        return "MakeWoodenMinecart";
    case stock_item::nest_box:
        return "MakeRockNestBox";
    case stock_item::hatch_cover:
        return "ConstructHatchCover";
    case stock_item::hive:
        return "MakeRockHive";
    case stock_item::jug:
        return "MakeRockJug";
    case stock_item::quiver:
        return "MakeQuiver";
    case stock_item::flask:
        return "MakeFlask";
    case stock_item::backpack:
        return "MakeBackpack";
    case stock_item::giant_corkscrew:
        return "MakeGiantCorkscrew";
    case stock_item::pipe_section:
        return "MakePipeSection";
    case stock_item::coal:
        return "MakeCharcoal";
    case stock_item::stepladder:
        return "MakeWoodenStepladder";
    case stock_item::goblet:
        return "MakeGoblet";
    case stock_item::bookcase:
        return "MakeRockBookcase";
    case stock_item::quire:
        return "MakeQuire";
    case stock_item::rock_pot:
        return "MakeRockPot";
    case stock_item::book_binding:
        return "MakeBookBinding";
    default:
        break; // TODO
    }

    std::ostringstream stringify;
    stringify << k;
    std::string str = stringify.str();
    str[0] += 'A' - 'a';
    return "Construct" + str;
}

std::function<bool(df::item *)> Stocks::furniture_find(stock_item::item k)
{
    switch (k)
    {
    case stock_item::chest:
    {
        return [](df::item *item) -> bool
        {
            df::item_boxst *i = virtual_cast<df::item_boxst>(item);
            return i && i->mat_type == 0;
        };
    }
    case stock_item::wheelbarrow:
    case stock_item::minecart:
    case stock_item::nest_box:
    case stock_item::hive:
    case stock_item::jug:
    case stock_item::stepladder:
    case stock_item::bookcase:
    case stock_item::quire:
    case stock_item::rock_pot:
    case stock_item::book_binding:
    {
        if (!manager_subtype.count(furniture_order(k)))
            return [](df::item *) -> bool { return false; };
        int32_t subtype = manager_subtype.at(furniture_order(k));
        return [subtype](df::item *item) -> bool
        {
            df::item_toolst *i = virtual_cast<df::item_toolst>(item);
            return i && i->subtype->subtype == subtype;
        };
    }
    case stock_item::mechanism:
    {
        return [](df::item *i) -> bool
        {
            return virtual_cast<df::item_trappartsst>(i);
        };
    }
    case stock_item::weapon:
    {
        return [](df::item *item) -> bool
        {
            df::item_weaponst *i = virtual_cast<df::item_weaponst>(item);
            return i && !i->subtype->flags.is_set(weapon_flags::TRAINING);
        };
    }
    case stock_item::weapon_rack:
    {
        return [](df::item *i) -> bool
        {
            return virtual_cast<df::item_weaponrackst>(i);
        };
    }
    case stock_item::armor_stand:
    {
        return [](df::item *i) -> bool
        {
            return virtual_cast<df::item_armorstandst>(i);
        };
    }
    case stock_item::traction_bench:
    {
        return [](df::item *i) -> bool
        {
            return virtual_cast<df::item_traction_benchst>(i);
        };
    }
    case stock_item::rope:
    {
        return [](df::item *i) -> bool
        {
            return i->getType() == item_type::CHAIN;
        };
    }
    case stock_item::cage:
    case stock_item::cage_metal:
    {
        return [k](df::item *i) -> bool
        {
            if (i->getType() != item_type::CAGE)
            {
                return false;
            }

            MaterialInfo mat(i);
            if ((mat.material && mat.material->flags.is_set(material_flags::IS_METAL)) != (k == stock_item::cage_metal))
            {
                return false;
            }

            for (auto ref = i->general_refs.begin(); ref != i->general_refs.end(); ref++)
            {
                if (virtual_cast<df::general_ref_contains_unitst>(*ref))
                    return false;
                if (virtual_cast<df::general_ref_contains_itemst>(*ref))
                    return false;
                df::general_ref_building_holderst *bh = virtual_cast<df::general_ref_building_holderst>(*ref);
                if (bh && virtual_cast<df::building_trapst>(bh->getBuilding()))
                    return false;
            }
            return true;
        };
    }
    default:
        break; // TODO
    }

    std::ostringstream str;
    str << "item_" << k << "st";
    virtual_identity *sym = virtual_identity::find(str.str());
    return [sym](df::item *i) -> bool
    {
        return sym->is_instance(i);
    };
}

// find one item of this type (:bed, etc)
df::item *Stocks::find_furniture_item(stock_item::item itm)
{
    std::function<bool(df::item *)> find = furniture_find(itm);
    std::string order = furniture_order(itm);
    df::items_other_id oidx = items_other_id::IN_PLAY;
    df::job_type job;
    if ((!find_enum_item(&job, order) || !find_enum_item(&oidx, ENUM_KEY_STR(item_type, ENUM_ATTR(job_type, item, job)))) && Manager.RealOrder.count(order))
    {
        order = Manager.RealOrder.at(order);
        if (find_enum_item(&job, order))
        {
            find_enum_item(&oidx, ENUM_KEY_STR(item_type, ENUM_ATTR(job_type, item, job)));
        }
    }
    for (auto it = world->items.other[oidx].begin(); it != world->items.other[oidx].end(); it++)
    {
        if (find(*it) && is_item_free(*it))
        {
            return *it;
        }
    }
    return nullptr;
}

// return nr of free items of this type
int32_t Stocks::find_furniture_itemcount(stock_item::item itm)
{
    std::function<bool(df::item *)> find = furniture_find(itm);
    std::string order = furniture_order(itm);
    df::items_other_id oidx = items_other_id::IN_PLAY;
    df::job_type job;
    if ((!find_enum_item(&job, order) || !find_enum_item(&oidx, ENUM_KEY_STR(item_type, ENUM_ATTR(job_type, item, job)))) && Manager.RealOrder.count(order))
    {
        order = Manager.RealOrder.at(order);
        if (find_enum_item(&job, order))
        {
            find_enum_item(&oidx, ENUM_KEY_STR(item_type, ENUM_ATTR(job_type, item, job)));
        }
    }
    int32_t n = 0;
    for (auto it = world->items.other[oidx].begin(); it != world->items.other[oidx].end(); it++)
    {
        if (find(*it) && is_item_free(*it))
        {
            n++;
        }
    }
    return n;
}

void Stocks::farmplot(color_ostream & out, room *r, bool initial)
{
    df::building_farmplotst *bld = virtual_cast<df::building_farmplotst>(r->dfbuilding());
    if (!bld)
        return;

    bool subterranean = Maps::getTileDesignation(r->pos())->bits.subterranean;
    df::coord2d region(Maps::getTileBiomeRgn(r->pos()));
    extern int get_biome_type(int world_coord_x, int world_coord_y);
    df::biome_type biome = subterranean ? biome_type::SUBTERRANEAN_WATER : static_cast<df::biome_type>(get_biome_type(region.x, region.y));
    df::plant_raw_flags plant_biome;
    if (!find_enum_item(&plant_biome, "BIOME_" + enum_item_key(biome)))
    {
        ai->debug(out, "[ERROR] stocks: could not find plant raw flag for biome: " + enum_item_key(biome));
        return;
    }

    std::vector<int32_t> may;
    for (int32_t i = 0; i < int32_t(world->raws.plants.all.size()); i++)
    {
        df::plant_raw *p = world->raws.plants.all[i];
        if (!p->flags.is_set(plant_biome))
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
        if (r->farm_type == farm_type::food)
        {
            for (auto i = may.begin(); i != may.end(); i++)
            {
                df::plant_raw *p = world->raws.plants.all[*i];

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
                        pids.push_back(*i);
                    }
                    continue;
                }
                if (pm.material->flags.is_set(material_flags::EDIBLE_RAW) || pm.material->flags.is_set(material_flags::EDIBLE_COOKED) || p->flags.is_set(plant_raw_flags::DRINK))
                {
                    pids.push_back(*i);
                    continue;
                }
                if (p->flags.is_set(plant_raw_flags::MILL))
                {
                    MaterialInfo mm(p->material_defs.type_mill, p->material_defs.idx_mill);
                    if (mm.material->flags.is_set(material_flags::EDIBLE_RAW) || mm.material->flags.is_set(material_flags::EDIBLE_COOKED))
                    {
                        pids.push_back(*i);
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
                            pids.push_back(*i);
                            break;
                        }
                    }
                }
            }
        }
        else if (r->farm_type == farm_type::cloth)
        {
            if (isfirst)
            {
                for (auto i = may.begin(); i != may.end(); i++)
                {
                    df::plant_raw *p = world->raws.plants.all[*i];
                    if (p->flags.is_set(df::plant_raw_flags(season)) && thread_plants.count(*i))
                    {
                        pids.push_back(*i);
                    }
                }
            }
            // only grow dyes the first field if there is no cloth crop available
            if (pids.empty())
            {
                for (auto i = may.begin(); i != may.end(); i++)
                {
                    df::plant_raw *p = world->raws.plants.all[*i];
                    if (p->flags.is_set(df::plant_raw_flags(season)) && (thread_plants.count(*i) || dye_plants.count(*i)))
                    {
                        pids.push_back(*i);
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
            int32_t ascore = plants.count(a) ? int32_t(plants.at(a)) : 0;
            int32_t bscore = plants.count(b) ? int32_t(plants.at(b)) : 0;
            if (seeds.count(a))
            {
                ascore -= int32_t(seeds.at(a));
                bscore -= int32_t(seeds.at(b));
            }
            ascore += farmplots.count(std::make_pair(season, a)) ? 3 * 3 * 2 * int32_t(farmplots.at(std::make_pair(season, a))) : 0;
            bscore += farmplots.count(std::make_pair(season, b)) ? 3 * 3 * 2 * int32_t(farmplots.at(std::make_pair(season, b))) : 0;
            return ascore < bscore;
        });

        if (pids.empty())
        {
            std::ostringstream str;
            str << r->farm_type;
            if (!isfirst && complained_about_no_plants.insert(std::make_tuple(r->farm_type, biome, season)).second)
            {
                ai->debug(out, stl_sprintf("[ERROR] stocks: no legal plants for %s farm plot (%s) for season %d", str.str().c_str(), enum_item_key_str(biome), season));
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
    for (auto item = world->items.other[items_other_id::SLAB].begin(); item != world->items.other[items_other_id::SLAB].end(); item++)
    {
        df::item_slabst *sl = virtual_cast<df::item_slabst>(*item);
        if (sl->engraving_type == slab_engraving_type::Memorial && sl->topic == histfig_id)
        {
            return;
        }
    }

    df::manager_order_template tmpl;
    tmpl.job_type = job_type::EngraveSlab;
    tmpl.item_type = item_type::NONE;
    tmpl.item_subtype = -1;
    tmpl.mat_type = 0;
    tmpl.mat_index = -1;
    tmpl.hist_figure_id = histfig_id;
    add_manager_order(out, tmpl);
}

bool Stocks::need_more(stock_item::item type)
{
    int32_t want = Watch.Needed.count(type) ? num_needed(type) : Watch.WatchStock.count(type) ? Watch.WatchStock.at(type) : 10;
    if (Watch.NeededPerDwarf.count(type))
        want += Watch.NeededPerDwarf.at(type) * int32_t(ai->pop->citizen.size()) / 100 * 9;

    return (count.count(type) ? count.at(type) : 0) < want;
}

bool Stocks::willing_to_trade_item(color_ostream & out, df::item *item)
{
    if (virtual_cast<df::item_foodst>(item))
    {
        return true;
    }

    if (item->isFoodStorage())
    {
        bool any_contents = false;

        for (auto it = item->general_refs.begin(); it != item->general_refs.end(); it++)
        {
            if ((*it)->getType() == general_ref_type::CONTAINS_ITEM)
            {
                any_contents = true;

                if (!willing_to_trade_item(out, (*it)->getItem()))
                {
                    return false;
                }
            }
        }

        return any_contents;
    }

    return false;
}

bool Stocks::want_trader_item(color_ostream &, df::item *item)
{
    if (item->hasSpecificImprovements(improvement_type::WRITING) || item->getType() == item_type::BOOK)
    {
        return true;
    }

    if (item->getType() == item_type::WOOD || item->getType() == item_type::BOULDER || item->getType() == item_type::BAR)
    {
        return true;
    }

    if (item->getType() == item_type::CLOTH || item->getType() == item_type::SKIN_TANNED || item->getType() == item_type::THREAD)
    {
        return true;
    }

    if (item->getType() == item_type::CHEESE || item->getType() == item_type::EGG || item->getType() == item_type::FISH || item->getType() == item_type::FISH_RAW || item->getType() == item_type::MEAT || item->getType() == item_type::PLANT || item->getType() == item_type::PLANT_GROWTH)
    {
        return true;
    }

    if (item->getType() == item_type::INSTRUMENT)
    {
        return true;
    }

    if (item->getType() == item_type::ANVIL && count[stock_item::anvil] == 0 && ai->plan->find_room(room_type::workshop, [](room *r) -> bool { return r->workshop_type == workshop_type::MetalsmithsForge && r->status != room_status::plan && !r->dfbuilding(); }))
    {
        return true;
    }

    return false;
}

bool Stocks::want_trader_item_more(df::item *a, df::item *b)
{
    if (a->getType() == item_type::WOOD && b->getType() != item_type::WOOD)
    {
        return true;
    }
    else if (b->getType() == item_type::WOOD && a->getType() != item_type::WOOD)
    {
        return false;
    }

    if (a->getType() == item_type::ANVIL && b->getType() != item_type::ANVIL)
    {
        return true;
    }
    else if (b->getType() == item_type::ANVIL && a->getType() != item_type::ANVIL)
    {
        return false;
    }

    if ((a->hasSpecificImprovements(improvement_type::WRITING) || a->getType() == item_type::BOOK) && !(b->hasSpecificImprovements(improvement_type::WRITING) || b->getType() == item_type::BOOK))
    {
        return true;
    }
    else if ((b->hasSpecificImprovements(improvement_type::WRITING) || b->getType() == item_type::BOOK) && !(a->hasSpecificImprovements(improvement_type::WRITING) || a->getType() == item_type::BOOK))
    {
        return false;
    }

    if (a->getType() == item_type::INSTRUMENT && b->getType() != item_type::INSTRUMENT)
    {
        return true;
    }
    else if (b->getType() == item_type::INSTRUMENT && a->getType() != item_type::INSTRUMENT)
    {
        return false;
    }

    return false;
}
