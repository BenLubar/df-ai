#include "stocks.h"

#include "Error.h"

#include "modules/Items.h"
#include "modules/Materials.h"
#include "modules/Units.h"

#include "df/building_trapst.h"
#include "df/caste_raw.h"
#include "df/creature_raw.h"
#include "df/entity_raw.h"
#include "df/general_ref_building_holderst.h"
#include "df/general_ref_contains_itemst.h"
#include "df/general_ref_contains_unitst.h"
#include "df/historical_entity.h"
#include "df/item_ammost.h"
#include "df/item_animaltrapst.h"
#include "df/item_armorst.h"
#include "df/item_barrelst.h"
#include "df/item_binst.h"
#include "df/item_boxst.h"
#include "df/item_bucketst.h"
#include "df/item_cagest.h"
#include "df/item_clothst.h"
#include "df/item_corpsepiecest.h"
#include "df/item_flaskst.h"
#include "df/item_foodst.h"
#include "df/item_globst.h"
#include "df/item_glovesst.h"
#include "df/item_helmst.h"
#include "df/item_pantsst.h"
#include "df/item_shieldst.h"
#include "df/item_shoesst.h"
#include "df/item_threadst.h"
#include "df/item_toolst.h"
#include "df/item_weaponst.h"
#include "df/itemdef_ammost.h"
#include "df/itemdef_armorst.h"
#include "df/itemdef_glovesst.h"
#include "df/itemdef_helmst.h"
#include "df/itemdef_pantsst.h"
#include "df/itemdef_shieldst.h"
#include "df/itemdef_shoesst.h"
#include "df/itemdef_toolst.h"
#include "df/itemdef_trapcompst.h"
#include "df/itemdef_weaponst.h"
#include "df/itemimprovement.h"
#include "df/ui.h"
#include "df/vehicle.h"
#include "df/world.h"

REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

int32_t Stocks::find_item_info::default_count(int32_t &, df::item *i)
{
    return virtual_cast<df::item_actual>(i)->stack_size;
}

df::item *Stocks::find_free_item(stock_item::item k)
{
    auto helper = find_item_helper(k);

    for (auto item : world->items.other[helper.oidx])
    {
        if (helper.pred(item) && helper.free(item))
        {
            return item;
        }
    }

    return nullptr;
}

static int32_t count_stacks(int32_t &, df::item *)
{
    return 1;
}

static bool is_cage_free(df::item *i)
{
    if (!Stocks::is_item_free(i))
    {
        return false;
    }

    if (Items::getGeneralRef(i, general_ref_type::CONTAINS_UNIT))
    {
        return false;
    }

    if (Items::getGeneralRef(i, general_ref_type::CONTAINS_ITEM))
    {
        return false;
    }

    if (auto bh = Items::getGeneralRef(i, general_ref_type::BUILDING_HOLDER))
    {
        if (virtual_cast<df::building_trapst>(bh->getBuilding()))
        {
            return false;
        }
    }

    return true;
}

