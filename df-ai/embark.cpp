#include "ai.h"
#include "camera.h"
#include "embark.h"
#include "event_manager.h"

#include "modules/Gui.h"
#include "modules/Screen.h"
#include "modules/Translation.h"

#include "df/region_map_entry.h"
#include "df/viewscreen_choose_start_sitest.h"
#include "df/viewscreen_new_regionst.h"
#include "df/viewscreen_setupdwarfgamest.h"
#include "df/viewscreen_textviewerst.h"
#include "df/viewscreen_titlest.h"
#include "df/viewscreen_update_regionst.h"
#include "df/world.h"
#include "df/world_data.h"

REQUIRE_GLOBAL(standing_orders_job_cancel_announce);
REQUIRE_GLOBAL(world);

std::string AI_RANDOM_EMBARK_WORLD = "region1";

Embark::Embark(AI *ai) :
    ai(ai),
    selected_embark(false)
{
    if (AI_RANDOM_EMBARK)
    {
        events.onupdate_register_once("df-ai random_embark", [this](color_ostream & out) -> bool
                {
                    return update(out);
                });
    }
}

Embark::~Embark()
{
}

command_result Embark::startup(color_ostream & out)
{
    // do nothing
    return CR_OK;
}

command_result Embark::onupdate_register(color_ostream & out)
{
    // do nothing
    return CR_OK;
}

command_result Embark::onupdate_unregister(color_ostream & out)
{
    if (AI_RANDOM_EMBARK)
    {
        ai->debug(out, "game over. restarting in 1 minute.");
        ai->timeout_sameview(60, [this](color_ostream & out)
                {
                    ai->debug(out, "restarting.");
                    interface_key_set keys;
                    keys.insert(interface_key::LEAVESCREEN);
                    Gui::getCurViewscreen()->feed(&keys);

                    events.onupdate_register_once("df-ai restart wait", [this](color_ostream & out) -> bool
                            {
                                if (!strict_virtual_cast<df::viewscreen_titlest>(Gui::getCurViewscreen()))
                                {
                                    return false;
                                }

                                if (!NO_QUIT)
                                {
                                    Gui::getCurViewscreen()->breakdown_level = interface_breakdown_types::QUIT;
                                    return true;
                                }

                                extern bool full_reset_requested;
                                full_reset_requested = true;
                                return true;
                            });
                });
    }
    return CR_OK;
}

