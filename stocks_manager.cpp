#include "ai.h"
#include "stocks.h"
#include "event_manager.h"

#include "modules/Gui.h"
#include "modules/Materials.h"

#include "df/itemdef_ammost.h"
#include "df/itemdef_toolst.h"
#include "df/itemdef_trapcompst.h"
#include "df/itemdef_weaponst.h"
#include "df/manager_order.h"
#include "df/manager_order_template.h"
#include "df/tool_uses.h"
#include "df/viewscreen_createquotast.h"
#include "df/viewscreen_dwarfmodest.h"
#include "df/viewscreen_joblistst.h"
#include "df/viewscreen_jobmanagementst.h"
#include "df/world.h"

REQUIRE_GLOBAL(world);

// return the number of current manager orders that share the same material (leather, cloth)
// ignore inorganics, ignore order
int32_t Stocks::count_manager_orders_matcat(const df::job_material_category & matcat, df::job_type order)
{
    int32_t cnt = 0;
    for (auto mo : world->manager_orders)
    {
        if (mo->material_category.whole == matcat.whole && mo->job_type != order)
        {
            cnt += mo->amount_left;
        }
    }
    events.each_exclusive<ManagerOrderExclusive>([&cnt, matcat, order](const ManagerOrderExclusive *excl) -> bool
    {
        if (excl->tmpl.material_category.whole == matcat.whole && excl->tmpl.job_type != order)
        {
            cnt += excl->amount;
        }

        return false;
    });

    return cnt;
}

template<typename T>
static bool template_equals(const T *a, const df::manager_order_template *b)
{
    if (a->job_type != b->job_type)
        return false;
    if (a->reaction_name != b->reaction_name)
        return false;
    if (a->item_type != b->item_type)
        return false;
    if (a->item_subtype != b->item_subtype)
        return false;
    if (a->mat_type != b->mat_type)
        return false;
    if (a->mat_index != b->mat_index)
        return false;
    if (a->item_category.whole != b->item_category.whole)
        return false;
    if (a->hist_figure_id != b->hist_figure_id)
        return false;
    if (a->material_category.whole != b->material_category.whole)
        return false;
    return true;
}

int32_t Stocks::count_manager_orders(color_ostream &, const df::manager_order_template & tmpl)
{
    int32_t amount = 0;

    for (auto mo : world->manager_orders)
    {
        if (template_equals(mo, &tmpl))
        {
            amount += mo->amount_left;
        }
    }
    events.each_exclusive<ManagerOrderExclusive>([&amount, tmpl](const ManagerOrderExclusive *excl) -> bool
    {
        if (template_equals(&excl->tmpl, &tmpl))
        {
            amount += excl->amount;
        }

        return false;
    });

    return amount;
}

ManagerOrderExclusive::ManagerOrderExclusive(AI *ai, const df::manager_order_template & tmpl, int32_t amount)
    : ExclusiveCallback("add_manager_order: " + AI::describe_job(&tmpl)),
    ai(ai),
    tmpl(tmpl),
    amount(amount),
    search_word()
{
    search_word = AI::describe_job(&tmpl);
    size_t pos = search_word.find(' ');
    if (pos != std::string::npos)
    {
        if (search_word.substr(0, pos) == "Smelt" && search_word.substr(search_word.length() - 4) == " Ore")
        {
            size_t pos2 = search_word.rfind(' ');
            pos = search_word.rfind(' ', pos2);
            search_word = search_word.substr(pos, pos2 - pos);
        }
        else if ((search_word.substr(0, pos) == "Construct" || search_word.substr(0, pos) == "Make" || search_word.substr(0, pos) == "Prepare" || search_word.substr(0, pos) == "Forge") && isalpha(search_word.at(search_word.length() - 1)))
        {
            pos = search_word.rfind(' ');
            search_word = search_word.substr(pos);
        }
        else
        {
            search_word = search_word.substr(0, pos);
        }
    }
}

