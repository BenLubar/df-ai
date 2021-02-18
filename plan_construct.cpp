#include "ai.h"
#include "plan.h"
#include "debug.h"

#include "modules/Buildings.h"
#include "modules/Job.h"
#include "modules/Maps.h"

#include "df/block_square_event_material_spatterst.h"
#include "df/building_archerytargetst.h"
#include "df/building_civzonest.h"
#include "df/building_coffinst.h"
#include "df/building_def_furnacest.h"
#include "df/building_def_item.h"
#include "df/building_def_workshopst.h"
#include "df/building_doorst.h"
#include "df/building_furnacest.h"
#include "df/building_hatchst.h"
#include "df/building_stockpilest.h"
#include "df/building_tablest.h"
#include "df/building_trapst.h"
#include "df/building_workshopst.h"
#include "df/builtin_mats.h"
#include "df/general_ref_building_holderst.h"
#include "df/general_ref_building_triggertargetst.h"
#include "df/item_boulderst.h"
#include "df/job.h"
#include "df/job_item.h"
#include "df/map_block.h"
#include "df/plant.h"
#include "df/ui.h"
#include "df/ui_sidebar_menus.h"
#include "df/viewscreen_dwarfmodest.h"
#include "df/viewscreen_layer_stockpilest.h"
#include "df/world.h"

REQUIRE_GLOBAL(cursor);
REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(ui_sidebar_menus);
REQUIRE_GLOBAL(world);

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

