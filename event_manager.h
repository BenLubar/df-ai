#pragma once

#include "dfhack_shared.h"

#include <functional>

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

    bool register_exclusive(const std::string & descr, std::function<bool(color_ostream &)> b, int32_t ticks = 1);

    void onstatechange(color_ostream & out, state_change_event event);
    void onupdate(color_ostream & out);
protected:
    friend class AI;
    void clear();
private:
    std::function<bool(color_ostream &)> exclusive;
    int32_t exclusive_cur;
    int32_t exclusive_ticks;
    std::vector<OnupdateCallback *> onupdate_list;
    std::vector<OnstatechangeCallback *> onstatechange_list;
};

extern EventManager events;
