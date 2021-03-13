#include "ai.h"
#include "stocks.h"
#include "population.h"

#include "modules/Items.h"
#include "modules/Materials.h"

#include "df/creature_raw.h"
#include "df/inorganic_raw.h"
#include "df/item_armorst.h"
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

struct Watch Watch;

void Watch::reset()
{
    Needed.clear();
    NeededPerDwarf.clear();
    WatchStock.clear();
    AlsoCount.clear();

    Needed[stock_item::ammo_combat] = 50;
    Needed[stock_item::ammo_training] = 50;
    Needed[stock_item::anvil] = 1;
    Needed[stock_item::armor_feet] = 4;
    Needed[stock_item::armor_hands] = 4;
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
    Needed[stock_item::bone] = 2;
    Needed[stock_item::book_binding] = 1;
    Needed[stock_item::bookcase] = 2;
    Needed[stock_item::bucket] = 2;
    Needed[stock_item::cabinet] = 4;
    Needed[stock_item::cage] = 3;
    Needed[stock_item::cage_metal] = 1;
    Needed[stock_item::chair] = 3;
    Needed[stock_item::chest] = 4;
    Needed[stock_item::clothes_feet] = 4;
    Needed[stock_item::clothes_hands] = 4;
    Needed[stock_item::clothes_head] = 2;
    Needed[stock_item::clothes_legs] = 2;
    Needed[stock_item::clothes_torso] = 2;
    Needed[stock_item::coal] = 12;
    Needed[stock_item::coffin] = 2;
    Needed[stock_item::crutch] = 1;
    Needed[stock_item::die] = 3;
    Needed[stock_item::door] = 4;
    Needed[stock_item::drink] = 50;
    Needed[stock_item::dye] = 10;
    Needed[stock_item::dye_seeds] = 10;
    Needed[stock_item::flask] = 2;
    Needed[stock_item::floodgate] = 1;
    Needed[stock_item::food_storage] = 4;
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
    Needed[stock_item::offering_place] = 1;
    Needed[stock_item::paper] = 5;
    Needed[stock_item::pedestal] = 1;
    Needed[stock_item::pick] = 2;
    Needed[stock_item::pipe_section] = 1;
    Needed[stock_item::plaster_powder] = 1;
    Needed[stock_item::quern] = 3;
    Needed[stock_item::quire] = 5;
    Needed[stock_item::quiver] = 2;
    Needed[stock_item::raw_coke] = 1;
    Needed[stock_item::rope] = 1;
    Needed[stock_item::screw] = 1;
    Needed[stock_item::slab] = 1;
    Needed[stock_item::slurry] = 5;
    Needed[stock_item::soap] = 1;
    Needed[stock_item::splint] = 1;
    Needed[stock_item::stepladder] = 2;
    Needed[stock_item::table] = 3;
    Needed[stock_item::thread_seeds] = 10;
    Needed[stock_item::toy] = 2;
    Needed[stock_item::traction_bench] = 1;
    Needed[stock_item::weapon_melee] = 2;
    Needed[stock_item::weapon_rack] = 1;
    Needed[stock_item::weapon_ranged] = 2;
    Needed[stock_item::wheelbarrow] = 1;
    Needed[stock_item::wood] = 16;

    NeededPerDwarf[stock_item::ammo_combat] = 250;
    NeededPerDwarf[stock_item::ammo_training] = 250;
    NeededPerDwarf[stock_item::armor_feet] = 6;
    NeededPerDwarf[stock_item::armor_hands] = 6;
    NeededPerDwarf[stock_item::armor_head] = 3;
    NeededPerDwarf[stock_item::armor_legs] = 3;
    NeededPerDwarf[stock_item::armor_shield] = 3;
    NeededPerDwarf[stock_item::armor_torso] = 3;
    NeededPerDwarf[stock_item::cloth] = 20;
    NeededPerDwarf[stock_item::clothes_feet] = 40;
    NeededPerDwarf[stock_item::clothes_hands] = 40;
    NeededPerDwarf[stock_item::clothes_head] = 20;
    NeededPerDwarf[stock_item::clothes_legs] = 20;
    NeededPerDwarf[stock_item::clothes_torso] = 20;
    NeededPerDwarf[stock_item::drink] = 200;
    NeededPerDwarf[stock_item::meal] = 100;
    NeededPerDwarf[stock_item::slab] = 10;
    NeededPerDwarf[stock_item::soap] = 20;
    NeededPerDwarf[stock_item::toy] = 5;
    NeededPerDwarf[stock_item::weapon_melee] = 5;
    NeededPerDwarf[stock_item::weapon_ranged] = 5;

    WatchStock[stock_item::bag_plant] = 4;
    WatchStock[stock_item::bone] = 8;
    WatchStock[stock_item::clay] = 1;
    WatchStock[stock_item::cloth_nodye] = 10;
    WatchStock[stock_item::drink_fruit] = 5;
    WatchStock[stock_item::drink_plant] = 5;
    WatchStock[stock_item::food_ingredients] = 4;
    WatchStock[stock_item::goblinite] = 0;
    WatchStock[stock_item::honey] = 0;
    WatchStock[stock_item::honeycomb] = 0;
    WatchStock[stock_item::metal_ore] = 6;
    WatchStock[stock_item::metal_strand] = 0;
    WatchStock[stock_item::milk] = 0;
    WatchStock[stock_item::mill_plant] = 4;
    WatchStock[stock_item::raw_coke] = 2;
    WatchStock[stock_item::raw_fish] = 0;
    WatchStock[stock_item::rough_gem] = 6;
    WatchStock[stock_item::shell] = 1;
    WatchStock[stock_item::skull] = 2;
    WatchStock[stock_item::tallow] = 1;
    WatchStock[stock_item::thread_plant] = 10;
    WatchStock[stock_item::wool] = 0;
    WatchStock[stock_item::written_on_quire] = 0;

    AlsoCount.insert(stock_item::cloth);
    AlsoCount.insert(stock_item::dye_plant);
    AlsoCount.insert(stock_item::leather);
    AlsoCount.insert(stock_item::slurry_plant);
    AlsoCount.insert(stock_item::statue);
    AlsoCount.insert(stock_item::stone);
    AlsoCount.insert(stock_item::thread);
}

