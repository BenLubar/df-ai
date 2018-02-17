#pragma once

extern volatile bool lockstep_hooked;
void Hook_Update();
void Hook_Shutdown();
void Hook_Shutdown_Now();
