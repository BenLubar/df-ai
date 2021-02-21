#include "ai.h"
#include "plan.h"

#include <unordered_map>

#include "modules/World.h"

command_result Plan::persist(color_ostream &)
{
    if (rooms_and_corridors.empty())
    {
        // we haven't initialized yet.
        return CR_OK;
    }
    std::ofstream f("data/save/current/df-ai-plan.dat", std::ofstream::trunc);
    save(f);
    return CR_OK;
}

command_result Plan::unpersist(color_ostream &)
{
    std::remove("data/save/current/df-ai-plan.dat");
    std::remove(("data/save/" + World::ReadWorldFolder() + "/df-ai-plan.dat").c_str());
    return CR_OK;
}

void Plan::save(std::ostream & out)
{
    std::vector<furniture *> all_furniture;
    std::vector<room *> all_rooms;
    std::unordered_map<furniture *, size_t> furniture_index;
    std::unordered_map<room *, size_t> room_index;

    auto add_furniture = [&all_furniture, &furniture_index](furniture *f)
    {
        if (furniture_index.count(f))
            return;
        furniture_index[f] = all_furniture.size();
        all_furniture.push_back(f);
    };

    auto add_room = [add_furniture, &all_rooms, &room_index](room *r)
    {
        if (room_index.count(r))
            return;
        room_index[r] = all_rooms.size();
        all_rooms.push_back(r);

        for (auto it = r->layout.begin(); it != r->layout.end(); it++)
        {
            add_furniture(*it);
        }
    };

    for (auto it = rooms_and_corridors.begin(); it != rooms_and_corridors.end(); it++)
    {
        add_room(*it);
    }

    std::ostringstream stringify;

    Json::Value converted_tasks(Json::arrayValue);
    for (auto it = tasks_generic.begin(); it != tasks_generic.end(); it++)
    {
        Json::Value t(Json::objectValue);
        stringify.str(std::string());
        stringify.clear();
        stringify << (*it)->type;
        t["t"] = stringify.str();
        if ((*it)->r)
        {
            t["r"] = Json::Int(room_index.at((*it)->r));
        }
        if ((*it)->f)
        {
            t["f"] = Json::Int(furniture_index.at((*it)->f));
        }
        if (!(*it)->last_status.empty())
        {
            t["l"] = (*it)->last_status;
        }
        if ((*it)->item_id != -1)
        {
            t["i"] = (*it)->item_id;
        }
        converted_tasks.append(t);
    }
    for (auto it = tasks_furniture.begin(); it != tasks_furniture.end(); it++)
    {
        Json::Value t(Json::objectValue);
        stringify.str(std::string());
        stringify.clear();
        stringify << (*it)->type;
        t["t"] = stringify.str();
        if ((*it)->r)
        {
            t["r"] = Json::Int(room_index.at((*it)->r));
        }
        if ((*it)->f)
        {
            t["f"] = Json::Int(furniture_index.at((*it)->f));
        }
        converted_tasks.append(t);
    }

    Json::Value converted_rooms(Json::arrayValue);
    for (auto it = all_rooms.begin(); it != all_rooms.end(); it++)
    {
        Json::Value r(Json::objectValue);
        stringify.str(std::string());
        stringify.clear();
        stringify << (*it)->status;
        r["status"] = stringify.str();
        stringify.str(std::string());
        stringify.clear();
        stringify << (*it)->type;
        r["type"] = stringify.str();
        if ((*it)->type == room_type::corridor || (*it)->corridor_type != corridor_type::type())
        {
            stringify.str(std::string());
            stringify.clear();
            stringify << (*it)->corridor_type;
            r["corridor_type"] = stringify.str();
        }
        if ((*it)->type == room_type::farmplot || (*it)->farm_type != farm_type::type())
        {
            stringify.str(std::string());
            stringify.clear();
            stringify << (*it)->farm_type;
            r["farm_type"] = stringify.str();
        }
        if ((*it)->type == room_type::stockpile || (*it)->stockpile_type != stockpile_type::type())
        {
            stringify.str(std::string());
            stringify.clear();
            stringify << (*it)->stockpile_type;
            r["stockpile_type"] = stringify.str();
        }
        if ((*it)->type == room_type::nobleroom || (*it)->nobleroom_type != nobleroom_type::type())
        {
            stringify.str(std::string());
            stringify.clear();
            stringify << (*it)->nobleroom_type;
            r["nobleroom_type"] = stringify.str();
        }
        if ((*it)->type == room_type::outpost || (*it)->outpost_type != outpost_type::type())
        {
            stringify.str(std::string());
            stringify.clear();
            stringify << (*it)->outpost_type;
            r["outpost_type"] = stringify.str();
        }
        if ((*it)->type == room_type::location || (*it)->location_type != location_type::type())
        {
            stringify.str(std::string());
            stringify.clear();
            stringify << (*it)->location_type;
            r["location_type"] = stringify.str();
        }
        if ((*it)->type == room_type::cistern || (*it)->cistern_type != cistern_type::type())
        {
            stringify.str(std::string());
            stringify.clear();
            stringify << (*it)->cistern_type;
            r["cistern_type"] = stringify.str();
        }
        if ((*it)->type == room_type::workshop || (*it)->workshop_type != df::workshop_type())
        {
            r["workshop_type"] = enum_item_key((*it)->workshop_type);
        }
        if ((*it)->type == room_type::furnace || (*it)->furnace_type != df::furnace_type())
        {
            r["furnace_type"] = enum_item_key((*it)->furnace_type);
        }
        if (((*it)->type == room_type::workshop && (*it)->workshop_type == workshop_type::Custom) ||
            ((*it)->type == room_type::furnace && (*it)->furnace_type == furnace_type::Custom) ||
            !(*it)->raw_type.empty())
        {
            r["raw_type"] = (*it)->raw_type;
        }
        r["comment"] = (*it)->comment;
        Json::Value r_min(Json::arrayValue);
        r_min.append(Json::Int((*it)->min.x));
        r_min.append(Json::Int((*it)->min.y));
        r_min.append(Json::Int((*it)->min.z));
        r["min"] = r_min;
        Json::Value r_max(Json::arrayValue);
        r_max.append(Json::Int((*it)->max.x));
        r_max.append(Json::Int((*it)->max.y));
        r_max.append(Json::Int((*it)->max.z));
        r["max"] = r_max;
        Json::Value r_accesspath(Json::arrayValue);
        for (auto it_ = (*it)->accesspath.begin(); it_ != (*it)->accesspath.end(); it_++)
        {
            r_accesspath.append(Json::Int(room_index.at(*it_)));
        }
        r["accesspath"] = r_accesspath;
        Json::Value r_layout(Json::arrayValue);
        for (auto it_ = (*it)->layout.begin(); it_ != (*it)->layout.end(); it_++)
        {
            r_layout.append(Json::Int(furniture_index.at(*it_)));
        }
        r["layout"] = r_layout;
        r["owner"] = Json::Int((*it)->owner);
        r["bld_id"] = Json::Int((*it)->bld_id);
        r["squad_id"] = Json::Int((*it)->squad_id);
        r["level"] = Json::Int((*it)->level);
        r["noblesuite"] = Json::Int((*it)->noblesuite);
        r["queue"] = (*it)->queue;
        if ((*it)->workshop)
        {
            r["workshop"] = Json::Int(room_index.at((*it)->workshop));
        }
        Json::Value r_users(Json::arrayValue);
        for (auto it_ = (*it)->users.begin(); it_ != (*it)->users.end(); it_++)
        {
            r_users.append(Json::Int(*it_));
        }
        r["users"] = r_users;
        Json::Value r_channel_enable(Json::arrayValue);
        r_channel_enable.append(Json::Int((*it)->channel_enable.x));
        r_channel_enable.append(Json::Int((*it)->channel_enable.y));
        r_channel_enable.append(Json::Int((*it)->channel_enable.z));
        r["channel_enable"] = r_channel_enable;
        Json::Value r_stock_disable(Json::arrayValue);
        for (auto it_ = (*it)->stock_disable.begin(); it_ != (*it)->stock_disable.end(); it_++)
        {
            r_stock_disable.append(ENUM_KEY_STR(stockpile_list, *it_));
        }
        r["stock_disable"] = r_stock_disable;
        r["stock_specific1"] = (*it)->stock_specific1;
        r["stock_specific2"] = (*it)->stock_specific2;
        r["has_users"] = Json::LargestUInt((*it)->has_users);
        r["furnished"] = (*it)->furnished;
        r["queue_dig"] = (*it)->queue_dig;
        r["temporary"] = (*it)->temporary;
        r["outdoor"] = (*it)->outdoor;
        r["channeled"] = (*it)->channeled;
        if ((*it)->build_when_accessible)
        {
            r["build_when_accessible"] = true;
        }
        r["required_value"] = (*it)->required_value;
        if ((*it)->data1 != -1 || (*it)->data2 != -1)
        {
            r["data1"] = Json::Int((*it)->data1);
        }
        if ((*it)->data2 != -1)
        {
            r["data2"] = Json::Int((*it)->data2);
        }
        converted_rooms.append(r);
    }

    Json::Value converted_furniture(Json::arrayValue);
    for (auto it = all_furniture.begin(); it != all_furniture.end(); it++)
    {
        Json::Value f(Json::objectValue);
        stringify.str(std::string());
        stringify.clear();
        stringify << (*it)->type;
        f["type"] = stringify.str();
        f["construction"] = ENUM_KEY_STR(construction_type, (*it)->construction);
        f["dig"] = ENUM_KEY_STR(tile_dig_designation, (*it)->dig);
        f["bld_id"] = Json::Int((*it)->bld_id);
        f["x"] = Json::Int((*it)->pos.x);
        f["y"] = Json::Int((*it)->pos.y);
        f["z"] = Json::Int((*it)->pos.z);
        if ((*it)->target)
        {
            f["target"] = Json::Int(furniture_index.at((*it)->target));
        }
        Json::Value f_users(Json::arrayValue);
        for (auto it_ = (*it)->users.begin(); it_ != (*it)->users.end(); it_++)
        {
            f_users.append(Json::Int(*it_));
        }
        f["users"] = f_users;
        f["has_users"] = Json::LargestUInt((*it)->has_users);
        f["ignore"] = (*it)->ignore;
        f["makeroom"] = (*it)->makeroom;
        f["internal"] = (*it)->internal;
        converted_furniture.append(f);
    }

    Json::Value all(Json::objectValue);
    all["t"] = converted_tasks;
    all["r"] = converted_rooms;
    all["f"] = converted_furniture;
    all["entrance"] = Json::Int(room_index.at(fort_entrance));
    all["p"] = priorities_to_json(priorities);

    out << all;
}

