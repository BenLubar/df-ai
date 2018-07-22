#include "ai.h"
#include "exclusive_callback.h"

#include "modules/Gui.h"
#include "modules/Screen.h"

#include "df/viewscreen.h"

ExclusiveCallback::ExclusiveCallback(const std::string & description, size_t wait_multiplier) :
    out_proxy(),
    pull(nullptr),
    push([&](coroutine_t::pull_type & input) { init(input); }),
    wait_multiplier(wait_multiplier),
    wait_frames(0),
    did_delay(true),
    feed_keys(),
    expectedScreen(),
    expectedFocus(),
    expectedParentFocus(),
    description(description)
{
    if (wait_multiplier < 1)
    {
        wait_multiplier = 1;
    }
}

ExclusiveCallback::~ExclusiveCallback()
{
}

void ExclusiveCallback::KeyNoDelay(df::interface_key key)
{
    feed_keys.push_back(key);
}

void ExclusiveCallback::Key(df::interface_key key)
{
    checkScreen();
    KeyNoDelay(key);
    Delay();
}

const static char safe_char[128] =
{
    'C', 'u', 'e', 'a', 'a', 'a', 'a', 'c', 'e', 'e', 'e', 'i', 'i', 'i', 'A', 'A',
    'E', 0, 0, 'o', 'o', 'o', 'u', 'u', 'y', 'O', 'U', 0, 0, 0, 0, 0,
    'a', 'i', 'o', 'u', 'n', 'N', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

void ExclusiveCallback::Char(char c)
{
    if (c < 0)
    {
        c = safe_char[(uint8_t)c - 128];
    }
    Key(Screen::charToKey(c));
}

void ExclusiveCallback::Delay(size_t frames)
{
    for (size_t i = 0; i < frames; i++)
    {
        out_proxy.clear();
        out_proxy.set(*(*pull)().get());
    }
    did_delay = true;
}

void ExclusiveCallback::AssertDelayed()
{
    if (!did_delay)
    {
        throw std::logic_error("previous iteration of exclusive callback did not call Delay.");
    }
    did_delay = false;
}

void ExclusiveCallback::checkScreen()
{
    if (!expectedScreen)
    {
        return;
    }

    df::viewscreen *curview = Gui::getCurViewscreen(true);

    bool bad = !expectedScreen->is_instance(curview);
    bad = bad || (!expectedFocus.empty() && Gui::getFocusString(curview) != expectedFocus);
    bad = bad || (!expectedParentFocus.empty() && Gui::getFocusString(curview->parent) != expectedFocus);

    if (bad)
    {
        out_proxy << "[FATAL ERROR] expected screen to be " << expectedScreen->getName();
        if (!expectedFocus.empty())
        {
            out_proxy << ":" << expectedFocus;
        }
        if (!expectedParentFocus.empty())
        {
            out_proxy << ":" << expectedParentFocus;
        }
        out_proxy << ", but it is " << virtual_identity::get(curview)->getName();
        if (!expectedFocus.empty())
        {
            out_proxy << ":" << Gui::getFocusString(curview);
        }
        if (!expectedParentFocus.empty())
        {
            out_proxy << ":" << Gui::getFocusString(curview->parent);
        }
        out_proxy << std::endl;

        // Force the debugger to breakpoint. This will probably crash if there's no debugger attached.
#if WIN32
        __debugbreak();
#else
        __asm__ volatile("int $0x03");
#endif
    }
}

bool ExclusiveCallback::run(color_ostream & out)
{
    if (wait_frames)
    {
        wait_frames--;
        return false;
    }

    bool done = !!push(&out);
    if (!feed_keys.empty())
    {
        for (auto key : feed_keys)
        {
            Gui::getCurViewscreen(true)->feed_key(key);
        }
        feed_keys.clear();
    }

    if (done)
    {
        wait_frames = wait_multiplier - 1;
        return false;
    }

    return true;
}

void ExclusiveCallback::init(coroutine_t::pull_type & input)
{
    pull = &input;
    out_proxy.set(*pull->get());
    Run(out_proxy);
    out_proxy.clear();
}
