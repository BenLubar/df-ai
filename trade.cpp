// Converted from https://github.com/mifki/dfremote/blob/37ee700a40ca77e1b1327b5a50581173538f1dc5/lua/depot.lua
// Used with permission.

#include "ai.h"
#include "trade.h"
#include "plan.h"

#include "modules/Items.h"
#include "modules/Job.h"

#include "df/building.h"
#include "df/caravan_state.h"
#include "df/caste_raw.h"
#include "df/creature_raw.h"
#include "df/entity_buy_prices.h"
#include "df/entity_buy_requests.h"
#include "df/entity_raw.h"
#include "df/general_ref_contains_itemst.h"
#include "df/general_ref_contains_unitst.h"
#include "df/general_ref_unit_workerst.h"
#include "df/graphic.h"
#include "df/historical_entity.h"
#include "df/item.h"
#include "df/itemdef_armorst.h"
#include "df/itemdef_glovesst.h"
#include "df/itemdef_helmst.h"
#include "df/itemdef_pantsst.h"
#include "df/itemdef_shieldst.h"
#include "df/itemdef_shoesst.h"
#include "df/itemdef_weaponst.h"
#include "df/job.h"
#include "df/sphere_type.h"
#include "df/ui.h"
#include "df/unit.h"
#include "df/viewscreen_tradegoodsst.h"

REQUIRE_GLOBAL(gps);
REQUIRE_GLOBAL(ui);

Trade::Trade(AI *ai) :
    ai(ai)
{
}

Trade::~Trade()
{
}

bool Trade::can_trade()
{
    auto room = ai->plan->find_room(room_type::tradedepot);
    auto bld = room ? room->dfbuilding() : nullptr;

    for (auto & caravan : ui->caravans)
    {
        if (caravan->trade_state == df::caravan_state::AtDepot && caravan->time_remaining > 0)
        {
            for (auto & job : bld->jobs)
            {
                if (job->job_type == job_type::TradeAtDepot)
                {
                    auto worker_ref = virtual_cast<df::general_ref_unit_workerst>(Job::getGeneralRef(job, general_ref_type::UNIT_WORKER));
                    auto worker = worker_ref ? df::unit::find(worker_ref->unit_id) : nullptr;

                    return worker && worker->pos.z == bld->z &&
                        worker->pos.x >= bld->x1 && worker->pos.x <= bld->x2 &&
                        worker->pos.y >= bld->y1 && worker->pos.y <= bld->y2;
                }
            }
        }
    }

    return false;
}

bool Trade::can_move_goods()
{
    for (auto & caravan : ui->caravans)
    {
        if (caravan->trade_state == df::caravan_state::Approaching || (caravan->trade_state == df::caravan_state::AtDepot && caravan->time_remaining > 0))
        {
            return true;
        }
    }

    return false;
}

/*
--luacheck: in=
function depot_movegoods_get()
    local ws = dfhack.gui.getCurViewscreen()
    if ws._type ~= df.viewscreen_dwarfmodest then
        error('wrong screen '..tostring(ws._type))
    end

    if df.global.ui.main.mode ~= 17 or df.global.world.selected_building == nil then
        error('no selected building')
    end

    local bld = df.global.world.selected_building
    if bld:getType() ~= df.building_type.TradeDepot then
        error('not a depot')
    end

    gui.simulateInput(ws, K'BUILDJOB_DEPOT_BRING')

    local movegoodsws = dfhack.gui.getCurViewscreen() --as:df.viewscreen_layer_assigntradest
    if movegoodsws._type ~= df.viewscreen_layer_assigntradest then
        error('can not switch to move goods screen')
    end

    gui.simulateInput(movegoodsws, K'ASSIGNTRADE_SORT')

    local ret = {}
    
    for i,info in ipairs(movegoodsws.info) do
        local title = itemname(info.item, 0, true)
        table.insert(ret, { title, info.distance, info.status })
    end

    return ret
end
*/

