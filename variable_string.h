#pragma once

#include <string>
#include <map>
#include <vector>

namespace Json
{
    class Value;
}

struct variable_string
{
    struct element_t
    {
        std::string text;
        bool variable;

        explicit element_t(const std::string & text);
        explicit element_t(const std::string & text, bool variable);
    };

    struct context_t
    {
        std::map<std::string, std::string> variables;

        context_t() = default;
        context_t(const context_t &) = default;
        context_t(const context_t &, const std::map<std::string, variable_string> &);

        template<typename K>
        static inline std::map<K, context_t> map(const context_t & parent, const std::map<K, std::map<std::string, variable_string>> & contexts)
        {
            std::map<K, context_t> out;
            for (auto ctx : contexts)
            {
                out[ctx.first] = context_t(parent, ctx.second);
            }
            return out;
        }

    private:
        friend struct variable_string;
        std::string operator[](const std::string &) const;
    };

    std::vector<element_t> contents;
    variable_string() = default;
    explicit variable_string(const std::string & text);
    explicit variable_string(const Json::Value & value);
    std::string operator()(const context_t &) const;
};

bool apply_variable_string(variable_string & var, Json::Value & data, const std::string & name, std::string & error, bool append = false);
