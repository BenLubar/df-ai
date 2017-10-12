#include "event_manager.h"
#include "ai.h"
#include "camera.h"

#include "df/viewscreen_movieplayerst.h"

#include "modules/Gui.h"
#include "modules/Screen.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);

EventManager events;

#define TICK_DEBUG(msg) do { if (config.tick_debug) { Core::getInstance().getConsole() << "[df-ai] TICK DEBUG: " << msg << std::endl; } } while (0)

OnupdateCallback::OnupdateCallback(const std::string & descr, std::function<bool(color_ostream &)> cb) :
    callback(cb),
    ticklimit(0),
    minyear(0),
    minyeartick(0),
    description(descr),
    hasTickLimit(false)
{
}

OnupdateCallback::OnupdateCallback(const std::string & descr, std::function<bool(color_ostream &)> cb, int32_t tl, int32_t initdelay) :
    callback(cb),
    ticklimit(tl),
    minyear(*cur_year),
    minyeartick(*cur_year_tick + initdelay),
    description(descr),
    hasTickLimit(true)
{
}

const int32_t yearlen = 12 * 28 * 1200;

bool OnupdateCallback::check_run(color_ostream & out, int32_t year, int32_t yeartick)
{
    if (hasTickLimit)
    {
        if (year < minyear || (year == minyear && yeartick < minyeartick))
        {
            return false;
        }
        minyear = year;
        minyeartick = yeartick + ticklimit;
        while (minyeartick > yearlen)
        {
            minyear++;
            minyeartick -= yearlen;
        }
    }

    if (callback(out))
    {
        OnupdateCallback *tmp = this;
        events.onupdate_unregister(tmp);
    }
    return true;
}

OnstatechangeCallback::OnstatechangeCallback(const std::string & descr, std::function<bool(color_ostream &, state_change_event)> cb) :
    cb(cb),
    description(descr)
{
}

EventManager::EventManager() :
    exclusive(),
    exclusive_cur(0),
    exclusive_ticks(0),
    onupdate_list(),
    onstatechange_list()
{
}

EventManager::~EventManager()
{
}

void EventManager::clear()
{
    TICK_DEBUG("clearing event listeners");
    for (auto it = onupdate_list.begin(); it != onupdate_list.end(); it++)
    {
        TICK_DEBUG("clearing onupdate: " << (*it)->description);
        delete *it;
    }
    onupdate_list.clear();
    for (auto it = onstatechange_list.begin(); it != onstatechange_list.end(); it++)
    {
        TICK_DEBUG("clearing onstatechange: " << (*it)->description);
        delete *it;
    }
    onstatechange_list.clear();
}

static bool update_cmp(OnupdateCallback *a, OnupdateCallback *b)
{
    if (a->minyear < b->minyear)
        return true;
    if (a->minyear > b->minyear)
        return false;
    return a->minyeartick < b->minyeartick;
}

OnupdateCallback *EventManager::onupdate_register(const std::string & descr, int32_t ticklimit, int32_t initialtickdelay, std::function<void(color_ostream &)> b)
{
    TICK_DEBUG("onupdate_register: " << descr << " tick limit " << ticklimit << " initial delay " << initialtickdelay);
    OnupdateCallback *h = new OnupdateCallback(descr, [b](color_ostream & out) -> bool { b(out); return false; }, ticklimit, initialtickdelay);
    onupdate_list.push_back(h);
    std::sort(onupdate_list.begin(), onupdate_list.end(), update_cmp);
    return h;
}

OnupdateCallback *EventManager::onupdate_register_once(const std::string & descr, std::function<bool(color_ostream &)> b)
{
    TICK_DEBUG("onupdate_register_once: " << descr);
    OnupdateCallback *h = new OnupdateCallback(descr, b);
    onupdate_list.push_back(h);
    std::sort(onupdate_list.begin(), onupdate_list.end(), update_cmp);
    return h;
}

OnupdateCallback *EventManager::onupdate_register_once(const std::string & descr, int32_t ticklimit, std::function<bool(color_ostream &)> b)
{
    TICK_DEBUG("onupdate_register_once: " << descr << " tick limit " << ticklimit);
    OnupdateCallback *h = new OnupdateCallback(descr, b, ticklimit, ticklimit);
    onupdate_list.push_back(h);
    std::sort(onupdate_list.begin(), onupdate_list.end(), update_cmp);
    return h;
}

