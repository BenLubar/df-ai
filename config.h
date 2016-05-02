#pragma once

#include "dfhack_shared.h"

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
};

extern Config config;
