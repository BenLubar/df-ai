#include "ai.h"
#include "hooks.h"
#include "camera.h"
#include "tinythread.h"

#include "MemAccess.h"

#include "modules/Gui.h"
#include "modules/Screen.h"

#include "df/enabler.h"
#include "df/graphic.h"
#include "df/init.h"
#include "df/interfacest.h"
#include "df/renderer.h"
#include "df/viewscreen.h"

#include <zlib.h>

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

static void lockstep_removescreen(df::viewscreen *scr)
{
    // df-ai: omitted minimap refreshing code as we don't use the minimap and we don't have the function symbols on Windows

    //FIX LINKS
    if (scr->parent)
    {
        scr->parent->child = scr->child;
    }
    if (scr->child)
    {
        scr->child->parent = scr->parent;
    }

    //WASTE SCREEN
    delete scr;
}

static void lockstep_remove_to_first()
{
    //GRAB LAST SCREEN AT THE END OF THE LIST
    df::viewscreen *lastscreen = &gview->view;
    while (lastscreen->child)
    {
        lastscreen = lastscreen->child;
    }

    //NO INTERFACE LEFT
    if (lastscreen == &gview->view)
    {
        return;
    }

    //GO AHEAD
    while (lastscreen->parent != &gview->view)
    {
        df::viewscreen *par = lastscreen->parent;
        lockstep_removescreen(lastscreen);
        lastscreen = par;
    }
}

static constexpr int32_t MOVIEBUFFSIZE = sizeof(df::interfacest::supermoviebuffer) / sizeof(df::interfacest::supermoviebuffer[0]);
static constexpr int32_t COMPMOVIEBUFFSIZE = sizeof(df::interfacest::supermoviebuffer_comp) / sizeof(df::interfacest::supermoviebuffer_comp[0]);
static constexpr int32_t SOUND_CHANNELNUM = sizeof(df::interfacest::supermovie_sound_time) / sizeof(df::interfacest::supermovie_sound_time[0]);

static bool CHECK_ERR(int err, const char* msg)
{
    if (err != Z_OK)
    {
        std::cerr << "zlib error (CMV \"" << gview->movie_file << "\" is probably corrupted): " << msg << ": " << zError(err) << std::endl;
        return true;
    }
    return false;
}

static int32_t lockstep_write_movie_chunk()
{
    int32_t inputsize = gview->supermovie_pos;
    if (inputsize > MOVIEBUFFSIZE)
    {
        inputsize = MOVIEBUFFSIZE;
    }

    //DUMP CURRENT BUFFER INTO A COMPRESSION STREAM
    z_stream c_stream;
    int err;

    c_stream.zalloc = (alloc_func)0;
    c_stream.zfree = (free_func)0;
    c_stream.opaque = (voidpf)0;

    err = deflateInit(&c_stream, Z_BEST_COMPRESSION);
    if (CHECK_ERR(err, "deflateInit"))
    {
        return 5000001;
    }

    c_stream.next_out = (Bytef*)gview->supermoviebuffer_comp;
    c_stream.avail_out = COMPMOVIEBUFFSIZE;

    c_stream.next_in = (Bytef*)gview->supermoviebuffer;
    c_stream.avail_in = inputsize;

    while (c_stream.total_in != inputsize && c_stream.total_out < COMPMOVIEBUFFSIZE)
    {
        //c_stream.avail_in = c_stream.avail_out = 1; // force small buffers
        err = deflate(&c_stream, Z_NO_FLUSH);
        if (CHECK_ERR(err, "deflate"))
        {
            return 5000001;
        }
    }

    // Finish the stream, still forcing small buffers:
    for (;;)
    {
        err = deflate(&c_stream, Z_FINISH);
        if (err == Z_STREAM_END)
        {
            break;
        }
        if (CHECK_ERR(err, "deflate"))
        {
            return 5000001;
        }
    }

    err = deflateEnd(&c_stream);
    if (CHECK_ERR(err, "deflateEnd"))
    {
        return 5000001;
    }

    int length = 0;

    if (c_stream.total_out > 0)
    {
        if (gview->first_movie_write)
        {
            //GET RID OF ANY EXISTING MOVIES IF THIS IS THE FIRST TIME THROUGH
            unlink(gview->movie_file.c_str());
        }

        //OPEN UP THE MOVIE FILE AND APPEND
        std::fstream f;
        f.open(gview->movie_file.c_str(), std::fstream::out | std::fstream::binary | std::fstream::app);

        if (f.is_open())
        {
            //WRITE A HEADER
            if (gview->first_movie_write)
            {
                const int32_t movie_version = 10001;
                f.write((const char *)&movie_version, sizeof(int32_t));

                int32_t header[3];
                header[0] = init->display.grid_x;
                header[1] = init->display.grid_y;
                extern AI *dwarfAI;
                if (dwarfAI->camera->movie_started_in_lockstep)
                {
                    header[1] *= 2;
                }
                header[3] = gview->supermovie_delayrate;
                f.write((const char *)&header, sizeof(header));

                int32_t s = gview->supermovie_sound.size();
                f.write((const char *)&s, sizeof(int32_t));
                char buf[50];
                for (s = 0; s < gview->supermovie_sound.size(); s++)
                {
                    strcpy(buf, gview->supermovie_sound.at(s)->c_str());
                    f.write(buf, sizeof(buf));
                }

                int i1, i2;
                for (i1 = 0; i1 < 200; i1++)
                {
                    for (i2 = 0; i2 < SOUND_CHANNELNUM; i2++)
                    {
                        int32_t sound_time = gview->supermovie_sound_time[i1][i2];
                        f.write((const char *)&sound_time, sizeof(int32_t));
                    }
                }
            }

            //WRITE IT
            uint32_t compsize = c_stream.total_out;
            f.write((const char *)&compsize, sizeof(uint32_t));
            f.write((const char *)gview->supermoviebuffer_comp, c_stream.total_out);

            f.seekg(0, std::ios::end);
            length = f.tellg();

            f.close();
        }
        else
        {
            gview->supermovie_on = 0;
        }

        gview->first_movie_write = 0;
    }

    return length;
}

