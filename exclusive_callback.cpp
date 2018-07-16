#include "ai.h"
#include "exclusive_callback.h"

#include "modules/Gui.h"

#include "df/viewscreen.h"

ExclusiveCallback::ExclusiveCallback(const std::string & description, size_t wait_multiplier) :
    out_proxy(),
    pull(nullptr),
    push([&](coroutine_t::pull_type & input) { init(input); }),
    wait_multiplier(wait_multiplier),
    wait_frames(0),
    did_delay(true),
    feed_keys(),
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

void ExclusiveCallback::Key(df::interface_key key)
{
    feed_keys.insert(key);
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
    AI::feed_char(c);
    Delay();
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
        throw std::exception("previous iteration of exclusive callback did not call Delay.");
    }
    did_delay = false;
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
        Gui::getCurViewscreen()->feed(&feed_keys);
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
