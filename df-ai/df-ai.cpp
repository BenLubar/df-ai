#include "ai.h"

DFHACK_PLUGIN("df-ai");
DFHACK_PLUGIN_IS_ENABLED(enabled);

// Protected by CoreSuspender
AI *ai = nullptr;

command_result status_command(color_ostream & out, std::vector<std::string> & args);

// Check whether we are enabled and make sure the AI data exists iff we are.
bool check_enabled(color_ostream & out)
{
    if (enabled)
    {
        if (!ai)
        {
            ai = new AI(out);
        }
        return true;
    }
    if (ai)
    {
        delete ai;
        ai = nullptr;
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
    if (args.size() != 0)
    {
        return CR_WRONG_USAGE;
    }

    CoreSuspender suspend;

    if (!check_enabled(out))
    {
        out << "The AI is currently not running. Use enable df-ai to enable the AI.\n";
        return CR_OK;
    }

    return ai->status(out);
}

DFhackCExport command_result plugin_onstatechange(color_ostream & out, state_change_event event)
{
    if (!check_enabled(out))
        return CR_OK;

    return ai->statechange(out, event);
}

DFhackCExport command_result plugin_onupdate(color_ostream &out)
{
    if (!check_enabled(out))
        return CR_OK;

    return ai->update(out);
}

// vim: et:sw=4:ts=4