void ManagerOrderExclusive::Run(color_ostream & out)
{
    ExpectScreen<df::viewscreen_dwarfmodest>();
    Key(interface_key::D_JOBLIST);
    ExpectScreen<df::viewscreen_joblistst>();
    Key(interface_key::UNITJOB_MANAGER);

    {
        ExpectScreen<df::viewscreen_jobmanagementst>();
        ExpectedScreen<df::viewscreen_jobmanagementst> view(this);

        bool first = true;
        bool multiple = false;
        int32_t old_order = -1;
        for (auto it = world->manager_orders.begin(); it != world->manager_orders.end(); it++)
        {
            if (template_equals(*it, &tmpl))
            {
                if (first)
                {
                    first = false;
                }
                else if ((*it)->amount_left == (*it)->amount_total)
                {
                    old_order = int32_t(it - world->manager_orders.begin());
                    amount += (*it)->amount_left;
                    multiple = true;
                    break;
                }
            }
        }

        if (multiple)
        {
            MoveToItem(&view->sel_idx, old_order);

            Key(interface_key::MANAGER_REMOVE);
        }
    }

    auto quantity = stl_sprintf("%d", std::min(amount, 9999));

    Key(interface_key::MANAGER_NEW_ORDER);

    {
        ExpectScreen<df::viewscreen_createquotast>();
        ExpectedScreen<df::viewscreen_createquotast> view(this);

        int32_t idx = -1;
        df::manager_order_template *target = nullptr;
        auto find_target = [&]() -> bool
        {
            for (auto it = view->orders.begin(); it != view->orders.end(); it++)
            {
                if (template_equals<df::manager_order_template>(*it, &tmpl))
                {
                    idx = it - view->orders.begin();
                    target = *it;
                    return true;
                }
            }
            return false;
        };

        if (!find_target())
        {
            target = df::allocate<df::manager_order_template>();
            *target = tmpl;
            idx = int32_t(view->orders.size());
            view->orders.push_back(target);
            view->all_orders.push_back(target);
        }

        EnterString(&view->str_filter, search_word);

        while (!find_target() && view->str_filter[0])
        {
            Key(interface_key::STRING_A000);
        }

        if (!find_target())
        {
            ai->debug(out, "[CHEAT] Failed to get a manager order for " + AI::describe_job(&tmpl) + "; forcing it.");
            *view->orders.at(0) = tmpl;
        }
        else
        {
            MoveToItem(&view->sel_idx, idx, interface_key::STANDARDSCROLL_PAGEDOWN, interface_key::STANDARDSCROLL_UP);
        }

        Key(interface_key::SELECT);

        EnterString(&view->str_quantity, quantity);
    }

    Key(interface_key::SELECT);

    ai->debug(out, "add_manager_order(" + quantity + ") " + AI::describe_job(&tmpl));

    ExpectScreen<df::viewscreen_jobmanagementst>();
    Key(interface_key::LEAVESCREEN);
    ExpectScreen<df::viewscreen_joblistst>();
    Key(interface_key::LEAVESCREEN);
    ExpectScreen<df::viewscreen_dwarfmodest>();
}

void Stocks::add_manager_order(color_ostream & out, const df::manager_order_template & tmpl, int32_t amount)
{
    std::ofstream discard;
    add_manager_order(out, tmpl, amount, discard);
}

void Stocks::add_manager_order(color_ostream & out, const df::manager_order_template & tmpl, int32_t amount, std::ostream & reason)
{
    if (amount <= 0)
    {
        return;
    }

    int32_t already_queued = count_manager_orders(out, tmpl);
    amount -= already_queued;
    if (amount <= 0)
    {
        reason << "already have manager order: " << AI::describe_job(&tmpl) << " (" << already_queued << " remaining)";
        return;
    }

    reason << "queued manager order (" << amount << "): " << AI::describe_job(&tmpl);
    events.queue_exclusive(new ManagerOrderExclusive(ai, tmpl, amount));
}
