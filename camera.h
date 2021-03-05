#pragma once

#include "event_manager.h"

class AI;

class Camera
{
    AI & ai;
    OnupdateCallback *ontick_handle;
    OnupdateCallback *onupdate_handle;
    OnstatechangeCallback *onstatechange_handle;
    friend class AI;

public:
    Camera(AI & ai);
    ~Camera();

    command_result startup(color_ostream & out);
    command_result onupdate_register(color_ostream & out);
    command_result onupdate_unregister(color_ostream & out);

    void check_record_status();
    void update_tick(color_ostream & out);
    void update(color_ostream & out);
    std::string status();

    int32_t following;
    std::vector<int32_t> following_prev;
    int32_t follow_unit;
    int32_t follow_item;
    bool follow_stop;
    bool movie_started_in_lockstep;
};
