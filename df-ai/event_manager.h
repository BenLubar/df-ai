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

    OnupdateCallback(std::string descr, std::function<bool(color_ostream &)> cb);
    OnupdateCallback(std::string descr, std::function<bool(color_ostream &)> cb, int32_t tl, int32_t initdelay = 0);

    bool check_run(color_ostream & out, int32_t year, int32_t yeartick);
};

struct OnstatechangeCallback
{
    std::function<void(color_ostream &, state_change_event)> cb;
    OnstatechangeCallback(std::function<void(color_ostream &, state_change_event)> cb);
};

struct EventManager
{
public:
    EventManager();
    ~EventManager();

    OnupdateCallback *onupdate_register(std::string descr, int32_t ticklimit, int32_t initialtickdelay, std::function<void(color_ostream &)> b);
    OnupdateCallback *onupdate_register_once(std::string descr, int32_t ticklimit, int32_t initialtickdelay, std::function<bool(color_ostream &)> b);
    OnupdateCallback *onupdate_register_once(std::string descr, int32_t ticklimit, std::function<bool(color_ostream &)> b);
    OnupdateCallback *onupdate_register_once(std::string descr, std::function<bool(color_ostream &)> b);
    void onupdate_unregister(OnupdateCallback *b);

    OnstatechangeCallback *onstatechange_register(std::function<void(color_ostream &, state_change_event)> b);
    OnstatechangeCallback *onstatechange_register_once(std::function<bool(color_ostream &, state_change_event)> b);
    void onstatechange_unregister(OnstatechangeCallback *&b);

    void onstatechange(color_ostream & out, state_change_event event);
    void onupdate(color_ostream & out);
protected:
    friend class AI;
    std::vector<OnupdateCallback *> onupdate_list;
    std::vector<OnstatechangeCallback *> onstatechange_list;
};

extern EventManager events;

// vim: et:sw=4:ts=4
