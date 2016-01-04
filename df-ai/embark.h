#pragma once

class AI;

class Embark
{
    AI *ai;

public:
    Embark(AI *ai);
    ~Embark();

    command_result startup(color_ostream & out);
    command_result onupdate_register(color_ostream & out);
    command_result onupdate_unregister(color_ostream & out);

    void update(color_ostream & out);

    std::string status();
};

// vim: et:sw=4:ts=4
