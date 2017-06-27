#include "ai.h"
#include "camera.h"
#include "plan.h"
#include "population.h"
#include "stocks.h"

#include <cstdio>
#include <sstream>
#include <unordered_map>
#include <fstream>

#include "jsoncpp.h"

#include "modules/Buildings.h"
#include "modules/Gui.h"
#include "modules/Items.h"
#include "modules/Job.h"
#include "modules/Maps.h"
#include "modules/Materials.h"
#include "modules/Screen.h"
#include "modules/Translation.h"
#include "modules/Units.h"
#include "modules/World.h"

#include "df/abstract_building.h"
#include "df/block_square_event_material_spatterst.h"
#include "df/block_square_event_mineralst.h"
#include "df/building_archerytargetst.h"
#include "df/building_civzonest.h"
#include "df/building_coffinst.h"
#include "df/building_def.h"
#include "df/building_def_furnacest.h"
#include "df/building_def_item.h"
#include "df/building_def_workshopst.h"
#include "df/building_doorst.h"
#include "df/building_floodgatest.h"
#include "df/building_furnacest.h"
#include "df/building_squad_use.h"
#include "df/building_tablest.h"
#include "df/building_trapst.h"
#include "df/building_wagonst.h"
#include "df/building_workshopst.h"
#include "df/buildings_other_id.h"
#include "df/builtin_mats.h"
#include "df/caste_raw.h"
#include "df/creature_raw.h"
#include "df/engraving.h"
#include "df/entity_position.h"
#include "df/feature_init_outdoor_riverst.h"
#include "df/feature_outdoor_riverst.h"
#include "df/furniture_type.h"
#include "df/general_ref_building_holderst.h"
#include "df/general_ref_building_triggertargetst.h"
#include "df/item_boulderst.h"
#include "df/item_toolst.h"
#include "df/itemdef_toolst.h"
#include "df/items_other_id.h"
#include "df/job.h"
#include "df/job_item.h"
#include "df/map_block.h"
#include "df/organic_mat_category.h"
#include "df/plant.h"
#include "df/plant_tree_info.h"
#include "df/plant_tree_tile.h"
#include "df/squad.h"
#include "df/stop_depart_condition.h"
#include "df/trap_type.h"
#include "df/ui.h"
#include "df/unit.h"
#include "df/vehicle.h"
#include "df/viewscreen_layer_stockpilest.h"
#include "df/world.h"
#include "df/world_site.h"
#include "df/world_underground_region.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cursor);
REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

const size_t dwarves_per_table = 3; // number of dwarves per dininghall table/chair
const int32_t dwarves_per_farmtile_num = 3; // number of dwarves per farmplot tile
const int32_t dwarves_per_farmtile_den = 2;
const size_t wantdig_max = 2; // dig at most this much wantdig rooms at a time
const int32_t spare_bedroom = 3; // dig this much free bedroom in advance when idle
const int32_t extra_farms = 2; // built after utilities are finished

farm_allowed_materials_t::farm_allowed_materials_t()
{
    set.insert(tiletype_material::GRASS_DARK);
    set.insert(tiletype_material::GRASS_LIGHT);
    set.insert(tiletype_material::GRASS_DRY);
    set.insert(tiletype_material::GRASS_DEAD);
    set.insert(tiletype_material::SOIL);
    set.insert(tiletype_material::PLANT);
    set.insert(tiletype_material::TREE);
}

farm_allowed_materials_t farm_allowed_materials;

std::ostream & operator <<(std::ostream & stream, task_type::type type)
{
    switch (type)
    {
    case task_type::check_construct:
        return stream << "check_construct";
    case task_type::check_furnish:
        return stream << "check_furnish";
    case task_type::check_idle:
        return stream << "check_idle";
    case task_type::check_rooms:
        return stream << "check_rooms";
    case task_type::construct_activityzone:
        return stream << "construct_activityzone";
    case task_type::construct_furnace:
        return stream << "construct_furnace";
    case task_type::construct_stockpile:
        return stream << "construct_stockpile";
    case task_type::construct_tradedepot:
        return stream << "construct_tradedepot";
    case task_type::construct_workshop:
        return stream << "construct_workshop";
    case task_type::dig_cistern:
        return stream << "dig_cistern";
    case task_type::dig_garbage:
        return stream << "dig_garbage";
    case task_type::dig_room:
        return stream << "dig_room";
    case task_type::furnish:
        return stream << "furnish";
    case task_type::monitor_cistern:
        return stream << "monitor_cistern";
    case task_type::setup_farmplot:
        return stream << "setup_farmplot";
    case task_type::want_dig:
        return stream << "want_dig";

    case task_type::_task_type_count:
        return stream << "???";
    }
    return stream << "???";
}


Plan::Plan(AI *ai) :
    ai(ai),
    onupdate_handle(nullptr),
    nrdig(),
    tasks_generic(),
    tasks_furniture(),
    bg_idx_generic(tasks_generic.end()),
    bg_idx_furniture(tasks_furniture.end()),
    rooms(),
    room_category(),
    room_by_z(),
    corridors(),
    cache_nofurnish(),
    fort_entrance(nullptr),
    map_veins(),
    important_workshops(),
    important_workshops2(),
    important_workshops3(),
    m_c_lever_in(nullptr),
    m_c_lever_out(nullptr),
    m_c_cistern(nullptr),
    m_c_reserve(nullptr),
    m_c_testgate_delay(-1),
    checkroom_idx(0),
    trycistern_count(0),
    map_vein_queue(),
    dug_veins(),
    noblesuite(-1),
    cavern_max_level(-1),
    last_idle_year(-1),
    allow_ice(false),
    should_search_for_metal(false),
    past_initial_phase(false),
    cistern_channel_requested(false)
{
    tasks_generic.push_back(new task(task_type::check_rooms));

    important_workshops.push_back(workshop_type::Butchers);
    important_workshops.push_back(workshop_type::Quern);
    important_workshops.push_back(workshop_type::Farmers);
    important_workshops.push_back(workshop_type::Mechanics);
    important_workshops.push_back(workshop_type::Still);

    important_workshops2.push_back(furnace_type::Smelter);
    important_workshops2.push_back(furnace_type::WoodFurnace);

    important_workshops3.push_back(workshop_type::Loom);
    important_workshops3.push_back(workshop_type::Craftsdwarfs);
    important_workshops3.push_back(workshop_type::Tanners);
    important_workshops3.push_back(workshop_type::Kitchen);

    categorize_all();
}

Plan::~Plan()
{
    for (auto it = tasks_generic.begin(); it != tasks_generic.end(); it++)
    {
        delete *it;
    }
    for (auto it = tasks_furniture.begin(); it != tasks_furniture.end(); it++)
    {
        delete *it;
    }
    for (auto it = rooms.begin(); it != rooms.end(); it++)
    {
        delete *it;
    }
    for (auto it = corridors.begin(); it != corridors.end(); it++)
    {
        delete *it;
    }
}

static bool find_item(df::items_other_id idx, df::item *&item, bool fire_safe = false, bool non_economic = false)
{
    for (auto it = world->items.other[idx].begin(); it != world->items.other[idx].end(); it++)
    {
        df::item *i = *it;
        if (Stocks::is_item_free(i) &&
            (!fire_safe || i->isTemperatureSafe(1)) &&
            (!non_economic || virtual_cast<df::item_boulderst>(i)->mat_type != 0 || !ui->economic_stone[virtual_cast<df::item_boulderst>(i)->mat_index]))
        {
            item = i;
            return true;
        }
    }
    return false;
}

static bool find_items(df::items_other_id idx, std::vector<df::item *> & items, size_t n, bool fire_safe = false, bool non_economic = false)
{
    size_t j = 0;
    for (auto it = world->items.other[idx].begin(); it != world->items.other[idx].end(); it++)
    {
        df::item *i = *it;
        if (Stocks::is_item_free(i) &&
            (!fire_safe || i->isTemperatureSafe(1)) &&
            (!non_economic || virtual_cast<df::item_boulderst>(i)->mat_type != 0 || !ui->economic_stone[virtual_cast<df::item_boulderst>(i)->mat_index]))
        {
            items.push_back(i);
            j++;
            if (j == n)
                return true;
        }
    }
    return false;
}

template<typename T>
static df::job_item *make_job_item(T *t)
{
    df::job_item *item = new df::job_item();
    item->item_type = t->item_type;
    item->item_subtype = t->item_subtype;
    item->mat_type = t->mat_type;
    item->mat_index = t->mat_index;
    item->reaction_class = t->reaction_class;
    item->has_material_reaction_product = t->has_material_reaction_product;
    item->flags1.whole = t->flags1.whole;
    item->flags2.whole = t->flags2.whole;
    item->flags3.whole = t->flags3.whole;
    item->flags4 = t->flags4;
    item->flags5 = t->flags5;
    item->metal_ore = t->metal_ore;
    item->min_dimension = t->min_dimension;
    item->quantity = t->quantity;
    item->has_tool_use = t->has_tool_use;
    return item;
}

// Not perfect, but it should at least cut down on cancellation spam.
static bool find_items(const std::vector<df::job_item *> & filters, std::vector<df::item *> & items)
{
    for (auto filter = filters.begin(); filter != filters.end(); filter++)
    {
        int32_t found = 0;

        for (auto it = world->items.all.begin(); it != world->items.all.end(); it++)
        {
            if (std::find(items.begin(), items.end(), *it) != items.end())
            {
                continue;
            }

            ItemTypeInfo iinfo(*it);
            MaterialInfo minfo(*it);
            if (!iinfo.matches(**filter, &minfo, true))
            {
                continue;
            }

            items.push_back(*it);
            found++;

            if ((*filter)->quantity >= found)
            {
                break;
            }
        }

        if ((*filter)->quantity < found)
        {
            return false;
        }
    }
    return true;
}

command_result Plan::startup(color_ostream & out)
{
    std::ifstream persist(("data/save/" + World::ReadWorldFolder() + "/df-ai-plan.dat").c_str());
    if (persist.good())
    {
        load(persist);
        return CR_OK;
    }

    command_result res = setup_blueprint(out);
    if (res != CR_OK)
        return res;

    categorize_all();

    return setup_ready(out);
}

command_result Plan::onupdate_register(color_ostream &)
{
    onupdate_handle = events.onupdate_register("df-ai plan", 240, 20, [this](color_ostream & out) { update(out); });
    return CR_OK;
}

command_result Plan::onupdate_unregister(color_ostream &)
{
    events.onupdate_unregister(onupdate_handle);
    return CR_OK;
}

static bool want_reupdate = false;

void Plan::update(color_ostream &)
{
    if (bg_idx_generic == tasks_generic.end())
    {
        bg_idx_generic = tasks_generic.begin();

        nrdig.clear();
        for (auto it = tasks_generic.begin(); it != tasks_generic.end(); it++)
        {
            task *t = *it;
            if (t->type != task_type::dig_room || (t->r->type == room_type::corridor && (t->r->corridor_type == corridor_type::veinshaft || t->r->corridor_type == corridor_type::outpost)))
                continue;
            df::coord size = t->r->size();
            if (t->r->type != room_type::corridor || size.z > 1)
                nrdig[t->r->queue]++;
            if (t->r->type != room_type::corridor && size.x * size.y * size.z >= 10)
                nrdig[t->r->queue]++;
        }

        want_reupdate = false;
        events.onupdate_register_once("df-ai plan bg generic", [this](color_ostream & out) -> bool
        {
            if (!Core::getInstance().isMapLoaded())
            {
                return true;
            }

            if (bg_idx_generic == tasks_generic.end())
            {
                if (want_reupdate)
                {
                    update(out);
                }
                return true;
            }
            task & t = **bg_idx_generic;

            bool del = false;
            switch (t.type)
            {
            case task_type::want_dig:
                if (t.r->is_dug() || nrdig[t.r->queue] < wantdig_max)
                {
                    digroom(out, t.r);
                    del = true;
                }
                break;
            case task_type::dig_room:
                fixup_open(out, t.r);
                if (t.r->is_dug())
                {
                    t.r->status = room_status::dug;
                    construct_room(out, t.r);
                    want_reupdate = true; // wantdig asap
                    del = true;
                }
                else
                {
                    t.r->dig();
                }
                break;
            case task_type::construct_tradedepot:
                del = try_construct_tradedepot(out, t.r);
                break;
            case task_type::construct_workshop:
                del = try_construct_workshop(out, t.r);
                break;
            case task_type::construct_furnace:
                del = try_construct_furnace(out, t.r);
                break;
            case task_type::construct_stockpile:
                del = try_construct_stockpile(out, t.r);
                break;
            case task_type::construct_activityzone:
                del = try_construct_activityzone(out, t.r);
                break;
            case task_type::setup_farmplot:
                del = try_setup_farmplot(out, t.r);
                break;
            case task_type::furnish:
                break;
            case task_type::check_furnish:
                break;
            case task_type::check_construct:
                del = try_endconstruct(out, t.r);
                break;
            case task_type::dig_cistern:
                del = try_digcistern(out, t.r);
                break;
            case task_type::dig_garbage:
                del = true;
                break;
            case task_type::check_idle:
                del = checkidle(out);
                break;
            case task_type::check_rooms:
                checkrooms(out);
                break;
            case task_type::monitor_cistern:
                monitor_cistern(out);
                break;
            case task_type::_task_type_count:
                break;
            }

            if (del)
            {
                delete *bg_idx_generic;
                tasks_generic.erase(bg_idx_generic++);
            }
            else
            {
                bg_idx_generic++;
            }
            return false;
        });
    }
    if (bg_idx_furniture == tasks_furniture.end())
    {
        bg_idx_furniture = tasks_furniture.begin();

        cache_nofurnish.clear();

        events.onupdate_register_once("df-ai plan bg furniture", [this](color_ostream & out) -> bool
        {
            if (!Core::getInstance().isMapLoaded())
            {
                return true;
            }

            if (bg_idx_furniture == tasks_furniture.end())
            {
                return true;
            }
            task & t = **bg_idx_furniture;

            bool del = false;
            switch (t.type)
            {
            case task_type::furnish:
                del = try_furnish(out, t.r, t.f);
                break;
            case task_type::check_furnish:
                del = try_endfurnish(out, t.r, t.f);
                break;
            default:
                break;
            }

            if (del)
            {
                delete *bg_idx_furniture;
                tasks_furniture.erase(bg_idx_furniture++);
            }
            else
            {
                bg_idx_furniture++;
            }
            return false;
        });
    }
}

command_result Plan::persist(color_ostream &)
{
    if (corridors.empty())
    {
        // we haven't initialized yet.
        return CR_OK;
    }
    std::ofstream f("data/save/current/df-ai-plan.dat", std::ofstream::trunc);
    save(f);
    return CR_OK;
}

command_result Plan::unpersist(color_ostream &)
{
    std::remove("data/save/current/df-ai-plan.dat");
    std::remove(("data/save/" + World::ReadWorldFolder() + "/df-ai-plan.dat").c_str());
    return CR_OK;
}

void Plan::save(std::ostream & out)
{
    std::vector<furniture *> all_furniture;
    std::vector<room *> all_rooms;
    std::unordered_map<furniture *, size_t> furniture_index;
    std::unordered_map<room *, size_t> room_index;

    auto add_furniture = [&all_furniture, &furniture_index](furniture *f)
    {
        if (furniture_index.count(f))
            return;
        furniture_index[f] = all_furniture.size();
        all_furniture.push_back(f);
    };

    auto add_room = [add_furniture, &all_rooms, &room_index](room *r)
    {
        if (room_index.count(r))
            return;
        room_index[r] = all_rooms.size();
        all_rooms.push_back(r);

        for (auto it = r->layout.begin(); it != r->layout.end(); it++)
        {
            add_furniture(*it);
        }
    };

    for (auto it = corridors.begin(); it != corridors.end(); it++)
    {
        add_room(*it);
    }
    for (auto it = rooms.begin(); it != rooms.end(); it++)
    {
        add_room(*it);
    }

    std::ostringstream stringify;

    Json::Value converted_tasks(Json::arrayValue);
    for (auto it = tasks_generic.begin(); it != tasks_generic.end(); it++)
    {
        Json::Value t(Json::objectValue);
        stringify.str(std::string());
        stringify.clear();
        stringify << (*it)->type;
        t["t"] = stringify.str();
        if ((*it)->r)
        {
            t["r"] = Json::Int(room_index.at((*it)->r));
        }
        if ((*it)->f)
        {
            t["f"] = Json::Int(furniture_index.at((*it)->f));
        }
        converted_tasks.append(t);
    }
    for (auto it = tasks_furniture.begin(); it != tasks_furniture.end(); it++)
    {
        Json::Value t(Json::objectValue);
        stringify.str(std::string());
        stringify.clear();
        stringify << (*it)->type;
        t["t"] = stringify.str();
        if ((*it)->r)
        {
            t["r"] = Json::Int(room_index.at((*it)->r));
        }
        if ((*it)->f)
        {
            t["f"] = Json::Int(furniture_index.at((*it)->f));
        }
        converted_tasks.append(t);
    }

    Json::Value converted_rooms(Json::arrayValue);
    for (auto it = all_rooms.begin(); it != all_rooms.end(); it++)
    {
        Json::Value r(Json::objectValue);
        stringify.str(std::string());
        stringify.clear();
        stringify << (*it)->status;
        r["status"] = stringify.str();
        stringify.str(std::string());
        stringify.clear();
        stringify << (*it)->type;
        r["type"] = stringify.str();
        if ((*it)->type == room_type::corridor || (*it)->corridor_type != corridor_type::type())
        {
            stringify.str(std::string());
            stringify.clear();
            stringify << (*it)->corridor_type;
            r["corridor_type"] = stringify.str();
        }
        if ((*it)->type == room_type::farmplot || (*it)->farm_type != farm_type::type())
        {
            stringify.str(std::string());
            stringify.clear();
            stringify << (*it)->farm_type;
            r["farm_type"] = stringify.str();
        }
        if ((*it)->type == room_type::stockpile || (*it)->stockpile_type != stockpile_type::type())
        {
            stringify.str(std::string());
            stringify.clear();
            stringify << (*it)->stockpile_type;
            r["stockpile_type"] = stringify.str();
        }
        if ((*it)->type == room_type::nobleroom || (*it)->nobleroom_type != nobleroom_type::type())
        {
            stringify.str(std::string());
            stringify.clear();
            stringify << (*it)->nobleroom_type;
            r["nobleroom_type"] = stringify.str();
        }
        if ((*it)->type == room_type::outpost || (*it)->outpost_type != outpost_type::type())
        {
            stringify.str(std::string());
            stringify.clear();
            stringify << (*it)->outpost_type;
            r["outpost_type"] = stringify.str();
        }
        if ((*it)->type == room_type::location || (*it)->location_type != location_type::type())
        {
            stringify.str(std::string());
            stringify.clear();
            stringify << (*it)->location_type;
            r["location_type"] = stringify.str();
        }
        if ((*it)->type == room_type::cistern || (*it)->cistern_type != cistern_type::type())
        {
            stringify.str(std::string());
            stringify.clear();
            stringify << (*it)->cistern_type;
            r["cistern_type"] = stringify.str();
        }
        if ((*it)->type == room_type::workshop || (*it)->workshop_type != df::workshop_type())
        {
            r["workshop_type"] = enum_item_key((*it)->workshop_type);
        }
        if ((*it)->type == room_type::furnace || (*it)->furnace_type != df::furnace_type())
        {
            r["furnace_type"] = enum_item_key((*it)->furnace_type);
        }
        if (((*it)->type == room_type::workshop && (*it)->workshop_type == workshop_type::Custom) ||
            ((*it)->type == room_type::furnace && (*it)->furnace_type == furnace_type::Custom) ||
            !(*it)->raw_type.empty())
        {
            r["raw_type"] = (*it)->raw_type;
        }
        r["comment"] = (*it)->comment;
        Json::Value r_min(Json::arrayValue);
        r_min.append(Json::Int((*it)->min.x));
        r_min.append(Json::Int((*it)->min.y));
        r_min.append(Json::Int((*it)->min.z));
        r["min"] = r_min;
        Json::Value r_max(Json::arrayValue);
        r_max.append(Json::Int((*it)->max.x));
        r_max.append(Json::Int((*it)->max.y));
        r_max.append(Json::Int((*it)->max.z));
        r["max"] = r_max;
        Json::Value r_accesspath(Json::arrayValue);
        for (auto it_ = (*it)->accesspath.begin(); it_ != (*it)->accesspath.end(); it_++)
        {
            r_accesspath.append(Json::Int(room_index.at(*it_)));
        }
        r["accesspath"] = r_accesspath;
        Json::Value r_layout(Json::arrayValue);
        for (auto it_ = (*it)->layout.begin(); it_ != (*it)->layout.end(); it_++)
        {
            r_layout.append(Json::Int(furniture_index.at(*it_)));
        }
        r["layout"] = r_layout;
        r["owner"] = Json::Int((*it)->owner);
        r["bld_id"] = Json::Int((*it)->bld_id);
        r["squad_id"] = Json::Int((*it)->squad_id);
        r["level"] = Json::Int((*it)->level);
        r["noblesuite"] = Json::Int((*it)->noblesuite);
        r["queue"] = (*it)->queue;
        if ((*it)->workshop)
        {
            r["workshop"] = Json::Int(room_index.at((*it)->workshop));
        }
        Json::Value r_users(Json::arrayValue);
        for (auto it_ = (*it)->users.begin(); it_ != (*it)->users.end(); it_++)
        {
            r_users.append(Json::Int(*it_));
        }
        r["users"] = r_users;
        Json::Value r_channel_enable(Json::arrayValue);
        r_channel_enable.append(Json::Int((*it)->channel_enable.x));
        r_channel_enable.append(Json::Int((*it)->channel_enable.y));
        r_channel_enable.append(Json::Int((*it)->channel_enable.z));
        r["channel_enable"] = r_channel_enable;
        Json::Value r_stock_disable(Json::arrayValue);
        for (auto it_ = (*it)->stock_disable.begin(); it_ != (*it)->stock_disable.end(); it_++)
        {
            r_stock_disable.append(ENUM_KEY_STR(stockpile_list, *it_));
        }
        r["stock_disable"] = r_stock_disable;
        r["stock_specific1"] = (*it)->stock_specific1;
        r["stock_specific2"] = (*it)->stock_specific2;
        r["has_users"] = (*it)->has_users;
        r["furnished"] = (*it)->furnished;
        r["queue_dig"] = (*it)->queue_dig;
        r["temporary"] = (*it)->temporary;
        r["outdoor"] = (*it)->outdoor;
        r["channeled"] = (*it)->channeled;
        converted_rooms.append(r);
    }

    Json::Value converted_furniture(Json::arrayValue);
    for (auto it = all_furniture.begin(); it != all_furniture.end(); it++)
    {
        Json::Value f(Json::objectValue);
        stringify.str(std::string());
        stringify.clear();
        stringify << (*it)->type;
        f["type"] = stringify.str();
        f["construction"] = ENUM_KEY_STR(construction_type, (*it)->construction);
        f["dig"] = ENUM_KEY_STR(tile_dig_designation, (*it)->dig);
        f["direction"] = (*it)->direction;
        f["way"] = (*it)->way;
        f["bld_id"] = Json::Int((*it)->bld_id);
        f["x"] = Json::Int((*it)->pos.x);
        f["y"] = Json::Int((*it)->pos.y);
        f["z"] = Json::Int((*it)->pos.z);
        if ((*it)->target)
        {
            f["target"] = Json::Int(furniture_index.at((*it)->target));
        }
        Json::Value f_users(Json::arrayValue);
        for (auto it_ = (*it)->users.begin(); it_ != (*it)->users.end(); it_++)
        {
            f_users.append(Json::Int(*it_));
        }
        f["users"] = f_users;
        f["has_users"] = (*it)->has_users;
        f["ignore"] = (*it)->ignore;
        f["makeroom"] = (*it)->makeroom;
        f["internal"] = (*it)->internal;
        converted_furniture.append(f);
    }

    Json::Value all(Json::objectValue);
    all["t"] = converted_tasks;
    all["r"] = converted_rooms;
    all["f"] = converted_furniture;

    out << all;
}

