#include "ai.h"
#include "hooks.h"
#include "camera.h"
#include "tinythread.h"

#include "MemAccess.h"
#include "SDL_events.h"

#include "modules/Gui.h"
#include "modules/Screen.h"

#include "df/enabler.h"
#include "df/graphic.h"
#include "df/init.h"
#include "df/interfacest.h"
#include "df/renderer.h"
#include "df/viewscreen_movieplayerst.h"

#include <zlib.h>

#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/mman.h>
#include <sys/time.h>
#endif

#ifdef DFAI_RELEASE
#define LOCKSTEP_DEBUG(msg)
#else
#define LOCKSTEP_DEBUG(msg) do { if (config.lockstep_debug) { Core::getInstance().getConsole() << "[df-ai] LOCKSTEP DEBUG: " << msg << std::endl; } } while (0)
#endif

REQUIRE_GLOBAL(enabler);
REQUIRE_GLOBAL(gps);
REQUIRE_GLOBAL(gview);
REQUIRE_GLOBAL(init);
REQUIRE_GLOBAL(ui);

DFhackCExport uint32_t SDL_GetTicks(void);
DFhackCExport int SDL_SemTryWait(void *sem);
DFhackCExport uint32_t SDL_ThreadID(void);
DFhackCExport void SDL_Quit(void);

static volatile uint32_t lockstep_tick_count = 0;
volatile bool lockstep_hooked = false;
volatile bool disabling_plugin = false;
volatile bool unloading_plugin = false;
extern bool & enabled;
static volatile bool lockstep_want_shutdown = false;
static volatile bool lockstep_ready_for_shutdown = false;
static volatile bool lockstep_want_shutdown_now = false;

#ifdef _WIN32
static uint8_t Real_GetTickCount[6];
static DWORD WINAPI Fake_GetTickCount(void)
{
    return lockstep_tick_count;
}
#else
static uint8_t Real_gettimeofday[6];
static int Fake_gettimeofday(struct timeval *tv, struct timezone *) throw()
{
    tv->tv_sec = lockstep_tick_count / 1000;
    tv->tv_usec = (lockstep_tick_count % 1000) * 1000;
    return 0;
}
#endif

static uint8_t Real_SDL_GetTicks[6];
static uint32_t Fake_SDL_GetTicks()
{
    return lockstep_tick_count;
}

static void Add_Hook(void *target, uint8_t(&real)[6], const void *fake)
{
    MemoryPatcher patcher;
    patcher.verifyAccess(target, 6, true);

    int32_t jump_offset((uint8_t *)fake - (uint8_t *)target - 4 - 1);
    real[0] = *((uint8_t *)target + 0);
    real[1] = *((uint8_t *)target + 1);
    real[2] = *((uint8_t *)target + 2);
    real[3] = *((uint8_t *)target + 3);
    real[4] = *((uint8_t *)target + 4);
    real[5] = *((uint8_t *)target + 5);
    *((uint8_t *)target + 0) = 0xE9;
    *((uint8_t *)target + 1) = *((uint8_t *)&jump_offset + 0);
    *((uint8_t *)target + 2) = *((uint8_t *)&jump_offset + 1);
    *((uint8_t *)target + 3) = *((uint8_t *)&jump_offset + 2);
    *((uint8_t *)target + 4) = *((uint8_t *)&jump_offset + 3);
    *((uint8_t *)target + 5) = 0x90;

#if defined(_WIN32)
    FlushInstructionCache(GetCurrentProcess(), target, 6);
#endif
}

