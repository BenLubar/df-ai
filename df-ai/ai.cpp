#include "ai.h"

#include <set>

#include "modules/Gui.h"
#include "modules/Translation.h"
#include "modules/Units.h"

#include "df/announcements.h"
#include "df/announcement_type.h"
#include "df/interface_key.h"
#include "df/report.h"
#include "df/viewscreen.h"
#include "df/viewscreen_requestagreementst.h"
#include "df/viewscreen_textviewerst.h"
#include "df/viewscreen_topicmeetingst.h"
#include "df/viewscreen_topicmeeting_takerequestsst.h"
#include "df/world.h"

REQUIRE_GLOBAL(announcements);
REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(pause_state);
REQUIRE_GLOBAL(world);

AI::AI(color_ostream & out) :
    rng(0),
    pop(out, this),
    plan(out, this),
    stocks(out, this),
    camera(out, this),
    embark(out, this),
    unpause_delay(0)
{
}

#define CHECK_RESULT(res, mod, fn) \
    res = mod.fn; \
    if (res != CR_OK) \
        return res

#define CHECK_RESULTS(fn) \
    command_result res; \
    CHECK_RESULT(res, pop, fn); \
    CHECK_RESULT(res, plan, fn); \
    CHECK_RESULT(res, stocks, fn); \
    CHECK_RESULT(res, camera, fn); \
    CHECK_RESULT(res, embark, fn); \
    return res

command_result AI::status(color_ostream & out)
{
    CHECK_RESULTS(status(out));
}

command_result AI::statechange(color_ostream & out, state_change_event event)
{
    check_unpause(out, event);
    CHECK_RESULTS(statechange(out, event));
}

command_result AI::update(color_ostream & out)
{
    if (unpause_delay && unpause_delay <= std::time(nullptr))
    {
        unpause_delay = 0;
        std::set<df::interface_key> keys;
        keys.insert(interface_key::LEAVESCREEN);
        keys.insert(interface_key::OPTION1);
        Gui::getCurViewscreen()->feed(&keys);
        unpause(out);
    }
    CHECK_RESULTS(update(out));
}

void AI::debug(color_ostream & out, const std::string & str)
{
    if (*cur_year == 0 && *cur_year_tick == 0)
    {
        // split up the string so trigraphs don't do weird things.
        out.print("[AI] ?????" "-??" "-??:???? %s\n", str.c_str());
    }
    else
    {
        out.print("[AI] %05d-%02d-%02d:%04d %s\n",
                *cur_year,
                *cur_year_tick / 50 / 24 / 28 + 1,
                *cur_year_tick / 50 / 24 % 28 + 1,
                *cur_year_tick % (24 * 50),
                str.c_str());
    }
}

void AI::unpause(color_ostream & out)
{
    std::set<df::interface_key> keys;
    keys.insert(interface_key::CLOSE_MEGA_ANNOUNCEMENT);
    while (!world->status.popups.empty())
    {
        Gui::getCurViewscreen()->feed(&keys);
    }
    if (*pause_state)
    {
        keys.clear();
        keys.insert(interface_key::D_PAUSE);
        Gui::getCurViewscreen()->feed(&keys);
    }
}

void AI::handle_pause_event(color_ostream & out, std::vector<df::report *>::reverse_iterator ann, std::vector<df::report *>::reverse_iterator end)
{
    // unsplit announce text
    df::report *announce = *ann;
    std::string fulltext = announce->text;
    while (announce->flags.bits.continuation && ann != end)
    {
        ann++;
        announce = *ann;
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
        case announcement_type::AMBUSH_DEFENDER:
        case announcement_type::AMBUSH_RESIDENT:
        case announcement_type::AMBUSH_THIEF:
        case announcement_type::AMBUSH_THIEF_SUPPORT_SKULKING:
        case announcement_type::AMBUSH_THIEF_SUPPORT_NATURE:
        case announcement_type::AMBUSH_THIEF_SUPPORT:
        case announcement_type::AMBUSH_MISCHIEVOUS:
        case announcement_type::AMBUSH_SNATCHER:
        case announcement_type::AMBUSH_SNATCHER_SUPPORT:
        case announcement_type::AMBUSH_AMBUSHER_NATURE:
        case announcement_type::AMBUSH_AMBUSHER:
        case announcement_type::AMBUSH_INJURED:
        case announcement_type::AMBUSH_OTHER:
        case announcement_type::AMBUSH_INCAPACITATED:
            debug(out, "pause: an ambush!");
            break;
        default:
            debug(out, "pause: unhandled pausing event");
            break;
    }

    if (announcements->flags[announce->type].bits.DO_MEGA)
    {
        unpause_delay = std::time(nullptr) + 5;
    }
    else
    {
        unpause(out);
    }
}

static bool is_pause_announcement_flag(df::report *ann)
{
    return ann->pos.isValid() && announcements->flags[ann->type].bits.PAUSE;
}