void Plan::load(std::istream & in)
{
    for (auto it = tasks_generic.begin(); it != tasks_generic.end(); it++)
    {
        delete *it;
    }
    tasks_generic.clear();
    for (auto it = tasks_furniture.begin(); it != tasks_furniture.end(); it++)
    {
        delete *it;
    }
    tasks_furniture.clear();
    for (auto it = rooms.begin(); it != rooms.end(); it++)
    {
        delete *it;
    }
    rooms.clear();
    for (auto it = corridors.begin(); it != corridors.end(); it++)
    {
        delete *it;
    }
    corridors.clear();

    Json::Value all(Json::objectValue);
    in >> all;

    std::vector<room *> all_rooms;
    std::vector<furniture *> all_furniture;

    for (unsigned int i = all["r"].size(); i > 0; i--)
    {
        all_rooms.push_back(new room(room_type::type(), df::coord(), df::coord()));
    }
    for (unsigned int i = all["f"].size(); i > 0; i--)
    {
        all_furniture.push_back(new furniture());
    }

    std::ostringstream stringify;

#define ENUM_NAMES(name, type) \
    std::map<std::string, name::type> name ## _names; \
    for (int i = 0; i < name::_ ## name ## _count; i++) \
    { \
        stringify.str(std::string()); \
        stringify.clear(); \
        stringify << name::type(i); \
        name ## _names[stringify.str()] = name::type(i); \
    }

    ENUM_NAMES(task_type, type);
    task_type_names["checkconstruct"] = task_type::check_construct;
    task_type_names["checkfurnish"] = task_type::check_furnish;
    task_type_names["checkidle"] = task_type::check_idle;
    task_type_names["checkrooms"] = task_type::check_rooms;
    task_type_names["digroom"] = task_type::dig_room;
    task_type_names["wantdig"] = task_type::want_dig;

    for (auto it = all["t"].begin(); it != all["t"].end(); it++)
    {
        task *t = new task(task_type_names.at((*it)["t"].asString()));
        if (it->isMember("r"))
        {
            t->r = all_rooms.at((*it)["r"].asInt());
        }
        if (it->isMember("f"))
        {
            t->f = all_furniture.at((*it)["f"].asInt());
        }
        if (t->type == task_type::furnish || t->type == task_type::check_furnish)
        {
            tasks_furniture.push_back(t);
        }
        else
        {
            tasks_generic.push_back(t);
        }
    }

    ENUM_NAMES(room_status, status);
    ENUM_NAMES(room_type, type);
    ENUM_NAMES(corridor_type, type);
    corridor_type_names[""] = corridor_type::corridor;
    ENUM_NAMES(farm_type, type);
    ENUM_NAMES(stockpile_type, type);
    ENUM_NAMES(nobleroom_type, type);
    nobleroom_type_names["diningroom"] = nobleroom_type::dining;
    ENUM_NAMES(outpost_type, type);
    ENUM_NAMES(location_type, type);
    ENUM_NAMES(cistern_type, type);
    ENUM_NAMES(layout_type, type);

    for (auto it = all_rooms.begin(); it != all_rooms.end(); it++)
    {
        const Json::Value & r = all["r"][Json::ArrayIndex(it - all_rooms.begin())];
        (*it)->status = room_status_names.at(r["status"].asString());
        (*it)->type = room_type_names.at(r["type"].asString());
        if (r.isMember("subtype"))
        {
            switch ((*it)->type)
            {
            case room_type::corridor:
                (*it)->corridor_type = corridor_type_names.at(r["subtype"].asString());
                break;
            case room_type::farmplot:
                (*it)->farm_type = farm_type_names.at(r["subtype"].asString());
                break;
            case room_type::workshop:
            {
                if (find_enum_item(&(*it)->workshop_type, r["subtype"].asString()))
                {
                    break;
                }
                if (find_enum_item(&(*it)->furnace_type, r["subtype"].asString()))
                {
                    (*it)->type = room_type::furnace;
                    break;
                }
                if (r["subtype"].asString() == "TradeDepot")
                {
                    (*it)->type = room_type::tradedepot;
                    break;
                }
                std::string subtype(r["subtype"].asString());
                for (auto it2 = subtype.begin(); it2 != subtype.end(); it2++)
                {
                    if (isupper(*it2) && !(*it)->raw_type.empty())
                    {
                        (*it)->raw_type.push_back('_');
                    }
                    (*it)->raw_type.push_back(toupper(*it2));
                }
                (*it)->workshop_type = workshop_type::Custom;
                break;
            }
            case room_type::stockpile:
                (*it)->stockpile_type = stockpile_type_names.at(r["subtype"].asString());
                break;
            case room_type::nobleroom:
                (*it)->nobleroom_type = nobleroom_type_names.at(r["subtype"].asString());
                break;
            case room_type::outpost:
                (*it)->outpost_type = outpost_type_names.at(r["subtype"].asString());
                break;
            case room_type::location:
                (*it)->location_type = location_type_names.at(r["subtype"].asString());
                break;
            case room_type::cistern:
                (*it)->cistern_type = cistern_type_names.at(r["subtype"].asString());
                break;
            default:
                assert(r["subtype"].asString().empty());
                break;
            }
        }
        else
        {
            if (r.isMember("corridor_type"))
                (*it)->corridor_type = corridor_type_names.at(r["corridor_type"].asString());
            if (r.isMember("farm_type"))
                (*it)->farm_type = farm_type_names.at(r["farm_type"].asString());
            if (r.isMember("stockpile_type"))
                (*it)->stockpile_type = stockpile_type_names.at(r["stockpile_type"].asString());
            if (r.isMember("nobleroom_type"))
                (*it)->nobleroom_type = nobleroom_type_names.at(r["nobleroom_type"].asString());
            if (r.isMember("outpost_type"))
                (*it)->outpost_type = outpost_type_names.at(r["outpost_type"].asString());
            if (r.isMember("location_type"))
                (*it)->location_type = location_type_names.at(r["location_type"].asString());
            if (r.isMember("cistern_type"))
                (*it)->cistern_type = cistern_type_names.at(r["cistern_type"].asString());
            if (r.isMember("workshop_type"))
                find_enum_item(&(*it)->workshop_type, r["workshop_type"].asString());
            if (r.isMember("furnace_type"))
                find_enum_item(&(*it)->furnace_type, r["furnace_type"].asString());
            if (r.isMember("raw_type"))
                (*it)->raw_type = r["raw_type"].asString();
        }
        (*it)->comment = r["comment"].asString();
        (*it)->min.x = r["min"][0].asInt();
        (*it)->min.y = r["min"][1].asInt();
        (*it)->min.z = r["min"][2].asInt();
        (*it)->max.x = r["max"][0].asInt();
        (*it)->max.y = r["max"][1].asInt();
        (*it)->max.z = r["max"][2].asInt();
        for (auto it_ = r["accesspath"].begin(); it_ != r["accesspath"].end(); it_++)
        {
            (*it)->accesspath.push_back(all_rooms.at(it_->asInt()));
        }
        for (auto it_ = r["layout"].begin(); it_ != r["layout"].end(); it_++)
        {
            (*it)->layout.push_back(all_furniture.at(it_->asInt()));
        }
        (*it)->owner = r["owner"].asInt();
        (*it)->bld_id = r["bld_id"].asInt();
        (*it)->squad_id = r["squad_id"].asInt();
        (*it)->level = r["level"].asInt();
        (*it)->noblesuite = r["noblesuite"].asInt();
        if (r.isMember("queue"))
        {
            (*it)->queue = r["queue"].asInt();
        }
        if (r.isMember("workshop"))
        {
            (*it)->workshop = all_rooms.at(r["workshop"].asInt());
        }
        for (auto it_ = r["users"].begin(); it_ != r["users"].end(); it_++)
        {
            (*it)->users.insert(it_->asInt());
        }
        if ((*it)->type == room_type::pasture)
        {
            ai->pop->pet_check.insert((*it)->users.begin(), (*it)->users.end());
        }
        (*it)->channel_enable.x = r["channel_enable"][0].asInt();
        (*it)->channel_enable.y = r["channel_enable"][1].asInt();
        (*it)->channel_enable.z = r["channel_enable"][2].asInt();
        for (auto it_ = r["stock_disable"].begin(); it_ != r["stock_disable"].end(); it_++)
        {
            df::stockpile_list disable;
            if (find_enum_item(&disable, it_->asString()))
            {
                (*it)->stock_disable.insert(disable);
            }
        }
        (*it)->stock_specific1 = r["stock_specific1"].asBool();
        (*it)->stock_specific2 = r["stock_specific2"].asBool();
        (*it)->has_users = r["has_users"].asBool();
        (*it)->furnished = r["furnished"].asBool();
        (*it)->queue_dig = r["queue_dig"].asBool();
        (*it)->temporary = r["temporary"].asBool();
        (*it)->outdoor = r["outdoor"].asBool();
        (*it)->channeled = r["channeled"].asBool();

        if ((*it)->type == room_type::corridor)
        {
            corridors.push_back(*it);
        }
        else
        {
            rooms.push_back(*it);
        }
    }

    for (auto it = all_furniture.begin(); it != all_furniture.end(); it++)
    {
        const Json::Value & f = all["f"][Json::ArrayIndex(it - all_furniture.begin())];
        if (f.isMember("type"))
        {
            (*it)->type = layout_type_names.at(f["type"].asString());
        }
        else
        {
            if (f["item"].asString() == "")
            {
                (*it)->type = layout_type::none;
            }
            else if (f["item"].asString() == "archerytarget")
            {
                (*it)->type = layout_type::archery_target;
            }
            else if (f["item"].asString() == "armorstand")
            {
                (*it)->type = layout_type::armor_stand;
            }
            else if (f["item"].asString() == "bed")
            {
                (*it)->type = layout_type::bed;
            }
            else if (f["item"].asString() == "bookcase")
            {
                (*it)->type = layout_type::bookcase;
            }
            else if (f["item"].asString() == "cabinet")
            {
                (*it)->type = layout_type::cabinet;
            }
            else if (f["item"].asString() == "chair")
            {
                (*it)->type = layout_type::chair;
            }
            else if (f["item"].asString() == "chest")
            {
                (*it)->type = layout_type::chest;
            }
            else if (f["item"].asString() == "coffin")
            {
                (*it)->type = layout_type::coffin;
            }
            else if (f["item"].asString() == "door")
            {
                (*it)->type = layout_type::door;
            }
            else if (f["item"].asString() == "floodgate")
            {
                (*it)->type = layout_type::floodgate;
            }
            else if (f["item"].asString() == "gear_assembly")
            {
                (*it)->type = layout_type::gear_assembly;
            }
            else if (f["item"].asString() == "hive")
            {
                (*it)->type = layout_type::hive;
            }
            else if (f["item"].asString() == "nestbox")
            {
                (*it)->type = layout_type::nest_box;
            }
            else if (f["item"].asString() == "roller")
            {
                (*it)->type = layout_type::roller;
            }
            else if (f["item"].asString() == "table")
            {
                (*it)->type = layout_type::table;
            }
            else if (f["item"].asString() == "tractionbench")
            {
                (*it)->type = layout_type::traction_bench;
            }
            else if (f["item"].asString() == "trap")
            {
                if (f["subtype"].asString() == "cage")
                {
                    (*it)->type = layout_type::cage_trap;
                }
                else if (f["subtype"].asString() == "lever")
                {
                    (*it)->type = layout_type::lever;
                }
                else if (f["subtype"].asString() == "trackstop")
                {
                    (*it)->type = layout_type::track_stop;
                }
            }
            else if (f["item"].asString() == "vertical_axle")
            {
                (*it)->type = layout_type::vertical_axle;
            }
            else if (f["item"].asString() == "weaponrack")
            {
                (*it)->type = layout_type::weapon_rack;
            }
            else if (f["item"].asString() == "well")
            {
                (*it)->type = layout_type::well;
            }
            else if (f["item"].asString() == "windmill")
            {
                (*it)->type = layout_type::windmill;
            }
        }
        find_enum_item(&(*it)->construction, f["construction"].asString());
        find_enum_item(&(*it)->dig, f["dig"].asString());
        (*it)->direction = f["direction"].asString();
        (*it)->way = f["way"].asString();
        (*it)->bld_id = f["bld_id"].asInt();
        (*it)->pos.x = f["x"].asInt();
        (*it)->pos.y = f["y"].asInt();
        (*it)->pos.z = f["z"].asInt();
        if (f.isMember("target"))
        {
            (*it)->target = all_furniture.at(f["target"].asInt());
        }
        for (auto it_ = f["users"].begin(); it_ != f["users"].end(); it_++)
        {
            ai->pop->citizen.insert(it_->asInt());
            (*it)->users.insert(it_->asInt());
        }
        (*it)->has_users = f["has_users"].asBool();
        (*it)->ignore = f["ignore"].asBool();
        (*it)->makeroom = f["makeroom"].asBool();
        (*it)->internal = f["internal"].asBool();
    }

    fort_entrance = corridors.at(0);
    categorize_all();
}

uint16_t Plan::getTileWalkable(df::coord t)
{
    if (df::map_block *b = Maps::getTileBlock(t))
        return b->walkable[t.x & 0xf][t.y & 0xf];
    return 0;
}

task *Plan::is_digging()
{
    for (auto it = tasks_generic.begin(); it != tasks_generic.end(); it++)
    {
        task *t = *it;
        if ((t->type == task_type::want_dig || t->type == task_type::dig_room) && t->r->type != room_type::corridor)
            return t;
    }
    return nullptr;
}

bool Plan::is_idle()
{
    if (!tasks_furniture.empty())
    {
        return false;
    }
    for (auto it = tasks_generic.begin(); it != tasks_generic.end(); it++)
    {
        task *t = *it;
        if (t->type != task_type::monitor_cistern && t->type != task_type::check_rooms && t->type != task_type::check_idle)
            return false;
    }
    return true;
}

void Plan::new_citizen(color_ostream & out, int32_t uid)
{
    if (std::find_if(tasks_generic.begin(), tasks_generic.end(), [](task *t) -> bool { return t->type == task_type::check_idle; }) == tasks_generic.end())
    {
        tasks_generic.push_back(new task(task_type::check_idle));
    }
    getdiningroom(out, uid);
    getbedroom(out, uid);
}

void Plan::del_citizen(color_ostream & out, int32_t uid)
{
    freecommonrooms(out, uid);
    freebedroom(out, uid);
}

bool Plan::checkidle(color_ostream & out)
{
    for (auto it = tasks_generic.begin(); it != tasks_generic.end(); it++)
    {
        if ((*it)->type == task_type::want_dig && (*it)->r->type != room_type::corridor && (*it)->r->queue == 0)
        {
            return false;
        }
    }

    // if nothing better to do, order the miners to dig remaining
    // stockpiles, workshops, and a few bedrooms
    auto ifplan = [](room *r) -> bool
    {
        return r->status == room_status::plan;
    };
    int32_t freebed = spare_bedroom;
    room *r = nullptr;
#define FIND_ROOM(cond, type, lambda) \
    if (r == nullptr && (cond)) \
        r = find_room(type, lambda)

    FIND_ROOM(true, room_type::stockpile, [](room *r) -> bool
    {
        return r->stockpile_type == stockpile_type::food &&
            r->level == 0 &&
            !r->workshop &&
            r->status == room_status::plan;
    });
    FIND_ROOM(!important_workshops.empty(), room_type::workshop, [this](room *r) -> bool
    {
        if (r->workshop_type == important_workshops.back() &&
            r->status == room_status::plan &&
            r->level == 0)
        {
            important_workshops.pop_back();
            return true;
        }
        return false;
    });
    FIND_ROOM(true, room_type::tradedepot, ifplan);
    FIND_ROOM(true, room_type::cistern, ifplan);
    FIND_ROOM(true, room_type::infirmary, ifplan);
    FIND_ROOM(!find_room(room_type::cemetary, [](room *r) -> bool { return r->status != room_status::plan; }), room_type::cemetary, ifplan);
    FIND_ROOM(!important_workshops2.empty(), room_type::furnace, [this](room *r) -> bool
    {
        if (r->furnace_type == important_workshops2.back() &&
            r->status == room_status::plan &&
            r->level == 0)
        {
            important_workshops2.pop_back();
            return true;
        }
        return false;
    });
    FIND_ROOM(!important_workshops3.empty(), room_type::workshop, [this](room *r) -> bool
    {
        if (r->workshop_type == important_workshops3.back() &&
            r->status == room_status::plan &&
            r->level == 0)
        {
            important_workshops3.pop_back();
            return true;
        }
        return false;
    });
    if (r == nullptr)
        should_search_for_metal = true;
    FIND_ROOM(true, room_type::pitcage, ifplan);
    FIND_ROOM(true, room_type::stockpile, [](room *r) -> bool
    {
        return r->status == room_status::plan &&
            r->level <= 1 &&
            r->workshop == nullptr;
    });
    FIND_ROOM(true, room_type::workshop, [](room *r) -> bool
    {
        return r->status == room_status::plan &&
            r->level == 0;
    });
    FIND_ROOM(true, room_type::furnace, [](room *r) -> bool
    {
        return r->status == room_status::plan &&
            r->level == 0;
    });
    if (r == nullptr && !fort_entrance->furnished)
    {
        r = fort_entrance;
        for (auto wagon : world->buildings.other[buildings_other_id::WAGON])
        {
            Buildings::deconstruct(wagon);
        }
    }
    int32_t need_food = extra_farms;
    int32_t need_cloth = extra_farms;
    FIND_ROOM(true, room_type::farmplot, ([&need_food, &need_cloth](room *r) -> bool
    {
        if (!r->users.empty())
        {
            return false;
        }

        if (r->farm_type == farm_type::food)
        {
            if (need_food <= 0)
            {
                return false;
            }

            if (r->status == room_status::plan)
            {
                return true;
            }

            need_food--;
        }
        else if (r->farm_type == farm_type::cloth)
        {
            if (need_cloth <= 0)
            {
                return false;
            }

            if (r->status == room_status::plan)
            {
                return true;
            }

            need_cloth--;
        }
        return false;
    }));
    FIND_ROOM(true, room_type::location, [](room *r) -> bool { return r->status == room_status::plan && r->location_type == location_type::tavern; });
    FIND_ROOM(true, room_type::outpost, [](room *r) -> bool
    {
        return r->status == room_status::plan && r->outpost_type == outpost_type::cavern;
    });
    FIND_ROOM(true, room_type::location, ifplan);
    FIND_ROOM(true, room_type::workshop, [](room *r) -> bool
    {
        return r->status == room_status::plan &&
            r->level == 1;
    });
    FIND_ROOM(true, room_type::furnace, [](room *r) -> bool
    {
        return r->status == room_status::plan &&
            r->level == 1;
    });
    FIND_ROOM(true, room_type::bedroom, [&freebed](room *r) -> bool
    {
        if (r->owner == -1)
        {
            freebed--;
            return freebed >= 0 && r->status == room_status::plan;
        }
        return false;
    });
    FIND_ROOM(true, room_type::stockpile, [](room *r) -> bool
    {
        return r->status == room_status::plan &&
            r->level <= 2;
    });
    auto finished_nofurnished = [](room *r) -> bool
    {
        return r->status == room_status::finished && !r->furnished;
    };
    FIND_ROOM(true, room_type::location, finished_nofurnished);
    FIND_ROOM(true, room_type::nobleroom, finished_nofurnished);
    FIND_ROOM(true, room_type::bedroom, finished_nofurnished);
    auto nousers_noplan = [](room *r) -> bool
    {
        return r->status != room_status::plan && std::find_if(r->layout.begin(), r->layout.end(), [](furniture *f) -> bool
        {
            return f->has_users && f->users.empty();
        }) != r->layout.end();
    };
    auto nousers_plan = [](room *r) -> bool
    {
        return r->status == room_status::plan && std::find_if(r->layout.begin(), r->layout.end(), [](furniture *f) -> bool
        {
            return f->has_users && f->users.empty();
        }) != r->layout.end();
    };
    FIND_ROOM(!find_room(room_type::dininghall, nousers_noplan), room_type::dininghall, nousers_plan);
    FIND_ROOM(!find_room(room_type::barracks, nousers_noplan), room_type::barracks, nousers_plan);
    FIND_ROOM(true, room_type::stockpile, [](room *r) -> bool
    {
        return r->status == room_status::plan &&
            r->level <= 3;
    });
    FIND_ROOM(true, room_type::workshop, ifplan);
    FIND_ROOM(true, room_type::furnace, ifplan);
    FIND_ROOM(true, room_type::stockpile, ifplan);
    FIND_ROOM(true, room_type::outpost, ifplan);
    if (r == nullptr)
    {
        if (!past_initial_phase)
        {
            past_initial_phase = true;
            std::function<bool(room *)> unignore_all_furniture = [this, &out](room *r) -> bool
            {
                for (auto f : r->layout)
                {
                    f->ignore = false;
                }
                if (r->status == room_status::dug || r->status == room_status::finished)
                {
                    furnish_room(out, r);
                }
                return false;
            };
            find_room(room_type::dininghall, unignore_all_furniture);
            find_room(room_type::barracks, unignore_all_furniture);
            find_room(room_type::nobleroom, unignore_all_furniture);
            find_room(room_type::bedroom, unignore_all_furniture);
            find_room(room_type::cemetary, unignore_all_furniture);
        }

        bool any_outpost = false;
        for (auto it = tasks_generic.begin(); it != tasks_generic.end(); it++)
        {
            if ((*it)->type != task_type::want_dig && (*it)->type != task_type::dig_room)
            {
                continue;
            }
            if ((*it)->r->type == room_type::outpost || ((*it)->r->type == room_type::corridor && (*it)->r->corridor_type == corridor_type::outpost))
            {
                any_outpost = true;
                break;
            }
        }
        if (!any_outpost && setup_blueprint_caverns(out) == CR_OK)
        {
            ai->debug(out, "found next cavern");
            categorize_all();
            return false;
        }
    }
    FIND_ROOM(true, room_type::dininghall, ifplan);
    FIND_ROOM(true, room_type::barracks, ifplan);
    FIND_ROOM(true, room_type::farmplot, ifplan);
    FIND_ROOM(true, room_type::nobleroom, ifplan);
    FIND_ROOM(true, room_type::bedroom, ifplan);
    FIND_ROOM(true, room_type::cemetary, ifplan);

    if (r == nullptr && last_idle_year != *cur_year)
    {
        if (last_idle_year != -1)
        {
            idleidle(out);
        }
        last_idle_year = *cur_year;
    }
#undef FIND_ROOM

    if (r)
    {
        ai->debug(out, "checkidle " + describe_room(r));
        wantdig(out, r);
        if (r->status == room_status::finished)
        {
            r->furnished = true;
            for (auto it = r->layout.begin(); it != r->layout.end(); it++)
            {
                (*it)->ignore = false;
            }
            furnish_room(out, r);
            smooth_room(out, r);
        }
        return false;
    }

    return false;
}

static bool last_idleidle_nothing = false;
static std::vector<room *> idleidle_tab;

void Plan::idleidle(color_ostream & out)
{
    if (!idleidle_tab.empty())
    {
        return;
    }

    idleidle_tab.clear();
    for (auto it = rooms.begin(); it != rooms.end(); it++)
    {
        room *r = *it;
        if (r->status != room_status::plan && r->status != room_status::dig && !r->temporary &&
            (r->type == room_type::nobleroom ||
                r->type == room_type::bedroom ||
                r->type == room_type::dininghall ||
                r->type == room_type::cemetary ||
                r->type == room_type::infirmary ||
                r->type == room_type::barracks ||
                r->type == room_type::location ||
                r->type == room_type::stockpile))
            idleidle_tab.push_back(r);
    }
    for (auto it = corridors.begin(); it != corridors.end(); it++)
    {
        room *r = *it;
        if (r->status != room_status::plan && r->status != room_status::dig && r->corridor_type == corridor_type::corridor)
            idleidle_tab.push_back(r);
    }
    if (idleidle_tab.empty())
    {
        last_idleidle_nothing = false;
        return;
    }

    bool engrave = last_idleidle_nothing;
    last_idleidle_nothing = true;

    if (engrave)
    {
        ai->debug(out, "engrave fort");
    }
    else
    {
        ai->debug(out, "smooth fort");
    }

    events.onupdate_register_once("df-ai plan idleidle", 4, [this, engrave](color_ostream & out) -> bool
    {
        if (!Core::getInstance().isMapLoaded())
        {
            idleidle_tab.clear();
            last_idleidle_nothing = false;
        }
        if (idleidle_tab.empty())
        {
            return true;
        }
        if (smooth_room(out, idleidle_tab.back(), engrave))
        {
            last_idleidle_nothing = false;
        }
        idleidle_tab.pop_back();
        return false;
    });
}

void Plan::checkrooms(color_ostream & out)
{
    size_t ncheck = 4;
    for (size_t i = ncheck * 4; i > 0; i--)
    {
        if (checkroom_idx < corridors.size() && corridors[checkroom_idx]->status != room_status::plan)
        {
            checkroom(out, corridors[checkroom_idx]);
        }

        if (checkroom_idx < rooms.size() && rooms[checkroom_idx]->status != room_status::plan)
        {
            checkroom(out, rooms[checkroom_idx]);
            ncheck--;
        }
        checkroom_idx++;
        if (ncheck <= 0)
            break;
    }
    if (checkroom_idx >= rooms.size() && checkroom_idx >= corridors.size())
        checkroom_idx = 0;
}

// ensure room was not tantrumed etc
void Plan::checkroom(color_ostream & out, room *r)
{
    if (r->status == room_status::plan)
    {
        return;
    }

    // fix missing walls/staircases
    fixup_open(out, r);
    // designation cancelled: damp stone, cave-in, or tree
    r->dig();

    if (r->status == room_status::dug || r->status == room_status::finished)
    {
        // tantrumed furniture
        for (auto it = r->layout.begin(); it != r->layout.end(); it++)
        {
            furniture *f = *it;
            if (f->ignore)
                continue;
            df::coord t = r->min + f->pos;
            if (f->bld_id != -1 && !df::building::find(f->bld_id))
            {
                std::ostringstream str;
                str << "fix furniture " << f->type << " in " << describe_room(r);
                ai->debug(out, str.str(), t);
                f->bld_id = -1;

                tasks_furniture.push_back(new task(task_type::furnish, r, f));
            }
            if (f->construction != construction_type::NONE)
            {
                try_furnish_construction(out, f->construction, t);
            }
        }
        // tantrumed building
        if (r->bld_id != -1 && !r->dfbuilding())
        {
            ai->debug(out, "rebuild " + describe_room(r), r->pos());
            r->bld_id = -1;
            construct_room(out, r);
        }
    }
}

void Plan::getbedroom(color_ostream & out, int32_t id)
{
    room *r = find_room(room_type::bedroom, [id](room *r) -> bool { return r->owner == id; });
    if (!r)
        r = find_room(room_type::bedroom, [](room *r) -> bool { return r->status != room_status::plan && r->owner == -1; });
    if (!r)
        r = find_room(room_type::bedroom, [](room *r) -> bool { return r->status == room_status::plan && !r->queue_dig; });
    if (r)
    {
        r->queue = 1;
        wantdig(out, r);
        set_owner(out, r, id);
        ai->debug(out, "assign " + describe_room(r), r->pos());
        if (r->status == room_status::finished)
            furnish_room(out, r);
    }
    else
    {
        ai->debug(out, stl_sprintf("[ERROR] AI can't getbedroom(%d)", id));
    }
}

void Plan::getdiningroom(color_ostream & out, int32_t id)
{
    // skip allocating space if there's already a dining room for this dwarf.
    auto is_user = [id](furniture *f) -> bool
    {
        return f->users.count(id);
    };
    if (find_room(room_type::dininghall, [is_user](room *r) -> bool
    {
        return std::find_if(r->layout.begin(), r->layout.end(), is_user) != r->layout.end();
    }))
        return;

    if (room *r = find_room(room_type::farmplot, [](room *r_) -> bool
    {
        df::coord size = r_->size();
        return r_->farm_type == farm_type::food &&
            !r_->outdoor &&
            int32_t(r_->users.size()) < size.x * size.y *
            dwarves_per_farmtile_num / dwarves_per_farmtile_den;
    }))
    {
        wantdig(out, r);
        r->users.insert(id);
    }

    if (room *r = find_room(room_type::farmplot, [](room *r_) -> bool
    {
        df::coord size = r_->size();
        return r_->farm_type == farm_type::cloth &&
            !r_->outdoor &&
            int32_t(r_->users.size()) < size.x * size.y *
            dwarves_per_farmtile_num / dwarves_per_farmtile_den;
    }))
    {
        wantdig(out, r);
        r->users.insert(id);
    }

    if (room *r = find_room(room_type::farmplot, [](room *r_) -> bool
    {
        df::coord size = r_->size();
        return r_->farm_type == farm_type::food &&
            r_->outdoor &&
            int32_t(r_->users.size()) < size.x * size.y *
            dwarves_per_farmtile_num / dwarves_per_farmtile_den;
    }))
    {
        wantdig(out, r);
        r->users.insert(id);
    }

    if (room *r = find_room(room_type::farmplot, [](room *r_) -> bool
    {
        df::coord size = r_->size();
        return r_->farm_type == farm_type::cloth &&
            r_->outdoor &&
            int32_t(r_->users.size()) < size.x * size.y *
            dwarves_per_farmtile_num / dwarves_per_farmtile_den;
    }))
    {
        wantdig(out, r);
        r->users.insert(id);
    }

    if (room *r = find_room(room_type::dininghall, [](room *r_) -> bool
    {
        for (auto it = r_->layout.begin(); it != r_->layout.end(); it++)
        {
            furniture *f = *it;
            if (f->has_users && f->users.size() < dwarves_per_table)
                return true;
        }
        return false;
    }))
    {
        wantdig(out, r);
        for (auto it = r->layout.begin(); it != r->layout.end(); it++)
        {
            furniture *f = *it;
            if (f->type == layout_type::table && f->users.size() < dwarves_per_table)
            {
                f->ignore = false;
                f->users.insert(id);
                break;
            }
        }
        for (auto it = r->layout.begin(); it != r->layout.end(); it++)
        {
            furniture *f = *it;
            if (f->type == layout_type::chair && f->users.size() < dwarves_per_table)
            {
                f->ignore = false;
                f->users.insert(id);
                break;
            }
        }
        if (r->status == room_status::finished)
        {
            furnish_room(out, r);
        }
    }
}

void Plan::attribute_noblerooms(color_ostream & out, const std::set<int32_t> & id_list)
{
    // XXX tomb may be populated...
    while (room *old = find_room(room_type::nobleroom, [id_list](room *r) -> bool { return r->owner != -1 && !id_list.count(r->owner); }))
    {
        set_owner(out, old, -1);
    }

    for (auto it = id_list.begin(); it != id_list.end(); it++)
    {
        std::vector<Units::NoblePosition> entpos;
        Units::getNoblePositions(&entpos, df::unit::find(*it));
        room *base = find_room(room_type::nobleroom, [it](room *r) -> bool { return r->owner == *it; });
        if (!base)
            base = find_room(room_type::nobleroom, [](room *r) -> bool { return r->owner == -1; });
        std::set<nobleroom_type::type> seen;
        while (room *r = find_room(room_type::nobleroom, [base, seen](room *r_) -> bool { return r_->noblesuite == base->noblesuite && !seen.count(r_->nobleroom_type); }))
        {
            seen.insert(r->nobleroom_type);
            set_owner(out, r, *it);
#define DIG_ROOM_IF(type, req) \
            if (r->nobleroom_type == nobleroom_type::type && std::find_if(entpos.begin(), entpos.end(), [](Units::NoblePosition np) -> bool { return np.position->required_ ## req > 0; }) != entpos.end()) \
            { \
                r->queue = 2; \
                wantdig(out, r); \
            }
            DIG_ROOM_IF(tomb, tomb);
            DIG_ROOM_IF(dining, dining);
            DIG_ROOM_IF(bedroom, bedroom);
            DIG_ROOM_IF(office, office);
#undef DIG_ROOM_IF
        }
    }
}

void Plan::getsoldierbarrack(color_ostream & out, int32_t id)
{
    df::unit *u = df::unit::find(id);
    if (!u)
        return;
    int32_t squad_id = u->military.squad_id;
    if (squad_id == -1)
        return;


    room *r = find_room(room_type::barracks, [squad_id](room *r) -> bool { return r->squad_id == squad_id; });
    if (!r)
    {
        r = find_room(room_type::barracks, [](room *r) -> bool { return r->squad_id == -1; });
        if (!r)
        {
            ai->debug(out, "[ERROR] no free barracks");
            return;
        }
        r->squad_id = squad_id;
        ai->debug(out, stl_sprintf("squad %d assign %s", squad_id, describe_room(r).c_str()));
        wantdig(out, r);
        if (df::building *bld = r->dfbuilding())
        {
            assign_barrack_squad(out, bld, squad_id);
        }
    }

    auto find_furniture = [id, r](layout_type::type type)
    {
        for (auto f : r->layout)
        {
            if (f->type == type && f->users.count(id))
            {
                f->ignore = false;
                return;
            }
        }
        for (auto f : r->layout)
        {
            if (f->type == type && (type == layout_type::archery_target || f->users.size() < (type == layout_type::weapon_rack ? 4 : 1)))
            {
                f->users.insert(id);
                f->ignore = false;
                return;
            }
        }
    };
    find_furniture(layout_type::weapon_rack);
    find_furniture(layout_type::armor_stand);
    find_furniture(layout_type::bed);
    find_furniture(layout_type::cabinet);
    find_furniture(layout_type::chest);
    find_furniture(layout_type::archery_target);

    if (r->status == room_status::finished)
    {
        furnish_room(out, r);
    }
}

void Plan::assign_barrack_squad(color_ostream &, df::building *bld, int32_t squad_id)
{
    std::vector<df::building_squad_use *> *squads = bld->getSquads();
    if (squads) // archerytarget has no such field
    {
        auto su = std::find_if(squads->begin(), squads->end(), [squad_id](df::building_squad_use *su) -> bool { return su->squad_id == squad_id; });
        if (su == squads->end())
        {
            df::building_squad_use *newSquad = df::allocate<df::building_squad_use>();
            newSquad->squad_id = squad_id;
            su = squads->insert(su, newSquad);
        }
        (*su)->mode.bits.sleep = 1;
        (*su)->mode.bits.train = 1;
        (*su)->mode.bits.indiv_eq = 1;
        (*su)->mode.bits.squad_eq = 1;
    }

    df::squad *squad = df::squad::find(squad_id);
    auto sr = std::find_if(squad->rooms.begin(), squad->rooms.end(), [bld](df::squad::T_rooms *sr) -> bool { return sr->building_id == bld->id; });
    if (sr == squad->rooms.end())
    {
        df::squad::T_rooms *newRoom = df::allocate<df::squad::T_rooms>();
        newRoom->building_id = bld->id;
        sr = squad->rooms.insert(sr, newRoom);
    }
    (*sr)->mode.bits.sleep = 1;
    (*sr)->mode.bits.train = 1;
    (*sr)->mode.bits.indiv_eq = 1;
    (*sr)->mode.bits.squad_eq = 1;
}

void Plan::getcoffin(color_ostream & out)
{
    if (room *r = find_room(room_type::cemetary, [](room *r_) -> bool { return std::find_if(r_->layout.begin(), r_->layout.end(), [](furniture *f) -> bool { return f->has_users && f->users.empty(); }) != r_->layout.end(); }))
    {
        wantdig(out, r);
        for (auto it = r->layout.begin(); it != r->layout.end(); it++)
        {
            furniture *f = *it;
            if (f->type == layout_type::coffin && f->users.empty())
            {
                f->users.insert(0);
                f->ignore = false;
                break;
            }
        }
        if (r->status == room_status::finished)
        {
            furnish_room(out, r);
        }
    }
}

// free / deconstruct the bedroom assigned to this dwarf
void Plan::freebedroom(color_ostream & out, int32_t id)
{
    if (room *r = find_room(room_type::bedroom, [id](room *r_) -> bool { return r_->owner == id; }))
    {
        ai->debug(out, "free " + describe_room(r), r->pos());
        set_owner(out, r, -1);
        for (auto it = r->layout.begin(); it != r->layout.end(); it++)
        {
            furniture *f = *it;
            if (f->ignore)
                continue;
            if (f->type == layout_type::door)
                continue;
            if (df::building *bld = df::building::find(f->bld_id))
            {
                Buildings::deconstruct(bld);
            }
            f->bld_id = -1;
        }
        r->bld_id = -1;
    }
}

// free / deconstruct the common facilities assigned to this dwarf
// optionnaly restricted to a single subtype among:
//  [dininghall, farmplots, barracks]
void Plan::freecommonrooms(color_ostream & out, int32_t id)
{
    freecommonrooms(out, id, room_type::dininghall);
    freecommonrooms(out, id, room_type::farmplot);
    freecommonrooms(out, id, room_type::barracks);
}

void Plan::freecommonrooms(color_ostream & out, int32_t id, room_type::type subtype)
{
    if (subtype == room_type::farmplot)
    {
        find_room(subtype, [id](room *r) -> bool
        {
            r->users.erase(id);
            return false;
        });
    }
    else
    {
        find_room(subtype, [this, &out, id](room *r) -> bool
        {
            for (auto it = r->layout.begin(); it != r->layout.end(); it++)
            {
                furniture *f = *it;
                if (!f->has_users)
                    continue;
                if (f->ignore)
                    continue;
                if (f->users.erase(id) && f->users.empty() && !past_initial_phase)
                {
                    // delete the specific table/chair/bed/etc for the dwarf
                    if (f->bld_id != -1 && f->bld_id != r->bld_id)
                    {
                        if (df::building *bld = df::building::find(f->bld_id))
                        {
                            Buildings::deconstruct(bld);
                        }
                        f->bld_id = -1;
                        f->ignore = true;
                    }

                    // clear the whole room if it is entirely unused
                    if (r->bld_id != -1 && std::find_if(r->layout.begin(), r->layout.end(), [](furniture *f) -> bool { return f->has_users && !f->users.empty(); }) == r->layout.end())
                    {
                        if (df::building *bld = r->dfbuilding())
                        {
                            Buildings::deconstruct(bld);
                        }
                        r->bld_id = -1;

                        if (r->squad_id != -1)
                        {
                            ai->debug(out, stl_sprintf("squad %d free %s", r->squad_id, describe_room(r).c_str()), r->pos());
                            r->squad_id = -1;
                        }
                    }
                }
            }
            return false;
        });
    }
}

void Plan::freesoldierbarrack(color_ostream & out, int32_t id)
{
    freecommonrooms(out, id, room_type::barracks);
}

df::building *Plan::getpasture(color_ostream & out, int32_t pet_id)
{
    df::unit *pet = df::unit::find(pet_id);

    // don't assign multiple pastures
    if (df::general_ref *ref = Units::getGeneralRef(pet, general_ref_type::BUILDING_CIVZONE_ASSIGNED))
    {
        return ref->getBuilding();
    }

    size_t limit = 1000 - (11 * 11 * 1000 / df::creature_raw::find(pet->race)->caste[pet->caste]->misc.grazer); // 1000 = arbitrary, based on dfwiki?pasture
    if (room *r = find_room(room_type::pasture, [limit](room *r_) -> bool
    {
        size_t sum = 0;
        for (auto it = r_->users.begin(); it != r_->users.end(); it++)
        {
            df::unit *u = df::unit::find(*it);
            // 11*11 == pasture dimensions
            sum += 11 * 11 * 1000 / df::creature_raw::find(u->race)->caste[u->caste]->misc.grazer;
        }
        return sum < limit;
    }))
    {
        r->users.insert(pet_id);
        if (r->bld_id == -1)
            construct_room(out, r);
        return r->dfbuilding();
    }
    return nullptr;
}

void Plan::freepasture(color_ostream &, int32_t pet_id)
{
    if (room *r = find_room(room_type::pasture, [pet_id](room *r_) -> bool { return r_->users.count(pet_id); }))
    {
        r->users.erase(pet_id);
    }
}

void Plan::set_owner(color_ostream &, room *r, int32_t uid)
{
    r->owner = uid;
    if (r->bld_id != -1)
    {
        df::unit *u = df::unit::find(uid);
        if (df::building *bld = r->dfbuilding())
        {
            Buildings::setOwner(bld, u);
        }
    }
}

void Plan::dig_tile(df::coord t, df::tile_dig_designation dig)
{
    if (ENUM_ATTR(tiletype, material, *Maps::getTileType(t)) == tiletype_material::TREE && dig != tile_dig_designation::No)
    {
        dig = tile_dig_designation::Default;
        t = find_tree_base(t);
    }

    df::tile_designation *des = Maps::getTileDesignation(t);
    if (dig != tile_dig_designation::No && des->bits.dig == tile_dig_designation::No && !des->bits.hidden)
    {
        for (auto job = world->job_list.next; job != nullptr; job = job->next)
        {
            if (ENUM_ATTR(job_type, type, job->item->job_type) == job_type_class::Digging && job->item->pos == t)
            {
                // someone already enroute to dig here, avoid 'Inappropriate
                // dig square' spam
                return;
            }
        }
    }

    des->bits.dig = dig;
    Maps::getTileOccupancy(t)->bits.dig_marked = 0;
    if (dig != tile_dig_designation::No)
    {
        Maps::getTileBlock(t)->flags.bits.designated = 1;
    }
}

// queue a room for digging when other dig jobs are finished
void Plan::wantdig(color_ostream & out, room *r)
{
    if (r->queue_dig || r->status != room_status::plan)
        return;
    ai->debug(out, "wantdig " + describe_room(r));
    r->queue_dig = true;
    r->dig(true);
    tasks_generic.push_back(new task(task_type::want_dig, r));
}

void Plan::digroom(color_ostream & out, room *r)
{
    if (r->status != room_status::plan)
        return;
    ai->debug(out, "digroom " + describe_room(r));
    r->queue_dig = false;
    r->status = room_status::dig;
    fixup_open(out, r);
    r->dig();

    tasks_generic.push_back(new task(task_type::dig_room, r));

    for (auto it = r->accesspath.begin(); it != r->accesspath.end(); it++)
    {
        digroom(out, *it);
    }

    for (auto it = r->layout.begin(); it != r->layout.end(); it++)
    {
        furniture *f = *it;
        if (f->type == layout_type::none || f->type == layout_type::floodgate)
            continue;
        if (f->dig != tile_dig_designation::Default)
            continue;
        tasks_furniture.push_back(new task(task_type::furnish, r, f));
    }

    if (r->type == room_type::workshop)
    {
        find_room(room_type::stockpile, [this, &out, r](room *r_) -> bool
        {
            if (r_->workshop != r)
            {
                return false;
            }

            // dig associated stockpiles
            digroom(out, r_);

            // search all stockpiles in case there's more than one
            return false;
        });
    }

    df::coord size = r->size();
    if (r->type != room_type::corridor || size.z > 1)
    {
        nrdig[r->queue]++;
        if (size.x * size.y * size.z >= 10)
            nrdig[r->queue]++;
    }
}

bool Plan::construct_room(color_ostream & out, room *r)
{
    ai->debug(out, "construct " + describe_room(r));

    if (r->type == room_type::corridor)
    {
        return furnish_room(out, r);
    }

    if (r->type == room_type::stockpile)
    {
        furnish_room(out, r);
        tasks_generic.push_back(new task(task_type::construct_stockpile, r));
        return true;
    }

    if (r->type == room_type::tradedepot)
    {
        tasks_generic.push_back(new task(task_type::construct_tradedepot, r));
        return true;
    }

    if (r->type == room_type::workshop)
    {
        tasks_generic.push_back(new task(task_type::construct_workshop, r));
        return true;
    }

    if (r->type == room_type::furnace)
    {
        tasks_generic.push_back(new task(task_type::construct_furnace, r));
        return true;
    }

    if (r->type == room_type::farmplot)
    {
        return construct_farmplot(out, r);
    }

    if (r->type == room_type::cistern)
    {
        return construct_cistern(out, r);
    }

    if (r->type == room_type::cemetary)
    {
        return furnish_room(out, r);
    }

    if (r->type == room_type::infirmary || r->type == room_type::pasture || r->type == room_type::pitcage || r->type == room_type::location || r->type == room_type::garbagedump)
    {
        furnish_room(out, r);
        if (try_construct_activityzone(out, r))
            return true;
        tasks_generic.push_back(new task(task_type::construct_activityzone, r));
        return true;
    }

    if (r->type == room_type::dininghall)
    {
        if (!r->temporary)
        {
            if (room *t = find_room(room_type::dininghall, [](room *r) -> bool { return r->temporary; }))
            {
                move_dininghall_fromtemp(out, r, t);
            }
        }
        return furnish_room(out, r);
    }

    return furnish_room(out, r);
}

bool Plan::furnish_room(color_ostream &, room *r)
{
    for (auto it = r->layout.begin(); it != r->layout.end(); it++)
    {
        furniture *f = *it;
        tasks_furniture.push_back(new task(task_type::furnish, r, f));
    }
    r->status = room_status::finished;
    return true;
}

const static struct traptypes
{
    std::map<std::string, df::trap_type> map;
    traptypes()
    {
        map["cage"] = trap_type::CageTrap;
        map["lever"] = trap_type::Lever;
        map["trackstop"] = trap_type::TrackStop;
    }
} traptypes;

bool Plan::try_furnish(color_ostream & out, room *r, furniture *f)
{
    if (f->bld_id != -1)
        return true;
    if (f->ignore)
        return true;
    df::coord tgtile = r->min + f->pos;
    df::tiletype tt = *Maps::getTileType(tgtile);
    if (f->construction != construction_type::NONE)
    {
        if (try_furnish_construction(out, f->construction, tgtile))
        {
            if (f->type == layout_type::none)
                return true;
        }
        else
        {
            return false; // don't try to furnish item before construction is done
        }
    }

    if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt)) == tiletype_shape_basic::Wall)
        return false;

    df::building_type building_type = building_type::NONE;
    int building_subtype = -1;
    std::string stocks_furniture_type;

    switch (f->type)
    {
    case layout_type::none:
        return true;

    case layout_type::archery_target:
        return try_furnish_archerytarget(out, r, f, tgtile);
    case layout_type::armor_stand:
        building_type = building_type::Armorstand;
        stocks_furniture_type = "armorstand";
        break;
    case layout_type::bed:
        building_type = building_type::Bed;
        stocks_furniture_type = "bed";
        break;
    case layout_type::bookcase:
        building_type = building_type::Bookcase;
        stocks_furniture_type = "bookcase";
        break;
    case layout_type::cabinet:
        building_type = building_type::Cabinet;
        stocks_furniture_type = "cabinet";
        break;
    case layout_type::cage_trap:
        if (ai->stocks->count["cage"] < 1)
        {
            // avoid too much spam
            return false;
        }
        building_type = building_type::Trap;
        building_subtype = trap_type::CageTrap;
        stocks_furniture_type = "mechanism";
        break;
    case layout_type::chair:
        building_type = building_type::Chair;
        stocks_furniture_type = "chair";
        break;
    case layout_type::chest:
        building_type = building_type::Box;
        stocks_furniture_type = "chest";
        break;
    case layout_type::coffin:
        building_type = building_type::Coffin;
        stocks_furniture_type = "coffin";
        break;
    case layout_type::door:
        building_type = building_type::Door;
        stocks_furniture_type = "door";
        break;
    case layout_type::floodgate:
        // require the floor to be smooth before we build a floodgate on it
        // because we can't smooth a floor under an open floodgate.
        if (!is_smooth(tgtile))
        {
            std::set<df::coord> tiles;
            tiles.insert(tgtile);
            smooth(tiles);
            return false;
        }
        building_type = building_type::Floodgate;
        stocks_furniture_type = "floodgate";
        break;
    case layout_type::gear_assembly:
        building_type = building_type::GearAssembly;
        stocks_furniture_type = "mechanism";
        break;
    case layout_type::hive:
        building_type = building_type::Hive;
        stocks_furniture_type = "hive";
        break;
    case layout_type::lever:
        building_type = building_type::Trap;
        building_subtype = trap_type::Lever;
        stocks_furniture_type = "mechanism";
        break;
    case layout_type::nest_box:
        building_type = building_type::NestBox;
        stocks_furniture_type = "nestbox";
        break;
    case layout_type::roller:
        return try_furnish_roller(out, r, f, tgtile);
    case layout_type::table:
        building_type = building_type::Table;
        stocks_furniture_type = "table";
        break;
    case layout_type::track_stop:
        building_type = building_type::Trap;
        building_subtype = trap_type::TrackStop;
        stocks_furniture_type = "mechanism";
    case layout_type::traction_bench:
        building_type = building_type::TractionBench;
        stocks_furniture_type = "traction_bench";
        break;
    case layout_type::vertical_axle:
        building_type = building_type::AxleVertical;
        stocks_furniture_type = "wood";
        break;
    case layout_type::weapon_rack:
        building_type = building_type::Weaponrack;
        stocks_furniture_type = "weaponrack";
        break;
    case layout_type::well:
        return try_furnish_well(out, r, f, tgtile);
    case layout_type::windmill:
        return try_furnish_windmill(out, r, f, tgtile);

    case layout_type::_layout_type_count:
        return true;
    }

    if (cache_nofurnish.count(stocks_furniture_type))
    {
        return false;
    }

    if (Maps::getTileOccupancy(tgtile)->bits.building != tile_building_occ::None)
    {
        // TODO warn if this stays for too long?
        return false;
    }

    if (ENUM_ATTR(tiletype, shape, tt) == tiletype_shape::RAMP ||
        ENUM_ATTR(tiletype, material, tt) == tiletype_material::TREE ||
        ENUM_ATTR(tiletype, material, tt) == tiletype_material::ROOT)
    {
        dig_tile(tgtile, f->dig);
        return false;
    }

    if (df::item *itm = ai->stocks->find_furniture_item(stocks_furniture_type))
    {
        std::ostringstream str;
        str << "furnish " << f->type << " in " << describe_room(r);
        ai->debug(out, str.str());
        df::building *bld = Buildings::allocInstance(tgtile, building_type, building_subtype);
        Buildings::setSize(bld, df::coord(1, 1, 1));
        std::vector<df::item *> item;
        item.push_back(itm);
        Buildings::constructWithItems(bld, item);
        if (f->makeroom)
        {
            r->bld_id = bld->id;
        }
        f->bld_id = bld->id;
        tasks_furniture.push_back(new task(task_type::check_furnish, r, f));
        return true;
    }

    cache_nofurnish.insert(stocks_furniture_type);
    return false;
}

bool Plan::try_furnish_well(color_ostream &, room *r, furniture *f, df::coord t)
{
    df::item *block, *mecha, *buckt, *chain;
    if (find_item(items_other_id::BLOCKS, block) &&
        find_item(items_other_id::TRAPPARTS, mecha) &&
        find_item(items_other_id::BUCKET, buckt) &&
        find_item(items_other_id::CHAIN, chain))
    {
        df::building *bld = Buildings::allocInstance(t, building_type::Well);
        Buildings::setSize(bld, df::coord(1, 1, 1));
        std::vector<df::item *> items;
        items.push_back(block);
        items.push_back(mecha);
        items.push_back(buckt);
        items.push_back(chain);
        Buildings::constructWithItems(bld, items);
        f->bld_id = bld->id;
        tasks_furniture.push_back(new task(task_type::check_furnish, r, f));
        return true;
    }
    return false;
}

bool Plan::try_furnish_archerytarget(color_ostream &, room *r, furniture *f, df::coord t)
{
    df::item *bould = nullptr;
    if (!find_item(items_other_id::BOULDER, bould, false, true))
        return false;

    df::building *bld = Buildings::allocInstance(t, building_type::ArcheryTarget);
    Buildings::setSize(bld, df::coord(1, 1, 1));
    virtual_cast<df::building_archerytargetst>(bld)->archery_direction = f->pos.y > 2 ? df::building_archerytargetst::TopToBottom : df::building_archerytargetst::BottomToTop;
    std::vector<df::item *> item;
    item.push_back(bould);
    Buildings::constructWithItems(bld, item);
    f->bld_id = bld->id;
    tasks_furniture.push_back(new task(task_type::check_furnish, r, f));
    return true;
}

bool Plan::try_furnish_construction(color_ostream &, df::construction_type ctype, df::coord t)
{
    df::tiletype tt = *Maps::getTileType(t);
    if (ENUM_ATTR(tiletype, material, tt) == tiletype_material::TREE)
    {
        dig_tile(ai->plan->find_tree_base(t));
        return false;
    }

    df::tiletype_shape_basic sb = ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt));
    if (ctype == construction_type::Wall)
    {
        if (sb == tiletype_shape_basic::Wall)
        {
            return true;
        }
    }

    if (ctype == construction_type::Ramp)
    {
        if (sb == tiletype_shape_basic::Ramp)
        {
            return true;
        }
    }

    if (ctype == construction_type::UpStair || ctype == construction_type::DownStair || ctype == construction_type::UpDownStair)
    {
        if (sb == tiletype_shape_basic::Stair)
        {
            return true;
        }
    }

    if (ctype == construction_type::Floor)
    {
        if (sb == tiletype_shape_basic::Floor)
        {
            return true;
        }
        if (sb == tiletype_shape_basic::Ramp || sb == tiletype_shape_basic::Wall)
        {
            dig_tile(t);
            return true;
        }
    }

    // fall through = must build actual construction

    if (ENUM_ATTR(tiletype, material, tt) == tiletype_material::CONSTRUCTION)
    {
        // remove existing invalid construction
        dig_tile(t);
        return false;
    }

    for (auto it = world->buildings.all.begin(); it != world->buildings.all.end(); it++)
    {
        df::building *b = *it;
        if (b->z == t.z && !b->room.extents &&
            b->x1 <= t.x && b->x2 >= t.x &&
            b->y1 <= t.y && b->y2 >= t.y)
        {
            return false;
        }
    }

    df::item *mat = nullptr;
    if (!find_item(items_other_id::BLOCKS, mat))
    {
        if (find_room(room_type::workshop, [](room *r) -> bool { return r->workshop_type == workshop_type::Masons && r->status == room_status::finished && r->dfbuilding() != nullptr; }) != nullptr)
        {
            // we don't have blocks but we can make them.
            return false;
        }
        if (!find_item(items_other_id::BOULDER, mat, false, true))
        {
            return false;
        }
    }

    df::building *bld = Buildings::allocInstance(t, building_type::Construction, ctype);
    Buildings::setSize(bld, df::coord(1, 1, 1));
    std::vector<df::item *> item;
    item.push_back(mat);
    Buildings::constructWithItems(bld, item);
    return true;
}

