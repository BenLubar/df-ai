#pragma once

#include "dfhack_shared.h"

#include "df/interface_key.h"

#include <boost/coroutine2/coroutine.hpp>

class ExclusiveCallback
{
protected:
    ExclusiveCallback(const std::string & description, size_t wait_multiplier = 1);
    virtual ~ExclusiveCallback();

    void Key(df::interface_key key);
    void Char(char c);
    void Delay(size_t frames = 1);

    inline void MoveToItem(const int32_t *current, int32_t target, df::interface_key inc = interface_key::STANDARDSCROLL_DOWN, df::interface_key dec = interface_key::STANDARDSCROLL_UP)
    {
        while (*current != target)
        {
            Key(*current < target ? inc : dec);
        }
    }
    template<size_t N>
    inline void EnterString(const char (*current)[N], const std::string & target)
    {
        size_t len = strnlen(*current, N);
        while (len > target.size() || *current != target.substr(0, len))
        {
            Key(interface_key::STRING_A000); // backspace
            len--;
        }

        while (len < target.size())
        {
            Char(target.at(len));
            len++;
        }
    }
    inline void EnterString(std::string *current, const std::string & target)
    {
        while (current->size() > target.size() || *current != target.substr(0, current->size()))
        {
            Key(interface_key::STRING_A000); // backspace
        }

        while (current->size() < target.size())
        {
            Char(target.at(current->size()));
        }
    }

    virtual bool SuppressStateChange(color_ostream &, state_change_event event) { return event == SC_VIEWSCREEN_CHANGED; }
    virtual ExclusiveCallback *ReplaceOnScreenChange() { return nullptr; }
    virtual void Run(color_ostream & out) = 0;

private:
    class ostream_proxy : public color_ostream_proxy
    {
    private:
        explicit ostream_proxy() : color_ostream_proxy(Core::getInstance().getConsole())
        {
            target = nullptr;
        }
        void set(color_ostream & out) { target = &out; }
        void clear() { *this << std::flush; target = nullptr; }
        friend class ExclusiveCallback;
    } out_proxy;

    using coroutine_t = boost::coroutines2::coroutine<color_ostream &>;
    coroutine_t::pull_type *pull;
    coroutine_t::push_type push;
    size_t wait_multiplier;
    size_t wait_frames;

    bool run(color_ostream & out);
    void init(coroutine_t::pull_type & input);

    friend struct EventManager;

public:
    const std::string description;
};
