#include "plan_priorities.h"
#include "ai.h"
#include "plan.h"

#include <functional>

#include "modules/Buildings.h"

#include "df/buildings_other_id.h"
#include "df/world.h"

REQUIRE_GLOBAL(world);

#define BEGIN_ENUM BEGIN_IMPLEMENT_ENUM
#define ENUM_ITEM IMPLEMENT_ENUM_ITEM
#define END_ENUM END_IMPLEMENT_ENUM
PLAN_PRIORITY_ENUMS
#undef BEGIN_ENUM
#undef ENUM_ITEM
#undef END_ENUM

#define AI_ENUM_PROPERTY(type, name) \
    if (name##_not.count(obj->name)) \
    { \
        return false; \
    } \
    if (!name.empty() && !name.count(obj->name)) \
    { \
        return false; \
    }
#define DF_ENUM_PROPERTY(type, name) \
    if (name##_not.count(obj->name)) \
    { \
        return false; \
    } \
    if (!name.empty() && !name.count(obj->name)) \
    { \
        return false; \
    }
#define AI_ENUM_SET_PROPERTY(type, name) \
    for (auto e : obj->name) \
    { \
        if (name##_not.count(e)) \
        { \
            return false; \
        } \
        if (!name.empty() && !name.count(e)) \
        { \
            return false; \
        } \
    }
#define DF_ENUM_SET_PROPERTY(type, name) \
    for (auto e : obj->name) \
    { \
        if (name##_not.count(e)) \
        { \
            return false; \
        } \
        if (!name.empty() && !name.count(e)) \
        { \
            return false; \
        } \
    }
#define STRING_PROPERTY(name) \
    if (name##_not.count(obj->name)) \
    { \
        return false; \
    } \
    if (!name.empty() && !name.count(obj->name)) \
    { \
        return false; \
    }
#define BOOL_PROPERTY(name, value) \
    if (!name.is_match(obj->value)) \
    { \
        return false; \
    }
#define BETWEEN_PROPERTY(type, name, value) \
    if (!name.is_match(obj->value)) \
    { \
        return false; \
    }
#define COUNT_PROPERTY(filter, name) \
    for (auto & f : name) \
    { \
        if (!f.is_match(obj->name)) \
        { \
            return false; \
        } \
    }
#define FILTER_PROPERTY(filter, name) \
{ \
    bool any_match = name.empty(); \
    for (auto & f : name) \
    { \
        if (f.is_match(obj->name)) \
        { \
            any_match = true; \
            break; \
        } \
    } \
    if (!any_match) \
    { \
        return false; \
    } \
    for (auto & f : name##_not) \
    { \
        if (f.is_match(obj->name)) \
        { \
            return false; \
        } \
    } \
}

bool plan_priority_t::room_filter_t::is_match(room * const & obj) const
{
    if (!obj)
    {
        return false;
    }

    ROOM_FILTER_PROPERTIES

    return true;
}

bool plan_priority_t::furniture_filter_t::is_match(furniture * const & obj) const
{
    if (!obj)
    {
        return false;
    }

    FURNITURE_FILTER_PROPERTIES

    return true;
}

#undef AI_ENUM_PROPERTY
#undef DF_ENUM_PROPERTY
#undef AI_ENUM_SET_PROPERTY
#undef DF_ENUM_SET_PROPERTY
#undef STRING_PROPERTY
#undef BOOL_PROPERTY
#undef BETWEEN_PROPERTY
#undef COUNT_PROPERTY
#undef FILTER_PROPERTY

#define STR2(x) #x
#define STR(x) STR2(x)

#define AI_ENUM_PROPERTY(type, name) \
    if (!obj.isMember(STR(name))) \
    { \
        name.clear(); \
    } \
    else if (obj[STR(name)].isArray()) \
    { \
        if (!apply_enum_set(name, obj, STR(name), error)) \
        { \
            return false; \
        } \
    } \
    else \
    { \
        type e; \
        if (!apply_enum(e, obj, STR(name), error)) \
        { \
            return false; \
        } \
        name.clear(); \
        name.insert(e); \
    } \
    if (!obj.isMember(STR(name) "_not")) \
    { \
        name##_not.clear(); \
    } \
    else if (obj[STR(name) "_not"].isArray()) \
    { \
        if (!apply_enum_set(name##_not, obj, STR(name) "_not", error)) \
        { \
            return false; \
        } \
    } \
    else \
    { \
        type e; \
        if (!apply_enum(e, obj, STR(name) "_not", error)) \
        { \
            return false; \
        } \
        name##_not.clear(); \
        name##_not.insert(e); \
    }
#define DF_ENUM_PROPERTY(type, name) \
    if (!obj.isMember(STR(name))) \
    { \
        name.clear(); \
    } \
    else if (obj[STR(name)].isArray()) \
    { \
        if (!apply_enum_set(name, obj, STR(name), error)) \
        { \
            return false; \
        } \
    } \
    else \
    { \
        type e; \
        if (!apply_enum(e, obj, STR(name), error)) \
        { \
            return false; \
        } \
        name.clear(); \
        name.insert(e); \
    } \
    if (!obj.isMember(STR(name) "_not")) \
    { \
        name##_not.clear(); \
    } \
    else if (obj[STR(name) "_not"].isArray()) \
    { \
        if (!apply_enum_set(name##_not, obj, STR(name) "_not", error)) \
        { \
            return false; \
        } \
    } \
    else \
    { \
        type e; \
        if (!apply_enum(e, obj, STR(name) "_not", error)) \
        { \
            return false; \
        } \
        name##_not.clear(); \
        name##_not.insert(e); \
    }
#define AI_ENUM_SET_PROPERTY(type, name) \
    if (!obj.isMember(STR(name))) \
    { \
        name.clear(); \
    } \
    else if (obj[STR(name)].isArray()) \
    { \
        if (!apply_enum_set(name, obj, STR(name), error)) \
        { \
            return false; \
        } \
    } \
    else \
    { \
        type e; \
        if (!apply_enum(e, obj, STR(name), error)) \
        { \
            return false; \
        } \
        name.clear(); \
        name.insert(e); \
    } \
    if (!obj.isMember(STR(name) "_not")) \
    { \
        name##_not.clear(); \
    } \
    else if (obj[STR(name) "_not"].isArray()) \
    { \
        if (!apply_enum_set(name##_not, obj, STR(name) "_not", error)) \
        { \
            return false; \
        } \
    } \
    else \
    { \
        type e; \
        if (!apply_enum(e, obj, STR(name) "_not", error)) \
        { \
            return false; \
        } \
        name##_not.clear(); \
        name##_not.insert(e); \
    }
#define DF_ENUM_SET_PROPERTY(type, name) \
    if (!obj.isMember(STR(name))) \
    { \
        name.clear(); \
    } \
    else if (obj[STR(name)].isArray()) \
    { \
        if (!apply_enum_set(name, obj, STR(name), error)) \
        { \
            return false; \
        } \
    } \
    else \
    { \
        type e; \
        if (!apply_enum(e, obj, STR(name), error)) \
        { \
            return false; \
        } \
        name.clear(); \
        name.insert(e); \
    } \
    if (!obj.isMember(STR(name) "_not")) \
    { \
        name##_not.clear(); \
    } \
    else if (obj[STR(name) "_not"].isArray()) \
    { \
        if (!apply_enum_set(name##_not, obj, STR(name) "_not", error)) \
        { \
            return false; \
        } \
    } \
    else \
    { \
        type e; \
        if (!apply_enum(e, obj, STR(name) "_not", error)) \
        { \
            return false; \
        } \
        name##_not.clear(); \
        name##_not.insert(e); \
    }
#define STRING_PROPERTY(name) \
    if (!obj.isMember(STR(name))) \
    { \
        name.clear(); \
    } \
    else \
    { \
        Json::Value val = obj[STR(name)]; \
        obj.removeMember(STR(name)); \
        if (val.isArray() && std::find_if(val.begin(), val.end(), [](Json::Value & v) -> bool { return !v.isString(); }) == val.end()) \
        { \
            name.clear(); \
            for (auto & v : val) \
            { \
                if (!name.insert(v.asString()).second) \
                { \
                    error = STR(name) " contains duplicate value " + v.asString(); \
                    return false; \
                } \
            } \
        } \
        else if (val.isString()) \
        { \
            name.clear(); \
            name.insert(val.asString()); \
        } \
        else \
        { \
            error = STR(name) " must be a string or an array of strings"; \
            return false; \
        } \
    } \
    if (!obj.isMember(STR(name) "_not")) \
    { \
        name##_not.clear(); \
    } \
    else \
    { \
        Json::Value val = obj[STR(name) "_not"]; \
        obj.removeMember(STR(name) "_not"); \
        if (val.isArray() && std::find_if(val.begin(), val.end(), [](Json::Value & v) -> bool { return !v.isString(); }) == val.end()) \
        { \
            name##_not.clear(); \
            for (auto & v : val) \
            { \
                if (!name##_not.insert(v.asString()).second) \
                { \
                    error = STR(name) "_not contains duplicate value " + v.asString(); \
                    return false; \
                } \
            } \
        } \
        else if (val.isString()) \
        { \
            name##_not.clear(); \
            name##_not.insert(val.asString()); \
        } \
        else \
        { \
            error = STR(name) "_not must be a string or an array of strings"; \
            return false; \
        } \
    }
#define BOOL_PROPERTY(name, value) \
    if (!obj.isMember(STR(name))) \
    { \
        name.has_value = false; \
    } \
    else \
    { \
        Json::Value val = obj[STR(name)]; \
        obj.removeMember(STR(name)); \
        if (!name.apply(val, error)) \
        { \
            return false; \
        } \
    }
#define BETWEEN_PROPERTY(type, name, value) \
    if (!obj.isMember(STR(name))) \
    { \
        name.has_min = false; \
        name.has_max = false; \
    } \
    else \
    { \
        Json::Value val = obj[STR(name)]; \
        obj.removeMember(STR(name)); \
        if (!name.apply(val, error)) \
        { \
            return false; \
        } \
    }
#define COUNT_PROPERTY(filter, name) \
    if (!apply_optional_vector(name, obj, STR(name), error)) \
    { \
        return false; \
    }
#define FILTER_PROPERTY(filter, name) \
    if (!apply_optional_vector(name, obj, STR(name), error) || !apply_optional_vector(name##_not, obj, STR(name) "_not", error)) \
    { \
        return false; \
    }

bool plan_priority_t::apply(Json::Value & obj, std::string & error)
{
    keep_going = false;
    if (obj.isMember("continue") && !apply_bool(keep_going, obj, "continue", error))
    {
        return false;
    }

    if (!obj.isMember("action"))
    {
        error = "missing required property: \"action\"";
        return false;
    }
    if (!apply_enum(action, obj, "action", error))
    {
        return false;
    }

    FILTER_PROPERTY(room_filter_t, match)
    COUNT_PROPERTY(room_filter_t, count)

    return apply_unhandled_properties(obj, "priority", error);
}

bool plan_priority_t::room_filter_t::apply(Json::Value & obj, std::string & error)
{
    ROOM_FILTER_PROPERTIES

    return apply_unhandled_properties(obj, "room filter", error);
}

bool plan_priority_t::furniture_filter_t::apply(Json::Value & obj, std::string & error)
{
    FURNITURE_FILTER_PROPERTIES

    return apply_unhandled_properties(obj, "layout filter", error);
}

#undef AI_ENUM_PROPERTY
#undef DF_ENUM_PROPERTY
#undef AI_ENUM_SET_PROPERTY
#undef DF_ENUM_SET_PROPERTY
#undef STRING_PROPERTY
#undef BOOL_PROPERTY
#undef BETWEEN_PROPERTY
#undef COUNT_PROPERTY
#undef FILTER_PROPERTY

template<typename elem_t, typename vec_t = std::set<elem_t>>
static void optional_vector_to_json(Json::Value & obj, const std::string & name, const vec_t & vec, std::function<Json::Value(const elem_t &)> convert)
{
    if (vec.empty())
    {
        return;
    }
    if (vec.size() == 1)
    {
        obj[name] = convert(*vec.begin());
        return;
    }
    Json::Value arr(Json::arrayValue);
    for (auto & e : vec)
    {
        arr.append(convert(e));
    }
}

#define AI_ENUM_PROPERTY(type, name) \
    optional_vector_to_json<type>(obj, STR(name), name, [](const type e) -> Json::Value { std::ostringstream str; str << e; return str.str(); }); \
    optional_vector_to_json<type>(obj, STR(name) "_not", name##_not, [](const type e) -> Json::Value { std::ostringstream str; str << e; return str.str(); });
#define DF_ENUM_PROPERTY(type, name) \
    optional_vector_to_json<type>(obj, STR(name), name, [](const type e) -> Json::Value { return enum_item_key(e); }); \
    optional_vector_to_json<type>(obj, STR(name) "_not", name##_not, [](const type e) -> Json::Value { return enum_item_key(e); });
#define AI_ENUM_SET_PROPERTY(type, name) \
    optional_vector_to_json<type>(obj, STR(name), name, [](const type e) -> Json::Value { std::ostringstream str; str << e; return str.str(); }); \
    optional_vector_to_json<type>(obj, STR(name) "_not", name##_not, [](const type e) -> Json::Value { std::ostringstream str; str << e; return str.str(); });
#define DF_ENUM_SET_PROPERTY(type, name) \
    optional_vector_to_json<type>(obj, STR(name), name, [](const type e) -> Json::Value { return enum_item_key(e); }); \
    optional_vector_to_json<type>(obj, STR(name) "_not", name##_not, [](const type e) -> Json::Value { return enum_item_key(e); });
#define STRING_PROPERTY(name) \
    optional_vector_to_json<std::string>(obj, STR(name), name, [](const std::string & s) -> Json::Value { return s; }); \
    optional_vector_to_json<std::string>(obj, STR(name) "_not", name##_not, [](const std::string & s) -> Json::Value { return s; });
#define BOOL_PROPERTY(name, value) \
    if (name.has_value) \
    { \
        obj[STR(name)] = name.to_json(); \
    }
#define BETWEEN_PROPERTY(type, name, value) \
    if (name.has_min || name.has_max) \
    { \
        obj[STR(name)] = name.to_json(); \
    }
#define COUNT_PROPERTY(filter, name) \
    optional_vector_to_json<count_t<filter>, std::vector<count_t<filter>>>(obj, STR(name), name, [](const count_t<filter> & c) -> Json::Value { return c.to_json(); });
#define FILTER_PROPERTY(filter, name) \
    optional_vector_to_json<filter, std::vector<filter>>(obj, STR(name), name, [](const filter & f) -> Json::Value { return f.to_json(); }); \
    optional_vector_to_json<filter, std::vector<filter>>(obj, STR(name) "_not", name##_not, [](const filter & f) -> Json::Value { return f.to_json(); });

Json::Value plan_priority_t::to_json() const
{
    Json::Value obj(Json::objectValue);

    if (keep_going)
    {
        obj["continue"] = true;
    }

    std::ostringstream str;
    str << action;
    obj["action"] = str.str();

    FILTER_PROPERTY(room_filter_t, match)
    COUNT_PROPERTY(room_filter_t, count)

    return obj;
}

Json::Value plan_priority_t::room_filter_t::to_json() const
{
    Json::Value obj(Json::objectValue);

    ROOM_FILTER_PROPERTIES

    return obj;
}

Json::Value plan_priority_t::furniture_filter_t::to_json() const
{
    Json::Value obj(Json::objectValue);

    FURNITURE_FILTER_PROPERTIES

    return obj;
}

#undef AI_ENUM_PROPERTY
#undef DF_ENUM_PROPERTY
#undef AI_ENUM_SET_PROPERTY
#undef DF_ENUM_SET_PROPERTY
#undef STRING_PROPERTY
#undef BOOL_PROPERTY
#undef BETWEEN_PROPERTY
#undef COUNT_PROPERTY
#undef FILTER_PROPERTY

bool plan_priority_t::act(AI & ai, color_ostream & out, std::ostream & reason) const
{
    auto check_count = [this, &ai]() -> bool
    {
        for (auto & c : count)
        {
            if (!c.is_match(ai.plan.rooms_and_corridors))
            {
                return false;
            }
        }
        return true;
    };

    if (!check_count())
    {
        return false;
    }

    switch (action)
    {
        case plan_priority_action::dig:
        case plan_priority_action::dig_immediate:
        case plan_priority_action::unignore_furniture:
        case plan_priority_action::finish:
            for (room *r : ai.plan.rooms_and_corridors)
            {
                bool any_match = false;
                for (auto & f : match_not)
                {
                    if (f.is_match(r))
                    {
                        any_match = true;
                    }
                }
                if (any_match)
                {
                    continue;
                }

                for (auto & f : match)
                {
                    if (f.is_match(r))
                    {
                        any_match = true;
                        break;
                    }
                }
                if (!any_match)
                {
                    continue;
                }

                switch (action)
                {
                    case plan_priority_action::dig:
                        if (do_dig(ai, out, r))
                        {
                            reason << "want dig: " << AI::describe_room(r);
                            if (!keep_going || !check_count())
                            {
                                return true;
                            }
                            reason << "; ";
                        }
                        break;
                    case plan_priority_action::dig_immediate:
                        if (do_dig_immediate(ai, out, r))
                        {
                            reason << "dig room: " << AI::describe_room(r);
                            if (!keep_going || !check_count())
                            {
                                return true;
                            }
                            reason << "; ";
                        }
                        break;
                    case plan_priority_action::unignore_furniture:
                        if (do_unignore_furniture(ai, out, r))
                        {
                            reason << "furnishing: " << AI::describe_room(r);
                            if (!keep_going || !check_count())
                            {
                                return true;
                            }
                            reason << "; ";
                        }
                        break;
                    case plan_priority_action::finish:
                        if (do_finish(ai, out, r))
                        {
                            reason << "finishing: " << AI::describe_room(r);
                            if (!keep_going || !check_count())
                            {
                                return true;
                            }
                            reason << "; ";
                        }
                        break;
                    case plan_priority_action::start_ore_search:
                    case plan_priority_action::past_initial_phase:
                    case plan_priority_action::deconstruct_wagons:
                    case plan_priority_action::dig_next_cavern_outpost:
                    case plan_priority_action::_plan_priority_action_count:
                        break;
                }
            }
            break;
        case plan_priority_action::start_ore_search:
            if (do_start_ore_search(ai, out))
            {
                reason << "starting search for ore; ";
                return !keep_going;
            }
            break;
        case plan_priority_action::past_initial_phase:
            if (do_past_initial_phase(ai, out))
            {
                reason << "past initial phase; ";
                return !keep_going;
            }
            break;
        case plan_priority_action::deconstruct_wagons:
            if (do_deconstruct_wagons(ai, out))
            {
                reason << "deconstructing wagons; ";
                return !keep_going;
            }
            break;
        case plan_priority_action::dig_next_cavern_outpost:
            if (do_dig_next_cavern_outpost(ai, out))
            {
                reason << "digging next cavern outpost; ";
                return !keep_going;
            }
            break;
        case plan_priority_action::_plan_priority_action_count:
            break;
    }
    return false;
}

bool plan_priority_t::do_dig(AI & ai, color_ostream & out, room *r)
{
    return ai.plan.wantdig(out, r, r->outdoor ? 1 : 0);
}

bool plan_priority_t::do_dig_immediate(AI & ai, color_ostream & out, room *r)
{
    return ai.plan.digroom(out, r, true);
}

bool plan_priority_t::do_unignore_furniture(AI & ai, color_ostream & out, room *r)
{
    bool any = false;
    for (furniture *f : r->layout)
    {
        if (f->ignore)
        {
            any = true;
            f->ignore = false;
        }
    }

    if (any && (r->status == room_status::dug || r->status == room_status::finished))
    {
        ai.plan.furnish_room(out, r);
    }

    return any;
}

bool plan_priority_t::do_finish(AI & ai, color_ostream & out, room *r)
{
    if (r->status != room_status::finished)
    {
        return false;
    }

    if (r->furnished)
    {
        return false;
    }

    r->furnished = true;

    for (furniture *f : r->layout)
    {
        f->ignore = false;
    }

    ai.plan.furnish_room(out, r);
    ai.plan.smooth_room(out, r);

    return true;
}

bool plan_priority_t::do_start_ore_search(AI & ai, color_ostream &)
{
    if (ai.plan.should_search_for_metal)
    {
        return false;
    }
    ai.plan.should_search_for_metal = true;
    return true;
}

bool plan_priority_t::do_past_initial_phase(AI & ai, color_ostream &)
{
    if (ai.plan.past_initial_phase)
    {
        return false;
    }
    ai.plan.past_initial_phase = true;
    return true;
}

bool plan_priority_t::do_deconstruct_wagons(AI & ai, color_ostream &)
{
    if (ai.plan.deconstructed_wagons)
    {
        return false;
    }
    for (auto wagon : world->buildings.other[buildings_other_id::WAGON])
    {
        Buildings::deconstruct(wagon);
    }
    ai.plan.deconstructed_wagons = true;
    return true;
}

bool plan_priority_t::do_dig_next_cavern_outpost(AI & ai, color_ostream & out)
{
   bool any_outpost = false;
   for (auto & t : ai.plan.tasks_generic)
   {
       if (t->type != task_type::want_dig && t->type != task_type::dig_room && t->type != task_type::dig_room_immediate)
       {
           continue;
       }
       if (t->r->type == room_type::outpost || (t->r->type == room_type::corridor && t->r->corridor_type == corridor_type::outpost))
       {
           any_outpost = true;
           break;
       }
   }
   if (!any_outpost && ai.plan.setup_blueprint_caverns(out) == CR_OK)
   {
       ai.debug(out, "found next cavern");
       ai.plan.categorize_all();
       return true;
   }
   return false;
}

Json::Value priorities_to_json(const std::vector<plan_priority_t> & vec)
{
    Json::Value arr(Json::arrayValue);
    for (auto & p : vec)
    {
        arr.append(p.to_json());
    }
    return arr;
}

bool priorities_from_json(std::vector<plan_priority_t> & vec, Json::Value & arr, std::string & error)
{
    if (!arr.isArray())
    {
        error = "priorities must be an array";
        return false;
    }

    vec.clear();
    vec.resize(size_t(arr.size()));
    for (Json::ArrayIndex i = 0; i < arr.size(); i++)
    {
        if (!vec.at(size_t(i)).apply(arr[i], error))
        {
            error += stl_sprintf(" (at priorities index %d)", i);
            return false;
        }
    }
    return true;
}