bool Plan::try_furnish_windmill(color_ostream &, room *r, furniture *f, df::coord t)
{
    if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(t))) != tiletype_shape_basic::Open)
        return false;

    std::vector<df::item *> mat;
    if (!find_items(items_other_id::WOOD, mat, 4))
        return false;

    df::building *bld = Buildings::allocInstance(t - df::coord(1, 1, 0), building_type::Windmill);
    Buildings::setSize(bld, df::coord(3, 3, 1));
    Buildings::constructWithItems(bld, mat);
    f->bld_id = bld->id;
    tasks_furniture.push_back(new task(task_type::check_furnish, r, f));
    return true;
}

bool Plan::try_furnish_roller(color_ostream &, room *r, furniture *f, df::coord t)
{
    df::item *mecha, *chain;
    if (find_item(items_other_id::TRAPPARTS, mecha) &&
        find_item(items_other_id::CHAIN, chain))
    {
        df::building *bld = Buildings::allocInstance(t, building_type::Rollers);
        Buildings::setSize(bld, df::coord(1, 1, 1));
        std::vector<df::item *> items;
        items.push_back(mecha);
        items.push_back(chain);
        Buildings::constructWithItems(bld, items);
        r->bld_id = bld->id;
        f->bld_id = bld->id;
        tasks_furniture.push_back(new task(task_type::check_furnish, r, f));
        return true;
    }
    return false;
}

