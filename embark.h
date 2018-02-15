#pragma once

#include "event_manager.h"

class AI;

class EmbarkExclusive : public ExclusiveCallback
{
    AI * const ai;
    df::coord2d selected_site_diff;
    bool unknown_screen;

public:
    EmbarkExclusive(AI *ai);
    virtual ~EmbarkExclusive();

    virtual void Run(color_ostream & out);

private:
    void SelectVerticalMenuItem(volatile int32_t & current, int32_t target);
    void SelectHorizontalMenuItem(volatile int32_t & current, int32_t target);
};

class RestartWaitExclusive : public ExclusiveCallback
{
    AI * const ai;

public:
    RestartWaitExclusive(AI *ai);
    virtual ~RestartWaitExclusive();

    virtual void Run(color_ostream & out);
};
