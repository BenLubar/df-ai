#include "ai.h"
#include "camera.h"
#include "embark.h"
#include "event_manager.h"
#include "hooks.h"

#pragma GCC diagnostic push 
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "LuaTools.h"
#pragma GCC diagnostic pop

#include "modules/Gui.h"
#include "modules/Screen.h"
#include "modules/Translation.h"

#include "df/region_map_entry.h"
#include "df/viewscreen_adopt_regionst.h"
#include "df/viewscreen_choose_start_sitest.h"
#include "df/viewscreen_dwarfmodest.h"
#include "df/viewscreen_export_regionst.h"
#include "df/viewscreen_game_cleanerst.h"
#include "df/viewscreen_loadgamest.h"
#include "df/viewscreen_movieplayerst.h"
#include "df/viewscreen_new_regionst.h"
#include "df/viewscreen_setupdwarfgamest.h"
#include "df/viewscreen_textviewerst.h"
#include "df/viewscreen_titlest.h"
#include "df/viewscreen_update_regionst.h"
#include "df/world.h"
#include "df/world_data.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(standing_orders_gather_refuse_outside);
REQUIRE_GLOBAL(standing_orders_job_cancel_announce);
REQUIRE_GLOBAL(world);

EmbarkExclusive::EmbarkExclusive(AI *ai) :
    ExclusiveCallback("embarking"),
    ai(ai)
{
}

EmbarkExclusive::~EmbarkExclusive()
{
}

template<typename T>
static bool viewscreen_is()
{
    return strict_virtual_cast<T>(Gui::getCurViewscreen(false)) != nullptr;
}

static inline std::function<bool()> viewscreen_is(const std::string & focus)
{
    return [focus]() -> bool
    {
        return Gui::getCurFocus(false) == focus;
    };
}

template<typename T>
static bool viewscreen_is_not()
{
    return strict_virtual_cast<T>(Gui::getCurViewscreen(false)) == nullptr;
}

static inline std::function<bool()> viewscreen_is_not(const std::string & focus)
{
    return [focus]() -> bool
    {
        return Gui::getCurFocus(false) != focus;
    };
}

