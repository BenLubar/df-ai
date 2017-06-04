// Converted from https://github.com/mifki/dfremote/blob/414809907133def8b4ad36f61ba0f2c991726b9f/lua/depot.lua
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
#include "df/entity_sell_category.h"
#include "df/entity_sell_prices.h"
#include "df/entity_sell_requests.h"
#include "df/general_ref_contains_itemst.h"
#include "df/general_ref_contains_unitst.h"
#include "df/general_ref_unit_workerst.h"
#include "df/graphic.h"
#include "df/historical_entity.h"
#include "df/item.h"
#include "df/item_armorst.h"
#include "df/item_glovesst.h"
#include "df/item_helmst.h"
#include "df/item_pantsst.h"
#include "df/item_shieldst.h"
#include "df/item_shoesst.h"
#include "df/item_weaponst.h"
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
                uint8_t ch = gps->screen[(2 * gps->dimy + j) * 4];

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

static bool match_mat_vec(const df::material_vec_ref & mat_vec, size_t idx, int16_t mat_type, int32_t mat_index)
{
    return mat_type == mat_vec.mat_type.at(idx) && mat_index == mat_vec.mat_index.at(idx);
}
static bool match_inorganic_mat(const std::vector<int32_t> & mat_indices, size_t idx, int16_t mat_type, int32_t mat_index)
{
    return mat_type == 0 && mat_index == mat_indices.at(idx);
}
static bool match_item_type_subtype(const std::vector<int16_t> & subtypes, size_t idx, int16_t item_subtype)
{
    return item_subtype == subtypes.at(idx);
}
static bool match_race_caste(const std::vector<int32_t> & races, const std::vector<int16_t> & castes, size_t idx, int16_t race, int32_t caste)
{
    return int32_t(race) == races.at(idx) && caste == int32_t(castes.at(idx));
}

