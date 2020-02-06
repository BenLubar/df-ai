#include "ai.h"
#include "blueprint.h"
#include "debug.h"

bool blueprint_plan_template::apply(Json::Value data, std::string & error)
{
    if (data.isMember("max_retries"))
    {
        Json::Value value = data["max_retries"];
        data.removeMember("max_retries");
        if (!value.isIntegral())
        {
            error = "max_retries has wrong type (should be integer)";
            return false;
        }

        if (value.asInt() <= 0)
        {
            error = "max_retries must be positive";
            return false;
        }

        max_retries = size_t(value.asInt());
    }

    if (data.isMember("max_failures"))
    {
        Json::Value value = data["max_failures"];
        data.removeMember("max_failures");
        if (!value.isIntegral())
        {
            error = "max_failures has wrong type (should be integer)";
            return false;
        }

        if (value.asInt() <= 0)
        {
            error = "max_failures must be positive";
            return false;
        }

        max_failures = size_t(value.asInt());
    }

    if (data.isMember("start"))
    {
        Json::Value value = data["start"];
        data.removeMember("start");
        if (!value.isString())
        {
            error = "start has wrong type (should be string)";
            return false;
        }

        start = value.asString();
    }
    else
    {
        error = "missing start";
        return false;
    }

    if (data.isMember("outdoor"))
    {
        Json::Value value = data["outdoor"];
        data.removeMember("outdoor");
        if (!value.isArray() || std::find_if(value.begin(), value.end(), [](Json::Value & v) -> bool { return !v.isString(); }) != value.end())
        {
            error = "outdoor has wrong type (should be array of strings)";
            return false;
        }

        for (auto & v : value)
        {
            if (!outdoor.insert(v.asString()).second)
            {
                error = "duplicate outdoor tag: " + v.asString();
                return false;
            }
        }
    }

    if (data.isMember("tags"))
    {
        Json::Value value = data["tags"];
        data.removeMember("tags");
        if (!value.isObject())
        {
            error = "tags has wrong type (should be object)";
            return false;
        }

        std::vector<std::string> tag_names{ std::move(value.getMemberNames()) };
        for (auto & tag_name : tag_names)
        {
            Json::Value & tag = value[tag_name];
            if (!tag.isArray() || std::find_if(tag.begin(), tag.end(), [](Json::Value & v) -> bool { return !v.isString(); }) != tag.end())
            {
                error = "tag " + tag_name + " has wrong type (should be array of strings)";
                return false;
            }
            auto & tag_set = tags[tag_name];
            for (auto & v : tag)
            {
                if (!tag_set.insert(v.asString()).second)
                {
                    error = "duplicate tag: " + tag_name + " -> " + v.asString();
                    return false;
                }
            }
        }
    }

    if (data.isMember("limits"))
    {
        Json::Value value = data["limits"];
        data.removeMember("limits");
        if (!value.isObject())
        {
            error = "limits has wrong type (should be object)";
            return false;
        }

        auto limit_names = value.getMemberNames();
        for (auto & limit_name : limit_names)
        {
            Json::Value & limit = value[limit_name];
            if (!limit.isArray() || limit.size() != 2 || !limit[0].isIntegral() || !limit[1].isIntegral())
            {
                error = "limit " + limit_name + " has wrong type (should be [integer, integer])";
                return false;
            }

            if (limit[0].asInt() < 0)
            {
                error = "limit " + limit_name + " has invalid minimum";
                return false;
            }

            if (limit[1].asInt() < limit[0].asInt())
            {
                error = "limit " + limit_name + " has invalid maximum";
                return false;
            }

            limits[limit_name] = std::make_pair(size_t(limit[0].asInt()), size_t(limit[1].asInt()));
        }
    }

    if (data.isMember("instance_limits"))
    {
        Json::Value value = data["instance_limits"];
        data.removeMember("instance_limits");
        if (!value.isObject())
        {
            error = "instance_limits has wrong type (should be object)";
            return false;
        }

        std::vector<std::string> type_names{ std::move(value.getMemberNames()) };
        for (auto & type_name : type_names)
        {
            Json::Value & type = value[type_name];
            if (!type.isObject())
            {
                error = "instance_limits " + type_name + " has wrong type (should be object)";
                return false;
            }

            auto & limits = instance_limits[type_name];
            std::vector<std::string> instance_names{ std::move(type.getMemberNames()) };
            for (auto & instance_name : instance_names)
            {
                Json::Value & limit = type[instance_name];
                if (!limit.isArray() || limit.size() != 2 || !limit[0].isIntegral() || !limit[1].isIntegral())
                {
                    error = "instance_limit " + type_name + " " + instance_name + " has wrong type (should be [integer, integer])";
                    return false;
                }

                if (limit[0].asInt() < 0)
                {
                    error = "instance_limit " + type_name + " " + instance_name + " has invalid minimum";
                    return false;
                }

                if (limit[1].asInt() < limit[0].asInt())
                {
                    error = "instance_limit " + type_name + " " + instance_name + " has invalid maximum";
                    return false;
                }

                limits[instance_name] = std::make_pair(size_t(limit[0].asInt()), size_t(limit[1].asInt()));
            }
        }
    }

    if (data.isMember("variables"))
    {
        Json::Value vars = data["vaiables"];
        data.removeMember("variables");
        if (!vars.isObject())
        {
            error = "variables has wrong type (should be object)";
            return false;
        }

        auto var_names = vars.getMemberNames();
        for (auto & var_name : var_names)
        {
            Json::Value & var_value = vars[var_name];
            if (!var_value.isString())
            {
                error = "variable " + var_name + " has wrong type (should be string)";
                return false;
            }

            context.variables[var_name] = var_value.asString();
        }
    }

    if (data.isMember("padding_x"))
    {
        Json::Value value = data["padding_x"];
        data.removeMember("padding_x");
        if (!value.isArray() || value.size() != 2 || !value[0].isIntegral() || !value[1].isIntegral())
        {
            error = "padding_x has wrong type (should be [integer, integer])";
            return false;
        }

        if (value[0].asInt() > 0)
        {
            error = "padding_x[0] must not be positive";
            return false;
        }

        if (value[1].asInt() < 0)
        {
            error = "padding_x[1] must not be negative";
            return false;
        }

        padding_x = std::make_pair((int16_t)value[0].asInt(), (int16_t)value[1].asInt());
    }

    if (data.isMember("padding_y"))
    {
        Json::Value value = data["padding_y"];
        data.removeMember("padding_y");
        if (!value.isArray() || value.size() != 2 || !value[0].isIntegral() || !value[1].isIntegral())
        {
            error = "padding_y has wrong type (should be [integer, integer])";
            return false;
        }

        if (value[0].asInt() > 0)
        {
            error = "padding_y[0] must not be positive";
            return false;
        }

        if (value[1].asInt() < 0)
        {
            error = "padding_y[1] must not be negative";
            return false;
        }

        padding_y = std::make_pair((int16_t)value[0].asInt(), (int16_t)value[1].asInt());
    }

    if (data.isMember("priorities"))
    {
        Json::Value p = data["priorities"];
        data.removeMember("priorities");
        if (!priorities_from_json(priorities, p, error))
        {
            return false;
        }
    }

    return apply_unhandled_properties(data, "", error);
}

