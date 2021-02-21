#include "ai.h"
#include "debug.h"
#include "plan.h"

#include "modules/Buildings.h"
#include "modules/Maps.h"

#include "df/building_cagest.h"
#include "df/building_trapst.h"
#include "df/item_cagest.h"
#include "df/tile_occupancy.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);

const static size_t wantdig_max = 2; // dig at most this much wantdig rooms at a time

static bool want_reupdate = false;

void Plan::update(color_ostream &)
{
    last_update_year = *cur_year;
    last_update_tick = *cur_year_tick;
    if (bg_idx_generic == tasks_generic.end())
    {
        bg_idx_generic = tasks_generic.begin();

        nrdig.clear();
        for (auto it = tasks_generic.begin(); it != tasks_generic.end(); it++)
        {
            task *t = *it;
            if ((t->type != task_type::dig_room && t->type != task_type::dig_room_immediate) || (t->r->type == room_type::corridor && (t->r->corridor_type == corridor_type::veinshaft || t->r->corridor_type == corridor_type::outpost)))
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
            std::ostringstream reason;
            task & t = **bg_idx_generic;

            auto any_immediate = [this]() -> bool
            {
                for (auto t : tasks_generic)
                {
                    if (t->type == task_type::dig_room_immediate)
                    {
                        return true;
                    }
                }
                return false;
            };

            bool del = false;
            switch (t.type)
            {
            case task_type::want_dig:
                if (any_immediate())
                {
                    reason << "waiting for more important room to be dug";
                }
                else if (t.r->is_dug() || nrdig[t.r->queue] < wantdig_max)
                {
                    digroom(out, t.r);
                    del = true;
                }
                else
                {
                    reason << "dig queue " << t.r->queue << " has " << nrdig[t.r->queue] << " of " << wantdig_max << " slots already filled";
                }
                break;
            case task_type::dig_room:
            case task_type::dig_room_immediate:
                fixup_open(out, t.r);
                if (t.r->is_dug(reason))
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
                del = try_construct_tradedepot(out, t.r, reason);
                break;
            case task_type::construct_workshop:
                del = try_construct_workshop(out, t.r, reason);
                break;
            case task_type::construct_farmplot:
                del = try_construct_farmplot(out, t.r, reason);
                break;
            case task_type::construct_furnace:
                del = try_construct_furnace(out, t.r, reason);
                break;
            case task_type::construct_stockpile:
                del = try_construct_stockpile(out, t.r, reason);
                break;
            case task_type::construct_activityzone:
                del = try_construct_activityzone(out, t.r, reason);
                break;
            case task_type::construct_windmill:
                del = try_construct_windmill(out, t.r, reason);
                break;
            case task_type::monitor_farm_irrigation:
                del = monitor_farm_irrigation(out, t.r, reason);
                break;
            case task_type::setup_farmplot:
                del = try_setup_farmplot(out, t.r, reason);
                break;
            case task_type::furnish:
                break;
            case task_type::check_furnish:
                break;
            case task_type::check_construct:
                del = try_endconstruct(out, t.r, reason);
                break;
            case task_type::dig_cistern:
                del = try_digcistern(out, t.r);
                break;
            case task_type::dig_garbage:
                del = true;
                break;
            case task_type::check_idle:
                del = checkidle(out, reason);
                break;
            case task_type::check_rooms:
                checkrooms(out);
                break;
            case task_type::monitor_cistern:
                monitor_cistern(out, reason);
                break;
            case task_type::monitor_room_value:
                del = monitor_room_value(out, t.r, reason);
                break;
            case task_type::rescue_caged:
                del = rescue_caged(out, t.r, t.f, t.item_id, reason);
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
                t.last_status = reason.str();
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
            std::ostringstream reason;
            task & t = **bg_idx_furniture;

            bool del = false;
            switch (t.type)
            {
            case task_type::furnish:
                del = try_furnish(out, t.r, t.f, reason);
                break;
            case task_type::check_furnish:
                del = try_endfurnish(out, t.r, t.f, reason);
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
                t.last_status = reason.str();
                bg_idx_furniture++;
            }
            return false;
        });
    }
}

