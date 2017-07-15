#include "blueprint.h"

#include "modules/Filesystem.h"
#include "modules/Maps.h"

template<typename idx_t>
static bool apply_index(bool & has_idx, idx_t & idx, Json::Value & data, const std::string & name, std::string & error)
{
    Json::Value value = data.removeMember(name);
    if (!value.isInt())
    {
        error = name + " has wrong type (should be integer)";
        return false;
    }

    if (value.asInt() < 0)
    {
        error = name + " cannot be negative";
        return false;
    }

    has_idx = true;
    idx = idx_t(value.asInt());

    return true;
}

template<typename idx_t>
static bool apply_indexes(std::vector<idx_t> & idx, Json::Value & data, const std::string & name, std::string & error)
{
    Json::Value value = data.removeMember(name);
    if (!value.isArray() || std::find_if(value.begin(), value.end(), [](Json::Value & v) -> bool { return !v.isInt(); }) != value.end())
    {
        error = name + " has wrong type (should be array of integers)";
        return false;
    }

    for (auto & i : value)
    {
        if (i.asInt() < 0)
        {
            error = name + " cannot be negative";
            return false;
        }

        idx.push_back(idx_t(i.asInt()));
    }

    return true;
}

template<typename enum_t>
static bool apply_enum(enum_t & var, Json::Value & data, const std::string & name, std::string & error)
{
    Json::Value value = data.removeMember(name);
    if (!value.isString())
    {
        error = name + " has wrong type (should be string)";
        return false;
    }

    if (find_enum_item(&var, value.asString()))
    {
        return true;
    }

    error = "invalid " + name + " (" + value.asString() + ")";
    return false;
}

template<typename enum_t>
static bool apply_enum_set(std::set<enum_t> & var, Json::Value & data, const std::string & name, std::string & error)
{
    Json::Value value = data.removeMember(name);
    if (!value.isArray() || std::find_if(value.begin(), value.end(), [](Json::Value & v) -> bool { return !v.isString(); }) != value.end())
    {
        error = name + " has wrong type (should be array of strings)";
        return false;
    }

    for (auto & i : value)
    {
        enum_t e;
        if (!find_enum_item(&e, i.asString()))
        {
            error = "invalid " + name + " (" + i.asString() + ")";
            return false;
        }

        if (!var.insert(e).second)
        {
            error = "duplicate " + name + " (" + i.asString() + ")";
            return false;
        }
    }

    return true;
}

template<typename int_t>
static bool apply_int(int_t & var, Json::Value & data, const std::string & name, std::string & error)
{
    Json::Value value = data.removeMember(name);
    if (!value.isIntegral())
    {
        error = name + " has wrong type (should be integer)";
        return false;
    }

    if (std::is_unsigned<int_t>::value)
    {
        if (value.asLargestUInt() > std::numeric_limits<int_t>::max())
        {
            error = name + " is too big!";
            return false;
        }

        var = int_t(value.asLargestUInt());
    }
    else
    {
        if (value.asLargestInt() < std::numeric_limits<int_t>::min())
        {
            error = name + " is too small!";
            return false;
        }

        if (value.asLargestInt() > std::numeric_limits<int_t>::max())
        {
            error = name + " is too big!";
            return false;
        }

        var = int_t(value.asLargestInt());
    }

    return true;
}

static bool apply_bool(bool & var, Json::Value & data, const std::string & name, std::string & error)
{
    Json::Value value = data.removeMember(name);
    if (!value.isBool())
    {
        error = name + " has wrong type (should be true or false)";
        return false;
    }

    var = value.asBool();

    return true;
}

static bool apply_string(std::string & var, Json::Value & data, const std::string & name, std::string & error, bool append = false)
{
    Json::Value value = data.removeMember(name);
    if (!value.isString())
    {
        error = name + " has wrong type (should be string)";
        return false;
    }

    if (append)
    {
        var += value.asString();
    }
    else
    {
        var = value.asString();
    }

    return true;
}

