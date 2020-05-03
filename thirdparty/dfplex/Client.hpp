/*
 * Dwarfplex is based on Webfort, created by Vitaly Pronkin on 14/05/14.
 * Copyright (c) 2020 white-rabbit, ISC license
*/

#pragma once

#include "modules/Screen.h"
#include "df/coord.h"
#include "df/interface_key.h"
#include "df/ui.h"
#include "df/ui_unit_view_mode.h"

#include "keymap.hpp"
#include "hackutil.hpp"

#include <string>
#include <vector>
#include <queue>
#include <set>
#include <memory>
#include <functional>

struct ClientIdentity
{
    std::string addr;
    std::string nick;
    uint8_t nick_colour = 0;
    bool is_admin = false;
};

// a command which is part of the algorithm for
// restoring menu state.
struct RestoreKey
{
    std::set<df::interface_key> m_interface_keys;
    
    // should we check state after applying this key?
    // (should not be used with m_need_wait)
    bool m_check_state = false;
    size_t m_check_start; // if check fails, rewind back to here.
    bool m_catch = false; // this will intercept an escape. (requires m_check_start to be set.)
    
    // after applying the key, this should be the state of the menu.
    menu_id m_post_menu;
    size_t m_post_menu_depth;
    
    // this is the observed state of the menu after applying.
    bool m_catch_observed_autorewind = false; // check the observed state for this screen when comparing menu IDs on exit.
    menu_id m_observed_menu;
    size_t m_observed_menu_depth=0;
    bool m_blockcatch = false; // if the observed menu does not match the previous frames', error to check_start.
    
    // additional misc. properties ------------------------------------------
    
    // restore cursor position before/after applying this?
    bool m_pre_restore_cursor = false;
    Coord m_cursor; // only used for pre, never post.
    bool m_post_restore_cursor = false;

    // don't do the default sidebar-refresh after this command is applied?
    bool m_suppress_sidebar_refresh = false;
    
    // restore custom stockpile settings after applying?
    bool m_restore_stockpile_state = false;
    
    // restore squad state after applying?
    bool m_restore_squad_state = false;
    
    // restore unit view menu state after applying?
    bool m_restore_unit_view_state = false;
    
    // do not save cursor position
    bool m_freeze_cursor = false;
    
    RestoreKey()=default;
    RestoreKey(df::interface_key key)
        : m_interface_keys{ key }
    { }
    RestoreKey(RestoreKey&&)=default;
    RestoreKey(const RestoreKey&)=default;
    RestoreKey& operator=(const RestoreKey&)=default;
    RestoreKey& operator=(RestoreKey&&)=default;
};

// information to be fed to df to restore ui per-client.
struct UIState
{
    // pressing these keys from dwarfmode/Default will restore the UI state of the user.
    std::vector<RestoreKey> m_restore_keys;
    static const size_t K_RESTORE_PROGRESS_ROOT_MAX = 15;
    size_t m_restore_progress_root = 0; // how long have we tried returning to the root?
    size_t m_restore_progress = 0; // how far into m_restore_keys we've gotten.
    
    bool m_suppress_sidebar_refresh = false; // suppress the sidebar refresh at the end of this
    bool m_freeze_cursor = false;
    
    // pause state
    // 0: not paused
    // 1: paused due to being in a menu
    bool m_pause_required = false;
    
    bool m_viewcoord_set = false;
    Coord m_viewcoord;

    // for switching to with the [ ] keys.
    bool m_stored_viewcoord_skip = false;
    bool m_stored_camera_return = false; // return after an event is set; remove this eventually...?
    Coord m_stored_viewcoord;
    bool m_following_client = false;
    std::shared_ptr<ClientIdentity> m_client_screen_cycle;
    
    // view dimensions
    int32_t m_map_dimx=-1, m_map_dimy=-1;
    
    bool m_cursorcoord_set = false;
    Coord m_cursorcoord;

    // Build menu cursor position
    // if false, do not restore the build menu cursor position
    bool m_buildcoord_set = false;
    Coord m_buildcoord; // stores the last position of the cursor from the build menu, if any
    
    bool m_designationcoord_share = false;
    bool m_designationcoord_set = false;
    Coord m_designationcoord;
    
    bool m_burrowcoord_share = false;
    bool m_burrowcoord_set = false;
    Coord m_burrowcoord;
    
    bool m_squadcoord_share = false;
    bool m_squadcoord_start_set = false;
    Coord m_squadcoord_start;
    
    // pair of colour and offset from cursorcoord
    std::vector<std::pair<int8_t, Coord>> m_construction_plan;
    
    // pressing tab from the root menu changes these.
    uint8_t m_menu_width = 1, m_area_map_width = 2;
    
    // designation state (in vanilla, chop is default.)
    df::interface_key m_designate_mode = df::interface_key::DESIGNATE_CHOP;
    bool m_designate_marker = false, m_designate_priority_set = false;
    int32_t m_designate_priority = 4;
    // TODO: capture designate mine mode? auto-mine mode may be problematic...
    
    // stockpile state
    df::interface_key m_stockpile_mode = df::interface_key::STOCKPILE_ANIMAL;
    
    // unit view mode
    df::ui_unit_view_mode::T_value m_unit_view_mode = df::ui_unit_view_mode::General;
    bool m_show_combat = true;
    bool m_show_labor = true;
    bool m_show_misc = true;
    int32_t m_view_unit = -1;
    int32_t m_view_unit_labor_scroll = 0;
    int32_t m_view_unit_labor_submenu = -1;
    bool m_defer_restore_cursor = false;
    int32_t m_viewcycle = 0;
    
