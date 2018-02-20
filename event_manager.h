#pragma once

#include "dfhack_shared.h"

#include <functional>

#include "df/interface_key.h"

struct OnupdateCallback
{
    std::function<bool(color_ostream &)> callback;
    int32_t ticklimit;
    int32_t minyear;
    int32_t minyeartick;
    std::string description;
    bool hasTickLimit;

    OnupdateCallback(const std::string & descr, std::function<bool(color_ostream &)> cb);
    OnupdateCallback(const std::string & descr, std::function<bool(color_ostream &)> cb, int32_t tl, int32_t initdelay = 0);

    bool check_run(color_ostream & out, int32_t year, int32_t yeartick);
};

struct OnstatechangeCallback
{
    std::function<bool(color_ostream &, state_change_event)> cb;
    std::string description;

    OnstatechangeCallback(const std::string & descr, std::function<bool(color_ostream &, state_change_event)> cb);
};

class ExclusiveCallback
{
protected:
    ExclusiveCallback(const std::string & description, size_t wait_multiplier = 1);
    virtual ~ExclusiveCallback();

    void Do(std::function<void()> step);
    void If(std::function<bool()> cond, std::function<void()> step_true, std::function<void()> step_false = []() {});
    void While(std::function<bool()> cond, std::function<void()> step);
    void Key(df::interface_key key);
    void Char(std::function<char()> ch);
    void Delay(size_t frames = 1);

    void MoveToItem(int32_t current, int32_t target, df::interface_key inc = interface_key::STANDARDSCROLL_DOWN, df::interface_key dec = interface_key::STANDARDSCROLL_UP);
    void EnterString(const std::string & current, const std::string & target);

    virtual bool SuppressStateChange(color_ostream & out, state_change_event event) { return event == SC_VIEWSCREEN_CHANGED; }
    virtual void Run(color_ostream & out) = 0;

    struct wait_for_next_frame {};

private:
    size_t wait_multiplier;
    size_t wait_frames;
    std::vector<size_t> current_step;
    std::vector<size_t> last_step;

    bool run(color_ostream & out);

    bool step_in();
    void step_out();
    void step_reset();
    void step_skip(size_t steps = 1);

    friend struct EventManager;

public:
    const std::string description;
};

struct EventManager
{
public:
    EventManager();
    ~EventManager();

    OnupdateCallback *onupdate_register(const std::string & descr, int32_t ticklimit, int32_t initialtickdelay, std::function<void(color_ostream &)> b);
    OnupdateCallback *onupdate_register_once(const std::string & descr, int32_t ticklimit, int32_t initialtickdelay, std::function<bool(color_ostream &)> b);
    OnupdateCallback *onupdate_register_once(const std::string & descr, int32_t ticklimit, std::function<bool(color_ostream &)> b);
    OnupdateCallback *onupdate_register_once(const std::string & descr, std::function<bool(color_ostream &)> b);
    void onupdate_unregister(OnupdateCallback *&b);

    OnstatechangeCallback *onstatechange_register(const std::string & descr, std::function<void(color_ostream &, state_change_event)> b);
    OnstatechangeCallback *onstatechange_register_once(const std::string & descr, std::function<bool(color_ostream &, state_change_event)> b);
    void onstatechange_unregister(OnstatechangeCallback *&b);

    bool register_exclusive(ExclusiveCallback *cb, bool force = false);
    void queue_exclusive(ExclusiveCallback *cb);
    inline bool has_exclusive() const { return exclusive != nullptr; }
    template<typename E>
    inline bool has_exclusive() const { return dynamic_cast<E *>(exclusive) != nullptr; }

    void onstatechange(color_ostream & out, state_change_event event);
    void onupdate(color_ostream & out);
private:
    friend class AI;
    void clear();

    ExclusiveCallback *exclusive;
    std::list<ExclusiveCallback *> exclusive_queue;
    std::vector<OnupdateCallback *> onupdate_list;
    std::vector<OnstatechangeCallback *> onstatechange_list;
};

extern EventManager events;