/*
--luacheck: in=
function depot_movegoods_get2()
    local ws = dfhack.gui.getCurViewscreen()
    if ws._type ~= df.viewscreen_dwarfmodest then
        error('wrong screen '..tostring(ws._type))
    end

    if df.global.ui.main.mode ~= 17 or df.global.world.selected_building == nil then
        error('no selected building')
    end

    local bld = df.global.world.selected_building
    if bld:getType() ~= df.building_type.TradeDepot then
        error('not a depot')
    end

    gui.simulateInput(ws, K'BUILDJOB_DEPOT_BRING')

    local movegoodsws = dfhack.gui.getCurViewscreen() --as:df.viewscreen_layer_assigntradest
    if movegoodsws._type ~= df.viewscreen_layer_assigntradest then
        error('can not switch to move goods screen')
    end

    gui.simulateInput(movegoodsws, K'ASSIGNTRADE_SORT')

    local ret = {}
    
    for i,info in ipairs(movegoodsws.info) do
        local title = itemname(info.item, 0, true)
        table.insert(ret, { title, info.item.id, info.status, info.status }) -- .distance
    end

    return ret
end
*/

/*
--luacheck: in=number,number
function depot_movegoods_set(idx, status)
    local ws = dfhack.gui.getCurViewscreen() --as:df.viewscreen_layer_assigntradest

    if ws._type ~= df.viewscreen_layer_assigntradest then
        return
    end
        
    if type(status) == 'boolean' then
        status = status and 1 or 0
    end

    ws.info[idx].status = status
end
*/

void Trade::read_trader_reply(std::string & reply, std::string & mood)
{
    reply.clear();
    mood.clear();
    bool start = false;

    for (int32_t j = 1; j < gps->dimy - 2; j++)
    {
        bool empty = true;

        for (int32_t i = 2; i < gps->dimx - 1; i++)
        {
            char ch = gps->screen[(i * gps->dimy + j) * 4];

            if (!start)
            {
                start = ch == ':';
            }
            else
            {
                if (ch != 0)
                {
                    if (ch != ' ')
                    {
                        if (empty && !reply.empty() && reply.back() != ' ')
                        {
                            reply.push_back(' ');
                        }
                        empty = false;
                    }

                    if (!empty && (ch != ' ' || reply.back() != ' '))
                    {
                        reply.push_back(ch);
                    }
                }
            }
        }

        if (empty && !reply.empty())
        {
            for (j = j + 1; j < gps->dimy - 2; j++)
            {
                char ch = gps->screen[(2 * gps->dimy + j) * 4];

                if (ch != 0)
                {
                    if (ch == 219)
                    {
                        break;
                    }

                    for (int32_t i = 2; i < gps->dimx - 1; i++)
                    {
                        ch = gps->screen[(i*gps->dimy + j) * 4];

                        if (ch != ' ' || (!mood.empty() && mood.back() != ' '))
                        {
                            mood.push_back(ch);
                        }
                    }

                    break;
                }
            }

            break;
        }
    }
}

