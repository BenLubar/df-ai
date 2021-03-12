#include "ai.h"
#include "plan.h"

#include "modules/Buildings.h"
#include "modules/Units.h"

#include "df/building_squad_use.h"
#include "df/caste_raw.h"
#include "df/creature_raw.h"
#include "df/squad.h"

void Plan::new_citizen(color_ostream & out, int32_t uid)
{
    add_task(task_type::check_idle);
    getdiningroom(out, uid);
    getbedroom(out, uid);

    size_t required_jail_cells = ai.pop.citizen.size() / 10;
    ai.find_room(room_type::jail, [this, &out, &required_jail_cells](room *r) -> bool
    {
        if (required_jail_cells == 0)
        {
            return true;
        }

        if (r->status == room_status::plan)
        {
            return false;
        }

        for (auto f : r->layout)
        {
            if (f->type == layout_type::cage || f->type == layout_type::restraint)
            {
                if (!f->ignore)
                {
                    required_jail_cells--;
                    if (required_jail_cells == 0)
                    {
                        return true;
                    }
                }
                else
                {
                    f->ignore = false;
                    required_jail_cells--;
                    if (r->status == room_status::finished)
                    {
                        furnish_room(out, r);
                    }
                    if (required_jail_cells == 0)
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    });
}

void Plan::del_citizen(color_ostream & out, int32_t uid)
{
    freecommonrooms(out, uid);
    freebedroom(out, uid);
}

void Plan::getbedroom(color_ostream & out, int32_t id)
{
    room *r = ai.find_room(room_type::bedroom, [id](room *r) -> bool { return r->owner == id; });
    if (!r)
        r = ai.find_room(room_type::bedroom, [](room *r) -> bool { return r->status != room_status::plan && r->owner == -1; });
    if (!r)
        r = ai.find_room(room_type::bedroom, [](room *r) -> bool { return r->status == room_status::plan && !r->queue_dig; });
    if (r)
    {
        wantdig(out, r, -1);
        set_owner(out, r, id);
        ai.debug(out, "assign " + AI::describe_room(r));
        if (r->status == room_status::finished)
            furnish_room(out, r);
    }
    else
    {
        ai.debug(out, stl_sprintf("[ERROR] AI can't getbedroom(%d)", id));
    }
}

void Plan::getdiningroom(color_ostream & out, int32_t id)
{
    // skip allocating space if there's already a dining room for this dwarf.
    auto is_user = [id](furniture *f) -> bool
    {
        return f->users.count(id);
    };
    if (ai.find_room(room_type::dininghall, [is_user](room *r) -> bool
    {
        return std::find_if(r->layout.begin(), r->layout.end(), is_user) != r->layout.end();
    }))
        return;

    if (room *r = ai.find_room(room_type::farmplot, [](room *r_) -> bool
    {
        return r_->farm_type == farm_type::food &&
            !r_->outdoor &&
            r_->users.size() < r_->has_users;
    }))
    {
        wantdig(out, r, -3);
        r->users.insert(id);
    }

    if (room *r = ai.find_room(room_type::farmplot, [](room *r_) -> bool
    {
        return r_->farm_type == farm_type::cloth &&
            !r_->outdoor &&
            r_->users.size() < r_->has_users;
    }))
    {
        wantdig(out, r, -3);
        r->users.insert(id);
    }

    if (room *r = ai.find_room(room_type::farmplot, [](room *r_) -> bool
    {
        return r_->farm_type == farm_type::food &&
            r_->outdoor &&
            r_->users.size() < r_->has_users;
    }))
    {
        wantdig(out, r, -3);
        r->users.insert(id);
    }

    if (room *r = ai.find_room(room_type::farmplot, [](room *r_) -> bool
    {
        return r_->farm_type == farm_type::cloth &&
            r_->outdoor &&
            r_->users.size() < r_->has_users;
    }))
    {
        wantdig(out, r, -3);
        r->users.insert(id);
    }

    if (room *r = ai.find_room(room_type::dininghall, [](room *r_) -> bool
    {
        for (auto it = r_->layout.begin(); it != r_->layout.end(); it++)
        {
            furniture *f = *it;
            if (f->has_users && f->users.size() < f->has_users)
                return true;
        }
        return false;
    }))
    {
        wantdig(out, r, -2);
        for (auto it = r->layout.begin(); it != r->layout.end(); it++)
        {
            furniture *f = *it;
            if (f->type == layout_type::table && f->users.size() < f->has_users)
            {
                f->ignore = false;
                f->users.insert(id);
                break;
            }
        }
        for (auto it = r->layout.begin(); it != r->layout.end(); it++)
        {
            furniture *f = *it;
            if (f->type == layout_type::chair && f->users.size() < f->has_users)
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
    while (room *old = ai.find_room(room_type::nobleroom, [id_list](room *r) -> bool { return r->owner != -1 && !id_list.count(r->owner); }))
    {
        old->required_value = 0;
        set_owner(out, old, -1);
    }

    for (auto it = id_list.begin(); it != id_list.end(); it++)
    {
        std::vector<Units::NoblePosition> entpos;
        Units::getNoblePositions(&entpos, df::unit::find(*it));
        room *base = ai.find_room(room_type::nobleroom, [it](room *r) -> bool { return r->owner == *it; });
        if (!base)
            base = ai.find_room(room_type::nobleroom, [](room *r) -> bool { return r->owner == -1; });
        std::set<nobleroom_type::type> seen;
        while (room *r = ai.find_room(room_type::nobleroom, [base, seen](room *r_) -> bool { return r_->noblesuite == base->noblesuite && !seen.count(r_->nobleroom_type); }))
        {
            seen.insert(r->nobleroom_type);
            set_owner(out, r, *it);
#define DIG_ROOM_IF(type, req) \
            if (r->nobleroom_type == nobleroom_type::type && std::find_if(entpos.begin(), entpos.end(), [](Units::NoblePosition np) -> bool { return np.position->required_ ## req > 0; }) != entpos.end()) \
            { \
                for (auto & np : entpos) \
                { \
                    r->required_value = r->required_value > np.position->required_ ## req ? r->required_value : np.position->required_ ## req; \
                } \
                wantdig(out, r, -2); \
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

    room *r = ai.find_room(room_type::barracks, [squad_id](room *r) -> bool { return r->squad_id == squad_id; });
    if (!r)
    {
        r = ai.find_room(room_type::barracks, [](room *r) -> bool { return r->squad_id == -1; });
        if (!r)
        {
            ai.debug(out, "[ERROR] no free barracks");
            return;
        }
        r->squad_id = squad_id;
        ai.debug(out, stl_sprintf("squad %d assign %s", squad_id, AI::describe_room(r).c_str()));
        wantdig(out, r, -2);
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
            if (f->type == type && f->users.size() < f->has_users)
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
    if (room *r = ai.find_room(room_type::cemetery, [](room *r_) -> bool { return std::find_if(r_->layout.begin(), r_->layout.end(), [](furniture *f) -> bool { return f->has_users && f->users.empty(); }) != r_->layout.end(); }))
    {
        wantdig(out, r, -1);
        for (auto it = r->layout.begin(); it != r->layout.end(); it++)
        {
            furniture *f = *it;
            if (f->type == layout_type::coffin && f->users.empty())
            {
                f->users.insert(-1);
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
    if (room *r = ai.find_room(room_type::bedroom, [id](room *r_) -> bool { return r_->owner == id; }))
    {
        ai.debug(out, "free " + AI::describe_room(r));
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
        ai.find_room(subtype, [id](room *r) -> bool
        {
            r->users.erase(id);
            return false;
        });
    }
    else
    {
        ai.find_room(subtype, [this, &out, id](room *r) -> bool
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
                            ai.debug(out, stl_sprintf("squad %d free %s", r->squad_id, AI::describe_room(r).c_str()));
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
    if (auto ref = Units::getGeneralRef(pet, general_ref_type::BUILDING_CIVZONE_ASSIGNED))
    {
        auto bld = ref->getBuilding();
        if (auto r = ai.find_room(room_type::pasture, [bld](room *r_) -> bool { return r_->dfbuilding() == bld; }))
        {
            if (r->low_grass())
            {
                r->users.erase(pet->id);
                bld = nullptr;
            }
        }

        if (bld)
        {
            return bld;
        }
    }

    size_t limit = 1000 - (11 * 11 * 1000 / df::creature_raw::find(pet->race)->caste[pet->caste]->misc.grazer); // 1000 = arbitrary, based on dfwiki?pasture
    if (room *r = ai.find_room(room_type::pasture, [limit](room *r_) -> bool
    {
        if (r_->low_grass())
        {
            return false;
        }
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
    if (room *r = ai.find_room(room_type::pasture, [pet_id](room *r_) -> bool { return r_->users.count(pet_id); }))
    {
        r->users.erase(pet_id);
    }
}

bool Plan::pastures_ready(color_ostream & out)
{
    return !ai.find_room(room_type::pasture, [](room *r) -> bool
    {
        return r->status != room_status::plan && !r->dfbuilding();
    }) && !ai.find_room(room_type::pasture, [&](room *r) -> bool
    {
        if (r->status == room_status::plan)
        {
            r->queue = -4;
            digroom(out, r);
            return true;
        }

        return false;
    });
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
    rooms_and_corridors.erase(std::remove(rooms_and_corridors.begin(), rooms_and_corridors.end(), t), rooms_and_corridors.end());
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
