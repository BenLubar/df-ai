#include "blueprint.h"

static bool check_indexes(const std::vector<room_base::furniture_t *> & layout, const std::vector<room_base::room_t *> & rooms, std::string & error)
{
    room_base::layoutindex_t layout_limit(layout.size());
    room_base::roomindex_t room_limit(rooms.size());

    for (auto f : layout)
    {
        if (f && !f->check_indexes(layout_limit, room_limit, error))
        {
            return false;
        }
    }

    for (auto r : rooms)
    {
        if (r && !r->check_indexes(layout_limit, room_limit, error))
        {
            return false;
        }
    }

    return true;
}

room_base::~room_base()
{
    for (auto & f : layout)
    {
        delete f;
    }
    for (auto & r : rooms)
    {
        delete r;
    }
}

bool room_base::apply(Json::Value data, std::string & error, bool allow_placeholders)
{
    if (data.isMember("f"))
    {
        if (!data["f"].isArray())
        {
            error = "f (furniture list) must be an array";
            return false;
        }

        for (auto & fdata : data["f"])
        {
            furniture_t *f = new furniture_t();
            if (!f->apply(fdata, error, allow_placeholders))
            {
                delete f;
                return false;
            }

            layout.push_back(f);
        }

        data.removeMember("f");
    }

    if (data.isMember("r"))
    {
        if (!data["r"].isArray())
        {
            error = "r (room list) must be an array";
            return false;
        }

        for (auto & rdata : data["r"])
        {
            room_t *r = new room_t();
            if (!r->apply(rdata, error, allow_placeholders))
            {
                delete r;
                return false;
            }

            rooms.push_back(r);
        }

        data.removeMember("r");
    }

    if (!check_indexes(layout, rooms, error))
    {
        return false;
    }

    return apply_unhandled_properties(data, "", error);
}

bool room_template::apply(Json::Value data, std::string & error)
{
    min_placeholders = 0;

    if (!room_base::apply(data, error, true))
    {
        return false;
    }

    for (auto & f : layout)
    {
        if (f->has_placeholder)
        {
            min_placeholders = std::max(min_placeholders, f->placeholder + 1);
        }
    }
    for (auto & r : rooms)
    {
        if (r->has_placeholder)
        {
            min_placeholders = std::max(min_placeholders, r->placeholder + 1);
        }
    }

    return true;
}

bool room_instance::apply(Json::Value data, std::string & error)
{
    if (data.isMember("blacklist"))
    {
        if (!data["blacklist"].isArray())
        {
            error = "blacklist must be an array";
            return false;
        }

        for (auto & b : data["blacklist"])
        {
            if (!b.isString())
            {
                error = "blacklist entries must be strings";
                return false;
            }

            blacklist.insert(b.asString());
        }

        data.removeMember("blacklist");
    }

    if (data.isMember("p"))
    {
        if (!data["p"].isArray())
        {
            error = "p (placeholder list) must be an array";
            return false;
        }

        for (auto & p : data["p"])
        {
            if (!p.isObject())
            {
                error = "placeholders must be objects";
                return false;
            }

            placeholders.push_back(new Json::Value(p));
        }

        data.removeMember("p");
    }

    return room_base::apply(data, error, false);
}

room_instance::~room_instance()
{
    for (auto & p : placeholders)
    {
        delete p;
    }
}

room_blueprint::room_blueprint(const room_template *tmpl, const room_instance *inst) :
    origin(0, 0, 0),
    tmpl(tmpl),
    inst(inst),
    type(inst->type),
    tmpl_name(tmpl->name),
    name(inst->name),
    layout(),
    rooms(),
    max_noblesuite(-1),
    corridor(),
    interior(),
    no_room(),
    no_corridor()
{
}

