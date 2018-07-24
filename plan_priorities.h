#pragma once

#include "dfhack_shared.h"
#include "json/json.h"
#include "room.h"

class AI;

#define PLAN_PRIORITY_ENUMS \
BEGIN_ENUM(plan_priority, action) \
    ENUM_ITEM(dig) \
    ENUM_ITEM(dig_immediate) \
    ENUM_ITEM(unignore_furniture) \
    ENUM_ITEM(finish) \
    ENUM_ITEM(start_ore_search) \
    ENUM_ITEM(past_initial_phase) \
    ENUM_ITEM(deconstruct_wagons) \
    ENUM_ITEM(dig_next_cavern_outpost) \
END_ENUM(plan_priority, action)

#define BEGIN_ENUM BEGIN_DECLARE_ENUM
#define ENUM_ITEM DECLARE_ENUM_ITEM
#define END_ENUM END_DECLARE_ENUM
PLAN_PRIORITY_ENUMS
#undef BEGIN_ENUM
#undef ENUM_ITEM
#undef END_ENUM

struct plan_priority_t
{
    template<typename int_t, int_t min_value = std::numeric_limits<int_t>::min(), int_t max_value = std::numeric_limits<int_t>::max()>
    struct between_t
    {
        using object_type = int_t;

        between_t() : min(), max(), has_min(false), has_max(false)
        {
        }

        int_t min;
        int_t max;
        bool has_min;
        bool has_max;

        bool is_match(int_t i) const
        {
            return (!has_min || i >= min) && (!has_max || i <= max);
        }

        bool apply(Json::Value & val, std::string & error)
        {
            Json::Value scratch(Json::objectValue);
            if (val.isIntegral())
            {
                scratch["between"] = val;
                if (!apply_int(min, scratch, "between", error, min_value, max_value))
                {
                    return false;
                }
                has_min = true;
                has_max = true;
                max = min;
                return true;
            }
            if (!val.isArray() || val.size() != 2)
            {
                error = "between must be an integer or an array of two integers or nulls";
                return false;
            }

            if (val[0].isNull())
            {
                has_min = false;
            }
            else
            {
                has_min = true;
                scratch["min"] = val[0];
                if (!apply_int(min, scratch, "min", error, min_value, max_value))
                {
                    return false;
                }
            }

            if (val[1].isNull())
            {
                has_max = false;
            }
            else
            {
                has_max = true;
                scratch["max"] = val[1];
                if (!apply_int(max, scratch, "max", error, has_min ? min : min_value, max_value))
                {
                    return false;
                }
            }

            return true;
        }
        Json::Value to_json() const
        {
            if (has_min && has_max && min == max)
            {
                if (std::is_unsigned<int_t>::value)
                {
                    return Json::LargestUInt(min);
                }
                return Json::LargestInt(min);
            }

            Json::Value val(Json::arrayValue);

            if (has_min)
            {
                if (std::is_unsigned<int_t>::value)
                {
                    val.append(Json::LargestUInt(min));
                }
                else
                {
                    val.append(Json::LargestInt(min));
                }
            }
            else
            {
                val.append(Json::nullValue);
            }

            if (has_max)
            {
                if (std::is_unsigned<int_t>::value)
                {
                    val.append(Json::LargestUInt(max));
                }
                else
                {
                    val.append(Json::LargestInt(max));
                }
            }
            else
            {
                val.append(Json::nullValue);
            }

            return val;
        }
    };

    template<typename filter_t, typename object_t = std::vector<typename filter_t::object_type>>
    struct count_t
    {
        using object_type = object_t;

        std::vector<filter_t> match;
        between_t<size_t> is;

        bool is_match(const object_type & obj) const
        {
            if (!is.has_min && !is.has_max)
            {
                return true;
            }

            size_t count = 0;
            for (const auto & i : obj)
            {
                bool any = false;
                for (auto & m : match)
                {
                    if (m.is_match(i))
                    {
                        any = true;
                        break;
                    }
                }
                if (any)
                {
                    count++;
                    if (is.has_max && count > is.max)
                    {
                        return false;
                    }
                    if (is.has_min && count >= is.min && !is.has_max)
                    {
                        return true;
                    }
                }
            }

            return is.is_match(count);
        }

        bool apply(Json::Value & val, std::string & error)
        {
            if (!val.isObject() || !val.isMember("match") || !val.isMember("is"))
            {
                error = "count must be an object with \"match\" and \"is\" properties";
                return false;
            }
            if (!apply_optional_vector(match, val, "match", error))
            {
                error += " (match)";
                return false;
            }
            Json::Value is_val = val["is"];
            val.removeMember("is");
            if (!is.apply(is_val, error))
            {
                error += " (is)";
                return false;
            }

            return apply_unhandled_properties(val, "count", error);
        }

        Json::Value to_json() const
        {
            Json::Value obj(Json::objectValue);
            if (match.size() == 1)
            {
                obj["match"] = match.at(0).to_json();
            }
            else
            {
                Json::Value arr(Json::arrayValue);
                for (auto & m : match)
                {
                    arr.append(m.to_json());
                }
                obj["match"] = arr;
            }
            obj["is"] = is.to_json();
            return obj;
        }
    };

    struct bool_filter_t
    {
        using object_type = bool;

        bool_filter_t() : has_value(false), value()
        {
        }

        bool has_value;
        object_type value;

        bool is_match(const object_type & obj) const
        {
            return !has_value || value == obj;
        }

