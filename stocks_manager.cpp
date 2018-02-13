#include "ai.h"
#include "stocks.h"

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
#include "df/world.h"

REQUIRE_GLOBAL(world);

void Stocks::init_manager_subtype()
{
    manager_subtype.clear();

    MaterialInfo candy;
    if (candy.findInorganic("RAW_ADAMANTINE"))
        manager_subtype[stock_item::raw_adamantine] = static_cast<int16_t>(candy.index);

    if (!world->raws.itemdefs.tools_by_type[tool_uses::HEAVY_OBJECT_HAULING].empty())
        manager_subtype[stock_item::wheelbarrow] = world->raws.itemdefs.tools_by_type[tool_uses::HEAVY_OBJECT_HAULING][0]->subtype;
    if (!world->raws.itemdefs.tools_by_type[tool_uses::TRACK_CART].empty())
        manager_subtype[stock_item::minecart] = world->raws.itemdefs.tools_by_type[tool_uses::TRACK_CART][0]->subtype;
    if (!world->raws.itemdefs.tools_by_type[tool_uses::NEST_BOX].empty())
        manager_subtype[stock_item::nest_box] = world->raws.itemdefs.tools_by_type[tool_uses::NEST_BOX][0]->subtype;
    if (!world->raws.itemdefs.tools_by_type[tool_uses::HIVE].empty())
        manager_subtype[stock_item::hive] = world->raws.itemdefs.tools_by_type[tool_uses::HIVE][0]->subtype;
    if (!world->raws.itemdefs.tools_by_type[tool_uses::LIQUID_CONTAINER].empty())
        manager_subtype[stock_item::jug] = world->raws.itemdefs.tools_by_type[tool_uses::LIQUID_CONTAINER][0]->subtype;
    if (!world->raws.itemdefs.tools_by_type[tool_uses::STAND_AND_WORK_ABOVE].empty())
        manager_subtype[stock_item::stepladder] = world->raws.itemdefs.tools_by_type[tool_uses::STAND_AND_WORK_ABOVE][0]->subtype;
    if (!world->raws.itemdefs.tools_by_type[tool_uses::BOOKCASE].empty())
        manager_subtype[stock_item::bookcase] = world->raws.itemdefs.tools_by_type[tool_uses::BOOKCASE][0]->subtype;
    for (auto def : world->raws.itemdefs.tools_by_type[tool_uses::CONTAIN_WRITING])
    {
        if (def->flags.is_set(tool_flags::INCOMPLETE_ITEM))
        {
            manager_subtype[stock_item::quire] = def->subtype;
            break;
        }
    }
    if (!world->raws.itemdefs.tools_by_type[tool_uses::PROTECT_FOLDED_SHEETS].empty())
        manager_subtype[stock_item::book_binding] = world->raws.itemdefs.tools_by_type[tool_uses::PROTECT_FOLDED_SHEETS][0]->subtype;
    if (!world->raws.itemdefs.tools_by_type[tool_uses::FOOD_STORAGE].empty())
        manager_subtype[stock_item::rock_pot] = world->raws.itemdefs.tools_by_type[tool_uses::FOOD_STORAGE][0]->subtype;

    for (auto def : world->raws.itemdefs.tools)
    {
        if (def->id == "ITEM_TOOL_HONEYCOMB")
        {
            manager_subtype[stock_item::honeycomb] = def->subtype;
            break;
        }
    }
    for (auto def : world->raws.itemdefs.weapons)
    {
        if (def->id == "ITEM_WEAPON_CROSSBOW")
        {
            manager_subtype[stock_item::crossbow] = def->subtype;
            break;
        }
    }
    for (auto def : world->raws.itemdefs.ammo)
    {
        if (def->id == "ITEM_AMMO_BOLTS")
        {
            manager_subtype[stock_item::bone_bolts] = def->subtype;
            break;
        }
    }
    for (auto def : world->raws.itemdefs.trapcomps)
    {
        if (def->id == "ITEM_TRAPCOMP_ENORMOUSCORKSCREW")
        {
            manager_subtype[stock_item::giant_corkscrew] = def->subtype;
            break;
        }
    }
}

// return the number of current manager orders that share the same material (leather, cloth)
// ignore inorganics, ignore order
int32_t Stocks::count_manager_orders_matcat(const df::job_material_category & matcat, df::job_type order)
{
    int32_t cnt = 0;
    for (auto mo : world->manager_orders)
    {
        if (mo->material_category.whole == matcat.whole && mo->job_type != order)
        {
            cnt += mo->amount_total;
        }
    }
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

    for (auto it = world->manager_orders.begin(); it != world->manager_orders.end(); it++)
    {
        if (template_equals<df::manager_order>(*it, &tmpl))
        {
            amount += (*it)->amount_left;
        }
    }

    return amount;
}

void Stocks::add_manager_order(color_ostream & out, const df::manager_order_template & tmpl, int32_t amount)
{
    amount -= count_manager_orders(out, tmpl);
    if (amount <= 0)
    {
        return;
    }

    if (!ai->is_dwarfmode_viewscreen())
    {
        ai->debug(out, stl_sprintf("cannot add manager order for %s - not on main screen", tmpl.job_type == job_type::CustomReaction ? tmpl.reaction_name.c_str() : ENUM_ATTR(job_type, caption, tmpl.job_type)));
        return;
    }
    AI::feed_key(interface_key::D_JOBLIST);
    AI::feed_key(interface_key::UNITJOB_MANAGER);
    AI::feed_key(interface_key::MANAGER_NEW_ORDER);
    auto view = strict_virtual_cast<df::viewscreen_createquotast>(Gui::getCurViewscreen(true));
    if (!view)
    {
        ai->debug(out, stl_sprintf("[ERROR] viewscreen when queueing manager job is %s, not viewscreen_createquotast", virtual_identity::get(Gui::getCurViewscreen(true))->getName()));
        return;
    }
    int32_t idx = -1;
    df::manager_order_template *target = nullptr;
    for (auto it = view->orders.begin(); it != view->orders.end(); it++)
    {
        if (template_equals<df::manager_order_template>(*it, &tmpl))
        {
            idx = it - view->orders.begin();
            target = *it;
            break;
        }
    }
    if (!target)
    {
        target = df::allocate<df::manager_order_template>();
        *target = tmpl;
        idx = int32_t(view->orders.size());
        view->orders.push_back(target);
        view->all_orders.push_back(target);
    }

    while (view->sel_idx < idx)
    {
        AI::feed_key(interface_key::STANDARDSCROLL_PAGEDOWN);
    }
    while (view->sel_idx > idx)
    {
        AI::feed_key(interface_key::STANDARDSCROLL_UP);
    }
    AI::feed_key(interface_key::SELECT);

    if (amount >= 10000)
    {
        amount = 9999;
    }
    AI::feed_char('0' + char((amount / 1000) % 10));
    AI::feed_char('0' + char((amount / 100) % 10));
    AI::feed_char('0' + char((amount / 10) % 10));
    AI::feed_char('0' + char(amount % 10));
    AI::feed_key(interface_key::SELECT);
    AI::feed_key(interface_key::LEAVESCREEN);
    AI::feed_key(interface_key::LEAVESCREEN);
    ai->debug(out, stl_sprintf("add_manager_order(%d) %s", amount, AI::describe_job(world->manager_orders.back()).c_str()));
}
