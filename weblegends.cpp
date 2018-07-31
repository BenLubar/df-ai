#include "ai.h"
#include "embark.h"
#include "event_manager.h"
#include "plan.h"
#include "population.h"
#include "stocks.h"

#include "thirdparty/weblegends/weblegends-plugin.h"

#include "df/world.h"

extern std::unique_ptr<AI> dwarfAI;
extern bool & enabled;

REQUIRE_GLOBAL(world);

enum AIPages {
    Status,             // Status
    //Report,           // Report - No longer used
    Report_Plan,        // Tasks
    Report_Population,  // Population
    Report_Stocks,      // Stocks
    Plan,               // Blueprints
    Version,            // Version
};

// https://stackoverflow.com/a/24315631/2664560
static void replace_all(std::string & str, const std::string & from, const std::string & to)
{
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos)
    {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
}

std::string html_escape(const std::string & str)
{
    std::string escaped(str);
    replace_all(escaped, "&", "&amp;");
    replace_all(escaped, "<", "&lt;");
    replace_all(escaped, ">", "&gt;");
    replace_all(escaped, "\n", "<br/>");
    return escaped;
}

std::string getAIPageURL(const int currentPage){
    switch (currentPage)
    {
        // case AIPages::Report:
        //     return "\"df-ai/report\"";
        case AIPages::Report_Plan: // Tasks
            return "\"df-ai/report/plan\"";
        case AIPages::Report_Population:
            return "\"df-ai/report/population\"";
        case AIPages::Report_Stocks:
            return "\"df-ai/report/stocks\"";
        case AIPages::Plan: // Blueprints
            return "\"df-ai/plan\"";
        case AIPages::Version:
            return "\"df-ai/version\"";
            break;
        default: // default to status page
            return "\"df-ai\"";
    }
}

int getAIPage(const std::string url){
    // if (url == "/report")
    //     return Report;
    if (url == "/report/plan")
        return AIPages::Report_Plan; // Tasks
    else if (url == "/report/population")
        return AIPages::Report_Population;
    else if (url == "/report/stocks")
        return AIPages::Report_Stocks;
    else if (url == "/plan")
        return AIPages::Plan; // Blueprints
    else if (url == "/version")
        return AIPages::Version;
    else // default to status page
        return AIPages::Status;
}

void create_nav_menu(weblegends_handler_v1 & handler, const std::string url)
{
    std::string title;
    int currentPage = 0;
    // Get current page
    currentPage = getAIPage(url);
    // Set the title based on current page
    switch ((AIPages)currentPage)
    {
        // case AIPages::Report:
        //     title = "DF-AI - Report";
        //     break;
        case AIPages::Report_Plan:
            title = "DF-AI - Tasks";
            break;
        case AIPages::Report_Population:
            title = "DF-AI - Population";
            break;
        case AIPages::Report_Stocks:
            title = "DF-AI - Stocks";
            break;
        case AIPages::Plan:
            title = "DF-AI - Blueprints";
            break;
        case AIPages::Version:
            title = "DF-AI - Version";
            break;
        default: // default to status page
            title = "DF-AI - Status";
    }
    // Build Nav Menu =========
    handler.cp437_out() << "<!DOCTYPE html><html lang=\"en\"><head><title>" << title << "</title><base href=\"../..\"/><link rel=\"stylesheet\" href=\"style.css\"/><link rel=\"stylesheet\" href=\"df-ai/aistyle.css\"/></head>";
    handler.cp437_out() << "<body><p>";
    // Weblegends Home
    handler.cp437_out() << "<a href=\"\">Home</a> - ";
    // Status
    handler.cp437_out() << "<a href=" << getAIPageURL(AIPages::Status);
    if ((AIPages)currentPage == AIPages::Status)
        handler.cp437_out() << " class=\"navSelected\"";
    handler.cp437_out() << ">Status</a> - ";
    // Report - old, split into sub-pages
    // handler.cp437_out() << "<a href=" << getAIPageURL(AIPages::Report);
    // if ((AIPages)currentPage == Report)
    //     handler.cp437_out() << " class=\"navSelected\"";
    // handler.cp437_out() << ">Report</a> - ";
    // Tasks
    handler.cp437_out() << "<a href=" << getAIPageURL(AIPages::Report_Plan);
    if ((AIPages)currentPage == AIPages::Report_Plan)
        handler.cp437_out() << " class=\"navSelected\"";
    handler.cp437_out() << ">Tasks</a> - ";
    // Population
    handler.cp437_out() << "<a href=" << getAIPageURL(AIPages::Report_Population);
    if ((AIPages)currentPage == AIPages::Report_Population)
        handler.cp437_out() << " class=\"navSelected\"";
    handler.cp437_out() << ">Population</a> - ";
    // Stocks
    handler.cp437_out() << "<a href=" << getAIPageURL(AIPages::Report_Stocks);
    if ((AIPages)currentPage == AIPages::Report_Stocks)
        handler.cp437_out() << " class=\"navSelected\"";
    handler.cp437_out() << ">Stocks</a> - ";
    // Blueprint
    handler.cp437_out() << "<a href=" << getAIPageURL(AIPages::Plan);
    if ((AIPages)currentPage == AIPages::Plan)
        handler.cp437_out() << " class=\"navSelected\"";
    handler.cp437_out() << ">Blueprint</a> - ";
    // Version
    handler.cp437_out() << "<a href=" << getAIPageURL(AIPages::Version);
    if ((AIPages)currentPage == AIPages::Version)
        handler.cp437_out() << " class=\"navSelected\"";
    handler.cp437_out() << ">Version</a> ";
    // close P from beginning of menu
    handler.cp437_out() << "</p>";
    return;
}


