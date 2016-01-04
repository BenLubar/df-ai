#pragma once

class AI;

class Embark
{
    AI *ai;
    bool selected_embark;

public:
    Embark(AI *ai);
    ~Embark();

    command_result startup(color_ostream & out);
    command_result onupdate_register(color_ostream & out);
    command_result onupdate_unregister(color_ostream & out);

    bool update(color_ostream & out);
};

extern std::string AI_RANDOM_EMBARK_WORLD;

// vim: et:sw=4:ts=4