static bool apply_coord(df::coord & var, Json::Value & data, const std::string & name, std::string & error)
{
    Json::Value value = data.removeMember(name);
    if (!value.isArray() || value.size() != 3 || !value[0].isInt() || !value[1].isInt() || !value[2].isInt())
    {
        error = name + " has the wrong type (should be an array of three integers)";
        return false;
    }

    var.x = int16_t(value[0].asInt());
    var.y = int16_t(value[1].asInt());
    var.z = int16_t(value[2].asInt());

    return true;
}

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

    for (auto r : layout)
    {
        if (r && !r->check_indexes(layout_limit, room_limit, error))
        {
            return false;
        }
    }

    return true;
}

room_base::furniture_t::furniture_t() :
    has_placeholder(false),
    placeholder(),
    type(layout_type::none),
    construction(construction_type::NONE),
    dig(tile_dig_designation::Default),
    pos(0, 0, 0),
    has_target(false),
    target(),
    has_users(false),
    ignore(false),
    makeroom(false),
    internal(false),
    comment()
{
}

bool room_base::furniture_t::apply(Json::Value data, std::string & error, bool allow_placeholders)
{
    std::ostringstream scratch;

    if (allow_placeholders && data.isMember("placeholder") && !apply_index(has_placeholder, placeholder, data, "placeholder", error))
    {
        return false;
    }

    if (data.isMember("type") && !apply_enum(type, data, "type", error))
    {
        return false;
    }

    if (data.isMember("construction") && !apply_enum(construction, data, "construction", error))
    {
        return false;
    }

    if (data.isMember("dig") && !apply_enum(dig, data, "dig", error))
    {
        return false;
    }

    if (data.isMember("x") && !apply_int(pos.x, data, "x", error))
    {
        return false;
    }
    if (data.isMember("y") && !apply_int(pos.y, data, "y", error))
    {
        return false;
    }
    if (data.isMember("z") && !apply_int(pos.z, data, "z", error))
    {
        return false;
    }

    if (data.isMember("target") && !apply_index(has_target, target, data, "target", error))
    {
        return false;
    }

    if (data.isMember("has_users") && !apply_bool(has_users, data, "has_users", error))
    {
        return false;
    }

    if (data.isMember("ignore") && !apply_bool(ignore, data, "ignore", error))
    {
        return false;
    }

    if (data.isMember("makeroom") && !apply_bool(makeroom, data, "makeroom", error))
    {
        return false;
    }

    if (data.isMember("internal") && !apply_bool(internal, data, "internal", error))
    {
        return false;
    }

    if (data.isMember("comment") && !apply_string(comment, data, "comment", error, true))
    {
        return false;
    }

    std::vector<std::string> remaining_members(data.getMemberNames());
    if (remaining_members.empty())
    {
        return true;
    }

    error = "";
    const char *before = "unhandled furniture properties: ";
    for (auto & m : remaining_members)
    {
        error += before;
        error += m;
        before = ", ";
    }

    return false;
}

void room_base::furniture_t::shift(room_base::layoutindex_t layout_start, room_base::roomindex_t room_start)
{
    if (has_target)
    {
        target += layout_start;
    }
}

bool room_base::furniture_t::check_indexes(room_base::layoutindex_t layout_limit, room_base::roomindex_t room_limit, std::string & error) const
{
    if (has_target && target >= layout_limit)
    {
        error = "invalid target";
        return false;
    }

    return true;
}

room_base::room_t::room_t() :
    has_placeholder(false),
    placeholder(),
    type(room_type::corridor),
    corridor_type(),
    farm_type(),
    stockpile_type(),
    nobleroom_type(),
    outpost_type(),
    location_type(),
    cistern_type(),
    workshop_type(),
    furnace_type(),
    raw_type(),
    comment(),
    min(),
    max(),
    accesspath(),
    layout(),
    level(-1),
    noblesuite(-1),
    queue(0),
    has_workshop(false),
    workshop(),
    stock_disable(),
    stock_specific1(false),
    stock_specific2(false),
    has_users(false),
    temporary(false),
    outdoor(false)
{
}

