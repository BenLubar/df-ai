#include "ai.h"
#include "population.h"
#include "plan.h"
#include "stocks.h"
#include "camera.h"
#include "embark.h"

#include "modules/Gui.h"
#include "modules/Screen.h"
#include "modules/Translation.h"
#include "modules/Units.h"

#include "df/announcements.h"
#include "df/item.h"
#include "df/report.h"
#include "df/viewscreen.h"
#include "df/viewscreen_optionst.h"
#include "df/viewscreen_requestagreementst.h"
#include "df/viewscreen_textviewerst.h"
#include "df/viewscreen_topicmeeting_takerequestsst.h"
#include "df/viewscreen_topicmeetingst.h"
#include "df/world.h"

#include <sstream>

REQUIRE_GLOBAL(announcements);
REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(pause_state);
REQUIRE_GLOBAL(world);

AI::AI() :
    rng(0),
    logger("df-ai.log", std::ofstream::out | std::ofstream::app),
    pop(new Population(this)),
    plan(new Plan(this)),
    stocks(new Stocks(this)),
    camera(new Camera(this)),
    embark(new Embark(this)),
    status_onupdate(nullptr),
    pause_onupdate(nullptr),
    seen_cvname()
{
    seen_cvname.insert("viewscreen_dwarfmodest");
}

AI::~AI()
{
    delete embark;
    delete camera;
    delete stocks;
    delete plan;
    delete pop;
    logger.close();
    for (auto u : events.onstatechange_list)
        delete u;
    events.onstatechange_list.clear();
    for (auto u : events.onupdate_list)
        delete u;
    events.onupdate_list.clear();
}

std::string AI::timestamp(int32_t y, int32_t t)
{
    if (y == 0 && t == 0)
    {
        // split up to avoid trigraphs
        return "?????" "-" "??" "-" "??" ":" "????";
    }
    return stl_sprintf("%05d-%02d-%02d:%04d",
            y,                    // year
            t / 50 / 24 / 28 + 1, // month
            t / 50 / 24 % 28 + 1, // day
            t % (24 * 50));       // time
}

std::string AI::timestamp()
{
    return timestamp(*cur_year, *cur_year_tick);
}

std::string AI::describe_item(df::item *i)
{
    std::string s;
    i->getItemDescription(&s, 0);
    return s;
}

std::string AI::describe_unit(df::unit *u)
{
    if (!u)
        return "(unknown unit)";

    std::string s = Translation::TranslateName(&u->name, false);
    s = Translation::capitalize(s);
    if (!s.empty())
        s += ", ";
    s += Units::getProfessionName(u);
    return s;
}

void AI::debug(color_ostream & out, std::string str, df::coord announce)
{
    Gui::showZoomAnnouncement(df::announcement_type(0), announce, "AI: " + str, 7, false);
    debug(out, str);
}

void AI::debug(color_ostream & out, std::string str)
{
    std::string ts = timestamp();

    if (DEBUG)
    {
        out << "AI: " << ts << " " << DF2CONSOLE(str) << "\n";
    }
    logger << ts << " ";
    size_t pos = 0;
    while (true)
    {
        size_t end = str.find('\n', pos);
        if (end == std::string::npos)
        {
            logger << DF2UTF(str.substr(pos)) << "\n";
            break;
        }
        logger << DF2UTF(str.substr(pos, end - pos)) << "\n                 ";
        pos = end + 1;
    }
    logger.flush();
}

command_result AI::startup(color_ostream & out)
{
    command_result res = CR_OK;
    if (res == CR_OK)
        res = Core::getInstance().runCommand(out, "enable autolabor");
    if (res == CR_OK)
        res = pop->startup(out);
    if (res == CR_OK)
        res = plan->startup(out);
    if (res == CR_OK)
        res = stocks->startup(out);
    if (res == CR_OK)
        res = camera->startup(out);
    if (res == CR_OK)
        res = embark->startup(out);
    return res;
}

void AI::unpause()
{
    while (!world->status.popups.empty())
    {
        interface_key_set keys;
        keys.insert(interface_key::CLOSE_MEGA_ANNOUNCEMENT);
        Gui::getCurViewscreen()->feed(&keys);
    }
    if (*pause_state)
    {
        interface_key_set keys;
        keys.insert(interface_key::D_PAUSE);
        Gui::getCurViewscreen()->feed(&keys);
    }
}