room_blueprint::room_blueprint(const room_blueprint & rb) :
    origin(rb.origin),
    tmpl(),
    inst(),
    type(rb.type),
    tmpl_name(rb.tmpl_name),
    name(rb.name),
    layout(),
    rooms(),
    max_noblesuite(-1),
    corridor(),
    interior(),
    no_room(),
    no_corridor()
{
    for (auto f : rb.layout)
    {
        layout.push_back(new room_base::furniture_t(*f));
    }

    for (auto r : rb.rooms)
    {
        rooms.push_back(new room_base::room_t(*r));
    }

    build_cache();
}

room_blueprint::room_blueprint(const room_blueprint & rb, df::coord offset, const variable_string::context_t & context) :
    origin(rb.origin + offset),
    tmpl(),
    inst(),
    type(rb.type),
    tmpl_name(rb.tmpl_name),
    name(rb.name),
    layout(),
    rooms(),
    max_noblesuite(-1),
    corridor(),
    interior(),
    no_room(),
    no_corridor()
{
    for (auto f : rb.layout)
    {
        f = new room_base::furniture_t(*f);

        f->context = context;

        layout.push_back(f);
    }

    for (auto r : rb.rooms)
    {
        r = new room_base::room_t(*r);

        r->context = context;

        r->min = r->min + offset;
        r->max = r->max + offset;

        rooms.push_back(r);
    }

    build_cache();
}

room_blueprint::~room_blueprint()
{
    for (auto & f : layout)
    {
        delete f;
    }
    for (auto & r : rooms)
    {
        delete r;
    }
}