bool room_base::room_t::apply(Json::Value data, std::string & error, bool allow_placeholders)
{
    if (allow_placeholders && data.isMember("placeholder") && !apply_index(has_placeholder, placeholder, data, "placeholder", error))
    {
        return false;
    }

    if (data.isMember("type") && !apply_enum(type, data, "type", error))
    {
        return false;
    }

    if (data.isMember("corridor_type") && !apply_enum(corridor_type, data, "corridor_type", error))
    {
        return false;
    }
    if (data.isMember("farm_type") && !apply_enum(farm_type, data, "farm_type", error))
    {
        return false;
    }
    if (data.isMember("stockpile_type") && !apply_enum(stockpile_type, data, "stockpile_type", error))
    {
        return false;
    }
    if (data.isMember("nobleroom_type") && !apply_enum(nobleroom_type, data, "nobleroom_type", error))
    {
        return false;
    }
    if (data.isMember("outpost_type") && !apply_enum(outpost_type, data, "outpost_type", error))
    {
        return false;
    }
    if (data.isMember("location_type") && !apply_enum(location_type, data, "location_type", error))
    {
        return false;
    }
    if (data.isMember("cistern_type") && !apply_enum(cistern_type, data, "cistern_type", error))
    {
        return false;
    }
    if (data.isMember("workshop_type") && !apply_enum(workshop_type, data, "workshop_type", error))
    {
        return false;
    }
    if (data.isMember("furnace_type") && !apply_enum(furnace_type, data, "furnace_type", error))
    {
        return false;
    }

    if (data.isMember("raw_type") && !apply_string(raw_type, data, "raw_type", error))
    {
        return false;
    }

    if (data.isMember("comment") && !apply_string(comment, data, "comment", error, true))
    {
        return false;
    }

    if (!min.isValid())
    {
        if (data.isMember("min"))
        {
            if (!apply_coord(min, data, "min", error))
            {
                return false;
            }
        }
        else
        {
            if (data.isMember("max"))
            {
                error = "missing min on room";
            }
            else
            {
                error = "missing min and max on room";
            }
            return false;
        }

        if (data.isMember("max"))
        {
            if (!apply_coord(max, data, "max", error))
            {
                return false;
            }
        }
        else
        {
            error = "missing max on room";
            return false;
        }

        if (min.x > max.x)
        {
            error = "min.x > max.x";
            return false;
        }
        if (min.y > max.y)
        {
            error = "min.y > max.y";
            return false;
        }
        if (min.z > max.z)
        {
            error = "min.z > max.z";
            return false;
        }
    }

    if (data.isMember("accesspath") && !apply_indexes(accesspath, data, "accesspath", error))
    {
        return false;
    }
    if (data.isMember("layout") && !apply_indexes(layout, data, "layout", error))
    {
        return false;
    }

    if (data.isMember("level") && !apply_int(level, data, "level", error))
    {
        return false;
    }
    if (data.isMember("noblesuite") && !apply_int(noblesuite, data, "noblesuite", error))
    {
        return false;
    }
    if (data.isMember("queue") && !apply_int(queue, data, "queue", error))
    {
        return false;
    }

    if (data.isMember("workshop") && !apply_index(has_workshop, workshop, data, "workshop", error))
    {
        return false;
    }

    if (data.isMember("stock_disable") && !apply_enum_set(stock_disable, data, "stock_disable", error))
    {
        return false;
    }
    if (data.isMember("stock_specific1") && !apply_bool(stock_specific1, data, "stock_specific1", error))
    {
        return false;
    }
    if (data.isMember("stock_specific2") && !apply_bool(stock_specific2, data, "stock_specific2", error))
    {
        return false;
    }

    if (data.isMember("has_users") && !apply_bool(has_users, data, "has_users", error))
    {
        return false;
    }
    if (data.isMember("temporary") && !apply_bool(temporary, data, "temporary", error))
    {
        return false;
    }
    if (data.isMember("outdoor") && !apply_bool(outdoor, data, "outdoor", error))
    {
        return false;
    }

    if (data.isMember("in_corridor") && !apply_bool(in_corridor, data, "in_corridor", error))
    {
        return false;
    }

    std::vector<std::string> remaining_members(data.getMemberNames());
    if (remaining_members.empty())
    {
        return true;
    }

    error = "";
    const char *before = "unhandled room properties: ";
    for (auto & m : remaining_members)
    {
        error += before;
        error += m;
        before = ", ";
    }

    return false;
}