static void lockstep_finish_movie()
{
    gview->supermovie_on = 0;
    gview->currentblocksize = 0;
    gview->nextfilepos = 0;
    gview->supermovie_pos = 0;
    extern AI *dwarfAI;
    dwarfAI->camera->check_record_status();
}

static void lockstep_handlemovie(bool flushall)
{
    //SAVE A MOVIE FRAME INTO THE CURRENT MOVIE BUFFER
    if (gview->supermovie_on == 1)
    {
        if (gview->supermovie_delaystep > 0 && !flushall)
        {
            gview->supermovie_delaystep--;
        }
        else
        {
            if (!flushall)
            {
                gview->supermovie_delaystep = gview->supermovie_delayrate;
            }

            if (!flushall || gview->supermovie_delaystep == 0)
            {
                //SAVING CHARACTERS, THEN COLORS
                short x2, y2;
                for (x2 = 0; x2 < init->display.grid_x; x2++)
                {
                    for (y2 = 0; y2 < init->display.grid_y; y2++)
                    {
                        gview->supermoviebuffer[gview->supermovie_pos] = gps->screen[x2 * gps->dimy * 4 + y2 * 4 + 0];

                        gview->supermovie_pos++;
                    }
                }
                extern AI *dwarfAI;
                if (dwarfAI->camera->movie_started_in_lockstep)
                {
                    for (x2 = 0; x2 < init->display.grid_x; x2++)
                    {
                        for (y2 = 0; y2 < init->display.grid_y; y2++)
                        {
                            gview->supermoviebuffer[gview->supermovie_pos] = x2 < 80 && y2 < 25 ? dwarfAI->lockstep_log_buffer[y2][x2] : ' ';

                            gview->supermovie_pos++;
                        }
                    }
                }
                char frame_col;
                for (x2 = 0; x2 < init->display.grid_x; x2++)
                {
                    for (y2 = 0; y2 < init->display.grid_y; y2++)
                    {
                        frame_col = gps->screen[x2 * gps->dimy * 4 + y2 * 4 + 1];
                        frame_col |= (gps->screen[x2 * gps->dimy * 4 + y2 * 4 + 2] << 3);
                        if (gps->screen[x2 * gps->dimy * 4 + y2 * 4 + 3])
                        {
                            frame_col |= 64;
                        }
                        gview->supermoviebuffer[gview->supermovie_pos] = frame_col;

                        gview->supermovie_pos++;
                    }
                }
                if (dwarfAI->camera->movie_started_in_lockstep)
                {
                    for (x2 = 0; x2 < init->display.grid_x; x2++)
                    {
                        for (y2 = 0; y2 < init->display.grid_y; y2++)
                        {
                            gview->supermoviebuffer[gview->supermovie_pos] = 7;

                            gview->supermovie_pos++;
                        }
                    }
                }
            }

            int frame_size = init->display.grid_x * init->display.grid_y * 2;
            extern AI *dwarfAI;
            if (dwarfAI->camera->movie_started_in_lockstep)
            {
                frame_size *= 2;
            }
            if (gview->supermovie_pos + frame_size >= MOVIEBUFFSIZE || flushall)
            {
                int length = lockstep_write_movie_chunk();

                if (length > 5000000)
                {
                    lockstep_finish_movie();
                }
                else
                {
                    gview->supermovie_pos = 0;
                }
            }
        }
    }
}

static bool lockstep_mainloop()
{
    //NO INTERFACE LEFT, QUIT
    if (gview->view.child == 0)return true;

    //GRAB CURRENT SCREEN AT THE END OF THE LIST
    df::viewscreen *currentscreen = Gui::getCurViewscreen(false);
    //MOVE SCREENS BACK
    switch (currentscreen->breakdown_level) {
    case interface_breakdown_types::NONE:
    {
        currentscreen->logic();

        if (currentscreen->movies_okay())
        {
            //HANDLE MOVIES
            lockstep_handlemovie(false);
        }

        // df-ai: don't process input
        break;
    }
    case interface_breakdown_types::QUIT:
    {
        lockstep_handlemovie(true);
        return true;
    }
    case interface_breakdown_types::STOPSCREEN:
    {
        if (currentscreen->movies_okay())
        {
            //HANDLE MOVIES
            lockstep_handlemovie(false);
        }

        lockstep_removescreen(currentscreen);

        break;
    }
    case interface_breakdown_types::TOFIRST:
    {
        if (currentscreen->movies_okay())
        {
            //HANDLE MOVIES
            lockstep_handlemovie(false);
        }

        lockstep_remove_to_first();

        break;
    }
    }

    return 0;
}


static void lockstep_loop()
{
    while (config.lockstep)
    {
        lockstep_mainloop();
        SDL_NumJoysticks();
        if (!config.lockstep)
        {
            break;
        }
        lockstep_tick_count += 10;
        if (!enabler->flag.bits.maxfps)
        {
            lockstep_mainloop();
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
