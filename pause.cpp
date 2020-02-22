#include "ai.h"
#include "camera.h"
#include "embark.h"
#include "plan.h"

#include "modules/Gui.h"
#include "modules/Screen.h"

#include "df/d_init.h"
#include "df/report.h"
#include "df/viewscreen_movieplayerst.h"
#include "df/viewscreen_requestagreementst.h"
#include "df/viewscreen_textviewerst.h"
#include "df/viewscreen_topicmeetingst.h"
#include "df/viewscreen_topicmeeting_fill_land_holder_positionsst.h"
#include "df/viewscreen_topicmeeting_takerequestsst.h"
#include "df/world.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(d_init);
REQUIRE_GLOBAL(pause_state);
REQUIRE_GLOBAL(world);

void AI::unpause()
{
    while (!world->status.popups.empty())
    {
        Gui::getCurViewscreen(true)->feed_key(interface_key::CLOSE_MEGA_ANNOUNCEMENT);
    }
    if (*pause_state)
    {
        Gui::getCurViewscreen(true)->feed_key(interface_key::D_PAUSE);
    }
    ignore_pause(last_good_x, last_good_y, last_good_z);
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
        if (!tag_enemies(out))
        {
            debug(out, "[ERROR] could not find megabeast");
        }
        break;
    }
    case announcement_type::BERSERK_CITIZEN:
    case announcement_type::UNDEAD_ATTACK:
    case announcement_type::CAVE_COLLAPSE:
        break;
    case announcement_type::DIG_CANCEL_DAMP:
    case announcement_type::DIG_CANCEL_WARM:
        ignore_pause(last_good_x, last_good_y, last_good_z);
        break;
    case announcement_type::BIRTH_CITIZEN:
    case announcement_type::BIRTH_ANIMAL:
        break;
    case announcement_type::D_MIGRANTS_ARRIVAL:
    case announcement_type::D_MIGRANT_ARRIVAL:
    case announcement_type::MIGRANT_ARRIVAL:
    case announcement_type::NOBLE_ARRIVAL:
    case announcement_type::FORT_POSITION_SUCCESSION:
        plan.make_map_walkable(out); // just in case we missed a frozen river or something during setup
        break;
    case announcement_type::DIPLOMAT_ARRIVAL:
    case announcement_type::LIAISON_ARRIVAL:
    case announcement_type::CARAVAN_ARRIVAL:
    case announcement_type::TRADE_DIPLOMAT_ARRIVAL:
    case announcement_type::STRANGE_MOOD:
    case announcement_type::MOOD_BUILDING_CLAIMED:
    case announcement_type::ARTIFACT_BEGUN:
    case announcement_type::MADE_ARTIFACT:
    case announcement_type::FEATURE_DISCOVERY:
    case announcement_type::STRUCK_DEEP_METAL:
    case announcement_type::TRAINING_FULL_REVERSION:
    case announcement_type::NAMED_ARTIFACT:
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

    if (d_init->announcements.flags[announce->type].bits.DO_MEGA)
    {
        timeout_sameview([this](color_ostream &) { unpause(); });
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
            return d_init->announcements.flags[a->type].bits.PAUSE;
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
            bool space = false;
            std::ostringstream text;
            for (auto span : view->formatted_text)
            {
                if (span->text)
                {
                    if (!space && !span->flags.bits.no_newline)
                    {
                        space = true;
                        text << ' ';
                    }
                    for (char *c = span->text; *c; c++)
                    {
                        if (*c == ' ')
                        {
                            if (!space)
                            {
                                text << ' ';
                                space = true;
                            }
                        }
                        else
                        {
                            text << *c;
                            space = false;
                        }
                    }
                }
            }
            std::string stripped(text.str());

            if (stripped.find("I am your liaison from the Mountainhomes. Let's discuss your situation.") != std::string::npos ||
                stripped.find("I look forward to our meeting next year.") != std::string::npos ||
                stripped.find("A diplomat has left unhappy.") != std::string::npos ||
                stripped.find("What a pleasant surprise! Not a single tree here weeps from the abuses meted out with such ease by your people. Joy! The dwarves have turned a page, not that we would make paper. A travesty! Perhaps it is better said that the dwarves have turned over a new leaf, and the springtime for our two races has only just begun.") != std::string::npos ||
                stripped.find("You have disrespected the trees in this area, but this is what we have come to expect from your stunted kind. Further abuse cannot be tolerated. Let this be a warning to you.") != std::string::npos ||
                stripped.find("Greetings from the woodlands. We have much to discuss.") != std::string::npos ||
                stripped.find("Although we do not always see eye to eye (ha!), I bid you farewell. May you someday embrace nature as you embrace the rocks and mud.") != std::string::npos ||
                stripped.find("Greetings, noble dwarf. There is much to discuss.") != std::string::npos ||
                (stripped.find("It has been an honor, noble") != std::string::npos && stripped.find(". I bid you farewell.") != std::string::npos) ||
                (stripped.find("On behalf of the") != std::string::npos && stripped.find("Guild, let me extend greetings to your people. There is much to discuss.") != std::string::npos) ||
                (stripped.find("Again on behalf of the") != std::string::npos && stripped.find("Guild, let me bid farewell to you and your stout dwarves.") != std::string::npos))
            {
                debug(out, "exit diplomat textviewerst:" + stripped);
                timeout_sameview([](color_ostream &)
                {
                    Gui::getCurViewscreen(true)->feed_key(interface_key::LEAVESCREEN);
                });
            }
            else if (stripped.find("A vile force of darkness has arrived!") != std::string::npos ||
                stripped.find("have brought the full forces of their lands against you.") != std::string::npos ||
                stripped.find("The enemy have come and are laying siege to the fortress.") != std::string::npos ||
                stripped.find("The dead walk. Hide while you still can!") != std::string::npos)
            {
                debug(out, "exit siege textviewerst:" + stripped);
                timeout_sameview([this](color_ostream &)
                {
                    Gui::getCurViewscreen(true)->feed_key(interface_key::LEAVESCREEN);
                    unpause();
                });
            }
            else
            {
                debug(out, "[ERROR] paused in unknown textviewerst:" + stripped);
            }
        }
        else if (strict_virtual_cast<df::viewscreen_topicmeetingst>(curview))
        {
            debug(out, "exit diplomat topicmeetingst");
            timeout_sameview([](color_ostream &)
            {
                Gui::getCurViewscreen(true)->feed_key(interface_key::OPTION1);
            });
        }
        else if (strict_virtual_cast<df::viewscreen_topicmeeting_takerequestsst>(curview))
        {
            debug(out, "exit diplomat topicmeeting_takerequestsst");
            timeout_sameview([](color_ostream &)
            {
                Gui::getCurViewscreen(true)->feed_key(interface_key::LEAVESCREEN);
            });
        }
        else if (strict_virtual_cast<df::viewscreen_requestagreementst>(curview))
        {
            debug(out, "exit diplomat requestagreementst");
            timeout_sameview([](color_ostream &)
            {
                Gui::getCurViewscreen(true)->feed_key(interface_key::LEAVESCREEN);
            });
        }
        else if (strict_virtual_cast<df::viewscreen_topicmeeting_fill_land_holder_positionsst>(curview))
        {
            debug(out, "exit diplomat viewscreen_topicmeeting_fill_land_holder_positionsst");
            timeout_sameview([](color_ostream &)
            {
                Gui::getCurViewscreen(true)->feed_key(interface_key::LEAVESCREEN);
            });
        }
        else if (auto view = strict_virtual_cast<df::viewscreen_movieplayerst>(curview))
        {
            if (!view->is_playing)
            {
                Screen::dismiss(curview);
                camera.check_record_status();
            }
        }
        else if (auto hack = dfhack_viewscreen::try_cast(curview))
        {
            std::string focus = hack->getFocusString();
            if (focus == "lua/status_overlay")
            {
                debug(out, "dismissing gui/extended-status overlay");
                Screen::dismiss(hack);
            }
            else if (focus == "lua/warn-starving")
            {
                debug(out, "exit warn-starving dialog");
                timeout_sameview([](color_ostream &)
                {
                    Gui::getCurViewscreen(true)->feed_key(interface_key::LEAVESCREEN);
                });
            }
            else if (seen_focus.insert(focus).second)
            {
                debug(out, "[ERROR] paused in unknown DFHack viewscreen " + focus);
            }
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
