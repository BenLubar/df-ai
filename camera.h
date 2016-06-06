#pragma once

#include "event_manager.h"

class AI;

class Camera
{
    AI *ai;
    OnupdateCallback *onupdate_handle;
    OnstatechangeCallback *onstatechange_handle;

    int32_t following;
    std::vector<int32_t> following_prev;

public:
    Camera(AI *ai);
    ~Camera();

    command_result startup(color_ostream & out);
    command_result onupdate_register(color_ostream & out);
    command_result onupdate_unregister(color_ostream & out);

    void check_record_status();
    void update(color_ostream & out);
    void ignore_pause(int32_t x, int32_t y, int32_t z);
    std::string status();
};

// vim: et:sw=4:ts=4