task *Plan::is_digging()
{
    for (auto t : tasks_generic)
    {
        if ((t->type == task_type::want_dig || t->type == task_type::dig_room || t->type == task_type::dig_room_immediate) && t->r->type != room_type::corridor)
        {
            return t;
        }
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

bool Plan::checkidle(color_ostream & out, std::ostream & reason)
{
    for (auto t : tasks_generic)
    {
        if ((t->type == task_type::want_dig || t->type == task_type::dig_room_immediate) && t->r->type != room_type::corridor && t->r->queue == 0)
        {
            reason << "already have queued room: " << AI::describe_room(t->r);
            return false;
        }
    }

    if (last_idle_year != *cur_year)
    {
        if (last_idle_year != -1)
        {
            idleidle(out);
            reason << "smoothing fortress; ";
        }
        last_idle_year = *cur_year;
    }

    if (!priorities.empty())
    {
        for (auto & priority : priorities)
        {
            if (priority.working)
            {
                bool any_active_task = false;
                for (auto t : tasks_generic)
                {
                    if (priority.match_task(t))
                    {
                        any_active_task = true;
                        break;
                    }
                }
                if (!any_active_task)
                {
                    for (auto t : tasks_furniture)
                    {
                        if (priority.match_task(t))
                        {
                            any_active_task = true;
                            break;
                        }
                    }
                }
                if (!any_active_task)
                {
                    priority.working = false;
                }
            }

            if (priority.act(ai, out, reason))
            {
                break;
            }
        }
        return false;
    }

    reason << "no priority list!";
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
    for (auto r : rooms_and_corridors)
    {
        if (r->status != room_status::plan && r->status != room_status::dig && !r->temporary &&
            (r->type == room_type::nobleroom ||
                r->type == room_type::bedroom ||
                r->type == room_type::dininghall ||
                r->type == room_type::cemetery ||
                r->type == room_type::infirmary ||
                r->type == room_type::barracks ||
                r->type == room_type::location ||
                r->type == room_type::stockpile))
            idleidle_tab.push_back(r);
    }
    for (auto r : room_category[room_type::corridor])
    {
        if (r->status != room_status::plan && r->status != room_status::dig && r->corridor_type == corridor_type::corridor && !r->outdoor)
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
        ai.debug(out, "engrave fort");
    }
    else
    {
        ai.debug(out, "smooth fort");
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
    size_t ncheck = 8;
    for (size_t i = ncheck * 4; i > 0; i--)
    {
        if (checkroom_idx < rooms_and_corridors.size())
        {
            room* r = rooms_and_corridors.at(checkroom_idx);
            if (r->status == room_status::plan && r->build_when_accessible)
            {
                bool all_ready = true;
                for (auto ap : r->accesspath)
                {
                    if (ap->status < room_status::dug)
                    {
                        all_ready = false;
                    }
                }

                if (all_ready)
                {
                    wantdig(out, r, 2);
                }
            }

            if (r->status != room_status::plan)
            {
                checkroom(out, r);
                ncheck--;
            }
        }
        checkroom_idx++;
        if (ncheck <= 0)
            break;
    }
    if (checkroom_idx >= rooms_and_corridors.size())
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

    if (r->required_value > 0 && r->compute_value() < r->required_value)
    {
        add_task(task_type::monitor_room_value, r);
    }

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
                str << "fix furniture " << f->type << " in " << AI::describe_room(r);
                ai.debug(out, str.str(), t);
                f->bld_id = -1;

                add_task(task_type::furnish, r, f);
            }
            if (f->construction != construction_type::NONE)
            {
                std::ostringstream discard;
                try_furnish_construction(out, f->construction, t, discard);
            }
        }
        // tantrumed building
        if (r->bld_id != -1 && !r->dfbuilding())
        {
            ai.debug(out, "rebuild " + AI::describe_room(r), r->pos());
            r->bld_id = -1;
            construct_room(out, r);
        }
    }
}

// queue a room for digging when other dig jobs are finished
bool Plan::wantdig(color_ostream & out, room *r, int32_t queue)
{
    if (r->queue_dig || r->status != room_status::plan)
    {
        return false;
    }

    if (queue != 0)
    {
        r->queue = queue;
    }
    ai.debug(out, "wantdig " + AI::describe_room(r));
    r->queue_dig = true;
    r->dig(true);
    add_task(task_type::want_dig, r);
    return true;
}

bool Plan::digroom(color_ostream & out, room *r, bool immediate)
{
    if (r->status != room_status::plan)
        return false;
    ai.debug(out, "digroom " + AI::describe_room(r));
    r->queue_dig = false;
    r->status = room_status::dig;
    fixup_open(out, r);
    r->dig();

    add_task(immediate ? task_type::dig_room_immediate : task_type::dig_room, r);

    for (auto it = r->accesspath.begin(); it != r->accesspath.end(); it++)
    {
        digroom(out, *it, immediate);
    }

    for (auto it = r->layout.begin(); it != r->layout.end(); it++)
    {
        furniture *f = *it;
        if (f->type == layout_type::none || f->type == layout_type::floodgate)
            continue;
        if (f->dig != tile_dig_designation::Default)
            continue;
        add_task(task_type::furnish, r, f);
    }

    ai.find_room(room_type::stockpile, [this, &out, r, immediate](room *r_) -> bool
    {
        if (r_->workshop != r)
        {
            return false;
        }

        // dig associated stockpiles
        digroom(out, r_, immediate);

        // search all stockpiles in case there's more than one
        return false;
    });

    df::coord size = r->size();
    if (r->type != room_type::corridor || size.z > 1)
    {
        nrdig[r->queue]++;
        if (size.x * size.y * size.z >= 10)
            nrdig[r->queue]++;
    }

    return true;
}