Json::Value Watch::to_json()
{
    Json::Value map(Json::objectValue);
    for (auto & needed : Needed)
    {
        std::ostringstream str;
        str << needed.first;
        map[str.str()] = Json::Value(Json::objectValue);
        map[str.str()]["needed"] = needed.second;
        if (NeededPerDwarf.count(needed.first))
        {
            map[str.str()]["per_cent"] = NeededPerDwarf.at(needed.first);
        }
        if (WatchStock.count(needed.first))
        {
            map[str.str()]["maximum"] = WatchStock.at(needed.first);
        }
    }
    for (auto & watch : WatchStock)
    {
        if (Needed.count(watch.first))
        {
            continue;
        }
        std::ostringstream str;
        str << watch.first;
        map[str.str()] = Json::Value(Json::objectValue);
        map[str.str()]["maximum"] = watch.second;
    }
    for (auto track : AlsoCount)
    {
        std::ostringstream str;
        str << track;
        map[str.str()] = Json::Value(Json::objectValue);
        map[str.str()]["track_only"] = true;
    }
    return map;
}

bool Watch::from_json(Json::Value & map, std::string & error)
{
    if (!map.isObject())
    {
        error = "stock_goals must be an object";
        return false;
    }

    reset();

    std::map<std::string, stock_item::item> item_names;
    for (auto item = stock_item::item(0); item < stock_item::_stock_item_count; item = stock_item::item(item + 1))
    {
        std::ostringstream str;
        str << item;
        item_names[str.str()] = item;
    }

    for (auto & member : map.getMemberNames())
    {
        if (!item_names.count(member))
        {
            error = "unexpected stock_goals item name '" + member + "'";
            return false;
        }

        auto item = item_names.at(member);

        Json::Value & v = map[member];
        size_t count = v.size();
        if (count == 1 && v.isMember("track_only") && v["track_only"].isBool() && v["track_only"].asBool())
        {
            Needed.erase(item);
            NeededPerDwarf.erase(item);
            WatchStock.erase(item);
            AlsoCount.insert(item);
            continue;
        }

        if (count == 3 && v.isMember("needed") && v["needed"].isIntegral() && v.isMember("per_cent") && v["per_cent"].isIntegral() && v.isMember("maximum") && v["maximum"].isIntegral())
        {
            Needed[item] = v["needed"].asInt();
            NeededPerDwarf[item] = v["per_cent"].asInt();
            WatchStock[item] = v["maximum"].asInt();
            AlsoCount.erase(item);
            continue;
        }

        if (count == 2 && v.isMember("needed") && v["needed"].isIntegral() && v.isMember("maximum") && v["maximum"].isIntegral())
        {
            Needed[item] = v["needed"].asInt();
            NeededPerDwarf.erase(item);
            WatchStock[item] = v["maximum"].asInt();
            AlsoCount.erase(item);
            continue;
        }

        if (count == 2 && v.isMember("needed") && v["needed"].isIntegral() && v.isMember("per_cent") && v["per_cent"].isIntegral())
        {
            Needed[item] = v["needed"].asInt();
            NeededPerDwarf[item] = v["per_cent"].asInt();
            WatchStock.erase(item);
            AlsoCount.erase(item);
            continue;
        }

        if (count == 1 && v.isMember("needed") && v["needed"].isIntegral())
        {
            Needed[item] = v["needed"].asInt();
            NeededPerDwarf.erase(item);
            WatchStock.erase(item);
            AlsoCount.erase(item);
            continue;
        }

        if (count == 1 && v.isMember("maximum") && v["maximum"].isIntegral())
        {
            Needed.erase(item);
            NeededPerDwarf.erase(item);
            WatchStock[item] = v["maximum"].asInt();
            AlsoCount.erase(item);
            continue;
        }

        error = "unexpected format for stock_goals item '" + member + "'";
        return false;
    }

    return true;
}