void room_base::room_t::shift(room_base::layoutindex_t layout_start, room_base::roomindex_t room_start)
{
    for (auto & r : accesspath)
    {
        r += room_start;
    }
    for (auto & f : layout)
    {
        f += layout_start;
    }
    if (has_workshop)
    {
        workshop += room_start;
    }
}

bool room_base::room_t::check_indexes(room_base::layoutindex_t layout_limit, room_base::roomindex_t room_limit, std::string & error) const
{
    for (auto r : accesspath)
    {
        if (r >= room_limit)
        {
            error = "invalid accesspath";
            return false;
        }
    }
    for (auto f : layout)
    {
        if (f >= layout_limit)
        {
            error = "invalid layout";
            return false;
        }
    }
    if (has_workshop && workshop >= room_limit)
    {
        error = "invalid workshop";
        return false;
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

    std::vector<std::string> remaining_members(data.getMemberNames());
    if (remaining_members.empty())
    {
        return true;
    }

    error = "";
    const char *before = "unhandled properties: ";
    for (auto & m : remaining_members)
    {
        error += before;
        error += m;
        before = ", ";
    }

    return false;
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
    layout(tmpl->layout.size() + inst->layout.size()),
    rooms(tmpl->rooms.size() + inst->rooms.size()),
    interior(),
    no_room(),
    no_corridor()
{
}

room_blueprint::room_blueprint(const room_blueprint & rb, df::coord offset) :
    origin(rb.origin + offset),
    tmpl(),
    inst(),
    layout(rb.layout.size()),
    rooms(rb.rooms.size()),
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
        r = new room_base::room_t(*r);

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
        Json::Value *p = f->placeholder != -1 ? inst->placeholders.at(size_t(f->placeholder)) : nullptr;
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
        Json::Value *p = r->placeholder != -1 ? inst->placeholders.at(size_t(r->placeholder)) : nullptr;
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

    tmpl = nullptr;
    inst = nullptr;

    build_cache();

    return true;
}

void room_blueprint::build_cache()
{
    interior.clear();
    no_room.clear();
    no_corridor.clear();

    for (auto r : rooms)
    {
        std::set<df::coord> holes;
        for (auto fi : r->layout)
        {
            auto f = layout.at(fi);
            if (f->dig == tile_dig_designation::No || f->construction == construction_type::Wall)
            {
                holes.insert(r->min + f->pos);
            }
            else
            {
                interior.insert(r->min + f->pos);
                for (int16_t dx = -1; dx <= 1; dx++)
                {
                    for (int16_t dy = -1; dy <= 1; dy++)
                    {
                        no_room.insert(r->min + f->pos + df::coord(dx, dy, 0));
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

                    interior.insert(t);
                    for (int16_t dx = -1; dx <= 1; dx++)
                    {
                        for (int16_t dy = -1; dy <= 1; dy++)
                        {
                            no_room.insert(t + df::coord(dx, dy, 0));
                            if (!r->in_corridor)
                            {
                                no_corridor.insert(t + df::coord(dx, dy, 0));
                            }
                        }
                    }
                }
            }
        }
    }
}

blueprint_plan::~blueprint_plan()
{
    for (auto r : rooms)
    {
        delete r;
    }
    for (auto f : layout)
    {
        delete f;
    }
}

bool blueprint_plan::add(const room_blueprint & rb, std::string & error)
{
    for (auto c : rb.interior)
    {
        if (no_room.count(c))
        {
            error = "room interior intersects no_room tile";
            return false;
        }
    }
    for (auto c : rb.no_corridor)
    {
        if (corridor.count(c))
        {
            error = "room no_corridor intersects corridor tile";
            return false;
        }
    }
    for (auto c : rb.no_room)
    {
        if (interior.count(c))
        {
            error = "room no_room intersects interior tile";
            return false;
        }
    }

    room_base::roomindex_t parent;
    if (!build_corridor_to(rb, parent, error))
    {
        return false;
    }

    room_base::layoutindex_t layout_start = layout.size();
    room_base::roomindex_t room_start = rooms.size();

    for (auto f : rb.layout)
    {
        f = new room_base::furniture_t(*f);
        f->shift(layout_start, room_start);
        layout.push_back(f);
    }

    for (auto r : rb.rooms)
    {
        r = new room_base::room_t(*r);
        r->shift(layout_start, room_start);
        r->accesspath.push_back(parent);
        rooms.push_back(r);
    }

    for (auto c : rb.interior)
    {
        interior.insert(c);
    }

    for (auto c : rb.no_room)
    {
        no_room.insert(c);
    }

    for (auto c : rb.no_corridor)
    {
        no_corridor.insert(c);
        corridor_connect.erase(c);
    }

    return true;
}

bool blueprint_plan::build_corridor_to(const room_blueprint & rb, room_base::roomindex_t & parent, std::string & error)
{
    if (!Maps::isValidTilePos(rb.origin))
    {
        error = "invalid target for corridor";
        return false;
    }

    if (corridor_connect.count(rb.origin))
    {
        return true;
    }

    df::coord target;
    if (!rb.no_corridor.count(rb.origin))
    {
        target = rb.origin;
    }
    else if (!rb.no_corridor.count(rb.origin + df::coord(-1, 0, 0)))
    {
        target = rb.origin + df::coord(-1, 0, 0);
    }
    else if (!rb.no_corridor.count(rb.origin + df::coord(1, 0, 0)))
    {
        target = rb.origin + df::coord(1, 0, 0);
    }
    else if (!rb.no_corridor.count(rb.origin + df::coord(0, -1, 0)))
    {
        target = rb.origin + df::coord(0, -1, 0);
    }
    else if (!rb.no_corridor.count(rb.origin + df::coord(0, 1, 0)))
    {
        target = rb.origin + df::coord(0, 1, 0);
    }
    else
    {
        error = "could not find room entrance";
        return false;
    }

    std::vector<room_base::room_t *> accesspath;

    int32_t best_distance = -1;

    for (auto & c : corridor_connect)
    {
        bool fail = false;
        int32_t distance = 0;

        // TODO: pathfinding

        if (c.first.x >= target.x)
        {
            for (int16_t x = c.first.x; x <= target.x; x++)
            {
                if (no_corridor.count(df::coord(x, c.first.y, c.first.z)) || rb.no_corridor.count(df::coord(x, c.first.y, c.first.z)))
                {
                    fail = true;
                    break;
                }
                distance++;
            }
        }
        else
        {
            for (int16_t x = c.first.x; x >= target.x; x--)
            {
                if (no_corridor.count(df::coord(x, c.first.y, c.first.z)) || rb.no_corridor.count(df::coord(x, c.first.y, c.first.z)))
                {
                    fail = true;
                    break;
                }
                distance++;
            }
        }

        if (fail)
        {
            continue;
        }

        if (c.first.y >= target.y)
        {
            for (int16_t y = c.first.y; y <= target.y; y++)
            {
                if (no_corridor.count(df::coord(target.x, y, c.first.z)) || rb.no_corridor.count(df::coord(target.x, y, c.first.z)))
                {
                    fail = true;
                    break;
                }
                distance++;
            }
        }
        else
        {
            for (int16_t y = c.first.y; y >= target.y; y--)
            {
                if (no_corridor.count(df::coord(target.x, y, c.first.z)) || rb.no_corridor.count(df::coord(target.x, y, c.first.z)))
                {
                    fail = true;
                    break;
                }
                distance++;
            }
        }

        if (fail)
        {
            continue;
        }

        if (c.first.z >= target.z)
        {
            for (int16_t z = c.first.z; z <= target.z; z++)
            {
                if (no_corridor.count(df::coord(target.x, target.y, z)) || rb.no_corridor.count(df::coord(target.x, target.y, z)))
                {
                    fail = true;
                    break;
                }
                distance++;
            }
        }
        else
        {
            for (int16_t z = c.first.z; z >= target.z; z--)
            {
                if (no_corridor.count(df::coord(target.x, target.y, z)) || rb.no_corridor.count(df::coord(target.x, target.y, z)))
                {
                    fail = true;
                    break;
                }
                distance++;
            }
        }

        if (!fail && (best_distance == -1 || distance < best_distance))
        {
            for (auto r : accesspath)
            {
                delete r;
            }
            accesspath.clear();

            parent = c.second;

            if (c.first.x != target.x)
            {
                auto r = new room_base::room_t();
                r->type = room_type::corridor;
                r->corridor_type = corridor_type::corridor;
                r->accesspath.push_back(parent);
                r->min = df::coord(std::min(c.first.x, target.x), c.first.y, c.first.z);
                r->max = df::coord(std::max(c.first.x, target.x), c.first.y, c.first.z);
                if (c.first.y != target.y || c.first.z != target.z)
                {
                    if (c.first.x > target.x)
                    {
                        r->min.x++;
                    }
                    else
                    {
                        r->max.x--;
                    }
                }
                parent = rooms.size() + accesspath.size();
                accesspath.push_back(r);
            }

            if (c.first.y != target.y)
            {
                auto r = new room_base::room_t();
                r->type = room_type::corridor;
                r->corridor_type = corridor_type::corridor;
                r->accesspath.push_back(parent);
                r->min = df::coord(target.x, std::min(c.first.y, target.y), c.first.z);
                r->max = df::coord(target.x, std::max(c.first.y, target.y), c.first.z);
                if (c.first.z != target.z)
                {
                    if (c.first.y > target.y)
                    {
                        r->min.y++;
                    }
                    else
                    {
                        r->max.y--;
                    }
                }
                parent = rooms.size() + accesspath.size();
                accesspath.push_back(r);
            }

            if (c.first.z != target.z)
            {
                auto r = new room_base::room_t();
                r->type = room_type::corridor;
                r->corridor_type = corridor_type::corridor;
                r->accesspath.push_back(parent);
                r->min = df::coord(target.x, target.y, std::min(c.first.z, target.z));
                r->max = df::coord(target.x, target.y, std::max(c.first.z, target.z));
                parent = rooms.size() + accesspath.size();
                accesspath.push_back(r);
            }

            best_distance = distance;
        }
    }

    if (best_distance == -1)
    {
        error = "could not find path";
        return false;
    }

    size_t base = rooms.size();

    for (auto r : accesspath)
    {
        rooms.push_back(r);

        for (int16_t x = r->min.x; x <= r->max.x; x++)
        {
            for (int16_t y = r->min.y; y <= r->max.y; y++)
            {
                for (int16_t z = r->min.z; z <= r->max.z; z++)
                {
                    df::coord t(x, y, z);
                    if (r->min.z == r->max.z)
                    {
                        corridor.insert(t);
                    }
                    else
                    {
                        interior.insert(t);
                    }
                    no_room.insert(t);
                    no_corridor.insert(t);
                    corridor_connect.erase(t);
                }
            }
        }
    }

    for (size_t i = 0; i < accesspath.size(); i++)
    {
        auto r = accesspath.at(i);
        for (int16_t z = r->min.z; z <= r->max.z; z++)
        {
            for (int16_t x = r->min.x; x <= r->max.x; x++)
            {
                df::coord t(x, r->min.y - 1, z);
                if (!no_corridor.count(t) && !corridor_connect.count(t))
                {
                    corridor_connect[t] = base + i;
                }
                t.y = r->max.y + 1;
                if (!no_corridor.count(t) && !corridor_connect.count(t))
                {
                    corridor_connect[t] = base + i;
                }
            }
            for (int16_t y = r->min.y; y <= r->max.y; y++)
            {
                df::coord t(r->min.x - 1, y, z);
                if (!no_corridor.count(t) && !corridor_connect.count(t))
                {
                    corridor_connect[t] = base + i;
                }
                t.x = r->max.x + 1;
                if (!no_corridor.count(t) && !corridor_connect.count(t))
                {
                    corridor_connect[t] = base + i;
                }
            }
        }
    }

    return true;
}

void blueprint_plan::create(std::vector<room *> & real_corridors, std::vector<room *> & real_rooms) const
{
    std::vector<furniture *> real_layout(layout.size());
    std::vector<room *> real_rooms_and_corridors(rooms.size());

    for (size_t i = 0; i < layout.size(); i++)
    {
        real_layout.push_back(new furniture());
    }
    for (size_t i = 0; i < rooms.size(); i++)
    {
        real_rooms_and_corridors.push_back(new room(room_type::type(), df::coord(), df::coord()));
    }

    for (size_t i = 0; i < layout.size(); i++)
    {
        auto in = layout.at(i);
        auto out = real_layout.at(i);

        out->type = in->type;
        out->construction = in->construction;
        out->dig = in->dig;
        out->pos = in->pos;

        if (in->has_target)
        {
            out->target = real_layout.at(in->target);
        }
        else
        {
            out->target = nullptr;
        }

        out->has_users = in->has_users;
        out->ignore = in->ignore;
        out->makeroom = in->makeroom;
        out->internal = in->internal;

        out->comment = in->comment;
    }
    for (size_t i = 0; i < rooms.size(); i++)
    {
        auto in = rooms.at(i);
        auto out = real_rooms_and_corridors.at(i);

        out->type = in->type;

        out->corridor_type = in->corridor_type;
        out->farm_type = in->farm_type;
        out->stockpile_type = in->stockpile_type;
        out->nobleroom_type = in->nobleroom_type;
        out->outpost_type = in->outpost_type;
        out->location_type = in->location_type;
        out->cistern_type = in->cistern_type;
        out->workshop_type = in->workshop_type;
        out->furnace_type = in->furnace_type;

        out->raw_type = in->raw_type;

        out->comment = in->comment;

        out->min = in->min;
        out->max = in->max;

        for (auto ri : in->accesspath)
        {
            out->accesspath.push_back(real_rooms_and_corridors.at(ri));
        }
        for (auto fi : in->layout)
        {
            out->layout.push_back(real_layout.at(fi));
        }

        out->level = in->level;
        out->noblesuite = in->noblesuite;
        out->queue = in->queue;

        if (in->has_workshop)
        {
            out->workshop = real_rooms_and_corridors.at(in->workshop);
        }
        else
        {
            out->workshop = nullptr;
        }

        out->stock_disable = in->stock_disable;
        out->stock_specific1 = in->stock_specific1;
        out->stock_specific2 = in->stock_specific2;

        out->has_users = in->has_users;
        out->temporary = in->temporary;
        out->outdoor = in->outdoor;
    }

    std::partition_copy(real_rooms_and_corridors.begin(), real_rooms_and_corridors.end(), real_corridors.end(), real_rooms.end(), [](room *r) -> bool { return r->type == room_type::corridor; });
}

template<typename T>
static T *load_json(const std::string & name, std::string & error)
{
    error.clear();
    std::ifstream f(name);
    Json::Value val;
    Json::CharReaderBuilder b;
    if (!Json::parseFromStream(b, f, &val, &error))
    {
    }
    else if (f.good())
    {
        if (val.isObject())
        {
            T *t = new T();
            if (t->apply(val, error))
            {
                return t;
            }
            delete t;
        }
        else
        {
            error = "must be a JSON object";
        }
    }
    else
    {
        error = "error reading file";
    }
    error = name + ": " + error;
    return nullptr;
}

blueprints_t::blueprints_t(color_ostream & out)
{
    std::string error;

    std::map<std::string, std::pair<std::vector<std::pair<std::string, room_template *>>, std::vector<std::pair<std::string, room_instance *>>>> rooms;

    std::vector<std::string> types;
    if (!Filesystem::listdir("df-ai-blueprints/rooms/templates", types))
    {
        for (auto & type : types)
        {
            if (type.find('.') != std::string::npos)
            {
                continue;
            }

std::vector<std::string> names;
if (!Filesystem::listdir("df-ai-blueprints/rooms/templates/" + type, names))
{
    if (type.rfind(".json") != type.size() - strlen(".json"))
    {
        continue;
    }

    for (auto & name : names)
    {
        std::string path = "df-ai-blueprints/rooms/templates/" + type + "/" + name;

        if (auto inst = load_json<room_template>(path, error))
        {
            rooms[type].first.push_back(std::make_pair(path, inst));
        }
        else
        {
            out.printerr("%s\n", error.c_str());
        }
    }
}
        }
    }
    types.clear();
    if (!Filesystem::listdir("df-ai-blueprints/rooms/instances", types))
    {
        for (auto & type : types)
        {
            if (type.find('.') != std::string::npos)
            {
                continue;
            }

            std::vector<std::string> names;
            if (!Filesystem::listdir("df-ai-blueprints/rooms/instances/" + type, names))
            {
                for (auto & name : names)
                {
                    if (type.rfind(".json") != type.size() - strlen(".json"))
                    {
                        continue;
                    }

                    std::string path = "df-ai-blueprints/rooms/instances/" + type + "/" + name;

                    if (auto inst = load_json<room_instance>(path, error))
                    {
                        rooms[type].second.push_back(std::make_pair(path, inst));
                    }
                    else
                    {
                        out.printerr("%s\n", error.c_str());
                    }
                }
            }
        }
    }

    for (auto & type : rooms)
    {
        auto & rbs = blueprints[type.first];
        rbs.reserve(type.second.first.size() * type.second.second.size());
        for (auto tmpl : type.second.first)
        {
            for (auto inst : type.second.second)
            {
                room_blueprint *rb = new room_blueprint(tmpl.second, inst.second);
                if (!rb->apply(error))
                {
                    out.printerr("%s + %s: %s\n", tmpl.first.c_str(), inst.first.c_str(), error.c_str());
                    delete rb;
                    continue;
                }
                rbs.push_back(rb);
            }
            delete tmpl.second;
        }
        for (auto inst : type.second.second)
        {
            delete inst.second;
        }
    }
}

blueprints_t::~blueprints_t()
{
    for (auto & type : blueprints)
    {
        for (auto rb : type.second)
        {
            delete rb;
        }
    }
}

std::vector<const room_blueprint *> blueprints_t::operator[](const std::string & type) const
{
    auto it = blueprints.find(type);
    if (it == blueprints.end())
    {
        return std::vector<const room_blueprint *>();
    }

    return std::vector<const room_blueprint *>(it->second.begin(), it->second.end());
}
