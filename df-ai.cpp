// a dwarf fortress autonomous artificial intelligence (more or less)

#include "ai.h"
#include "event_manager.h"
#include "hooks.h"

#include <fstream>

#include "DFHackVersion.h"
#include "df-ai-git-describe.h"
#include "thirdparty/weblegends/weblegends-plugin.h"

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
bool ai_weblegends_handler(std::ostringstream & out, const std::string & url);

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
        Hook_Shutdown();
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
        "ai version\n"
        "  Shows information that uniquely identifies this build of df-ai.\n"
        "ai report\n"
        "  Writes a more detailed status report to df-ai-report.log\n"
        "ai enable events\n"
        "  Write events in JSON format to df-ai-events.json\n"
        "ai disable events\n"
        "  Stop writing events to df-ai-events.json\n"
        "ai enable lockstep\n"
        "  Runs Dwarf Fortress as fast as possible, locking animations to the frame rate instead of real time. May cause crashes; use at your own risk.\n"
        "ai disable lockstep\n"
        "  Undoes \"ai enable lockstep\".\n"
        "ai enable camera\n"
        "  Enables AI control of the camera.\n"
        "ai disable camera\n"
        "  Undoes \"ai enable camera\".\n"
    ));

    add_weblegends_handler("df-ai", &ai_weblegends_handler, "Artificial Intelligence");

    return CR_OK;
}

// This is called right before the plugin library is removed from memory.
DFhackCExport command_result plugin_shutdown(color_ostream & out)
{
    CoreSuspender suspend;

    remove_weblegends_handler("df-ai");

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

void ai_version(std::ostream & out, bool html)
{
#ifdef DFHACK64
    constexpr int bits = 64;
#else
    constexpr int bits = 32;
#endif
#ifdef WIN32
    constexpr const char *os = "Windows";
#elif _LINUX
    constexpr const char *os = "Linux";
#elif _DARWIN
    constexpr const char *os = "Mac";
#else
    constexpr const char *os = "UNKNOWN";
#endif
    const char *br = html ? "<br/>" : "\n";
    auto commit = [&out, br, html](const std::string & name, const std::string & repo, const std::string & commit)
    {
        out << "  " << name << " ";
        if (html)
        {
            out << "<a href=\"https://github.com/" << repo << "/commit/" << commit << "\">" << commit << "</a>";
        }
        else
        {
            out << commit;
        }
        out << br;
    };
    out << "Dwarf Fortress " << DF_VERSION << br;
    out << "  " << os << " " << bits << "-bit" << br;
    out << "df-ai " << DF_AI_GIT_DESCRIPTION << br;
    commit("code", "BenLubar/df-ai", DF_AI_GIT_COMMIT);
    out << "DFHack " << DFHACK_GIT_DESCRIPTION << std::endl;
    commit("library", "DFHack/dfhack", DFHACK_GIT_COMMIT);
    commit("structures", "DFHack/df-structures", DFHACK_GIT_XML_COMMIT);
}

command_result ai_command(color_ostream & out, std::vector<std::string> & args)
{
    CoreSuspender suspend;

    if (args.size() == 1 && args[0] == "version")
    {
        ai_version(out);
        return CR_OK;
    }

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
        AI::write_df(f, str);
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
#define MODE(name) \
        else if (args[1] == #name) \
        { \
            if (enable == config.name) \
            { \
                out << #name " mode is already " << (enable ? "enabled" : "disabled") << std::endl; \
                return CR_OK; \
            } \
\
            config.set(out, config.name, enable); \
            return CR_OK; \
        }
        MODE(lockstep)
        MODE(camera)
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

    Hook_Update();

    if (ui->main.autosave_request)
    {
        command_result res = dwarfAI->persist(out);
        if (res != CR_OK)
            return res;
    }

    events.onupdate(out);
    return CR_OK;
}