Stocks::find_item_info Stocks::find_item_helper(stock_item::item k)
{
    switch (k)
    {
    case stock_item::ammo:
    {
        return find_item_helper_ammo();
    }
    case stock_item::anvil:
    {
        return find_item_info(items_other_id::ANVIL);
    }
    case stock_item::armor_feet:
    {
        return find_item_helper_armor(items_other_id::SHOES);
    }
    case stock_item::armor_hands:
    {
        return find_item_helper_armor(items_other_id::GLOVES);
    }
    case stock_item::armor_head:
    {
        return find_item_helper_armor(items_other_id::HELM);
    }
    case stock_item::armor_legs:
    {
        return find_item_helper_armor(items_other_id::PANTS);
    }
    case stock_item::armor_shield:
    {
        return find_item_helper_armor(items_other_id::SHIELD);
    }
    case stock_item::armor_stand:
    {
        return find_item_info(items_other_id::ARMORSTAND);
    }
    case stock_item::armor_torso:
    {
        return find_item_helper_armor(items_other_id::ARMOR);
    }
    case stock_item::ash:
    {
        return find_item_info(items_other_id::BAR, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.material && mat.material->id == "ASH";
        });
    }
    case stock_item::axe:
    {
        return find_item_helper_weapon(job_skill::AXE);
    }
    case stock_item::backpack:
    {
        return find_item_info(items_other_id::BACKPACK);
    }
    case stock_item::bag:
    {
        return find_item_info(items_other_id::BOX, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.isAnyCloth() || mat.material->flags.is_set(material_flags::LEATHER);
        });
    }
    case stock_item::bag_plant:
    {
        return find_item_info(items_other_id::PLANT, [this](df::item *i) -> bool
        {
            return bag_plants.count(i->getMaterialIndex()) && bag_plants.at(i->getMaterialIndex()) == i->getMaterial();
        });
    }
    case stock_item::barrel:
    {
        return find_item_info(items_other_id::BARREL, [](df::item *) -> bool { return true; }, &find_item_info::default_count, [](df::item *i) -> bool
        {
            return is_item_free(i) && virtual_cast<df::item_barrelst>(i)->stockpile.id == -1;
        });
    }
    case stock_item::bed:
    {
        return find_item_info(items_other_id::BED);
    }
    case stock_item::bin:
    {
        return find_item_info(items_other_id::BIN, [](df::item *) -> bool { return true; }, &find_item_info::default_count, [](df::item *i) -> bool
        {
            return is_item_free(i) && virtual_cast<df::item_binst>(i)->stockpile.id == -1;
        });
    }
    case stock_item::block:
    {
        return find_item_info(items_other_id::BLOCKS);
    }
    case stock_item::bone:
    {
        return find_item_info(items_other_id::CORPSEPIECE, [](df::item *item) -> bool
        {
            df::item_corpsepiecest *i = virtual_cast<df::item_corpsepiecest>(item);
            return i->corpse_flags.bits.bone && !i->corpse_flags.bits.unbutchered;
        });
    }
    case stock_item::book_binding:
    {
        return find_item_helper_tool(tool_uses::PROTECT_FOLDED_SHEETS);
    }
    case stock_item::bookcase:
    {
        return find_item_helper_tool(tool_uses::BOOKCASE);
    }
    case stock_item::bucket:
    {
        return find_item_info(items_other_id::BUCKET);
    }
    case stock_item::cabinet:
    {
        return find_item_info(items_other_id::CABINET);
    }
    case stock_item::cage:
    {
        return find_item_info(items_other_id::CAGE, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.material && !mat.material->flags.is_set(material_flags::IS_METAL);
        }, &find_item_info::default_count, &is_cage_free);
    }
    case stock_item::cage_metal:
    {
        return find_item_info(items_other_id::CAGE, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.material && mat.material->flags.is_set(material_flags::IS_METAL);
        }, &find_item_info::default_count, &is_cage_free);
    }
    case stock_item::chair:
    {
        return find_item_info(items_other_id::CHAIR);
    }
    case stock_item::chest:
    {
        return find_item_info(items_other_id::BOX, [](df::item *i) -> bool
        {
            return i->getMaterial() == 0;
        });
    }
    case stock_item::clay:
    {
        return find_item_info(items_other_id::BOULDER, [this](df::item *i) -> bool
        {
            return clay_stones.count(i->getMaterialIndex());
        });
    }
    case stock_item::cloth:
    {
        return find_item_info(items_other_id::CLOTH);
    }
    case stock_item::cloth_nodye:
    {
        return find_item_info(items_other_id::CLOTH, [this](df::item *i) -> bool
        {
            df::item_clothst *c = virtual_cast<df::item_clothst>(i);
            for (auto imp : c->improvements)
            {
                if (dye_plants.count(imp->mat_index) && dye_plants.at(imp->mat_index) == imp->mat_type)
                {
                    return false;
                }
            }
            return true;
        });
    }
    case stock_item::clothes_feet:
    {
        return find_item_helper_clothes(items_other_id::SHOES);
    }
    case stock_item::clothes_hands:
    {
        return find_item_helper_clothes(items_other_id::GLOVES);
    }
    case stock_item::clothes_head:
    {
        return find_item_helper_clothes(items_other_id::HELM);
    }
    case stock_item::clothes_legs:
    {
        return find_item_helper_clothes(items_other_id::PANTS);
    }
    case stock_item::clothes_torso:
    {
        return find_item_helper_clothes(items_other_id::ARMOR);
    }
    case stock_item::coal:
    {
        return find_item_info(items_other_id::BAR, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.material && mat.material->id == "COAL";
        });
    }
    case stock_item::coffin:
    {
        return find_item_info(items_other_id::COFFIN);
    }
    case stock_item::crutch:
    {
        return find_item_info(items_other_id::CRUTCH);
    }
    case stock_item::door:
    {
        return find_item_info(items_other_id::DOOR);
    }
    case stock_item::drink:
    {
        return find_item_info(items_other_id::DRINK);
    }
    case stock_item::drink_fruit:
    {
        return find_item_info(items_other_id::PLANT_GROWTH, [this](df::item *i) -> bool
        {
            return drink_fruits.count(i->getMaterialIndex()) && drink_fruits.at(i->getMaterialIndex()) == i->getMaterial();
        }, &count_stacks);
    }
    case stock_item::drink_plant:
    {
        return find_item_info(items_other_id::PLANT, [this](df::item *i) -> bool
        {
            return drink_plants.count(i->getMaterialIndex()) && drink_plants.at(i->getMaterialIndex()) == i->getMaterial();
        }, &count_stacks);
    }
    case stock_item::dye:
    {
        return find_item_info(items_other_id::POWDER_MISC, [this](df::item *i) -> bool
        {
            return dye_plants.count(i->getMaterialIndex()) && dye_plants.at(i->getMaterialIndex()) == i->getMaterial();
        });
    }
    case stock_item::dye_plant:
    {
        return find_item_info(items_other_id::PLANT, [this](df::item *i) -> bool
        {
            return mill_plants.count(i->getMaterialIndex()) && mill_plants.at(i->getMaterialIndex()) == i->getMaterial() && dye_plants.count(i->getMaterialIndex());
        }, &count_stacks);
    }
    case stock_item::dye_seeds:
    {
        return find_item_info(items_other_id::SEEDS, [this](df::item *i) -> bool
        {
            return dye_plants.count(i->getMaterialIndex()) && grow_plants.count(i->getMaterialIndex());
        });
    }
    case stock_item::flask:
    {
        return find_item_info(items_other_id::FLASK);
    }
    case stock_item::floodgate:
    {
        return find_item_info(items_other_id::FLOODGATE);
    }
    case stock_item::food_ingredients:
    {
        std::set<std::tuple<df::item_type, int16_t, int16_t, int32_t>> forbidden;
        for (size_t i = 0; i < ui->kitchen.item_types.size(); i++)
        {
            if ((ui->kitchen.exc_types[i] & 1) == 1)
            {
                forbidden.insert(std::make_tuple(ui->kitchen.item_types[i], ui->kitchen.item_subtypes[i], ui->kitchen.mat_types[i], ui->kitchen.mat_indices[i]));
            }
        }

        return find_item_info(items_other_id::ANY_COOKABLE, [forbidden](df::item *i) -> bool
        {
            if (virtual_cast<df::item_flaskst>(i))
                return false;
            if (virtual_cast<df::item_cagest>(i))
                return false;
            if (virtual_cast<df::item_barrelst>(i))
                return false;
            if (virtual_cast<df::item_bucketst>(i))
                return false;
            if (virtual_cast<df::item_animaltrapst>(i))
                return false;
            if (virtual_cast<df::item_boxst>(i))
                return false;
            if (virtual_cast<df::item_toolst>(i))
                return false;
            return !forbidden.count(std::make_tuple(i->getType(), i->getSubtype(), i->getMaterial(), i->getMaterialIndex()));
        }, &count_stacks);
    }
    case stock_item::food_storage:
    {
        return find_item_helper_tool(tool_uses::FOOD_STORAGE);
    }
    case stock_item::goblet:
    {
        return find_item_info(items_other_id::GOBLET);
    }
    case stock_item::gypsum:
    {
        return find_item_info(items_other_id::BOULDER, [this](df::item *i) -> bool
        {
            return is_gypsum(i);
        });
    }
    case stock_item::hatch_cover:
    {
        return find_item_info(items_other_id::HATCH_COVER);
    }
    case stock_item::hive:
    {
        return find_item_helper_tool(tool_uses::HIVE);
    }
    case stock_item::honey:
    {
        MaterialInfo honey;
        honey.findCreature("HONEY_BEE", "HONEY");

        return find_item_info(items_other_id::LIQUID_MISC, [honey](df::item *i) -> bool
        {
            return i->getMaterialIndex() == honey.index && i->getMaterial() == honey.type;
        });
    }
    case stock_item::honeycomb:
    {
        return find_item_info(items_other_id::TOOL, [](df::item *i) -> bool
        {
            auto def = virtual_cast<df::item_toolst>(i)->subtype;
            return def->id == "ITEM_TOOL_HONEYCOMB";
        });
    }
    case stock_item::jug:
    {
        return find_item_helper_tool(tool_uses::LIQUID_CONTAINER);
    }
    case stock_item::leather:
    {
        return find_item_info(items_other_id::SKIN_TANNED);
    }
    case stock_item::lye:
    {
        return find_item_info(items_other_id::LIQUID_MISC, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.material && mat.material->id == "LYE";
        });
    }
    case stock_item::meal:
    {
        return find_item_info(items_other_id::ANY_GOOD_FOOD, [](df::item *i) -> bool
        {
            return virtual_cast<df::item_foodst>(i);
        });
    }
    case stock_item::mechanism:
    {
        return find_item_info(items_other_id::TRAPPARTS);
    }
    case stock_item::metal_ore:
    {
        return find_item_info(items_other_id::BOULDER, [this](df::item *i) -> bool
        {
            return is_metal_ore(i);
        });
    }
    case stock_item::metal_strand:
    {
        return find_item_info(items_other_id::BOULDER, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.inorganic && !mat.inorganic->thread_metal.mat_index.empty();
        });
    }
    case stock_item::milk:
    {
        return find_item_info(items_other_id::LIQUID_MISC, [this](df::item *i) -> bool
        {
            return milk_creatures.count(i->getMaterialIndex()) && milk_creatures.at(i->getMaterialIndex()) == i->getMaterial();
        });
    }
    case stock_item::mill_plant:
    {
        return find_item_info(items_other_id::PLANT, [this](df::item *i) -> bool
        {
            return mill_plants.count(i->getMaterialIndex()) && mill_plants.at(i->getMaterialIndex()) == i->getMaterial();
        }, &count_stacks);
    }
    case stock_item::minecart:
    {
        return find_item_helper_tool(tool_uses::TRACK_CART);
    }
    case stock_item::nest_box:
    {
        return find_item_helper_tool(tool_uses::NEST_BOX);
    }
    case stock_item::paper:
    {
        return find_item_info(items_other_id::SHEET);
    }
    case stock_item::pick:
    {
        return find_item_helper_digger(job_skill::MINING);
    }
    case stock_item::pipe_section:
    {
        return find_item_info(items_other_id::PIPE_SECTION);
    }
    case stock_item::plaster_powder:
    {
        return find_item_info(items_other_id::POWDER_MISC, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.material && mat.material->hardens_with_water.mat_type != -1;
        });
    }
    case stock_item::quern:
    {
        return find_item_info(items_other_id::QUERN, [](df::item *) -> bool { return true; }, &count_stacks, [](df::item *) -> bool
        {
            // also count used querns
            return true;
        });
    }
    case stock_item::quire:
    {
        return find_item_helper_tool(tool_uses::CONTAIN_WRITING, [](df::itemdef_toolst *def) -> bool
        {
            return def->flags.is_set(tool_flags::INCOMPLETE_ITEM);
        });
    }
    case stock_item::quiver:
    {
        return find_item_info(items_other_id::QUIVER);
    }
    case stock_item::raw_coke:
    {
        return find_item_info(items_other_id::BOULDER, [this](df::item *i) -> bool
        {
            return !is_raw_coke(i).empty();
        });
    }
    case stock_item::raw_fish:
    {
        return find_item_info(items_other_id::FISH_RAW, [](df::item *i) -> bool
        {
            return !i->flags.bits.rotten;
        });
    }
    case stock_item::rope:
    {
        return find_item_info(items_other_id::CHAIN);
    }
    case stock_item::rough_gem:
    {
        return find_item_info(items_other_id::ROUGH, [](df::item *i) -> bool
        {
            return i->getMaterial() == 0;
        });
    }
    case stock_item::screw:
    {
        std::set<int16_t> idefs;
        for (auto id : ui->main.fortress_entity->entity_raw->equipment.trapcomp_id)
        {
            auto def = df::itemdef_trapcompst::find(id);

            if (def->flags.is_set(trapcomp_flags::IS_SCREW))
            {
                idefs.insert(id);
            }
        }

        return find_item_info(items_other_id::TRAPCOMP, [idefs](df::item *item) -> bool
        {
            return idefs.count(item->getSubtype());
        }, &find_item_info::default_count, [this](df::item *i) -> bool {
            return is_item_free(i);
        }, idefs, false);
    }
    case stock_item::shell:
    {
        return find_item_info(items_other_id::CORPSEPIECE, [](df::item *item) -> bool
        {
            df::item_corpsepiecest *i = virtual_cast<df::item_corpsepiecest>(item);
            return i->corpse_flags.bits.shell && !i->corpse_flags.bits.unbutchered;
        });
    }
    case stock_item::skull:
    {
        return find_item_info(items_other_id::CORPSEPIECE, [](df::item *item) -> bool
        {
            df::item_corpsepiecest *i = virtual_cast<df::item_corpsepiecest>(item);
            auto race = df::creature_raw::find(i->race);
            auto caste = race ? race->caste.at(i->caste) : nullptr;
            return i->corpse_flags.bits.skull && !i->corpse_flags.bits.unbutchered && (!caste || !caste->flags.is_set(caste_raw_flags::CAN_LEARN));
        });
    }
    case stock_item::slab:
    {
        return find_item_info(items_other_id::SLAB, [](df::item *i) -> bool
        {
            return i->getSlabEngravingType() == slab_engraving_type::Slab;
        });
    }
    case stock_item::slurry:
    {
        return find_item_info(items_other_id::GLOB, [](df::item *i) -> bool
        {
            if (!virtual_cast<df::item_globst>(i)->mat_state.bits.paste)
            {
                return false;
            }

            MaterialInfo mat(i);
            for (auto it : mat.material->reaction_class)
            {
                if (*it == "PAPER_SLURRY")
                {
                    return true;
                }
            }

            return false;
        });
    }
    case stock_item::slurry_plant:
    {
        return find_item_info(items_other_id::PLANT, [this](df::item *i) -> bool
        {
            return slurry_plants.count(i->getMaterialIndex()) && slurry_plants.at(i->getMaterialIndex()) == i->getMaterial();
        });
    }
    case stock_item::soap:
    {
        return find_item_info(items_other_id::BAR, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.material && mat.material->id == "SOAP";
        });
    }
    case stock_item::splint:
    {
        return find_item_info(items_other_id::SPLINT);
    }
    case stock_item::statue:
    {
        return find_item_info(items_other_id::STATUE);
    }
    case stock_item::stepladder:
    {
        return find_item_helper_tool(tool_uses::STAND_AND_WORK_ABOVE);
    }
    case stock_item::stone:
    {
        return find_item_info(items_other_id::BOULDER, [](df::item *i) -> bool
        {
            return !ui->economic_stone[i->getMaterialIndex()];
        });
    }
    case stock_item::table:
    {
        return find_item_info(items_other_id::TABLE);
    }
    case stock_item::tallow:
    {
        return find_item_info(items_other_id::GLOB, [](df::item *i) -> bool
        {
            MaterialInfo mat(i);
            return mat.material && mat.material->id == "TALLOW";
        });
    }
    case stock_item::thread:
    {
        return find_item_info(items_other_id::THREAD, [](df::item *i) -> bool
        {
            return !i->flags.bits.spider_web && virtual_cast<df::item_threadst>(i)->dimension == 15000;
        });
    }
    case stock_item::thread_plant:
    {
        return find_item_info(items_other_id::PLANT, [this](df::item *i) -> bool
        {
            return thread_plants.count(i->getMaterialIndex()) && thread_plants.at(i->getMaterialIndex()) == i->getMaterial();
        }, &count_stacks);
    }
    case stock_item::thread_seeds:
    {
        return find_item_info(items_other_id::SEEDS, [this](df::item *i) -> bool
        {
            return thread_plants.count(i->getMaterialIndex()) && grow_plants.count(i->getMaterialIndex());
        });
    }
    case stock_item::toy:
    {
        return find_item_info(items_other_id::TOY);
    }
    case stock_item::traction_bench:
    {
        return find_item_info(items_other_id::TRACTION_BENCH);
    }
    case stock_item::weapon_melee:
    {
        return find_item_helper_weapon();
    }
    case stock_item::weapon_rack:
    {
        return find_item_info(items_other_id::WEAPONRACK);
    }
    case stock_item::weapon_ranged:
    {
        return find_item_helper_weapon(job_skill::NONE, false, true);
    }
    case stock_item::weapon_training:
    {
        return find_item_helper_weapon(job_skill::NONE, true);
    }
    case stock_item::wheelbarrow:
    {
        return find_item_helper_tool(tool_uses::HEAVY_OBJECT_HAULING);
    }
    case stock_item::wood:
    {
        return find_item_info(items_other_id::WOOD);
    }
    case stock_item::wool:
    {
        // used for SpinThread which currently ignores the material_amount
        // note: if it didn't, use either HairWool or Yarn but not both
        return find_item_info(items_other_id::CORPSEPIECE, [](df::item *item) -> bool
        {
            df::item_corpsepiecest *i = virtual_cast<df::item_corpsepiecest>(item);
            return i->corpse_flags.bits.hair_wool || i->corpse_flags.bits.yarn;
        });
    }
    case stock_item::written_on_quire:
    {
        return find_item_info(items_other_id::TOOL, [this](df::item *i) -> bool
        {
            auto def = virtual_cast<df::item_toolst>(i)->subtype;
            return std::find(def->tool_use.begin(), def->tool_use.end(), tool_uses::CONTAIN_WRITING) != def->tool_use.end() &&
                def->flags.is_set(tool_flags::INCOMPLETE_ITEM) && i->hasSpecificImprovements(improvement_type::WRITING);
        });
    }
    case stock_item::_stock_item_count:
    {
        break;
    }
    }

    throw DFHack::Error::InvalidArgument();
}