void AI::handle_pause_event(color_ostream & out, df::report *announce)
{
    // unsplit announce text
    std::string fulltext = announce->text;
    auto idx = std::find(world->status.announcements.rbegin(), world->status.announcements.rend(), announce);
    while (announce->flags.bits.continuation)
    {
        idx++;
        if (idx == world->status.announcements.rend())
            break;
        announce = *idx;
        fulltext = announce->text + " " + fulltext;
    }
    debug(out, "pause: " + fulltext);

    switch (announce->type)
    {
        case announcement_type::MEGABEAST_ARRIVAL:
            debug(out, "pause: uh oh, megabeast...");
            break;
        case announcement_type::BERSERK_CITIZEN:
            debug(out, "pause: berserk");
            break;
        case announcement_type::UNDEAD_ATTACK:
            debug(out, "pause: i see dead people");
            break;
        case announcement_type::CAVE_COLLAPSE:
            debug(out, "pause: kevin?");
            break;
        case announcement_type::DIG_CANCEL_DAMP:
        case announcement_type::DIG_CANCEL_WARM:
            camera->ignore_pause();
            debug(out, "pause: lazy miners");
            break;
        case announcement_type::BIRTH_CITIZEN:
            debug(out, "pause: newborn");
            break;
        case announcement_type::BIRTH_ANIMAL:
            break;
        case announcement_type::D_MIGRANTS_ARRIVAL:
        case announcement_type::D_MIGRANT_ARRIVAL:
        case announcement_type::MIGRANT_ARRIVAL:
        case announcement_type::NOBLE_ARRIVAL:
        case announcement_type::FORT_POSITION_SUCCESSION:
            debug(out, "pause: more minions");
            break;
        case announcement_type::DIPLOMAT_ARRIVAL:
        case announcement_type::LIAISON_ARRIVAL:
        case announcement_type::CARAVAN_ARRIVAL:
        case announcement_type::TRADE_DIPLOMAT_ARRIVAL:
            debug(out, "pause: visitors");
            break;
        case announcement_type::STRANGE_MOOD:
        case announcement_type::MOOD_BUILDING_CLAIMED:
        case announcement_type::ARTIFACT_BEGUN:
        case announcement_type::MADE_ARTIFACT:
            debug(out, "pause: mood");
            break;
        case announcement_type::FEATURE_DISCOVERY:
        case announcement_type::STRUCK_DEEP_METAL:
            debug(out, "pause: dig dig dig");
            break;
        case announcement_type::TRAINING_FULL_REVERSION:
            debug(out, "pause: born to be wild");
            break;
        case announcement_type::NAMED_ARTIFACT:
            debug(out, "pause: hallo");
            break;
        default:
            {
                const static std::string prefix("AMBUSH");
                std::string type(ENUM_KEY_STR(announcement_type, announce->type));
                if (std::mismatch(prefix.begin(), prefix.end(), type.begin()).first == prefix.end())
                {
                    debug(out, "pause: an ambush!");
                }
                else
                {
                    debug(out, "pause: unhandled pausing event " + type);
                    // return;
                }
                break;
            }
    }

    if (announcements->flags[announce->type].bits.DO_MEGA)
    {
        timeout_sameview([](color_ostream & out)
                {
                    unpause();
                });
    }
    else
    {
        unpause();
    }
}