OnupdateCallback *EventManager::onupdate_register_once(const std::string & descr, int32_t ticklimit, int32_t initialtickdelay, std::function<bool(color_ostream &)> b)
{
    TICK_DEBUG("onupdate_register_once: " << descr << " tick limit " << ticklimit << " initial delay " << initialtickdelay);
    OnupdateCallback *h = new OnupdateCallback(descr, b, ticklimit, initialtickdelay);
    onupdate_list.push_back(h);
    std::sort(onupdate_list.begin(), onupdate_list.end(), update_cmp);
    return h;
}

void EventManager::onupdate_unregister(OnupdateCallback *&b)
{
    TICK_DEBUG("onupdate_unregister: " << b->description);
    onupdate_list.erase(std::remove(onupdate_list.begin(), onupdate_list.end(), b), onupdate_list.end());
    delete b;
    b = nullptr;
}

OnstatechangeCallback *EventManager::onstatechange_register(const std::string & descr, std::function<void(color_ostream &, state_change_event)> b)
{
    TICK_DEBUG("onstatechange_register: " << descr);
    OnstatechangeCallback *h = new OnstatechangeCallback(descr, [b](color_ostream & out, state_change_event event) -> bool { b(out, event); return false; });
    onstatechange_list.push_back(h);
    return h;
}

OnstatechangeCallback *EventManager::onstatechange_register_once(const std::string & descr, std::function<bool(color_ostream &, state_change_event)> b)
{
    TICK_DEBUG("onstatechange_register_once: " << descr);
    OnstatechangeCallback *h = new OnstatechangeCallback(descr, b);
    onstatechange_list.push_back(h);
    return h;
}

void EventManager::onstatechange_unregister(OnstatechangeCallback *&b)
{
    TICK_DEBUG("onstatechange_unregister: " << b->description);
    onstatechange_list.erase(std::remove(onstatechange_list.begin(), onstatechange_list.end(), b), onstatechange_list.end());
    delete b;
    b = nullptr;
}

bool EventManager::register_exclusive(const std::string & descr, std::function<bool(color_ostream &)> b, int32_t ticks)
{
    TICK_DEBUG("register_exclusive: " << descr << " ticks " << ticks);
    if (exclusive)
    {
        TICK_DEBUG("already have an exclusive");
        return false;
    }
    TICK_DEBUG("exclusive registered");
    exclusive = b;
    exclusive_ticks = ticks;
    return true;
}

void EventManager::onupdate(color_ostream & out)
{
    if (exclusive)
    {
        exclusive_cur++;
        if (exclusive_cur >= exclusive_ticks)
        {
            if (exclusive(out))
            {
                TICK_DEBUG("onupdate: exclusive completed");
                exclusive = nullptr;
                exclusive_cur = 0;
                exclusive_ticks = 0;
            }
            else
            {
                TICK_DEBUG("onupdate: exclusive called");
                exclusive_cur = 0;
            }
        }
        else
        {
            TICK_DEBUG("onupdate: waiting on exclusive");
        }
        return;
    }

    // make a copy
    std::vector<OnupdateCallback *> list = onupdate_list;

    for (auto it = list.begin(); it != list.end(); it++)
    {
        TICK_DEBUG("onupdate: checking: " << (*it)->description);
        if (!(*it)->check_run(out, *cur_year, *cur_year_tick))
        {
            TICK_DEBUG("onupdate: stopped iteration at: " << (*it)->description);
            break;
        }
        TICK_DEBUG("onupdate: called: " << (*it)->description);
    }

    std::sort(onupdate_list.begin(), onupdate_list.end(), update_cmp);
}
void EventManager::onstatechange(color_ostream & out, state_change_event event)
{
    if (exclusive)
    {
        if (event == SC_VIEWSCREEN_CHANGED)
        {
            df::viewscreen *curview = Gui::getCurViewscreen(true);
            if (strict_virtual_cast<df::viewscreen_movieplayerst>(curview))
            {
                TICK_DEBUG("onstatechange: dismissing recording finished for exclusive");
                Screen::dismiss(curview);
                extern AI *dwarfAI;
                dwarfAI->camera->check_record_status();
            }
        }
        TICK_DEBUG("onstatechange: ignoring due to exclusive");
        return;
    }

    // make a copy
    std::vector<OnstatechangeCallback *> list = onstatechange_list;

    for (auto it = list.begin(); it != list.end(); it++)
    {
        TICK_DEBUG("onstatechange: checking: " << (*it)->description);
        if ((*it)->cb(out, event))
        {
            onstatechange_unregister(*it);
        }
        else
        {
            TICK_DEBUG("onstatechange: called: " << (*it)->description);
        }
    }
}

// vim: et:sw=4:ts=4
