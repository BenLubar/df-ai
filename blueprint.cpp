#include "blueprint.h"

#include "modules/Filesystem.h"

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

room_blueprint::room_blueprint(const room_template *tmpl, const room_instance *inst, df::coord origin) : tmpl(tmpl), inst(inst), origin(origin)
{
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

    return true;
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
                    if (auto inst = load_json<room_template>("df-ai-blueprints/rooms/templates/" + type + "/" + name, error))
                    {
                        blueprints[type].first.push_back(inst);
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

                    if (auto inst = load_json<room_instance>("df-ai-blueprints/rooms/instances/" + type + "/" + name, error))
                    {
                        blueprints[type].second.push_back(inst);
                    }
                    else
                    {
                        out.printerr("%s\n", error.c_str());
                    }
                }
            }
        }
    }
}

blueprints_t::~blueprints_t()
{
    for (auto & type : blueprints)
    {
        for (auto & tmpl : type.second.first)
        {
            delete tmpl;
        }
        for (auto & inst : type.second.second)
        {
            delete inst;
        }
    }
}