bool ai_weblegends_handler(weblegends_handler_v1 & handler, const std::string & url)
{
    // Requesting Sytlesheet
    if (url == "/aistyle.css")
    {
        handler.headers()["Content-Type"] = "text/css; charset=utf-8";
        handler.raw_out() << "table {\n"
            "\tbackground-color: #eee;\n"
            "\tcolor: #000;\n"
            "}\n"
            "\n"
            "td, th {\n"
            "\tpadding: 0.1em 0.5em;\n"
            "}\n"
            "\n"
            "td {\n"
            "\tbackground-color: #fff;\n"
            "}\n"
            "\n"
            "td.num {\n"
            "\ttext-align: right;\n"
            "}\n"
            "\n"
            "tbody th {\n"
            "\ttext-align: left;\n"
            "}\n"
            ".navSelected {\n"
            "\tcolor: inherit;\n"
            "\ttext-decoration: none;\n"
            "\tfont-weight: bold;\n"
            "}\n";
        return true;
    }

    // df-ai is not loaded or enabled
    if (!enabled || !dwarfAI)
    {
        handler.status_code() = 503;
        handler.status_description() = "Service Unavailable";
        handler.cp437_out() << "<!DOCTYPE html><html lang=\"en\"><head><title>df-ai - Service Unavailable</title></head>";
        handler.cp437_out() << "<body><p><i>AI is not active.</i></p></body></html>";
        return true;
    }

    // Report - old, split into sub-pages
    if (url == "/report")
    {
        handler.status_code() = 301;
        handler.status_description() = "Moved Permanently";
        handler.headers()["Location"] = "report/population";
        handler.cp437_out() << "<!DOCTYPE html><html lang=\"en\"><head><title>df-ai report</title><meta http-equiv=\"Refresh\" content=\"0; url=report/population\"/></head>";
        handler.cp437_out() << "<body><p><a href=\"report/population\">Report has been split into sub-pages.</a></p></body></html>";
        return true;
    }

    //===================
    // Create nav menu
    create_nav_menu(handler,url);
    
    // Status
    if (url == "")
    {
        handler.cp437_out() << "<p>" << html_escape(dwarfAI->status()) << "</p></body></html>";
        return true;
    }
#define REPORT(module) \
    if (events.has_exclusive<EmbarkExclusive>()) \
    { \
        handler.status_code() = 503; \
        handler.status_description() = "Service Unavailable"; \
        handler.headers()["Retry-After"] = "1"; \
        handler.cp437_out() << "<p><i>Report is not available during embark.</i></p>"; \
    } \
    else \
    { \
        dwarfAI->module.report(handler.cp437_out(), true); \
    }

    // Tasks Report
    if (url == "/report/plan")
    {
        handler.cp437_out() << "<h1 id=\"Plan_Tasks\">Tasks</h1>";
        REPORT(plan);
        handler.cp437_out() << "</body></html>";
        return true;
    }
    // Population Report
    if (url == "/report/population")
    {
        handler.cp437_out() << "<h1 id=\"Population\">Population</h1>";
        REPORT(pop);
        handler.cp437_out() << "</body></html>";
        return true;
    }
    // Stocks Report
    if (url == "/report/stocks")
    {
        handler.cp437_out() << "<h1 id=\"Stocks\">Stocks</h1>";
        REPORT(stocks);
        handler.cp437_out() << "</body></html>";
        return true;
    }
#undef REPORT
    // Blueprints
    if (url == "/plan")
    {
        dwarfAI->plan.weblegends_write_svg(handler.cp437_out());
        handler.cp437_out() << "</body></html>";
        return true;
    }
    // Version
    if (url == "/version")
    {
        std::ostringstream version;
        ai_version(version, true);
        handler.cp437_out() << "<pre style=\"white-space:pre-wrap\">" << version.str() << "</pre></body></html>";
        return true;
    }
    return false;
}