const static struct item_type_to_sell_category_t
{
    std::map<df::item_type, std::vector<df::entity_sell_category>> map;

    const std::vector<df::entity_sell_category> & operator[](df::item_type item_type) const
    {
        auto it = map.find(item_type);
        if (it == map.end())
        {
            const static std::vector<df::entity_sell_category> empty;
            return empty;
        }
        return it->second;
    }

    item_type_to_sell_category_t()
    {
        map[item_type::BAR].push_back(entity_sell_category::MetalBars);
        map[item_type::BAR].push_back(entity_sell_category::Miscellaneous);
        map[item_type::SMALLGEM].push_back(entity_sell_category::SmallCutGems);
        map[item_type::BLOCKS].push_back(entity_sell_category::StoneBlocks);
        map[item_type::ROUGH].push_back(entity_sell_category::Glass);
        map[item_type::BOULDER].push_back(entity_sell_category::Stone);
        map[item_type::BOULDER].push_back(entity_sell_category::Clay);
        map[item_type::WOOD].push_back(entity_sell_category::Wood);
        map[item_type::CHAIN].push_back(entity_sell_category::RopesPlant);
        map[item_type::CHAIN].push_back(entity_sell_category::RopesSilk);
        map[item_type::CHAIN].push_back(entity_sell_category::RopesYarn);
        map[item_type::FLASK].push_back(entity_sell_category::FlasksWaterskins);
        map[item_type::GOBLET].push_back(entity_sell_category::CupsMugsGoblets);
        map[item_type::INSTRUMENT].push_back(entity_sell_category::Instruments);
        map[item_type::TOY].push_back(entity_sell_category::Toys);
        map[item_type::CAGE].push_back(entity_sell_category::Cages);
        map[item_type::BARREL].push_back(entity_sell_category::Barrels);
        map[item_type::BUCKET].push_back(entity_sell_category::Buckets);
        map[item_type::WEAPON].push_back(entity_sell_category::Weapons);
        map[item_type::WEAPON].push_back(entity_sell_category::TrainingWeapons);
        map[item_type::WEAPON].push_back(entity_sell_category::DiggingImplements);
        map[item_type::ARMOR].push_back(entity_sell_category::Bodywear);
        map[item_type::SHOES].push_back(entity_sell_category::Footwear);
        map[item_type::SHIELD].push_back(entity_sell_category::Shields);
        map[item_type::HELM].push_back(entity_sell_category::Headwear);
        map[item_type::GLOVES].push_back(entity_sell_category::Handwear);
        map[item_type::BOX].push_back(entity_sell_category::BagsYarn);
        map[item_type::BOX].push_back(entity_sell_category::BagsLeather);
        map[item_type::BOX].push_back(entity_sell_category::BagsPlant);
        map[item_type::BOX].push_back(entity_sell_category::BagsSilk);
        map[item_type::FIGURINE].push_back(entity_sell_category::Crafts);
        map[item_type::AMULET].push_back(entity_sell_category::Crafts);
        map[item_type::SCEPTER].push_back(entity_sell_category::Crafts);
        map[item_type::AMMO].push_back(entity_sell_category::Ammo);
        map[item_type::CROWN].push_back(entity_sell_category::Crafts);
        map[item_type::RING].push_back(entity_sell_category::Crafts);
        map[item_type::EARRING].push_back(entity_sell_category::Crafts);
        map[item_type::BRACELET].push_back(entity_sell_category::Crafts);
        map[item_type::GEM].push_back(entity_sell_category::LargeCutGems);
        map[item_type::ANVIL].push_back(entity_sell_category::Anvils);
        map[item_type::MEAT].push_back(entity_sell_category::Meat);
        map[item_type::FISH].push_back(entity_sell_category::Fish);
        map[item_type::FISH_RAW].push_back(entity_sell_category::Fish);
        map[item_type::PET].push_back(entity_sell_category::Pets);
        map[item_type::SEEDS].push_back(entity_sell_category::Seeds);
        map[item_type::PLANT].push_back(entity_sell_category::Plants);
        map[item_type::SKIN_TANNED].push_back(entity_sell_category::Leather);
        map[item_type::PLANT_GROWTH].push_back(entity_sell_category::FruitsNuts);
        map[item_type::PLANT_GROWTH].push_back(entity_sell_category::GardenVegetables);
        map[item_type::THREAD].push_back(entity_sell_category::ThreadPlant);
        map[item_type::THREAD].push_back(entity_sell_category::ThreadSilk);
        map[item_type::THREAD].push_back(entity_sell_category::ThreadYarn);
        map[item_type::CLOTH].push_back(entity_sell_category::ClothPlant);
        map[item_type::CLOTH].push_back(entity_sell_category::ClothSilk);
        map[item_type::CLOTH].push_back(entity_sell_category::ClothYarn);
        map[item_type::PANTS].push_back(entity_sell_category::Legwear);
        map[item_type::BACKPACK].push_back(entity_sell_category::Backpacks);
        map[item_type::QUIVER].push_back(entity_sell_category::Quivers);
        map[item_type::TRAPCOMP].push_back(entity_sell_category::TrapComponents);
        map[item_type::DRINK].push_back(entity_sell_category::Drinks);
        map[item_type::POWDER_MISC].push_back(entity_sell_category::Powders);
        map[item_type::POWDER_MISC].push_back(entity_sell_category::Sand);
        map[item_type::CHEESE].push_back(entity_sell_category::Cheese);
        map[item_type::LIQUID_MISC].push_back(entity_sell_category::Extracts);
        map[item_type::LIQUID_MISC].push_back(entity_sell_category::Miscellaneous);
        map[item_type::SPLINT].push_back(entity_sell_category::Splints);
        map[item_type::CRUTCH].push_back(entity_sell_category::Crutches);
        map[item_type::TOOL].push_back(entity_sell_category::Tools);
        map[item_type::EGG].push_back(entity_sell_category::Eggs);
        map[item_type::SHEET].push_back(entity_sell_category::Parchment);
    }
} item_type_to_sell_category;

