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
    onupdate_list(),
    onstatechange_list()
{
}

EventManager::~EventManager()
{
}

void EventManager::clear()
{
    TICK_DEBUG("clearing exclusive callback");
    delete exclusive;
    exclusive = nullptr;
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

bool EventManager::register_exclusive(ExclusiveCallback *cb)
{
    TICK_DEBUG("register_exclusive: " << cb->description);
    if (exclusive)
    {
        TICK_DEBUG("already have an exclusive");
        delete cb;
        return false;
    }
    TICK_DEBUG("exclusive registered");
    exclusive = cb;
    return true;
}

void EventManager::onupdate(color_ostream & out)
{
    if (exclusive)
    {
        if (exclusive->run(out))
        {
            TICK_DEBUG("onupdate: exclusive completed");
            delete exclusive;
            exclusive = nullptr;
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

ExclusiveCallback::ExclusiveCallback(const std::string & description, size_t wait_multiplier) : description(description), wait_multiplier(wait_multiplier), wait_frames(0)
{
    if (wait_multiplier < 1)
    {
        wait_multiplier = 1;
    }
}

ExclusiveCallback::~ExclusiveCallback()
{
}

void ExclusiveCallback::Do(std::function<void()> step)
{
    if (step_in())
    {
        step();
        step_out();
    }
}

void ExclusiveCallback::If(std::function<bool()> cond, std::function<void()> step_true, std::function<void()> step_false)
{
    if (step_in())
    {
        bool ok = cond();
        step_out();

        if (!ok)
        {
            step_skip();
        }
    }

    if (step_in())
    {
        step_true();
        step_out();

        step_skip();
    }

    if (step_in())
    {
        step_false();
        step_out();
    }
}

void ExclusiveCallback::While(std::function<bool()> cond, std::function<void()> step)
{
    if (step_in())
    {
        for (;;)
        {
            if (step_in())
            {
                bool ok = cond();
                step_out();
                if (!ok)
                {
                    break;
                }
            }

            if (step_in())
            {
                step();
                step_out();
            }

            step_reset();
        }

        step_out();
    }
}

void ExclusiveCallback::Key(df::interface_key key)
{
    if (step_in())
    {
        AI::feed_key(key);
        step_out();
        throw wait_for_next_frame();
    }
}

void ExclusiveCallback::Char(std::function<char()> ch)
{
    if (step_in())
    {
        AI::feed_char(ch());
        step_out();
        throw wait_for_next_frame();
    }
}

void ExclusiveCallback::Delay(size_t frames)
{
    for (size_t i = 0; i < frames; i++)
    {
        if (step_in())
        {
            step_out();
            throw wait_for_next_frame();
        }
    }
}

bool ExclusiveCallback::run(color_ostream & out)
{
    if (wait_frames)
    {
        wait_frames--;
        return false;
    }

    current_step.clear();
    current_step.push_back(0);

    try
    {
        Run(out);
        return true;
    }
    catch (wait_for_next_frame)
    {
        wait_frames = wait_multiplier - 1;
        last_step = current_step;
        return false;
    }
}

bool ExclusiveCallback::step_in()
{
    size_t & step = current_step.back();
    step++;

    if (!last_step.empty())
    {
        for (size_t i = 0; i < current_step.size(); i++)
        {
            if (current_step.at(i) != last_step.at(i))
            {
                if (i == last_step.size() - 1 && current_step.at(i) == last_step.at(i) + 1)
                {
                    // we exited between step_out() and the next step_in().
                    break;
                }

                step++;
                return false;
            }
        }

        if (current_step.size() == last_step.size())
        {
            last_step.clear();
        }
    }

    current_step.push_back(0);

    return true;
}

void ExclusiveCallback::step_out()
{
    current_step.pop_back();
    current_step.back()++;
}

void ExclusiveCallback::step_reset()
{
    last_step.clear();
    current_step.back() = 0;
}

void ExclusiveCallback::step_skip(size_t steps)
{
    last_step = current_step;
    last_step.back() += steps * 2;
}

// vim: et:sw=4:ts=4
