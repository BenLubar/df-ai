#include "event_manager.h"
#include "ai.h"
#include "camera.h"
#include "embark.h"
#include "exclusive_callback.h"
#include "debug.h"
#include "thirdparty/dfplex/Client.hpp"

#include "df/viewscreen_movieplayerst.h"
#include "df/viewscreen_textviewerst.h"

#include "modules/Gui.h"
#include "modules/Screen.h"
#include "modules/Units.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(pause_state);
REQUIRE_GLOBAL(ui);

EventManager events;

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
    exclusive{},
    delay_delete_exclusive{},
    exclusive_queue{},
    onupdate_list{},
    onstatechange_list{},
    dfplex_client{}
{
}

EventManager::~EventManager()
{
    remove_dfplex_client();
}

void EventManager::clear()
{
    DFAI_DEBUG(tick, 1, "clearing exclusive callback");
    if (exclusive)
    {
        DFAI_DEBUG(tick, 1, "clearing exclusive: " << exclusive->description);
        exclusive = nullptr;
    }
    if (delay_delete_exclusive)
    {
        DFAI_DEBUG(tick, 1, "[delayed] clearing exclusive: " << delay_delete_exclusive->description);
        delay_delete_exclusive = nullptr;
    }
    DFAI_DEBUG(tick, 1, "clearing exclusive queue");
    for (auto & e : exclusive_queue)
    {
        DFAI_DEBUG(tick, 1, "clearing exclusive: " << e->description);
        e = nullptr;
    }
    exclusive_queue.clear();
    DFAI_DEBUG(tick, 1, "clearing event listeners");
    for (auto it : onupdate_list)
    {
        DFAI_DEBUG(tick, 1, "clearing onupdate: " << it->description);
        delete it;
    }
    onupdate_list.clear();
    for (auto it : onstatechange_list)
    {
        DFAI_DEBUG(tick, 1, "clearing onstatechange: " << it->description);
        delete it;
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
    DFAI_DEBUG(tick, 1, "onupdate_register: " << descr << " tick limit " << ticklimit << " initial delay " << initialtickdelay);
    OnupdateCallback *h = new OnupdateCallback(descr, [b](color_ostream & out) -> bool { b(out); return false; }, ticklimit, initialtickdelay);
    onupdate_list.push_back(h);
    std::sort(onupdate_list.begin(), onupdate_list.end(), update_cmp);
    return h;
}

OnupdateCallback *EventManager::onupdate_register_once(const std::string & descr, std::function<bool(color_ostream &)> b)
{
    DFAI_DEBUG(tick, 1, "onupdate_register_once: " << descr);
    OnupdateCallback *h = new OnupdateCallback(descr, b);
    onupdate_list.push_back(h);
    std::sort(onupdate_list.begin(), onupdate_list.end(), update_cmp);
    return h;
}

OnupdateCallback *EventManager::onupdate_register_once(const std::string & descr, int32_t ticklimit, std::function<bool(color_ostream &)> b)
{
    DFAI_DEBUG(tick, 1, "onupdate_register_once: " << descr << " tick limit " << ticklimit);
    OnupdateCallback *h = new OnupdateCallback(descr, b, ticklimit, ticklimit);
    onupdate_list.push_back(h);
    std::sort(onupdate_list.begin(), onupdate_list.end(), update_cmp);
    return h;
}

OnupdateCallback *EventManager::onupdate_register_once(const std::string & descr, int32_t ticklimit, int32_t initialtickdelay, std::function<bool(color_ostream &)> b)
{
    DFAI_DEBUG(tick, 1, "onupdate_register_once: " << descr << " tick limit " << ticklimit << " initial delay " << initialtickdelay);
    OnupdateCallback *h = new OnupdateCallback(descr, b, ticklimit, initialtickdelay);
    onupdate_list.push_back(h);
    std::sort(onupdate_list.begin(), onupdate_list.end(), update_cmp);
    return h;
}

void EventManager::onupdate_unregister(OnupdateCallback *&b)
{
    DFAI_DEBUG(tick, 1, "onupdate_unregister: " << b->description);
    onupdate_list.erase(std::remove(onupdate_list.begin(), onupdate_list.end(), b), onupdate_list.end());
    delete b;
    b = nullptr;
}

OnstatechangeCallback *EventManager::onstatechange_register(const std::string & descr, std::function<void(color_ostream &, state_change_event)> b)
{
    DFAI_DEBUG(tick, 1, "onstatechange_register: " << descr);
    OnstatechangeCallback *h = new OnstatechangeCallback(descr, [b](color_ostream & out, state_change_event event) -> bool { b(out, event); return false; });
    onstatechange_list.push_back(h);
    return h;
}

OnstatechangeCallback *EventManager::onstatechange_register_once(const std::string & descr, std::function<bool(color_ostream &, state_change_event)> b)
{
    DFAI_DEBUG(tick, 1, "onstatechange_register_once: " << descr);
    OnstatechangeCallback *h = new OnstatechangeCallback(descr, b);
    onstatechange_list.push_back(h);
    return h;
}

void EventManager::onstatechange_unregister(OnstatechangeCallback *&b)
{
    DFAI_DEBUG(tick, 1, "onstatechange_unregister: " << b->description);
    onstatechange_list.erase(std::remove(onstatechange_list.begin(), onstatechange_list.end(), b), onstatechange_list.end());
    delete b;
    b = nullptr;
}

void EventManager::create_dfplex_client()
{
    if (dfplex_client)
    {
        // already created
        return;
    }

    auto func_ptr = static_cast<std::function<Client *(client_update_cb &&)> *>(Core::getInstance().GetData("dfplex_add_client_cb"));
    if (!func_ptr)
    {
        // dfplex not available
        return;
    }

    dfplex_client = (*func_ptr)([this](Client *client, const ClientUpdateInfo & info)
    {
        if (info.on_destroy)
        {
            remove_dfplex_client();
            return;
        }

        client->id->nick_colour = (*cur_year_tick >> 5) & 7;

        extern std::unique_ptr<AI> dwarfAI;
        ui->follow_unit = dwarfAI->camera.following;
        if (AI::is_dwarfmode_viewscreen())
        {
            if (auto follow = df::unit::find(ui->follow_unit))
            {
                Gui::revealInDwarfmodeMap(Units::getPosition(follow), true);
            }
        }

        onupdate(Core::getInstance().getConsole(), [client](std::vector<df::interface_key> & keys)
        {
            for (auto key : keys)
            {
                client->keyqueue.emplace(key);
            }
            keys.clear();
        });
    });
    dfplex_client->id->nick = "df-ai";
}

void EventManager::remove_dfplex_client()
{
    if (!dfplex_client)
    {
        return;
    }

    if (auto func = static_cast<std::function<void(Client *)> *>(Core::getInstance().GetData("dfplex_remove_client")))
    {
        (*func)(dfplex_client);
    }
    dfplex_client = nullptr;
}

bool EventManager::register_exclusive(std::unique_ptr<ExclusiveCallback> && cb, bool force)
{
    DFAI_DEBUG(tick, 1, "register_exclusive: " << cb->description);
    if (exclusive)
    {
        DFAI_DEBUG(tick, 1, "already have an exclusive");
        if (force)
        {
            DFAI_DEBUG(tick, 1, "forcing registration");
            if (delay_delete_exclusive)
            {
                // not active
            }
            else
            {
                delay_delete_exclusive = std::move(exclusive);
            }
        }
        else
        {
            return false;
        }
    }
    DFAI_DEBUG(tick, 1, "exclusive registered");
    exclusive = std::move(cb);
    return true;
}

void EventManager::queue_exclusive(std::unique_ptr<ExclusiveCallback> && cb)
{
    DFAI_DEBUG(tick, 1, "queue_exclusive: " << cb->description);
    exclusive_queue.push_back(std::move(cb));
}
std::string EventManager::status()
{
    std::ostringstream str;

    if (exclusive)
    {
        str << "exclusive: " << exclusive->description;
    }
    else
    {
        str << "(no exclusive)";
    }

    return str.str();
}
void EventManager::report(std::ostream & out, bool html)
{
    if (html)
    {
        if (onupdate_list.empty() && onstatechange_list.empty())
        {
            out << "<p><i>(no listeners)</i></p>";
        }
        else
        {
            out << "<ul>";
            for (auto & u : onupdate_list)
            {
                if (u->hasTickLimit)
                {
                    continue;
                }
                out << "<li><b>Every Tick:</b> " << html_escape(u->description) << "</li>";
            }
            for (auto & u : onupdate_list)
            {
                if (!u->hasTickLimit)
                {
                    continue;
                }
                out << "<li><b>Once Per " << u->ticklimit << " ticks";
                // TODO: int32_t minyear;
                // TODO: int32_t minyeartick;
                out << ":</b> " << html_escape(u->description) << "</li>";
            }
            for (auto & c : onstatechange_list)
            {
                out << "<li><b>State Change:</b> " << html_escape(c->description) << "</li>";
            }
            out << "</ul>";
        }
    }
    else
    {
        if (onupdate_list.empty() && onstatechange_list.empty())
        {
            out << "(no listeners)\n\n";
        }
        else
        {
            for (auto & u : onupdate_list)
            {
                if (u->hasTickLimit)
                {
                    continue;
                }
                out << "- Every Tick: " << u->description << "\n";
            }
            for (auto & u : onupdate_list)
            {
                if (!u->hasTickLimit)
                {
                    continue;
                }
                out << "- Once Per " << u->ticklimit << " ticks";
                // TODO: int32_t minyear;
                // TODO: int32_t minyeartick;
                out << ": " << html_escape(u->description) << "\n";
            }
            for (auto & c : onstatechange_list)
            {
                out << "- State Change: " << html_escape(c->description) << "\n";
            }
            out << "\n";
        }
    }
    if (html)
    {
        out << "<h2 id=\"Events_Exclusive\">Exclusive</h2><p>";
    }
    else
    {
        out << "## Exclusive\n\n";
    }
    if (exclusive)
    {
        out << maybe_escape(exclusive->description, html);
    }
    else
    {
        out << (html ? "<i>(none)</i>" : "(none)");
    }
    if (html)
    {
        out << "</p><h3 id=\"Events_Exclusive_Queue\">Queue</h3>";
        if (exclusive_queue.empty())
        {
            out << "<p><i>(empty)</i></p>";
        }
        else
        {
            out << "<ul>";
            for (auto & e : exclusive_queue)
            {
                out << "<li>" << html_escape(e->description) << "</li>";
            }
            out << "</ul>";
        }
    }
    else
    {
        out << "\n\n### Queue\n\n";
        if (exclusive_queue.empty())
        {
            out << "(empty)\n";
        }
        else
        {
            for (auto & e : exclusive_queue)
            {
                out << "- " << e->description << "\n";
            }
        }
    }
}

void EventManager::onupdate(color_ostream & out, const std::function<void(std::vector<df::interface_key> &)> & send_keys)
{
    if (delay_delete_exclusive)
    {
        DFAI_DEBUG(tick, 1, "onupdate: [delayed] deleting exclusive: " << delay_delete_exclusive->description);
        delay_delete_exclusive = nullptr;
    }

    if (!exclusive && !exclusive_queue.empty() && AI::is_dwarfmode_viewscreen())
    {
        DFAI_DEBUG(tick, 1, "onupdate: next exclusive from queue");
        exclusive = std::move(exclusive_queue.front());
        exclusive_queue.pop_front();
    }

    if (exclusive)
    {
        if (exclusive->run(out, send_keys))
        {
            DFAI_DEBUG(tick, 1, "onupdate: exclusive completed: " << exclusive->description);
            exclusive = nullptr;
        }
        else
        {
            DFAI_DEBUG(tick, 1, "onupdate: waiting on exclusive: " << exclusive->description);
        }
        return;
    }

    // make a copy
    std::vector<OnupdateCallback *> list{ onupdate_list.cbegin(), onupdate_list.cend() };

    for (auto it = list.begin(); it != list.end(); it++)
    {
        DFAI_DEBUG(tick, 1, "onupdate: checking: " << (*it)->description);
        if (!(*it)->check_run(out, *cur_year, *cur_year_tick))
        {
            DFAI_DEBUG(tick, 1, "onupdate: stopped iteration at: " << (*it)->description);
            break;
        }
        DFAI_DEBUG(tick, 1, "onupdate: called: " << (*it)->description);
    }

    std::sort(onupdate_list.begin(), onupdate_list.end(), update_cmp);
}
void EventManager::onstatechange(color_ostream & out, state_change_event event)
{
    if (event == SC_VIEWSCREEN_CHANGED)
    {
        df::viewscreen *curview = Gui::getCurViewscreen(true);
        if (auto view = strict_virtual_cast<df::viewscreen_textviewerst>(curview))
        {
            if (view->formatted_text.size() == 1)
            {
                const std::string & text = view->formatted_text.at(0)->text;
                if (text == "Your strength has been broken." ||
                    text == "Your settlement has crumbled to its end." ||
                    text == "Your settlement has been abandoned.")
                {
                    extern std::unique_ptr<AI> dwarfAI;
                    if (dwarfAI)
                    {
                        dwarfAI->debug(out, "you just lost the game: " + text);
                        dwarfAI->debug(out, "Exiting AI");
                        dwarfAI->onupdate_unregister(out);

                        // get rid of all the remaining event handlers
                        events.clear();

                        // remove embark-specific saved data
                        dwarfAI->unpersist(out);
                        dwarfAI->skip_persist = true;

                        if (config.random_embark)
                        {
                            register_exclusive(std::make_unique<RestartWaitExclusive>(*dwarfAI), true);
                        }

                        // don't unpause, to allow for 'die'
                    }
                    return;
                }
            }
        }
    }

    // check if we should be a client rather than uniplexing
    create_dfplex_client();

    if (exclusive)
    {
        if (auto view = strict_virtual_cast<df::viewscreen_movieplayerst>(Gui::getCurViewscreen(true)))
        {
            // Don't cancel the intro video this way - it causes the sounds from the intro to get stuck in CMV recordings.
            if (!view->is_playing)
            {
                DFAI_DEBUG(tick, 1, "onstatechange: dismissing recording finished for exclusive");
                Screen::dismiss(view);
                extern std::unique_ptr<AI> dwarfAI;
                dwarfAI->camera.check_record_status();
                return;
            }
        }
        if (event == SC_VIEWSCREEN_CHANGED)
        {
            if (ExclusiveCallback *e2 = exclusive->ReplaceOnScreenChange())
            {
                exclusive.reset(e2);
            }
        }
        if (exclusive->SuppressStateChange(out, event))
        {
            return;
        }
    }

    // make a copy
    std::vector<OnstatechangeCallback *> list{ onstatechange_list.cbegin(), onstatechange_list.cend() };

    for (auto it = list.begin(); it != list.end(); it++)
    {
        DFAI_DEBUG(tick, 1, "onstatechange: checking: " << (*it)->description);
        if ((*it)->cb(out, event))
        {
            onstatechange_unregister(*it);
        }
        else
        {
            DFAI_DEBUG(tick, 1, "onstatechange: called: " << (*it)->description);
        }
    }
}

// vim: et:sw=4:ts=4
