#pragma once

#include "dfhack_shared.h"
#include "jsoncpp.h"

#include <algorithm>
#include <set>
#include <sstream>

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
static bool apply_int(int_t & var, Json::Value & data, const std::string & name, std::string & error, int_t min_value = std::numeric_limits<int_t>::min(), int_t max_value = std::numeric_limits<int_t>::max())
{
    Json::Value value = data.removeMember(name);
    if (!value.isIntegral())
    {
        error = name + " has wrong type (should be integer)";
        return false;
    }

    if (std::is_unsigned<int_t>::value)
    {
        if (value.asLargestUInt() < Json::LargestUInt(min_value))
        {
            error = name + " is too small!";
            return false;
        }

        if (value.asLargestUInt() > Json::LargestUInt(max_value))
        {
            error = name + " is too big!";
            return false;
        }

        var = int_t(value.asLargestUInt());
    }
    else
    {
        if (value.asLargestInt() < Json::LargestInt(min_value))
        {
            error = name + " is too small!";
            return false;
        }

        if (value.asLargestInt() > Json::LargestInt(max_value))
        {
            error = name + " is too big!";
            return false;
        }

        var = int_t(value.asLargestInt());
    }

    return true;
}

static inline bool apply_bool(bool & var, Json::Value & data, const std::string & name, std::string & error)
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

template<typename element_t>
static bool apply_optional_vector(std::vector<element_t> & vec, Json::Value & data, const std::string & name, std::string & error)
{
    vec.clear();
    if (!data.isMember(name))
    {
        return true;
    }

    Json::Value value = data.removeMember(name);
    if (!value.isArray())
    {
        vec.resize(1);
        if (!vec.at(0).apply(value, error))
        {
            error += " (" + name + ")";
            return false;
        }
        return true;
    }

    vec.resize(size_t(value.size()));
    for (Json::ArrayIndex i = 0; i < value.size(); i++)
    {
        if (!vec.at(size_t(i)).apply(value[i], error))
        {
            error += stl_sprintf(" (%s index %d)", name.c_str(), i);
            return false;
        }
    }
    return true;
}

static inline bool apply_unhandled_properties(Json::Value & data, const std::string & name, std::string & error)
{
    std::vector<std::string> remaining_members(data.getMemberNames());
    if (remaining_members.empty())
    {
        return true;
    }

    error = "";
    std::string before = "unhandled " + name + " properties: ";
    for (auto & m : remaining_members)
    {
        error += before;
        error += m;
        before = ", ";
    }

    return false;
}

template<typename T>
static bool df_ai_find_enum_item(T *var, const std::string & name, T count)
{
    std::ostringstream scratch;
    for (T i = T(); i < count; i = (T)(i + 1))
    {
        scratch.str(std::string());
        scratch << i;
        if (scratch.str() == name)
        {
            *var = i;
            return true;
        }
    }
    return false;
}

#define BEGIN_DECLARE_ENUM(prefix, name) \
    namespace prefix ## _ ## name \
    { \
        enum name \
        {
#define DECLARE_ENUM_ITEM(item) \
            item ,
#define END_DECLARE_ENUM(prefix, name) \
            _ ## prefix ## _ ## name ## _count \
        }; \
    } \
    std::ostream & operator <<(std::ostream & stream, prefix ## _ ## name :: name name ); \
    namespace DFHack \
    { \
        template<> inline bool find_enum_item< prefix ## _ ## name :: name >( prefix ## _ ## name :: name *var, const std::string & name) { return df_ai_find_enum_item(var, name, prefix ## _ ## name :: _ ## prefix ## _ ## name ## _count); } \
    }

#define BEGIN_IMPLEMENT_ENUM(prefix, name) \
    std::ostream & operator <<(std::ostream & stream, prefix ## _ ## name :: name name ) \
    { \
        using _enum = prefix ## _ ## name :: name ; \
        switch ( name ) \
        {
#define IMPLEMENT_ENUM_ITEM(item) \
            case _enum:: item : \
                return stream << #item ;
#define END_IMPLEMENT_ENUM(prefix, name) \
            case _enum::_ ## prefix ## _ ## name ## _count: \
                return stream << "???"; \
        } \
        return stream << "???"; \
    }
