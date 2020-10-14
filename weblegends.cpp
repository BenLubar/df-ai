#include "ai.h"
#include "embark.h"
#include "event_manager.h"
#include "plan.h"
#include "population.h"
#include "stocks.h"
#include "plan_setup.h"

#include "thirdparty/weblegends/weblegends-plugin.h"

#include "df/world.h"

extern std::unique_ptr<AI> dwarfAI;
extern bool & enabled;

REQUIRE_GLOBAL(world);

struct ai_web_page
{
    std::string path;
    std::string title;
    bool unavailable_during_embark;
    bool is_error_page;
    std::function<void(weblegends_layout_v1 &)> render;
};

namespace
{
    extern const ai_web_page not_active_page;
    extern const ai_web_page embarking_page;
    extern const std::vector<ai_web_page> web_pages;
}

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
    return DF2UTF(escaped);
}

static void process_query_arg(weblegends_handler_v1 & handler, const std::string & key, const std::string & value)
{
    if (key == "refresh")
    {
        if (value.empty())
            return;
        if (value.at(0) <= '1' || value.at(0) >= '9')
            return;
        for (size_t i = 1; i < value.length(); i++)
        {
            if (value.at(i) <= '0' || value.at(i) >= '9')
                return;
        }

        handler.headers()["Refresh"] = value;
    }
}

static void process_query(weblegends_handler_v1 & handler, std::string & query)
{
    while (!query.empty())
    {
        auto and_pos = query.find('&');

        // && or ?& in URL
        if (and_pos == 0)
        {
            query.erase(0, 1);
            continue;
        }

        std::string key = query.substr(0, and_pos);
        std::string value;
        if (and_pos == std::string::npos)
        {
            query.clear();
        }
        else
        {
            query.erase(0, and_pos + 1);
        }

        auto eq_pos = key.find('=');
        if (eq_pos < and_pos)
        {
            value = key.substr(eq_pos + 1);
            key.erase(eq_pos);
        }

        process_query_arg(handler, key, value);
    }
}

static const ai_web_page *select_current_page(weblegends_layout_v1 & layout, const std::string & url)
{
    size_t num_slashes = std::count(url.begin(), url.end(), '/');
    std::ostringstream base_path;
    if (num_slashes)
    {
        base_path << "..";
        num_slashes--;
    }
    for (size_t i = 0; i < num_slashes; i++)
    {
        base_path << "/..";
    }

    layout.set_base_path(base_path.str());
    const ai_web_page *cur_page = nullptr;
    for (auto & page : web_pages)
    {
        if (url == page.path)
            cur_page = &page;

        layout.add_sidebar_link("df-ai" + page.path, page.title);
    }

    if (!cur_page)
    {
        return nullptr;
    }

    if (!enabled || !dwarfAI)
    {
        return &not_active_page;
    }

    if (cur_page->unavailable_during_embark && dwarfAI->is_embarking())
    {
        return &embarking_page;
    }

    return cur_page;
}

