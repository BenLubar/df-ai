#include "ai.h"
#include "plan_setup.h"

std::string viewscreen_ai_plan_setupst::getFocusString()
{
    return "df-ai/plan/setup";
}

void viewscreen_ai_plan_setupst::render()
{
    dfhack_viewscreen::render();

    Screen::clear();
    Screen::drawBorder("  df-ai Planning  ");

    auto bounds = Screen::getScreenRect();
    bounds.first.x += 2;
    bounds.first.y += 2;
    bounds.second.x -= 2;
    bounds.second.y -= 2;
    Screen::Painter painter(bounds);

    int offset = painter.height() - int(setup.log.size());
    if (offset > 0)
    {
        offset = 0;
    }

    Screen::Pen pen(0, COLOR_GREY, 0);
    Screen::Pen quiet_pen(0, COLOR_DARKGREY, 0);

    for (auto & line : setup.log)
    {
        if (offset < 0)
        {
            offset++;
            continue;
        }

        painter.string(line.first, line.second ? pen : quiet_pen);
        painter.newline();
    }
}

#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#define STATIC_FIELDS_GROUP
#include "../DataStaticsFields.cpp"

using df::identity_traits;

#define CUR_STRUCT viewscreen_ai_plan_setupst
static const struct_field_info viewscreen_ai_plan_setupst_fields[] = {
    { FLD_END }
};
#undef CUR_STRUCT
virtual_identity viewscreen_ai_plan_setupst::_identity(sizeof(viewscreen_ai_plan_setupst), nullptr, "viewscreen_ai_plan_setupst", nullptr, &dfhack_viewscreen::_identity, viewscreen_ai_plan_setupst_fields, true);