    // burrow state
    bool m_brush_erasing = false;
    
    // civlist screen
    int32_t m_civ_x = -1, m_civ_y = -1;
    
    // stabilizes list-menus (e.g announcements, reports)
    // one per viewscreen on the stack.
    std::vector<int32_t> m_list_cursor;
    
    // resize state
    bool m_building_in_resize = false;
    int32_t m_building_resize_radius = 4;
    
    // squad state
    struct {
        bool in_select_indiv;
        int32_t sel_indiv_squad; // refers-to squad_id
        std::vector<int32_t> indiv_selected; // refers-to historical figures
        std::vector<int32_t> squad_selected; // refers-to squad id
        std::vector<int32_t> kill_selected; // refers-to unit id
    } m_squads;
    
    std::string debug_trace() const;
    
    // dfplex-specific UI information
    std::string m_dfplex_chat_message;
    bool m_dfplex_chat_entering = false;
    bool m_dfplex_chat_config = false;
    bool m_dfplex_chat_name_entering = false;
    bool m_dfplex_hide_chat = false;
    
    // resets most UI state
    void reset()
    {
        m_restore_keys.clear();
        m_restore_progress_root = 0;
        m_restore_progress = 0;
        m_pause_required = false;
        m_brush_erasing = false;
        m_viewcoord_set = false;
        m_cursorcoord_set = false;
        m_burrowcoord_share = false;
        m_burrowcoord_set = false;
        m_designationcoord_set = false;
        m_designationcoord_share = false;
        m_suppress_sidebar_refresh = false;
        m_building_in_resize = false;
        m_building_resize_radius = 4;
        m_stored_camera_return = false;
        m_stored_viewcoord_skip = false;
        m_following_client = false;
        m_construction_plan.clear();
        m_unit_view_mode = df::ui_unit_view_mode::General;
        m_show_combat = true;
        m_show_labor = true;
        m_show_misc = true;
        m_defer_restore_cursor = false;
        m_view_unit = -1;
        m_view_unit_labor_scroll = 0;
        m_view_unit_labor_submenu = -1;
        m_freeze_cursor = false;
        m_viewcycle = 0;
        m_list_cursor.clear();
        m_civ_x = -1;
        m_civ_y = -1;
        m_dfplex_chat_entering = false;
        m_dfplex_chat_message = "";
        m_dfplex_chat_config = false;
        m_dfplex_chat_name_entering = false;
        m_map_dimx = m_map_dimy = -1;
    }
    
    // makes the UI ready to handle a new plex re-entry.
    void next()
    {
        m_restore_progress_root = 0;
        m_restore_progress = 0;
        m_suppress_sidebar_refresh = false;
    }
};

// FIXME -- someone who understands the pen class can
// double check this implementation.
inline bool operator==(const DFHack::Screen::Pen& a, const DFHack::Screen::Pen& b)
{
    if (a.tile_mode != b.tile_mode)
    {
        return false;
    }
    if (a.tile != b.tile)
    {
        return false;
    }
    if (a.bold != b.bold) return false;
    if (!a.tile)
    {
        if (a.ch != b.ch) return false;
        if (a.fg != b.fg) return false;
        if (a.bg != b.bg) return false;
    }
    else
    {
        switch (a.tile_mode)
        {
        case DFHack::Screen::Pen::AsIs:
            break;
        case DFHack::Screen::Pen::CharColor:
            if (a.fg != b.fg) return false;
            if (a.bg != b.bg) return false;
            break;
        case DFHack::Screen::Pen::TileColor:
            if (a.tile_fg != b.tile_fg) return false;
            if (a.tile_bg != b.tile_bg) return false;
            break;
        }
    }
    return true;
}

// a tile on the screen, with some extra information
struct ClientTile
{
    DFHack::Screen::Pen pen;
    bool modified;
    bool is_text;
    bool is_overworld;
    bool is_map;
    
    bool operator==(const ClientTile& other) const
    {
        // ignore modified
        return pen == other.pen && is_text == other.is_text;
    }
    
    bool operator!=(const ClientTile& other) const
    {
        return ! (*this == other);
    }
};

// array of all tiles on the screen
typedef ClientTile screenbuf_t[256 * 256];

struct Client;
struct ClientUpdateInfo
{
    bool is_multiplex;
    bool on_destroy;
};
typedef std::function<void(Client*, const ClientUpdateInfo&)> client_update_cb;

struct Client {
    std::shared_ptr<ClientIdentity> id{ new ClientIdentity() };
    
    // Called once per update.
    //
    // If multiplexing, update occurs after state restore but
    // before post-state capture (and before screen capture).
    //
    // Should populate Client::keyqueue. For example:
    //   client->keyqueue.emplace(df::enums::interface_key::D_DESIGNATE);
    //
    // also called if the client is deleted (on_destroy will be true.)
    client_update_cb update_cb;
    
    std::string info_message; // this string is displayed to the user.
    
    std::string m_debug_info = ""; // debug info sent to user.
    bool m_debug_enabled = false;
    
    // client's screen
    screenbuf_t sc;
    uint8_t dimx=0, dimy=0;
    uint8_t desired_dimx=80, desired_dimy=25;
    
    // pending keypresses from client.
    std::queue<KeyEvent> keyqueue;
    
    UIState ui;
};