bool room_blueprint::apply(std::string & error)
{
    if (!tmpl || !inst)
    {
        error = "room_blueprint::apply has already been called!";
        return false;
    }

    if (size_t(tmpl->min_placeholders) > inst->placeholders.size())
    {
        error = "not enough placeholders in instance";
        return false;
    }

    room_base::layoutindex_t inst_layout_start(tmpl->layout.size());
    room_base::roomindex_t inst_rooms_start(tmpl->rooms.size());

    // use twos complement to avoid a compiler warning
    room_base::layoutindex_t inst_layout_start_inv = (~inst_layout_start) + 1;
    room_base::roomindex_t inst_rooms_start_inv = (~inst_rooms_start) + 1;

    for (auto & ft : tmpl->layout)
    {
        room_base::furniture_t *f = new room_base::furniture_t(*ft);
        Json::Value *p = f->has_placeholder ? inst->placeholders.at(size_t(f->placeholder)) : nullptr;
        if (p)
        {
            if (p->isMember("skip") && (*p)["skip"].asBool())
            {
                delete f;
                f = nullptr;
            }
            else
            {
                f->shift(inst_layout_start_inv, inst_rooms_start_inv);
                if (!f->apply(*p, error))
                {
                    delete f;
                    return false;
                }
                f->shift(inst_layout_start, inst_rooms_start);
            }
        }
        layout.push_back(f);
    }
    for (auto & rt : tmpl->rooms)
    {
        room_base::room_t *r = new room_base::room_t(*rt);
        Json::Value *p = r->has_placeholder ? inst->placeholders.at(size_t(r->placeholder)) : nullptr;
        if (p)
        {
            if (p->isMember("skip") && (*p)["skip"].asBool())
            {
                delete r;
                r = nullptr;
            }
            else
            {
                r->shift(inst_layout_start_inv, inst_rooms_start_inv);
                if (!r->apply(*p, error))
                {
                    delete r;
                    return false;
                }
                r->shift(inst_layout_start, inst_rooms_start);
            }
        }
        rooms.push_back(r);
    }
    for (auto & fi : inst->layout)
    {
        room_base::furniture_t *f = new room_base::furniture_t(*fi);
        f->shift(inst_layout_start, inst_rooms_start);
        layout.push_back(f);
    }
    for (auto & ri : inst->rooms)
    {
        room_base::room_t *r = new room_base::room_t(*ri);
        r->shift(inst_layout_start, inst_rooms_start);
        rooms.push_back(r);
    }

    if (!check_indexes(layout, rooms, error))
    {
        return false;
    }

    std::vector<room_base::layoutindex_t> layout_move;
    room_base::layoutindex_t layoutindex = 0;
    std::vector<room_base::roomindex_t> room_move;
    room_base::roomindex_t roomindex = 0;

    for (auto f : layout)
    {
        layout_move.push_back(layoutindex);
        if (f)
        {
            layoutindex++;

            if (f->has_target && !layout.at(f->target))
            {
                f->has_target = false;
            }
        }
    }

    for (auto r : rooms)
    {
        room_move.push_back(roomindex);
        if (r)
        {
            roomindex++;

            r->accesspath.erase(std::remove_if(r->accesspath.begin(), r->accesspath.end(), [this](room_base::roomindex_t idx) -> bool
            {
                return !rooms.at(idx);
            }), r->accesspath.end());

            r->layout.erase(std::remove_if(r->layout.begin(), r->layout.end(), [this](room_base::layoutindex_t idx) -> bool
            {
                return !layout.at(idx);
            }), r->layout.end());

            if (r->has_workshop && !rooms.at(r->workshop))
            {
                r->has_workshop = false;
            }
        }
    }

    layout.erase(std::remove_if(layout.begin(), layout.end(), [](room_base::furniture_t *f) -> bool { return !f; }), layout.end());
    rooms.erase(std::remove_if(rooms.begin(), rooms.end(), [](room_base::room_t *r) -> bool { return !r; }), rooms.end());

    for (auto f : layout)
    {
        if (f->has_target)
        {
            f->target = layout_move.at(f->target);
        }
    }

    for (auto r : rooms)
    {
        for (auto & a : r->accesspath)
        {
            a = room_move.at(a);
        }

        for (auto & f : r->layout)
        {
            f = layout_move.at(f);
        }

        if (r->has_workshop)
        {
            r->workshop = room_move.at(r->workshop);
        }
    }

    std::set<room_base::layoutindex_t> used_furniture;
    for (auto r : rooms)
    {
        for (auto f : r->layout)
        {
            if (!used_furniture.insert(f).second)
            {
                error = "multiple uses of a single furniture";
                return false;
            }
        }
    }

    layout_move.clear();
    layoutindex = 0;
    for (room_base::layoutindex_t i = 0; i < layout.size(); i++)
    {
        layout_move.push_back(layoutindex);
        if (!used_furniture.count(i))
        {
            delete layout.at(i);
            layout.at(i) = nullptr;
        }
        else
        {
            layout.at(layoutindex) = layout.at(i);
            layoutindex++;
        }
    }

    layout.erase(layout.begin() + layoutindex, layout.end());

    for (auto f : layout)
    {
        if (f->has_target)
        {
            f->target = layout_move.at(f->target);
        }
    }

    for (auto r : rooms)
    {
        r->blueprint = type + "/" + tmpl_name + "/" + name;

        for (auto & f : r->layout)
        {
            f = layout_move.at(f);
        }
    }

    tmpl = nullptr;
    inst = nullptr;

    build_cache();

    return true;
}

