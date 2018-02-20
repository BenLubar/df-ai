#pragma once

extern volatile bool lockstep_hooked;
extern volatile bool disabling_plugin;
extern volatile bool unloading_plugin;
void Hook_Update();
void Hook_Shutdown();
void Hook_Shutdown_Now();
