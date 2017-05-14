#pragma once

#include "dfhack_shared.h"

class AI;

class Embark
{
    AI *ai;
    bool selected_embark;
    bool embarking;

public:
    Embark(AI *ai);
    ~Embark();

    command_result startup(color_ostream & out);
    command_result onupdate_register(color_ostream & out);
    command_result onupdate_unregister(color_ostream & out);
    void register_restart_timer(color_ostream & out);

    inline bool is_embarking() { return embarking; }

    bool update(color_ostream & out);
};
