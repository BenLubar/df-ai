/*
 * Dwarfplex is based on Webfort, created by Vitaly Pronkin on 14/05/14.
 * Copyright (c) 2014 mifki, ISC license.
 * Copyright (c) 2020 white-rabbit, ISC license
*/

#pragma once

#include "Core.h"
#include "Console.h"
#include "modules/World.h"
#include "df/ui_sidebar_mode.h"
#include "df/viewscreen.h"
#include "df/viewscreen_meetingst.h"
#include "df/unit.h"
#include "modules/Gui.h"
#include "modules/Screen.h"

#include <stdint.h>
#include <string>
#include <sstream>

struct Coord
{
    int32_t x, y, z;
    Coord()
        : x(0)
        , y(0)
        , z(0)
    { }
    Coord(int32_t x, int32_t y, int32_t z)
        : x(x)
        , y(y)
        , z(z)
    { }
    Coord(const Coord&)=default;
    Coord(Coord&&)=default;
    Coord(const df::coord& c)
        : x(c.x)
        , y(c.y)
        , z(c.z)
    { }
    
    Coord& operator=(const Coord&)=default;
    Coord& operator=(Coord&&)=default;
    
    // due to an unknown bug, all operator commands need to be explicitly invoked
    // (e.g. a.operator-(b);)
    operator bool() const
    {
        return x != 0 || y != 0 || z != 0;
    }
    Coord operator-(const Coord& other) const
    {
        return{ x-other.x, y-other.y, z-other.z };
    }
    Coord operator+(const Coord& other) const
    {
        return{ x+other.x, y+other.y, z+other.z };
    }
    bool operator!=(const Coord& other) const
    {
        return x != other.x || y != other.y || z != other.z;
    }
};

typedef std::string menu_id;
static const menu_id K_NOCHECK = "*";

menu_id get_current_menu_id();

// returns true if a describes b. (e.g if a == b.)
bool menu_id_matches(const menu_id& a, const menu_id& b);

#define UPDATE_VS(vs, id) {vs = DFHack::Gui::getCurViewscreen(true); id = df::virtual_identity::get(vs); }

// meant for printing debug information about a unit.
std::string unit_info(int32_t unit_id);
std::string historical_figure_info(int32_t unit_id);

void show_announcement(std::string announcement);

std::string historical_figure_info(int32_t figure_id);

// returns how many not-dismissed ancestors this viewscreen has.
size_t get_vs_depth(df::viewscreen* vs);

inline bool is_dwarf_mode()
{
    DFHack::t_gamemodes gm;
    DFHack::World::ReadGameMode(gm);
    return gm.g_mode == df::game_mode::DWARF;
}

bool is_at_root();
bool is_realtime_dwarf_menu(); // root menu or squads submenu
bool return_to_root(); // error -> returns false

// closes all viewscreens *if possible*
// does not deallocate them
// returns false on error
bool defer_return_to_root();

bool is_text_tile(int x, int y, bool &is_map, bool &is_overworld);

// https://stackoverflow.com/a/42844629
inline bool endsWith(const std::string& str, const std::string& suffix)
{
    return str.size() >= suffix.size() && 0 == str.compare(str.size()-suffix.size(), suffix.size(), suffix);
}

inline bool startsWith(const std::string& str, const std::string& prefix)
{
    return str.rfind(prefix, 0) == 0;
}

inline bool contains(const std::string& str, const std::string& infix)
{
    return str.find(infix) != std::string::npos;
}

// like the word warp in MiscUtils, but respects existing line endings.
// TODO: use c++17's std::string_view instead.
std::vector<std::string> word_wrap_lines(const std::string& str, uint16_t width);

template<class T>
inline bool contains(const std::set<T>& container, const T& value)
{
    return container.find(value) != container.end();
}

// from https://stackoverflow.com/a/24315631
inline std::string replace_all(std::string str, const std::string& from, const std::string& to) {
    size_t start_pos = 0;
    while((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
    return str;
}

// checks if x is in inclusive range [a, b] or [b, a]
inline bool in_range(int32_t x, int32_t a, int32_t b)
{
    return (x >= a && x <= b) || (x >= b && x<= a);
}

void remove_screen(df::viewscreen* v);

inline uint8_t pen_colour(const DFHack::Screen::Pen& p)
{
    return (p.bold << 6) | (p.bg << 3) | p.fg;
}

inline void set_pen_colour(DFHack::Screen::Pen& p, uint8_t col)
{
    p.bold = !!(col & 64);
    p.bg = (col >> 3) & 7;
    p.fg = col & 7;
}

inline bool is_designation_mode_sub(df::ui_sidebar_mode mode)
{
    if (mode >= df::ui_sidebar_mode::DesignateItemsClaim && mode <= df::ui_sidebar_mode::DesignateItemsUnhide)
    {
        return true;
    }
    
    return false;
}

// basic designations only
inline bool is_designation_mode(df::ui_sidebar_mode mode)
{
    using namespace df;
    if (mode >= ui_sidebar_mode::DesignateMine && mode <= ui_sidebar_mode::DesignateCarveFortification)
    {
        return true;
    }
    if (mode == ui_sidebar_mode::DesignateRemoveConstruction)
    {
        return true;
    }
    if (mode >= ui_sidebar_mode::DesignateChopTrees && mode <= ui_sidebar_mode::DesignateToggleMarker)
    {
        return true;
    }
    return false;
}

void center_view_on_coord(const Coord&);

bool is_siege();

int32_t renaming_squad_id();