static void init_managed_workshop(color_ostream &, room *, df::building *bld)
{
    if (auto w = virtual_cast<df::building_workshopst>(bld))
    {
        w->profile.max_general_orders = 10;
    }
    else if (auto f = virtual_cast<df::building_furnacest>(bld))
    {
        f->profile.max_general_orders = 10;
    }
    else if (auto t = virtual_cast<df::building_trapst>(bld))
    {
        t->profile.max_general_orders = 10;
    }
}

bool Plan::try_construct_tradedepot(color_ostream &, room *r)
{
    std::vector<df::item *> boulds;
    if (find_items(items_other_id::BOULDER, boulds, 3, false, true))
    {
        df::building *bld = Buildings::allocInstance(r->min, building_type::TradeDepot);
        Buildings::setSize(bld, r->size());
        Buildings::constructWithItems(bld, boulds);
        r->bld_id = bld->id;
        tasks_generic.push_back(new task(task_type::check_construct, r));
        return true;
    }
    return false;
}

bool Plan::try_construct_workshop(color_ostream & out, room *r)
{
    if (!r->constructions_done())
        return false;

    if (r->workshop_type == workshop_type::Dyers)
    {
        df::item *barrel, *bucket;
        if (find_item(items_other_id::BARREL, barrel) &&
            find_item(items_other_id::BUCKET, bucket))
        {
            df::building *bld = Buildings::allocInstance(r->min, building_type::Workshop, workshop_type::Dyers);
            Buildings::setSize(bld, r->size());
            std::vector<df::item *> items;
            items.push_back(barrel);
            items.push_back(bucket);
            Buildings::constructWithItems(bld, items);
            r->bld_id = bld->id;
            init_managed_workshop(out, r, bld);
            tasks_generic.push_back(new task(task_type::check_construct, r));
            return true;
        }
    }
    else if (r->workshop_type == workshop_type::Ashery)
    {
        df::item *block, *barrel, *bucket;
        if (find_item(items_other_id::BLOCKS, block) &&
            find_item(items_other_id::BARREL, barrel) &&
            find_item(items_other_id::BUCKET, bucket))
        {
            df::building *bld = Buildings::allocInstance(r->min, building_type::Workshop, workshop_type::Ashery);
            Buildings::setSize(bld, r->size());
            std::vector<df::item *> items;
            items.push_back(block);
            items.push_back(barrel);
            items.push_back(bucket);
            Buildings::constructWithItems(bld, items);
            r->bld_id = bld->id;
            init_managed_workshop(out, r, bld);
            tasks_generic.push_back(new task(task_type::check_construct, r));
            return true;
        }
    }
    else if (r->workshop_type == workshop_type::MetalsmithsForge)
    {
        df::item *anvil, *bould;
        if (find_item(items_other_id::ANVIL, anvil, true) &&
            find_item(items_other_id::BOULDER, bould, true, true))
        {
            df::building *bld = Buildings::allocInstance(r->min, building_type::Workshop, workshop_type::MetalsmithsForge);
            Buildings::setSize(bld, r->size());
            std::vector<df::item *> items;
            items.push_back(anvil);
            items.push_back(bould);
            Buildings::constructWithItems(bld, items);
            r->bld_id = bld->id;
            init_managed_workshop(out, r, bld);
            tasks_generic.push_back(new task(task_type::check_construct, r));
            return true;
        }
    }
    else if (r->workshop_type == workshop_type::Quern)
    {
        df::item *quern;
        if (find_item(items_other_id::QUERN, quern))
        {
            df::building *bld = Buildings::allocInstance(r->min, building_type::Workshop, workshop_type::Quern);
            Buildings::setSize(bld, r->size());
            std::vector<df::item *> item;
            item.push_back(quern);
            Buildings::constructWithItems(bld, item);
            r->bld_id = bld->id;
            init_managed_workshop(out, r, bld);
            tasks_generic.push_back(new task(task_type::check_construct, r));
            return true;
        }
    }
    else if (r->workshop_type == workshop_type::Custom)
    {
        auto cursor = std::find_if(world->raws.buildings.all.begin(), world->raws.buildings.all.end(), [r](df::building_def *def) -> bool { return def->code == r->raw_type; });
        df::building_def_workshopst *def = cursor == world->raws.buildings.all.end() ? nullptr : virtual_cast<df::building_def_workshopst>(*cursor);
        if (!def)
        {
            ai->debug(out, "Cannot find workshop type: " + r->raw_type);
            return true;
        }
        std::vector<df::job_item *> filters;
        for (auto it = def->build_items.begin(); it != def->build_items.end(); it++)
        {
            filters.push_back(make_job_item(*it));
        }
        std::vector<df::item *> items;
        if (!find_items(filters, items))
        {
            for (auto it = filters.begin(); it != filters.end(); it++)
            {
                delete *it;
            }
            return false;
        }
        df::building *bld = Buildings::allocInstance(r->min, building_type::Workshop, workshop_type::Custom, def->id);
        Buildings::setSize(bld, r->size());
        Buildings::constructWithFilters(bld, filters);
        r->bld_id = bld->id;
        init_managed_workshop(out, r, bld);
        tasks_generic.push_back(new task(task_type::check_construct, r));
        return true;
    }
    else
    {
        df::item *bould;
        if (find_item(items_other_id::BOULDER, bould, false, true) ||
            // use wood if we can't find stone
            find_item(items_other_id::WOOD, bould))
        {
            df::building *bld = Buildings::allocInstance(r->min, building_type::Workshop, r->workshop_type);
            Buildings::setSize(bld, r->size());
            std::vector<df::item *> item;
            item.push_back(bould);
            Buildings::constructWithItems(bld, item);
            r->bld_id = bld->id;
            init_managed_workshop(out, r, bld);
            tasks_generic.push_back(new task(task_type::check_construct, r));
            return true;
            // XXX else quarry?
        }
    }
    return false;
}

