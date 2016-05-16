// a dwarf fortress autonomous artificial intelligence (more or less)

#include "ai.h"
#include "event_manager.h"

#include <fstream>

#include "modules/Gui.h"
#include "modules/Screen.h"

#include "df/viewscreen_dwarfmodest.h"
#include "df/viewscreen_optionst.h"
#include "df/viewscreen_titlest.h"

DFHACK_PLUGIN("df-ai");
DFHACK_PLUGIN_IS_ENABLED(enabled);

REQUIRE_GLOBAL(pause_state);
REQUIRE_GLOBAL(ui);

// Protected by CoreSuspender
AI *dwarfAI = nullptr;
bool full_reset_requested = false;

command_result ai_command(color_ostream & out, std::vector<std::string> & args);

// Check whether we are enabled and make sure the AI data exists iff we are.
bool check_enabled(color_ostream & out)
{
    if (enabled)
    {
        if (full_reset_requested && strict_virtual_cast<df::viewscreen_titlest>(Gui::getCurViewscreen(true)))
        {
            delete dwarfAI;
            dwarfAI = nullptr;
            full_reset_requested = false;
        }
        if (!dwarfAI)
        {
            dwarfAI = new AI();

            events.onupdate_register_once("df-ai start", [](color_ostream & out) -> bool
                    {
                        df::viewscreen_dwarfmodest *view = strict_virtual_cast<df::viewscreen_dwarfmodest>(Gui::getCurViewscreen(true));
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
        out << "AI: removed onupdate" << std::endl;
        delete dwarfAI;
        dwarfAI = nullptr;
    }
    return false;
}

// Mandatory init function. If you have some global state, create it here.
DFhackCExport command_result plugin_init(color_ostream & out, std::vector<PluginCommand> & commands)
{
    config.load(out);
    config.save(out);

    commands.push_back(PluginCommand(
        "ai",
        "Dwarf Fortress + Artificial Intelligence",
        ai_command,
        false,
        "ai\n"
        "  Shows the status of the AI. Use enable df-ai to enable the AI.\n"
        "ai report\n"
        "  Shows a more detailed status report.\n"
        "ai enable events\n"
        "  Write events in JSON format to df-ai-events.json\n"
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

command_result ai_command(color_ostream & out, std::vector<std::string> & args)
{
    CoreSuspender suspend;

    if (!check_enabled(out))
    {
        out << "The AI is currently not running. Use enable df-ai to enable the AI." << std::endl;
        return CR_OK;
    }

    if (args.empty())
    {
        AI::write_df(out, dwarfAI->status(), "\n", "\n", DF2CONSOLE);
        return CR_OK;
    }

    if (args.size() == 1 && args[0] == "report")
    {
        std::string str = dwarfAI->report();
        if (str.empty())
        {
            out << "cannot write report during embark" << std::endl;
            return CR_OK;
        }

        std::ofstream f("df-ai-report.log", std::ofstream::trunc);
        AI::write_df(f, str, "\n", "\n", DF2UTF);
        out << "report written to df-ai-report.log" << std::endl;
        return CR_OK;
    }

    if (args.size() == 2 && (args[0] == "enable" || args[0] == "disable"))
    {
        bool enable = args[0] == "enable";

        if (args[1] == "events")
        {
            if (enable == dwarfAI->eventsJson.is_open())
            {
                out << "df-ai-events.json is already " << (enable ? "enabled" : "disabled") << std::endl;
                return CR_OK;
            }

            if (enable)
            {
                dwarfAI->eventsJson.open("df-ai-events.json", std::ofstream::out | std::ofstream::app);
            }
            else
            {
                dwarfAI->eventsJson.close();
            }
            return CR_OK;
        }
    }

    return CR_WRONG_USAGE;
}

DFhackCExport command_result plugin_onstatechange(color_ostream & out, state_change_event event)
{
    if (!check_enabled(out))
        return CR_OK;

    if (event == SC_VIEWSCREEN_CHANGED && strict_virtual_cast<df::viewscreen_optionst>(Gui::getCurViewscreen(true)))
    {
        command_result res = dwarfAI->persist(out);
        if (res != CR_OK)
            return res;
    }

    events.onstatechange(out, event);
    return CR_OK;
}

DFhackCExport command_result plugin_onupdate(color_ostream & out)
{
    if (!check_enabled(out))
        return CR_OK;

    if (ui->main.autosave_request)
    {
        command_result res = dwarfAI->persist(out);
        if (res != CR_OK)
            return res;
    }

    events.onupdate(out);
    return CR_OK;
}

// vim: et:sw=4:ts=4