static void clean_text(std::string & text)
{
    bool remove = true;
    auto dest = text.begin();
    for (auto src : text)
    {
        if (src == ' ')
        {
            if (!remove)
            {
                remove = true;
                *(dest++) = src;
            }
        }
        else
        {
            remove = false;
            *(dest++) = src;
        }
    }
    text.erase(dest, text.end());
}

static bool contains(const std::string & text, const std::string & sub)
{
    return text.find(sub) != std::string::npos;
}

void AI::check_unpause(color_ostream & out, state_change_event event)
{
    if (unpause_delay)
        return;

    // automatically unpause the game (only for game-generated pauses)
    switch (event)
    {
        case SC_PAUSED:
        {
            auto ann = std::find_if(world->status.announcements.rbegin(), world->status.announcements.rend(), is_pause_announcement_flag);
            if (ann != world->status.announcements.rend() && (*ann)->year == *cur_year && (*ann)->time == *cur_year_tick)
            {
                handle_pause_event(out, ann, world->status.announcements.rend());
                return;
            }
            debug(out, "pause without an event");
            unpause(out);
            return;
        }
        case SC_VIEWSCREEN_CHANGED:
        {
            df::viewscreen *view = Gui::getCurViewscreen();

            df::viewscreen_textviewerst *textviewer = strict_virtual_cast<df::viewscreen_textviewerst>(view);
            if (textviewer)
            {
                std::string text;
                for (auto t : textviewer->formatted_text)
                {
                    text += t->text;
                }
                clean_text(text);

                if (contains(text, "I am your liaison from the Mountainhomes. Let's discuss your situation.") ||
                        (contains(text, "Farewell, ") && contains(text, "I look forward to our meeting next year.")) ||
                        contains(text, "A diplomat has left unhappy.") ||
                        contains(text, "You have disrespected the trees in this area, but this is what we have come to expect from your stunted kind. Further abuse cannot be tolerated. Let this be a warning to you.") ||
                        contains(text, "Greetings from the woodlands. We have much to discuss.") ||
                        contains(text, "Although we do not always see eye to eye (ha!), I bid you farwell. May you someday embrace nature as you embrace the rocks and mud."))
                {
                    debug(out, "diplomat: " + text);
                    unpause_delay = std::time(nullptr) + 5;
                    return;
                }

                if (contains(text, "A vile force of darkness has arrived!") ||
                        contains(text, " have brought the full forces of their lands against you.") ||
                        contains(text, "The enemy have come and are laying siege to the fortress.") ||
                        contains(text, "The dead walk. Hide while you still can!"))
                {
                    debug(out, "siege: " + text);
                    unpause_delay = std::time(nullptr) + 5;
                    return;
                }

                if (contains(text, "Your strength has been broken.") ||
                        contains(text, "Your settlement has crumbled to its end.") ||
                        contains(text, "Your settlement has been abandoned."))
                {
                    debug(out, "you just lost the game: " + text);
                    unpause_delay = std::time(nullptr) + 5;
                    return;
                }

                debug(out, "paused in unknown textviewerst: " + text);
                unpause_delay = std::time(nullptr) + 15;
                return;
            }

            df::viewscreen_topicmeetingst *meeting = strict_virtual_cast<df::viewscreen_topicmeetingst>(view);
            if (meeting)
            {
                std::string text;
                for (auto t : meeting->text)
                {
                    text += *t;
                }
                clean_text(text);

                debug(out, "diplomat (meeting): " + text);
                unpause_delay = std::time(nullptr) + 5;
                return;
            }

            df::viewscreen_topicmeeting_takerequestsst *requests = strict_virtual_cast<df::viewscreen_topicmeeting_takerequestsst>(view);
            if (requests)
            {
                debug(out, "diplomat (requests)");
                unpause_delay = std::time(nullptr) + 5;
                return;
            }

            df::viewscreen_requestagreementst *agreement = strict_virtual_cast<df::viewscreen_requestagreementst>(view);
            if (agreement)
            {
                debug(out, "diplomat (agreement)");
                unpause_delay = std::time(nullptr) + 5;
                return;
            }

            debug(out, "paused in unknown viewscreen");
            unpause_delay = std::time(nullptr) + 15;
            return;
        }
        default:
            return;
    }
}

std::string AI::describe_unit(df::unit *unit)
{
    return Translation::TranslateName(Units::getVisibleName(unit), false) +
        ", " + Units::getProfessionName(unit);
}

/*
def abandon!(view=df.curview)
    return unless $AI_RANDOM_EMBARK
    view.child = DFHack::ViewscreenOptionst.cpp_new(:parent => view, :options => [6])
    view = view.child
    view.feed_keys(:SELECT)
    view.feed_keys(:MENU_CONFIRM)
    # current view switches to a textviewer at this point
    df.curview.feed_keys(:SELECT)
end
*/

// vim: et:sw=4:ts=4