// exact copy of Item::getValue() but also accepts caravan_state, race and quantity
int32_t Trade::item_value_for_caravan(df::item *item, df::caravan_state *caravan, df::historical_entity *entity, df::creature_raw *creature, int32_t qty)
{
    auto item_type = item->getType();
    auto item_subtype = item->getSubtype();
    auto mat_type = item->getMaterial();
    auto mat_subtype = item->getMaterialIndex();

    // Get base value for item type, subtype, and material
    int32_t value;
    if (item_type == item_type::CHEESE)
    {
        // TODO: seems to be wrong in dfhack's getItemBaseValue() ?
        value = 10;
    }
    else
    {
        value = Items::getItemBaseValue(item_type, item_subtype, mat_type, mat_subtype);
    }

    // Apply entity value modifications
    if (entity && creature && entity->entity_raw->sphere_alignment[sphere_type::WAR] != 256)
    {
        // weapons
        if (item_type == item_type::WEAPON)
        {
            auto def = df::itemdef_weaponst::find(item_subtype);
            if (creature->adultsize >= def->minimum_size)
            {
                value = value * 2;
            }
        }

        // armor gloves shoes helms pants
        // TODO: why 7 ?
        if (creature->adultsize >= 7)
        {
            if (item_type == item_type::ARMOR)
            {
                auto def = df::itemdef_armorst::find(item_subtype);
                if (def->armorlevel > 0 || def->flags.is_set(armor_flags::METAL_ARMOR_LEVELS))
                {
                    value = value * 2;
                }
            }
            else if (item_type == item_type::GLOVES)
            {
                auto def = df::itemdef_glovesst::find(item_subtype);
                if (def->armorlevel > 0 || def->flags.is_set(gloves_flags::METAL_ARMOR_LEVELS))
                {
                    value = value * 2;
                }
            }
            else if (item_type == item_type::SHOES)
            {
                auto def = df::itemdef_shoesst::find(item_subtype);
                if (def->armorlevel > 0 || def->flags.is_set(shoes_flags::METAL_ARMOR_LEVELS))
                {
                    value = value * 2;
                }
            }
            else if (item_type == item_type::HELM)
            {
                auto def = df::itemdef_helmst::find(item_subtype);
                if (def->armorlevel > 0 || def->flags.is_set(helm_flags::METAL_ARMOR_LEVELS))
                {
                    value = value * 2;
                }
            }
            else if (item_type == item_type::PANTS)
            {
                auto def = df::itemdef_pantsst::find(item_subtype);
                if (def->armorlevel > 0 || def->flags.is_set(pants_flags::METAL_ARMOR_LEVELS))
                {
                    value = value * 2;
                }
            }
        }

        // shields
        if (item_type == item_type::SHIELD)
        {
            auto def = df::itemdef_shieldst::find(item_subtype);
            if (def->armorlevel > 0)
            {
                value = value * 2;
            }
        }

        // ammo
        if (item_type == item_type::AMMO)
        {
            value = value * 2;
        }

        // quiver
        if (item_type == item_type::QUIVER)
        {
            value = value * 2;
        }
    }

    // Improve value based on quality
    auto quality = item->getQuality();
    value = value * (quality + 1);
    if (quality == 5)
    {
        value = value * 2;
    }

    // Add improvement values
    auto impValue = item->getThreadDyeValue(caravan) + item->getImprovementsValue(caravan);
    if (item_type == item_type::AMMO) // Ammo improvements are worth less
    {
        impValue = impValue / 30;
    }
    value = value + impValue;

    // Degrade value due to wear
    auto wear = item->getWear();
    if (wear == 1)
    {
        value = value * 3 / 4;
    }
    else if (wear == 2)
    {
        value = value / 2;
    }
    else if (wear == 3)
    {
        value = value / 4;
    }

    // Ignore value bonuses from magic, since that never actually happens

    // Artifacts have 10x value
    if (item->flags.bits.artifact_mood)
    {
        value = value * 10;
    }

    // Boost value from stack size or the supplied quantity
    if (qty > 0)
    {
        value = value * qty;
    }
    else
    {
        value = value * item->getStackSize();
    }
    // ...but not for coins
    if (item_type == item_type::COIN)
    {
        value = value / 500;
        if (value < 1)
        {
            value = 1;
        }
    }

    // Handle vermin swarms
    if (item_type == item_type::VERMIN || item_type == item_type::PET)
    {
        int32_t divisor = 1;
        auto creature = df::creature_raw::find(mat_type);
        if (creature && mat_subtype < creature->caste.size())
        {
            divisor = creature->caste.at(mat_subtype)->misc.petvalue_divisor;
        }
        if (divisor > 1)
        {
            value = value / divisor;
        }
    }

    return value;
}