template<typename I>
static Stocks::find_item_info find_item_helper_equip_helper(df::items_other_id oidx, std::set<int16_t> idefs, int32_t div = 1, std::function<bool(I *)> pred = [](I *) -> bool { return true; }, std::function<bool(I *)> free = [](I *) -> bool { return true; }, bool count_min = true)
{
    auto match = [idefs, pred](df::item *item) -> bool
    {
        if (auto u = Items::getHolderUnit(item))
        {
            if (!Units::isCitizen(u))
            {
                return false;
            }
        }

        I *i = virtual_cast<I>(item);

        return i && idefs.count(i->subtype->subtype) && pred(i);
    };

    auto count = [div](int32_t & init, df::item *i) -> int32_t
    {
        if (init == -1)
        {
            init = div;
        }
        int32_t n = 0;
        init -= i->getStackSize();
        while (init <= 0)
        {
            n++;
            init += div;
        }
        return n;
    };

    return Stocks::find_item_info(oidx, match, count, [free](df::item *i) -> bool
    {
        return Stocks::is_item_free(i) && free(virtual_cast<I>(i));
    }, idefs, count_min);
};

static Stocks::find_item_info find_item_helper_weapon_helper(const std::vector<int16_t> & defs, df::job_skill skill, bool training, bool ranged = false, bool count_min = true)
{
    std::set<int16_t> idefs;
    for (int16_t id : defs)
    {
        auto def = df::itemdef_weaponst::find(id);

        if (skill != job_skill::NONE && (ranged ? def->skill_ranged : def->skill_melee) != skill)
        {
            continue;
        }

        if (ranged == (def->skill_ranged == job_skill::NONE))
        {
            continue;
        }

        if (def->flags.is_set(weapon_flags::TRAINING) != training)
        {
            continue;
        }

        idefs.insert(id);
    }

    return find_item_helper_equip_helper<df::item_weaponst>(items_other_id::WEAPON, idefs, 1, [](df::item_weaponst *) -> bool { return true; }, [](df::item_weaponst *) -> bool { return true; }, count_min);
}

