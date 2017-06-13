#include "ai.h"
#include "hooks.h"
#include "tinythread.h"

#include "df/enabler.h"
#include "df/init.h"
#include "df/renderer.h"

#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/mman.h>
#include <sys/time.h>
#endif

REQUIRE_GLOBAL(enabler);
REQUIRE_GLOBAL(init);

DFhackCExport uint32_t SDL_GetTicks(void);
DFhackCExport void SDL_Delay(uint32_t ms);

static tthread::mutex lockstep_mutex;
static volatile uint32_t lockstep_frames = 0;
static volatile uint32_t lockstep_sim = 1;

static volatile uint32_t lockstep_tick_count = 0;

#ifdef _WIN32
static char Real_GetTickCount[6];
static DWORD WINAPI Fake_GetTickCount(void)
{
    return lockstep_tick_count;
}
#else
static char Real_gettimeofday[6];
static int Fake_gettimeofday(struct timeval *tv, struct timezone *tz)
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
    lockstep_tick_count += 20;

    tthread::lock_guard<tthread::mutex> lock(lockstep_mutex);
    while (lockstep_frames != 0 || lockstep_sim == 0)
    {
        if (enabler)
        {
            SDL_SemWait(enabler->async_frombox.sem);
            if (!enabler->async_frombox.queue.empty())
            {
                auto & r = enabler->async_frombox.queue.front();
                bool complete = true;
                using async_msg = df::enabler::T_async_frombox::T_queue::T_msg;
                switch (r.msg)
                {
                case async_msg::set_fps:
                {
                    if (r.fps == 0)
                        enabler->fps = 1048576;
                    else
                        enabler->fps = r.fps;
                    enabler->fps_per_gfps = enabler->fps / enabler->gfps;

                    df::enabler::T_async_tobox::T_queue cmd;
                    cmd.cmd = df::enabler::T_async_tobox::T_queue::T_cmd::set_fps;
                    cmd.val = enabler->fps;
                    SDL_SemWait(enabler->async_tobox.sem);
                    enabler->async_tobox.queue.push_back(cmd);
                    SDL_SemPost(enabler->async_tobox.sem);
                    SDL_SemPost(enabler->async_tobox.sem_fill);

                    df::enabler::T_async_tobox::T_queue cmd2;
                    cmd2.cmd = df::enabler::T_async_tobox::T_queue::T_cmd::start;
                    SDL_SemWait(enabler->async_tobox.sem);
                    enabler->async_tobox.queue.push_back(cmd2);
                    SDL_SemPost(enabler->async_tobox.sem);
                    SDL_SemPost(enabler->async_tobox.sem_fill);
                    break;
                }
                case async_msg::set_gfps:
                {
                    if (r.fps == 0)
                        enabler->gfps = 50;
                    else
                        enabler->gfps = r.fps;
                    enabler->fps_per_gfps = enabler->fps / enabler->gfps;
                    break;
                }
                case async_msg::push_resize:
                {
                    df::enabler::T_overridden_grid_sizes sizes;
                    sizes.anon_1 = init->display.grid_x;
                    sizes.anon_2 = init->display.grid_y;
                    enabler->overridden_grid_sizes.push_back(sizes);
                    enabler->renderer->grid_resize(r.x, r.y);
                    break;
                }
                case async_msg::pop_resize:
                {
                    if (!enabler->overridden_grid_sizes.empty())
                    {
                        enabler->overridden_grid_sizes.clear();

                        SDL_SemWait(enabler->async_zoom.sem);
                        enabler->async_zoom.queue.push_back(zoom_commands::zoom_resetgrid);
                        SDL_SemPost(enabler->async_zoom.sem);
                        SDL_SemPost(enabler->async_zoom.sem_fill);
                    }
                    break;
                }
                default:
                {
                    complete = false;
                    break;
                }
                }
                if (complete)
                {
                    SDL_SemWait(enabler->async_frombox.sem_fill);
                    enabler->async_frombox.queue.pop_front();
                    SDL_SemPost(enabler->async_fromcomplete);
                }
            }
            SDL_SemPost(enabler->async_frombox.sem);
        }
        lockstep_mutex.unlock();
        tthread::this_thread::yield();
        lockstep_mutex.lock();
    }
    lockstep_sim--;
}

static void Add_Hook(void *target, char(&real)[6], const void *fake)
{
    int32_t jump_offset((char*)fake - (char*)target - 4 - 1);
#if _WIN32
    DWORD oldProtect;
    VirtualProtect((char *)target, 6, PAGE_EXECUTE_READWRITE, &oldProtect);
#else
    mprotect(target, 6, PROT_READ | PROT_WRITE | PROT_EXEC);
#endif
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
#if _WIN32
    VirtualProtect((char *)target, 6, oldProtect, &oldProtect);
#else
    mprotect(target, 6, PROT_READ | PROT_EXEC);
#endif
}

static void Remove_Hook(void *target, char(&real)[6], const void *)
{
#if _WIN32
    DWORD oldProtect;
    VirtualProtect((char *)target, 6, PAGE_EXECUTE_READWRITE, &oldProtect);
#else
    mprotect(target, 6, PROT_READ | PROT_WRITE | PROT_EXEC);
#endif
    *((char *)target + 0) = real[0];
    *((char *)target + 1) = real[1];
    *((char *)target + 2) = real[2];
    *((char *)target + 3) = real[3];
    *((char *)target + 4) = real[4];
    *((char *)target + 5) = real[5];
#if _WIN32
    VirtualProtect((char *)target, 6, oldProtect, &oldProtect);
#else
    mprotect(target, 6, PROT_READ | PROT_EXEC);
#endif
}

static bool lockstep_hooked = false;

void Hook_Update()
{
    if (lockstep_hooked != config.lockstep)
    {
        if (config.lockstep)
        {
#ifdef _WIN32
            Add_Hook(GetTickCount, Real_GetTickCount, Fake_GetTickCount);
#else
            Add_Hook(gettimeofday, Real_gettimeofday, Fake_gettimeofday);
#endif
            Add_Hook(SDL_GetTicks, Real_SDL_GetTicks, Fake_SDL_GetTicks);
            Add_Hook(SDL_Delay, Real_SDL_Delay, Fake_SDL_Delay);
            lockstep_hooked = true;
        }
        else
        {
            Hook_Shutdown();
        }
    }

    if (config.lockstep)
    {
        tthread::lock_guard<tthread::mutex> lock(lockstep_mutex);
        if (enabler)
        {
            if (enabler->flag.bits.maxfps)
            {
                lockstep_sim++;
                lockstep_frames = 0;
            }
            else
            {
                if (lockstep_frames && !enabler->async_frames)
                {
                    lockstep_sim++;
                }
                lockstep_frames = enabler->async_frames;
            }
        }
    }
}

void Hook_Shutdown()
{
    if (!lockstep_hooked)
    {
        return;
    }

#ifdef _WIN32
    Remove_Hook(GetTickCount, Real_GetTickCount, Fake_GetTickCount);
#else
    Remove_Hook(gettimeofday, Real_gettimeofday, Fake_gettimeofday);
#endif
    Remove_Hook(SDL_GetTicks, Real_SDL_GetTicks, Fake_SDL_GetTicks);
    Remove_Hook(SDL_Delay, Real_SDL_Delay, Fake_SDL_Delay);

    lockstep_hooked = false;
}