void Plan::weblegends_write_svg(std::ostream & out)
{
    bool first = true;
    for (auto level = room_by_z.rbegin(); level != room_by_z.rend(); level++)
    {
        if (first)
        {
            first = false;
        }
        else
        {
            out << "<hr/>";
        }
        out << "<svg xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"100%\" viewBox=\"0 0 " << (world->map.x_count - 1) << " " << (world->map.y_count - 1) << "\" id=\"level-" << level->first << "\">";
        for (room_status::status s = room_status::plan; s != room_status::_room_status_count; s = static_cast<room_status::status>(s + 1))
        {
            for (auto r : level->second)
            {
                if (r->status != s)
                {
                    continue;
                }
                if (r->min.z <= level->first && r->max.z >= level->first)
                {
                    // "<a xlink:href=\"df-ai/plan/room-" << r->id << "\">"
                    out << "<g><title>" << html_escape(AI::describe_room(r)) << "</title><path fill-rule=\"evenodd\" d=\"M" << r->min.x << " " << r->min.y << "h" << (r->max.x - r->min.x + 1) << "v" << (r->max.y - r->min.y + 1) << "h" << (r->min.x - r->max.x - 1) << "v" << (r->min.y - r->max.y - 1);
                    std::set<df::coord> holes;
                    for (auto f : r->layout)
                    {
                        if (r->min.z + f->pos.z != level->first)
                        {
                            continue;
                        }
                        if ((r->outdoor ? (f->pos.z != 0 && f->dig == tile_dig_designation::Channel) : (f->dig == tile_dig_designation::No || f->construction == construction_type::Wall)) && holes.insert(f->pos).second)
                        {
                            out << "M" << (r->min.x + f->pos.x) << " " << (r->min.y + f->pos.y) << "h1v1h-1v-1";
                        }
                    }
                    out << "\" fill=\"";
                    switch (s)
                    {
                        case room_status::plan:
                            if (r->queue_dig)
                            {
                                out << "#69c";
                            }
                            else
                            {
                                out << "#aaa";
                            }
                            break;
                        case room_status::dig:
                            out << "#db3";
                            break;
                        case room_status::dug:
                        case room_status::finished:
                            out << "#333";
                            break;
                        case room_status::_room_status_count:
                            break;
                    }
                    out << "\"></path></g>";
                }
            }
        }
        for (room_status::status s = room_status::plan; s != room_status::_room_status_count; s = static_cast<room_status::status>(s + 1))
        {
            for (auto r : level->second)
            {
                if (r->status != s)
                {
                    continue;
                }
                for (auto f : r->layout)
                {
                    if (r->min.z + f->pos.z != level->first || (r->outdoor ? (f->pos.z != 0 && f->dig == tile_dig_designation::Channel) : (f->dig == tile_dig_designation::No || f->construction == construction_type::Wall)))
                    {
                        continue;
                    }
                    out << "<g><title>" << html_escape(AI::describe_furniture(f)) << "</title><rect x=\"" << (r->min.x + f->pos.x) << ".1\" y=\"" << (r->min.y + f->pos.y) << ".1\" width=\"0.8\" height=\"0.8\" fill=\"";
                    if (f->bld_id != -1)
                    {
                        out << "#963";
                    }
                    else if (f->construction != construction_type::NONE || f->dig != tile_dig_designation::Default)
                    {
                        out << "#444";
                    }
                    else
                    {
                        out << "#bbb";
                    }
                    out << "\"></rect></g>";
                }
            }
        }
        out << "</svg>";
    }
}