// Not perfect, but it should at least cut down on cancellation spam.
static bool find_items(const std::vector<df::job_item *> & filters, std::vector<df::item *> & items, std::ostream & reason)
{
    bool found_all = true;

    for (auto filter : filters)
    {
        int32_t found = 0;

        for (auto i : world->items.other[items_other_id::IN_PLAY])
        {
            if (std::find(items.begin(), items.end(), i) != items.end())
            {
                continue;
            }

            ItemTypeInfo iinfo(i);
            MaterialInfo minfo(i);
            if (!iinfo.matches(*filter, &minfo, true))
            {
                continue;
            }

            items.push_back(i);
            found++;

            if (filter->quantity >= found)
            {
                break;
            }
        }

        if (filter->quantity < found)
        {
            if (found_all)
            {
                reason << "could not find ";
                found_all = false;
            }
            else
            {
                reason << " or ";
            }

            reason << ItemTypeInfo(filter->item_type, filter->item_subtype).toString();
        }
    }
    return found_all;
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

bool Plan::construct_room(color_ostream & out, room *r)
{
    ai.debug(out, "construct " + AI::describe_room(r));

    if (r->required_value > 0)
    {
        add_task(task_type::monitor_room_value, r);
    }

    if (r->type == room_type::corridor)
    {
        return furnish_room(out, r);
    }

    if (r->type == room_type::stockpile)
    {
        furnish_room(out, r);
        add_task(task_type::construct_stockpile, r);
        return true;
    }

    if (r->type == room_type::tradedepot)
    {
        add_task(task_type::construct_tradedepot, r);
        return true;
    }

    if (r->type == room_type::workshop)
    {
        add_task(task_type::construct_workshop, r);
        return true;
    }

    if (r->type == room_type::furnace)
    {
        add_task(task_type::construct_furnace, r);
        return true;
    }

    if (r->type == room_type::farmplot)
    {
        add_task(task_type::construct_farmplot, r);
        return true;
    }

    if (r->type == room_type::windmill)
    {
        add_task(task_type::construct_windmill, r);
        return true;
    }

    if (r->type == room_type::cistern)
    {
        return construct_cistern(out, r);
    }

    if (r->type == room_type::cemetery)
    {
        return furnish_room(out, r);
    }

    if (r->type == room_type::infirmary || r->type == room_type::pasture || r->type == room_type::pitcage || r->type == room_type::pond || r->type == room_type::location || r->type == room_type::garbagedump)
    {
        furnish_room(out, r);
        std::ostringstream discard;
        if (try_construct_activityzone(out, r, discard))
            return true;
        add_task(task_type::construct_activityzone, r);
        return true;
    }

    if (r->type == room_type::dininghall)
    {
        if (!r->temporary)
        {
            if (room *t = ai.find_room(room_type::dininghall, [](room *r) -> bool { return r->temporary; }))
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
        add_task(task_type::furnish, r, f);
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

bool Plan::try_furnish(color_ostream & out, room *r, furniture *f, std::ostream & reason)
{
    if (f->bld_id != -1)
        return true;
    if (f->ignore)
        return true;

    df::coord tgtile = r->min + f->pos;
    DFAI_ASSERT_VALID_TILE(tgtile, " (furniture position for " << AI::describe_furniture(f) << " in room " << AI::describe_room(r) << ")");

    df::tiletype tt = *Maps::getTileType(tgtile);
    if (f->construction != construction_type::NONE)
    {
        if (try_furnish_construction(out, f->construction, tgtile, reason))
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
    {
        reason << "waiting for wall to be excavated";
        return false;
    }

    df::building_type building_type = building_type::NONE;
    int building_subtype = -1;
    stock_item::item stocks_furniture_type;

    switch (f->type)
    {
    case layout_type::none:
        return true;

    case layout_type::archery_target:
        return try_furnish_archerytarget(out, r, f, tgtile, reason);
    case layout_type::armor_stand:
        building_type = building_type::Armorstand;
        stocks_furniture_type = stock_item::armor_stand;
        break;
    case layout_type::bed:
        building_type = building_type::Bed;
        stocks_furniture_type = stock_item::bed;
        break;
    case layout_type::bookcase:
        building_type = building_type::Bookcase;
        stocks_furniture_type = stock_item::bookcase;
        break;
    case layout_type::cabinet:
        building_type = building_type::Cabinet;
        stocks_furniture_type = stock_item::cabinet;
        break;
    case layout_type::cage:
        building_type = building_type::Cage;
        stocks_furniture_type = stock_item::cage_metal;
        break;
    case layout_type::cage_trap:
        if (ai.stocks.count_free[stock_item::cage] < 1)
        {
            // avoid too much spam
            reason << "no empty cages available";
            return false;
        }
        building_type = building_type::Trap;
        building_subtype = trap_type::CageTrap;
        stocks_furniture_type = stock_item::mechanism;
        break;
    case layout_type::chair:
        building_type = building_type::Chair;
        stocks_furniture_type = stock_item::chair;
        break;
    case layout_type::chest:
        building_type = building_type::Box;
        stocks_furniture_type = stock_item::chest;
        break;
    case layout_type::coffin:
        building_type = building_type::Coffin;
        stocks_furniture_type = stock_item::coffin;
        break;
    case layout_type::door:
        building_type = building_type::Door;
        stocks_furniture_type = stock_item::door;
        break;
    case layout_type::floodgate:
        // require the floor to be smooth before we build a floodgate on it
        // because we can't smooth a floor under an open floodgate.
        if (!is_smooth(tgtile))
        {
            std::set<df::coord> tiles;
            tiles.insert(tgtile);
            smooth(tiles);
            reason << "floor under floodgate is not smooth";
            return false;
        }
        building_type = building_type::Floodgate;
        stocks_furniture_type = stock_item::floodgate;
        break;
    case layout_type::gear_assembly:
        building_type = building_type::GearAssembly;
        stocks_furniture_type = stock_item::mechanism;
        break;
    case layout_type::hatch:
        building_type = building_type::Hatch;
        stocks_furniture_type = stock_item::hatch_cover;
        break;
    case layout_type::hive:
        building_type = building_type::Hive;
        stocks_furniture_type = stock_item::hive;
        break;
    case layout_type::lever:
        building_type = building_type::Trap;
        building_subtype = trap_type::Lever;
        stocks_furniture_type = stock_item::mechanism;
        break;
    case layout_type::nest_box:
        building_type = building_type::NestBox;
        stocks_furniture_type = stock_item::nest_box;
        break;
    case layout_type::restraint:
        building_type = building_type::Chain;
        stocks_furniture_type = stock_item::rope;
        break;
    case layout_type::roller:
        return try_furnish_roller(out, r, f, tgtile, reason);
    case layout_type::statue:
        building_type = building_type::Statue;
        stocks_furniture_type = stock_item::statue;
        break;
    case layout_type::table:
        building_type = building_type::Table;
        stocks_furniture_type = stock_item::table;
        break;
    case layout_type::track_stop:
        building_type = building_type::Trap;
        building_subtype = trap_type::TrackStop;
        stocks_furniture_type = stock_item::mechanism;
        break;
    case layout_type::traction_bench:
        building_type = building_type::TractionBench;
        stocks_furniture_type = stock_item::traction_bench;
        break;
    case layout_type::vertical_axle:
        building_type = building_type::AxleVertical;
        stocks_furniture_type = stock_item::wood;
        break;
    case layout_type::weapon_rack:
        building_type = building_type::Weaponrack;
        stocks_furniture_type = stock_item::weapon_rack;
        break;
    case layout_type::well:
        return try_furnish_well(out, r, f, tgtile, reason);

    case layout_type::_layout_type_count:
        return true;
    }

    if (cache_nofurnish.count(stocks_furniture_type))
    {
        reason << "no " << stocks_furniture_type << " available";
        return false;
    }

    if (Maps::getTileOccupancy(tgtile)->bits.building != tile_building_occ::None)
    {
        // TODO warn if this stays for too long?
        reason << "tile occupied by building";
        return false;
    }

    if (ENUM_ATTR(tiletype, shape, tt) == tiletype_shape::RAMP)
    {
        AI::dig_tile(tgtile, f->dig);
        reason << "tile occupied by ramp";
        return false;
    }
    auto tm = ENUM_ATTR(tiletype, material, tt);
    if (tm == tiletype_material::TREE || tm == tiletype_material::ROOT)
    {
        AI::dig_tile(tgtile, f->dig);
        reason << "tile occupied by " << enum_item_key(tm);
        return false;
    }

    if (df::item *itm = ai.stocks.find_free_item(stocks_furniture_type))
    {
        std::ostringstream str;
        str << "furnish " << AI::describe_furniture(f) << " in " << AI::describe_room(r);
        ai.debug(out, str.str());
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
        add_task(task_type::check_furnish, r, f);
        return true;
    }

    cache_nofurnish.insert(stocks_furniture_type);
    reason << "no " << stocks_furniture_type << " available";
    return false;
}

bool Plan::try_furnish_well(color_ostream &, room *r, furniture *f, df::coord t, std::ostream & reason)
{
    df::item *block = nullptr;
    df::item *mecha = nullptr;
    df::item *buckt = nullptr;
    df::item *chain = nullptr;
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
        add_task(task_type::check_furnish, r, f);
        return true;
    }
    reason << "missing: ";
    if (!block)
    {
        reason << "block";
    }
    if (!mecha)
    {
        if (block)
        {
            reason << ", ";
        }
        reason << "mechanisms";
    }
    if (!buckt)
    {
        if (block || mecha)
        {
            reason << ", ";
        }
        reason << "bucket";
    }
    if (!chain)
    {
        if (block || mecha || buckt)
        {
            reason << ", ";
        }
        reason << "rope/chain";
    }
    return false;
}

bool Plan::try_furnish_archerytarget(color_ostream &, room *r, furniture *f, df::coord t, std::ostream & reason)
{
    df::item *bould = nullptr;
    if (!find_item(items_other_id::BOULDER, bould, false, true))
    {
        reason << "no boulder available";
        return false;
    }

    df::building *bld = Buildings::allocInstance(t, building_type::ArcheryTarget);
    Buildings::setSize(bld, df::coord(1, 1, 1));
    virtual_cast<df::building_archerytargetst>(bld)->archery_direction = f->pos.y > 2 ? df::building_archerytargetst::TopToBottom : df::building_archerytargetst::BottomToTop;
    std::vector<df::item *> item;
    item.push_back(bould);
    Buildings::constructWithItems(bld, item);
    f->bld_id = bld->id;
    add_task(task_type::check_furnish, r, f);
    return true;
}

bool Plan::try_furnish_construction(color_ostream &, df::construction_type ctype, df::coord t, std::ostream & reason)
{
    df::tiletype tt = *Maps::getTileType(t);
    if (ENUM_ATTR(tiletype, material, tt) == tiletype_material::TREE)
    {
        df::plant *tree = nullptr;
        AI::dig_tile(find_tree_base(t, &tree));
        if (auto plant = tree ? df::plant_raw::find(tree->material) : nullptr)
        {
            reason << plant->name << " ";
        }
        reason << "tree in the way";
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
            AI::dig_tile(t);
            return true;
        }
    }

    // fall through = must build actual construction

    if (ENUM_ATTR(tiletype, material, tt) == tiletype_material::CONSTRUCTION)
    {
        // remove existing invalid construction
        AI::dig_tile(t);
        reason << "have " << enum_item_key(sb) << " construction but want " << enum_item_key(ctype) << " construction";
        return false;
    }

    for (auto it = world->buildings.all.begin(); it != world->buildings.all.end(); it++)
    {
        df::building *b = *it;
        if (b->z == t.z && !b->room.extents &&
            b->x1 <= t.x && b->x2 >= t.x &&
            b->y1 <= t.y && b->y2 >= t.y)
        {
            reason << "building in the way: " << enum_item_key(b->getType());
            return false;
        }
    }

    df::item *mat = nullptr;
    if (!find_item(items_other_id::BLOCKS, mat))
    {
        if (ai.find_room(room_type::workshop, [](room *r) -> bool { return r->workshop_type == workshop_type::Masons && r->status == room_status::finished && r->dfbuilding() != nullptr; }) != nullptr)
        {
            // we don't have blocks but we can make them.
            reason << "waiting for blocks to become available";
            return false;
        }
        if (!find_item(items_other_id::BOULDER, mat, false, true))
        {
            reason << "no building materials available";
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

bool Plan::try_construct_windmill(color_ostream &, room *r, std::ostream & reason)
{
    df::coord t = r->pos();
    auto sb = ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(t)));
    if (sb != tiletype_shape_basic::Open)
    {
        reason << "need channel (tile is currently " << enum_item_key(sb) << ")";
        return false;
    }

    std::vector<df::item *> mat;
    if (!find_items(items_other_id::WOOD, mat, 4))
    {
        reason << "have " << mat.size() << "/4 logs";
        return false;
    }

    df::building *bld = Buildings::allocInstance(t - df::coord(1, 1, 0), building_type::Windmill);
    Buildings::setSize(bld, df::coord(3, 3, 1));
    Buildings::constructWithItems(bld, mat);
    r->bld_id = bld->id;
    add_task(task_type::check_construct, r);
    return true;
}

bool Plan::try_furnish_roller(color_ostream &, room *r, furniture *f, df::coord t, std::ostream & reason)
{
    df::item *mecha = nullptr;
    df::item *chain = nullptr;
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
        add_task(task_type::check_furnish, r, f);
        return true;
    }
    if (mecha)
    {
        reason << "need rope or chain";
    }
    else if (chain)
    {
        reason << "need mechanisms";
    }
    else
    {
        reason << "need mechanisms and rope or chain";
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

bool Plan::try_construct_tradedepot(color_ostream &, room *r, std::ostream & reason)
{
    std::vector<df::item *> boulds;
    if (find_items(items_other_id::BOULDER, boulds, 3, false, true))
    {
        df::building *bld = Buildings::allocInstance(r->min, building_type::TradeDepot);
        Buildings::setSize(bld, r->size());
        Buildings::constructWithItems(bld, boulds);
        r->bld_id = bld->id;
        add_task(task_type::check_construct, r);
        return true;
    }
    reason << "have " << boulds.size() << "/3 boulders";
    return false;
}

bool Plan::try_construct_workshop(color_ostream & out, room *r, std::ostream & reason)
{
    if (!r->constructions_done(reason))
        return false;

    if (r->workshop_type == workshop_type::Dyers)
    {
        df::item *barrel = nullptr, *bucket = nullptr;
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
            add_task(task_type::check_construct, r);
            return true;
        }
        reason << "could not find ";
        if (!barrel)
        {
            reason << "barrel";
        }
        if (!bucket)
        {
            if (!barrel)
            {
                reason << " or ";
            }
            reason << "bucket";
        }
    }
    else if (r->workshop_type == workshop_type::Ashery)
    {
        df::item *block = nullptr, *barrel = nullptr, *bucket = nullptr;
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
            add_task(task_type::check_construct, r);
            return true;
        }
        reason << "could not find ";
        if (!block)
        {
            reason << "blocks";
        }
        if (!barrel)
        {
            if (!block)
            {
                if (!bucket)
                {
                    reason << ", ";
                }
                else
                {
                    reason << " or ";
                }
            }
            reason << "barrel";
        }
        if (!bucket)
        {
            if (!block || !barrel)
            {
                if (!block && !barrel)
                {
                    reason << ", ";
                }
                reason << " or ";
            }
            reason << "bucket";
        }
    }
    else if (r->workshop_type == workshop_type::MetalsmithsForge)
    {
        df::item *anvil = nullptr, *bould = nullptr;
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
            add_task(task_type::check_construct, r);
            return true;
        }
        reason << "could not find ";
        if (!anvil)
        {
            reason << "anvil";
        }
        if (!bould)
        {
            if (!anvil)
            {
                reason << " or ";
            }
            reason << "fire-safe boulder";
        }
    }
    else if (r->workshop_type == workshop_type::Quern)
    {
        df::item *quern = nullptr;
        if (find_item(items_other_id::QUERN, quern))
        {
            df::building *bld = Buildings::allocInstance(r->min, building_type::Workshop, workshop_type::Quern);
            Buildings::setSize(bld, r->size());
            std::vector<df::item *> item;
            item.push_back(quern);
            Buildings::constructWithItems(bld, item);
            r->bld_id = bld->id;
            init_managed_workshop(out, r, bld);
            add_task(task_type::check_construct, r);
            return true;
        }
        reason << "could not find quern";
    }
    else if (r->workshop_type == workshop_type::Custom)
    {
        auto cursor = std::find_if(world->raws.buildings.all.begin(), world->raws.buildings.all.end(), [r](df::building_def *def) -> bool { return def->code == r->raw_type; });
        df::building_def_workshopst *def = cursor == world->raws.buildings.all.end() ? nullptr : virtual_cast<df::building_def_workshopst>(*cursor);
        if (!def)
        {
            ai.debug(out, "Cannot find workshop type: " + r->raw_type);
            return true;
        }
        std::vector<df::job_item *> filters;
        for (auto it = def->build_items.begin(); it != def->build_items.end(); it++)
        {
            filters.push_back(make_job_item(*it));
        }
        std::vector<df::item *> items;
        if (!find_items(filters, items, reason))
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
        add_task(task_type::check_construct, r);
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
            add_task(task_type::check_construct, r);
            return true;
            // XXX else quarry?
        }
        reason << "could not find building material";
    }
    return false;
}

bool Plan::try_construct_furnace(color_ostream & out, room *r, std::ostream & reason)
{
    if (!r->constructions_done(reason))
        return false;

    if (r->furnace_type == furnace_type::Custom)
    {
        auto cursor = std::find_if(world->raws.buildings.all.begin(), world->raws.buildings.all.end(), [r](df::building_def *def) -> bool { return def->code == r->raw_type; });
        df::building_def_furnacest *def = cursor == world->raws.buildings.all.end() ? nullptr : virtual_cast<df::building_def_furnacest>(*cursor);
        if (!def)
        {
            ai.debug(out, "Cannot find furnace type: " + r->raw_type);
            return true;
        }
        std::vector<df::job_item *> filters;
        for (auto it = def->build_items.begin(); it != def->build_items.end(); it++)
        {
            filters.push_back(make_job_item(*it));
        }
        std::vector<df::item *> items;
        if (!find_items(filters, items, reason))
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
        add_task(task_type::check_construct, r);
        return true;
    }
    else
    {
        df::item *bould = nullptr;
        if (find_item(items_other_id::BOULDER, bould, true, true))
        {
            df::building *bld = Buildings::allocInstance(r->min, building_type::Furnace, r->furnace_type);
            Buildings::setSize(bld, r->size());
            std::vector<df::item *> item;
            item.push_back(bould);
            Buildings::constructWithItems(bld, item);
            r->bld_id = bld->id;
            init_managed_workshop(out, r, bld);
            add_task(task_type::check_construct, r);
            return true;
        }
        reason << "could not find fire-safe boulder";
        return false;
    }
}

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
        map[stockpile_type::fresh_raw_hide] = stockpile_list::Refuse;
    }
} stockpile_keys;