bool Plan::try_construct_furnace(color_ostream & out, room *r)
{
    if (!r->constructions_done())
        return false;

    if (r->furnace_type == furnace_type::Custom)
    {
        auto cursor = std::find_if(world->raws.buildings.all.begin(), world->raws.buildings.all.end(), [r](df::building_def *def) -> bool { return def->code == r->raw_type; });
        df::building_def_furnacest *def = cursor == world->raws.buildings.all.end() ? nullptr : virtual_cast<df::building_def_furnacest>(*cursor);
        if (!def)
        {
            ai->debug(out, "Cannot find furnace type: " + r->raw_type);
            return true;
        }
        std::vector<df::job_item *> filters;
        for (auto it = def->build_items.begin(); it != def->build_items.end(); it++)
        {
            filters.push_back(make_job_item(*it));
        }
        std::vector<df::item *> items;
        if (!find_items(filters, items))
        {
            for (auto it = filters.begin(); it != filters.end(); it++)
            {
                delete *it;
            }
            return false;
        }
        df::building *bld = Buildings::allocInstance(r->min, building_type::Furnace, furnace_type::Custom, def->id);
        Buildings::setSize(bld, r->size());
        Buildings::constructWithFilters(bld, filters);
        r->bld_id = bld->id;
        init_managed_workshop(out, r, bld);
        tasks_generic.push_back(new task(task_type::check_construct, r));
        return true;
    }
    else
    {
        df::item *bould;
        if (find_item(items_other_id::BOULDER, bould, true, true))
        {
            df::building *bld = Buildings::allocInstance(r->min, building_type::Furnace, r->furnace_type);
            Buildings::setSize(bld, r->size());
            std::vector<df::item *> item;
            item.push_back(bould);
            Buildings::constructWithItems(bld, item);
            r->bld_id = bld->id;
            init_managed_workshop(out, r, bld);
            tasks_generic.push_back(new task(task_type::check_construct, r));
            return true;
        }
        return false;
    }
}

bool Plan::try_construct_stockpile(color_ostream & out, room *r)
{
    if (!r->constructions_done())
        return false;

    if (!AI::is_dwarfmode_viewscreen())
        return false;

    int32_t start_x, start_y, start_z;
    Gui::getViewCoords(start_x, start_y, start_z);

    AI::feed_key(interface_key::D_STOCKPILES);
    cursor->x = r->min.x + 1;
    cursor->y = r->min.y;
    cursor->z = r->min.z;
    AI::feed_key(interface_key::CURSOR_LEFT);
    AI::feed_key(interface_key::STOCKPILE_CUSTOM);
    AI::feed_key(interface_key::STOCKPILE_CUSTOM_SETTINGS);
    const static struct stockpile_keys
    {
        std::map<stockpile_type::type, df::stockpile_list> map;

        stockpile_keys()
        {
            map[stockpile_type::animals] = stockpile_list::Animals;
            map[stockpile_type::food] = stockpile_list::Food;
            map[stockpile_type::weapons] = stockpile_list::Weapons;
            map[stockpile_type::armor] = stockpile_list::Armor;
            map[stockpile_type::furniture] = stockpile_list::Furniture;
            map[stockpile_type::corpses] = stockpile_list::Corpses;
            map[stockpile_type::refuse] = stockpile_list::Refuse;
            map[stockpile_type::wood] = stockpile_list::Wood;
            map[stockpile_type::stone] = stockpile_list::Stone;
            map[stockpile_type::gems] = stockpile_list::Gems;
            map[stockpile_type::bars_blocks] = stockpile_list::BarsBlocks;
            map[stockpile_type::cloth] = stockpile_list::Cloth;
            map[stockpile_type::leather] = stockpile_list::Leather;
            map[stockpile_type::ammo] = stockpile_list::Ammo;
            map[stockpile_type::coins] = stockpile_list::Coins;
            map[stockpile_type::finished_goods] = stockpile_list::Goods;
            map[stockpile_type::sheets] = stockpile_list::Sheet;
        }
    } stockpile_keys;
    df::viewscreen_layer_stockpilest *view = strict_virtual_cast<df::viewscreen_layer_stockpilest>(Gui::getCurViewscreen(true));
    df::stockpile_list wanted_group = stockpile_keys.map.at(r->stockpile_type);
    while (view->cur_group != stockpile_list::AdditionalOptions)
    {
        if (view->cur_group == wanted_group)
        {
            AI::feed_key(interface_key::STOCKPILE_SETTINGS_ENABLE);
            AI::feed_key(interface_key::STOCKPILE_SETTINGS_PERMIT_ALL);
        }
        else
        {
            AI::feed_key(interface_key::STOCKPILE_SETTINGS_DISABLE);
        }
        AI::feed_key(interface_key::STANDARDSCROLL_DOWN);
    }
    while (view->cur_group != wanted_group)
    {
        AI::feed_key(interface_key::STANDARDSCROLL_UP);
    }
    AI::feed_key(interface_key::STANDARDSCROLL_RIGHT);
    for (auto it = r->stock_disable.begin(); it != r->stock_disable.end(); it++)
    {
        while (view->cur_list != *it)
        {
            AI::feed_key(interface_key::STANDARDSCROLL_DOWN);
        }
        AI::feed_key(interface_key::STOCKPILE_SETTINGS_FORBID_SUB);
    }
    if (r->stock_specific1)
    {
        AI::feed_key(interface_key::STOCKPILE_SETTINGS_SPECIFIC1);
    }
    if (r->stock_specific2)
    {
        AI::feed_key(interface_key::STOCKPILE_SETTINGS_SPECIFIC2);
    }
    AI::feed_key(interface_key::LEAVESCREEN);
    AI::feed_key(interface_key::SELECT);
    for (int16_t x = r->min.x; x < r->max.x; x++)
    {
        AI::feed_key(interface_key::CURSOR_RIGHT);
    }
    for (int16_t y = r->min.y; y < r->max.y; y++)
    {
        AI::feed_key(interface_key::CURSOR_DOWN);
    }
    AI::feed_key(interface_key::SELECT);
    AI::feed_key(interface_key::LEAVESCREEN);
    ai->camera->ignore_pause(start_x, start_y, start_z);
    df::building_stockpilest *bld = virtual_cast<df::building_stockpilest>(world->buildings.all.back());
    r->bld_id = bld->id;
    furnish_room(out, r);

    if (r->workshop && r->stockpile_type == stockpile_type::stone)
    {
        room_items(out, r, [](df::item *i) { i->flags.bits.dump = 1; });
    }

    if (r->level == 0 &&
        r->stockpile_type != stockpile_type::stone && r->stockpile_type != stockpile_type::wood)
    {
        if (room *rr = find_room(room_type::stockpile, [r](room *o) -> bool { return o->stockpile_type == r->stockpile_type && o->level == 1; }))
        {
            wantdig(out, rr);
        }
    }

    // setup stockpile links with adjacent level
    find_room(room_type::stockpile, [r, bld](room *o) -> bool
    {
        int32_t diff = o->level - r->level;
        if (o->workshop && r->workshop)
        {
            return false;
        }
        if (o->workshop)
        {
            diff = -1;
        }
        else if (r->workshop)
        {
            diff = 1;
        }
        if (o->stockpile_type == r->stockpile_type && diff != 0)
        {
            if (df::building_stockpilest *obld = virtual_cast<df::building_stockpilest>(o->dfbuilding()))
            {
                df::building_stockpilest *b_from, *b_to;
                if (diff > 0)
                {
                    b_from = obld;
                    b_to = bld;
                }
                else
                {
                    b_from = bld;
                    b_to = obld;
                }
                for (auto btf = b_to->links.take_from_pile.begin(); btf != b_to->links.take_from_pile.end(); btf++)
                {
                    if ((*btf)->id == b_from->id)
                    {
                        return false;
                    }
                }
                b_to->links.take_from_pile.push_back(b_from);
                b_from->links.give_to_pile.push_back(b_to);
            }
        }
        return false; // loop on all stockpiles
    });

    return true;
}

bool Plan::try_construct_activityzone(color_ostream & out, room *r)
{
    if (!r->constructions_done())
        return false;

    if (!AI::is_dwarfmode_viewscreen())
        return false;

    df::coord size = r->size();

    df::building_civzonest *bld = virtual_cast<df::building_civzonest>(Buildings::allocInstance(r->min, building_type::Civzone, civzone_type::ActivityZone));
    Buildings::setSize(bld, size);
    delete[] bld->room.extents;
    bld->room.extents = new uint8_t[size.x * size.y]();
    bld->room.x = r->min.x;
    bld->room.y = r->min.y;
    bld->room.width = size.x;
    bld->room.height = size.y;
    memset(bld->room.extents, 1, size.x * size.y);
    Buildings::constructAbstract(bld);
    r->bld_id = bld->id;
    bld->is_room = true;

    bld->zone_flags.bits.active = 1;
    if (r->type == room_type::infirmary)
    {
        bld->zone_flags.bits.hospital = 1;
        bld->hospital.max_splints = 5;
        bld->hospital.max_thread = 75000;
        bld->hospital.max_cloth = 50000;
        bld->hospital.max_crutches = 5;
        bld->hospital.max_plaster = 750;
        bld->hospital.max_buckets = 2;
        bld->hospital.max_soap = 750;
    }
    else if (r->type == room_type::garbagedump)
    {
        bld->zone_flags.bits.garbage_dump = 1;
    }
    else if (r->type == room_type::pasture)
    {
        bld->zone_flags.bits.pen_pasture = 1;
        bld->pit_flags.whole |= 2;
    }
    else if (r->type == room_type::pitcage)
    {
        bld->zone_flags.bits.pit_pond = 1;
    }
    else if (r->type == room_type::location)
    {
        int32_t start_x, start_y, start_z;
        Gui::getViewCoords(start_x, start_y, start_z);

        AI::feed_key(interface_key::D_CIVZONE);

        df::coord pos = r->pos();
        Gui::revealInDwarfmodeMap(pos, true);
        Gui::setCursorCoords(pos.x, pos.y, pos.z);

        AI::feed_key(interface_key::CURSOR_LEFT);
        AI::feed_key(interface_key::CIVZONE_MEETING);
        AI::feed_key(interface_key::ASSIGN_LOCATION);
        AI::feed_key(interface_key::LOCATION_NEW);
        if (r->location_type == location_type::tavern)
        {
            AI::feed_key(interface_key::LOCATION_INN_TAVERN);
        }
        else if (r->location_type == location_type::library)
        {
            AI::feed_key(interface_key::LOCATION_LIBRARY);
        }
        else if (r->location_type == location_type::temple)
        {
            AI::feed_key(interface_key::LOCATION_TEMPLE);
            AI::feed_key(interface_key::SELECT); // no specific deity
        }
        else
        {
            AI::feed_key(interface_key::LEAVESCREEN);
            AI::feed_key(interface_key::LEAVESCREEN);
            std::ostringstream str;
            str << r->location_type;
            ai->debug(out, "[ERROR] unknown location type: " + str.str());
        }
        AI::feed_key(interface_key::LEAVESCREEN);

        ai->camera->ignore_pause(start_x, start_y, start_z);
    }

    return true;
}

bool Plan::construct_farmplot(color_ostream & out, room *r)
{
    for (int16_t x = r->min.x; x <= r->max.x; x++)
    {
        for (int16_t y = r->min.y; y <= r->max.y; y++)
        {
            for (int16_t z = r->min.z; z <= r->max.z; z++)
            {
                if (!farm_allowed_materials.set.count(ENUM_ATTR(tiletype, material, *Maps::getTileType(x, y, z))))
                {
                    df::map_block *block = Maps::getTileBlock(x, y, z);
                    auto e = std::find_if(block->block_events.begin(), block->block_events.end(), [](df::block_square_event *e) -> bool
                    {
                        df::block_square_event_material_spatterst *spatter = virtual_cast<df::block_square_event_material_spatterst>(e);
                        return spatter &&
                            spatter->mat_type == builtin_mats::MUD &&
                            spatter->mat_index == -1;
                    });
                    if (e == block->block_events.end())
                    {
                        df::block_square_event_material_spatterst *spatter = df::allocate<df::block_square_event_material_spatterst>();
                        spatter->mat_type = builtin_mats::MUD;
                        spatter->mat_index = -1;
                        spatter->min_temperature = 60001;
                        spatter->max_temperature = 60001;
                        e = block->block_events.insert(e, spatter);
                    }
                    df::block_square_event_material_spatterst *spatter = virtual_cast<df::block_square_event_material_spatterst>(*e);
                    if (spatter->amount[x & 0xf][y & 0xf] < 50)
                    {
                        ai->debug(out, stl_sprintf("cheat: mud invocation (%d, %d, %d)", x, y, z));
                        spatter->amount[x & 0xf][y & 0xf] = 50; // small pile of mud
                    }
                }
            }
        }
    }

    df::building *bld = Buildings::allocInstance(r->min, building_type::FarmPlot);
    Buildings::setSize(bld, r->size());
    Buildings::constructWithItems(bld, std::vector<df::item *>());
    r->bld_id = bld->id;
    furnish_room(out, r);
    if (room *st = find_room(room_type::stockpile, [r](room *o) -> bool { return o->workshop == r; }))
    {
        digroom(out, st);
    }
    tasks_generic.push_back(new task(task_type::setup_farmplot, r));
    return true;
}

void Plan::move_dininghall_fromtemp(color_ostream &, room *r, room *t)
{
    // if we dug a real hall, get rid of the temporary one
    for (auto f : t->layout)
    {
        if (f->type == layout_type::none || !f->has_users)
        {
            continue;
        }
        for (auto of : r->layout)
        {
            if (of->type == f->type && of->has_users && of->users.empty())
            {
                of->users = f->users;
                if (!f->ignore)
                {
                    of->ignore = false;
                }
                if (df::building *bld = df::building::find(f->bld_id))
                {
                    Buildings::deconstruct(bld);
                }
                break;
            }
        }
    }
    rooms.erase(std::remove(rooms.begin(), rooms.end(), t), rooms.end());
    for (auto it = tasks_generic.begin(); it != tasks_generic.end(); )
    {
        if ((*it)->r == t)
        {
            delete *it;
            if (bg_idx_generic == it)
            {
                bg_idx_generic++;
            }
            tasks_generic.erase(it++);
        }
        else
        {
            it++;
        }
    }
    for (auto it = tasks_furniture.begin(); it != tasks_furniture.end(); )
    {
        if ((*it)->r == t)
        {
            delete *it;
            if (bg_idx_furniture == it)
            {
                bg_idx_furniture++;
            }
            tasks_furniture.erase(it++);
        }
        else
        {
            it++;
        }
    }
    delete t;
    categorize_all();
}

bool Plan::smooth_room(color_ostream &, room *r, bool engrave)
{
    std::set<df::coord> tiles;
    for (int16_t x = r->min.x - 1; x <= r->max.x + 1; x++)
    {
        for (int16_t y = r->min.y - 1; y <= r->max.y + 1; y++)
        {
            for (int16_t z = r->min.z; z <= r->max.z; z++)
            {
                df::coord t(x, y, z);
                if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(t))) != tiletype_shape_basic::Wall ||
                    std::find_if(room_by_z.at(z).begin(), room_by_z.at(z).end(), [t](room *o) -> bool { return o->include(t) && o->dig_mode(t) != tile_dig_designation::No &&
                        std::find_if(o->layout.begin(), o->layout.end(), [t, o](furniture *f) -> bool { return o->min + f->pos == t &&
                            f->dig == tile_dig_designation::No && f->construction == construction_type::Wall; }) == o->layout.end(); }) == room_by_z.at(z).end())
                {
                    tiles.insert(t);
                }
            }
        }
    }
    return smooth(tiles, engrave);
}

// smooth a room and its accesspath corridors (recursive)
void Plan::smooth_room_access(color_ostream & out, room *r)
{
    smooth_room(out, r);
    for (auto it = r->accesspath.begin(); it != r->accesspath.end(); it++)
    {
        smooth_room_access(out, *it);
    }
}

void Plan::smooth_cistern(color_ostream & out, room *r)
{
    for (auto it = r->accesspath.begin(); it != r->accesspath.end(); it++)
    {
        smooth_cistern_access(out, *it);
    }

    std::set<df::coord> tiles;
    for (int16_t z = r->min.z; z <= r->max.z; z++)
    {
        for (int16_t x = r->min.x - 1; x <= r->max.x + 1; x++)
        {
            for (int16_t y = r->min.y - 1; y <= r->max.y + 1; y++)
            {
                if (z != r->min.z &&
                    r->min.x <= x && x <= r->max.x &&
                    r->min.y <= y && y <= r->max.y)
                    continue;

                tiles.insert(df::coord(x, y, z));
            }
        }
    }
    smooth(tiles);
}

// smooth only the inside of the room and any walls, but not adjacent floors
void Plan::smooth_cistern_access(color_ostream & out, room *r)
{
    std::set<df::coord> tiles;
    for (int16_t x = r->min.x - 1; x <= r->max.x + 1; x++)
    {
        for (int16_t y = r->min.y - 1; y <= r->max.y + 1; y++)
        {
            for (int16_t z = r->min.z; z <= r->max.z; z++)
            {
                if (x < r->min.x || r->max.x < x || y < r->min.y || r->max.y < y)
                {
                    if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(x, y, z))) != tiletype_shape_basic::Wall)
                    {
                        continue;
                    }
                }
                tiles.insert(df::coord(x, y, z));
            }
        }
    }
    smooth(tiles);
    for (auto it = r->accesspath.begin(); it != r->accesspath.end(); it++)
    {
        smooth_cistern_access(out, *it);
    }
}

bool Plan::construct_cistern(color_ostream & out, room *r)
{
    furnish_room(out, r);
    smooth_cistern(out, r);

    // remove boulders
    dump_items_access(out, r);

    // build levers for floodgates
    wantdig(out, find_room(room_type::location, [](room *r) -> bool { return r->location_type == location_type::tavern; }));

    // check smoothing progress, channel intermediate levels
    if (r->cistern_type == cistern_type::well)
    {
        tasks_generic.push_back(new task(task_type::dig_cistern, r));
    }

    return true;
}

// marks all items in room and its access for dumping
// return true if any item is found
bool Plan::dump_items_access(color_ostream & out, room *r)
{
    bool found = false;
    room_items(out, r, [&found](df::item *i)
    {
        found = true;
        i->flags.bits.dump = 1;
    });
    for (auto it = r->accesspath.begin(); it != r->accesspath.end(); it++)
    {
        found = dump_items_access(out, *it) || found;
    }
    return found;
}