bool Plan::monitor_room_value(color_ostream &, room *r, std::ostream & reason)
{
    if (r->required_value <= 0)
    {
        return true;
    }

    int32_t value = r->compute_value();
    if (value < 0)
    {
        reason << "cannot compute room value";

        for (auto f : r->layout)
        {
            if (f->makeroom)
            {
                reason << " (waiting for " << ai.describe_furniture(f) << " to be built)";
                break;
            }
        }

        return false;
    }

    if (value >= r->required_value)
    {
        return true;
    }

    // TODO: smooth/engrave, build statues, etc.
    reason << "room value is " << (r->required_value - value) << " too low (TODO)";
    return false;
}

bool Plan::rescue_caged(color_ostream & out, room *r, furniture *f, int32_t item_id, std::ostream & reason)
{
    auto cage = virtual_cast<df::item_cagest>(df::item::find(item_id));
    if (!cage)
    {
        // cage no longer exists.
        return true;
    }

    df::unit *prisoner = nullptr;
    for (auto ref : cage->general_refs)
    {
        if (ref->getType() == general_ref_type::CONTAINS_UNIT)
        {
            prisoner = ref->getUnit();
            break;
        }
    }
    df::building_cagest* cage_bld = nullptr;
    for (auto ref : cage->general_refs)
    {
        if (ref->getType() == general_ref_type::BUILDING_HOLDER)
        {
            cage_bld = virtual_cast<df::building_cagest>(ref->getBuilding());
            break;
        }
    }

    if (prisoner == nullptr)
    {
        // there's no longer someone in the cage.
        for (auto ref : cage->general_refs)
        {
            // if we secured the cage to the floor, free it up.
            if (cage_bld)
            {
                Buildings::deconstruct(cage_bld);
            }
        }
        return true;
    }

    auto lever = virtual_cast<df::building_trapst>(df::building::find(f->bld_id));
    if (!lever || lever->getBuildStage() != lever->getMaxBuildStage())
    {
        reason << "cannot free " << AI::describe_unit(prisoner) << ": missing lever";
        return false;
    }
    DFAI_ASSERT(lever->trap_type == trap_type::Lever, "lever is trap_type::" + enum_item_key(lever->trap_type));

    reason << "freeing " << AI::describe_unit(prisoner) << ": ";

    // step 1: someone's trapped in a cage. check for a free tile in the "release zone"
    // and create a job to secure the cage to that spot.
    if (!cage_bld)
    {
        for (auto ref : cage->specific_refs)
        {
            if (ref->type == specific_ref_type::JOB)
            {
                reason << "waiting for cage to be secured to floor";
                return false;
            }
        }

        df::coord free_tile;
        for (int16_t x = r->min.x; x <= r->max.x; x++)
        {
            for (int16_t y = r->min.y; y <= r->max.y; y++)
            {
                df::coord c(x, y, r->min.z);

                auto tt = *Maps::getTileType(c);
                if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt)) != tiletype_shape_basic::Floor)
                {
                    continue;
                }

                if (Maps::getTileOccupancy(c)->bits.building != tile_building_occ::None)
                {
                    continue;
                }

                free_tile = c;
                break;
            }
        }

        if (free_tile.isValid())
        {
            auto bld = Buildings::allocInstance(free_tile, building_type::Cage);
            Buildings::setSize(bld, df::coord(1, 1, 1));

            std::vector<df::item*> cage_item;
            cage_item.push_back(cage);
            Buildings::constructWithItems(bld, cage_item);

            reason << "waiting for cage to be secured to floor";
            return false;
        }

        reason << "waiting for space in release zone";

        return false;
    }

    // step 2: the cage has been secured in the "release zone". make sure we have two
    // mechanisms and tell the dwarves to link the lever to the cage.
    furniture cage_f;
    cage_f.bld_id = cage_bld->id;
    if (!link_lever(out, f, &cage_f, reason))
    {
        return false;
    }

    // step 3: the cage is linked to the lever. give it a pull. if all goes well,
    // we'll end up freeing the task on the next iteration as the cage will be empty
    // and left on the floor as an item.
    pull_lever(out, f, reason);

    // we could deconstruct the lever periodically to reclaim mechanisms, but the
    // potential humor of having a lever with hundreds of mechanisms outweighs the
    // cost of a few extra boulders being needed in most forts.
    return false;
}

void Plan::add_task(task_type::type type, room *r, furniture *f, int32_t item_id)
{
    auto & tasks = (type == task_type::furnish || type == task_type::check_furnish) ? tasks_furniture : tasks_generic;
    for (auto t : tasks)
    {
        if (t->type == type && t->r == r && t->f == f && t->item_id == item_id)
        {
            return;
        }
    }
    tasks.push_back(new task(type, r, f, item_id));
}
