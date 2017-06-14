#include "event_manager.h"
#include "ai.h"
#include "camera.h"

#include "df/viewscreen_movieplayerst.h"

#include "modules/Gui.h"
#include "modules/Screen.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);

EventManager events;

OnupdateCallback::OnupdateCallback(std::string descr, std::function<bool(color_ostream &)> cb) :
    callback(cb),
    ticklimit(0),
    minyear(0),
    minyeartick(0),
    description(descr),
    hasTickLimit(false)
{
}

OnupdateCallback::OnupdateCallback(std::string descr, std::function<bool(color_ostream &)> cb, int32_t tl, int32_t initdelay) :
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

OnstatechangeCallback::OnstatechangeCallback(std::function<bool(color_ostream &, state_change_event)> cb) :
    cb(cb)
{
}

EventManager::EventManager() :
    onupdate_list(),
    onstatechange_list(),
    exclusive(),
    exclusive_cur(0),
    exclusive_ticks(0)
{
}

EventManager::~EventManager()
{
}

void EventManager::clear()
{
    for (auto it = onupdate_list.begin(); it != onupdate_list.end(); it++)
    {
        delete *it;
    }
    onupdate_list.clear();
    for (auto it = onstatechange_list.begin(); it != onstatechange_list.end(); it++)
    {
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

OnupdateCallback *EventManager::onupdate_register(std::string descr, int32_t ticklimit, int32_t initialtickdelay, std::function<void(color_ostream &)> b)
{
    OnupdateCallback *h = new OnupdateCallback(descr, [b](color_ostream & out) -> bool { b(out); return false; }, ticklimit, initialtickdelay);
    onupdate_list.push_back(h);
    std::sort(onupdate_list.begin(), onupdate_list.end(), update_cmp);
    return h;
}

OnupdateCallback *EventManager::onupdate_register_once(std::string descr, std::function<bool(color_ostream &)> b)
{
    OnupdateCallback *h = new OnupdateCallback(descr, b);
    onupdate_list.push_back(h);
    std::sort(onupdate_list.begin(), onupdate_list.end(), update_cmp);
    return h;
}

OnupdateCallback *EventManager::onupdate_register_once(std::string descr, int32_t ticklimit, std::function<bool(color_ostream &)> b)
{
    OnupdateCallback *h = new OnupdateCallback(descr, b, ticklimit, ticklimit);
    onupdate_list.push_back(h);
    std::sort(onupdate_list.begin(), onupdate_list.end(), update_cmp);
    return h;
}

OnupdateCallback *EventManager::onupdate_register_once(std::string descr, int32_t ticklimit, int32_t initialtickdelay, std::function<bool(color_ostream &)> b)
{
    OnupdateCallback *h = new OnupdateCallback(descr, b, ticklimit, initialtickdelay);
    onupdate_list.push_back(h);
    std::sort(onupdate_list.begin(), onupdate_list.end(), update_cmp);
    return h;
}

void EventManager::onupdate_unregister(OnupdateCallback *&b)
{
    onupdate_list.erase(std::remove(onupdate_list.begin(), onupdate_list.end(), b), onupdate_list.end());
    delete b;
    b = nullptr;
}

OnstatechangeCallback *EventManager::onstatechange_register(std::function<void(color_ostream &, state_change_event)> b)
{
    OnstatechangeCallback *h = new OnstatechangeCallback([b](color_ostream & out, state_change_event event) -> bool { b(out, event); return false; });
    onstatechange_list.push_back(h);
    return h;
}

OnstatechangeCallback *EventManager::onstatechange_register_once(std::function<bool(color_ostream &, state_change_event)> b)
{
    OnstatechangeCallback *h = new OnstatechangeCallback(b);
    onstatechange_list.push_back(h);
    return h;
}

void EventManager::onstatechange_unregister(OnstatechangeCallback *&b)
{
    onstatechange_list.erase(std::remove(onstatechange_list.begin(), onstatechange_list.end(), b), onstatechange_list.end());
    delete b;
    b = nullptr;
}

bool EventManager::register_exclusive(std::function<bool(color_ostream &)> b, int32_t ticks)
{
    if (exclusive)
    {
        return false;
    }
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
                exclusive = nullptr;
                exclusive_cur = 0;
                exclusive_ticks = 0;
            }
            exclusive_cur = 0;
        }
        return;
    }

    // make a copy
    std::vector<OnupdateCallback *> list = onupdate_list;

    for (auto it = list.begin(); it != list.end(); it++)
    {
        if (!(*it)->check_run(out, *cur_year, *cur_year_tick))
        {
            break;
        }
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
                Screen::dismiss(curview);
                extern AI *dwarfAI;
                dwarfAI->camera->check_record_status();
            }
        }
        return;
    }

    // make a copy
    std::vector<OnstatechangeCallback *> list = onstatechange_list;

    for (auto it = list.begin(); it != list.end(); it++)
    {
        if ((*it)->cb(out, event))
        {
            onstatechange_unregister(*it);
        }
    }
}

// vim: et:sw=4:ts=4
