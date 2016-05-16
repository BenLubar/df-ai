#include "ai.h"
#include "population.h"
#include "plan.h"
#include "stocks.h"
#include "camera.h"
#include "embark.h"

#include "modules/Gui.h"
#include "modules/Maps.h"
#include "modules/Screen.h"
#include "modules/Translation.h"
#include "modules/Units.h"
#include "modules/World.h"

#include "df/announcements.h"
#include "df/creature_raw.h"
#include "df/interface_button_building_new_jobst.h"
#include "df/item.h"
#include "df/job.h"
#include "df/manager_order.h"
#include "df/manager_order_template.h"
#include "df/report.h"
#include "df/viewscreen.h"
#include "df/viewscreen_dwarfmodest.h"
#include "df/viewscreen_movieplayerst.h"
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
REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

AI::AI() :
    rng(0),
    logger("df-ai.log", std::ofstream::out | std::ofstream::app),
    eventsJson(),
    pop(new Population(this)),
    plan(new Plan(this)),
    stocks(new Stocks(this)),
    camera(new Camera(this)),
    embark(new Embark(this)),
    status_onupdate(nullptr),
    pause_onupdate(nullptr),
    tag_enemies_onupdate(nullptr),
    seen_cvname(),
    skip_persist(false)
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
    events.clear();
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

std::string AI::describe_name(const df::language_name & name, bool in_english, bool only_last_part)
{
    std::string s = Translation::TranslateName(&name, in_english, only_last_part);
    return Translation::capitalize(s);
}

std::string AI::describe_unit(df::unit *u)
{
    if (!u)
        return "(unknown unit)";

    std::string s = describe_name(u->name);
    if (!s.empty())
        s += ", ";
    s += Units::getProfessionName(u);
    return s;
}

template<typename T>
static std::string do_describe_job(T *job)
{
    std::string desc;
    auto button = df::allocate<df::interface_button_building_new_jobst>();
    button->reaction_name = job->reaction_name;
    button->hist_figure_id = job->hist_figure_id;
    button->job_type = job->job_type;
    button->item_type = job->item_type;
    button->item_subtype = job->item_subtype;
    button->mat_type = job->mat_type;
    button->mat_index = job->mat_index;
    button->item_category = job->item_category;
    button->material_category = job->material_category;

    button->getLabel(&desc);
    delete button;

    return desc;
}

std::string AI::describe_job(df::job *job)
{
    return do_describe_job(job);
}

std::string AI::describe_job(df::manager_order *job)
{
    return do_describe_job(job);
}

std::string AI::describe_job(df::manager_order_template *job)
{
    return do_describe_job(job);
}

bool AI::feed_key(df::viewscreen *view, df::interface_key key)
{
    static interface_key_set keys; // protected by CoreSuspender
    keys.clear();
    keys.insert(key);
    view->feed(&keys);
    return !keys.count(key);
}

bool AI::feed_key(df::interface_key key)
{
    return feed_key(Gui::getCurViewscreen(true), key);
}

bool AI::feed_char(df::viewscreen *view, char ch)
{
    return feed_key(view, Screen::charToKey(ch));
}

bool AI::feed_char(char ch)
{
    return feed_key(Screen::charToKey(ch));
}

bool AI::is_dwarfmode_viewscreen()
{
    if (ui->main.mode != ui_sidebar_mode::Default)
        return false;
    if (!world->status.popups.empty())
        return false;
    if (!strict_virtual_cast<df::viewscreen_dwarfmodest>(Gui::getCurViewscreen(true)))
        return false;
    return true;
}

void AI::write_df(std::ostream & out, const std::string & str, const std::string & newline, const std::string & suffix, std::function<std::string(const std::string &)> translate)
{
    size_t pos = 0;
    while (true)
    {
        size_t end = str.find('\n', pos);
        if (end == std::string::npos)
        {
            out << translate(str.substr(pos)) << suffix;
            break;
        }
        out << translate(str.substr(pos, end - pos)) << newline;
        pos = end + 1;
    }
    out.flush();
}

void AI::debug(color_ostream & out, const std::string & str, df::coord announce)
{
    Gui::showZoomAnnouncement(df::announcement_type(0), announce, "AI: " + str, 7, false);
    debug(out, str);
}

void AI::debug(color_ostream & out, const std::string & str)
{
    std::string ts = timestamp();

    if (config.debug)
    {
        write_df(out, "AI: " + ts + " " + str, "\n", "\n", DF2CONSOLE);
    }
    write_df(logger, ts + " " + str, "\n                 ");
}

