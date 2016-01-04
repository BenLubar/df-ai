#include "event_manager.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);

EventManager events;

OnupdateCallback::OnupdateCallback(std::string descr, std::function<void(color_ostream &)> cb) :
    callback(cb),
    ticklimit(0),
    minyear(0),
    minyeartick(0),
    description(descr),
    hasTickLimit(false)
{
}

OnupdateCallback::OnupdateCallback(std::string descr, std::function<void(color_ostream &)> cb, int32_t tl, int32_t initdelay) :
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

    callback(out);
    return true;
}

OnstatechangeCallback::OnstatechangeCallback(std::function<void(color_ostream &, state_change_event)> cb) :
    cb(cb)
{
}

EventManager::EventManager() :
    onupdate_list(),
    onstatechange_list()
{
}

EventManager::~EventManager()
{
    for (auto u : onupdate_list)
    {
        delete u;
    }
    for (auto u : onstatechange_list)
    {
        delete u;
    }
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
    OnupdateCallback *h = new OnupdateCallback(descr, b, ticklimit, initialtickdelay);
    onupdate_list.push_back(h);
    std::sort(onupdate_list.begin(), onupdate_list.end(), update_cmp);
    return h;
}

OnupdateCallback *EventManager::onupdate_register_once(std::string descr, int32_t ticklimit, std::function<bool(color_ostream &)> b)
{
    OnupdateCallback *h;
    h = new OnupdateCallback(descr, [this, &h, b](color_ostream & out)
            {
                if (b(out))
                {
                    onupdate_unregister(h);
                }
            }, ticklimit, ticklimit);
    onupdate_list.push_back(h);
    std::sort(onupdate_list.begin(), onupdate_list.end(), update_cmp);
    return h;
}

OnupdateCallback *EventManager::onupdate_register_once(std::string descr, std::function<bool(color_ostream &)> b)
{
    OnupdateCallback *h;
    h = new OnupdateCallback(descr, [this, &h, b](color_ostream & out)
            {
                if (b(out))
                {
                    onupdate_unregister(h);
                }
            });
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
    OnstatechangeCallback *h = new OnstatechangeCallback(b);
    onstatechange_list.push_back(h);
    return h;
}

OnstatechangeCallback *EventManager::onstatechange_register_once(std::function<bool(color_ostream &, state_change_event)> b)
{
    OnstatechangeCallback *h;
    h = new OnstatechangeCallback([this, &h, b](color_ostream & out, state_change_event event)
    {
        if (b(out, event))
        {
            onstatechange_unregister(h);
        }
    });
    onstatechange_list.push_back(h);
    return h;
}

void EventManager::onstatechange_unregister(OnstatechangeCallback *&b)
{
    onstatechange_list.erase(std::remove(onstatechange_list.begin(), onstatechange_list.end(), b), onstatechange_list.end());
    delete b;
    b = nullptr;
}

void EventManager::onupdate(color_ostream & out)
{
    // make a copy
    std::vector<OnupdateCallback *> list = onupdate_list;

    for (auto cb : list)
    {
        if (!cb->check_run(out, *cur_year, *cur_year_tick))
            break;
    }

    std::sort(onupdate_list.begin(), onupdate_list.end(), update_cmp);
}
void EventManager::onstatechange(color_ostream & out, state_change_event event)
{
    // make a copy
    std::vector<OnstatechangeCallback *> list = onstatechange_list;

    for (auto cb : list)
    {
        cb->cb(out, event);
    }
}

// vim: et:sw=4:ts=4
