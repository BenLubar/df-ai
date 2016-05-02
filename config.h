#pragma once

#include "dfhack_shared.h"

#include "df/embark_finder_option.h"

const int32_t embark_options_count = df::enum_traits<df::embark_finder_option>::last_item_value + 1;

struct Config
{
    Config();
    void load(color_ostream & out);
    void save(color_ostream & out);
    void set_random_embark_world(color_ostream & out, const std::string & value);

    bool random_embark;
    std::string random_embark_world;
    bool debug;
    bool record_movie;
    bool no_quit;
    int32_t embark_options[embark_options_count];
};

extern Config config;