Stocks::find_item_info Stocks::find_item_helper_weapon(df::job_skill skill, bool training, bool ranged)
{
    return find_item_helper_weapon_helper(ui->main.fortress_entity->entity_raw->equipment.weapon_id, skill, training, ranged, skill == job_skill::NONE);
}

Stocks::find_item_info Stocks::find_item_helper_digger(df::job_skill skill, bool training)
{
    return find_item_helper_weapon_helper(ui->main.fortress_entity->entity_raw->equipment.digger_id, skill, training, false, skill == job_skill::NONE);
}

Stocks::find_item_info Stocks::find_item_helper_ammo(df::job_skill skill, bool training)
{
    std::set<std::string> classes;
    for (int16_t id : ui->main.fortress_entity->entity_raw->equipment.weapon_id)
    {
        auto def = df::itemdef_weaponst::find(id);

        if (skill != job_skill::NONE && def->skill_ranged != skill)
        {
            continue;
        }

        if (def->skill_ranged == job_skill::NONE)
        {
            continue;
        }

        if (def->flags.is_set(weapon_flags::TRAINING) != training)
        {
            continue;
        }

        classes.insert(def->ranged_ammo);
    }

    std::set<int16_t> idefs;
    for (int16_t id : ui->main.fortress_entity->entity_raw->equipment.ammo_id)
    {
        auto def = df::itemdef_ammost::find(id);

        if (!classes.count(def->ammo_class))
        {
            continue;
        }

        idefs.insert(id);
    }

    return find_item_helper_equip_helper<df::item_ammost>(items_other_id::AMMO, idefs);
}