bool Embark::update(color_ostream & out)
{
    df::viewscreen *curview = Gui::getCurViewscreen();
    if (!curview || curview->breakdown_level != interface_breakdown_types::NONE)
        return false;

    interface_key_set keys;
    if (df::viewscreen_titlest *view = strict_virtual_cast<df::viewscreen_titlest>(curview))
    {
        ai->camera->check_record_status();
        switch (view->sel_subpage)
        {
            case df::viewscreen_titlest::None:
                {
                    auto start_game = std::find(view->menu_line_id.begin(), view->menu_line_id.end(), df::viewscreen_titlest::Start);
                    if (!AI_RANDOM_EMBARK_WORLD.empty() && start_game != view->menu_line_id.end())
                    {
                        ai->debug(out, "choosing \"Start Game\"");
                        view->sel_menu_line = start_game - view->menu_line_id.begin();
                    }
                    else
                    {
                        ai->debug(out, "choosing \"New World\"");
                        view->sel_menu_line = std::find(view->menu_line_id.begin(), view->menu_line_id.end(), df::viewscreen_titlest::NewWorld) - view->menu_line_id.begin();
                    }
                    keys.insert(interface_key::SELECT);
                    view->feed(&keys);
                    break;
                }
            case df::viewscreen_titlest::StartSelectWorld:
                {
                    if (AI_RANDOM_EMBARK_WORLD.empty())
                    {
                        ai->debug(out, "leaving \"Select World\" (no save name)");
                        keys.insert(interface_key::LEAVESCREEN);
                        view->feed(&keys);
                        return false;
                    }
                    auto save = std::find_if(view->start_savegames.begin(), view->start_savegames.end(), [](df::viewscreen_titlest::T_start_savegames *s) -> bool
                            {
                                return s->save_dir == AI_RANDOM_EMBARK_WORLD;
                            });
                    if (save != view->start_savegames.end())
                    {
                        ai->debug(out, stl_sprintf("selecting save #%d (%s, %s)",
                                    save - view->start_savegames.begin(),
                                    (*save)->world_name_str.c_str(),
                                    Translation::TranslateName(&(*save)->world_name, true).c_str()));
                        view->sel_submenu_line = save - view->start_savegames.begin();
                        keys.insert(interface_key::SELECT);
                    }
                    else
                    {
                        ai->debug(out, "could not find save named " + AI_RANDOM_EMBARK_WORLD);
                        AI_RANDOM_EMBARK_WORLD = "";
                        keys.insert(interface_key::LEAVESCREEN);
                    }
                    view->feed(&keys);
                    break;
                }
            case df::viewscreen_titlest::StartSelectMode:
                {
                    auto fortress_mode = std::find(view->submenu_line_id.begin(), view->submenu_line_id.end(), 0);
                    if (fortress_mode != view->submenu_line_id.end())
                    {
                        ai->debug(out, "choosing \"Dwarf Fortress Mode\"");
                        view->sel_menu_line = fortress_mode - view->submenu_line_id.begin();
                        keys.insert(interface_key::SELECT);
                    }
                    else
                    {
                        ai->debug(out, "leaving \"Select Mode\" (no fortress mode available)");
                        AI_RANDOM_EMBARK_WORLD = "";
                        keys.insert(interface_key::LEAVESCREEN);
                    }
                    view->feed(&keys);
                    break;
                }
            default:
                break;
        }
    }
    else if (df::viewscreen_new_regionst *view = strict_virtual_cast<df::viewscreen_new_regionst>(curview))
    {
        if (!AI_RANDOM_EMBARK_WORLD.empty())
            return false;

        if (!view->welcome_msg.empty())
        {
            ai->debug(out, "leaving world gen disclaimer");
            keys.insert(interface_key::LEAVESCREEN);
            view->feed(&keys);
        }
        else if (world->worldgen_status.state == 0)
        {
            ai->debug(out, "choosing \"Generate World\"");
            view->world_size = 1;
            keys.insert(interface_key::MENU_CONFIRM);
            view->feed(&keys);
        }
        else if (world->worldgen_status.state == 10)
        {
            ai->debug(out, "world gen finished, save name is " + world->cur_savegame.save_dir);
            AI_RANDOM_EMBARK_WORLD = world->cur_savegame.save_dir;
            keys.insert(interface_key::SELECT);
            view->feed(&keys);
        }
    }
    else if (df::viewscreen_update_regionst *view = strict_virtual_cast<df::viewscreen_update_regionst>(curview))
    {
        ai->debug(out, "updating world, goal: " + AI::timestamp(view->year, view->year_tick));
    }
    else if (df::viewscreen_choose_start_sitest *view = strict_virtual_cast<df::viewscreen_choose_start_sitest>(curview))
    {
        if (view->finder.finder_state == -1)
        {
            ai->debug(out, "choosing \"Site Finder\"");
            keys.insert(interface_key::SETUP_FIND);
            view->feed(&keys);
            keys.clear();
            view->finder.options[embark_finder_option::DimensionX] = 3;
            view->finder.options[embark_finder_option::DimensionY] = 2;
            view->finder.options[embark_finder_option::Aquifer] = 0;
            view->finder.options[embark_finder_option::River] = 1;
            view->finder.options[embark_finder_option::Savagery] = 2;
            keys.insert(interface_key::SELECT);
            view->feed(&keys);
        }
        else if (view->finder.search_x == -1)
        {
            if (selected_embark)
            {
                return false;
            }
            else if (view->finder.finder_state == 2)
            {
                ai->debug(out, "choosing \"Embark\"");
                keys.insert(interface_key::LEAVESCREEN);
                view->feed(&keys);
                keys.clear();

                df::coord2d start = view->location.region_pos;
                std::vector<df::coord2d> sites;
                for (int16_t x = 0; x < world->world_data->world_width; x++)
                {
                    for (int16_t y = 0; y < world->world_data->world_height; y++)
                    {
                        if (world->world_data->region_map[x][y].finder_rank >= 10000)
                        {
                            sites.push_back(df::coord2d(x, y));
                        }
                    }
                }
                assert(!sites.empty());
                ai->debug(out, stl_sprintf("found sites count: %d", sites.size()));
                df::coord2d site = sites[std::uniform_int_distribution<size_t>(0, sites.size() - 1)(ai->rng)];
                df::coord2d diff = site - start;

                if (diff.x >= 0)
                {
                    for (int16_t x = 0; x < diff.x; x++)
                    {
                        keys.insert(interface_key::CURSOR_RIGHT);
                        view->feed(&keys);
                        keys.clear();
                    }
                }
                else
                {
                    for (int16_t x = 0; x > diff.x; x--)
                    {
                        keys.insert(interface_key::CURSOR_LEFT);
                        view->feed(&keys);
                        keys.clear();
                    }
                }

                if (diff.y >= 0)
                {
                    for (int16_t y = 0; y < diff.y; y++)
                    {
                        keys.insert(interface_key::CURSOR_DOWN);
                        view->feed(&keys);
                        keys.clear();
                    }
                }
                else
                {
                    for (int16_t y = 0; y > diff.y; y--)
                    {
                        keys.insert(interface_key::CURSOR_UP);
                        view->feed(&keys);
                        keys.clear();
                    }
                }

                selected_embark = true;

                ai->timeout_sameview(15, [](color_ostream & out)
                        {
                            df::viewscreen *view = Gui::getCurViewscreen();
                            interface_key_set keys;
                            keys.insert(interface_key::SETUP_EMBARK);
                            view->feed(&keys);
                            keys.clear();
                            // dismiss warnings
                            keys.insert(interface_key::SELECT);
                            view->feed(&keys);
                        });
            }
            else
            {
                ai->debug(out, "leaving embark selector (no good embarks)");
                AI_RANDOM_EMBARK_WORLD = "";
                view->breakdown_level = interface_breakdown_types::QUIT; // XXX
            }
        }
        else
        {
            ai->debug(out, stl_sprintf("searching for a site (%d/%d, %d/%d)",
                        view->finder.search_x,
                        world->world_data->world_width / 16,
                        view->finder.search_y,
                        world->world_data->world_height / 16));
        }
    }
    else if (df::viewscreen_setupdwarfgamest *view = strict_virtual_cast<df::viewscreen_setupdwarfgamest>(curview))
    {
        ai->debug(out, "choosing \"Play Now\"");
        keys.insert(interface_key::SELECT);
        view->feed(&keys);
        // TODO custom embark loadout
    }
    else if (df::viewscreen_textviewerst *view = strict_virtual_cast<df::viewscreen_textviewerst>(curview))
    {
        ai->debug(out, "site is ready.");
        ai->timeout_sameview([this](color_ostream & out)
                {
                    ai->debug(out, "disabling minimap.");
                    interface_key_set keys;
                    keys.insert(interface_key::LEAVESCREEN);
                    Gui::getCurViewscreen()->feed(&keys);
                    Gui::setMenuWidth(3, 3);
                    if (!DEBUG)
                    {
                        *standing_orders_job_cancel_announce = 0;
                    }
                });
        return true;
    }
    return false;
}

// vim: et:sw=4:ts=4
