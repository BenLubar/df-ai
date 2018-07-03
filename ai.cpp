#include "ai.h"
#include "population.h"
#include "plan.h"
#include "stocks.h"
#include "camera.h"
#include "embark.h"
#include "trade.h"

#include "modules/Gui.h"
#include "modules/Screen.h"

#include "df/enabler.h"
#include "df/viewscreen_dwarfmodest.h"
#include "df/viewscreen_movieplayerst.h"
#include "df/viewscreen_optionst.h"
#include "df/world.h"

REQUIRE_GLOBAL(enabler);
REQUIRE_GLOBAL(pause_state);
REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

AI::AI() :
    rng(0),
    logger("df-ai.log", std::ofstream::out | std::ofstream::app),
    eventsJson(),
    pop(new Population(this)),
    plan(new Plan(this)),
    stocks(new Stocks(this)),
    camera(new Camera(this)),
    trade(new Trade(this)),
    status_onupdate(nullptr),
    pause_onupdate(nullptr),
    tag_enemies_onupdate(nullptr),
    seen_focus(),
    seen_cvname(),
    last_good_x(-1),
    last_good_y(-1),
    last_good_z(-1),
    skip_persist(false)
{
    seen_cvname.insert("viewscreen_dwarfmodest");
    Gui::getViewCoords(last_good_x, last_good_y, last_good_z);

    for (int32_t y = 0; y < 25; y++)
    {
        for (int32_t x = 0; x < 80; x++)
        {
            lockstep_log_buffer[y][x] = ' ';
        }
    }

    if (config.random_embark)
    {
        events.register_exclusive(new EmbarkExclusive(this));
    }
}

AI::~AI()
{
    delete trade;
    delete camera;
    delete stocks;
    delete plan;
    delete pop;
    logger.close();
    events.clear();
}

bool AI::feed_key(df::viewscreen *view, df::interface_key key)
{
    interface_key_set keys;
    keys.insert(key);
    view->feed(&keys);
    return !keys.count(key);
}

bool AI::feed_key(df::interface_key key)
{
    return feed_key(Gui::getCurViewscreen(true), key);
}

bool AI::feed_char(df::viewscreen *view, char ch)
{
    return feed_key(view, Screen::charToKey(ch));
}

bool AI::feed_char(char ch)
{
    return feed_key(Screen::charToKey(ch));
}

bool AI::is_dwarfmode_viewscreen()
{
    if (ui->main.mode != ui_sidebar_mode::Default)
        return false;
    if (!world->status.popups.empty())
        return false;
    if (!strict_virtual_cast<df::viewscreen_dwarfmodest>(Gui::getCurViewscreen(true)))
        return false;
    return true;
}

command_result AI::startup(color_ostream & out)
{
    command_result res = Core::getInstance().runCommand(out, "disable confirm");;
    if (res == CR_OK && !config.manage_labors.empty())
        res = Core::getInstance().runCommand(out, "enable " + config.manage_labors);
    if (res == CR_OK && config.manage_labors == "autolabor")
        res = Core::getInstance().runCommand(out, "multicmd autolabor DETAIL 1 5 ; autolabor PLANT 5 200");
    if (res == CR_OK && config.manage_labors == "labormanager")
        res = Core::getInstance().runCommand(out, "multicmd labormanager max DETAIL 5 ; labormanager priority PLANT 60 ; labormanager priority MINE 30");
    if (res == CR_OK)
        res = pop->startup(out);
    if (res == CR_OK)
        res = plan->startup(out);
    if (res == CR_OK)
        res = stocks->startup(out);
    if (res == CR_OK)
        res = camera->startup(out);
    return res;
}

class AbandonExclusive : public ExclusiveCallback
{
public:
    AbandonExclusive() : ExclusiveCallback("abandon", 2) {}

    virtual void Run(color_ostream &)
    {
        Do([&]()
        {
            auto view = df::allocate<df::viewscreen_optionst>();

            // TODO: These are the options from regular fortress mode. Are they different during a siege?
            view->options.push_back(df::viewscreen_optionst::Return);
            view->options.push_back(df::viewscreen_optionst::Save);
            view->options.push_back(df::viewscreen_optionst::KeyBindings);
            view->options.push_back(df::viewscreen_optionst::ExportImage);
            view->options.push_back(df::viewscreen_optionst::MusicSound);
            view->options.push_back(df::viewscreen_optionst::AbortRetire);
            view->options.push_back(df::viewscreen_optionst::Abandon);

            Screen::show(std::unique_ptr<df::viewscreen>(view));
        });

        Delay();

        Do([&]()
        {
            auto view = virtual_cast<df::viewscreen_optionst>(Gui::getCurViewscreen(true));
            if (!view)
            {
                return;
            }

            auto option = std::find(view->options.begin(), view->options.end(), df::viewscreen_optionst::Abandon);
            MoveToItem(view->sel_idx, int32_t(option - view->options.begin()));
        });

        Key(interface_key::SELECT);
        Key(interface_key::MENU_CONFIRM);

        // current view switches to a textviewer at this point
        Key(interface_key::SELECT);
    }
};