static void Remove_Hook(void *target, uint8_t(&real)[6], const void *)
{
    MemoryPatcher patcher;
    patcher.verifyAccess(target, 6, true);

    *((uint8_t *)target + 0) = real[0];
    *((uint8_t *)target + 1) = real[1];
    *((uint8_t *)target + 2) = real[2];
    *((uint8_t *)target + 3) = real[3];
    *((uint8_t *)target + 4) = real[4];
    *((uint8_t *)target + 5) = real[5];

#if defined(_WIN32)
    FlushInstructionCache(GetCurrentProcess(), target, 6);
#endif
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
    swap3(r->screentexpos, r->screentexpos_old, gps->screentexpos);
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

    while (int32_t(c_stream.total_in) != inputsize && c_stream.total_out < COMPMOVIEBUFFSIZE)
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
                const int32_t movie_version = 10000;
                f.write((const char *)&movie_version, sizeof(int32_t));

                int32_t header[3];
                header[0] = init->display.grid_x;
                header[1] = init->display.grid_y;
                extern AI *dwarfAI;
                if (dwarfAI->camera->movie_started_in_lockstep)
                {
                    header[1] *= 2;
                }
                header[2] = gview->supermovie_delayrate;
                f.write((const char *)&header, sizeof(header));
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
                extern AI *dwarfAI;
                //SAVING CHARACTERS, THEN COLORS
                short x2, y2;
                for (x2 = 0; x2 < init->display.grid_x; x2++)
                {
                    for (y2 = 0; y2 < init->display.grid_y; y2++)
                    {
                        gview->supermoviebuffer[gview->supermovie_pos] = gps->screen[x2 * gps->dimy * 4 + y2 * 4 + 0];

                        gview->supermovie_pos++;
                    }
                    if (dwarfAI && dwarfAI->camera->movie_started_in_lockstep)
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
                    if (dwarfAI && dwarfAI->camera->movie_started_in_lockstep)
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
            if (gview->supermovie_pos + frame_size >= MOVIEBUFFSIZE || flushall || !dwarfAI || !dwarfAI->camera->movie_started_in_lockstep)
            {
                int length = lockstep_write_movie_chunk();

                if (length > 5000000 || !dwarfAI || !dwarfAI->camera->movie_started_in_lockstep)
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

static bool lockstep_drain_sdl()
{
    SDL::Event event;

    while (SDL_PollEvent(&event))
    {
        LOCKSTEP_DEBUG("event: " << event.type);
        // Handle SDL events
        switch ((SDL::EventType)event.type) {
        case SDL::ET_KEYDOWN:
        case SDL::ET_KEYUP:
            break;
        case SDL::ET_QUIT:
            return true;
        case SDL::ET_MOUSEBUTTONDOWN:
        case SDL::ET_MOUSEBUTTONUP:
        case SDL::ET_MOUSEMOTION:
            break;
        case SDL::ET_ACTIVEEVENT:
        case SDL::ET_VIDEOEXPOSE:
            break;
        case SDL::ET_VIDEORESIZE:
            enabler->renderer->resize(event.resize.w, event.resize.h);
            break;
        default:
            break;
        }
    }

    return false;
}

static bool Hook_Want_Disable()
{
    if (!config.lockstep || !enabled || disabling_plugin || unloading_plugin)
    {
        lockstep_want_shutdown = true;
    }
    return lockstep_want_shutdown;
}

static void lockstep_loop()
{
    while (!Hook_Want_Disable())
    {
        LOCKSTEP_DEBUG("calling mainloop (A)");
        if (lockstep_mainloop())
        {
            LOCKSTEP_DEBUG("want shutdown (A)");
            Hook_Shutdown_Now();
            break;
        }
        LOCKSTEP_DEBUG("calling DFHack (A)");
        SDL_NumJoysticks();
        if (Hook_Want_Disable())
        {
            LOCKSTEP_DEBUG("want disable (A)");
            break;
        }
        lockstep_tick_count += 10;
        if (!enabler->flag.bits.maxfps && !ui->main.autosave_request)
        {
            LOCKSTEP_DEBUG("calling mainloop (B)");
            if (lockstep_mainloop())
            {
                LOCKSTEP_DEBUG("want shutdown (B)");
                Hook_Shutdown_Now();
                break;
            }
            LOCKSTEP_DEBUG("calling DFHack (B)");
            SDL_NumJoysticks();
            if (Hook_Want_Disable())
            {
                LOCKSTEP_DEBUG("want disable (B)");
                break;
            }
        }
        else
        {
            LOCKSTEP_DEBUG((enabler->flag.bits.maxfps ? "maxfps" : "autosave_request"));
        }
        lockstep_tick_count += 10;
        enabler->last_tick = lockstep_tick_count;
        enabler->clock = lockstep_tick_count;
        LOCKSTEP_DEBUG("draining events");
        if (lockstep_drain_sdl())
        {
            LOCKSTEP_DEBUG("user requested quit");
            break;
        }
        LOCKSTEP_DEBUG("swap_arrays");
        lockstep_swap_arrays();
        LOCKSTEP_DEBUG("render_things");
        lockstep_render_things();
        LOCKSTEP_DEBUG("update_all");
        enabler->renderer->update_all();
        LOCKSTEP_DEBUG("render");
        enabler->renderer->render();
    }

    extern AI *dwarfAI;
    if (dwarfAI && dwarfAI->camera->movie_started_in_lockstep)
    {
        LOCKSTEP_DEBUG("stopping movie started in lockstep to avoid corruption");
        // Stop the current CMV so it doesn't get corrupted on the next frame.
        lockstep_handlemovie(true);
    }
}

static struct df_ai_renderer : public df::renderer
{
    df_ai_renderer(df::renderer *real) : real_renderer(real)
    {
        copy_fields(real_renderer, this);
    }

    df::renderer *real_renderer;

    static void copy_fields(df::renderer *from, df::renderer *to)
    {
        to->screen = from->screen;
        to->screentexpos = from->screentexpos;
        to->screentexpos_addcolor = from->screentexpos_addcolor;
        to->screentexpos_grayscale = from->screentexpos_grayscale;
        to->screentexpos_cf = from->screentexpos_cf;
        to->screentexpos_cbr = from->screentexpos_cbr;
        to->screen_old = from->screen_old;
        to->screentexpos_old = from->screentexpos_old;
        to->screentexpos_addcolor_old = from->screentexpos_addcolor_old;
        to->screentexpos_grayscale_old = from->screentexpos_grayscale_old;
        to->screentexpos_cf_old = from->screentexpos_cf_old;
        to->screentexpos_cbr_old = from->screentexpos_cbr_old;
    }

    virtual void update_tile(int32_t x, int32_t y)
    {
        copy_fields(this, real_renderer);
        real_renderer->update_tile(x, y);
        copy_fields(real_renderer, this);
    }
    virtual void update_all()
    {
        copy_fields(this, real_renderer);
        real_renderer->update_all();
        copy_fields(real_renderer, this);
    }
    virtual void render()
    {
        copy_fields(this, real_renderer);
        real_renderer->render();
        copy_fields(real_renderer, this);
    }
    virtual void set_fullscreen()
    {
        copy_fields(this, real_renderer);
        real_renderer->set_fullscreen();
        copy_fields(real_renderer, this);
    }
    virtual void zoom(df::zoom_commands)
    {
        copy_fields(this, real_renderer);

        enabler->renderer = real_renderer;

        LOCKSTEP_DEBUG("lockstep_loop started");
        lockstep_loop();
        LOCKSTEP_DEBUG("lockstep_loop ended");

        lockstep_ready_for_shutdown = true;
    }
    virtual void resize(int32_t w, int32_t h)
    {
        copy_fields(this, real_renderer);
        real_renderer->resize(w, h);
        copy_fields(real_renderer, this);
    }
    virtual void grid_resize(int32_t w, int32_t h)
    {
        copy_fields(this, real_renderer);
        real_renderer->grid_resize(w, h);
        copy_fields(real_renderer, this);
    }
    virtual bool get_mouse_coords(int32_t *x, int32_t *y)
    {
        copy_fields(this, real_renderer);
        bool res = real_renderer->get_mouse_coords(x, y);
        copy_fields(real_renderer, this);
        return res;
    };
    virtual bool uses_opengl()
    {
        copy_fields(this, real_renderer);
        bool res = real_renderer->uses_opengl();
        copy_fields(real_renderer, this);
        return res;
    }
} *lockstep_renderer = nullptr;

// We need to access Core private methods and the egg_* set of functions aren't usually compiled in DFHack, so we steal their names:

int egg_init(void)
{
    LOCKSTEP_DEBUG("calling DisclaimSuspend");
    Core::getInstance().DisclaimSuspend(1000000);
    return 0;
}

int egg_shutdown(void)
{
    LOCKSTEP_DEBUG("calling ClaimSuspend");
    Core::getInstance().ClaimSuspend(true);
    return 0;
}

void Hook_Update()
{
    if (SDL_ThreadID() == enabler->renderer_threadid)
    {
        return;
    }

    if (lockstep_want_shutdown)
    {
        while (!lockstep_ready_for_shutdown)
        {
            tthread::this_thread::yield();
        }
        LOCKSTEP_DEBUG("trying to unhook");
        lockstep_want_shutdown = false;
        lockstep_ready_for_shutdown = false;
        Hook_Shutdown();
    }
    else if (!lockstep_hooked && config.lockstep && !Hook_Want_Disable() && !lockstep_want_shutdown_now)
    {
        LOCKSTEP_DEBUG("trying to hook");
        if (init->display.flag.is_set(init_display_flags::TEXT))
        {
            auto & out = Core::getInstance().getConsole();
            out << COLOR_LIGHTRED << "[df-ai] lockstep mode does not work with PRINT_MODE:TEXT. Disabling lockstep in the df-ai config.";
            out << COLOR_RESET << std::endl;
            config.set(out, config.lockstep, false);
            return;
        }

        if (strict_virtual_cast<df::viewscreen_movieplayerst>(Gui::getCurViewscreen(false)))
        {
            LOCKSTEP_DEBUG("not hooking while movie player is present");
            return;
        }

        lockstep_hooked = true;

#ifdef _WIN32
        lockstep_tick_count = GetTickCount();
        LOCKSTEP_DEBUG("initial lockstep_tick_count is " << lockstep_tick_count);
        Add_Hook((void *)GetTickCount, Real_GetTickCount, (void *)Fake_GetTickCount);
#else
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        lockstep_tick_count = tv.tv_sec * 1000 + tv.tv_usec / 1000;
        LOCKSTEP_DEBUG("initial lockstep_tick_count is " << lockstep_tick_count);
        Add_Hook((void *)gettimeofday, Real_gettimeofday, (void *)Fake_gettimeofday);
#endif
        LOCKSTEP_DEBUG("hooked time");
        Add_Hook((void *)SDL_GetTicks, Real_SDL_GetTicks, (void *)Fake_SDL_GetTicks);
        LOCKSTEP_DEBUG("hooked ticks");
        enabler->outstanding_gframes = 0;
        enabler->outstanding_frames = 0;

        lockstep_renderer = new df_ai_renderer(enabler->renderer);
        enabler->renderer = lockstep_renderer;

        egg_init();

        SDL_SemWait(enabler->async_zoom.sem);
        enabler->async_zoom.queue.push_back(zoom_commands::zoom_reset);
        SDL_SemPost(enabler->async_zoom.sem);
        SDL_SemPost(enabler->async_zoom.sem_fill);

        while (lockstep_hooked && !lockstep_want_shutdown)
        {
            while (SDL_SemTryWait(enabler->async_tobox.sem_fill) == 0)
            {
                SDL_SemWait(enabler->async_tobox.sem);
                auto msg = enabler->async_tobox.queue.front();
                enabler->async_tobox.queue.pop_front();
                switch (msg.cmd)
                {
                case df::enabler::T_async_tobox::T_queue::pause:
                case df::enabler::T_async_tobox::T_queue::render:
                {
                    df::enabler::T_async_frombox::T_queue complete;
                    complete.msg = df::enabler::T_async_frombox::T_queue::complete;
                    SDL_SemWait(enabler->async_frombox.sem);
                    enabler->async_frombox.queue.push_back(complete);
                    SDL_SemPost(enabler->async_frombox.sem);
                    SDL_SemPost(enabler->async_frombox.sem_fill);
                    break;
                }
                case df::enabler::T_async_tobox::T_queue::start:
                case df::enabler::T_async_tobox::T_queue::inc:
                case df::enabler::T_async_tobox::T_queue::set_fps:
                    break;
                }
                SDL_SemPost(enabler->async_tobox.sem);
            }

            tthread::this_thread::sleep_for(tthread::chrono::seconds(1));
        }

        egg_shutdown();
    }
}

extern bool check_enabled(color_ostream & out);

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
    LOCKSTEP_DEBUG("unhooked ticks");

    if (lockstep_renderer == enabler->renderer)
    {
        enabler->renderer = lockstep_renderer->real_renderer;
    }
    LOCKSTEP_DEBUG("deleting df_ai_renderer");
    delete lockstep_renderer;
    lockstep_renderer = nullptr;

    lockstep_hooked = false;

    if (lockstep_want_shutdown_now)
    {
        if (df::viewscreen *screen = Gui::getCurViewscreen(true))
        {
            screen->breakdown_level = interface_breakdown_types::QUIT;
        }
    }
    else if (disabling_plugin)
    {
        enabled = false;
        disabling_plugin = false;
        check_enabled(Core::getInstance().getConsole());
    }
}

void Hook_Shutdown_Now()
{
    lockstep_want_shutdown = true;
    lockstep_want_shutdown_now = true;
}
