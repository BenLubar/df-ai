#pragma once

#include "Core.h"
#include <Console.h>
#include <Export.h>
#include <PluginManager.h>

#include "DataDefs.h"

#include <random>

using namespace DFHack;
using namespace df::enums;

extern std::vector<std::string> *plugin_globals;

class Population;
class Plan;
class Stocks;
class Camera;
class Embark;
class AI;

class Population
{
    AI *ai;

public:
    Population(color_ostream & out, AI *parent);
    ~Population();

    command_result status(color_ostream & out);
    command_result statechange(color_ostream & out, state_change_event event);
    command_result update(color_ostream & out);
};

class Plan
{
    AI *ai;

public:
    Plan(color_ostream & out, AI *parent);
    ~Plan();

    command_result status(color_ostream & out);
    command_result statechange(color_ostream & out, state_change_event event);
    command_result update(color_ostream & out);
};

class Stocks
{
    AI *ai;

public:
    Stocks(color_ostream & out, AI *parent);
    ~Stocks();

    command_result status(color_ostream & out);
    command_result statechange(color_ostream & out, state_change_event event);
    command_result update(color_ostream & out);
};

class Camera
{
    AI *ai;
    int32_t following;
    int32_t following_prev[3];
    int following_index;
    int update_after_ticks;
    std::mt19937 rng;

public:
    Camera(color_ostream & out, AI *parent);
    ~Camera();

    command_result status(color_ostream & out);
    command_result statechange(color_ostream & out, state_change_event event);
    command_result update(color_ostream & out);

    void start_recording(color_ostream & out);
    static bool compare_view_priority(df::unit *, df::unit *);
    static int view_priority(df::unit *);
    bool followed_previously(int32_t id);
};

class Embark
{
    AI *ai;

public:
    Embark(color_ostream & out, AI *parent);
    ~Embark();

    command_result status(color_ostream & out);
    command_result statechange(color_ostream & out, state_change_event event);
    command_result update(color_ostream & out);
};

class AI
{
    Population pop;
    Plan plan;
    Stocks stocks;
    Camera camera;
    Embark embark;

public:
    AI(color_ostream & out);
    ~AI();

    command_result status(color_ostream & out);
    command_result statechange(color_ostream & out, state_change_event event);
    command_result update(color_ostream & out);

    void unpause(color_ostream & out);
};

// vim: et:sw=4:ts=4
