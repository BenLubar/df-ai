#include "ai.h"
#include "stocks.h"
#include "population.h"

#include "modules/Materials.h"

#include "df/creature_raw.h"
#include "df/inorganic_raw.h"
#include "df/item_slabst.h"
#include "df/manager_order.h"
#include "df/manager_order_template.h"
#include "df/matter_state.h"
#include "df/ui.h"
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

Watch::Watch()
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
    WatchStock[stock_item::food_ingredients] = 4;
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
    AlsoCount.insert(stock_item::dye_plant);
    AlsoCount.insert(stock_item::leather);
    AlsoCount.insert(stock_item::slurry_plant);
    AlsoCount.insert(stock_item::statue);
    AlsoCount.insert(stock_item::stone);
    AlsoCount.insert(stock_item::thread);
}

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
        df::coord fe = ai->fort_entrance_pos();
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
    auto compute_max = [](int32_t n) -> int32_t
    {
        int32_t max = 1;
        while (max <= n)
        {
            if (max * 2 > n)
            {
                return max * 2;
            }
            if (max * 5 > n)
            {
                return max * 5;
            }
            max *= 10;
        }
        return max;
    };

    if (html)
    {
        out << "<h2 id=\"Stocks_Need\">Need</h2><table><thead><tr><th>item</th><th>Current</th><th>Minimum</th><th></th></tr></thead><tbody>";
    }
    else
    {
        out << "## Need\n";
    }
    for (auto n : Watch.Needed)
    {
        int32_t needed = num_needed(n.first);
        if (html)
        {
            int32_t max = compute_max(std::max(needed, count[n.first]));
            out << "<tr><th>" << n.first << "</th><td>" << count[n.first] << "</td><td>" << needed << "</td><td><meter value=\"" << count[n.first] << "\" low=\"" << (needed - 1) << "\" optimum=\"" << needed << "\" max=\"" << max << "\"></meter></td></tr>";
        }
        else
        {
            out << "- " << n.first << ": " << count[n.first] << " / " << needed << "\n";
        }
    }

    if (html)
    {
        out << "</tbody></table><h2 id=\"Stocks_Watch\">Watch</h2><table><thead><tr><th>item</th><th>Current</th><th>Maximum</th><th></th></tr></thead><tbody>";
    }
    else
    {
        out << "\n## Watch\n";
    }
    for (auto w : Watch.WatchStock)
    {
        if (html)
        {
            int32_t max = compute_max(std::max(w.second, count[w.first]));
            out << "<tr><th>" << w.first << "</th><td>" << count[w.first] << "</td><td>" << w.second << "</td><td><meter value=\"" << count[w.first] << "\" high=\"" << (w.second + 1) << "\" optimum=\"" << w.second << "\" max=\"" << max << "\"></meter></td></tr>";
        }
        else
        {
            out << "- " << w.first << ": " << count[w.first] << " / " << w.second << "\n";
        }
    }

    if (html)
    {
        out << "</tbody></table><h2 id=\"Stocks_Track\">Track</h2><table><thead><tr><th>Item</th><th>Current</th></tr></thead><tbody>";
    }
    else
    {
        out << "\n## Track\n";
    }
    for (auto t : Watch.AlsoCount)
    {
        if (html)
        {
            out << "<tr><th>" << t << "</th><td>" << count[t] << "</td></tr>";
        }
        else
        {
            out << "- " << t << ": " << count[t] << "\n";
        }
    }

    if (html)
    {
        out << "</tbody></table><h2 id=\"Stocks_Ingots\">Ingots</h2><table><thead><tr><th>Metal</th><th>Current</th></tr></thead><tbody>";
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
            out << "<tr><th>" << html_escape(mat->material.state_name[matter_state::Solid]) << "</th><td>" << t.second << "</td></tr>";
        }
        else
        {
            out << "- " << mat->material.state_name[matter_state::Solid] << ": " << t.second << "\n";
        }
    }

    if (html)
    {
        out << "</tbody></table><h2 id=\"Stocks_Orders\">Orders</h2><table><thead><tr><th>Order</th><th>Remaining</th><th>Total</th><th></th></tr></thead><tbody>";
    }
    else
    {
        out << "\n## Orders\n";
    }
    for (auto mo : world->manager_orders)
    {
        if (html)
        {
            out << "<tr><th>" << html_escape(AI::describe_job(mo)) << "</th><td>" << mo->amount_left << "</td><td>" << mo->amount_total << "</td>";
            if (!mo->status.bits.validated && mo->amount_left == mo->amount_total)
            {
                out << "<td><progress max=\"" << mo->amount_total << "\"></progress></td></tr>";
            }
            else
            {
                out << "<td><progress max=\"" << mo->amount_total << "\" value=\"" << (mo->amount_total - mo->amount_left) << "\"></progress></td></tr>";
            }
        }
        else
        {
            out << "- ";
            out << stl_sprintf("% 4d /% 4d ", mo->amount_left, mo->amount_total);
            out << AI::describe_job(mo) << "\n";
        }
    }
    if (html)
    {
        out << "</tbody></table>";
    }
    else
    {
        out << "\n";
    }
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

void Stocks::queue_slab(color_ostream & out, int32_t histfig_id)
{
    for (auto item : world->items.other[items_other_id::SLAB])
    {
        df::item_slabst *sl = virtual_cast<df::item_slabst>(item);
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