const static struct sell_category_matchers_t
{
    std::map<df::entity_sell_category, std::function<bool(df::historical_entity *, size_t, df::item_type, int16_t, int16_t, int32_t)>> map;

    bool operator()(df::entity_sell_category category, df::historical_entity *entity, size_t idx, df::item_type item_type, int16_t item_subtype, int16_t mat_type, int32_t mat_index) const
    {
        auto it = map.find(category);
        if (it == map.end())
        {
            return false;
        }
        return it->second(entity, idx, item_type, item_subtype, mat_type, mat_index);
    }

    sell_category_matchers_t()
    {
        map[entity_sell_category::Leather] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.organic.leather, idx, mat_type, mat_index);
        };
        map[entity_sell_category::ClothPlant] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.organic.fiber, idx, mat_type, mat_index);
        };
        map[entity_sell_category::ClothSilk] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.organic.silk, idx, mat_type, mat_index);
        };

        map[entity_sell_category::Crafts] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.misc_mat.crafts, idx, mat_type, mat_index);
        };

        map[entity_sell_category::Wood] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.organic.wood, idx, mat_type, mat_index);
        };

        map[entity_sell_category::MetalBars] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_inorganic_mat(entity->resources.metals, idx, mat_type, mat_index);
        };
        map[entity_sell_category::SmallCutGems] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_inorganic_mat(entity->resources.gems, idx, mat_type, mat_index);
        };
        map[entity_sell_category::LargeCutGems] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_inorganic_mat(entity->resources.gems, idx, mat_type, mat_index);
        };
        map[entity_sell_category::StoneBlocks] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_inorganic_mat(entity->resources.stones, idx, mat_type, mat_index);
        };

        map[entity_sell_category::Seeds] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.seeds, idx, mat_type, mat_index);
        };

        map[entity_sell_category::Anvils] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.metal.anvil, idx, mat_type, mat_index);
        };

        map[entity_sell_category::Weapons] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t item_subtype, int16_t, int32_t) -> bool
        {
            return match_item_type_subtype(entity->resources.weapon_type, idx, item_subtype);
        };
        map[entity_sell_category::TrainingWeapons] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t item_subtype, int16_t, int32_t) -> bool
        {
            return match_item_type_subtype(entity->resources.training_weapon_type, idx, item_subtype);
        };
        map[entity_sell_category::Ammo] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t item_subtype, int16_t, int32_t) -> bool
        {
            return match_item_type_subtype(entity->resources.ammo_type, idx, item_subtype);
        };
        map[entity_sell_category::TrapComponents] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t item_subtype, int16_t, int32_t) -> bool
        {
            return match_item_type_subtype(entity->resources.trapcomp_type, idx, item_subtype);
        };
        map[entity_sell_category::DiggingImplements] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t item_subtype, int16_t, int32_t) -> bool
        {
            return match_item_type_subtype(entity->resources.digger_type, idx, item_subtype);
        };

        map[entity_sell_category::Bodywear] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t item_subtype, int16_t, int32_t) -> bool
        {
            return match_item_type_subtype(entity->resources.armor_type, idx, item_subtype);
        };
        map[entity_sell_category::Headwear] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t item_subtype, int16_t, int32_t) -> bool
        {
            return match_item_type_subtype(entity->resources.helm_type, idx, item_subtype);
        };
        map[entity_sell_category::Handwear] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t item_subtype, int16_t, int32_t) -> bool
        {
            return match_item_type_subtype(entity->resources.gloves_type, idx, item_subtype);
        };
        map[entity_sell_category::Footwear] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t item_subtype, int16_t, int32_t) -> bool
        {
            return match_item_type_subtype(entity->resources.shoes_type, idx, item_subtype);
        };
        map[entity_sell_category::Legwear] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t item_subtype, int16_t, int32_t) -> bool
        {
            return match_item_type_subtype(entity->resources.pants_type, idx, item_subtype);
        };
        map[entity_sell_category::Shields] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t item_subtype, int16_t, int32_t) -> bool
        {
            return match_item_type_subtype(entity->resources.shield_type, idx, item_subtype);
        };

        map[entity_sell_category::Toys] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t item_subtype, int16_t, int32_t) -> bool
        {
            return match_item_type_subtype(entity->resources.toy_type, idx, item_subtype);
        };
        map[entity_sell_category::Instruments] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t item_subtype, int16_t, int32_t) -> bool
        {
            return match_item_type_subtype(entity->resources.instrument_type, idx, item_subtype);
        };

        map[entity_sell_category::Pets] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_race_caste(entity->resources.animals.pet_races, entity->resources.animals.pet_castes, idx, mat_type, mat_index);
        };

        map[entity_sell_category::Drinks] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.misc_mat.booze, idx, mat_type, mat_index);
        };
        map[entity_sell_category::Cheese] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.misc_mat.cheese, idx, mat_type, mat_index);
        };
        map[entity_sell_category::Powders] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.misc_mat.powders, idx, mat_type, mat_index);
        };
        map[entity_sell_category::Extracts] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.misc_mat.extracts, idx, mat_type, mat_index);
        };
        map[entity_sell_category::Meat] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.misc_mat.meat, idx, mat_type, mat_index);
        };

        map[entity_sell_category::Fish] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_race_caste(entity->resources.fish_races, entity->resources.fish_castes, idx, mat_type, mat_index);
        };

        map[entity_sell_category::Plants] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.plants, idx, mat_type, mat_index);
        };

        // TODO: FruitsNuts, GardenVegetables, MeatFishRecipes, OtherRecipes,

        map[entity_sell_category::Stone] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_inorganic_mat(entity->resources.stones, idx, mat_type, mat_index);
        };

        map[entity_sell_category::Cages] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.misc_mat.cages, idx, mat_type, mat_index);
        };

        map[entity_sell_category::BagsLeather] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.organic.leather, idx, mat_type, mat_index);
        };
        map[entity_sell_category::BagsPlant] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.organic.fiber, idx, mat_type, mat_index);
        };
        map[entity_sell_category::BagsSilk] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.organic.silk, idx, mat_type, mat_index);
        };

        map[entity_sell_category::ThreadPlant] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.organic.fiber, idx, mat_type, mat_index);
        };
        map[entity_sell_category::ThreadSilk] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.organic.silk, idx, mat_type, mat_index);
        };

        map[entity_sell_category::RopesPlant] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.organic.fiber, idx, mat_type, mat_index);
        };
        map[entity_sell_category::RopesSilk] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.organic.silk, idx, mat_type, mat_index);
        };

        map[entity_sell_category::Barrels] = [](df::historical_entity *entity, size_t idx, df::item_type item_type, int32_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.misc_mat.barrels, idx, mat_type, mat_index);
        };
        map[entity_sell_category::FlasksWaterskins] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.misc_mat.flasks, idx, mat_type, mat_index);
        };
        map[entity_sell_category::Quivers] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.misc_mat.quivers, idx, mat_type, mat_index);
        };
        map[entity_sell_category::Backpacks] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.misc_mat.backpacks, idx, mat_type, mat_index);
        };
        map[entity_sell_category::Sand] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.misc_mat.sand, idx, mat_type, mat_index);
        };
        map[entity_sell_category::Glass] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.misc_mat.glass, idx, mat_type, mat_index);
        };

        map[entity_sell_category::Miscellaneous] = [](df::historical_entity *entity, size_t idx, df::item_type item_type, int16_t item_subtype, int16_t mat_type, int32_t mat_index) -> bool
        {
            return item_type == entity->resources.wood_products.item_type.at(idx) &&
                item_subtype == entity->resources.wood_products.item_subtype.at(idx) &&
                mat_type == entity->resources.wood_products.material.mat_type.at(idx) &&
                mat_index == entity->resources.wood_products.material.mat_index.at(idx);
        };

        map[entity_sell_category::Buckets] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.misc_mat.barrels, idx, mat_type, mat_index);
        };
        map[entity_sell_category::Splints] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.misc_mat.barrels, idx, mat_type, mat_index);
        };
        map[entity_sell_category::Crutches] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.misc_mat.barrels, idx, mat_type, mat_index);
        };

        map[entity_sell_category::Eggs] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_race_caste(entity->resources.egg_races, entity->resources.egg_castes, idx, mat_type, mat_index);
        };

        map[entity_sell_category::BagsYarn] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.organic.wool, idx, mat_type, mat_index);
        };
        map[entity_sell_category::RopesYarn] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.organic.wool, idx, mat_type, mat_index);
        };
        map[entity_sell_category::ClothYarn] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.organic.wool, idx, mat_type, mat_index);
        };
        map[entity_sell_category::ThreadYarn] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.organic.wool, idx, mat_type, mat_index);
        };

        map[entity_sell_category::Tools] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t item_subtype, int16_t, int32_t) -> bool
        {
            return match_item_type_subtype(entity->resources.tool_type, idx, item_subtype);
        };

        map[entity_sell_category::Clay] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.misc_mat.clay, idx, mat_type, mat_index);
        };

        map[entity_sell_category::Parchment] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.organic.parchment, idx, mat_type, mat_index);
        };
        map[entity_sell_category::CupsMugsGoblets] = [](df::historical_entity *entity, size_t idx, df::item_type, int16_t, int16_t mat_type, int32_t mat_index) -> bool
        {
            return match_mat_vec(entity->resources.misc_mat.crafts, idx, mat_type, mat_index);
        };
    }
} sell_category_matchers;