class ConstructStockpile : public ExclusiveCallback
{
    AI & ai;
    room *r;

public:
    ConstructStockpile(AI & ai, room *r) :
        ExclusiveCallback("construct stockpile for " + AI::describe_room(r), 2),
        ai(ai),
        r(r)
    {
    }

protected:
    void Run(color_ostream & out)
    {
        ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Default");

        int32_t start_x, start_y, start_z;
        Gui::getViewCoords(start_x, start_y, start_z);

        Key(interface_key::D_STOCKPILES);

        ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Stockpiles");

        Gui::revealInDwarfmodeMap(r->min + df::coord(1, 0, 0), true);
        Gui::setCursorCoords(r->min.x + 1, r->min.y, r->min.z);

        Key(interface_key::CURSOR_LEFT);
        Key(interface_key::STOCKPILE_CUSTOM);
        Key(interface_key::STOCKPILE_CUSTOM_SETTINGS);

        // don't constrain the focus string
        ExpectScreen<df::viewscreen_layer_stockpilest>("");

        ExpectedScreen<df::viewscreen_layer_stockpilest> view(this);

        auto wanted_group = stockpile_keys.map.at(r->stockpile_type);
        while (view->cur_group != stockpile_list::AdditionalOptions)
        {
            if (view->cur_group == wanted_group)
            {
                Key(interface_key::STOCKPILE_SETTINGS_ENABLE);
                Key(interface_key::STOCKPILE_SETTINGS_PERMIT_ALL);
            }
            else
            {
                Key(interface_key::STOCKPILE_SETTINGS_DISABLE);
            }
            Key(interface_key::STANDARDSCROLL_DOWN);
        }
        while (view->cur_group != wanted_group)
        {
            Key(interface_key::STANDARDSCROLL_UP);
        }
        Key(interface_key::STANDARDSCROLL_RIGHT);
        if (r->stockpile_type == stockpile_type::fresh_raw_hide)
        {
            Key(interface_key::STOCKPILE_SETTINGS_FORBID_ALL);
            Key(interface_key::STANDARDSCROLL_RIGHT);
            Key(interface_key::STANDARDSCROLL_DOWN);
            DFAI_ASSERT(view->item_status.at(1) == &view->settings->refuse.fresh_raw_hide, "fresh raw hide not in expected location");
            Key(interface_key::SELECT);
        }
        else
        {
            if (r->stock_disable.size() > view->list_ids.size() / 2)
            {
                std::vector<df::stockpile_list> stock_enable = view->list_ids;
                for (auto disable : r->stock_disable)
                {
                    auto it = std::find(stock_enable.begin(), stock_enable.end(), disable);
                    if (it != stock_enable.end())
                    {
                        stock_enable.erase(it);
                    }
                }

                Key(interface_key::STOCKPILE_SETTINGS_FORBID_ALL);
                KeyNoDelay(interface_key::STOCKPILE_SETTINGS_SPECIFIC1);
                KeyNoDelay(interface_key::STOCKPILE_SETTINGS_SPECIFIC2);

                for (auto enable : stock_enable)
                {
                    while (view->cur_list != enable)
                    {
                        Key(interface_key::STANDARDSCROLL_DOWN);
                    }
                    Key(interface_key::STOCKPILE_SETTINGS_PERMIT_SUB);
                }
            }
            else
            {
                for (auto disable : r->stock_disable)
                {
                    while (view->cur_list != disable)
                    {
                        Key(interface_key::STANDARDSCROLL_DOWN);
                    }
                    Key(interface_key::STOCKPILE_SETTINGS_FORBID_SUB);
                }
            }
            if (r->stock_specific1)
            {
                Key(interface_key::STOCKPILE_SETTINGS_SPECIFIC1);
            }
            if (r->stock_specific2)
            {
                Key(interface_key::STOCKPILE_SETTINGS_SPECIFIC2);
            }
        }
        Key(interface_key::LEAVESCREEN);

        ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Stockpiles");

        size_t buildings_before = world->buildings.all.size();
        Key(interface_key::SELECT);
        for (int16_t x = r->min.x; x < r->max.x; x++)
        {
            Key(interface_key::CURSOR_RIGHT);
        }
        for (int16_t y = r->min.y; y < r->max.y; y++)
        {
            Key(interface_key::CURSOR_DOWN);
        }
        Key(interface_key::SELECT);
        Key(interface_key::LEAVESCREEN);

        ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Default");

        df::building_stockpilest *bld = virtual_cast<df::building_stockpilest>(world->buildings.all.back());
        if (!bld || buildings_before == world->buildings.all.size())
        {
            ai.debug(out, "Failed to create stockpile: " + AI::describe_room(r));
            return;
        }

        r->bld_id = bld->id;
        ai.plan.furnish_room(out, r);

        if (r->workshop && r->stockpile_type == stockpile_type::stone)
        {
            ai.plan.room_items(out, r, [](df::item *i) { i->flags.bits.dump = 1; });
        }

        if (r->level == 0 &&
            r->stockpile_type != stockpile_type::stone && r->stockpile_type != stockpile_type::wood)
        {
            if (room *rr = ai.find_room(room_type::stockpile, [&](room *o) -> bool { return o->stockpile_type == r->stockpile_type && o->level == 1; }))
            {
                ai.plan.wantdig(out, rr);
            }
        }

        // TODO: do this through the UI
        // setup stockpile links with adjacent level
        ai.find_room(room_type::stockpile, [&](room *o) -> bool
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
                    for (auto btf : b_to->links.take_from_pile)
                    {
                        if (btf->id == b_from->id)
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

        ai.ignore_pause(start_x, start_y, start_z);
    }
};

bool Plan::try_construct_stockpile(color_ostream &, room *r, std::ostream & reason)
{
    if (!r->constructions_done(reason))
    {
        return false;
    }

    events.queue_exclusive(std::make_unique<ConstructStockpile>(ai, r));

    return true;
}

class ConstructActivityZone : public ExclusiveCallback
{
    AI & ai;
    room *r;

public:
    ConstructActivityZone(AI & ai, room *r) :
        ExclusiveCallback("construct activity zone for " + AI::describe_room(r), 2),
        ai(ai),
        r(r)
    {
        dfplex_blacklist = true;
    }

protected:
    void Run(color_ostream &)
    {
        ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Default");

        int32_t start_x, start_y, start_z;
        Gui::getViewCoords(start_x, start_y, start_z);

        Key(interface_key::D_CIVZONE);

        ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Zones");

        Gui::revealInDwarfmodeMap(r->min + df::coord(1, 0, 0), true);
        Gui::setCursorCoords(r->min.x + 1, r->min.y, r->min.z);

        Key(interface_key::CURSOR_LEFT);
        Key(interface_key::SELECT);

        for (int16_t x = r->min.x; x < r->max.x; x++)
        {
            Key(interface_key::CURSOR_RIGHT);
        }
        for (int16_t y = r->min.y; y < r->max.y; y++)
        {
            Key(interface_key::CURSOR_DOWN);
        }
        for (int16_t z = r->min.z; z < r->max.z; z++)
        {
            Key(interface_key::CURSOR_UP_Z);
        }

        Key(interface_key::SELECT);
        auto bld = virtual_cast<df::building_civzonest>(world->buildings.all.back());
        DFAI_ASSERT(bld, "newly created civzone is missing!");
        r->bld_id = bld->id;

        if (bld->zone_flags.bits.active != 1)
        {
            Key(interface_key::CIVZONE_ACTIVE);
        }

        if (r->type == room_type::infirmary)
        {
            Key(interface_key::CIVZONE_HOSPITAL);

            // for the DFHack animal hospital plugin:
            Key(interface_key::CIVZONE_ANIMAL_TRAINING);
        }
        else if (r->type == room_type::garbagedump)
        {
            Key(interface_key::CIVZONE_DUMP);
        }
        else if (r->type == room_type::pasture)
        {
            Key(interface_key::CIVZONE_PEN);
        }
        else if (r->type == room_type::pitcage)
        {
            Key(interface_key::CIVZONE_POND);
            if (bld->pit_flags.bits.is_pond != 0)
            {
                Key(interface_key::CIVZONE_POND_OPTIONS);
                ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/ZonesPitInfo");
                Key(interface_key::CIVZONE_POND_WATER);
                Key(interface_key::LEAVESCREEN);
                ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Zones");
            }
        }
        else if (r->type == room_type::pond)
        {
            Key(interface_key::CIVZONE_POND);
            if (bld->pit_flags.bits.is_pond != 1)
            {
                Key(interface_key::CIVZONE_POND_OPTIONS);
                ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/ZonesPitInfo");
                Key(interface_key::CIVZONE_POND_WATER);
                Key(interface_key::LEAVESCREEN);
                ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Zones");
            }
            if (r->temporary && r->workshop && r->workshop->type == room_type::farmplot)
            {
                ai.plan.add_task(task_type::monitor_farm_irrigation, r);
            }
        }
        else if (r->type == room_type::location)
        {
            Key(interface_key::CIVZONE_MEETING);
            Key(interface_key::ASSIGN_LOCATION);
            ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/ZonesLocationInfo");
            Key(interface_key::LOCATION_NEW);
            bool known = false;
            switch (r->location_type)
            {
            case location_type::guildhall:
            {
                Key(interface_key::LOCATION_GUILDHALL);
                auto gh_type = df::profession(r->data1);
                DFAI_ASSERT(gh_type != profession::NONE, "guild hall must have profession");
                do
                {
                    Key(interface_key::SECONDSCROLL_DOWN);
                }
                while (ui_sidebar_menus->location.cursor_profession &&
                        ui_sidebar_menus->location.profession.at(ui_sidebar_menus->location.cursor_profession) != gh_type);
                DFAI_ASSERT(ui_sidebar_menus->location.profession.at(ui_sidebar_menus->location.cursor_profession) == gh_type, "could not find profession for this guildhall");
                Key(interface_key::SELECT);
                known = true;
                break;
            }
            case location_type::tavern:
                Key(interface_key::LOCATION_INN_TAVERN);
                known = true;
                break;
            case location_type::library:
                Key(interface_key::LOCATION_LIBRARY);
                known = true;
                break;
            case location_type::temple:
            {
                Key(interface_key::LOCATION_TEMPLE);
                if (r->data1 != -1)
                {
                    do
                    {
                        Key(interface_key::SECONDSCROLL_DOWN);
                    }
                    while (ui_sidebar_menus->location.cursor_deity &&
                            (ui_sidebar_menus->location.deity_type.at(ui_sidebar_menus->location.cursor_deity) != df::temple_deity_type(r->data1) ||
                             ui_sidebar_menus->location.deity_data.at(ui_sidebar_menus->location.cursor_deity).Deity != r->data2));
                    DFAI_ASSERT(ui_sidebar_menus->location.cursor_deity, "could not find religion for this temple");
                }
                Key(interface_key::SELECT);
                known = true;
                break;
            }
            case location_type::_location_type_count:
                break;
            }
            if (!known)
            {
                Key(interface_key::LEAVESCREEN);
                Key(interface_key::LEAVESCREEN);
            }
            ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Zones");
        }

        Key(interface_key::LEAVESCREEN);

        ExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Default");

        ai.ignore_pause(start_x, start_y, start_z);
    }
};

bool Plan::try_construct_activityzone(color_ostream &, room *r, std::ostream & reason)
{
    if (!r->constructions_done(reason))
    {
        return false;
    }

    if (r->type == room_type::pond && r->workshop && !r->workshop->is_dug())
    {
        reason << "waiting for pond target to be dug";
        return false;
    }

    events.queue_exclusive(std::make_unique<ConstructActivityZone>(ai, r));

    return true;
}

bool Plan::monitor_farm_irrigation(color_ostream & out, room *r, std::ostream & reason)
{
    if (can_place_farm(out, r->workshop, false, reason))
    {
        auto zone = virtual_cast<df::building_civzonest>(r->dfbuilding());
        zone->pit_flags.bits.is_pond = 0;
        return true;
    }

    if (auto pond = virtual_cast<df::building_civzonest>(r->dfbuilding()))
    {
        for (auto j : pond->jobs)
        {
            j->flags.bits.do_now = 1;
        }
    }

    return false;
}

bool Plan::can_place_farm(color_ostream & out, room *r, bool cheat, std::ostream & reason)
{
    size_t need = (r->max.x - r->min.x + 1) * (r->max.y - r->min.y + 1) * (r->max.z - r->min.z + 1);
    size_t have = 0;
    for (int16_t x = r->min.x; x <= r->max.x; x++)
    {
        for (int16_t y = r->min.y; y <= r->max.y; y++)
        {
            for (int16_t z = r->min.z; z <= r->max.z; z++)
            {
                if (farm_allowed_materials.set.count(ENUM_ATTR(tiletype, material, *Maps::getTileType(x, y, z))))
                {
                    have++;
                    continue;
                }

                df::map_block *block = Maps::getTileBlock(x, y, z);
                auto e = std::find_if(block->block_events.begin(), block->block_events.end(), [](df::block_square_event *e) -> bool
                {
                    df::block_square_event_material_spatterst *spatter = virtual_cast<df::block_square_event_material_spatterst>(e);
                    return spatter &&
                        spatter->mat_type == builtin_mats::MUD &&
                        spatter->mat_index == -1;
                });
                if (cheat)
                {
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
                    if (spatter->amount[x & 0xf][y & 0xf] == 0)
                    {
                        ai.debug(out, stl_sprintf("cheat: mud invocation (%d, %d, %d)", x, y, z));
                        spatter->amount[x & 0xf][y & 0xf] = 50; // small pile of mud
                    }
                    have++;
                }
                else
                {
                    if (e == block->block_events.end())
                    {
                        continue;
                    }

                    df::block_square_event_material_spatterst *spatter = virtual_cast<df::block_square_event_material_spatterst>(*e);
                    if (spatter->amount[x & 0xf][y & 0xf] == 0)
                    {
                        continue;
                    }
                    have++;
                }
            }
        }
    }

    if (have == need)
    {
        return true;
    }

    reason << "waiting for irrigation (" << have << "/" << need << ")";
    return false;
}

bool Plan::try_construct_farmplot(color_ostream & out, room *r, std::ostream & reason)
{
    auto pond = ai.find_room(room_type::pond, [r](room *p) -> bool
    {
        return p->temporary && p->workshop == r;
    });
    if (!can_place_farm(out, r, !pond, reason))
    {
        return false;
    }

    df::building *bld = Buildings::allocInstance(r->min, building_type::FarmPlot);
    Buildings::setSize(bld, r->size());
    Buildings::constructWithItems(bld, std::vector<df::item *>());
    r->bld_id = bld->id;
    furnish_room(out, r);
    if (room *st = ai.find_room(room_type::stockpile, [r](room *o) -> bool { return o->workshop == r; }))
    {
        digroom(out, st);
    }
    add_task(task_type::setup_farmplot, r);
    return true;
}

bool Plan::try_setup_farmplot(color_ostream & out, room *r, std::ostream & reason)
{
    df::building *bld = r->dfbuilding();
    if (!bld)
        return true;
    if (bld->getBuildStage() < bld->getMaxBuildStage())
    {
        reason << "waiting for field to be tilled (" << bld->getBuildStage() << "/" << bld->getMaxBuildStage() << ")";
        return false;
    }

    ai.stocks.farmplot(out, r);

    return true;
}

bool Plan::try_endfurnish(color_ostream & out, room *r, furniture *f, std::ostream & reason)
{
    if (!AI::is_dwarfmode_viewscreen())
    {
        // some of these things need to use the UI.
        reason << "not on main viewscreen";
        return false;
    }

    df::building *bld = df::building::find(f->bld_id);
    if (!bld)
    {
        // destroyed building?
        return true;
    }
    if (bld->getBuildStage() < bld->getMaxBuildStage())
    {
        reason << "waiting for construction (" << bld->getBuildStage() << "/" << bld->getMaxBuildStage() << ")";
        return false;
    }

    if (f->type == layout_type::archery_target)
    {
        f->makeroom = true;
    }
    else if (f->type == layout_type::coffin)
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
    else if (f->type == layout_type::floodgate)
    {
        for (auto rr : rooms_and_corridors)
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
    else if (f->type == layout_type::hatch)
    {
        df::building_hatchst *hatch = virtual_cast<df::building_hatchst>(bld);
        if (r->type == room_type::pitcage)
        {
            hatch->door_flags.bits.forbidden = 1;
            hatch->door_flags.bits.pet_passable = 0;
        }
        else
        {
            hatch->door_flags.bits.pet_passable = 1;
        }
        hatch->door_flags.bits.internal = f->internal ? 1 : 0;
    }
    else if (f->type == layout_type::lever)
    {
        return setup_lever(out, r, f);
    }

    if (r->type == room_type::jail && (f->type == layout_type::cage || f->type == layout_type::restraint))
    {
        if (!f->makeroom)
        {
            delete[] bld->room.extents;
            bld->room.extents = new df::building_extents_type[1];
            bld->room.extents[0] = building_extents_type::DistanceBoundary;
            bld->room.x = r->min.x + f->pos.x;
            bld->room.y = r->min.y + f->pos.y;
            bld->room.width = 1;
            bld->room.height = 1;
            bld->is_room = true;
        }
        bld->flags.bits.justice = 1;
    }

    if (r->type == room_type::infirmary)
    {
        // Toggle hospital off and on because it's easier than figuring out
        // what Dwarf Fortress does. Shouldn't cancel any jobs, but might
        // create jobs if we just built a box.

        int32_t start_x, start_y, start_z;
        Gui::getViewCoords(start_x, start_y, start_z);

        Gui::getCurViewscreen(true)->feed_key(interface_key::D_CIVZONE);

        df::coord pos = r->pos();
        Gui::revealInDwarfmodeMap(pos, true);
        Gui::setCursorCoords(pos.x, pos.y, pos.z);

        Gui::getCurViewscreen(true)->feed_key(interface_key::CURSOR_LEFT);
        Gui::getCurViewscreen(true)->feed_key(interface_key::CIVZONE_HOSPITAL);
        Gui::getCurViewscreen(true)->feed_key(interface_key::CIVZONE_HOSPITAL);
        Gui::getCurViewscreen(true)->feed_key(interface_key::LEAVESCREEN);

        ai.ignore_pause(start_x, start_y, start_z);
    }

    if (!f->makeroom)
    {
        return true;
    }
    if (!r->is_dug(reason))
    {
        reason << " (waiting to make room)";
        return false;
    }

    ai.debug(out, "makeroom " + AI::describe_room(r));

    df::coord size = r->size() + df::coord(2, 2, 0);

    delete[] bld->room.extents;
    bld->room.extents = new df::building_extents_type[size.x * size.y]();
    bld->room.x = r->min.x - 1;
    bld->room.y = r->min.y - 1;
    bld->room.width = size.x;
    bld->room.height = size.y;
    auto set_ext = [&bld](int16_t x, int16_t y, df::building_extents_type v)
    {
        bld->room.extents[bld->room.width * (y - bld->room.y) + (x - bld->room.x)] = v;
    };
    for (int16_t rx = r->min.x - 1; rx <= r->max.x + 1; rx++)
    {
        for (int16_t ry = r->min.y - 1; ry <= r->max.y + 1; ry++)
        {
            if (ENUM_ATTR(tiletype, shape, *Maps::getTileType(rx, ry, r->min.z)) == tiletype_shape::WALL)
            {
                set_ext(rx, ry, building_extents_type::Wall);
            }
            else
            {
                set_ext(rx, ry, r->include(df::coord(rx, ry, r->min.z)) ? building_extents_type::Interior : building_extents_type::DistanceBoundary);
            }
        }
    }
    for (auto f_ : r->layout)
    {
        if (f_->type != layout_type::door)
            continue;
        df::coord t = r->min + f_->pos;
        set_ext(t.x, t.y, building_extents_type::None);
        // tile in front of the door tile is 4 (TODO door in corner...)
        if (t.x < r->min.x)
            set_ext(t.x + 1, t.y, building_extents_type::DistanceBoundary);
        if (t.x > r->max.x)
            set_ext(t.x - 1, t.y, building_extents_type::DistanceBoundary);
        if (t.y < r->min.y)
            set_ext(t.x, t.y + 1, building_extents_type::DistanceBoundary);
        if (t.y > r->max.y)
            set_ext(t.x, t.y - 1, building_extents_type::DistanceBoundary);
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

bool Plan::try_endconstruct(color_ostream & out, room *r, std::ostream & reason)
{
    df::building *bld = r->dfbuilding();
    if (bld && bld->getBuildStage() < bld->getMaxBuildStage())
    {
        reason << "waiting for construction (" << bld->getBuildStage() << "/" << bld->getMaxBuildStage() << ")";
        return false;
    }
    furnish_room(out, r);
    return true;
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