void AI::statechanged(color_ostream & out, state_change_event st)
{
    // automatically unpause the game (only for game-generated pauses)
    if (st == SC_PAUSED)
    {
        auto la = std::find_if(world->status.announcements.rbegin(), world->status.announcements.rend(), [](df::report *a) -> bool
                {
                    return announcements->flags[a->type].bits.PAUSE;
                });
        if (la != world->status.announcements.rend() &&
                (*la)->year == *cur_year &&
                (*la)->time == *cur_year_tick)
        {
            handle_pause_event(out, *la);
        }
        else
        {
            unpause();
            debug(out, "pause without an event");
        }
    }
    else if (st == SC_VIEWSCREEN_CHANGED)
    {
        df::viewscreen *curview = Gui::getCurViewscreen();
        df::viewscreen_textviewerst *view = strict_virtual_cast<df::viewscreen_textviewerst>(curview);
        if (view)
        {
            std::ostringstream text;
            for (auto t : view->formatted_text)
            {
                if (t->text)
                {
                    text << " " << t->text;
                }
            }

            std::string stripped = text.str();
            stripped.erase(std::remove(stripped.begin(), stripped.end(), ' '), stripped.end());

            if (stripped.find("I" "am" "your" "liaison" "from" "the" "Mountainhomes." "Let's" "discuss" "your" "situation.") != std::string::npos ||
                    stripped.find("I" "look" "forward" "to" "our" "meeting" "next" "year.") != std::string::npos ||
                    stripped.find("A" "diplomat" "has" "left" "unhappy.") != std::string::npos ||
                    stripped.find("You" "have" "disrespected" "the" "trees" "in" "this" "area," "but" "this" "is" "what" "we" "have" "come" "to" "expect" "from" "your" "stunted" "kind." "Further" "abuse" "cannot" "be" "tolerated." "Let" "this" "be" "a" "warning" "to" "you.") != std::string::npos ||
                    stripped.find("Greetings" "from" "the" "woodlands." "We" "have" "much" "to" "discuss.") != std::string::npos ||
                    stripped.find("Although" "we" "do" "not" "always" "see" "eye" "to" "eye" "(ha!)," "I" "bid" "you" "farewell." "May" "you" "someday" "embrace" "nature" "as" "you" "embrace" "the" "rocks" "and" "mud.") != std::string::npos)
            {
                debug(out, "exit diplomat textviewerst:" + text.str());
                timeout_sameview([](color_ostream & out)
                        {
                            interface_key_set keys;
                            keys.insert(interface_key::LEAVESCREEN);
                            Gui::getCurViewscreen()->feed(&keys);
                        });
            }
            else if (stripped.find("A" "vile" "force" "of" "darkness" "has" "arrived!") != std::string::npos ||
                    stripped.find("have" "brought" "the" "full" "forces" "of" "their" "lands" "against" "you.") != std::string::npos ||
                    stripped.find("The" "enemy" "have" "come" "and" "are" "laying" "siege" "to" "the" "fortress.") != std::string::npos ||
                    stripped.find("The" "dead" "walk." "Hide" "while" "you" "still" "can!") != std::string::npos)
            {
                debug(out, "exit siege textviewerst:" + text.str());
                timeout_sameview([](color_ostream & out)
                        {
                            interface_key_set keys;
                            keys.insert(interface_key::LEAVESCREEN);
                            Gui::getCurViewscreen()->feed(&keys);
                            unpause();
                        });
            }
            else if (stripped.find("Your" "strength" "has" "been" "broken.") != std::string::npos ||
                    stripped.find("Your" "settlement" "has" "crumbled" "to" "its" "end.") != std::string::npos ||
                    stripped.find("Your" "settlement" "has" "been" "abandoned.") != std::string::npos)
            {
                debug(out, "you just lost the game:" + text.str());
                debug(out, "Exiting AI");
                onupdate_unregister(out);

                // get rid of all the remaining event handlers
                for (auto u : events.onstatechange_list)
                    delete u;
                events.onstatechange_list.clear();
                for (auto u : events.onupdate_list)
                    delete u;
                events.onupdate_list.clear();

                embark->register_restart_timer(out);

                // don't unpause, to allow for 'die'
            }
            else
            {
                debug(out, "[ERROR] paused in unknown textviewerst:" + text.str());
            }
        }
        else if (strict_virtual_cast<df::viewscreen_topicmeetingst>(curview))
        {
            debug(out, "exit diplomat topicmeetingst");
            timeout_sameview([](color_ostream & out)
                    {
                        interface_key_set keys;
                        keys.insert(interface_key::OPTION1);
                        Gui::getCurViewscreen()->feed(&keys);
                    });
        }
        else if (strict_virtual_cast<df::viewscreen_topicmeeting_takerequestsst>(curview))
        {
            debug(out, "exit diplomat topicmeeting_takerequestsst");
            timeout_sameview([](color_ostream & out)
                    {
                        interface_key_set keys;
                        keys.insert(interface_key::LEAVESCREEN);
                        Gui::getCurViewscreen()->feed(&keys);
                    });
        }
        else if (strict_virtual_cast<df::viewscreen_requestagreementst>(curview))
        {
            debug(out, "exit diplomat requestagreementst");
            timeout_sameview([](color_ostream & out)
                    {
                        interface_key_set keys;
                        keys.insert(interface_key::LEAVESCREEN);
                        Gui::getCurViewscreen()->feed(&keys);
                    });
        }
        else if (virtual_identity *ident = virtual_identity::get(curview))
        {
            std::string cvname = ident->getName();
            if (seen_cvname.insert(cvname).second)
            {
                debug(out, "[ERROR] paused in unknown viewscreen " + cvname);
            }
        }
    }
}

