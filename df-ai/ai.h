#pragma once

#include "Core.h"
#include <Console.h>
#include <Export.h>
#include <PluginManager.h>

#include "DataDefs.h"

#include <random>
#include <ctime>

#include "df/report.h"

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
    bool embarking;
    std::string world_name;
    std::time_t timeout;

public:
    Embark(color_ostream & out, AI *parent);
    ~Embark();

    command_result status(color_ostream & out);
    command_result statechange(color_ostream & out, state_change_event event);
    command_result update(color_ostream & out);
};

class AI
{
protected:
    std::mt19937 rng;
    Population pop;
    friend class Population;
    Plan plan;
    friend class Plan;
    Stocks stocks;
    friend class Stocks;
    Camera camera;
    friend class Camera;
    Embark embark;
    friend class Embark;
    std::time_t unpause_delay;

public:
    AI(color_ostream & out);
    ~AI();

    command_result status(color_ostream & out);
    command_result statechange(color_ostream & out, state_change_event event);
    command_result update(color_ostream & out);

    void debug(color_ostream & out, const std::string & str);
    void unpause(color_ostream & out);
    void check_unpause(color_ostream & out, state_change_event event);
    void handle_pause_event(color_ostream & out, std::vector<df::report *>::reverse_iterator ann, std::vector<df::report *>::reverse_iterator end);
};

// vim: et:sw=4:ts=4