// yield every on_ground items in the room
void Plan::room_items(color_ostream &, room *r, std::function<void(df::item *)> f)
{
    for (int16_t z = r->min.z; z <= r->max.z; z++)
    {
        for (int16_t x = r->min.x & -16; x <= r->max.x; x += 16)
        {
            for (int16_t y = r->min.y & -16; y <= r->max.y; y += 16)
            {
                auto & items = Maps::getTileBlock(x, y, z)->items;
                for (auto it = items.begin(); it != items.end(); it++)
                {
                    df::item *i = df::item::find(*it);
                    if (i->flags.bits.on_ground &&
                        r->min.x <= i->pos.x && i->pos.x <= r->max.x &&
                        r->min.y <= i->pos.y && i->pos.y <= r->max.y &&
                        z == i->pos.z)
                    {
                        f(i);
                    }
                }
            }
        }
    }
}

bool Plan::smooth_xyz(df::coord min, df::coord max, bool engrave)
{
    std::set<df::coord> tiles;
    for (int16_t x = min.x; x <= max.x; x++)
    {
        for (int16_t y = min.y; y <= max.y; y++)
        {
            for (int16_t z = min.z; z <= max.z; z++)
            {
                tiles.insert(df::coord(x, y, z));
            }
        }
    }
    return smooth(tiles, engrave);
}

bool Plan::smooth(std::set<df::coord> tiles, bool engrave)
{
    bool all_already_smooth = true;

    // remove tiles that are not smoothable
    for (auto it = tiles.begin(); it != tiles.end(); )
    {
        // not a smoothable material
        df::tiletype tt = *Maps::getTileType(*it);
        df::tiletype_material mat = ENUM_ATTR(tiletype, material, tt);
        if (mat != tiletype_material::STONE &&
            mat != tiletype_material::MINERAL)
        {
            tiles.erase(it++);
            continue;
        }

        // already designated for something
        df::tile_designation des = *Maps::getTileDesignation(*it);
        if (des.bits.dig != tile_dig_designation::No ||
            des.bits.smooth != 0 ||
            des.bits.hidden)
        {
            all_already_smooth = false;
            tiles.erase(it++);
            continue;
        }

        // already smooth
        if (is_smooth(*it, engrave))
        {
            tiles.erase(it++);
            continue;
        }

        // wrong shape
        df::tiletype_shape s = ENUM_ATTR(tiletype, shape, tt);
        df::tiletype_shape_basic sb = ENUM_ATTR(tiletype_shape, basic_shape, s);
        if (sb != tiletype_shape_basic::Wall &&
            sb != tiletype_shape_basic::Floor)
        {
            tiles.erase(it++);
            continue;
        }

        it++;
    }

    // remove tiles that are already being smoothed
    for (auto j = world->job_list.next; j != nullptr; j = j->next)
    {
        if (j->item->job_type == job_type::DetailWall ||
            j->item->job_type == job_type::DetailFloor)
        {
            all_already_smooth = false;
            tiles.erase(j->item->pos);
        }
    }

    // mark the tiles to be smoothed!
    for (auto it = tiles.begin(); it != tiles.end(); it++)
    {
        Maps::getTileDesignation(*it)->bits.smooth = engrave ? 2 : 1;
        Maps::getTileOccupancy(*it)->bits.dig_marked = 0;
        Maps::getTileBlock(*it)->flags.bits.designated = 1;
    }

    return all_already_smooth;
}

bool Plan::is_smooth(df::coord t, bool engrave)
{
    df::tiletype tt = *Maps::getTileType(t);
    df::tiletype_material mat = ENUM_ATTR(tiletype, material, tt);
    df::tiletype_shape s = ENUM_ATTR(tiletype, shape, tt);
    df::tiletype_shape_basic sb = ENUM_ATTR(tiletype_shape, basic_shape, s);
    df::tiletype_special sp = ENUM_ATTR(tiletype, special, tt);
    df::tile_occupancy occ = *Maps::getTileOccupancy(t);
    df::tile_building_occ bld = occ.bits.building;
    return mat == tiletype_material::SOIL ||
        mat == tiletype_material::GRASS_LIGHT ||
        mat == tiletype_material::GRASS_DARK ||
        mat == tiletype_material::PLANT ||
        mat == tiletype_material::ROOT ||
        mat == tiletype_material::TREE ||
        mat == tiletype_material::FROZEN_LIQUID ||
        sp == tiletype_special::TRACK ||
        (sp == tiletype_special::SMOOTH && (!engrave || std::find_if(world->engravings.begin(), world->engravings.end(), [t](df::engraving *e) -> bool { return e->pos == t; }) != world->engravings.end())) ||
        s == tiletype_shape::FORTIFICATION ||
        sb == tiletype_shape_basic::Open ||
        sb == tiletype_shape_basic::Stair ||
        (bld != tile_building_occ::None &&
            bld != tile_building_occ::Dynamic);
}

// check smoothing progress, channel intermediate floors when done
bool Plan::try_digcistern(color_ostream & out, room *r)
{
    auto dig_channel = [r](df::coord t)
    {
        if (std::find_if(r->layout.begin(), r->layout.end(), [r, t](furniture *f) -> bool
        {
            return f->dig == tile_dig_designation::Channel &&
                f->pos == t - r->min;
        }) == r->layout.end())
        {
            furniture *f = new furniture();
            f->dig = tile_dig_designation::Channel;
            f->pos = t - r->min;
            r->layout.push_back(f);
        }
        dig_tile(t, tile_dig_designation::Channel);
    };

    // XXX hardcoded layout..
    int32_t cnt = 0;
    int16_t acc_y = r->accesspath[0]->min.y;
    for (int16_t z = r->min.z + 1; z <= r->max.z; z++)
    {
        for (int16_t x = r->max.x; x >= r->min.x; x--)
        {
            bool stop = false;
            for (int16_t y = r->min.y; y <= r->max.y; y++)
            {
                switch (ENUM_ATTR(tiletype_shape, basic_shape,
                    ENUM_ATTR(tiletype, shape,
                        *Maps::getTileType(x, y, z))))
                {
                case tiletype_shape_basic::Floor:
                    stop = true;
                    if (!is_smooth(df::coord(x + 1, y - 1, z)) ||
                        !is_smooth(df::coord(x + 1, y, z)) ||
                        !is_smooth(df::coord(x + 1, y + 1, z)))
                    {
                        continue;
                    }
                    if (ENUM_ATTR(tiletype_shape, basic_shape,
                        ENUM_ATTR(tiletype, shape,
                            *Maps::getTileType(x - 1, y, z))) ==
                        tiletype_shape_basic::Floor)
                    {
                        dig_channel(df::coord(x, y, z));
                    }
                    else
                    {
                        // last column before stairs
                        df::coord nt10(x, y - 1, z);
                        df::coord nt12(x, y + 1, z);
                        df::coord nt00(x - 1, y - 1, z);
                        df::coord nt02(x - 1, y + 1, z);
                        if (y > acc_y ?
                            is_smooth(nt12) && is_smooth(nt02) :
                            is_smooth(nt10) && is_smooth(nt00))
                        {
                            dig_channel(df::coord(x, y, z));
                        }
                    }
                    break;
                case tiletype_shape_basic::Open:
                    if (r->min.x <= x && x <= r->max.x &&
                        r->min.y <= y && y <= r->max.y)
                    {
                        cnt++;
                    }
                    break;
                default:
                    break;
                }
            }
            if (stop)
                break;
        }
    }

    trycistern_count++;
    if (trycistern_count % 12 == 0)
    {
        smooth_cistern(out, r);
    }

    df::coord size = r->size();
    if (cnt == size.x * size.y * (size.z - 1))
    {
        r->channeled = true;
        return true;
    }
    return false;
}

bool Plan::try_setup_farmplot(color_ostream & out, room *r)
{
    df::building *bld = r->dfbuilding();
    if (!bld)
        return true;
    if (bld->getBuildStage() < bld->getMaxBuildStage())
        return false;

    ai->stocks->farmplot(out, r);

    return true;
}

bool Plan::try_endfurnish(color_ostream & out, room *r, furniture *f)
{
    if (!AI::is_dwarfmode_viewscreen())
    {
        // some of these things need to use the UI.
        return false;
    }

    df::building *bld = df::building::find(f->bld_id);
    if (!bld)
    {
        // destroyed building?
        return true;
    }
    if (bld->getBuildStage() < bld->getMaxBuildStage())
        return false;

    if (f->type == layout_type::coffin)
    {
        df::building_coffinst *coffin = virtual_cast<df::building_coffinst>(bld);
        coffin->burial_mode.bits.allow_burial = 1;
        coffin->burial_mode.bits.no_citizens = 0;
        coffin->burial_mode.bits.no_pets = 1;
    }
    else if (f->type == layout_type::door)
    {
        df::building_doorst *door = virtual_cast<df::building_doorst>(bld);
        door->door_flags.bits.pet_passable = 1;
        door->door_flags.bits.internal = f->internal ? 1 : 0;
    }
    else if (f->type == layout_type::lever)
    {
        return setup_lever(out, r, f);
    }
    else if (f->type == layout_type::floodgate)
    {
        if (!f->way.empty())
        {
            for (auto rr : rooms)
            {
                if (rr->status == room_status::plan)
                    continue;
                for (auto ff : rr->layout)
                {
                    if (ff->type == layout_type::lever && ff->target == f)
                    {
                        link_lever(out, ff, f);
                    }
                }
            }
        }
    }
    else if (f->type == layout_type::archery_target)
    {
        f->makeroom = true;
    }

    if (r->type == room_type::infirmary)
    {
        // Toggle hospital off and on because it's easier than figuring out
        // what Dwarf Fortress does. Shouldn't cancel any jobs, but might
        // create jobs if we just built a box.

        int32_t start_x, start_y, start_z;
        Gui::getViewCoords(start_x, start_y, start_z);

        AI::feed_key(interface_key::D_CIVZONE);

        df::coord pos = r->pos();
        Gui::revealInDwarfmodeMap(pos, true);
        Gui::setCursorCoords(pos.x, pos.y, pos.z);

        AI::feed_key(interface_key::CURSOR_LEFT);
        AI::feed_key(interface_key::CIVZONE_HOSPITAL);
        AI::feed_key(interface_key::CIVZONE_HOSPITAL);
        AI::feed_key(interface_key::LEAVESCREEN);

        ai->camera->ignore_pause(start_x, start_y, start_z);
    }

    if (!f->makeroom)
        return true;
    if (!r->is_dug())
        return false;

    ai->debug(out, "makeroom " + describe_room(r));

    df::coord size = r->size() + df::coord(2, 2, 0);

    delete[] bld->room.extents;
    bld->room.extents = new uint8_t[size.x * size.y]();
    bld->room.x = r->min.x - 1;
    bld->room.y = r->min.y - 1;
    bld->room.width = size.x;
    bld->room.height = size.y;
    auto set_ext = [&bld](int16_t x, int16_t y, uint8_t v)
    {
        bld->room.extents[bld->room.width * (y - bld->room.y) + (x - bld->room.x)] = v;
    };
    for (int16_t rx = r->min.x - 1; rx <= r->max.x + 1; rx++)
    {
        for (int16_t ry = r->min.y - 1; ry <= r->max.y + 1; ry++)
        {
            if (ENUM_ATTR(tiletype, shape, *Maps::getTileType(rx, ry, r->min.z)) == tiletype_shape::WALL)
            {
                set_ext(rx, ry, 2);
            }
            else
            {
                set_ext(rx, ry, r->include(df::coord(rx, ry, r->min.z)) ? 3 : 4);
            }
        }
    }
    for (auto f_ : r->layout)
    {
        if (f_->type != layout_type::door)
            continue;
        df::coord t = r->min + f_->pos;
        set_ext(t.x, t.y, 0);
        // tile in front of the door tile is 4 (TODO door in corner...)
        if (t.x < r->min.x)
            set_ext(t.x + 1, t.y, 4);
        if (t.x > r->max.x)
            set_ext(t.x - 1, t.y, 4);
        if (t.y < r->min.y)
            set_ext(t.x, t.y + 1, 4);
        if (t.y > r->max.y)
            set_ext(t.x, t.y - 1, 4);
    }
    bld->is_room = true;

    set_owner(out, r, r->owner);
    furnish_room(out, r);

    if (r->type == room_type::dininghall)
    {
        virtual_cast<df::building_tablest>(bld)->table_flags.bits.meeting_hall = 1;
    }
    else if (r->type == room_type::barracks)
    {
        df::building *bld = r->dfbuilding();
        if (f->type == layout_type::archery_target)
        {
            bld = df::building::find(f->bld_id);
        }
        if (r->squad_id != -1 && bld)
        {
            assign_barrack_squad(out, bld, r->squad_id);
        }
    }

    return true;
}

bool Plan::setup_lever(color_ostream & out, room *r, furniture *f)
{
    if (r->type == room_type::location && r->location_type == location_type::tavern)
    {
        std::string way = f->way;
        if (!f->target)
        {
            room *cistern = find_room(room_type::cistern, [](room *r) -> bool { return r->cistern_type == cistern_type::reserve; });
            for (auto gate : cistern->layout)
            {
                if (gate->type == layout_type::floodgate && gate->way == way)
                {
                    f->target = gate;
                    break;
                }
            }
        }

        if (link_lever(out, f, f->target))
        {
            pull_lever(out, f);
            if (way == "in")
            {
                tasks_generic.push_back(new task(task_type::monitor_cistern));
            }

            return true;
        }
    }
    return false;
}

bool Plan::link_lever(color_ostream &, furniture *src, furniture *dst)
{
    if (src->bld_id == -1 || dst->bld_id == -1)
        return false;
    df::building *bld = df::building::find(src->bld_id);
    if (!bld || bld->getBuildStage() < bld->getMaxBuildStage())
        return false;
    df::building *tbld = df::building::find(dst->bld_id);
    if (!tbld || tbld->getBuildStage() < tbld->getMaxBuildStage())
        return false;

    for (auto ref = bld->general_refs.begin(); ref != bld->general_refs.end(); ref++)
    {
        df::general_ref_building_triggertargetst *tt = virtual_cast<df::general_ref_building_triggertargetst>(*ref);
        if (tt && tt->building_id == tbld->id)
            return false;
    }
    for (auto j = bld->jobs.begin(); j != bld->jobs.end(); j++)
    {
        if ((*j)->job_type == job_type::LinkBuildingToTrigger)
        {
            for (auto ref = (*j)->general_refs.begin(); ref != (*j)->general_refs.end(); ref++)
            {
                df::general_ref_building_triggertargetst *tt = virtual_cast<df::general_ref_building_triggertargetst>(*ref);
                if (tt && tt->building_id == tbld->id)
                    return false;
            }
        }
    }

    std::vector<df::item *> mechas;
    if (!find_items(items_other_id::TRAPPARTS, mechas, 2))
        return false;

    df::general_ref_building_triggertargetst *reflink = df::allocate<df::general_ref_building_triggertargetst>();
    reflink->building_id = tbld->id;
    df::general_ref_building_holderst *refhold = df::allocate<df::general_ref_building_holderst>();
    refhold->building_id = bld->id;

    df::job *job = df::allocate<df::job>();
    job->job_type = job_type::LinkBuildingToTrigger;
    job->general_refs.push_back(reflink);
    job->general_refs.push_back(refhold);
    bld->jobs.push_back(job);
    Job::linkIntoWorld(job);

    Job::attachJobItem(job, mechas[0], df::job_item_ref::LinkToTarget);
    Job::attachJobItem(job, mechas[1], df::job_item_ref::LinkToTrigger);

    return true;
}

bool Plan::pull_lever(color_ostream & out, furniture *f)
{
    df::building *bld = df::building::find(f->bld_id);
    if (!bld)
    {
        ai->debug(out, "cistern: missing lever " + f->way);
        return false;
    }
    if (!bld->jobs.empty())
    {
        ai->debug(out, "cistern: lever has job " + f->way);
        return false;
    }
    ai->debug(out, "cistern: pull lever " + f->way);

    df::general_ref_building_holderst *ref = df::allocate<df::general_ref_building_holderst>();
    ref->building_id = bld->id;

    df::job *job = df::allocate<df::job>();
    job->job_type = job_type::PullLever;
    job->pos = df::coord(bld->x1, bld->y1, bld->z);
    job->general_refs.push_back(ref);
    bld->jobs.push_back(job);
    Job::linkIntoWorld(job);
    return true;
}