void EmbarkExclusive::Run(color_ostream & out)
{
    df::viewscreen *curview = Gui::getCurViewscreen(false);
    if (!curview || curview->breakdown_level != interface_breakdown_types::NONE)
    {
        throw wait_for_next_frame();
    }

    While(viewscreen_is_not<df::viewscreen_dwarfmodest>, [&]()
    {
        Do([&]()
        {
            unknown_screen = true;
        });

        Delay();

        If(viewscreen_is<df::viewscreen_adopt_regionst>, [&]()
        {
            unknown_screen = false;
        });

        If(viewscreen_is<df::viewscreen_export_regionst>, [&]()
        {
            unknown_screen = false;
        });

        If(viewscreen_is<df::viewscreen_game_cleanerst>, [&]()
        {
            unknown_screen = false;
        });

        If(viewscreen_is<df::viewscreen_movieplayerst>, [&]()
        {
            unknown_screen = false;
            Key(interface_key::LEAVESCREEN);
        });

        If(viewscreen_is<df::viewscreen_titlest>, [&]()
        {
            unknown_screen = false;
            df::viewscreen_titlest *view = strict_virtual_cast<df::viewscreen_titlest>(curview);
            if (!view)
            {
                return;
            }

            Do([&]()
            {
                ai->camera->check_record_status();
            });

            If([&]() -> bool { return view->sel_subpage == df::viewscreen_titlest::None; }, [&]()
            {
                auto continue_game = std::find(view->menu_line_id.begin(), view->menu_line_id.end(), df::viewscreen_titlest::Continue);
                If([&]() -> bool
                {
                    return !config.random_embark_world.empty() && continue_game != view->menu_line_id.end() && std::ifstream("data/save/" + config.random_embark_world + "/world.sav").good();
                }, [&]()
                {
                    Do([&]()
                    {
                        ai->debug(out, "choosing \"Continue Game\"");
                    });

                    SelectVerticalMenuItem(view->sel_menu_line, int32_t(continue_game - view->menu_line_id.begin()));
                }, [&]()
                {
                    auto start_game = std::find(view->menu_line_id.begin(), view->menu_line_id.end(), df::viewscreen_titlest::Start);
                    If([&]() -> bool
                    {
                        return !config.random_embark_world.empty() && start_game != view->menu_line_id.end() && std::ifstream("data/save/" + config.random_embark_world + "/world.dat").good();
                    }, [&]()
                    {
                        Do([&]()
                        {
                            ai->debug(out, "choosing \"Start Game\"");
                        });

                        SelectVerticalMenuItem(view->sel_menu_line, int32_t(start_game - view->menu_line_id.begin()));
                    }, [&]()
                    {
                        auto new_world = std::find(view->menu_line_id.begin(), view->menu_line_id.end(), df::viewscreen_titlest::NewWorld);

                        Do([&]()
                        {
                            ai->debug(out, "choosing \"New World\"");
                        });

                        SelectVerticalMenuItem(view->sel_menu_line, int32_t(new_world - view->menu_line_id.begin()));
                    });
                });
            });

            If([&]() -> bool { return view->sel_subpage == df::viewscreen_titlest::StartSelectWorld; }, [&]()
            {
                If([&]() -> bool { return config.random_embark_world.empty(); }, [&]()
                {
                    Do([&]()
                    {
                        ai->debug(out, "leaving \"Select World\" (no save name)");
                        Key(interface_key::LEAVESCREEN);
                    });
                }, [&]()
                {
                    auto save = std::find_if(view->start_savegames.begin(), view->start_savegames.end(), [](df::viewscreen_titlest::T_start_savegames *s) -> bool
                    {
                        return s->save_dir == config.random_embark_world;
                    });

                    If([&]() -> bool { return save == view->start_savegames.end(); }, [&]()
                    {
                        Do([&]()
                        {
                            ai->debug(out, "could not find a save named " + config.random_embark_world);
                            config.set(out, config.random_embark_world, std::string());
                        });

                        Key(interface_key::LEAVESCREEN);
                    }, [&]()
                    {
                        Do([&]()
                        {
                            ai->debug(out, stl_sprintf("selecting save #%d (%s)",
                                (save - view->start_savegames.begin()) + 1,
                                (*save)->world_name_str.c_str()));
                        });

                        SelectVerticalMenuItem(view->sel_submenu_line, int32_t(save - view->start_savegames.begin()));
                    });
                });
            });

            If([&]() -> bool { return view->sel_subpage == df::viewscreen_titlest::StartSelectMode; }, [&]()
            {
                auto fortress_mode = std::find(view->submenu_line_id.begin(), view->submenu_line_id.end(), 0);
                If([&]() -> bool { return fortress_mode == view->submenu_line_id.end(); }, [&]()
                {
                    Do([&]()
                    {
                        ai->debug(out, "leaving \"Select Mode\" (no fortress mode available)");
                        config.set(out, config.random_embark_world, std::string());
                    });

                    Key(interface_key::LEAVESCREEN);
                }, [&]()
                {
                    Do([&]()
                    {
                        ai->debug(out, "choosing \"Dwarf Fortress Mode\"");
                    });

                    SelectVerticalMenuItem(view->sel_menu_line, int32_t(fortress_mode - view->submenu_line_id.begin()));
                });
            });
        });

        If(viewscreen_is<df::viewscreen_loadgamest>, [&]()
        {
            unknown_screen = false;
            df::viewscreen_loadgamest *view = strict_virtual_cast<df::viewscreen_loadgamest>(curview);
            if (!view)
            {
                return;
            }

            While([&]() -> bool { return view->loading; }, [&]()
            {
                Delay();
            });

            If([&]() -> bool { return config.random_embark_world.empty(); }, [&]()
            {
                Do([&]()
                {
                    ai->debug(out, "leaving \"Select World\" (no save name)");
                });

                Key(interface_key::LEAVESCREEN);
            }, [&]()
            {
                auto save = std::find_if(view->saves.begin(), view->saves.end(), [](df::loadgame_save_info *s) -> bool
                {
                    return s->folder_name == config.random_embark_world;
                });

                If([&]() -> bool { return save == view->saves.end(); }, [&]()
                {
                    Do([&]()
                    {
                        ai->debug(out, "could not find save named " + config.random_embark_world);
                        config.set(out, config.random_embark_world, std::string());
                    });

                    Key(interface_key::LEAVESCREEN);
                }, [&]()
                {
                    Do([&]()
                    {
                        ai->debug(out, stl_sprintf("selecting save #%d (%s) (%s)",
                            (save - view->saves.begin()) + 1,
                            (*save)->world_name.c_str(),
                            (*save)->fort_name.c_str()));
                    });

                    SelectVerticalMenuItem(view->sel_idx, int32_t(save - view->saves.begin()));
                });
            });
        });

        If(viewscreen_is("dfhack/lua/load_screen"), [&]()
        {
            unknown_screen = false;
            dfhack_viewscreen *view = dfhack_viewscreen::try_cast(curview);
            df::viewscreen_loadgamest *parent = view ? strict_virtual_cast<df::viewscreen_loadgamest>(view->parent) : nullptr;
            if (!view || view->getFocusString() != "lua/load_screen" || !parent)
            {
                return;
            }

            If([&]() -> bool { return config.random_embark_world.empty(); }, [&]()
            {
                Do([&]()
                {
                    ai->debug(out, "leaving \"Select World\" (no save name)");
                });

                Key(interface_key::LEAVESCREEN);
            }, [&]()
            {
                std::vector<df::loadgame_save_info *> filtered_saves;
                for (auto save : parent->saves)
                {
                    size_t len = save->folder_name.length();
                    const static std::string tmpl("-#####-##-##");
                    if (len < tmpl.length())
                    {
                        filtered_saves.push_back(save);
                    }
                    else
                    {
                        for (size_t i = 0; i < tmpl.length(); i++)
                        {
                            char expect = tmpl.at(tmpl.length() - i - 1);
                            char actual = save->folder_name.at(len - i - 1);
                            if (expect == '#' ? actual < '0' || actual > '9' : actual != expect)
                            {
                                filtered_saves.push_back(save);
                                break;
                            }
                        }
                    }
                }

                auto save = std::find_if(filtered_saves.begin(), filtered_saves.end(), [](df::loadgame_save_info *s) -> bool
                {
                    return s->folder_name == config.random_embark_world;
                });

                If([&]() -> bool { return save == filtered_saves.end(); }, [&]()
                {
                    Do([&]()
                    {
                        ai->debug(out, "could not find save named " + config.random_embark_world);
                        config.set(out, config.random_embark_world, std::string());
                    });

                    Key(interface_key::LEAVESCREEN);
                }, [&]()
                {
                    auto L = Lua::Core::State;
                    lua_rawgetp(L, LUA_REGISTRYINDEX, view);
                    lua_getfield(L, -1, "sel_idx");
                    int32_t sel_idx = static_cast<int32_t>(lua_tointeger(L, -1)) - 1;
                    lua_pop(L, 2);

                    Do([&]()
                    {
                        ai->debug(out, stl_sprintf("selecting save #%d (%s) (%s)",
                            (save - filtered_saves.begin()) + 1,
                            (*save)->world_name.c_str(),
                            (*save)->fort_name.c_str()));
                    });

                    SelectVerticalMenuItem(sel_idx, int32_t(save - filtered_saves.begin()));
                });
            });
        });

        If([&]() -> bool
        {
            df::viewscreen *screen = Gui::getCurViewscreen(false);
            df::viewscreen *parent = screen ? screen->parent : nullptr;
            // TODO: get a real focus string for the load_screen_options dialog
            return Gui::getFocusString(screen) == "dfhack/lua" && Gui::getFocusString(parent) == "dfhack/lua/load_screen";
        }, [&]()
        {
            unknown_screen = false;
            dfhack_viewscreen *view = dfhack_viewscreen::try_cast(curview);
            if (!view)
                return;

            auto L = Lua::Core::State;
            lua_rawgetp(L, LUA_REGISTRYINDEX, view);
            lua_getfield(L, -1, "save_mtime");
            if (lua_isnoneornil(L, -1))
            {
                // not on load_screen_options
                lua_pop(L, 2);
                return;
            }
            lua_getfield(L, -2, "loading");
            bool startedLoading = !!lua_isboolean(L, -1);
            bool finishedLoading = startedLoading && !lua_toboolean(L, -1);
            lua_pop(L, 3);

            If([&]() -> bool { return startedLoading; }, [&]()
            {
                While([&]() -> bool { return !finishedLoading; }, [&]()
                {
                    Delay();
                });
            }, [&]()
            {
                Key(interface_key::SELECT);
            });
        });

        If(viewscreen_is<df::viewscreen_new_regionst>, [&]()
        {
            unknown_screen = false;
            df::viewscreen_new_regionst *view = strict_virtual_cast<df::viewscreen_new_regionst>(curview);
            if (!view)
            {
                return;
            }

            Do([&]()
            {
                config.set(out, config.random_embark_world, std::string());
            });

            If([&]() -> bool { return !view->welcome_msg.empty(); }, [&]()
            {
                Do([&]()
                {
                    ai->debug(out, "leaving world gen disclaimer");
                });

                Key(interface_key::LEAVESCREEN);
            });

            If([&]() -> bool { return view->simple_mode == 1; }, [&]()
            {
                Do([&]()
                {
                    ai->debug(out, "choosing \"Generate World\"");
                });

                int32_t want_size = std::min(std::max(config.world_size, 0), 4);

                If([&]() -> bool { return view->world_size != want_size; }, [&]()
                {
                    While([&]() -> bool { return view->cursor_line != 0; }, [&]()
                    {
                        Key(interface_key::STANDARDSCROLL_UP);
                    });

                    SelectHorizontalMenuItem(view->world_size, want_size);
                });

                int32_t want_minerals = 3;

                If([&]() -> bool { return view->mineral_occurence != want_minerals; }, [&]()
                {
                    While([&]() -> bool { return view->cursor_line != 6; }, [&]()
                    {
                        Key(interface_key::STANDARDSCROLL_DOWN);
                    });

                    SelectHorizontalMenuItem(view->mineral_occurence, want_minerals);
                });

                Key(interface_key::MENU_CONFIRM);
            });

            If([&]() -> bool { return !world->entities.all.empty() && view->simple_mode == 0 && world->worldgen_status.state == 10; }, [&]()
            {
                Do([&]()
                {
                    ai->debug(out, "world gen finished, save name is " + world->cur_savegame.save_dir);
                    config.set(out, config.random_embark_world, world->cur_savegame.save_dir);
                });

                Key(interface_key::SELECT);
            });
        });

        If(viewscreen_is<df::viewscreen_update_regionst>, [&]()
        {
            unknown_screen = false;
            df::viewscreen_update_regionst *view = strict_virtual_cast<df::viewscreen_update_regionst>(curview);
            if (!view)
            {
                return;
            }

            ai->debug(out, "updating world, goal: " + AI::timestamp(view->year, view->year_tick));
        });

        If(viewscreen_is<df::viewscreen_choose_start_sitest>, [&]()
        {
            unknown_screen = false;
            df::viewscreen_choose_start_sitest *view = strict_virtual_cast<df::viewscreen_choose_start_sitest>(curview);
            if (!view)
            {
                return;
            }

            If([&]() -> bool { return view->finder.finder_state == -1; }, [&]()
            {
                Do([&]()
                {
                    ai->debug(out, "choosing \"Site Finder\"");
                });

                Key(interface_key::SETUP_FIND);

                FOR_ENUM_ITEMS(embark_finder_option, o)
                {
                    If([&]() -> bool { return view->finder.options[o] != config.embark_options[o]; }, [&]()
                    {
                        auto visible = std::find(view->finder.visible_options.begin(), view->finder.visible_options.end(), o);

                        If([&]() -> bool { return visible == view->finder.visible_options.end(); }, [&]()
                        {
                            ai->debug(out, "[CHEAT] Setting hidden site finder option " + enum_item_key(o));
                            view->finder.options[o] = config.embark_options[o];
                        }, [&]()
                        {
                            While([&]() -> bool { return view->finder.cursor != int32_t(visible - view->finder.visible_options.begin()); }, [&]()
                            {
                                If([&]() -> bool { return view->finder.cursor > int32_t(visible - view->finder.visible_options.begin()); }, [&]()
                                {
                                    Key(interface_key::STANDARDSCROLL_UP);
                                }, [&]()
                                {
                                    Key(interface_key::STANDARDSCROLL_DOWN);
                                });
                            });

                            If([&]() -> bool { return o == embark_finder_option::DimensionX || o == embark_finder_option::DimensionY; }, [&]()
                            {
                                int32_t target = std::min(std::max(config.embark_options[o], 1), 16);
                                While([&]() -> bool { return view->finder.options[o] != target; }, [&]()
                                {
                                    If([&]() -> bool { return view->finder.options[o] > target; }, [&]()
                                    {
                                        Key(interface_key::STANDARDSCROLL_LEFT);
                                    }, [&]()
                                    {
                                        Key(interface_key::STANDARDSCROLL_RIGHT);
                                    });
                                });
                            }, [&]()
                            {
                                If([&]() -> bool { return config.embark_options[o] == -1; }, [&]()
                                {
                                    While([&]() -> bool { return view->finder.options[o] != -1; }, [&]()
                                    {
                                        Key(interface_key::STANDARDSCROLL_LEFT);
                                    });
                                }, [&]()
                                {
                                    If([&]() -> bool { return view->finder.options[o] == -1; }, [&]()
                                    {
                                        Key(interface_key::STANDARDSCROLL_RIGHT);
                                    });

                                    While([&]() -> bool { return view->finder.options[o] != -1 && view->finder.options[o] != config.embark_options[o]; }, [&]()
                                    {
                                        If([&]() -> bool { return view->finder.options[o] > config.embark_options[o]; }, [&]()
                                        {
                                            Key(interface_key::STANDARDSCROLL_LEFT);
                                        }, [&]()
                                        {
                                            Key(interface_key::STANDARDSCROLL_RIGHT);
                                        });
                                    });
                                });
                            });
                        });
                    });
                }

                Key(interface_key::SELECT);
            }, [&]()
            {
                If([&]() -> bool { return view->finder.search_x != -1 || view->finder.search_y != 0; }, [&]()
                {
                    ai->debug(out, stl_sprintf("searching for a site (%d/%d, %d/%d)",
                        view->finder.search_x,
                        world->world_data->world_width / 16,
                        view->finder.search_y,
                        world->world_data->world_height / 16));
                }, [&]()
                {
                    Do([&]()
                    {
                        ai->debug(out, "choosing \"Embark\"");
                    });

                    Key(interface_key::LEAVESCREEN);

                    Do([&]()
                    {
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
                        if (sites.empty())
                        {
                            ai->debug(out, "leaving embark selector (no good embarks)");
                            config.set(out, config.random_embark_world, std::string());
                            AI::abandon(out);
                            return;
                        }
                        ai->debug(out, stl_sprintf("found sites count: %d", sites.size()));
                        // Don't embark on the same region every time.
                        std::vector<std::seed_seq::result_type> seeds;
                        seeds.push_back(std::seed_seq::result_type(ai->rng()));
                        seeds.push_back(std::seed_seq::result_type(*cur_year));
                        seeds.push_back(std::seed_seq::result_type(*cur_year_tick));
                        std::seed_seq seeds_seq(seeds.begin(), seeds.end());
                        std::mt19937 rng(seeds_seq);
                        df::coord2d site = sites[std::uniform_int_distribution<size_t>(0, sites.size() - 1)(rng)];
                        selected_site_diff = site - start;
                    });

                    if (!selected_site_diff.isValid())
                    {
                        return;
                    }

                    While([&]() -> bool { return selected_site_diff.x > 0; }, [&]()
                    {
                        Do([&]()
                        {
                            selected_site_diff.x--;
                        });

                        Key(interface_key::CURSOR_RIGHT);
                    });

                    While([&]() -> bool { return selected_site_diff.x < 0; }, [&]()
                    {
                        Do([&]()
                        {
                            selected_site_diff.x++;
                        });

                        Key(interface_key::CURSOR_LEFT);
                    });

                    While([&]() -> bool { return selected_site_diff.y > 0; }, [&]()
                    {
                        Do([&]()
                        {
                            selected_site_diff.y--;
                        });

                        Key(interface_key::CURSOR_DOWN);
                    });

                    While([&]() -> bool { return selected_site_diff.y < 0; }, [&]()
                    {
                        Do([&]()
                        {
                            selected_site_diff.y++;
                        });

                        Key(interface_key::CURSOR_UP);
                    });

                    Delay(5 * 100);

                    Key(interface_key::CHANGETAB);

                    While([&]() -> bool { return view->page != df::viewscreen_choose_start_sitest::Biome; }, [&]()
                    {
                        Delay(5 * 100);

                        Key(interface_key::CHANGETAB);
                    });

                    Delay(5 * 100);

                    Key(interface_key::SETUP_BIOME_1);

                    bool biome_found = true;
                    While([&]() -> bool { return view->biome_highlighted && biome_found; }, [&]()
                    {
                        If([&]() -> bool
                        {
                            int32_t biome_idx = view->biome_idx + 1;
                            AI::feed_key(view, static_cast<df::interface_key>(interface_key::SETUP_BIOME_1 + biome_idx));
                            return view->biome_highlighted && biome_idx == view->biome_idx;
                        }, [&]()
                        {
                            Delay(5 * 100);
                        }, [&]()
                        {
                            biome_found = false;
                        });
                    });

                    Key(interface_key::SETUP_EMBARK);
                    // dismiss warnings
                    Key(interface_key::SELECT);
                });
            });
        });

        If(viewscreen_is<df::viewscreen_setupdwarfgamest>, [&]()
        {
            unknown_screen = false;
            Do([&]()
            {
                ai->debug(out, "choosing \"Play Now\"");
            });

            Key(interface_key::SELECT);
            // TODO custom embark loadout
        });

        If(viewscreen_is<df::viewscreen_textviewerst>, [&]()
        {
            unknown_screen = false;
            Do([&]()
            {
                ai->debug(out, "site is ready.");
            });

            Delay(5 * 100);

            Do([&]()
            {
                ai->debug(out, "disabling minimap.");
                AI::feed_key(interface_key::LEAVESCREEN);
                Gui::setMenuWidth(3, 3);
                *standing_orders_gather_refuse_outside = 1;
                *standing_orders_job_cancel_announce = config.cancel_announce;
            });

            Delay();
        });

        If([&]() -> bool { return unknown_screen; }, [&]()
        {
            ai->statechanged(out, SC_VIEWSCREEN_CHANGED);
        });
    });
}

