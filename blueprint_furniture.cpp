#include "blueprint.h"

room_base::furniture_t::furniture_t() :
    has_placeholder(false),
    placeholder(),
    type(layout_type::none),
    construction(construction_type::NONE),
    dig(tile_dig_designation::Default),
    pos(0, 0, 0),
    has_target(false),
    target(),
    has_users(0),
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

    if (data.isMember("has_users") && !apply_int(has_users, data, "has_users", error))
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

    if (data.isMember("comment") && !apply_variable_string(comment, data, "comment", error, true))
    {
        return false;
    }

    return apply_unhandled_properties(data, "furniture", error);
}

void room_base::furniture_t::shift(room_base::layoutindex_t layout_start, room_base::roomindex_t)
{
    if (has_target)
    {
        target += layout_start;
    }
}

bool room_base::furniture_t::check_indexes(room_base::layoutindex_t layout_limit, room_base::roomindex_t, std::string & error) const
{
    if (has_target && target >= layout_limit)
    {
        error = "invalid target";
        return false;
    }

    return true;
}