void Plan::monitor_cistern(color_ostream & out)
{
    if (!m_c_lever_in)
    {
        room *well = find_room(room_type::location, [](room *r) -> bool { return r->location_type == location_type::tavern; });
        for (auto f : well->layout)
        {
            if (f->type == layout_type::lever)
            {
                if (f->way == "in")
                    m_c_lever_in = f;
                else if (f->way == "out")
                    m_c_lever_out = f;
            }
        }
        m_c_cistern = find_room(room_type::cistern, [](room *r) -> bool { return r->cistern_type == cistern_type::well; });
        m_c_reserve = find_room(room_type::cistern, [](room *r) -> bool { return r->cistern_type == cistern_type::reserve; });
        if (m_c_reserve->channel_enable.isValid())
        {
            m_c_testgate_delay = m_c_cistern->channeled ? -1 : 2;
        }
    }

    df::building_trapst *l_in = nullptr, *l_out = nullptr;
    df::building_floodgatest *f_in = nullptr, *f_out = nullptr;
    if (m_c_lever_in->bld_id != -1)
        l_in = virtual_cast<df::building_trapst>(df::building::find(m_c_lever_in->bld_id));
    if (m_c_lever_in->target && m_c_lever_in->target->bld_id != -1)
        f_in = virtual_cast<df::building_floodgatest>(df::building::find(m_c_lever_in->target->bld_id));
    if (m_c_lever_out->bld_id != -1)
        l_out = virtual_cast<df::building_trapst>(df::building::find(m_c_lever_out->bld_id));
    if (m_c_lever_out->target && m_c_lever_out->target->bld_id != -1)
        f_out = virtual_cast<df::building_floodgatest>(df::building::find(m_c_lever_out->target->bld_id));

    if (l_in && f_in && !f_out && !l_in->linked_mechanisms.empty())
    {
        if (!f_in->gate_flags.bits.closing && !f_in->gate_flags.bits.closed)
        {
            if (!f_in->gate_flags.bits.opening)
            {
                pull_lever(out, m_c_lever_out);
            }
            return;
        }

        // f_in is linked, can furnish f_out now without risking walling
        // workers in the reserve
        for (auto l = m_c_reserve->layout.begin(); l != m_c_reserve->layout.end(); l++)
        {
            (*l)->ignore = false;
        }
        furnish_room(out, m_c_reserve);
    }

    if (!l_in || !f_in || !l_out || !f_out)
        return;
    if (l_in->linked_mechanisms.empty() || l_out->linked_mechanisms.empty())
        return;
    if (!l_in->jobs.empty() || !l_out->jobs.empty())
        return;

    // may use mechanisms for entrance cage traps now
    bool furnish_entrance = false;
    for (auto f = fort_entrance->layout.begin(); f != fort_entrance->layout.end(); f++)
    {
        if ((*f)->ignore)
        {
            (*f)->ignore = false;
            furnish_entrance = true;
        }
    }
    if (furnish_entrance)
    {
        furnish_room(out, fort_entrance);
    }

    bool f_in_closed = (f_in->gate_flags.bits.closed || f_in->gate_flags.bits.closing) && !f_in->gate_flags.bits.opening;
    bool f_out_closed = (f_out->gate_flags.bits.closed || f_out->gate_flags.bits.closing) && !f_out->gate_flags.bits.opening;

    if (m_c_testgate_delay != -1)
    {
        // expensive, dont check too often
        m_c_testgate_delay--;
        if (m_c_testgate_delay > 0)
            return;

        // re-dump garbage + smooth reserve accesspath
        construct_cistern(out, m_c_reserve);

        df::coord gate = m_c_reserve->channel_enable;
        if (ENUM_ATTR(tiletype_shape, basic_shape,
            ENUM_ATTR(tiletype, shape,
                *Maps::getTileType(gate))) ==
            tiletype_shape_basic::Wall)
        {
            ai->debug(out, "cistern: test channel");
            bool empty = true;
            std::list<room *> todo;
            todo.push_back(m_c_reserve);
            while (empty && !todo.empty())
            {
                room *r = todo.front();
                todo.pop_front();
                todo.insert(todo.end(), r->accesspath.begin(), r->accesspath.end());
                for (int16_t x = r->min.x - 1; empty && x <= r->max.x + 1; x++)
                {
                    for (int16_t y = r->min.y - 1; empty && y <= r->max.y + 1; y++)
                    {
                        for (int16_t z = r->min.z; z <= r->max.z; z++)
                        {
                            df::coord t(x, y, z);
                            if (!is_smooth(t) && Maps::getTileDesignation(t)->bits.flow_size < 3 && (r->type != room_type::corridor || (r->min.x <= x && x <= r->max.x && r->min.y <= y && y <= r->max.y) || ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(t))) == tiletype_shape_basic::Wall))
                            {
                                ai->debug(out, stl_sprintf("cistern: unsmoothed %s (%d, %d, %d) %s", enum_item_key_str(*Maps::getTileType(t)), x, y, z, describe_room(r).c_str()));
                                empty = false;
                                break;
                            }
                            df::tile_occupancy occ = *Maps::getTileOccupancy(t);
                            if (occ.bits.unit || occ.bits.unit_grounded)
                            {
                                for (auto u = world->units.active.begin(); u != world->units.active.end(); u++)
                                {
                                    if (Units::getPosition(*u) == t)
                                    {
                                        ai->debug(out, stl_sprintf("cistern: unit (%d, %d, %d) (%s) %s", x, y, z, AI::describe_unit(*u).c_str(), describe_room(r).c_str()));
                                        break;
                                    }
                                }
                                empty = false;
                                break;
                            }
                            if (occ.bits.item)
                            {
                                auto & items = Maps::getTileBlock(t)->items;
                                for (auto it = items.begin(); it != items.end(); it++)
                                {
                                    df::item *i = df::item::find(*it);
                                    if (Items::getPosition(i) == t)
                                    {
                                        ai->debug(out, stl_sprintf("cistern: item (%d, %d, %d) (%s) %s", x, y, z, AI::describe_item(i).c_str(), describe_room(r).c_str()));
                                    }
                                }
                                empty = false;
                                break;
                            }
                        }
                    }
                }
            }

            if (empty && !dump_items_access(out, m_c_reserve))
            {
                if (!f_out_closed)
                {
                    // avoid floods. wait for the output to be closed.
                    pull_lever(out, m_c_lever_out);
                    m_c_testgate_delay = 4;
                }
                else
                {
                    if (f_in_closed)
                    {
                        pull_lever(out, m_c_lever_in);
                    }
                    ai->debug(out, "cistern: do channel");
                    cistern_channel_requested = true;
                    dig_tile(gate + df::coord(0, 0, 1), tile_dig_designation::Channel);
                    m_c_testgate_delay = 16;
                }
            }
            else
            {
                room *well = find_room(room_type::location, [](room *r) -> bool { return r->location_type == location_type::tavern; });
                if (Maps::getTileDesignation(well->min + df::coord(-2, well->size().y / 2, 0))->bits.flow_size == 7)
                {
                    // something went not as planned, but we have a water source
                    m_c_testgate_delay = -1;
                }
                else
                {
                    if (!cistern_channel_requested)
                    {
                        // make sure we can actually access the cistern
                        if (f_in_closed)
                            pull_lever(out, m_c_lever_in);
                        if (f_out_closed)
                            pull_lever(out, m_c_lever_out);
                    }
                    m_c_testgate_delay = 16;
                }
            }
        }
        else
        {
            m_c_testgate_delay = -1;
            // channel surrounding tiles
            for (int16_t x = -1; x <= 1; x++)
            {
                for (int16_t y = -1; y <= 1; y++)
                {
                    df::tiletype tt = *Maps::getTileType(gate + df::coord(x, y, 0));
                    if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt)) != tiletype_shape_basic::Wall)
                        continue;
                    df::tiletype_material tm = ENUM_ATTR(tiletype, material, tt);
                    if (tm != tiletype_material::STONE && tm != tiletype_material::MINERAL && tm != tiletype_material::SOIL && tm != tiletype_material::ROOT)
                        continue;
                    if (spiral_search(gate + df::coord(x, y, 0), 1, 1, [](df::coord ttt) -> bool { df::tile_designation *td = Maps::getTileDesignation(ttt); return td && td->bits.feature_local; }).isValid())
                    {
                        dig_tile(gate + df::coord(x, y, 1), tile_dig_designation::Channel);
                    }
                }
            }
        }
        return;
    }

    if (!m_c_cistern->channeled)
        return;

    // cistlvl = water level for the top floor
    // if it is flooded, reserve output tile is probably > 4 and prevents
    // buildingdestroyers from messing with the floodgates
    uint32_t cistlvl = Maps::getTileDesignation(m_c_cistern->pos() + df::coord(0, 0, 1))->bits.flow_size;
    uint32_t resvlvl = Maps::getTileDesignation(m_c_reserve->pos())->bits.flow_size;

    df::coord river = spiral_search(m_c_reserve->channel_enable, 0, 2, [](df::coord t) -> bool { df::tile_designation *td = Maps::getTileDesignation(t); return td && td->bits.feature_local; });
    bool river_is_frozen_or_dry = river.isValid() && (ENUM_ATTR(tiletype, material, *Maps::getTileType(river)) == tiletype_material::FROZEN_LIQUID || Maps::getTileDesignation(river)->bits.flow_size == 0);

    if (resvlvl <= 1)
    {
        // reserve empty, fill it
        if (!f_out_closed)
        {
            pull_lever(out, m_c_lever_out);
        }
        else if (f_in_closed)
        {
            pull_lever(out, m_c_lever_in);
        }
    }
    else if (resvlvl == 7 || river_is_frozen_or_dry)
    {
        // reserve full, empty into cistern if needed
        if (!f_in_closed)
        {
            pull_lever(out, m_c_lever_in);
        }
        else
        {
            if (cistlvl < 7)
            {
                if (f_out_closed)
                {
                    pull_lever(out, m_c_lever_out);
                }
            }
            else
            {
                if (!f_out_closed)
                {
                    pull_lever(out, m_c_lever_out);
                }
            }
        }
    }
    else
    {
        // ensure it's either filling up or emptying
        if (f_in_closed && f_out_closed)
        {
            if (resvlvl >= 6 && cistlvl < 7)
            {
                pull_lever(out, m_c_lever_out);
            }
            else
            {
                pull_lever(out, m_c_lever_in);
            }
        }
    }
}

bool Plan::try_endconstruct(color_ostream & out, room *r)
{
    df::building *bld = r->dfbuilding();
    if (bld && bld->getBuildStage() < bld->getMaxBuildStage())
        return false;
    furnish_room(out, r);
    return true;
}

// returns one tile of an outdoor river (if one exists)
df::coord Plan::scan_river(color_ostream &)
{
    df::feature_init_outdoor_riverst *ifeat = nullptr;
    for (auto f = world->features.map_features.begin(); f != world->features.map_features.end(); f++)
    {
        ifeat = virtual_cast<df::feature_init_outdoor_riverst>(*f);
        if (ifeat)
            break;
    }
    df::coord invalid;
    invalid.clear();
    if (!ifeat)
        return invalid;
    df::feature_outdoor_riverst *feat = ifeat->feature;

    for (size_t i = 0; i < feat->embark_pos.x.size(); i++)
    {
        int16_t x = 48 * (feat->embark_pos.x[i] - world->map.region_x);
        int16_t y = 48 * (feat->embark_pos.y[i] - world->map.region_y);
        if (x < 0 || x >= world->map.x_count ||
            y < 0 || y >= world->map.y_count)
            continue;
        int16_t z1 = feat->min_map_z[i];
        int16_t z2 = feat->max_map_z[i];
        for (int16_t z = z1; z <= z2; z++)
        {
            for (int16_t dx = 0; dx < 48; dx++)
            {
                for (int16_t dy = 0; dy < 48; dy++)
                {
                    df::coord t(x + dx, y + dy, z);
                    if (Maps::getTileDesignation(t)->bits.feature_local)
                    {
                        return t;
                    }
                }
            }
        }
    }

    return invalid;
}

command_result Plan::make_map_walkable(color_ostream &)
{
    events.onupdate_register_once("df-ai plan make_map_walkable", [this](color_ostream & out) -> bool
    {
        if (!Core::getInstance().isMapLoaded())
        {
            return true;
        }

        if (find_room(room_type::corridor, [](room *r) -> bool { return r->corridor_type == corridor_type::walkable && r->status == room_status::dig; }))
        {
            return false;
        }

        // if we don't have a river, we're fine
        df::coord river = scan_river(out);
        if (!river.isValid())
            return true;
        df::coord surface = surface_tile_at(river.x, river.y);
        if (surface.isValid() && ENUM_ATTR(tiletype, material, *Maps::getTileType(surface)) == tiletype_material::BROOK)
            return true;

        Plan *plan = this;

        river = spiral_search(river, [plan](df::coord t) -> bool
        {
            // TODO rooms outline
            int16_t y = plan->fort_entrance->pos().y;
            if (t.y >= y + Plan::MinY && t.y <= y + Plan::MaxY)
                return false;
            df::tile_designation *td = Maps::getTileDesignation(t);
            return td && td->bits.feature_local;
        });
        if (!river.isValid())
            return true;

        // find a safe place for the first tile
        df::coord t1 = spiral_search(river, [plan](df::coord t) -> bool
        {
            if (!plan->map_tile_in_rock(t))
                return false;
            df::coord st = plan->surface_tile_at(t.x, t.y);
            return plan->map_tile_in_rock(st + df::coord(0, 0, -1));
        });
        if (!t1.isValid())
            return true;
        t1 = surface_tile_at(t1.x, t1.y);

        // if the game hasn't done any pathfinding yet, wait until the next frame and try again
        uint16_t t1w = getTileWalkable(t1);
        if (t1w == 0)
            return false;

        // find the second tile
        df::coord t2 = spiral_search(t1, [t1w](df::coord t) -> bool
        {
            uint16_t tw = Plan::getTileWalkable(t);
            return tw != 0 && tw != t1w;
        });
        if (!t2.isValid())
            return true;
        uint16_t t2w = getTileWalkable(t2);

        // make sure the second tile is in a safe place
        t2 = spiral_search(t2, [plan, t2w](df::coord t) -> bool
        {
            return Plan::getTileWalkable(t) == t2w && plan->map_tile_in_rock(t - df::coord(0, 0, 1));
        });
        if (!t2.isValid())
            return true;

        // find the bottom of the staircases
        int16_t z = -1;
        for (auto f = world->features.map_features.begin(); f != world->features.map_features.end(); f++)
        {
            df::feature_init_outdoor_riverst *r = virtual_cast<df::feature_init_outdoor_riverst>(*f);
            if (r)
            {
                z = *std::min_element(r->feature->min_map_z.begin(), r->feature->min_map_z.end()) - 1;
                break;
            }
        }

        // make the corridors
        std::vector<room *> cor;
        cor.push_back(new room(corridor_type::walkable, t1, df::coord(t1, z)));
        cor.push_back(new room(corridor_type::walkable, t2, df::coord(t2, z)));

        int16_t dx = t1.x - t2.x;
        if (-1 > dx)
            cor.push_back(new room(corridor_type::walkable, df::coord(t1.x + 1, t1.y, z), df::coord(t2.x - 1, t1.y, z)));
        else if (dx > 1)
            cor.push_back(new room(corridor_type::walkable, df::coord(t1.x - 1, t1.y, z), df::coord(t2.x + 1, t1.y, z)));

        int16_t dy = t1.y - t2.y;
        if (-1 > dy)
            cor.push_back(new room(corridor_type::walkable, df::coord(t2.x, t1.y + 1, z), df::coord(t2.x, t2.y - 1, z)));
        else if (dy > 1)
            cor.push_back(new room(corridor_type::walkable, df::coord(t2.x, t1.y - 1, z), df::coord(t2.x, t2.y + 1, z)));

        if ((-1 > dx || dx > 1) && (-1 > dy || dy > 1))
            cor.push_back(new room(corridor_type::walkable, df::coord(t2.x, t1.y, z), df::coord(t2.x, t1.y, z)));

        for (auto c = cor.begin(); c != cor.end(); c++)
        {
            corridors.push_back(*c);
            digroom(out, *c); // skip the queue
        }
        return true;
    });
    return CR_OK;
}

// scan the map, list all map veins in @map_veins (mat_index => [block coords],
// sorted by z)
command_result Plan::list_map_veins(color_ostream &)
{
    map_veins.clear();
    for (int16_t z = 0; z < world->map.z_count; z++)
    {
        for (int16_t xb = 0; xb < world->map.x_count_block; xb++)
        {
            for (int16_t yb = 0; yb < world->map.y_count_block; yb++)
            {
                df::map_block *block = world->map.block_index[xb][yb][z];
                if (!block)
                {
                    continue;
                }
                for (auto event = block->block_events.begin(); event != block->block_events.end(); event++)
                {
                    df::block_square_event_mineralst *vein = virtual_cast<df::block_square_event_mineralst>(*event);
                    if (vein)
                    {
                        map_veins[vein->inorganic_mat].insert(block->map_pos);
                    }
                }
            }
        }
    }
    return CR_OK;
}

static int32_t mat_index_vein(df::coord t)
{
    auto & events = Maps::getTileBlock(t)->block_events;
    for (auto it = events.rbegin(); it != events.rend(); it++)
    {
        df::block_square_event_mineralst *mineral = virtual_cast<df::block_square_event_mineralst>(*it);
        if (mineral && mineral->getassignment(t.x & 0xf, t.y & 0xf))
        {
            return mineral->inorganic_mat;
        }
    }
    return -1;
}

// mark a vein of a mat for digging, return expected boulder count
int32_t Plan::dig_vein(color_ostream & out, int32_t mat, int32_t want_boulders)
{
    // mat => [x, y, z, dig_mode] marked for to dig
    int32_t count = 0;
    // check previously queued veins
    if (map_vein_queue.count(mat))
    {
        auto & q = map_vein_queue.at(mat);
        q.erase(std::remove_if(q.begin(), q.end(), [this, mat, &count, &out](std::pair<df::coord, df::tile_dig_designation> d) -> bool
        {
            df::tiletype tt = *Maps::getTileType(d.first);
            df::tiletype_shape_basic sb = ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt));
            if (sb == tiletype_shape_basic::Open)
            {
                df::construction_type ctype;
                if (d.second == tile_dig_designation::Default)
                {
                    ctype = construction_type::Floor;
                }
                else if (!find_enum_item(&ctype, ENUM_KEY_STR(tile_dig_designation, d.second)))
                {
                    ai->debug(out, "[ERROR] could not find construction_type::" + ENUM_KEY_STR(tile_dig_designation, d.second));
                    return false;
                }
                return try_furnish_construction(out, ctype, d.first);
            }
            if (sb != tiletype_shape_basic::Wall)
            {
                return true;
            }
            if (Maps::getTileDesignation(d.first)->bits.dig == tile_dig_designation::No) // warm/wet tile
                dig_tile(d.first, d.second);
            if (ENUM_ATTR(tiletype, material, tt) == tiletype_material::MINERAL && mat_index_vein(d.first) == mat)
                count++;
            return false;
        }), q.end());
        if (q.empty())
        {
            map_vein_queue.erase(mat);
        }
    }

    if (!map_veins.count(mat))
    {
        return count / 4;
    }

    // queue new vein
    // delete it from map_veins
    // discard tiles that would dig into a plan room/corridor, or into a cavern
    // (hidden + !wall)
    // try to find a vein close to one we already dug
    for (size_t i = 0; i < 16; i++)
    {
        if (count / 4 >= want_boulders)
            break;
        auto & veins = map_veins.at(mat);
        auto it = std::find_if(veins.begin(), veins.end(), [this](df::coord c) -> bool
        {
            for (int16_t vx = -1; vx <= 1; vx++)
            {
                for (int16_t vy = -1; vy <= 1; vy++)
                {
                    if (dug_veins.count(c + df::coord(16 * vx, 16 * vy, 0)))
                    {
                        return true;
                    }
                }
            }
            return false;
        });
        if (it == veins.end())
            it--;
        df::coord v = *it;
        map_veins.at(mat).erase(it);
        int32_t cnt = do_dig_vein(out, mat, v);
        if (cnt > 0)
        {
            dug_veins.insert(v);
        }
        count += cnt;
        if (map_veins.at(mat).empty())
        {
            map_veins.erase(mat);
            break;
        }
    }

    return count / 4;
}

int32_t Plan::do_dig_vein(color_ostream & out, int32_t mat, df::coord b)
{
    ai->debug(out, "dig_vein " + world->raws.inorganics[mat]->id);
    int32_t count = 0;
    int16_t fort_minz = 0x7fff;
    for (auto c = corridors.begin(); c != corridors.end(); c++)
    {
        if ((*c)->corridor_type != corridor_type::veinshaft)
        {
            fort_minz = std::min(fort_minz, (*c)->min.z);
        }
    }
    if (b.z >= fort_minz)
    {
        int16_t y = fort_entrance->pos().y;
        // TODO rooms outline
        if (b.y > y + MinY && b.y < y + MaxY)
        {
            return count;
        }
    }

    auto & q = map_vein_queue[mat];

    // dig whole block
    // TODO have the dwarves search for the vein
    // TODO mine in (visible?) chunks
    int16_t minx = 16, maxx = -1, miny = 16, maxy = -1;
    for (int16_t dx = 0; dx < 16; dx++)
    {
        if (b.x + dx == 0 || b.x + dx >= world->map.x_count - 1)
            continue;
        for (int16_t dy = 0; dy < 16; dy++)
        {
            if (b.y + dy == 0 || b.y + dy >= world->map.y_count - 1)
                continue;
            df::coord t = b + df::coord(dx, dy, 0);
            if (ENUM_ATTR(tiletype, material, *Maps::getTileType(t)) == tiletype_material::MINERAL && mat_index_vein(t) == mat)
            {
                minx = std::min(minx, dx);
                maxx = std::max(maxx, dx);
                miny = std::min(miny, dy);
                maxy = std::max(maxy, dy);
            }
        }
    }

    if (minx > maxx)
        return count;

    bool need_shaft = true;
    std::vector<std::pair<df::coord, df::tile_dig_designation>> todo;
    for (int16_t dx = minx; dx <= maxx; dx++)
    {
        for (int16_t dy = miny; dy <= maxy; dy++)
        {
            df::coord t = b + df::coord(dx, dy, 0);
            if (Maps::getTileDesignation(t)->bits.dig == tile_dig_designation::No)
            {
                bool ok = true;
                bool ns = need_shaft;
                for (int16_t ddx = -1; ddx <= 1; ddx++)
                {
                    for (int16_t ddy = -1; ddy <= 1; ddy++)
                    {
                        df::coord tt = t + df::coord(ddx, ddy, 0);
                        if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(tt))) != tiletype_shape_basic::Wall)
                        {
                            if (Maps::getTileDesignation(tt)->bits.hidden)
                                ok = false;
                            else
                                ns = false;
                        }
                        else if (Maps::getTileDesignation(tt)->bits.dig != tile_dig_designation::No)
                        {
                            ns = false;
                        }
                    }
                }
                if (ok)
                {
                    todo.push_back(std::make_pair(t, tile_dig_designation::Default));
                    if (ENUM_ATTR(tiletype, material, *Maps::getTileType(t)) == tiletype_material::MINERAL && mat_index_vein(t) == mat)
                        count++;
                    need_shaft = ns;
                }
            }
        }
    }
    for (auto d = todo.begin(); d != todo.end(); d++)
    {
        q.push_back(*d);
        dig_tile(d->first, d->second);
    }

    if (need_shaft)
    {
        // TODO minecarts?

        // avoid giant vertical runs: slalom x+-1 every z%16

        df::coord vert = fort_entrance->pos();
        if (b.y > vert.y)
        {
            vert = df::coord(vert.x, vert.y + 30, b.z); // XXX 30...
        }
        else
        {
            vert = df::coord(vert.x, vert.y - 30, b.z);
        }
        if (vert.z % 32 > 16)
            vert.x++; // XXX check this

        df::coord t0 = b + df::coord((minx + maxx) / 2, (miny + maxy) / 2, 0);
        while (t0.y != vert.y)
        {
            dig_tile(t0, tile_dig_designation::Default);
            q.push_back(std::make_pair(t0, tile_dig_designation::Default));
            if (t0.y > vert.y)
                t0.y--;
            else
                t0.y++;
        }
        while (t0.x != vert.x)
        {
            dig_tile(t0, tile_dig_designation::Default);
            q.push_back(std::make_pair(t0, tile_dig_designation::Default));
            if (t0.x > vert.x)
                t0.x--;
            else
                t0.x++;
        }
        while (Maps::getTileDesignation(t0)->bits.hidden)
        {
            if (t0.z % 16 == 0)
            {
                dig_tile(t0, tile_dig_designation::DownStair);
                q.push_back(std::make_pair(t0, tile_dig_designation::DownStair));
                if (t0.z % 32 >= 16)
                    t0.x++;
                else
                    t0.x--;
                dig_tile(t0, tile_dig_designation::UpStair);
                q.push_back(std::make_pair(t0, tile_dig_designation::UpStair));
            }
            else
            {
                dig_tile(t0, tile_dig_designation::UpDownStair);
                q.push_back(std::make_pair(t0, tile_dig_designation::UpDownStair));
            }
            t0.z++;
        }
        if (!ENUM_ATTR(tiletype_shape, passable_low, ENUM_ATTR(tiletype, shape, *Maps::getTileType(t0))) && Maps::getTileDesignation(t0)->bits.dig == tile_dig_designation::No)
        {
            dig_tile(t0, tile_dig_designation::DownStair);
            q.push_back(std::make_pair(t0, tile_dig_designation::DownStair));
        }
    }

    return count;
}