        bool apply(Json::Value & val, std::string & error)
        {
            Json::Value scratch(Json::objectValue);
            scratch["bool"] = val;
            has_value = true;
            return apply_bool(value, scratch, "bool", error);
        }
        Json::Value to_json() const
        {
            return value;
        }
    };

#define ROOM_FILTER_PROPERTIES \
    AI_ENUM_PROPERTY(room_status::status, status) \
    AI_ENUM_PROPERTY(room_type::type, type) \
    AI_ENUM_PROPERTY(corridor_type::type, corridor_type) \
    AI_ENUM_PROPERTY(farm_type::type, farm_type) \
    AI_ENUM_PROPERTY(stockpile_type::type, stockpile_type) \
    AI_ENUM_PROPERTY(nobleroom_type::type, nobleroom_type) \
    AI_ENUM_PROPERTY(outpost_type::type, outpost_type) \
    AI_ENUM_PROPERTY(location_type::type, location_type) \
    AI_ENUM_PROPERTY(cistern_type::type, cistern_type) \
    DF_ENUM_PROPERTY(df::workshop_type, workshop_type) \
    DF_ENUM_PROPERTY(df::furnace_type, furnace_type) \
    STRING_PROPERTY(raw_type) \
    STRING_PROPERTY(comment) \
    COUNT_PROPERTY(room_filter_t, accesspath) \
    COUNT_PROPERTY(furniture_filter_t, layout) \
    BOOL_PROPERTY(has_owner, owner != -1) \
    BOOL_PROPERTY(has_squad, squad_id != -1) \
    BETWEEN_PROPERTY(int32_t, level, level) \
    FILTER_PROPERTY(room_filter_t, workshop) \
    BETWEEN_PROPERTY(size_t, users, users.size()) \
    DF_ENUM_SET_PROPERTY(df::stockpile_list, stock_disable) \
    BOOL_PROPERTY(stock_specific1, stock_specific1) \
    BOOL_PROPERTY(stock_specific2, stock_specific2) \
    BETWEEN_PROPERTY(size_t, has_users, has_users) \
    BOOL_PROPERTY(furnished, furnished) \
    BOOL_PROPERTY(queue_dig, queue_dig) \
    BOOL_PROPERTY(temporary, temporary) \
    BOOL_PROPERTY(outdoor, outdoor) \
    BOOL_PROPERTY(channeled, channeled)

#define FURNITURE_FILTER_PROPERTIES \
    AI_ENUM_PROPERTY(layout_type::type, type) \
    DF_ENUM_PROPERTY(df::construction_type, construction) \
    DF_ENUM_PROPERTY(df::tile_dig_designation, dig) \
    FILTER_PROPERTY(furniture_filter_t, target) \
    BETWEEN_PROPERTY(size_t, users, users.size()) \
    BETWEEN_PROPERTY(size_t, has_users, has_users) \
    BOOL_PROPERTY(ignore, ignore) \
    BOOL_PROPERTY(makeroom, makeroom) \
    BOOL_PROPERTY(internal, internal) \
    STRING_PROPERTY(comment)

#define AI_ENUM_PROPERTY(type, name) std::set<type> name, name##_not;
#define DF_ENUM_PROPERTY(type, name) std::set<type> name, name##_not;
#define AI_ENUM_SET_PROPERTY(type, name) std::set<type> name, name##_not;
#define DF_ENUM_SET_PROPERTY(type, name) std::set<type> name, name##_not;
#define STRING_PROPERTY(name) std::set<std::string> name, name##_not;
#define BOOL_PROPERTY(name, value) bool_filter_t name;
#define BETWEEN_PROPERTY(type, name, value) between_t<type> name;
#define COUNT_PROPERTY(filter, name) std::vector<count_t<filter>> name, name##_not;
#define FILTER_PROPERTY(filter, name) std::vector< filter > name, name##_not;

    struct furniture_filter_t
    {
        using object_type = furniture *;

        FURNITURE_FILTER_PROPERTIES

        bool is_match(const object_type & obj) const;

        bool apply(Json::Value & val, std::string & error);
        Json::Value to_json() const;
    };

    struct room_filter_t
    {
        using object_type = room *;

        ROOM_FILTER_PROPERTIES

        bool is_match(const object_type & obj) const;

        bool apply(Json::Value & val, std::string & error);
        Json::Value to_json() const;
    };

    bool keep_going;
    plan_priority_action::action action;

    FILTER_PROPERTY(room_filter_t, match)
    COUNT_PROPERTY(room_filter_t, count)

    bool act(AI & ai, color_ostream & out, std::ostream & reason) const;

    bool apply(Json::Value & val, std::string & error);
    Json::Value to_json() const;

private:
    static bool do_dig(AI & ai, color_ostream & out, room *r);
    static bool do_dig_immediate(AI & ai, color_ostream & out, room *r);
    static bool do_unignore_furniture(AI & ai, color_ostream & out, room *r);
    static bool do_finish(AI & ai, color_ostream & out, room *r);
    static bool do_start_ore_search(AI & ai, color_ostream & out);
    static bool do_past_initial_phase(AI & ai, color_ostream & out);
    static bool do_deconstruct_wagons(AI & ai, color_ostream & out);
    static bool do_dig_next_cavern_outpost(AI & ai, color_ostream & out);
};

#undef AI_ENUM_PROPERTY
#undef DF_ENUM_PROPERTY
#undef AI_ENUM_SET_PROPERTY
#undef DF_ENUM_SET_PROPERTY
#undef STRING_PROPERTY
#undef BOOL_PROPERTY
#undef BETWEEN_PROPERTY
#undef COUNT_PROPERTY
#undef FILTER_PROPERTY

Json::Value priorities_to_json(const std::vector<plan_priority_t> & vec);
bool priorities_from_json(std::vector<plan_priority_t> & vec, Json::Value & arr, std::string & error);
