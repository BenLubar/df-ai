#include "variable_string.h"

#include "json/json.h"

bool apply_variable_string(variable_string & var, Json::Value & data, const std::string & name, std::string & error, bool append)
{
    Json::Value value = data[name];
    data.removeMember(name);
    try
    {
        variable_string str(value);

        if (append)
        {
            var.contents.insert(var.contents.end(), str.contents.begin(), str.contents.end());
        }
        else
        {
            var.contents = str.contents;
        }

        return true;
    }
    catch (std::invalid_argument & ex)
    {
        error = name + " is invalid (" + ex.what() + ")";

        return false;
    }
}

variable_string::element_t::element_t(const std::string & text) :
    element_t(text.empty() || text.at(0) != '$' ? text : text.substr(1), !text.empty() && text.at(0) == '$')
{
}

variable_string::element_t::element_t(const std::string & text, bool variable) :
    text(text),
    variable(variable)
{
}

variable_string::context_t::context_t(const context_t & parent, const std::map<std::string, variable_string> & values) :
    variables(parent.variables)
{
    for (auto & v : values)
    {
        variables[v.first] = v.second(parent);
    }
}

std::string variable_string::context_t::operator[](const std::string & name) const
{
    auto it = variables.find(name);
    return it == variables.end() ? "$" + name : it->second;
}

variable_string::variable_string(const std::string & text)
{
    contents.push_back(element_t(text, false));
}

variable_string::variable_string(const Json::Value & value)
{
    if (value.isString())
    {
        contents.push_back(element_t(value.asString(), false));
    }
    else if (value.isArray())
    {
        contents.reserve(value.size());
        for (auto & el : value)
        {
            if (el.isString())
            {
                contents.push_back(element_t(el.asString()));
            }
            else
            {
                throw std::invalid_argument("elements of array should be strings");
            }
        }
    }
    else
    {
        throw std::invalid_argument("should be string or array of strings");
    }
}

std::string variable_string::operator()(const variable_string::context_t & ctx) const
{
    std::ostringstream str;
    for (auto v : contents)
    {
        str << (v.variable ? ctx[v.text] : v.text);
    }
    return str.str();
}