template<typename Prices>
static int32_t item_price_for_caravan(Trade *trade, df::item *item, df::caravan_state *caravan, df::historical_entity *entity, df::creature_raw *creature, Prices *pricetable, int32_t qty)
{
    auto value = trade->item_value_for_caravan(item, caravan, entity, creature, qty);

    if (!pricetable)
    {
        return value;
    }

    MaterialInfo mat(item);

    auto & reqs = pricetable->items;
    for (size_t i = 0; i < reqs->item_type.size(); i++)
    {
        if (item->getType() == reqs->item_type.at(i) && (reqs->item_subtype.at(i) == -1 || item->getSubtype() == reqs->item_subtype.at(i)))
        {
            if (reqs->mat_cats.at(i).whole == 0 || mat.matches(reqs->mat_cats.at(i)))
            {
                return value * pricetable->price.at(i) / 128;
            }
        }
    }

    return value;
}

int32_t Trade::item_price_for_caravan(df::item *item, df::caravan_state *caravan, df::historical_entity *entity, df::creature_raw *creature, df::entity_buy_prices *pricetable, int32_t qty)
{
    return ::item_price_for_caravan(this, item, caravan, entity, creature, pricetable, qty);
}

template<typename Prices>
static int32_t item_or_container_price_for_caravan(Trade *trade, df::item *item, df::caravan_state *caravan, df::historical_entity *entity, df::creature_raw *creature, Prices *pricetable, int32_t qty)
{
    auto value = item_price_for_caravan(trade, item, caravan, entity, creature, pricetable, qty);

    for (auto & ref : item->general_refs)
    {
        if (auto contains_item = virtual_cast<df::general_ref_contains_itemst>(ref))
        {
            auto item2 = df::item::find(contains_item->item_id);
            value += item_price_for_caravan(trade, item2, caravan, entity, creature, pricetable, 0);
        }
        else if (auto contains_unit = virtual_cast<df::general_ref_contains_unitst>(ref))
        {
            auto unit2 = df::unit::find(contains_unit->unit_id);
            auto creature2 = df::creature_raw::find(unit2->race);
            auto caste = creature2->caste.at(unit2->caste);
            value += caste->misc.petvalue;
        }
    }

    return value;
}

int32_t Trade::item_or_container_price_for_caravan(df::item *item, df::caravan_state *caravan, df::historical_entity *entity, df::creature_raw *creature, df::entity_buy_prices *pricetable, int32_t qty)
{
    return ::item_or_container_price_for_caravan(this, item, caravan, entity, creature, pricetable, qty);
}

int32_t Trade::depot_calculate_profit(df::viewscreen_tradegoodsst *ws)
{
    auto creature = df::creature_raw::find(ws->entity->race);

    int32_t trader_profit = 0;
    for (size_t i = 0; i < ws->trader_selected.size(); i++)
    {
        if (ws->trader_selected.at(i))
        {
            trader_profit -= item_or_container_price_for_caravan(ws->trader_items.at(i), ws->caravan, ws->entity, creature, ws->caravan->buy_prices, ws->trader_count.at(i)); // sell_prices
        }
    }
    for (size_t i = 0; i < ws->broker_selected.size(); i++)
    {
        if (ws->broker_selected.at(i))
        {
            trader_profit += item_or_container_price_for_caravan(ws->broker_items.at(i), ws->caravan, ws->entity, creature, ws->caravan->buy_prices, ws->broker_count.at(i));
        }
    }

    return trader_profit;
}