// same as ruby spiral_search, but search starting with center of each side
df::coord Plan::spiral_search(df::coord t, int16_t max, int16_t min, int16_t step, std::function<bool(df::coord)> b)
{
    if (min == 0)
    {
        if (b(t))
            return t;
        min += step;
    }
    const static struct sides
    {
        std::vector<df::coord> vec;
        sides()
        {
            vec.push_back(df::coord(0, 1, 0));
            vec.push_back(df::coord(1, 0, 0));
            vec.push_back(df::coord(0, -1, 0));
            vec.push_back(df::coord(-1, 0, 0));
        };
    } sides;
    for (int16_t r = min; r <= max; r += step)
    {
        for (auto it = sides.vec.begin(); it != sides.vec.end(); it++)
        {
            df::coord tt = t + *it * r;
            if (Maps::isValidTilePos(tt.x, tt.y, tt.z) && b(tt))
                return tt;
        }

        for (size_t s = 0; s < sides.vec.size(); s++)
        {
            df::coord dr = sides.vec[(s + sides.vec.size() - 1) % sides.vec.size()];
            df::coord dv = sides.vec[s];

            for (int16_t v = -r; v < r; v += step)
            {
                if (v == 0)
                    continue;

                df::coord tt = t + dr * r + dv * v;
                if (Maps::isValidTilePos(tt.x, tt.y, tt.z) && b(tt))
                    return tt;
            }
        }
    }

    df::coord invalid;
    invalid.clear();
    return invalid;
}

// check that tile is surrounded by solid rock/soil walls
bool Plan::map_tile_in_rock(df::coord tile)
{
    for (int16_t dx = -1; dx <= 1; dx++)
    {
        for (int16_t dy = -1; dy <= 1; dy++)
        {
            df::tiletype *tt = Maps::getTileType(tile + df::coord(dx, dy, 0));
            if (!tt)
            {
                return false;
            }

            if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *tt)) != tiletype_shape_basic::Wall)
            {
                return false;
            }

            df::tiletype_material tm = ENUM_ATTR(tiletype, material, *tt);
            if (tm != tiletype_material::STONE && tm != tiletype_material::MINERAL && tm != tiletype_material::SOIL && tm != tiletype_material::ROOT && (!allow_ice || tm != tiletype_material::FROZEN_LIQUID))
            {
                return false;
            }
        }
    }
    return true;
}

// check tile is surrounded by solid walls or visible tile
bool Plan::map_tile_nocavern(df::coord tile)
{
    for (int16_t dx = -1; dx <= 1; dx++)
    {
        for (int16_t dy = -1; dy <= 1; dy++)
        {
            df::coord t = tile + df::coord(dx, dy, 0);
            df::tile_designation *td = Maps::getTileDesignation(t);
            if (!td)
            {
                continue;
            }


            if (!td->bits.hidden)
            {
                if (td->bits.flow_size >= 4)
                    return false;
                if (!allow_ice && ENUM_ATTR(tiletype, material, *Maps::getTileType(t)) == tiletype_material::FROZEN_LIQUID)
                    return false;
                continue;
            }

            df::tiletype tt = *Maps::getTileType(t);

            if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt)) != tiletype_shape_basic::Wall)
            {
                return false;
            }

            df::tiletype_material tm = ENUM_ATTR(tiletype, material, tt);
            if (tm != tiletype_material::STONE && tm != tiletype_material::MINERAL && tm != tiletype_material::SOIL && tm != tiletype_material::ROOT && (!allow_ice || tm != tiletype_material::FROZEN_LIQUID))
            {
                return false;
            }
        }
    }
    return true;
}

// check tile is a hidden floor
bool Plan::map_tile_cavernfloor(df::coord t)
{
    df::tile_designation *td = Maps::getTileDesignation(t);
    if (!td)
        return false;
    if (!td->bits.hidden)
        return false;
    if (td->bits.flow_size != 0)
        return false;
    df::tiletype tt = *Maps::getTileType(t);
    df::tiletype_material tm = ENUM_ATTR(tiletype, material, tt);
    if (tm != tiletype_material::STONE && tm != tiletype_material::MINERAL && tm != tiletype_material::SOIL && tm != tiletype_material::ROOT && tm != tiletype_material::GRASS_LIGHT && tm != tiletype_material::GRASS_DARK && tm != tiletype_material::PLANT && tm != tiletype_material::SOIL)
        return false;
    return ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt)) == tiletype_shape_basic::Floor;
}

bool Plan::map_tile_undiscovered_cavern(df::coord t)
{
    df::tile_designation *td = Maps::getTileDesignation(t);
    if (!td->bits.feature_global)
    {
        return false;
    }

    auto region = df::world_underground_region::find(Maps::getTileBlock(t)->global_feature);
    if (!region || !region->feature_init)
    {
        return false;
    }
    return !region->feature_init->flags.is_set(feature_init_flags::Discovered);
}

df::coord Plan::surface_tile_at(int16_t tx, int16_t ty, bool allow_trees)
{
    int16_t dx = tx & 0xf;
    int16_t dy = ty & 0xf;

    bool tree = false;
    for (int16_t z = world->map.z_count - 1; z >= 0; z--)
    {
        df::map_block *b = Maps::getTileBlock(tx, ty, z);
        if (!b)
            continue;
        df::tiletype tt = b->tiletype[dx][dy];
        df::tiletype_shape ts = ENUM_ATTR(tiletype, shape, tt);
        df::tiletype_shape_basic tsb = ENUM_ATTR(tiletype_shape, basic_shape, ts);
        if (tsb == tiletype_shape_basic::Open)
            continue;
        df::tiletype_material tm = ENUM_ATTR(tiletype, material, tt);
        if (tm == tiletype_material::POOL || tm == tiletype_material::RIVER)
        {
            df::coord invalid;
            invalid.clear();
            return invalid;
        }
        if (tm == tiletype_material::TREE)
        {
            tree = true;
        }
        else if (tsb == tiletype_shape_basic::Floor || tsb == tiletype_shape_basic::Ramp)
        {
            return df::coord(tx, ty, z);
        }
        else if (tree)
        {
            df::coord c(tx, ty, z + 1);
            if (!allow_trees)
                c.clear();
            return c;
        }
    }
    df::coord invalid;
    invalid.clear();
    return invalid;
}

std::string Plan::status()
{
    std::map<task_type::type, size_t> task_count;
    std::map<layout_type::type, std::set<df::coord>> furnishing;
    for (auto t : tasks_generic)
    {
        task_count[t->type]++;
    }
    for (auto t : tasks_furniture)
    {
        task_count[t->type]++;
        if (!t->f->type == layout_type::none)
        {
            furnishing[t->f->type].insert(t->r->min + t->f->pos);
        }
    }
    std::ostringstream s;
    bool first = true;
    for (auto tc = task_count.begin(); tc != task_count.end(); tc++)
    {
        if (first)
        {
            first = false;
        }
        else
        {
            s << ", ";
        }
        s << tc->first << ": " << tc->second;
    }
    if (task *t = is_digging())
    {
        if (t->type == task_type::dig_room)
        {
            s << ", digging: " << describe_room(t->r);
        }
        else if (t->type == task_type::want_dig)
        {
            s << ", waiting to dig: " << describe_room(t->r);
        }
    }
    first = true;
    for (auto f = furnishing.begin(); f != furnishing.end(); f++)
    {
        if (first)
        {
            first = false;
            s << "\nfurnishing: ";
        }
        else
        {
            s << ", ";
        }
        s << f->first << ": " << f->second.size();
    }
    return s.str();
}

std::string Plan::report()
{
    std::ostringstream s;

    s << "## Tasks\n";
    for (auto it = tasks_generic.begin(); it != tasks_generic.end(); it++)
    {
        if (bg_idx_generic == it)
        {
            s << "--- current position (generic) ---\n";
        }

        task *t = *it;
        s << "- " << t->type << "\n";
        if (t->r != nullptr)
        {
            s << "  " << describe_room(t->r) << "\n";
        }
        if (t->f != nullptr)
        {
            s << "  " << describe_furniture(t->f) << "\n";
        }
    }
    if (bg_idx_generic == tasks_generic.end())
    {
        s << "--- current position (generic) ---\n";
    }
    s << "\n";
    for (auto it = tasks_furniture.begin(); it != tasks_furniture.end(); it++)
    {
        if (bg_idx_furniture == it)
        {
            s << "--- current position (furniture) ---\n";
        }

        task *t = *it;
        s << "- " << t->type << "\n";
        if (t->r != nullptr)
        {
            s << "  " << describe_room(t->r) << "\n";
        }
        if (t->f != nullptr)
        {
            s << "  " << describe_furniture(t->f) << "\n";
        }
    }
    if (bg_idx_furniture == tasks_furniture.end())
    {
        s << "--- current position (furniture) ---\n";
    }
    s << "\n";

    return s.str();
}

void Plan::categorize_all()
{
    room_category.clear();
    room_by_z.clear();
    for (auto r : rooms)
    {
        room_category[r->type].push_back(r);
        for (int32_t z = r->min.z; z <= r->max.z; z++)
        {
            room_by_z[z].insert(r);
        }
        for (auto f : r->layout)
        {
            room_by_z[r->min.z + f->pos.z].insert(r);
        }
    }
    for (auto r : corridors)
    {
        for (int32_t z = r->min.z; z <= r->max.z; z++)
        {
            room_by_z[z].insert(r);
        }
        for (auto f : r->layout)
        {
            room_by_z[r->min.z + f->pos.z].insert(r);
        }
    }

    if (room_category.count(room_type::stockpile))
    {
        auto & stockpiles = room_category.at(room_type::stockpile);
        std::sort(stockpiles.begin(), stockpiles.end(), [this](room *a, room *b) -> bool
        {
            if (a->level < b->level)
                return true;
            if (a->level > b->level)
                return false;

            int16_t ax = a->min.x < fort_entrance->min.x ? -a->min.x : a->min.x;
            int16_t bx = b->min.x < fort_entrance->min.x ? -b->min.x : b->min.x;
            if (ax < bx)
                return true;
            if (ax > bx)
                return false;

            return a->min.y < b->min.y;
        });
    }
}

std::string Plan::describe_room(room *r)
{
    if (!r)
    {
        return "(unknown room)";
    }

    std::ostringstream s;
    s << r->type;
    switch (r->type)
    {
    case room_type::cistern:
        s << " (" << r->cistern_type << ")";
        break;
    case room_type::corridor:
        s << " (" << r->corridor_type << ")";
        break;
    case room_type::farmplot:
        s << " (" << r->farm_type << ")";
        break;
    case room_type::furnace:
        if (r->furnace_type == furnace_type::Custom)
        {
            s << " (\"" << r->raw_type << "\")";
        }
        else
        {
            s << " (" << enum_item_key(r->furnace_type) << ")";
        }
        break;
    case room_type::location:
        s << " (" << r->location_type << ")";
        if (auto civzone = virtual_cast<df::building_civzonest>(r->dfbuilding()))
        {
            if (auto site = df::world_site::find(civzone->site_id))
            {
                if (auto loc = binsearch_in_vector(site->buildings, civzone->location_id))
                {
                    if (auto name = loc->getName())
                    {
                        s << " (" << AI::describe_name(*name, false) << " \"" << AI::describe_name(*name, true) << "\")";
                    }
                }
            }
        }
        break;
    case room_type::nobleroom:
        s << " (" << r->nobleroom_type << ")";
        break;
    case room_type::outpost:
        s << " (" << r->outpost_type << ")";
        break;
    case room_type::stockpile:
        s << " (" << r->stockpile_type << ")";
        break;
    case room_type::workshop:
        if (r->workshop_type == workshop_type::Custom)
        {
            s << " (\"" << r->raw_type << "\")";
        }
        else
        {
            s << " (" << enum_item_key(r->workshop_type) << ")";
        }
        break;
    default:
        break;
    }

    if (!r->comment.empty())
    {
        s << " (" << r->comment << ")";
    }

    if (df::unit *u = df::unit::find(r->owner))
    {
        s << " (owned by " << AI::describe_unit(u) << ")";
    }

    if (r->noblesuite != -1)
    {
        s << " (noble suite " << r->noblesuite << ")";
    }

    if (df::squad *squad = df::squad::find(r->squad_id))
    {
        s << " (used by " << AI::describe_name(squad->name, true) << ")";
    }

    if (r->level != -1)
    {
        s << " (level " << r->level << ")";
    }

    if (r->workshop)
    {
        s << " (" << describe_room(r->workshop) << ")";
    }

    if (r->has_users)
    {
        s << " (" << r->users.size() << " users)";
    }

    s << " (" << r->status << ")";

    return s.str();
}

std::string Plan::describe_furniture(furniture *f)
{
    if (!f)
    {
        return "(unknown furniture)";
    }

    std::ostringstream s;

    if (f->type != layout_type::none)
    {
        s << f->type;
    }
    else
    {
        s << "[no furniture]";
    }

    if (f->construction != construction_type::NONE)
    {
        s << " (construct " << ENUM_KEY_STR(construction_type, f->construction) << ")";
    }

    if (f->dig != tile_dig_designation::Default)
    {
        s << " (dig " << ENUM_KEY_STR(tile_dig_designation, f->dig) << ")";
    }

    if (!f->direction.empty())
    {
        s << " (" << f->direction << ")";
    }

    if (!f->way.empty())
    {
        s << " (" << f->way << ")";
    }

    if (f->has_users)
    {
        s << " (" << f->users.size() << " users)";
    }

    if (f->ignore)
    {
        s << " (ignored)";
    }

    if (f->makeroom)
    {
        s << " (room)";
    }

    if (f->internal)
    {
        s << " (internal)";
    }

    s << " (" << f->pos.x << ", " << f->pos.y << ", " << f->pos.z << ")";

    if (f->target != nullptr)
    {
        s << " (" << describe_furniture(f->target) << ")";
    }

    return s.str();
}

room *Plan::find_room(room_type::type type)
{
    if (room_category.empty())
    {
        for (auto r = rooms.begin(); r != rooms.end(); r++)
        {
            if ((*r)->type == type)
            {
                return *r;
            }
        }
        return nullptr;
    }

    if (room_category.count(type))
    {
        return room_category.at(type).front();
    }

    return nullptr;
}

room *Plan::find_room(room_type::type type, std::function<bool(room *)> b)
{
    if (room_category.empty())
    {
        for (auto r = rooms.begin(); r != rooms.end(); r++)
        {
            if ((*r)->type == type && b(*r))
            {
                return *r;
            }
        }
        return nullptr;
    }

    if (room_category.count(type))
    {
        auto & rs = room_category.at(type);
        for (auto r = rs.begin(); r != rs.end(); r++)
        {
            if (b(*r))
            {
                return *r;
            }
        }
    }

    return nullptr;
}

room *Plan::find_room_at(df::coord t)
{
    if (room_by_z.empty())
    {
        for (auto r = rooms.begin(); r != rooms.end(); r++)
        {
            if ((*r)->safe_include(t))
            {
                return *r;
            }
        }
        for (auto r = corridors.begin(); r != corridors.end(); r++)
        {
            if ((*r)->safe_include(t))
            {
                return *r;
            }
        }
        return nullptr;
    }

    auto by_z = room_by_z.find(t.z);
    if (by_z == room_by_z.end())
    {
        return nullptr;
    }
    for (auto r = by_z->second.begin(); r != by_z->second.end(); r++)
    {
        if ((*r)->safe_include(t))
        {
            return *r;
        }
    }
    return nullptr;
}

bool Plan::map_tile_intersects_room(df::coord t)
{
    return find_room_at(t) != nullptr;
}

df::coord Plan::find_tree_base(df::coord t)
{
    auto find = [t](df::plant *tree) -> bool
    {
        if (tree->pos == t)
        {
            return true;
        }

        if (!tree->tree_info || !tree->tree_info->body)
        {
            return false;
        }

        df::coord s = tree->pos - df::coord(tree->tree_info->dim_x / 2, tree->tree_info->dim_y / 2, 0);
        if (t.x < s.x || t.y < s.y || t.z < s.z ||
            t.x >= s.x + tree->tree_info->dim_x ||
            t.y >= s.y + tree->tree_info->dim_y ||
            t.z >= s.z + tree->tree_info->body_height)
        {
            return false;
        }

        if (!tree->tree_info->body[t.z - s.z])
        {
            return false;
        }
        df::plant_tree_tile tile = tree->tree_info->body[t.z - s.z][(t.x - s.x) + tree->tree_info->dim_x * (t.y - s.y)];
        return tile.whole != 0 && !tile.bits.blocked;
    };

    for (auto tree = world->plants.tree_dry.begin(); tree != world->plants.tree_dry.end(); tree++)
    {
        if (find(*tree))
        {
            return (*tree)->pos;
        }
    }

    for (auto tree = world->plants.tree_wet.begin(); tree != world->plants.tree_wet.end(); tree++)
    {
        if (find(*tree))
        {
            return (*tree)->pos;
        }
    }

    df::coord invalid;
    invalid.clear();
    return invalid;
}

void Plan::fixup_open(color_ostream & out, room *r)
{
    if (r->type == room_type::pasture)
    {
        return;
    }

    for (int16_t x = r->min.x; x <= r->max.x; x++)
    {
        for (int16_t y = r->min.y; y <= r->max.y; y++)
        {
            for (int16_t z = r->min.z; z <= r->max.z; z++)
            {
                df::coord t(x, y, z);
                for (auto it = r->layout.begin(); it != r->layout.end(); it++)
                {
                    furniture *f = *it;
                    df::coord ft = r->min + f->pos;
                    if (t == ft)
                    {
                        if (f->construction == construction_type::NONE)
                        {
                            fixup_open_tile(out, r, ft, f->dig, f);
                        }
                        t.clear();
                        break;
                    }
                }
                if (t.isValid())
                {
                    fixup_open_tile(out, r, t, r->dig_mode(t));
                }
            }
        }
    }
}

void Plan::fixup_open_tile(color_ostream & out, room *r, df::coord t, df::tile_dig_designation d, furniture *f)
{
    df::tiletype *tt = Maps::getTileType(t);
    if (!tt)
        return;

    df::tiletype_shape_basic sb = ENUM_ATTR(tiletype_shape, basic_shape,
        ENUM_ATTR(tiletype, shape, *tt));

    switch (d)
    {
    case tile_dig_designation::Channel:
        // do nothing
        break;
    case tile_dig_designation::No:
        if (sb == tiletype_shape_basic::Open || sb == tiletype_shape_basic::Floor)
        {
            fixup_open_helper(out, r, t, construction_type::Wall, f);
        }
        break;
    case tile_dig_designation::Default:
        if (sb == tiletype_shape_basic::Open)
        {
            fixup_open_helper(out, r, t, construction_type::Floor, f);
        }
        break;
    case tile_dig_designation::UpDownStair:
        if (sb == tiletype_shape_basic::Open || sb == tiletype_shape_basic::Floor)
        {
            fixup_open_helper(out, r, t, construction_type::UpDownStair, f);
        }
        break;
    case tile_dig_designation::UpStair:
        if (sb == tiletype_shape_basic::Open || sb == tiletype_shape_basic::Floor)
        {
            fixup_open_helper(out, r, t, construction_type::UpStair, f);
        }
        break;
    case tile_dig_designation::Ramp:
        if (sb == tiletype_shape_basic::Open || sb == tiletype_shape_basic::Floor)
        {
            fixup_open_helper(out, r, t, construction_type::Ramp, f);
        }
        break;
    case tile_dig_designation::DownStair:
        if (sb == tiletype_shape_basic::Open)
        {
            fixup_open_helper(out, r, t, construction_type::DownStair, f);
        }
        break;
    }
}

void Plan::fixup_open_helper(color_ostream & out, room *r, df::coord t, df::construction_type c, furniture *f)
{
    if (!f)
    {
        f = new furniture();
        f->pos = t - r->min;
        r->layout.push_back(f);
    }
    if (f->construction != c)
    {
        ai->debug(out, stl_sprintf("plan fixup_open %s %s(%d, %d, %d)", describe_room(r).c_str(), ENUM_KEY_STR(construction_type, c).c_str(), f->pos.x, f->pos.y, f->pos.z));
        tasks_furniture.push_back(new task(task_type::furnish, r, f));
    }
    f->construction = c;
}

// XXX
bool Plan::corridor_include_hack(const room *r, df::coord t)
{
    for (auto c = corridors.begin(); c != corridors.end(); c++)
    {
        if (!(*c)->include(t))
        {
            continue;
        }

        if ((*c)->min.z != (*c)->max.z)
        {
            return true;
        }
        for (auto a = (*c)->accesspath.begin(); a != (*c)->accesspath.end(); a++)
        {
            if (*a == r)
            {
                return true;
            }
        }
    }
    return false;
}