bool blueprint_plan_template::have_minimum_requirements(const std::map<std::string, size_t> & counts, const std::map<std::string, std::map<std::string, size_t>> & instance_counts) const
{
    bool ok = true;

    for (auto & limit : limits)
    {
        auto type = counts.find(limit.first);
        if (type == counts.end())
        {
            if (limit.second.first > 0)
            {
                DFAI_DEBUG(blueprint, 2, "Requirement not met: have 0 " << limit.first << " but want between " << limit.second.first << " and " << limit.second.second << ".");
                ok = false;
            }
            else
            {
                DFAI_DEBUG(blueprint, 2, "have 0 " << limit.first << " (want between " << limit.second.first << " and " << limit.second.second << ")");
            }
        }
        else
        {
            if (limit.second.first > type->second)
            {
                DFAI_DEBUG(blueprint, 2, "Requirement not met: have " << type->second << " " << limit.first << " but want between " << limit.second.first << " and " << limit.second.second << ".");
                ok = false;
            }
            else
            {
                DFAI_DEBUG(blueprint, 2, "have " << type->second << " " << limit.first << " (want between " << limit.second.first << " and " << limit.second.second << ")");
            }
        }
    }

    for (auto & type_limits : instance_limits)
    {
        auto type_counts = instance_counts.find(type_limits.first);
        if (type_counts == instance_counts.end())
        {
            for (auto & limit : type_limits.second)
            {
                if (limit.second.first > 0)
                {
                    DFAI_DEBUG(blueprint, 2, "Requirement not met: have 0 " << type_limits.first << "/" << limit.first << " but want between " << limit.second.first << " and " << limit.second.second << ".");
                    ok = false;
                }
                else
                {
                    DFAI_DEBUG(blueprint, 2, "have 0 " << type_limits.first << "/" << limit.first << " (want between " << limit.second.first << " and " << limit.second.second << ")");
                }
            }
        }
        else
        {
            for (auto & limit : type_limits.second)
            {
                auto count = type_counts->second.find(limit.first);
                if (count == type_counts->second.end())
                {
                    if (limit.second.first > 0)
                    {
                        DFAI_DEBUG(blueprint, 2, "Requirement not met: have 0 " << type_limits.first << "/" << limit.first << " but want between " << limit.second.first << " and " << limit.second.second << ".");
                        ok = false;
                    }
                    else
                    {
                        DFAI_DEBUG(blueprint, 2, "have 0 " << type_limits.first << "/" << limit.first << " (want between " << limit.second.first << " and " << limit.second.second << ")");
                    }
                }
                else
                {
                    if (limit.second.first > count->second)
                    {
                        DFAI_DEBUG(blueprint, 2, "Requirement not met: have " << count->second << " " << type_limits.first << "/" << limit.first << " but want between " << limit.second.first << " and " << limit.second.second << ".");
                        ok = false;
                    }
                    else
                    {
                        DFAI_DEBUG(blueprint, 2, "have " << count->second << " " << type_limits.first << "/" << limit.first << " (want between " << limit.second.first << " and " << limit.second.second << ")");
                    }
                }
            }
        }
    }

    return ok;
}