template<typename D>
static bool is_armor_metal(D *def) { return def->props.flags.is_set(armor_general_flags::METAL); }

template<typename I>
static Stocks::find_item_info find_item_helper_armor_helper(df::items_other_id oidx, const std::vector<int16_t> & ids, int32_t div = 1, std::function<bool(decltype(I::subtype))> pred = &is_armor_metal<typename std::remove_pointer<decltype(I::subtype)>::type>)
{
    typedef typename std::remove_pointer<decltype(I::subtype)>::type D;
    std::set<int16_t> idefs;
    for (auto id : ids)
    {
        if (pred(D::find(id)))
        {
            idefs.insert(id);
        }
    }
    return find_item_helper_equip_helper<I>(oidx, idefs, div, [](I *i) -> bool
    {
        return i->mat_type == 0; // XXX
    });
}

Stocks::find_item_info Stocks::find_item_helper_armor(df::items_other_id oidx)
{
    auto & ue = ui->main.fortress_entity->entity_raw->equipment;
    switch (oidx)
    {
    case items_other_id::ARMOR:
        return find_item_helper_armor_helper<df::item_armorst>(oidx, ue.armor_id);
    case items_other_id::SHIELD:
        return find_item_helper_armor_helper<df::item_shieldst>(oidx, ue.shield_id, 1, [](df::itemdef_shieldst *) -> bool { return true; });
    case items_other_id::HELM:
        return find_item_helper_armor_helper<df::item_helmst>(oidx, ue.helm_id);
    case items_other_id::PANTS:
        return find_item_helper_armor_helper<df::item_pantsst>(oidx, ue.pants_id);
    case items_other_id::GLOVES:
        return find_item_helper_armor_helper<df::item_glovesst>(oidx, ue.gloves_id, 2);
    case items_other_id::SHOES:
        return find_item_helper_armor_helper<df::item_shoesst>(oidx, ue.shoes_id, 2);
    default:
        throw DFHack::Error::InvalidArgument();
    }
}