/*
local counteroffer

--luacheck: in=
function depot_trade_overview()
    local ws = dfhack.gui.getCurViewscreen()
    if ws._type ~= df.viewscreen_tradegoodsst then
        if df.global.ui.main.mode ~= df.ui_sidebar_mode.QueryBuilding or df.global.world.selected_building == nil then
            error('no selected building')
        end

        local bld = df.global.world.selected_building
        if bld:getType() ~= df.building_type.TradeDepot then
            error('not a depot')
        end
    
        gui.simulateInput(ws, K'BUILDJOB_DEPOT_TRADE')

        ws = dfhack.gui.getCurViewscreen()
        ws:logic()
        ws = dfhack.gui.getCurViewscreen()
        ws:logic() --probably not required
        ws:render() --to populate item lists

        if ws._type ~= df.viewscreen_tradegoodsst then
            error('can not switch to trade screen')
        end        
    end

    local tradews = ws --as:df.viewscreen_tradegoodsst

    --todo: include whether can seize/offer
    local trader_profit = depot_calculate_profit()
    local reply, mood = read_trader_reply()
    
    local can_seize = (tradews.caravan.entity ~= df.global.ui.civ_id)
    local can_trade = not tradews.is_unloading and not tradews.caravan.flags.offended

    local have_appraisal = false
    for i,v in ipairs(tradews.broker.status.current_soul.skills) do
        if v.id == df.job_skill.APPRAISAL then
            have_appraisal = true
            break
        end
    end    

    local flags = packbits(can_trade, can_seize, have_appraisal)

    --local counteroffer = mp.NIL
    if #tradews.counteroffer > 0 then
        counteroffer = {}
        for i,item in ipairs(tradews.counteroffer) do
            local title = itemname(item, 0, true)
            table.insert(counteroffer, { title })
        end

        gui.simulateInput(tradews, K'SELECT')
    elseif not istrue(tradews.has_offer) then
        counteroffer = mp.NIL
    end

    local ret = { dfhack.df2utf(tradews.merchant_name), dfhack.df2utf(tradews.merchant_entity), reply, mood, trader_profit, flags, counteroffer }

    return ret
end
*/

/*
function is_contained(item)
    for i,ref in ipairs(item.general_refs) do
        if ref:getType() == df.general_ref_type.CONTAINED_IN_ITEM then
            return true
        end
    end

    return false
end
*/

/*
--luacheck: in=bool
function depot_trade_get_items(their)
    local ws = dfhack.gui.getCurViewscreen() --as:df.viewscreen_tradegoodsst
    if ws._type ~= df.viewscreen_tradegoodsst then
        error('wrong screen '..tostring(ws._type))
    end

    their = istrue(their)

    local items = their and ws.trader_items or ws.broker_items --as:df.item_actual[]
    local sel = their and ws.trader_selected or ws.broker_selected
    local counts = their and ws.trader_count or ws.broker_count
    local prices = ws.caravan.buy_prices --their and nil or ws.caravan.buy_prices --ws.caravan.sell_prices
    local creature = df.global.world.raws.creatures.all[ws.entity.race]

    local ret = {}

    for i,item in ipairs(items) do
        local title = itemname(item, 0, true)
        local value = item_or_container_price_for_caravan(item, ws.caravan, ws.entity, creature, nil, prices)

        local inner = is_contained(item)
        local entity_stolen = dfhack.items.getGeneralRef(item, df.general_ref_type.ENTITY_STOLEN) --as:df.general_ref_entity_stolenst
        local stolen = entity_stolen and (df.historical_entity.find(entity_stolen.entity_id) ~= nil)
        local flags = packbits(inner, stolen, item.flags.foreign)

        --todo: use getStackSize() ?
        table.insert(ret, { title, value, item.weight, sel[i], flags, item.stack_size, counts[i] })
    end

    return ret
end
*/