bool room_blueprint::warn(std::string & error)
{
    std::vector<bool> furniture_used;
    furniture_used.resize(layout.size(), false);
    for (auto ri = rooms.begin(); ri != rooms.end(); ri++)
    {
        auto r = *ri;
        for (auto fi : r->layout)
        {
            furniture_used.at(fi) = true;

            if (r->type != room_type::cistern)
            {
                auto f = layout.at(fi);
                df::coord pos = r->min + f->pos;
                if (pos.x > r->max.x + 1)
                {
                    error = stl_sprintf("room %d, furniture %zu: furniture outside of east wall", int(ri - rooms.begin()), fi);
                    return true;
                }
                if (pos.y > r->max.y + 1)
                {
                    error = stl_sprintf("room %d, furniture %zu: furniture outside of south wall", int(ri - rooms.begin()), fi);
                    return true;
                }
                if (pos.x < r->min.x - 1)
                {
                    error = stl_sprintf("room %d, furniture %zu: furniture outside of west wall", int(ri - rooms.begin()), fi);
                    return true;
                }
                if (pos.y < r->min.y - 1)
                {
                    error = stl_sprintf("room %d, furniture %zu: furniture outside of north wall", int(ri - rooms.begin()), fi);
                    return true;
                }
            }
        }
    }
    for (size_t i = 0; i < furniture_used.size(); i++)
    {
        if (!furniture_used.at(i))
        {
            error = stl_sprintf("furniture %zu: not included in any room layout", i);
            return true;
        }
    }
    bool doorAtEntrance = false;
    size_t doorsNearEntrance = 0;
    for (auto & r : rooms)
    {
        if (r->outdoor || (r->min.x <= 0 && r->max.x >= 0 && r->min.y <= 0 && r->max.y >= 0 && r->min.z <= 0 && r->max.z >= 0))
        {
            doorAtEntrance = true;
            break;
        }

        for (auto fi : r->layout)
        {
            auto & f = layout.at(fi);
            if (f->construction == construction_type::Wall || f->dig == tile_dig_designation::No)
            {
                continue;
            }

            df::coord pos = f->pos + r->min;
            if (pos.x == 0 && pos.y == 0 && pos.z == 0)
            {
                doorAtEntrance = true;
                break;
            }

            if ((r->min.x == 1 || r->max.x == -1) && pos.x == 0 && pos.y >= -1 && pos.y <= 1 && pos.z == 0)
            {
                doorsNearEntrance++;
                continue;
            }
            if ((r->min.y == 1 || r->max.y == -1) && pos.x >= -1 && pos.x <= 1 && pos.y == 0 && pos.z == 0)
            {
                doorsNearEntrance++;
                continue;
            }
        }

        if (doorAtEntrance || doorsNearEntrance >= 2)
        {
            break;
        }
    }
    if (!doorAtEntrance && doorsNearEntrance < 2)
    {
        error = "room has no door at entrance";
        return true;
    }
    return false;
}

