#include "ai.h"
#include "hooks.h"
#include "tinythread.h"

#include "MemAccess.h"

#include "df/enabler.h"
#include "df/graphic.h"
#include "df/init.h"
#include "df/interfacest.h"
#include "df/renderer.h"
#include "df/viewscreen.h"

#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/mman.h>
#include <sys/time.h>
#endif

REQUIRE_GLOBAL(enabler);
REQUIRE_GLOBAL(gps);
REQUIRE_GLOBAL(gview);
REQUIRE_GLOBAL(init);

DFhackCExport uint32_t SDL_GetTicks(void);
DFhackCExport void SDL_Delay(uint32_t ms);
DFhackCExport int SDL_SemTryWait(void *sem);

// Defined in Dwarf Fortress
char mainloop();

static volatile uint32_t lockstep_tick_count = 0;

#ifdef _WIN32
static char Real_GetTickCount[6];
static DWORD WINAPI Fake_GetTickCount(void)
{
    return lockstep_tick_count;
}
#else
static char Real_gettimeofday[6];
static int Fake_gettimeofday(struct timeval *tv, struct timezone *) throw()
{
    tv->tv_sec = lockstep_tick_count / 1000;
    tv->tv_usec = (lockstep_tick_count % 1000) * 1000;
    return 0;
}
#endif

static char Real_SDL_GetTicks[6];
static uint32_t Fake_SDL_GetTicks()
{
    return lockstep_tick_count;
}

static char Real_SDL_Delay[6];
static void Fake_SDL_Delay(uint32_t ms)
{
    (void)ms;

    while (config.lockstep)
    {
        tthread::this_thread::yield();
    }
}

static void Add_Hook(void *target, char(&real)[6], const void *fake)
{
    MemoryPatcher patcher;
    patcher.verifyAccess(target, 6, true);

    int32_t jump_offset((char*)fake - (char*)target - 4 - 1);
    real[0] = *((char *)target + 0);
    real[1] = *((char *)target + 1);
    real[2] = *((char *)target + 2);
    real[3] = *((char *)target + 3);
    real[4] = *((char *)target + 4);
    real[5] = *((char *)target + 5);
    *((char *)target + 0) = 0xE9;
    *((char *)target + 1) = *((char *)&jump_offset + 0);
    *((char *)target + 2) = *((char *)&jump_offset + 1);
    *((char *)target + 3) = *((char *)&jump_offset + 2);
    *((char *)target + 4) = *((char *)&jump_offset + 3);
    *((char *)target + 5) = 0x90;
}

static void Remove_Hook(void *target, char(&real)[6], const void *)
{
    MemoryPatcher patcher;
    patcher.verifyAccess(target, 6, true);

    *((char *)target + 0) = real[0];
    *((char *)target + 1) = real[1];
    *((char *)target + 2) = real[2];
    *((char *)target + 3) = real[3];
    *((char *)target + 4) = real[4];
    *((char *)target + 5) = real[5];
}

template<typename T>
inline static void swap3(T & a, T & b, T & c)
{
    a = b;
    b = c;
    c = a;
}

static void lockstep_swap_arrays() {
    auto & r = enabler->renderer;
    swap3(r->screen, r->screen_old, gps->screen);
    swap3(r->screentexpos, r->screentexpos_old, reinterpret_cast<int32_t * &>(gps->screentexpos));
    swap3(r->screentexpos_addcolor, r->screentexpos_addcolor_old, gps->screentexpos_addcolor);
    swap3(r->screentexpos_grayscale, r->screentexpos_grayscale_old, gps->screentexpos_grayscale);
    swap3(r->screentexpos_cf, r->screentexpos_cf_old, gps->screentexpos_cf);
    swap3(r->screentexpos_cbr, r->screentexpos_cbr_old, gps->screentexpos_cbr);
    gps->screen_limit = gps->screen + gps->dimx * gps->dimy * 4;
}

static void lockstep_render_things()
{
    //GRAB CURRENT SCREEN AT THE END OF THE LIST
    df::viewscreen *currentscreen = &gview->view;
    while (currentscreen->child != nullptr)
    {
        currentscreen = currentscreen->child;
    }

    //NO INTERFACE LEFT, LEAVE
    if (currentscreen == &gview->view)
    {
        return;
    }
  
    if (currentscreen->breakdown_level == interface_breakdown_types::NONE)
    {
	    currentscreen->render();
	}
    else
    {
    	memset(gps->screen, 0, gps->dimx * gps->dimy * 4);
	    memset(gps->screentexpos, 0, gps->dimx * gps->dimy * sizeof(long));
    }

    // don't render REC/PLAY/FPS indicators
}

static void lockstep_loop()
{
    while (config.lockstep)
    {
        mainloop();
        SDL_NumJoysticks();
        if (!config.lockstep)
        {
            break;
        }
        lockstep_tick_count += 10;
        if (!enabler->flag.bits.maxfps)
        {
            mainloop();
            SDL_NumJoysticks();
            if (!config.lockstep)
            {
                break;
            }
        }
        lockstep_tick_count += 10;
        enabler->last_tick = lockstep_tick_count;
        enabler->clock = lockstep_tick_count;
        lockstep_swap_arrays();
        lockstep_render_things();
        enabler->renderer->update_all();
        enabler->renderer->render();
    }
}

static bool lockstep_hooked = false;

void Hook_Update()
{
    if (!lockstep_hooked && config.lockstep)
    {
#ifdef _WIN32
        Add_Hook((void *)GetTickCount, Real_GetTickCount, (void *)Fake_GetTickCount);
#else
        Add_Hook((void *)gettimeofday, Real_gettimeofday, (void *)Fake_gettimeofday);
#endif
        Add_Hook((void *)SDL_GetTicks, Real_SDL_GetTicks, (void *)Fake_SDL_GetTicks);
        Add_Hook((void *)SDL_Delay, Real_SDL_Delay, (void *)Fake_SDL_Delay);
        lockstep_hooked = true;

        lockstep_loop();
        Hook_Shutdown();
    }
}

void Hook_Shutdown()
{
    if (!lockstep_hooked)
    {
        return;
    }

#ifdef _WIN32
    Remove_Hook((void *)GetTickCount, Real_GetTickCount, (void *)Fake_GetTickCount);
#else
    Remove_Hook((void *)gettimeofday, Real_gettimeofday, (void *)Fake_gettimeofday);
#endif
    Remove_Hook((void *)SDL_GetTicks, Real_SDL_GetTicks, (void *)Fake_SDL_GetTicks);
    Remove_Hook((void *)SDL_Delay, Real_SDL_Delay, (void *)Fake_SDL_Delay);

    lockstep_hooked = false;
}
