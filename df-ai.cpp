// a dwarf fortress autonomous artificial intelligence (more or less)

#include "ai.h"
#include "blueprint.h"
#include "event_manager.h"
#include "hooks.h"

#include <fstream>

#include "git-describe.h"
#define NO_DFHACK_VERSION_MACROS
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
std::unique_ptr<AI> dwarfAI{ nullptr };
bool full_reset_requested = false;

command_result ai_command(color_ostream & out, std::vector<std::string> & args);

// Check whether we are enabled and make sure the AI data exists iff we are.
bool check_enabled(color_ostream & out)
{
    if (enabled)
    {
        if (full_reset_requested && strict_virtual_cast<df::viewscreen_titlest>(Gui::getCurViewscreen(true)))
        {
            dwarfAI = nullptr;
            full_reset_requested = false;
        }
        if (!dwarfAI)
        {
            dwarfAI = std::make_unique<AI>();

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
                            view->feed_key(interface_key::D_PAUSE);
                        }
                        return true;
                    }
                    dwarfAI->onupdate_unregister(out);
                    if (config.random_embark)
                    {
                        AI::abandon(out);
                    }
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
        "ai abandon\n"
        "  Abandons the current fortress.\n"
        "ai validate\n"
        "  Verifies that df-ai-blueprints is set up correctly.\n"
    ));

    add_weblegends_handler("df-ai", &ai_weblegends_handler, "Artificial Intelligence");

    return CR_OK;
}

// This is called right before the plugin library is removed from memory.
DFhackCExport command_result plugin_shutdown(color_ostream & out)
{
    if (lockstep_hooked)
    {
        unloading_plugin = true;
        return CR_FAILURE;
    }

    CoreSuspender suspend;

    remove_weblegends_handler("df-ai");

    enabled = false;
    check_enabled(out); // delete the AI if it was enabled.
    return CR_OK;
}

DFhackCExport command_result plugin_enable(color_ostream & out, bool enable)
{
    if (!enable && lockstep_hooked)
    {
        disabling_plugin = true;
        out << COLOR_YELLOW << "Disabling lockstep mode. df-ai will deactivate when lockstep mode has exited.";
        out << COLOR_RESET << std::endl;
        return CR_FAILURE;
    }

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
#if defined(WIN32)
    constexpr const char *os = "Windows";
#elif defined(_LINUX)
    constexpr const char *os = "Linux";
#elif defined(_DARWIN)
    constexpr const char *os = "Mac";
#else
#error Unknown operating system.
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
    out << "Dwarf Fortress " << DFHack::Version::df_version() << br;
    out << "  " << os << " " << bits << "-bit" << br;
    out << "df-ai " << DF_AI_GIT_DESCRIPTION;
    if (*DFHACK_BUILD_ID)
        out << " (Build ID: " << DFHACK_BUILD_ID << ")";
    out << br;
    commit("code", "BenLubar/df-ai", DF_AI_GIT_COMMIT);
    out << "DFHack " << DFHack::Version::git_description();
    if (*DFHack::Version::dfhack_build_id())
        out << " (Build ID: " << DFHack::Version::dfhack_build_id() << ")";
    out << br;
    commit("library", "DFHack/dfhack", DFHack::Version::git_commit());
    commit("structures", "DFHack/df-structures", DFHack::Version::git_xml_commit());
    out << std::flush;
}

command_result ai_command(color_ostream & out, std::vector<std::string> & args)
{
    CoreSuspender suspend;

    if (args.size() == 1 && args[0] == "version")
    {
        ai_version(out);
        return CR_OK;
    }

    if (args.size() == 1 && args[0] == "validate")
    {
        blueprints_t blueprints(out);
        return blueprints.is_valid ? CR_OK : CR_FAILURE;
    }

    if (args.size() == 2 && args[0] == "validate")
    {
        blueprints_t blueprints(out);
        std::ofstream f(args[1]);
        blueprints.write_rooms(f);
        return blueprints.is_valid ? CR_OK : CR_FAILURE;
    }

    if (!check_enabled(out))
    {
        out << "The AI is currently not running. Use enable df-ai to enable the AI." << std::endl;
        return CR_OK;
    }

    if (args.empty())
    {
        // Run log.cpp - AI::status()
        AI::write_df(out, dwarfAI->status(), "\n", "\n", [](const std::string & s) -> std::string { return DF2CONSOLE(s); });
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
    else if (args.size() == 1 && args[0] == "abandon")
    {
        if (!Core::getInstance().isWorldLoaded())
        {
            out << "cannot abandon while no world is loaded" << std::endl;
            return CR_OK;
        }

        AI::abandon(out);
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

    if (event == SC_BEGIN_UNLOAD)
    {
        unloading_plugin = true;
    }

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
