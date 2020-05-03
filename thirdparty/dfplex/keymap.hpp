/*
 * Dwarfplex is based on Webfort, created by Vitaly Pronkin on 14/05/14.
 * Copyright (c) 2020 white-rabbit, ISC license
*/

#pragma once

#include <stdint.h>
#include <iostream>
#include <map>
#include <vector>
#include <set>
#include <cassert>
#include <memory>

#include "df/interface_key.h"
#include "SDL_keysym.h"

enum class EventType {
    type_unicode,
    type_key,
    type_button,
    type_interface,
};

// a key event represents either a set of interface keys or
// the keypress event that would generate a set of interface keys.
struct KeyEvent {
    EventType type;
    uint8_t mod = 0;      // not defined for type=unicode. 1: shift, 2: ctrl, 4:alt

    std::unique_ptr<std::set<df::interface_key>> interface_keys;
    uint16_t unicode = 0;
    SDL::Key key = SDL::K_UNKNOWN;
    uint8_t button = 0;

    inline bool operator== (const KeyEvent &other) const {
        if (mod != other.mod) return false;
        if (type != other.type) return false;
        switch (type) {
            case EventType::type_unicode: return unicode == other.unicode;
            case EventType::type_key: return key == other.key;
            case EventType::type_button: return button == other.button;
            default: return false;
        }
    }

    inline bool operator< (const KeyEvent &other) const {
        if (mod != other.mod) return mod < other.mod;
        if (type != other.type) return type < other.type;
        switch (type) {
            case EventType::type_unicode: return unicode < other.unicode;
            case EventType::type_key: return key < other.key;
            case EventType::type_button: return button < other.button;
            default: return false;
        }
    }
    
    KeyEvent()=default;
    KeyEvent(const KeyEvent& other)
    {
        *this = other;
    }
    KeyEvent(KeyEvent&&)=default;
    KeyEvent& operator=(const KeyEvent& other);
    KeyEvent& operator=(KeyEvent&&)=default;
    KeyEvent(const std::set<df::interface_key>& keys)
    {
        type = EventType::type_interface;
        interface_keys.reset(new std::set<df::interface_key>(keys));
    }
    KeyEvent(std::set<df::interface_key>&& keys)
    {
        type = EventType::type_interface;
        interface_keys.reset(new std::set<df::interface_key>(std::move(keys)));
    }
    KeyEvent(df::interface_key key)
    {
        type = EventType::type_interface;
        interface_keys.reset(new std::set<df::interface_key>());
        interface_keys->insert(key);
    }
};

std::ostream& operator<<(std::ostream& a, const KeyEvent& match);

class KeyMap
{
    std::multimap <KeyEvent, df::interface_key> keymap;

public:
    // returns false on error.
    bool loadKeyBindings(DFHack::color_ostream& out, const std::string& file);
    std::set<df::interface_key> toInterfaceKey(const KeyEvent & key);
    std::string getCommandNames(const std::set<df::interface_key>&);
    std::string getCommandName(df::interface_key);
};

// keybindings
extern KeyMap keybindings;