void AI::abandon(color_ostream &)
{
    events.register_exclusive(new AbandonExclusive(), true);
}

void AI::timeout_sameview(int32_t seconds, std::function<void(color_ostream &)> cb)
{
    // allow exclusive views to unpause the game
    if (events.has_exclusive())
    {
        cb(Core::getInstance().getConsole());
        return;
    }

    df::viewscreen *curscreen = Gui::getCurViewscreen(true);
    std::string name("unknown viewscreen");
    if (auto hack = dfhack_viewscreen::try_cast(curscreen))
    {
        name = "dfhack/" + hack->getFocusString();
    }
    else if (virtual_identity *ident = virtual_identity::get(curscreen))
    {
        name = ident->getName();
    }
    int32_t *counter = new int32_t(enabler->fps * seconds);

    events.onupdate_register_once("timeout_sameview on " + name, [this, curscreen, counter, cb](color_ostream & out) -> bool
    {
        if (auto view = strict_virtual_cast<df::viewscreen_movieplayerst>(Gui::getCurViewscreen(true)))
        {
            if (!view->is_playing)
            {
                Screen::dismiss(view);
                camera->check_record_status();
            }
        }
        if (Gui::getCurViewscreen(true) != curscreen)
        {
            delete counter;
            return true;
        }

        if (--*counter <= 0)
        {
            delete counter;
            cb(out);
            return true;
        }
        return false;
    });
}

static int32_t time_paused = 0;

command_result AI::onupdate_register(color_ostream & out)
{
    command_result res = CR_OK;
    if (res == CR_OK)
        res = pop->onupdate_register(out);
    if (res == CR_OK)
        res = plan->onupdate_register(out);
    if (res == CR_OK)
        res = stocks->onupdate_register(out);
    if (res == CR_OK)
        res = camera->onupdate_register(out);
    if (res == CR_OK)
    {
        status_onupdate = events.onupdate_register("df-ai status", 3 * 28 * 1200, 3 * 28 * 1200, [this](color_ostream & out) { debug(out, status()); });
        time_paused = 0;
        pause_onupdate = events.onupdate_register_once("df-ai unpause", [this](color_ostream &) -> bool
        {
            if (!*pause_state && world->status.popups.empty())
            {
                Gui::getViewCoords(last_good_x, last_good_y, last_good_z);
                time_paused = 0;
                return false;
            }

            time_paused++;

            if (time_paused == enabler->fps * 10)
            {
                timeout_sameview(10, [this](color_ostream &) { unpause(); });
                time_paused = -enabler->fps;
            }

            return false;
        });
        tag_enemies_onupdate = events.onupdate_register("df-ai tag_enemies", 1200, 1200, [this](color_ostream & out) { tag_enemies(out); });
        events.onstatechange_register_once("world unload watcher", [this](color_ostream & out, state_change_event st) -> bool
        {
            if (st == SC_WORLD_UNLOADED)
            {
                debug(out, "world unloaded, disabling self");
                onupdate_unregister(out);
                return true;
            }
            statechanged(out, st);
            return false;
        });
    }
    return res;
}

command_result AI::onupdate_unregister(color_ostream & out)
{
    command_result res = CR_OK;
    if (res == CR_OK)
        res = camera->onupdate_unregister(out);
    if (res == CR_OK)
        res = stocks->onupdate_unregister(out);
    if (res == CR_OK)
        res = plan->onupdate_unregister(out);
    if (res == CR_OK)
        res = pop->onupdate_unregister(out);
    if (res == CR_OK)
    {
        events.onupdate_unregister(status_onupdate);
        events.onupdate_unregister(pause_onupdate);
        events.onupdate_unregister(tag_enemies_onupdate);
    }
    return res;
}

command_result AI::persist(color_ostream & out)
{
    command_result res = CR_OK;
    if (skip_persist)
        return res;

    if (res == CR_OK)
        res = plan->persist(out);
    return res;
}

command_result AI::unpersist(color_ostream & out)
{
    command_result res = CR_OK;
    if (res == CR_OK)
        res = plan->unpersist(out);
    return res;
}