void AI::abandon(color_ostream & out)
{
    if (!AI_RANDOM_EMBARK)
        return;
    df::viewscreen_optionst *view = df::allocate<df::viewscreen_optionst>();
    view->options.push_back(df::viewscreen_optionst::Abandon);
    Screen::show(view);
    interface_key_set keys;
    keys.insert(interface_key::SELECT);
    view->feed(&keys);
    keys.clear();
    keys.insert(interface_key::MENU_CONFIRM);
    view->feed(&keys);
    // current view switches to a textviewer at this point
    keys.clear();
    keys.insert(interface_key::SELECT);
    Gui::getCurViewscreen()->feed(&keys);
}

void AI::timeout_sameview(std::time_t delay, std::function<void(color_ostream &)> cb)
{
    virtual_identity *curscreen = virtual_identity::get(Gui::getCurViewscreen());
    std::time_t timeoff = std::time(nullptr) + delay;

    events.onupdate_register_once(std::string("timeout_sameview on ") + curscreen->getName(), [curscreen, timeoff, cb](color_ostream & out) -> bool
            {
                if (virtual_identity::get(Gui::getCurViewscreen()) != curscreen)
                    return true;

                if (std::time(nullptr) >= timeoff)
                {
                    cb(out);
                    return true;
                }
                return false;
            });
}

static std::time_t last_unpause;

command_result AI::onupdate_register(color_ostream & out)
{
    command_result res = CR_OK;
    if (res == CR_OK)
        res = pop->onupdate_register(out);
    if (res == CR_OK)
        res = plan->onupdate_register(out);
    if (res == CR_OK)
        res = stocks->onupdate_register(out);
    if (res == CR_OK)
        res = camera->onupdate_register(out);
    if (res == CR_OK)
        res = embark->onupdate_register(out);
    if (res == CR_OK)
    {
        status_onupdate = events.onupdate_register("df-ai status", 3*28*1200, 3*28*1200, [this](color_ostream & out) { debug(out, status()); });
        last_unpause = std::time(nullptr);
        pause_onupdate = events.onupdate_register_once("df-ai unpause", [this](color_ostream & out) -> bool
                {
                    if (std::time(nullptr) < last_unpause + 11)
                        return false;
                    if (*pause_state)
                    {
                        timeout_sameview(10, [](color_ostream & out) { unpause(); });
                        last_unpause = std::time(nullptr);
                    }
                    return false;
                });
        events.onstatechange_register_once([this](color_ostream & out, state_change_event st) -> bool
                {
                    if (st == SC_WORLD_UNLOADED)
                    {
                        debug(out, "world unloaded, disabling self");
                        onupdate_unregister(out);
                        return true;
                    }
                    statechanged(out, st);
                    return false;
                });
    }
    return res;
}

command_result AI::onupdate_unregister(color_ostream & out)
{
    command_result res = CR_OK;
    if (res == CR_OK)
        res = embark->onupdate_unregister(out);
    if (res == CR_OK)
        res = camera->onupdate_unregister(out);
    if (res == CR_OK)
        res = stocks->onupdate_unregister(out);
    if (res == CR_OK)
        res = plan->onupdate_unregister(out);
    if (res == CR_OK)
        res = pop->onupdate_unregister(out);
    if (res == CR_OK)
    {
        events.onupdate_unregister(status_onupdate);
        events.onupdate_unregister(pause_onupdate);
    }
    return res;
}

std::string AI::status()
{
    std::ostringstream str;
    str << "Plan: " << plan->status() << "\n";
    str << "Pop: " << pop->status() << "\n";
    str << "Stocks: " << stocks->status() << "\n";
    str << "Camera: " << camera->status();
    return str.str();
}

// vim: et:sw=4:ts=4
