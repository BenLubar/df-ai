// a dwarf fortress autonomous artificial intelligence (more or less)

#include "ai.h"
#include "event_manager.h"

#include "modules/Gui.h"
#include "modules/Screen.h"

#include "df/viewscreen_titlest.h"
#include "df/viewscreen_dwarfmodest.h"

DFHACK_PLUGIN("df-ai");
DFHACK_PLUGIN_IS_ENABLED(enabled);

REQUIRE_GLOBAL(pause_state);

// Protected by CoreSuspender
AI *dwarfAI = nullptr;
bool full_reset_requested = false;

command_result status_command(color_ostream & out, std::vector<std::string> & args);

// Check whether we are enabled and make sure the AI data exists iff we are.
bool check_enabled(color_ostream & out)
{
    if (enabled)
    {
        if (full_reset_requested)
        {
            delete dwarfAI;
            dwarfAI = nullptr;
            full_reset_requested = false;
        }
        if (!dwarfAI)
        {
            df::viewscreen_titlest *view = strict_virtual_cast<df::viewscreen_titlest>(Gui::getCurViewscreen());
            if (view && !AI_RANDOM_EMBARK)
            {
                AI::feed_key(view, interface_key::SELECT);
                AI::feed_key(view, interface_key::SELECT);
            }

            dwarfAI = new AI();

            events.onupdate_register_once("df-ai start", [](color_ostream & out) -> bool
                    {
                        df::viewscreen_dwarfmodest *view = virtual_cast<df::viewscreen_dwarfmodest>(Gui::getCurViewscreen());
                        if (view)
                        {
                            command_result res = dwarfAI->onupdate_register(out);
                            if (res == CR_OK)
                                res = dwarfAI->startup(out);
                            if (res == CR_OK)
                            {
                                if (*pause_state)
                                {
                                    AI::feed_key(view, interface_key::D_PAUSE);
                                }
                                return true;
                            }
                            dwarfAI->onupdate_unregister(out);
                            dwarfAI->abandon(out);
                            full_reset_requested = true;
                            return true;
                        }
                        return false;
                    });
        }
        return true;
    }
    if (dwarfAI)
    {
        dwarfAI->onupdate_unregister(out);
        out << "removed onupdate\n";
        delete dwarfAI;
        dwarfAI = nullptr;
    }
    return false;
}

// Mandatory init function. If you have some global state, create it here.
DFhackCExport command_result plugin_init(color_ostream & out, std::vector<PluginCommand> & commands)
{
    commands.push_back(PluginCommand(
        "ai",
        "Dwarf Fortress + Artificial Intelligence",
        status_command,
        false,
        "  Shows the status of the AI. Use enable df-ai to enable the AI.\n"
    ));
    return CR_OK;
}

// This is called right before the plugin library is removed from memory.
DFhackCExport command_result plugin_shutdown(color_ostream & out)
{
    CoreSuspender suspend;
    enabled = false;
    check_enabled(out); // delete the AI if it was enabled.
    return CR_OK;
}

DFhackCExport command_result plugin_enable(color_ostream & out, bool enable)
{
    enabled = enable;
    check_enabled(out);
    return CR_OK;
}

command_result status_command(color_ostream & out, std::vector<std::string> & args)
{
    if (!args.empty())
    {
        return CR_WRONG_USAGE;
    }

    CoreSuspender suspend;

    if (!check_enabled(out))
    {
        out << "The AI is currently not running. Use enable df-ai to enable the AI.\n";
        return CR_OK;
    }

    AI::write_df(out, dwarfAI->status(), "\n", "\n", DF2CONSOLE);
    return CR_OK;
}

DFhackCExport command_result plugin_onstatechange(color_ostream & out, state_change_event event)
{
    if (!check_enabled(out))
        return CR_OK;

    events.onstatechange(out, event);
    return CR_OK;
}

DFhackCExport command_result plugin_onupdate(color_ostream & out)
{
    if (!check_enabled(out))
        return CR_OK;

    events.onupdate(out);
    return CR_OK;
}

// vim: et:sw=4:ts=4