Stocks::Stocks(AI & ai) :
    ai(ai),
    count_free(),
    count_total(),
    count_subtype(),
    act_reason(),
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
    last_treelist([&ai](df::coord a, df::coord b) -> bool
    {
        df::coord fe = ai.fort_entrance_pos();
        int16_t ascore = (a.x - fe.x) * (a.x - fe.x) + (a.y - fe.y) * (a.y - fe.y) + (a.z - fe.z) * (a.z - fe.z) * 16;
        int16_t bscore = (b.x - fe.x) * (b.x - fe.x) + (b.y - fe.y) * (b.y - fe.y) + (b.z - fe.z) * (b.z - fe.z) * 16;
        if (ascore < bscore)
            return true;
        if (ascore > bscore)
            return false;
        return a < b;
    }),
    last_cutpos(),
    cut_wait_counter(0),
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
    complained_about_no_plants(),
    cant_pickaxe(false)
{
    last_cutpos.clear();
}

Stocks::~Stocks()
{
}

void Stocks::reset()
{
    updating.clear();
    lastupdating = 0;
    count_free.clear();
    count_total.clear();
    count_subtype.clear();
    act_reason.clear();
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
    onupdate_handle = events.onupdate_register("df-ai stocks", 600, 30, [this](color_ostream & out) { update(out); });
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
        int32_t have = count_free[it->first];

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
        int32_t have = count_free[it->first];
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

static std::string compact_newlines(std::string str)
{
    size_t pos = 0;
    while ((pos = str.find('\n', pos)) != std::string::npos)
    {
        if (pos == 0 || pos + 1 == str.size() || str.at(pos + 1) == '\n')
        {
            str.erase(pos);
        }
        else
        {
            pos++;
        }
    }

    return str;
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
        out << "<h2 id=\"Stocks_Need\">Need</h2><table><thead><tr><th colspan=\"2\">Item</th><th>Available</th><th>Minimum</th><th>Total</th><th></th><th>Status</th></tr></thead><tbody>";
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
            if (count_subtype.count(n.first))
            {
                const auto & subtypes = count_subtype.at(n.first);
                df::item_type item_type = ENUM_ATTR(items_other_id, item, find_item_helper(n.first).oidx);
                size_t count = subtypes.size();
                for (auto subtype : subtypes)
                {
                    std::string name(ItemTypeInfo(item_type, subtype.first).toString());
                    int32_t max = compute_max(std::max(needed, subtype.second.first));
                    out << "<tr><th>" << n.first << "</th><td>" << name << "</td><td class=\"num\">" << subtype.second.first << "</td><td class=\"num\">" << needed << "</td><td class=\"num\">" << subtype.second.second << "</td><td><meter value=\"" << subtype.second.first << "\" low=\"" << (needed / 2) << "\" high=\"" << needed << "\" optimum=\"" << (needed + 1) << "\" max=\"" << max << "\"></meter></td>";
                    if (count)
                    {
                        out << "<td rowspan=\"" << count << "\"><i>" << html_escape(compact_newlines(act_reason[n.first].str())) << "</i></td>";
                        count = 0;
                    }
                    out << "</tr>";
                }
            }
            else
            {
                int32_t max = compute_max(std::max(needed, count_free[n.first]));
                out << "<tr><th>" << n.first << "</th><td>-</td><td class=\"num\">" << count_free[n.first] << "</td><td class=\"num\">" << needed << "</td><td class=\"num\">" << count_total[n.first] << "</td><td><meter value=\"" << count_free[n.first] << "\" low=\"" << (needed / 2) << "\" high=\"" << needed << "\" optimum=\"" << (needed + 1) << "\" max=\"" << max << "\"></meter></td><td><i>" << html_escape(compact_newlines(act_reason[n.first].str())) << "</i></td></tr>";
            }
        }
        else
        {
            out << "- " << n.first << ": " << count_free[n.first] << " / " << needed << "\n";
        }
    }

    if (html)
    {
        out << "</tbody></table><h2 id=\"Stocks_Watch\">Watch</h2><table><thead><tr><th>Item</th><th>Available</th><th>Maximum</th><th>Total</th><th></th><th>Status</th></tr></thead><tbody>";
    }
    else
    {
        out << "\n## Watch\n";
    }
    for (auto w : Watch.WatchStock)
    {
        if (html)
        {
            int32_t max = compute_max(std::max(w.second, count_free[w.first]));
            out << "<tr><th>" << w.first << "</th><td class=\"num\">" << count_free[w.first] << "</td><td>" << w.second << "</td><td class=\"num\">" << count_total[w.first] << "</td><td><meter value=\"" << count_free[w.first] << "\" low=\"" << w.second << "\" high=\"" << (w.second * 2) << "\" optimum=\"" << (w.second - 1) << "\" max=\"" << max << "\"></meter></td><td><i>" << html_escape(compact_newlines(act_reason[w.first].str())) << "</i></td></tr>";
        }
        else
        {
            out << "- " << w.first << ": " << count_free[w.first] << " / " << w.second << "\n";
        }
    }

    if (html)
    {
        out << "</tbody></table><h2 id=\"Stocks_Track\">Track</h2><table><thead><tr><th>Item</th><th>Available</th><th>Total</th></tr></thead><tbody>";
    }
    else
    {
        out << "\n## Track\n";
    }
    for (auto t : Watch.AlsoCount)
    {
        if (html)
        {
            out << "<tr><th>" << t << "</th><td class=\"num\">" << count_free[t] << "</td><td class=\"num\">" << count_total[t] << "</td></tr>";
        }
        else
        {
            out << "- " << t << ": " << count_free[t] << "\n";
        }
    }

    if (html)
    {
        out << "</tbody></table><h2 id=\"Stocks_Ingots\">Ingots</h2><table><thead><tr><th>Metal</th><th>Current</th><th>Potential</th></tr></thead><tbody>";
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
            std::ofstream discard;
            color_ostream_wrapper color_discard(discard);
            int32_t may_forge = may_forge_bars(color_discard, t.first, discard, 1, true) / 150;
            out << "<tr><th>" << html_escape(mat->material.state_name[matter_state::Solid]) << "</th><td class=\"num\">" << t.second << "</td><td class=\"num\">" << may_forge << "</td></tr>";
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
            out << "<tr><th>" << html_escape(AI::describe_job(mo)) << "</th><td class=\"num\">" << mo->amount_left << "</td><td class=\"num\">" << mo->amount_total << "</td>";
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
        assert(int32_t(i) == p->material_defs.idx[plant_material_def::basic_mat]);
        MaterialInfo basic(p->material_defs.type[plant_material_def::basic_mat], p->material_defs.idx[plant_material_def::basic_mat]);
        if (p->flags.is_set(plant_raw_flags::THREAD))
        {
            thread_plants[i] = basic.type;
        }
        if (p->flags.is_set(plant_raw_flags::MILL))
        {
            mill_plants[i] = basic.type;
            assert(int32_t(i) == p->material_defs.idx[plant_material_def::mill]);
            MaterialInfo mill(p->material_defs.type[plant_material_def::mill], p->material_defs.idx[plant_material_def::mill]);
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
        for (auto bar : bars)
        {
            simple_metal_ores.at(bar).insert(it - world->raws.inorganics.begin());
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

    return (count_free.count(type) ? count_free.at(type) : 0) < want;
}
