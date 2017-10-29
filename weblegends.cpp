#include "ai.h"
#include "plan.h"

#include "thirdparty/weblegends/weblegends-plugin.h"

#include "df/world.h"

extern AI *dwarfAI;
extern bool & enabled;

REQUIRE_GLOBAL(world);

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

bool ai_weblegends_handler(std::ostringstream & out, const std::string & url)
{
    if (!enabled || !dwarfAI)
    {
        out << "<!DOCTYPE html><html><head><title>df-ai</title></head>";
        out << "<body><p>AI is not active.</p></body></html>";
        return true;
    }
    if (url == "")
    {
        out << "<!DOCTYPE html><html><head><title>df-ai status</title></head>";
        out << "<body><p><b>Status</b> - <a href=\"df-ai/report\">Report</a> - <a href=\"df-ai/plan\">Plan</a> - <a href=\"df-ai/version\">Version</a></p></p>";
        out << "<pre style=\"white-space:pre-wrap\">" << html_escape(dwarfAI->status()) << "</pre></body></html>";
        return true;
    }
    if (url == "/report")
    {
        out << "<!DOCTYPE html><html><head><title>df-ai report</title><base href=\"..\"/></head>";
        out << "<body><p><a href=\"df-ai\">Status</a> - <b>Report</b> - <a href=\"df-ai/plan\">Plan</a> - <a href=\"df-ai/version\">Version</a></p>";
        out << "<pre style=\"white-space:pre-wrap\">" << dwarfAI->report(true) << "</pre></body></html>";
        return true;
    }
    if (url == "/version")
    {
        std::ostringstream version;
        ai_version(version, true);
        out << "<!DOCTYPE html><html><head><title>df-ai version</title><base href=\"..\"/></head>";
        out << "<body><p><a href=\"df-ai\">Status</a> - <a href=\"df-ai/report\">Report</a> - <a href=\"df-ai/plan\">Plan</a> - <b>Version</b></p>";
        out << "<pre style=\"white-space:pre-wrap\">" << version.str() << "</pre></body></html>";
        return true;
    }
    if (url == "/plan")
    {
        out << "<!DOCTYPE html><html><head><title>df-ai plan</title><base href=\"..\"/></head>";
        out << "<body><p><a href=\"df-ai\">Status</a> - <a href=\"df-ai/report\">Report</a> - <b>Plan</b> - <a href=\"df-ai/version\">Version</a></p>";
        dwarfAI->plan->weblegends_write_svg(out);
        out << "</body></html>";
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
                    out << "<g><title>" << html_escape(describe_room(r)) << "</title><path fill-rule=\"evenodd\" d=\"M" << r->min.x << " " << r->min.y << "h" << (r->max.x - r->min.x + 1) << "v" << (r->max.y - r->min.y + 1) << "h" << (r->min.x - r->max.x - 1) << "v" << (r->min.y - r->max.y - 1);
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
                    out << "<g><title>" << html_escape(describe_furniture(f)) << "</title><rect x=\"" << (r->min.x + f->pos.x) << ".1\" y=\"" << (r->min.y + f->pos.y) << ".1\" width=\"0.8\" height=\"0.8\" fill=\"";
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
