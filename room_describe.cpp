#include "ai.h"
#include "plan.h"

#include "df/abstract_building_templest.h"
#include "df/building_civzonest.h"
#include "df/historical_entity.h"
#include "df/historical_figure.h"
#include "df/squad.h"
#include "df/unit.h"
#include "df/world_site.h"

std::string AI::describe_room(room *r, bool html)
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
            s << " (\"" << maybe_escape(r->raw_type, html) << "\")";
        }
        else
        {
            s << " (" << enum_item_key(r->furnace_type) << ")";
        }
        break;
    case room_type::location:
        switch (r->location_type)
        {
        case location_type::guildhall:
            s << " (" << toLower(enum_item_key(df::profession(r->data1))) << " guildhall)";
            break;
        case location_type::temple:
            switch (df::temple_deity_type(r->data1))
            {
            case temple_deity_type::None:
                s << " (temple)";
                break;
            case temple_deity_type::Deity:
                s << " (temple to ";
                if (html)
                {
                    s << "<a href=\"fig-" << r->data2 << "\">";
                }
                if (auto hf = df::historical_figure::find(r->data2))
                {
                    s << maybe_escape(AI::describe_name(hf->name, false), html) << " \"" << maybe_escape(AI::describe_name(hf->name, true), html) << "\"";
                }
                else
                {
                    s << "[unknown deity]";
                }
                if (html)
                {
                    s << "</a>";
                }
                s << ")";
                break;
            case temple_deity_type::Religion:
                s << " (temple of ";
                if (html)
                {
                    s << "<a href=\"ent-" << r->data2 << "\">";
                }
                if (auto ent = df::historical_entity::find(r->data2))
                {
                    s << maybe_escape(AI::describe_name(ent->name, false), html) << " \"" << maybe_escape(AI::describe_name(ent->name, true), html) << "\"";
                }
                else
                {
                    s << "[unknown religion]";
                }
                if (html)
                {
                    s << "</a>";
                }
                s << ")";
                break;
            }
            break;
        default:
            s << " (" << r->location_type << ")";
            break;
        }
        if (auto civzone = virtual_cast<df::building_civzonest>(r->dfbuilding()))
        {
            if (auto site = df::world_site::find(civzone->site_id))
            {
                if (auto loc = binsearch_in_vector(site->buildings, civzone->location_id))
                {
                    if (auto name = loc->getName())
                    {
                        s << " (";
                        if (html)
                        {
                            s << "<a href=\"site-" << civzone->site_id << "/bld-" << loc->id << "\">";
                        }
                        s << maybe_escape(AI::describe_name(*name, false), html) << " \"" << maybe_escape(AI::describe_name(*name, true), html) << "\"";
                        if (html)
                        {
                            s << "</a>";
                        }
                        s << ")";
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
            s << " (\"" << maybe_escape(r->raw_type, html) << "\")";
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
        s << " (" << maybe_escape(r->comment, html) << ")";
    }

    if (df::unit *u = df::unit::find(r->owner))
    {
        s << " (owned by " << AI::describe_unit(u, html) << ")";
    }

    if (r->noblesuite != -1)
    {
        s << " (noble suite " << r->noblesuite << ")";
    }

    if (df::squad *squad = df::squad::find(r->squad_id))
    {
        s << " (used by " << maybe_escape(AI::describe_name(squad->name, true), html) << ")";
    }

    if (r->level != -1)
    {
        s << " (level " << r->level << ")";
    }

    if (r->workshop)
    {
        s << " (" << describe_room(r->workshop, html) << ")";
    }

    if (r->has_users)
    {
        size_t users_count = r->users.size();
        if (r->users.count(-1))
        {
            users_count--;
        }
        s << " (" << users_count << " users)";
    }

    if (r->status != room_status::finished)
    {
        s << " (" << r->status << ")";
    }

    return s.str();
}

std::string AI::describe_furniture(furniture *f, bool html)
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

    if (!f->comment.empty())
    {
        if (html)
        {
            s << " (" << html_escape(f->comment) << ")";
        }
        else
        {
            s << " (" << f->comment << ")";
        }
    }

    if (f->has_users)
    {
        size_t users_count = f->users.size();
        if (f->users.count(-1))
        {
            users_count--;
        }
        s << " (" << users_count << " users)";
    }

    if (f->ignore)
    {
        s << " (ignored)";
    }

    if (f->makeroom)
    {
        s << " (main)";
    }

    if (f->internal)
    {
        s << " (internal)";
    }

    s << " (" << f->pos.x << ", " << f->pos.y << ", " << f->pos.z << ")";

    if (f->target != nullptr)
    {
        s << " (" << describe_furniture(f->target, html) << ")";
    }

    return s.str();
}

bool Plan::find_building(df::building *bld, room * & r, furniture * & f)
{
    if (!bld)
    {
        return false;
    }

    if (room_by_z.empty())
    {
        for (auto r_ : rooms_and_corridors)
        {
            if (bld->getType() == building_type::Construction)
            {
                for (auto f_ : r_->layout)
                {
                    if (r_->min.z + f_->pos.z == bld->z && f_->construction == bld->getSubtype() && r_->min.x + f_->pos.x == bld->x1 && r_->min.y + f_->pos.y == bld->y1)
                    {
                        r = r_;
                        f = f_;
                        return true;
                    }
                }
                continue;
            }

            if (r_->bld_id == bld->id)
            {
                r = r_;
                return true;
            }

            for (auto f_ : r_->layout)
            {
                if (f_->bld_id == bld->id)
                {
                    r = r_;
                    f = f_;
                    return true;
                }
            }
        }
        return false;
    }

    auto by_z = room_by_z.find(bld->z);
    if (by_z == room_by_z.end())
    {
        return false;
    }
    for (auto r_ : by_z->second)
    {
        if (bld->getType() == building_type::Construction)
        {
            for (auto f_ : r_->layout)
            {
                if (f_->construction == bld->getSubtype() && r_->min.x + f_->pos.x == bld->x1 && r_->min.y + f_->pos.y == bld->y1)
                {
                    r = r_;
                    f = f_;
                    return true;
                }
            }
            continue;
        }

        if (r_->bld_id == bld->id)
        {
            r = r_;
            return true;
        }

        for (auto f_ : r_->layout)
        {
            if (f_->bld_id == bld->id)
            {
                r = r_;
                f = f_;
                return true;
            }
        }
    }
    return false;
}