template<typename I>
static Stocks::find_item_info find_item_helper_clothes_helper(df::items_other_id oidx, const std::vector<int16_t> & ids, int32_t div = 1)
{
    typedef typename std::remove_pointer<decltype(I::subtype)>::type D;
    std::set<int16_t> idefs;
    for (auto id : ids)
    {
        D *def = D::find(id);
        if (def->props.flags.is_set(armor_general_flags::SOFT)) // XXX
        {
            idefs.insert(id);
        }
    }
    return find_item_helper_equip_helper<I>(oidx, idefs, div, [](I *i) -> bool
    {
        return i->mat_type != 0; // XXX
    }, [](I *i) -> bool
    {
        return i->wear == 0;
    });
}

Stocks::find_item_info Stocks::find_item_helper_clothes(df::items_other_id oidx)
{
    auto & ue = ui->main.fortress_entity->entity_raw->equipment;
    switch (oidx)
    {
    case items_other_id::ARMOR:
        return find_item_helper_clothes_helper<df::item_armorst>(oidx, ue.armor_id);
    case items_other_id::HELM:
        return find_item_helper_clothes_helper<df::item_helmst>(oidx, ue.helm_id);
    case items_other_id::PANTS:
        return find_item_helper_clothes_helper<df::item_pantsst>(oidx, ue.pants_id);
    case items_other_id::GLOVES:
        return find_item_helper_clothes_helper<df::item_glovesst>(oidx, ue.gloves_id, 2);
    case items_other_id::SHOES:
        return find_item_helper_clothes_helper<df::item_shoesst>(oidx, ue.shoes_id, 2);
    default:
        throw DFHack::Error::InvalidArgument();
    }
}

Stocks::find_item_info Stocks::find_item_helper_tool(df::tool_uses use, std::function<bool(df::itemdef_toolst *)> pred)
{
    const auto & tool_id = ui->main.fortress_entity->entity_raw->equipment.tool_id;
    std::set<int16_t> idefs;
    for (auto def : world->raws.itemdefs.tools_by_type[use])
    {
        if (std::find(tool_id.begin(), tool_id.end(), def->subtype) != tool_id.end() && pred(def))
        {
            idefs.insert(def->subtype);
        }
    }

    return find_item_info(items_other_id::TOOL, [this, idefs](df::item *item) -> bool
    {
        auto i = virtual_cast<df::item_toolst>(item);
        return i && idefs.count(i->subtype->subtype);
    }, &find_item_info::default_count, [](df::item *item) -> bool
    {
        auto i = virtual_cast<df::item_toolst>(item);
        return i && i->stockpile.id == -1 && (i->vehicle_id == -1 || df::vehicle::find(i->vehicle_id)->route_id == -1);
    }, idefs, false);
}