// copy of Item::getValue() but also applies entity, race and caravan modifications, agreements adjustment, and custom qty
int32_t Trade::item_value_for_caravan(df::item *item, df::caravan_state *caravan, df::historical_entity *entity, df::creature_raw *creature, int32_t adjustment, int32_t qty)
{
    auto item_type = item->getType();
    auto item_subtype = item->getSubtype();
    auto mat_type = item->getMaterial();
    auto mat_index = item->getMaterialIndex();

    // Get base value for item type, subtype, and material
    int32_t value;
    if (item_type == item_type::CHEESE)
    {
        // TODO: seems to be wrong in dfhack's getItemBaseValue() ?
        value = 10;
    }
    else
    {
        value = Items::getItemBaseValue(item_type, item_subtype, mat_type, mat_index);
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
        //TODO: why 7 ?
        if (creature->adultsize >= 7)
        {
            if (auto armor = virtual_cast<df::item_armorst>(item))
            {
                auto def = armor->subtype;
                if (def->armorlevel > 0 || def->flags.is_set(armor_flags::METAL_ARMOR_LEVELS))
                {
                    value = value * 2;
                }
            }
            else if (auto gloves = virtual_cast<df::item_glovesst>(item))
            {
                auto def = gloves->subtype;
                if (def->armorlevel > 0 || def->flags.is_set(gloves_flags::METAL_ARMOR_LEVELS))
                {
                    value = value * 2;
                }
            }
            else if (auto shoes = virtual_cast<df::item_shoesst>(item))
            {
                auto def = shoes->subtype;
                if (def->armorlevel > 0 || def->flags.is_set(shoes_flags::METAL_ARMOR_LEVELS))
                {
                    value = value * 2;
                }
            }
            else if (auto helm = virtual_cast<df::item_helmst>(item))
            {
                auto def = helm->subtype;
                if (def->armorlevel > 0 || def->flags.is_set(helm_flags::METAL_ARMOR_LEVELS))
                {
                    value = value * 2;
                }
            }
            else if (auto pants = virtual_cast<df::item_pantsst>(item))
            {
                auto def = pants->subtype;
                if (def->armorlevel > 0 || def->flags.is_set(pants_flags::METAL_ARMOR_LEVELS))
                {
                    value = value * 2;
                }
            }
        }

        // shields
        if (auto shield = virtual_cast<df::item_shieldst>(item))
        {
            auto def = shield->subtype;
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

    value = value * adjustment / 128;

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
        auto pet = df::creature_raw::find(mat_type);
        if (pet && size_t(mat_index) < pet->caste.size())
        {
            divisor = pet->caste.at(size_t(mat_index))->misc.petvalue_divisor;
        }
        if (divisor > 1)
        {
            value = value / divisor;
        }
    }

    return value;
}

int32_t Trade::item_price_for_caravan(df::item *item, df::caravan_state *caravan, df::historical_entity *entity, df::creature_raw *creature, int32_t qty, df::entity_buy_prices *pricetable_buy, df::entity_sell_prices *pricetable_sell)
{
    auto item_type = item->getType();
    auto item_subtype = item->getSubtype();
    auto mat_type = item->getMaterial();
    auto mat_index = item->getMaterialIndex();

    int32_t adjustment_buy = 128;
    int32_t adjustment_sell = 128;

    if (pricetable_buy)
    {
        auto reqs = pricetable_buy->items;
        bool matched = false;
        for (size_t i = 0; i < reqs->item_type.size(); i++)
        {
            if (item_type == reqs->item_type.at(i) && (reqs->item_subtype.at(i) == -1 || item_subtype == reqs->item_subtype.at(i)))
            {
                if (reqs->mat_types.at(i) != -1)
                {
                    matched = (mat_type == reqs->mat_types.at(i) && mat_index == reqs->mat_indices.at(i));
                }
                else
                {
                    bool any_cat = reqs->mat_cats.at(i).whole == 0;

                    matched = any_cat || MaterialInfo(mat_type, mat_index).matches(reqs->mat_cats.at(i));
                }

                if (matched)
                {
                    adjustment_buy = pricetable_buy->price.at(i);
                    break;
                }
            }
        }
    }

    if (pricetable_sell)
    {
        auto & sell_cats = item_type_to_sell_category[item_type];
        for (auto cat : sell_cats)
        {
            bool matched = false;

            for (size_t i = 0; i < pricetable_sell->price[cat].size(); i++)
            {
                if (pricetable_sell->price[cat].at(i) != 128 && sell_category_matchers(cat, entity, i, item_type, item_subtype, mat_type, mat_index))
                {
                    matched = true;
                    adjustment_sell = pricetable_sell->price[cat].at(i);
                    break;
                }
            }

            if (matched)
            {
                break;
            }
        }
    }

    return item_value_for_caravan(item, caravan, entity, creature, std::max(adjustment_buy, adjustment_sell), qty);
}

int32_t Trade::item_or_container_price_for_caravan(df::item *item, df::caravan_state *caravan, df::historical_entity *entity, df::creature_raw *creature, int32_t qty, df::entity_buy_prices *pricetable_buy, df::entity_sell_prices *pricetable_sell)
{
    auto value = item_price_for_caravan(item, caravan, entity, creature, qty, pricetable_buy, pricetable_sell);

    for (auto & ref : item->general_refs)
    {
        if (auto contains_item = virtual_cast<df::general_ref_contains_itemst>(ref))
        {
            auto item2 = df::item::find(contains_item->item_id);
            value = value + item_or_container_price_for_caravan(item2, caravan, entity, creature, item2->getStackSize(), pricetable_buy, pricetable_sell);
        }
        else if (auto contains_unit = virtual_cast<df::general_ref_contains_unitst>(ref))
        {
            auto unit2 = df::unit::find(contains_unit->unit_id);
            auto creature_raw = df::creature_raw::find(unit2->race);
            auto caste_raw = creature_raw->caste.at(unit2->caste);
            value = value + caste_raw->misc.petvalue;
        }
    }

    return value;
}
