#pragma once

#include <string>

namespace df
{
    struct caravan_state;
    struct creature_raw;
    struct entity_buy_prices;
    struct entity_sell_prices;
    struct historical_entity;
    struct item;
    struct viewscreen_tradegoodsst;
}

class AI;

class Trade
{
    AI *ai;

public:
    Trade(AI *ai);
    ~Trade();

    bool can_trade();
    bool can_move_goods();
    void read_trader_reply(std::string & reply, std::string & mood);
    int32_t item_value_for_caravan(df::item *item, df::caravan_state *caravan, df::historical_entity *entity, df::creature_raw *creature, int32_t qty = 0);
    int32_t item_price_for_caravan(df::item *item, df::caravan_state *caravan, df::historical_entity *entity, df::creature_raw *creature, df::entity_buy_prices *pricetable, int32_t qty = 0);
    //int32_t item_price_for_caravan(df::item *item, df::caravan_state *caravan, df::historical_entity *entity, df::creature_raw *creature, df::entity_sell_prices *pricetable, int32_t qty = 0);
    int32_t item_or_container_price_for_caravan(df::item *item, df::caravan_state *caravan, df::historical_entity *entity, df::creature_raw *creature, df::entity_buy_prices *pricetable, int32_t qty = 0);
    //int32_t item_or_container_price_for_caravan(df::item *item, df::caravan_state *caravan, df::historical_entity *entity, df::creature_raw *creature, df::entity_sell_prices *pricetable, int32_t qty = 0);
    int32_t depot_calculate_profit(df::viewscreen_tradegoodsst *ws);
};