void Plan::load(std::istream & in)
{
    for (auto it = tasks_generic.begin(); it != tasks_generic.end(); it++)
    {
        delete *it;
    }
    tasks_generic.clear();
    for (auto it = tasks_furniture.begin(); it != tasks_furniture.end(); it++)
    {
        delete *it;
    }
    tasks_furniture.clear();
    for (auto it = rooms_and_corridors.begin(); it != rooms_and_corridors.end(); it++)
    {
        delete *it;
    }
    rooms_and_corridors.clear();
    priorities.clear();

    Json::Value all(Json::objectValue);
    in >> all;

    std::vector<room *> all_rooms;
    std::vector<furniture *> all_furniture;

    for (unsigned int i = all["r"].size(); i > 0; i--)
    {
        all_rooms.push_back(new room(room_type::type(), df::coord(), df::coord()));
    }
    for (unsigned int i = all["f"].size(); i > 0; i--)
    {
        all_furniture.push_back(new furniture());
    }

    std::ostringstream stringify;

#define ENUM_NAMES(name, type) \
    std::map<std::string, name::type> name ## _names; \
    for (int i = 0; i < name::_ ## name ## _count; i++) \
    { \
        stringify.str(std::string()); \
        stringify.clear(); \
        stringify << name::type(i); \
        name ## _names[stringify.str()] = name::type(i); \
    }

    ENUM_NAMES(task_type, type);
    task_type_names["checkconstruct"] = task_type::check_construct;
    task_type_names["checkfurnish"] = task_type::check_furnish;
    task_type_names["checkidle"] = task_type::check_idle;
    task_type_names["checkrooms"] = task_type::check_rooms;
    task_type_names["digroom"] = task_type::dig_room;
    task_type_names["wantdig"] = task_type::want_dig;

    for (auto it = all["t"].begin(); it != all["t"].end(); it++)
    {
        task *t = new task(task_type_names.at((*it)["t"].asString()));
        if (it->isMember("r"))
        {
            t->r = all_rooms.at((*it)["r"].asInt());
        }
        if (it->isMember("f"))
        {
            t->f = all_furniture.at((*it)["f"].asInt());
        }
        if (it->isMember("l"))
        {
            t->last_status = (*it)["l"].asString();
        }
        if (it->isMember("i"))
        {
            t->item_id = (*it)["i"].asInt();
        }
        if (t->type == task_type::furnish || t->type == task_type::check_furnish)
        {
            tasks_furniture.push_back(t);
        }
        else
        {
            tasks_generic.push_back(t);
        }
    }

    ENUM_NAMES(room_status, status);
    ENUM_NAMES(room_type, type);
    room_type_names["cemetary"] = room_type::cemetery;
    room_type_names["garbagepit"] = room_type::corridor;
    ENUM_NAMES(corridor_type, type);
    corridor_type_names[""] = corridor_type::corridor;
    ENUM_NAMES(farm_type, type);
    ENUM_NAMES(stockpile_type, type);
    ENUM_NAMES(nobleroom_type, type);
    nobleroom_type_names["diningroom"] = nobleroom_type::dining;
    ENUM_NAMES(outpost_type, type);
    ENUM_NAMES(location_type, type);
    ENUM_NAMES(cistern_type, type);
    ENUM_NAMES(layout_type, type);

    for (auto it = all_rooms.begin(); it != all_rooms.end(); it++)
    {
        const Json::Value & r = all["r"][Json::ArrayIndex(it - all_rooms.begin())];
        (*it)->status = room_status_names.at(r["status"].asString());
        (*it)->type = room_type_names.at(r["type"].asString());
        if (r.isMember("subtype"))
        {
            switch ((*it)->type)
            {
            case room_type::corridor:
                (*it)->corridor_type = corridor_type_names.at(r["subtype"].asString());
                break;
            case room_type::farmplot:
                (*it)->farm_type = farm_type_names.at(r["subtype"].asString());
                break;
            case room_type::workshop:
            {
                if (find_enum_item(&(*it)->workshop_type, r["subtype"].asString()))
                {
                    break;
                }
                if (find_enum_item(&(*it)->furnace_type, r["subtype"].asString()))
                {
                    (*it)->type = room_type::furnace;
                    break;
                }
                if (r["subtype"].asString() == "TradeDepot")
                {
                    (*it)->type = room_type::tradedepot;
                    break;
                }
                std::string subtype(r["subtype"].asString());
                for (auto it2 = subtype.begin(); it2 != subtype.end(); it2++)
                {
                    if (isupper(*it2) && !(*it)->raw_type.empty())
                    {
                        (*it)->raw_type.push_back('_');
                    }
                    (*it)->raw_type.push_back(toupper(*it2));
                }
                (*it)->workshop_type = workshop_type::Custom;
                break;
            }
            case room_type::stockpile:
                (*it)->stockpile_type = stockpile_type_names.at(r["subtype"].asString());
                break;
            case room_type::nobleroom:
                (*it)->nobleroom_type = nobleroom_type_names.at(r["subtype"].asString());
                break;
            case room_type::outpost:
                (*it)->outpost_type = outpost_type_names.at(r["subtype"].asString());
                break;
            case room_type::location:
                (*it)->location_type = location_type_names.at(r["subtype"].asString());
                break;
            case room_type::cistern:
                (*it)->cistern_type = cistern_type_names.at(r["subtype"].asString());
                break;
            default:
                assert(r["subtype"].asString().empty());
                break;
            }
        }
        else
        {
            if (r.isMember("corridor_type"))
                (*it)->corridor_type = corridor_type_names.at(r["corridor_type"].asString());
            if (r.isMember("farm_type"))
                (*it)->farm_type = farm_type_names.at(r["farm_type"].asString());
            if (r.isMember("stockpile_type"))
                (*it)->stockpile_type = stockpile_type_names.at(r["stockpile_type"].asString());
            if (r.isMember("nobleroom_type"))
                (*it)->nobleroom_type = nobleroom_type_names.at(r["nobleroom_type"].asString());
            if (r.isMember("outpost_type"))
                (*it)->outpost_type = outpost_type_names.at(r["outpost_type"].asString());
            if (r.isMember("location_type"))
                (*it)->location_type = location_type_names.at(r["location_type"].asString());
            if (r.isMember("cistern_type"))
                (*it)->cistern_type = cistern_type_names.at(r["cistern_type"].asString());
            if (r.isMember("workshop_type"))
                find_enum_item(&(*it)->workshop_type, r["workshop_type"].asString());
            if (r.isMember("furnace_type"))
                find_enum_item(&(*it)->furnace_type, r["furnace_type"].asString());
            if (r.isMember("raw_type"))
                (*it)->raw_type = r["raw_type"].asString();
        }
        (*it)->comment = r["comment"].asString();
        (*it)->min.x = r["min"][0].asInt();
        (*it)->min.y = r["min"][1].asInt();
        (*it)->min.z = r["min"][2].asInt();
        (*it)->max.x = r["max"][0].asInt();
        (*it)->max.y = r["max"][1].asInt();
        (*it)->max.z = r["max"][2].asInt();
        for (auto it_ = r["accesspath"].begin(); it_ != r["accesspath"].end(); it_++)
        {
            (*it)->accesspath.push_back(all_rooms.at(it_->asInt()));
        }
        for (auto it_ = r["layout"].begin(); it_ != r["layout"].end(); it_++)
        {
            (*it)->layout.push_back(all_furniture.at(it_->asInt()));
        }
        (*it)->owner = r["owner"].asInt();
        (*it)->bld_id = r["bld_id"].asInt();
        (*it)->squad_id = r["squad_id"].asInt();
        (*it)->level = r["level"].asInt();
        (*it)->noblesuite = r["noblesuite"].asInt();
        if (r.isMember("queue"))
        {
            (*it)->queue = r["queue"].asInt();
        }
        if (r.isMember("workshop"))
        {
            (*it)->workshop = all_rooms.at(r["workshop"].asInt());
        }
        for (auto it_ = r["users"].begin(); it_ != r["users"].end(); it_++)
        {
            (*it)->users.insert(it_->asInt());
        }
        if ((*it)->type == room_type::pasture)
        {
            ai.pop.pet_check.insert((*it)->users.begin(), (*it)->users.end());
        }
        (*it)->channel_enable.x = r["channel_enable"][0].asInt();
        (*it)->channel_enable.y = r["channel_enable"][1].asInt();
        (*it)->channel_enable.z = r["channel_enable"][2].asInt();
        for (auto it_ = r["stock_disable"].begin(); it_ != r["stock_disable"].end(); it_++)
        {
            df::stockpile_list disable;
            if (find_enum_item(&disable, it_->asString()))
            {
                (*it)->stock_disable.insert(disable);
            }
        }
        (*it)->stock_specific1 = r["stock_specific1"].asBool();
        (*it)->stock_specific2 = r["stock_specific2"].asBool();
        if (r["has_users"].isBool())
        {
            if (r["has_users"].asBool())
            {
                switch ((*it)->type)
                {
                    case room_type::farmplot:
                        (*it)->has_users = 13;
                        break;
                    default:
                        (*it)->has_users = 1;
                        break;
                }
            }
            else
            {
                (*it)->has_users = 0;
            }
        }
        else
        {
            (*it)->has_users = size_t(r["has_users"].asLargestUInt());
        }
        (*it)->furnished = r["furnished"].asBool();
        (*it)->queue_dig = r["queue_dig"].asBool();
        (*it)->temporary = r["temporary"].asBool();
        (*it)->outdoor = r["outdoor"].asBool();
        (*it)->channeled = r["channeled"].asBool();
        if (r.isMember("build_when_accessible"))
        {
            (*it)->build_when_accessible = r["build_when_accessible"].asBool();
        }
        if (r.isMember("required_value"))
        {
            (*it)->required_value = r["required_value"].asInt();
        }
        if (r.isMember("data1"))
        {
            (*it)->data1 = r["data1"].asInt();
        }
        if (r.isMember("data2"))
        {
            (*it)->data2 = r["data2"].asInt();
        }

        rooms_and_corridors.push_back(*it);
    }

    for (auto it = all_furniture.begin(); it != all_furniture.end(); it++)
    {
        const Json::Value & f = all["f"][Json::ArrayIndex(it - all_furniture.begin())];
        if (f.isMember("type"))
        {
            (*it)->type = layout_type_names.at(f["type"].asString());
        }
        else
        {
            if (f["item"].asString() == "")
            {
                (*it)->type = layout_type::none;
            }
            else if (f["item"].asString() == "archerytarget")
            {
                (*it)->type = layout_type::archery_target;
            }
            else if (f["item"].asString() == "armorstand")
            {
                (*it)->type = layout_type::armor_stand;
            }
            else if (f["item"].asString() == "bed")
            {
                (*it)->type = layout_type::bed;
            }
            else if (f["item"].asString() == "bookcase")
            {
                (*it)->type = layout_type::bookcase;
            }
            else if (f["item"].asString() == "cabinet")
            {
                (*it)->type = layout_type::cabinet;
            }
            else if (f["item"].asString() == "chair")
            {
                (*it)->type = layout_type::chair;
            }
            else if (f["item"].asString() == "chest")
            {
                (*it)->type = layout_type::chest;
            }
            else if (f["item"].asString() == "coffin")
            {
                (*it)->type = layout_type::coffin;
            }
            else if (f["item"].asString() == "door")
            {
                (*it)->type = layout_type::door;
            }
            else if (f["item"].asString() == "floodgate")
            {
                (*it)->type = layout_type::floodgate;
            }
            else if (f["item"].asString() == "gear_assembly")
            {
                (*it)->type = layout_type::gear_assembly;
            }
            else if (f["item"].asString() == "hatch")
            {
                (*it)->type = layout_type::hatch;
            }
            else if (f["item"].asString() == "hive")
            {
                (*it)->type = layout_type::hive;
            }
            else if (f["item"].asString() == "nestbox")
            {
                (*it)->type = layout_type::nest_box;
            }
            else if (f["item"].asString() == "roller")
            {
                (*it)->type = layout_type::roller;
            }
            else if (f["item"].asString() == "table")
            {
                (*it)->type = layout_type::table;
            }
            else if (f["item"].asString() == "tractionbench")
            {
                (*it)->type = layout_type::traction_bench;
            }
            else if (f["item"].asString() == "trap")
            {
                if (f["subtype"].asString() == "cage")
                {
                    (*it)->type = layout_type::cage_trap;
                }
                else if (f["subtype"].asString() == "lever")
                {
                    (*it)->type = layout_type::lever;
                }
                else if (f["subtype"].asString() == "trackstop")
                {
                    (*it)->type = layout_type::track_stop;
                }
            }
            else if (f["item"].asString() == "vertical_axle")
            {
                (*it)->type = layout_type::vertical_axle;
            }
            else if (f["item"].asString() == "weaponrack")
            {
                (*it)->type = layout_type::weapon_rack;
            }
            else if (f["item"].asString() == "well")
            {
                (*it)->type = layout_type::well;
            }
        }
        find_enum_item(&(*it)->construction, f["construction"].asString());
        find_enum_item(&(*it)->dig, f["dig"].asString());
        (*it)->bld_id = f["bld_id"].asInt();
        (*it)->pos.x = f["x"].asInt();
        (*it)->pos.y = f["y"].asInt();
        (*it)->pos.z = f["z"].asInt();
        if (f.isMember("target"))
        {
            (*it)->target = all_furniture.at(f["target"].asInt());
        }
        for (auto it_ = f["users"].begin(); it_ != f["users"].end(); it_++)
        {
            ai.pop.citizen.insert(it_->asInt());
            (*it)->users.insert(it_->asInt());
        }
        if (f["has_users"].isBool())
        {
            if (f["has_users"].asBool())
            {
                switch ((*it)->type)
                {
                    case layout_type::archery_target:
                        (*it)->has_users = 10;
                        break;
                    case layout_type::weapon_rack:
                        (*it)->has_users = 4;
                        break;
                    case layout_type::chair:
                        (*it)->has_users = 2;
                        break;
                    case layout_type::table:
                        (*it)->has_users = 2;
                        break;
                    default:
                        (*it)->has_users = 1;
                        break;
                }
            }
            else
            {
                (*it)->has_users = 0;
            }
        }
        else
        {
            (*it)->has_users = size_t(f["has_users"].asLargestUInt());
        }
        (*it)->ignore = f["ignore"].asBool();
        (*it)->makeroom = f["makeroom"].asBool();
        (*it)->internal = f["internal"].asBool();
        if (f.isMember("comment"))
        {
            (*it)->comment = f["comment"].asString();
        }
        else if (f.isMember("way") && !f["way"].asString().empty())
        {
            (*it)->comment = f["way"].asString();
            (*it)->internal = (*it)->comment == "in";
        }
    }

    if (all.isMember("entrance"))
    {
        fort_entrance = all_rooms.at(all["entrance"].asInt());
    }
    else
    {
        fort_entrance = rooms_and_corridors.at(0);
    }
    categorize_all();

    if (all.isMember("p"))
    {
        std::string error;
        priorities_from_json(priorities, all["p"], error);
    }
}
