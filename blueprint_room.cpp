#include "blueprint.h"

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
    has_users(0),
    temporary(false),
    outdoor(false),
    single_biome(false),
    require_walls(true),
    require_floor(true),
    require_grass(0),
    require_stone(false),
    in_corridor(false),
    remove_if_unused(false),
    exits(),
    context(),
    blueprint()
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

    if (data.isMember("raw_type") && !apply_variable_string(raw_type, data, "raw_type", error))
    {
        return false;
    }

    if (data.isMember("comment") && !apply_variable_string(comment, data, "comment", error, true))
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

        if (data.isMember("exits"))
        {
            Json::Value value = data["exits"];
            data.removeMember("exits");
            if (!value.isArray())
            {
                error = "exits has wrong type (should be array)";
                return false;
            }

            for (auto exit : value)
            {
                if (!exit.isArray() || exit.size() < 4 || exit.size() > 5 || !exit[0].isString() || !exit[1].isInt() || !exit[2].isInt() || !exit[3].isInt() || (exit.size() > 4 && !exit[4].isObject()))
                {
                    error = "exit has wrong type (should be [string, integer, integer, integer])";
                    return false;
                }

                df::coord t(exit[1].asInt(), exit[2].asInt(), exit[3].asInt());
                std::map<std::string, variable_string> context;
                if (exit.size() > 4)
                {
                    std::vector<std::string> vars(exit[4].getMemberNames());
                    for (auto var : vars)
                    {
                        if (!apply_variable_string(context[var], exit[4], var, error))
                        {
                            error = "exit variable " + error;
                            return false;
                        }
                    }
                }
                exits[t][exit[0].asString()] = context;
            }
        }
    }

    if (data.isMember("optional_walls"))
    {
        Json::Value value = data["optional_walls"];
        data.removeMember("optional_walls");
        if (!value.isArray())
        {
            error = "optional_walls has wrong type (should be array)";
        }

        for (auto optwall : value)
        {
            if (!optwall.isArray() || optwall.size() != 3 || !optwall[0].isInt() || !optwall[1].isInt() || !optwall[2].isInt())
            {
                error = "optional_walls element has the wrong type (should be an array of three integers)";
                return false;
            }

            // ensure key exists
            exits[df::coord(
                uint16_t(optwall[0].asInt()),
                uint16_t(optwall[1].asInt()),
                uint16_t(optwall[2].asInt())
            )];
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
    if (data.isMember("noblesuite"))
    {
        bool is_noblesuite;
        if (!apply_index(is_noblesuite, noblesuite, data, "noblesuite", error))
        {
            return false;
        }
        if (!is_noblesuite)
        {
            noblesuite = -1;
        }
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

    if (data.isMember("has_users") && !apply_int(has_users, data, "has_users", error))
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
    if (data.isMember("single_biome") && !apply_bool(single_biome, data, "single_biome", error))
    {
        return false;
    }

    if (data.isMember("require_walls") && !apply_bool(require_walls, data, "require_walls", error))
    {
        return false;
    }
    if (data.isMember("require_floor") && !apply_bool(require_floor, data, "require_floor", error))
    {
        return false;
    }
    if (data.isMember("require_grass") && !apply_int(require_grass, data, "require_grass", error))
    {
        return false;
    }
    if (data.isMember("require_stone") && !apply_bool(require_stone, data, "require_stone", error))
    {
        return false;
    }
    if (data.isMember("in_corridor") && !apply_bool(in_corridor, data, "in_corridor", error))
    {
        return false;
    }
    if (data.isMember("remove_if_unused") && !apply_bool(remove_if_unused, data, "remove_if_unused", error))
    {
        return false;
    }

    return apply_unhandled_properties(data, "room", error);
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
