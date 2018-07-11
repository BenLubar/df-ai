#include "ai.h"

#include "exclusive_callback.h"

ExclusiveCallback::ExclusiveCallback(const std::string & description, size_t wait_multiplier) :
    out_proxy(),
    pull(nullptr),
    push([&](coroutine_t::pull_type & input) { init(input); }),
    wait_multiplier(wait_multiplier),
    wait_frames(0),
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
    AI::feed_key(key);
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
        out_proxy.set(pull->get());
    }
}

bool ExclusiveCallback::run(color_ostream & out)
{
    if (wait_frames)
    {
        wait_frames--;
        return false;
    }

    if (push(out))
    {
        wait_frames = wait_multiplier - 1;
        return false;
    }

    return true;
}

void ExclusiveCallback::init(coroutine_t::pull_type & input)
{
    pull = &input;
    out_proxy.set(pull->get());
    Run(out_proxy);
    out_proxy.clear();
}