void AI::event(const std::string & name, const Json::Value & payload)
{
    if (!eventsJson.is_open())
    {
        return;
    }

    Json::Value wrapper(Json::objectValue);
    wrapper["unix"] = Json::LargestInt(time(nullptr));
    wrapper["year"] = Json::Int(*cur_year);
    wrapper["tick"] = Json::Int(*cur_year_tick);
    wrapper["name"] = name;
    wrapper["payload"] = payload;
    eventsJson << wrapper << std::endl;
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
        feed_key(interface_key::CLOSE_MEGA_ANNOUNCEMENT);
    }
    if (*pause_state)
    {
        feed_key(interface_key::D_PAUSE);
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
            {
                debug(out, "pause: uh oh, megabeast...");
                if (!tag_enemies(out))
                {
                    debug(out, "[ERROR] could not find megabeast");
                }
                break;
            }
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
        timeout_sameview([](color_ostream &) { unpause(); });
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
        df::viewscreen *curview = Gui::getCurViewscreen(true);
        df::viewscreen_textviewerst *view = strict_virtual_cast<df::viewscreen_textviewerst>(curview);
        if (view)
        {
            std::ostringstream text;
            for (auto it = view->formatted_text.begin(); it != view->formatted_text.end(); it++)
            {
                if ((*it)->text)
                {
                    text << " " << (*it)->text;
                }
            }

            std::string stripped = text.str();
            stripped.erase(std::remove(stripped.begin(), stripped.end(), ' '), stripped.end());

            if (stripped.find("I" "am" "your" "liaison" "from" "the" "Mountainhomes." "Let's" "discuss" "your" "situation.") != std::string::npos ||
                    stripped.find("I" "look" "forward" "to" "our" "meeting" "next" "year.") != std::string::npos ||
                    stripped.find("A" "diplomat" "has" "left" "unhappy.") != std::string::npos ||
                    stripped.find("What" "a" "pleasant" "surprise!" "Not" "a" "single" "tree" "here" "weeps" "from" "the" "abuses" "meted" "out" "with" "such" "ease" "by" "your" "people." "Joy!" "The" "dwarves" "have" "turned" "a" "page," "not" "that" "we" "would" "make" "paper." "A" "travesty!" "Perhaps" "it" "is" "better" "said" "that" "the" "dwarves" "have" "turned" "over" "a" "new" "leaf," "and" "the" "springtime" "for" "our" "two" "races" "has" "only" "just" "begun.") != std::string::npos ||
                    stripped.find("You" "have" "disrespected" "the" "trees" "in" "this" "area," "but" "this" "is" "what" "we" "have" "come" "to" "expect" "from" "your" "stunted" "kind." "Further" "abuse" "cannot" "be" "tolerated." "Let" "this" "be" "a" "warning" "to" "you.") != std::string::npos ||
                    stripped.find("Greetings" "from" "the" "woodlands." "We" "have" "much" "to" "discuss.") != std::string::npos ||
                    stripped.find("Although" "we" "do" "not" "always" "see" "eye" "to" "eye" "(ha!)," "I" "bid" "you" "farewell." "May" "you" "someday" "embrace" "nature" "as" "you" "embrace" "the" "rocks" "and" "mud.") != std::string::npos)
            {
                debug(out, "exit diplomat textviewerst:" + text.str());
                timeout_sameview([](color_ostream &)
                        {
                            AI::feed_key(interface_key::LEAVESCREEN);
                        });
            }
            else if (stripped.find("A" "vile" "force" "of" "darkness" "has" "arrived!") != std::string::npos ||
                    stripped.find("have" "brought" "the" "full" "forces" "of" "their" "lands" "against" "you.") != std::string::npos ||
                    stripped.find("The" "enemy" "have" "come" "and" "are" "laying" "siege" "to" "the" "fortress.") != std::string::npos ||
                    stripped.find("The" "dead" "walk." "Hide" "while" "you" "still" "can!") != std::string::npos)
            {
                debug(out, "exit siege textviewerst:" + text.str());
                timeout_sameview([](color_ostream &)
                        {
                            AI::feed_key(interface_key::LEAVESCREEN);
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
                events.clear();

                // remove embark-specific saved data
                unpersist(out);
                skip_persist = true;

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
            timeout_sameview([](color_ostream &)
                    {
                        AI::feed_key(interface_key::OPTION1);
                    });
        }
        else if (strict_virtual_cast<df::viewscreen_topicmeeting_takerequestsst>(curview))
        {
            debug(out, "exit diplomat topicmeeting_takerequestsst");
            timeout_sameview([](color_ostream &)
                    {
                        AI::feed_key(interface_key::LEAVESCREEN);
                    });
        }
        else if (strict_virtual_cast<df::viewscreen_requestagreementst>(curview))
        {
            debug(out, "exit diplomat requestagreementst");
            timeout_sameview([](color_ostream &)
                    {
                        AI::feed_key(interface_key::LEAVESCREEN);
                    });
        }
        else if (strict_virtual_cast<df::viewscreen_movieplayerst>(curview))
        {
            Screen::dismiss(curview);
            camera->check_record_status();
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

void AI::abandon(color_ostream &)
{
    if (!config.random_embark)
        return;
    df::viewscreen_optionst *view = df::allocate<df::viewscreen_optionst>();
    view->options.push_back(df::viewscreen_optionst::Abandon);
    Screen::show(view);
    feed_key(view, interface_key::SELECT);
    feed_key(view, interface_key::MENU_CONFIRM);
    // current view switches to a textviewer at this point
    feed_key(interface_key::SELECT);
}

bool AI::tag_enemies(color_ostream & out)
{
    bool found = false;
    for (auto it = world->units.active.rbegin(); it != world->units.active.rend(); it++)
    {
        df::unit *u = *it;
        df::creature_raw *race = df::creature_raw::find(u->race);
        if (Units::isAlive(u) && Units::getPosition(u).isValid() && !Units::isOwnCiv(u) &&
                !Maps::getTileDesignation(Units::getPosition(u))->bits.hidden &&
                (u->flags1.bits.marauder ||
                 u->flags2.bits.underworld ||
                 u->flags2.bits.visitor_uninvited ||
                 (race &&
                  (race->flags.is_set(creature_raw_flags::CASTE_MEGABEAST) ||
                   race->flags.is_set(creature_raw_flags::CASTE_SEMIMEGABEAST) ||
                   race->flags.is_set(creature_raw_flags::CASTE_FEATURE_BEAST) ||
                   race->flags.is_set(creature_raw_flags::CASTE_TITAN) ||
                   race->flags.is_set(creature_raw_flags::CASTE_UNIQUE_DEMON) ||
                   race->flags.is_set(creature_raw_flags::CASTE_DEMON) ||
                   race->flags.is_set(creature_raw_flags::CASTE_NIGHT_CREATURE_ANY)))))
        {
            if (pop->military_all_squads_attack_unit(out, u))
            {
                found = true;
            }
            // no break
        }
    }
    return found;
}

void AI::timeout_sameview(std::time_t delay, std::function<void(color_ostream &)> cb)
{
    virtual_identity *curscreen = virtual_identity::get(Gui::getCurViewscreen(true));
    std::time_t timeoff = std::time(nullptr) + delay;

    events.onupdate_register_once(std::string("timeout_sameview on ") + curscreen->getName(), [this, curscreen, timeoff, cb](color_ostream & out) -> bool
            {
                if (virtual_identity::get(Gui::getCurViewscreen(true)) != curscreen)
                {
                    if (auto view = strict_virtual_cast<df::viewscreen_movieplayerst>(Gui::getCurViewscreen(true)))
                    {
                        Screen::dismiss(view);
                        camera->check_record_status();
                        return false;
                    }
                    return true;
                }

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
        pause_onupdate = events.onupdate_register_once("df-ai unpause", [this](color_ostream &) -> bool
                {
                    if (std::time(nullptr) < last_unpause + 11)
                        return false;
                    if (*pause_state)
                    {
                        timeout_sameview(10, [](color_ostream &) { AI::unpause(); });
                        last_unpause = std::time(nullptr);
                    }
                    return false;
                });
        tag_enemies_onupdate = events.onupdate_register("df-ai tag_enemies", 7*1200, 7*1200, [this](color_ostream & out) { tag_enemies(out); });
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
        events.onupdate_unregister(tag_enemies_onupdate);
    }
    return res;
}

std::string AI::status()
{
    if (embark->is_embarking())
    {
        return "(embarking)";
    }

    std::ostringstream str;
    str << "Plan: " << plan->status() << "\n";
    str << "Pop: " << pop->status() << "\n";
    str << "Stocks: " << stocks->status() << "\n";
    str << "Camera: " << camera->status();
    return str.str();
}

std::string AI::report()
{
    if (embark->is_embarking())
    {
        return "";
    }

    std::ostringstream str;
    str << "# Plan\n" << plan->report() << "\n";
    str << "# Population\n" << pop->report() << "\n";
    str << "# Stocks\n" << stocks->report();
    return str.str();
}

command_result AI::persist(color_ostream & out)
{
    command_result res = CR_OK;
    if (skip_persist)
        return res;

    if (res == CR_OK)
        res = plan->persist(out);
    return res;
}

command_result AI::unpersist(color_ostream & out)
{
    command_result res = CR_OK;
    if (res == CR_OK)
        res = plan->unpersist(out);
    return res;
}

// vim: et:sw=4:ts=4
