#pragma once

#include "dfhack_shared.h"

#include <functional>

struct OnupdateCallback
{
    std::function<bool(color_ostream &)> callback;
    int32_t ticklimit;
    int32_t minyear;
    int32_t minyeartick;
    std::string description;
    bool hasTickLimit;

    OnupdateCallback(const std::string & descr, std::function<bool(color_ostream &)> cb);
    OnupdateCallback(const std::string & descr, std::function<bool(color_ostream &)> cb, int32_t tl, int32_t initdelay = 0);

    bool check_run(color_ostream & out, int32_t year, int32_t yeartick);
};

struct OnstatechangeCallback
{
    std::function<bool(color_ostream &, state_change_event)> cb;
    std::string description;

    OnstatechangeCallback(const std::string & descr, std::function<bool(color_ostream &, state_change_event)> cb);
};

class ExclusiveCallback;

struct EventManager
{
public:
    EventManager();
    ~EventManager();

    OnupdateCallback *onupdate_register(const std::string & descr, int32_t ticklimit, int32_t initialtickdelay, std::function<void(color_ostream &)> b);
    OnupdateCallback *onupdate_register_once(const std::string & descr, int32_t ticklimit, int32_t initialtickdelay, std::function<bool(color_ostream &)> b);
    OnupdateCallback *onupdate_register_once(const std::string & descr, int32_t ticklimit, std::function<bool(color_ostream &)> b);
    OnupdateCallback *onupdate_register_once(const std::string & descr, std::function<bool(color_ostream &)> b);
    void onupdate_unregister(OnupdateCallback *&b);

    OnstatechangeCallback *onstatechange_register(const std::string & descr, std::function<void(color_ostream &, state_change_event)> b);
    OnstatechangeCallback *onstatechange_register_once(const std::string & descr, std::function<bool(color_ostream &, state_change_event)> b);
    void onstatechange_unregister(OnstatechangeCallback *&b);

    bool register_exclusive(std::unique_ptr<ExclusiveCallback> && cb, bool force = false);
    void queue_exclusive(std::unique_ptr<ExclusiveCallback> && cb);
    inline bool has_exclusive() const { return exclusive != nullptr; }
    template<typename E>
    inline bool each_exclusive(std::function<bool(const E *)> fn) const
    {
        if (auto e = dynamic_cast<E *>(exclusive.get()))
        {
            if (fn(e))
            {
                return true;
            }
        }

        for (auto & queued : exclusive_queue)
        {
            if (auto e = dynamic_cast<E *>(queued.get()))
            {
                if (fn(e))
                {
                    return true;
                }
            }
        }

        return false;
    }
    template<typename E>
    inline bool has_exclusive(bool allow_queued = false) const
    {
        if (!allow_queued)
        {
            return dynamic_cast<E *>(exclusive.get()) != nullptr;
        }

        return each_exclusive<E>([](const E *) -> bool { return true; });
    }
    std::string status();
    void report(std::ostream & out, bool html);

    void onstatechange(color_ostream & out, state_change_event event);
    void onupdate(color_ostream & out);
private:
    friend class AI;
    void clear();

    std::unique_ptr<ExclusiveCallback> exclusive;
    std::unique_ptr<ExclusiveCallback> delay_delete_exclusive;
    std::list<std::unique_ptr<ExclusiveCallback>> exclusive_queue;
    std::vector<OnupdateCallback *> onupdate_list;
    std::vector<OnstatechangeCallback *> onstatechange_list;
};

extern EventManager events;
