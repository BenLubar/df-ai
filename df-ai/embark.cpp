#include "ai.h"

#include <sstream>
#include <ctime>

#include "modules/Gui.h"

#include "df/embark_finder_option.h"
#include "df/interface_breakdown_types.h"
#include "df/interface_key.h"
#include "df/region_map_entry.h"
#include "df/viewscreen.h"
#include "df/viewscreen_choose_start_sitest.h"
#include "df/viewscreen_new_regionst.h"
#include "df/viewscreen_setupdwarfgamest.h"
#include "df/viewscreen_textviewerst.h"
#include "df/viewscreen_titlest.h"
#include "df/world.h"
#include "df/world_data.h"

REQUIRE_GLOBAL(world);
REQUIRE_GLOBAL(ui_area_map_width);
REQUIRE_GLOBAL(ui_menu_width);
REQUIRE_GLOBAL(standing_orders_job_cancel_announce);

Embark::Embark(color_ostream & out, AI *parent) :
    ai(parent),
    embarking(false),
    world_name(""),
    timeout(0)
{
}

Embark::~Embark()
{
}

command_result Embark::status(color_ostream & out)
{
    return CR_OK;
}

command_result Embark::statechange(color_ostream & out, state_change_event event)
{
    return CR_OK;
}

command_result Embark::update(color_ostream & out)
{
    if (!embarking)
        return CR_OK;

    df::viewscreen *view = Gui::getCurViewscreen();
    if (!view || view->breakdown_level != interface_breakdown_types::NONE)
        return CR_OK;

    std::set<df::interface_key> keys;

    df::viewscreen_titlest *title = strict_virtual_cast<df::viewscreen_titlest>(view);
    if (title)
    {
        switch (title->sel_subpage)
        {
            case df::viewscreen_titlest::T_sel_subpage::None:
            {
                auto start = std::find(title->menu_line_id.begin(), title->menu_line_id.end(), df::viewscreen_titlest::T_menu_line_id::Start);
                if (!world_name.empty() && start != title->menu_line_id.end())
                {
                    title->sel_menu_line = start - title->menu_line_id.begin();
                    ai->debug(out, "choosing 'Start Game'");
                }
                else
                {
                    world_name = "";
                    auto create = std::find(title->menu_line_id.begin(), title->menu_line_id.end(), df::viewscreen_titlest::T_menu_line_id::NewWorld);
                    title->sel_menu_line = create - title->menu_line_id.begin();
                    ai->debug(out, "choosing 'New World'");
                }
                keys.insert(interface_key::SELECT);
                title->feed(&keys);
                return CR_OK;
            }
            case df::viewscreen_titlest::T_sel_subpage::StartSelectWorld:
            {
                if (world_name.empty())
                {
                    ai->debug(out, "leaving 'Select World' (no save name)");
                    keys.insert(interface_key::LEAVESCREEN);
                    title->feed(&keys);
                    return CR_OK;
                }
        
                keys.insert(interface_key::SELECT);
                title->feed(&keys);
                return CR_OK;
            }
            case df::viewscreen_titlest::T_sel_subpage::StartSelectMode:
            {
                auto fortress = std::find(title->submenu_line_id.begin(), title->submenu_line_id.end(), 0);
                if (!world_name.empty() && fortress != title->submenu_line_id.end())
                {
                    title->sel_submenu_line = fortress - title->submenu_line_id.begin();
                    ai->debug(out, "choosing 'Dwarf Fortress Mode'");
                    keys.insert(interface_key::SELECT);
                    title->feed(&keys);
                    return CR_OK;
                }
                ai->debug(out, "leaving 'Select Mode' (no fortress mode available)");
                world_name = "";
                keys.insert(interface_key::LEAVESCREEN);
                title->feed(&keys);
                return CR_OK;
            }
            default:
                return CR_OK;
        }
    }
    df::viewscreen_new_regionst *generate = strict_virtual_cast<df::viewscreen_new_regionst>(view);
    if (generate)
    {
        if (!world_name.empty())
            return CR_OK;

        if (!generate->welcome_msg.empty())
        {
            ai->debug(out, "leaving world gen disclaimer");
            keys.insert(interface_key::LEAVESCREEN);
            generate->feed(&keys);
            return CR_OK;
        }

        if (world->worldgen_status.state == 0)
        {
            ai->debug(out, "choosing 'Generate World'");
            generate->world_size = 1;
            keys.insert(interface_key::MENU_CONFIRM);
            generate->feed(&keys);
            return CR_OK;
        }

        if (world->worldgen_status.state == 0)
        {
            world_name = world->cur_savegame.save_dir;
            ai->debug(out, "world gen finished, save name is " + world_name);
            keys.insert(interface_key::SELECT);
            generate->feed(&keys);
            return CR_OK;
        }

        return CR_OK;
    }
    df::viewscreen_choose_start_sitest *start = strict_virtual_cast<df::viewscreen_choose_start_sitest>(view);
    if (start)
    {
        if (start->finder.finder_state == -1)
        {
            ai->debug(out, "choosing 'Site Finder'");
            keys.insert(interface_key::SETUP_FIND);
            start->feed(&keys);
            start->finder.options[embark_finder_option::DimensionX] = 3;
            start->finder.options[embark_finder_option::DimensionY] = 2;
            start->finder.options[embark_finder_option::Aquifer] = 0;
            start->finder.options[embark_finder_option::River] = 1;
            start->finder.options[embark_finder_option::Savagery] = 2;
            keys.clear();
            keys.insert(interface_key::SELECT);
            start->feed(&keys);
            return CR_OK;
        }

        if (start->finder.search_x == -1)
        {
            if (timeout)
            {
                if (std::time(nullptr) < timeout)
                    return CR_OK;
                keys.insert(interface_key::SETUP_EMBARK);
                start->feed(&keys);
                // dismiss embark warnings
                keys.clear();
                keys.insert(interface_key::SELECT);
                start->feed(&keys);
                timeout = 0;
                return CR_OK;
            }

            if (start->finder.finder_state == 2)
            {
                ai->debug(out, "choosing 'Embark'");
                keys.insert(interface_key::LEAVESCREEN);
                start->feed(&keys);

                df::coord2d source = start->location.region_pos;

                std::vector<df::coord2d> sites;
                for (int16_t x = 0; x < world->world_data->world_width; x++)
                {
                    for (int16_t y = 0; y < world->world_data->world_height; y++)
                    {
                        int32_t score = world->world_data->region_map[x][y].finder_rank;
                        if (score >= 10000)
                        {
                            sites.push_back(df::coord2d(x, y));
                        }
                    }
                }
                if (sites.empty())
                {
                    out.printerr("no good embarks, but site finder exited with success\n");
                    return CR_FAILURE;
                }
                df::coord2d dest = sites[std::uniform_int_distribution<std::size_t>(0, sites.size() - 1)(ai->rng)];
                dest.x -= source.x;
                dest.y -= source.y;

                if (dest.x >= 0)
                {
                    for (int16_t i = 0; i < dest.x; i++)
                    {
                        keys.clear();
                        keys.insert(interface_key::CURSOR_RIGHT);
                        start->feed(&keys);
                    }
                }
                else
                {
                    for (int16_t i = 0; i > dest.x; i--)
                    {
                        keys.clear();
                        keys.insert(interface_key::CURSOR_LEFT);
                        start->feed(&keys);
                    }
                }

                if (dest.y >= 0)
                {
                    for (int16_t i = 0; i < dest.y; i++)
                    {
                        keys.clear();
                        keys.insert(interface_key::CURSOR_DOWN);
                        start->feed(&keys);
                    }
                }
                else
                {
                    for (int16_t i = 0; i > dest.y; i--)
                    {
                        keys.clear();
                        keys.insert(interface_key::CURSOR_UP);
                        start->feed(&keys);
                    }
                }

                timeout = std::time(nullptr) + 15;
                ai->debug(out, "embarking in 15s");
                return CR_OK;
            }

            ai->debug(out, "leaving embark selector (no good embarks)");
            world_name = "";
            start->breakdown_level = interface_breakdown_types::QUIT;
            return CR_OK;
        }

        std::ostringstream msg;
        msg << "searching for a site (";
        msg << start->finder.search_x;
        msg << "/";
        msg << (world->world_data->world_width / 16);
        msg << ", ";
        msg << start->finder.search_y;
        msg << "/";
        msg << (world->world_data->world_height / 16);
        msg << ")";
        ai->debug(out, msg.str());
        return CR_OK;
    }
    df::viewscreen_setupdwarfgamest *setup = strict_virtual_cast<df::viewscreen_setupdwarfgamest>(view);
    if (setup)
    {
        ai->debug(out, "choosing 'Play Now'");
        keys.insert(interface_key::SELECT);
        setup->feed(&keys);
        return CR_OK;
    }
    df::viewscreen_textviewerst *text = strict_virtual_cast<df::viewscreen_textviewerst>(view);
    if (text)
    {
        if (timeout == 0)
        {
            timeout = std::time(nullptr) + 10;
            ai->debug(out, "starting in 10s");
            return CR_OK;
        }
        if (std::time(nullptr) < timeout)
            return CR_OK;

        embarking = false;
        timeout = 0;
        ai->debug(out, "site is ready. disabling minimap.");
        keys.insert(interface_key::LEAVESCREEN);
        text->feed(&keys);
        *ui_area_map_width = 3;
        *ui_menu_width = 3;
        *standing_orders_job_cancel_announce = 0;
        return CR_OK;
    }
    return CR_OK;
}

// vim: et:sw=4:ts=4