void EmbarkExclusive::SelectVerticalMenuItem(int32_t current, int32_t target)
{
    MoveToItem(current, target);

    Key(interface_key::SELECT);
}

void EmbarkExclusive::SelectHorizontalMenuItem(int32_t current, int32_t target)
{
    MoveToItem(current, target, interface_key::STANDARDSCROLL_RIGHT, interface_key::STANDARDSCROLL_LEFT);

    Key(interface_key::SELECT);
}

RestartWaitExclusive::RestartWaitExclusive(AI *ai) :
    ExclusiveCallback("restart wait"),
    ai(ai)
{
}

RestartWaitExclusive::~RestartWaitExclusive()
{
}

void RestartWaitExclusive::Run(color_ostream & out)
{
    Do([&]()
    {
        ai->debug(out, "game over. restarting in 1 minute.");
    });

    Delay(60 * 100);

    Do([&]()
    {
        ai->debug(out, "restarting.");
    });

    Key(interface_key::LEAVESCREEN);

    While([&]() -> bool { return !strict_virtual_cast<df::viewscreen_titlest>(Gui::getCurViewscreen(true)); }, [&]()
    {
        Delay();
    });

    if (!config.no_quit)
    {
        if (config.lockstep)
        {
            Hook_Shutdown_Now();
        }
        else
        {
            Gui::getCurViewscreen(true)->breakdown_level = interface_breakdown_types::QUIT;
        }
    }
    else
    {
        extern bool full_reset_requested;
        full_reset_requested = true;
    }
}