void room_blueprint::build_cache()
{
    max_noblesuite = -1;
    corridor.clear();
    interior.clear();
    no_room.clear();
    no_corridor.clear();

    for (auto r : rooms)
    {
        max_noblesuite = std::max(max_noblesuite, r->noblesuite);

        std::set<df::coord> holes;
        for (auto fi : r->layout)
        {
            auto f = layout.at(fi);
            if (f->dig == tile_dig_designation::No || f->construction == construction_type::Wall)
            {
                holes.insert(r->min + f->pos);
            }
            else if (r->type == room_type::corridor && r->corridor_type == corridor_type::corridor)
            {
                interior.insert(r->min + f->pos);
                no_room.insert(r->min + f->pos);
            }
            else if (r->in_corridor || !r->require_walls || r->outdoor)
            {
                interior.insert(r->min + f->pos);
                no_room.insert(r->min + f->pos);
            }
            else
            {
                if (f->type == layout_type::door)
                {
                    no_room.insert(r->min + f->pos);
                }
                else
                {
                    interior.insert(r->min + f->pos);
                    for (int16_t dx = -1; dx <= 1; dx++)
                    {
                        for (int16_t dy = -1; dy <= 1; dy++)
                        {
                            no_corridor.insert(r->min + f->pos + df::coord(dx, dy, 0));
                            no_room.insert(r->min + f->pos + df::coord(dx, dy, 0));
                        }
                    }
                }
            }
            if (f->dig == tile_dig_designation::Channel)
            {
                if (!r->exits.count(f->pos + df::coord(0, 0, -1)))
                {
                    interior.insert(r->min + f->pos + df::coord(0, 0, -1));
                    for (int16_t dx = -1; dx <= 1; dx++)
                    {
                        for (int16_t dy = -1; dy <= 1; dy++)
                        {
                            no_room.insert(r->min + f->pos + df::coord(dx, dy, -1));
                        }
                    }
                }
            }
            else if (f->dig == tile_dig_designation::Ramp)
            {
                interior.insert(r->min + f->pos + df::coord(0, 0, 1));
                for (int16_t dx = -1; dx <= 1; dx++)
                {
                    for (int16_t dy = -1; dy <= 1; dy++)
                    {
                        no_room.insert(r->min + f->pos + df::coord(dx, dy, 1));
                    }
                }
            }
        }

        for (int16_t x = r->min.x; x <= r->max.x; x++)
        {
            for (int16_t y = r->min.y; y <= r->max.y; y++)
            {
                for (int16_t z = r->min.z; z <= r->max.z; z++)
                {
                    df::coord t(x, y, z);
                    if (holes.count(t))
                    {
                        continue;
                    }

                    if (r->type == room_type::corridor && r->corridor_type == corridor_type::corridor && r->min.z == r->max.z)
                    {
                        if (!r->in_corridor)
                        {
                            corridor.insert(t);
                            no_corridor.insert(t);
                        }
                        else
                        {
                            interior.insert(t);
                            no_room.insert(t);
                        }
                    }
                    else if (r->in_corridor || !r->require_walls || r->outdoor)
                    {
                        interior.insert(t);
                        no_room.insert(t);
                        if (!r->in_corridor)
                        {
                            no_corridor.insert(t);
                        }
                    }
                    else
                    {
                        interior.insert(t);
                        for (int16_t dx = -1; dx <= 1; dx++)
                        {
                            for (int16_t dy = -1; dy <= 1; dy++)
                            {
                                if (!r->exits.count(t - r->min + df::coord(dx, dy, 0)))
                                {
                                    no_room.insert(t + df::coord(dx, dy, 0));
                                    no_corridor.insert(t + df::coord(dx, dy, 0));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void room_blueprint::write_layout(std::ostream & f)
{
    df::coord min, max;
    auto find_minmax = [&](const std::set<df::coord> & coords)
    {
        for (df::coord c : coords)
        {
            if (!min.isValid())
            {
                min = c;
            }
            if (!max.isValid())
            {
                max = c;
            }
            min.x = std::min(min.x, c.x);
            min.y = std::min(min.y, c.y);
            min.z = std::min(min.z, c.z);
            max.x = std::max(max.x, c.x);
            max.y = std::max(max.y, c.y);
            max.z = std::max(max.z, c.z);
        }
    };

    find_minmax(corridor);
    find_minmax(interior);
    find_minmax(no_corridor);
    find_minmax(no_room);

    for (int16_t z = min.z; z <= max.z; z++)
    {
        f << "z = " << z << std::endl;
        for (int16_t y = min.y; y <= max.y; y++)
        {
            if (y != min.y)
            {
                f << " ";
                for (int16_t x = min.x; x <= max.x; x++)
                {
                    f << "--";
                    if (x != max.x)
                    {
                        f << "+";
                    }
                }
                f << std::endl;
            }
            f << " ";
            for (int16_t x = min.x; x <= max.x; x++)
            {
                f << (corridor.count(df::coord(x, y, z)) ? "c" : " ");
                f << (no_corridor.count(df::coord(x, y, z)) ? "n" : " ");
                if (x != max.x)
                {
                    f << "|";
                }
            }
            f << std::endl << " ";
            for (int16_t x = min.x; x <= max.x; x++)
            {
                f << (interior.count(df::coord(x, y, z)) ? "i" : " ");
                f << (no_room.count(df::coord(x, y, z)) ? "r" : " ");
                if (x != max.x)
                {
                    f << "|";
                }
            }
            f << std::endl;
        }
        f << std::endl;
    }
}