bool ai_weblegends_handler(weblegends_handler_v1 & handler, const std::string & url_with_query)
{
    auto pos = url_with_query.find('?');
    if (pos != std::string::npos)
    {
        auto query = url_with_query.substr(pos + 1);
        process_query(handler, query);
    }
    auto url = url_with_query.substr(0, pos);

    auto layout = weblegends_allocate_layout();
    auto page = select_current_page(*layout, url);
    if (!page)
    {
        return false;
    }

    if (page->is_error_page)
    {
        handler.status_code() = 503;
        handler.status_description() = "Service Unavailable";
        handler.headers()["Retry-After"] = "5";
        handler.headers()["Refresh"] = "5";
    }

    layout->set_title("DF-AI - " + page->title);
    layout->add_header_link("df-ai" + url, page->title, true);
    page->render(*layout);
    layout->write_to(handler);

    return true;
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
                        if ((r->outdoor ? f->dig == tile_dig_designation::Channel : (f->dig == tile_dig_designation::No || f->construction == construction_type::Wall)) && holes.insert(f->pos).second)
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
                    if (r->min.z + f->pos.z != level->first)
                    {
                        continue;
                    }
                    if (f->pos.z == 0 && f->type == layout_type::none && f->construction == construction_type::Floor)
                    {
                        continue;
                    }
                    if (r->outdoor && f->dig == tile_dig_designation::Channel)
                    {
                        continue;
                    }
                    if (!r->outdoor && (f->dig == tile_dig_designation::No || f->construction == construction_type::Wall))
                    {
                        continue;
                    }
                    out << "<g><title>" << html_escape(AI::describe_furniture(f)) << "</title><rect x=\"" << (r->min.x + f->pos.x) << ".1\" y=\"" << (r->min.y + f->pos.y) << ".1\" width=\"0.8\" height=\"0.8\" fill=\"";
                    if (f->bld_id != -1)
                    {
                        out << "#963";
                    }
                    else if (s != room_status::plan && (f->construction != construction_type::NONE || f->dig != tile_dig_designation::Default))
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

static void render_not_active_page(weblegends_layout_v1 & layout)
{
    layout.content << "<p>Enter <code>enable df-ai</code> in the DFHack console to start DF-AI</p>";
    layout.content << "<p>Enter <code>help ai</code> for a list of console commands.</p>";
}

static void render_embarking_page(weblegends_layout_v1 & layout)
{
    layout.content << "<p><i>Report is not available during embark.</i></p>";
}

static void render_status_page(weblegends_layout_v1 & layout)
{
    layout.content << "<p>" << html_escape(dwarfAI->status()) << "</p>";
}

static void render_plan_report(weblegends_layout_v1 & layout)
{
    dwarfAI->plan.report(layout.content, true);
}

static void render_population_report(weblegends_layout_v1 & layout)
{
#define ANCHOR_LINK(name, anchor) layout.add_header_link("df-ai/report/population#" anchor, name)
    ANCHOR_LINK("Citizens", "Population_Citizens");
    ANCHOR_LINK("Military", "Population_Military");
    ANCHOR_LINK("Pets", "Population_Pets");
    ANCHOR_LINK("Visitors", "Population_Visitors");
    ANCHOR_LINK("Residents", "Population_Residents");
    ANCHOR_LINK("Crimes", "Population_Crimes");
    ANCHOR_LINK("Health", "Population_Health");
    ANCHOR_LINK("Deaths", "Population_Deaths");
    ANCHOR_LINK("Active Jobs", "Population_Jobs_Active");
    ANCHOR_LINK("Waiting Jobs", "Population_Jobs_Waiting");
#undef ANCHOR_LINK

    dwarfAI->pop.report(layout.content, true);
}

static void render_stocks_report(weblegends_layout_v1 & layout)
{
    dwarfAI->stocks.report(layout.content, true);
}

static void render_event_report(weblegends_layout_v1 & layout)
{
    events.report(layout.content, true);
}

static void render_blueprint_page(weblegends_layout_v1 & layout)
{
    dwarfAI->plan.weblegends_write_svg(layout.content);
}

static void render_version_page(weblegends_layout_v1 & layout)
{
    layout.content << "<pre style=\"white-space:pre-wrap\">";
    ai_version(layout.content, true);
    layout.content << "</pre>";
}

namespace
{
    const ai_web_page not_active_page = { "", "Not Active", false, true, &render_not_active_page };
    const ai_web_page embarking_page = { "", "Unavailable During Embark", false, true, &render_embarking_page };

    const std::vector<ai_web_page> web_pages =
    {
        { "", "Status", false, false, &render_status_page },
        { "/report/plan", "Tasks", true, false, &render_plan_report },
        { "/report/population", "Population", true, false, &render_population_report },
        { "/report/stocks", "Stocks", true, false, &render_stocks_report },
        { "/report/events", "Events", true, false, &render_event_report },
        { "/plan", "Blueprints", true, false, &render_blueprint_page },
        { "/version", "Version", false, false, &render_version_page },
    };
}