/*
--luacheck: in=bool
function depot_trade_get_items2(their)
    local ws = dfhack.gui.getCurViewscreen() --as:df.viewscreen_tradegoodsst
    if ws._type ~= df.viewscreen_tradegoodsst then
        error('wrong screen '..tostring(ws._type))
    end

    their = istrue(their)

    local items = their and ws.trader_items or ws.broker_items --as:df.item_actual[]
    local sel = their and ws.trader_selected or ws.broker_selected
    local counts = their and ws.trader_count or ws.broker_count
    local prices = ws.caravan.buy_prices --their and nil or ws.caravan.buy_prices --ws.caravan.sell_prices
    local creature = df.global.world.raws.creatures.all[ws.entity.race]

    local ret = {}

    for i,item in ipairs(items) do
        local title = itemname(item, 0, true)
        local value = item_or_container_price_for_caravan(item, ws.caravan, ws.entity, creature, nil, prices)

        local inner = is_contained(item)
        local entity_stolen = dfhack.items.getGeneralRef(item, df.general_ref_type.ENTITY_STOLEN) --as:df.general_ref_entity_stolenst
        local stolen = entity_stolen and (df.historical_entity.find(entity_stolen.entity_id) ~= nil)
        local flags = packbits(inner, stolen, item.flags.foreign)

        --todo: use getStackSize() ?
        table.insert(ret, { title, item.id, value, item.weight, sel[i], flags, item.stack_size, counts[i] })
    end

    return ret
end
*/

/*
--todo: support passing many items at once
--luacheck: in=bool,number,bool,number
function depot_trade_set(their, idx, trade, qty)
    local ws = dfhack.gui.getCurViewscreen() --as:df.viewscreen_tradegoodsst
    if ws._type ~= df.viewscreen_tradegoodsst then
        return
    end

    their = istrue(their)

    local items = their and ws.trader_items or ws.broker_items --as:df.item_actual[]
    local sel = their and ws.trader_selected or ws.broker_selected    
    local counts = their and ws.trader_count or ws.broker_count

    if istrue(trade) then
        if is_contained(items[idx]) then
            -- Go up find the container and deselect it
            for i=idx-1,0,-1 do
                if not is_contained(items[i]) then
                    sel[i] = 0
                    break
                end
            end
        else
            -- If selecting a container, deselect all individual items in it
            for i=idx+1,#items-1 do
                if is_contained(items[i]) then
                    sel[i] = 0
                    counts[i] = 0
                else
                    break
                end
            end            
        end

        sel[idx] = 1
        counts[idx] = items[idx].stack_size > qty and qty or 0
    else
        sel[idx] = 0
        counts[idx] = 0
    end

    return depot_calculate_profit()
end
*/

/*
--luacheck: in=
function depot_trade_dotrade()
    local ws = dfhack.gui.getCurViewscreen()
    if ws._type ~= df.viewscreen_tradegoodsst then
        return
    end

    gui.simulateInput(ws, K'TRADE_TRADE')

    --not sure these are needed
    ws:logic()
    ws:render()

    --todo: return the new depot_trade_overview info here?
    return true
end
*/

/*
--luacheck: in=
function depot_trade_seize()
    local ws = dfhack.gui.getCurViewscreen()
    if ws._type ~= df.viewscreen_tradegoodsst then
        return
    end

    gui.simulateInput(ws, K'TRADE_SEIZE')

    --not sure these are needed
    ws:logic()
    ws:render()

    --todo: return the new depot_trade_overview info here?
    return true    
end
*/

/*
--luacheck: in=
function depot_trade_offer()
    local ws = dfhack.gui.getCurViewscreen()
    if ws._type ~= df.viewscreen_tradegoodsst then
        return
    end

    gui.simulateInput(ws, K'TRADE_OFFER')

    --not sure these are needed
    ws:logic()
    ws:render()

    --todo: return the new depot_trade_overview info here?
    return true    
end
*/

/*
--luacheck: in=
function depot_access()
    local ws = dfhack.gui.getCurViewscreen()
    if ws._type ~= df.viewscreen_dwarfmodest then
        error('wrong screen '..tostring(ws._type))
    end

    reset_main()

    gui.simulateInput(ws, K'D_DEPOT')

    return df.global.ui.main.mode == df.ui_sidebar_mode.DepotAccess
end
*/
