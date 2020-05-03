#pragma once

#include "dfhack_shared.h"

#include <boost/coroutine2/coroutine.hpp>
#include <type_traits>

#include "Error.h"
#include "modules/Gui.h"

#include "df/interface_key.h"
#include "df/viewscreen.h"

#if WIN32
// TODO: actual filename/line number of caller
#define FL const char *filename = __FILE__, int lineno = __LINE__
#else
#define FL const char *filename = __builtin_FILE(), int lineno = __builtin_LINE()
#endif

template<typename T>
class viewscreen_relative_ptr
{
    T *current;
    bool in_viewscreen;

public:
    viewscreen_relative_ptr(T *ptr)
    {
        in_viewscreen = false;
        current = ptr;

        auto cur_viewscreen = Gui::getCurViewscreen(true);
        if (uintptr_t(ptr) - uintptr_t(cur_viewscreen) < virtual_identity::get(cur_viewscreen)->byte_size())
        {
            in_viewscreen = true;
            current = reinterpret_cast<T *>(uintptr_t(ptr) - uintptr_t(cur_viewscreen));
        }
    }

    T *get() const
    {
        if (in_viewscreen)
        {
            return reinterpret_cast<T *>(uintptr_t(current) + uintptr_t(Gui::getCurViewscreen(true)));
        }

        return current;
    }

    T & operator*() const
    {
        return *get();
    }

    T *operator->() const
    {
        return get();
    }
};

class ExclusiveCallback
{
public:
    ExclusiveCallback(const std::string & description, size_t wait_multiplier = 1);
    virtual ~ExclusiveCallback();

protected:
    template<typename T>
    class ExpectedScreen
    {
        ExclusiveCallback & cb;

    public:
        ExpectedScreen(ExclusiveCallback *cb) : cb(*cb) {}

        inline T *get(FL) const
        {
            return cb.getScreen<T>(filename, lineno);
        }
        inline T *operator->() const
        {
            return get();
        }
    };

    template<typename T>
    typename std::enable_if<std::is_base_of<df::viewscreen, T>::value>::type ExpectScreen(const std::string & focus, const std::string & parentFocus = std::string(), FL)
    {
        expectedScreen = &T::_identity;
        expectedFocus = focus;
        expectedParentFocus = parentFocus;

        checkScreen(filename, lineno);
    }
    template<typename T>
    typename std::enable_if<std::is_base_of<df::viewscreen, T>::value, bool>::type MaybeExpectScreen(const std::string & focus, const std::string & parentFocus = std::string(), FL)
    {
        T *screen = strict_virtual_cast<T>(Gui::getCurViewscreen(true));
        if (!screen)
        {
            return false;
        }

        if (!focus.empty() && Gui::getFocusString(screen) != focus)
        {
            return false;
        }

        if (!parentFocus.empty() && Gui::getFocusString(screen->parent) != parentFocus)
        {
            return false;
        }

        ExpectScreen<T>(focus, parentFocus, filename, lineno);

        return true;
    }
    void KeyNoDelay(df::interface_key key);
    void Key(df::interface_key key, const char *filename, int lineno);
    void Char(char c, const char *filename, int lineno);
    void Delay(size_t frames = 1);
    void AssertDelayed();

    inline void MoveToItem(const int32_t *cur, int32_t target, df::interface_key inc = interface_key::STANDARDSCROLL_DOWN, df::interface_key dec = interface_key::STANDARDSCROLL_UP, FL)
    {
        viewscreen_relative_ptr<const int32_t> current(cur);

        while (*current != target)
        {
            Key(*current < target ? inc : dec, filename, lineno);
        }
    }
    inline void MoveToItem(const std::function<int32_t()> & current, int32_t target, df::interface_key inc = interface_key::STANDARDSCROLL_DOWN, df::interface_key dec = interface_key::STANDARDSCROLL_UP, FL)
    {
        while (current() != target)
        {
            Key(current() < target ? inc : dec, filename, lineno);
        }
    }
    template<size_t N>
    inline void EnterString(const char (*cur)[N], const std::string & target, FL)
    {
        viewscreen_relative_ptr<const char[N]> current(cur);

        size_t len = strnlen(*cur, N);
        while (len > target.size() || *cur != target.substr(0, len))
        {
            Key(interface_key::STRING_A000, filename, lineno); // backspace
            len--;
        }

        while (len < target.size())
        {
            Char(target.at(len), filename, lineno);
            len++;
        }
    }
    inline void EnterString(std::string *cur, const std::string & target, const char *filename, int lineno)
    {
        viewscreen_relative_ptr<std::string> current(cur);

        while (current->size() > target.size() || *current != target.substr(0, current->size()))
        {
            Key(interface_key::STRING_A000, filename, lineno); // backspace
        }

        while (current->size() < target.size())
        {
            Char(target.at(current->size()), filename, lineno);
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

    template<typename T>
    friend class ExpectedScreen;
    template<typename T>
    T *getScreen(const char *filename, int lineno)
    {
        CHECK_INVALID_ARGUMENT(&T::_identity == expectedScreen);

        checkScreen(filename, lineno);

        return strict_virtual_cast<T>(Gui::getCurViewscreen(true));
    }

    using coroutine_t = boost::coroutines2::coroutine<color_ostream *>;
    coroutine_t::pull_type *pull;
    coroutine_t::push_type push;
    size_t wait_multiplier;
    size_t wait_frames;
    bool did_delay;
    std::vector<df::interface_key> feed_keys;
    virtual_identity *expectedScreen;
    std::string expectedFocus;
    std::string expectedParentFocus;

    void checkScreen(const char *filename, int lineno);
    bool run(color_ostream & out, const std::function<void(std::vector<df::interface_key> &)> & send_keys);
    void init(coroutine_t::pull_type & input);

    friend struct EventManager;

public:
    const std::string description;
};
#undef FL

#define Key(key) Key((key), __FILE__, __LINE__)
#define Char(c) Char((c), __FILE__, __LINE__)
#define EnterString(cur, tar) EnterString((cur), (tar), __FILE__, __LINE__)
