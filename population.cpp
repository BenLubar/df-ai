#include "ai.h"
#include "population.h"
#include "plan.h"
#include "thirdparty/weblegends/weblegends-plugin.h"

#include "modules/Units.h"
#include "modules/Job.h"

#include "df/caste_raw.h"
#include "df/creature_raw.h"
#include "df/general_ref_unit_infantst.h"
#include "df/general_ref_unit_workerst.h"
#include "df/historical_entity.h"
#include "df/historical_figure.h"
#include "df/history_event_hist_figure_diedst.h"
#include "df/incident.h"
#include "df/job.h"
#include "df/job_item.h"
#include "df/occupation.h"
#include "df/squad.h"
#include "df/squad_order.h"
#include "df/squad_position.h"
#include "df/unit_relationship_type.h"
#include "df/ui.h"
#include "df/world.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(standing_orders_forbid_used_ammo);
REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

Population::Population(AI *ai) :
    ai(ai),
    citizen(),
    military(),
    pet(),
    pet_check(),
    visitor(),
    resident(),
    update_counter(0),
    onupdate_handle(nullptr),
    seen_death(0),
    deathwatch_handle(nullptr),
    medic(),
    workers(),
    seen_badwork(),
    last_checked_crime_year(-1),
    last_checked_crime_tick(-1),
    did_trade(false)
{
}

Population::~Population()
{
}

int32_t Population::days_since(int32_t year, int32_t tick)
{
    return (*cur_year - year) * 12 * 28 + (*cur_year_tick - tick) / 1200;
}

command_result Population::startup(color_ostream &)
{
    *standing_orders_forbid_used_ammo = 0;
    return CR_OK;
}

command_result Population::onupdate_register(color_ostream &)
{
    onupdate_handle = events.onupdate_register("df-ai pop", 360, 10, [this](color_ostream & out) { update(out); });
    deathwatch_handle = events.onupdate_register("df-ai pop deathwatch", 1, 1, [this](color_ostream & out) { deathwatch(out); });
    return CR_OK;
}

command_result Population::onupdate_unregister(color_ostream &)
{
    events.onupdate_unregister(onupdate_handle);
    events.onupdate_unregister(deathwatch_handle);
    return CR_OK;
}

void Population::update(color_ostream & out)
{
    update_counter++;
    switch (update_counter % 10)
    {
    case 0:
        update_trading(out);
        break;
    case 1:
        update_citizenlist(out);
        break;
    case 2:
        update_nobles(out);
        break;
    case 3:
        update_jobs(out);
        break;
    case 4:
        update_military(out);
        update_crimes(out);
        break;
    case 5:
        update_pets(out);
        break;
    case 6:
        update_deads(out);
        break;
    case 7:
        update_caged(out);
        break;
    case 8:
        update_locations(out);
        break;
    case 9:
        if (ai->eventsJson.is_open())
        {
            Json::Value payload(Json::objectValue);
            payload["citizen"] = Json::UInt(citizen.size());
            payload["military"] = Json::UInt(military.size());
            payload["pet"] = Json::UInt(pet.size());
            payload["visitor"] = Json::UInt(visitor.size());
            payload["resident"] = Json::UInt(resident.size());
            ai->event("population", payload);
        }
        break;
    }
}

void Population::new_citizen(color_ostream & out, int32_t id)
{
    citizen.insert(id);
    ai->plan->new_citizen(out, id);
}

void Population::del_citizen(color_ostream & out, int32_t id)
{
    citizen.erase(id);
    military.erase(id);
    ai->plan->del_citizen(out, id);
}

void Population::update_citizenlist(color_ostream & out)
{
    std::set<int32_t> old = citizen;

    visitor.clear();
    resident.clear();

    // add new fort citizen to our list
    for (auto u : world->units.active)
    {
        if (Units::isCitizen(u) && !Units::isBaby(u))
        {
            if (old.count(u->id))
            {
                old.erase(u->id);
            }
            else
            {
                new_citizen(out, u->id);

                if (ai->eventsJson.is_open())
                {
                    Json::Value payload(Json::objectValue);
                    payload["id"] = Json::Int(u->id);
                    payload["name"] = DF2UTF(AI::describe_name(u->name, false));
                    payload["name_english"] = DF2UTF(AI::describe_name(u->name, true));
                    payload["birth_year"] = Json::Int(u->birth_year);
                    payload["birth_time"] = Json::Int(u->birth_time);
                    if (auto race = df::creature_raw::find(u->race))
                    {
                        payload["race"] = race->creature_id;
                        payload["caste"] = race->caste.at(u->caste)->caste_id;
                    }
                    payload["sex"] = u->sex == 0 ? "female" : u->sex == 1 ? "male" : "unknown";
                    ai->event("new citizen", payload);
                }
            }
        }
        else if (Units::isCitizen(u) && Units::isBaby(u))
        {
            auto mother = df::unit::find(u->relationship_ids[unit_relationship_type::Mother]);
            if (mother && Units::isAlive(mother) && Units::isSane(mother) && u->relationship_ids[unit_relationship_type::RiderMount] == -1 && mother->job.current_job == nullptr)
            {
                // http://www.bay12games.com/dwarves/mantisbt/view.php?id=5551
                ai->debug(out, "[DF Bug 5551] reuniting mother (" + AI::describe_unit(mother) + ") with infant (" + AI::describe_unit(u) + ")");
                auto seek_infant = df::allocate<df::job>();
                seek_infant->job_type = job_type::SeekInfant;
                seek_infant->flags.bits.special = 1;
                auto unit_infant = df::allocate<df::general_ref_unit_infantst>();
                unit_infant->unit_id = u->id;
                seek_infant->general_refs.push_back(unit_infant);
                auto unit_worker = df::allocate<df::general_ref_unit_workerst>();
                unit_worker->unit_id = mother->id;
                seek_infant->general_refs.push_back(unit_worker);
                Job::linkIntoWorld(seek_infant);
                mother->job.current_job = seek_infant;
            }
        }
        else if (u->flags1.bits.dead || u->flags1.bits.merchant || u->flags1.bits.diplomat || u->flags1.bits.forest || u->flags2.bits.slaughter)
        {
            // ignore
        }
        else if (u->flags2.bits.visitor)
        {
            visitor.insert(u->id);
        }
        else if (!Units::isOwnGroup(u) && std::find_if(u->occupations.begin(), u->occupations.end(), [](df::occupation *occ) -> bool { return occ->site_id == ui->site_id; }) != u->occupations.end())
        {
            resident.insert(u->id);
        }
    }

    // del those who are no longer here
    for (auto it : old)
    {
        // u.counters.death_tg.flags.discovered dead/missing
        del_citizen(out, it);

        if (ai->eventsJson.is_open())
        {
            Json::Value payload(Json::objectValue);
            payload["id"] = Json::Int(it);
            if (df::unit *u = df::unit::find(it))
            {
                payload["name"] = DF2UTF(AI::describe_name(u->name, false));
                payload["name_english"] = DF2UTF(AI::describe_name(u->name, true));
                payload["birth_year"] = Json::Int(u->birth_year);
                payload["birth_time"] = Json::Int(u->birth_time);
                if (df::incident *i = df::incident::find(u->counters.death_id))
                {
                    payload["death_year"] = Json::Int(i->event_year);
                    payload["death_time"] = Json::Int(i->event_time);
                    payload["death_cause"] = ENUM_KEY_STR(death_type, i->death_cause);
                }
                if (df::creature_raw *race = df::creature_raw::find(u->race))
                {
                    payload["race"] = race->creature_id;
                    payload["caste"] = race->caste[u->caste]->caste_id;
                }
                payload["sex"] = u->sex == 0 ? "female" : u->sex == 1 ? "male" : "unknown";
            }
            ai->event("del citizen", payload);
        }
    }
}

void Population::update_jobs(color_ostream &)
{
    for (auto j = world->jobs.list.next; j; j = j->next)
    {
        if (j->item->flags.bits.suspend && !j->item->flags.bits.repeat)
        {
            j->item->flags.bits.suspend = 0;
        }
    }
}

std::string Population::status()
{
    return stl_sprintf("%d citizen, %d military, %d pet, %d visitor, %d resident", citizen.size(), military.size(), pet.size(), visitor.size(), resident.size());
}

void Population::report(std::ostream & out, bool html)
{
    auto do_unit = [this, &out, html](int32_t id, bool skip_end = false)
    {
        auto u = df::unit::find(id);

        if (html)
        {
            out << "<li>";
        }
        else
        {
            out << "- ";
        }
        out << AI::describe_unit(u, html);

        if (u == nullptr)
        {
            if (html)
            {
                out << "</li>";
            }
            else
            {
                out << "\n";
            }
            return;
        }

        int32_t age = days_since(u->birth_year, u->birth_time);
        out << " (age " << (age / 12 / 28) << "y" << ((age / 28) % 12) << "m" << (age % 28) << "d)";

        if (room *r = ai->find_room_at(Units::getPosition(u)))
        {
            if (html)
            {
                out << "<br/>";
            }
            else
            {
                out << "\n  ";
            }
            out << AI::describe_room(r, html);
        }

        std::string job = AI::describe_job(u);
        if (!job.empty())
        {
            if (html)
            {
                out << "<br/>" << html_escape(job);
            }
            else
            {
                out << "\n  " << job;
            }
        }
        if (!skip_end)
        {
            if (html)
            {
                out << "</li>";
            }
            else
            {
                out << "\n";
            }
        }
    };

    if (html)
    {
        out << "<h2 id=\"Population_Citizens\">Citizens</h2><ul>";
    }
    else
    {
        out << "## Citizens\n";
    }
    for (auto u : citizen)
    {
        do_unit(u);
    }
    if (html)
    {
        out << "</ul><h2 id=\"Population_Military\">Military</h2>";
    }
    else
    {
        out << "\n## Military\n";
    }
    for (auto sqid : ui->main.fortress_entity->squads)
    {
        df::squad *sq = df::squad::find(sqid);

        if (html)
        {
            out << "<h3>" << html_escape(AI::describe_name(sq->name, false)) << ", " << html_escape(AI::describe_name(sq->name, true)) << "</h3>";
        }
        else
        {
            out << "### " << AI::describe_name(sq->name, false) << ", " << AI::describe_name(sq->name, true) << "\n";
        }

        if (html)
        {
            out << "<h4>Members</h4><ul>";
        }
        else
        {
            out << "#### Members\n";
        }
        for (auto sp : sq->positions)
        {
            if (sp->occupant == -1)
            {
                if (html)
                {
                    out << "<li><i>(vacant)</i></li>";
                }
                else
                {
                    out << "- (vacant)\n";
                }
            }
            else
            {
                auto hf = df::historical_figure::find(sp->occupant);
                do_unit(hf ? hf->unit_id : -1);
            }
        }

        if (html)
        {
            out << "</ul><h4>Targets</h4><ul>";
        }
        else
        {
            out << "\n#### Targets\n";
        }
        for (auto o : sq->orders)
        {
            std::string description;
            o->getDescription(&description);
            if (html)
            {
                out << "<li>" << html_escape(description) << "</li>";
            }
            else
            {
                out << "- " << description << "\n";
            }
        }
        if (sq->orders.empty())
        {
            if (html)
            {
                out << "<li><i>(none)</i></li>";
            }
            else
            {
                out << "(none)\n";
            }
        }
        if (html)
        {
            out << "</ul>";
        }
        else
        {
            out << "\n";
        }
    }

    if (html)
    {
        out << "<h2 id=\"Population_Pets\">Pets</h2><ul>";
    }
    else
    {
        out << "\n## Pets\n";
    }
    for (auto it : pet)
    {
        do_unit(it.first, true);

        bool first = true;
        auto trait = [&first, &out, html](const std::string & name)
        {
            if (first)
            {
                if (html)
                {
                    out << "<br/>";
                }
                else
                {
                    out << "\n  ";
                }
                first = false;
            }
            else
            {
                out << ", ";
            }
            if (html)
            {
                out << html_escape(name);
            }
            else
            {
                out << name;
            }
        };

        if (it.second.bits.milkable)
        {
            trait("milkable");
        }

        if (it.second.bits.shearable)
        {
            trait("shearable");
        }

        if (it.second.bits.hunts_vermin)
        {
            trait("hunts vermin");
        }

        if (it.second.bits.grazer)
        {
            trait("grazer");
        }

        if (!first)
        {
            if (html)
            {
                out << "</li>";
            }
            else
            {
                out << "\n";
            }
        }
    }

    if (html)
    {
        out << "</ul><h2 id=\"Population_Visitors\">Visitors</h2><ul>";
    }
    else
    {
        out << "\n## Visitors\n";
    }
    for (auto it : visitor)
    {
        do_unit(it);
    }

    if (html)
    {
        out << "</ul><h2 id=\"Population_Residents\">Residents</h2><ul>";
    }
    else
    {
        out << "\n## Residents\n";
    }
    for (auto it : resident)
    {
        do_unit(it);
    }

    if (html)
    {
        out << "</ul><h2 id=\"Population_Deaths\">Deaths</h2><ul>";
    }
    else
    {
        out << "\n## Deaths\n";
    }
    for (auto e : world->history.events2)
    {
        auto d = virtual_cast<df::history_event_hist_figure_diedst>(e);

        if (!d || d->site != ui->site_id)
        {
            continue;
        }

        if (html)
        {
            out << "<li>";
            weblegends_describe_event(out, d);
            out << "</li>";
        }
        else
        {
            out << "- " << AI::describe_event(d) << "\n";
        }
    }

    std::function<std::string(const std::string &)> escape = html ? html_escape : [](const std::string & s) -> std::string { return s; };

    if (html)
    {
        out << "</ul><h2 id=\"Population_Jobs\">Jobs</h2><ul>";
    }
    else
    {
        out << "\n## Jobs\n";
    }
    std::map<std::string, size_t> boring_job_count;
    for (auto j = world->jobs.list.next; j; j = j->next)
    {
        if (j->item->items.empty() && j->item->job_items.empty() && j->item->general_refs.empty())
        {
            boring_job_count[AI::describe_job(j->item)]++;
            continue;
        }
        if (html)
        {
            out << "<li>" << html_escape(AI::describe_job(j->item));
        }
        else
        {
            out << "- " << AI::describe_job(j->item) << "\n";
        }
        std::set<size_t> handled_items;
        for (auto & item : j->item->items)
        {
            handled_items.insert(size_t(item->job_item_idx));
            if (html)
            {
                out << "<br/>";
            }
            else
            {
                out << "  ";
            }
            out << "item (" << enum_item_key(item->role) << "): ";
            out << escape(AI::describe_item(item->item));
            if (item->is_fetching)
            {
                out << " (fetching)";
            }
            if (!html)
            {
                out << "\n";
            }
        }
        for (size_t i = 0; i < j->item->job_items.size(); i++)
        {
            if (handled_items.count(i))
            {
                continue;
            }
            auto & item = j->item->job_items.at(i);
            if (html)
            {
                out << "<br/>";
            }
            else
            {
                out << "  ";
            }
            out << "item (not yet selected): ";
            MaterialInfo mat(item);
            ItemTypeInfo typ(item);
            if (mat.isValid())
            {
                out << escape(mat.toString()) << " ";
            }
            if (typ.isValid())
            {
                out << escape(typ.toString()) << " ";
            }
            if (!item->has_material_reaction_product.empty())
            {
                out << "(has product: " << escape(item->has_material_reaction_product) << ") ";
            }
            if (item->has_tool_use != tool_uses::NONE)
            {
                out << "(has tool use: " << enum_item_key(item->has_tool_use) << ") ";
            }
            if (auto ore = df::inorganic_raw::find(item->metal_ore))
            {
                out << "(ore of " << escape(ore->material.state_name[matter_state::Solid]) << ") ";
            }
            std::vector<std::string> flags;
            bitfield_to_string(&flags, item->flags1);
            bitfield_to_string(&flags, item->flags2);
            bitfield_to_string(&flags, item->flags3);
            //bitfield_to_string(&flags, item->flags4);
            //bitfield_to_string(&flags, item->flags5);
            if (!flags.empty())
            {
                out << "(";
                bool first = true;
                for (auto & flag : flags)
                {
                    if (first)
                    {
                        first = false;
                    }
                    else
                    {
                        out << ", ";
                    }
                    out << flag;
                }
                out << ") ";
            }
            int32_t base_quantity;
            switch (item->item_type)
            {
                case item_type::BAR:
                case item_type::POWDER_MISC:
                case item_type::LIQUID_MISC:
                case item_type::DRINK:
                    base_quantity = 150;
                    break;
                case item_type::THREAD:
                    base_quantity = 15000;
                    break;
                case item_type::CLOTH:
                    base_quantity = 10000;
                    break;
                default:
                    base_quantity = 1;
                    break;
            }
            int32_t remainder = item->quantity % base_quantity;
            if (item->quantity / base_quantity != 1 || remainder != 0)
            {
                out << "(quantity: " << (item->quantity / base_quantity);
                if (remainder != 0)
                {
                    out << " and " << remainder << "/" << base_quantity;
                }
                out << ") ";
            }
            if (!html)
            {
                out << "\n";
            }
        }
        for (auto & ref : j->item->general_refs)
        {
            if (html)
            {
                out << "<br/>";
            }
            else
            {
                out << "  ";
            }
            switch (ref->getType())
            {
            case general_ref_type::UNIT_WORKER:
                out << "Worker: ";
                break;
            case general_ref_type::BUILDING_HOLDER:
                if (j->item->job_type == job_type::StoreItemInStockpile)
                {
                    out << "Stockpile: ";
                }
                else
                {
                    out << "Building: ";
                }
                break;
            case general_ref_type::BUILDING_USE_TARGET_1:
                if (ref->getBuilding()->getType() == building_type::Bed)
                {
                    out << "Bedroom: ";
                }
                else
                {
                    out << "Building: ";
                }
                break;
            default:
                out << toLower(enum_item_key(ref->getType())) << ": ";
                break;
            }
            if (auto item = ref->getItem())
            {
                out << escape(AI::describe_item(item));
            }
            if (auto unit = ref->getUnit())
            {
                out << AI::describe_unit(unit, html);
            }
            if (/*auto projectile =*/ ref->getProjectile())
            {
                out << "[not yet implemented]";
                // TODO: describe projectiles
            }
            if (auto building = ref->getBuilding())
            {
                room *r = nullptr;
                furniture *f = nullptr;
                if (ai->plan->find_building(building, r, f))
                {
                    if (f)
                    {
                        out << AI::describe_furniture(f, html);
                        out << " in ";
                    }
                    out << AI::describe_room(r, html);
                }
                else
                {
                    out << toLower(enum_item_key(building->getType()));
                }
            }
            if (/*auto entity =*/ ref->getEntity())
            {
                out << "[not yet implemented]";
                // TODO: describe entities
            }
            if (/*auto artifact =*/ ref->getArtifact())
            {
                out << "[not yet implemented]";
                // TODO: describe artifacts
            }
            if (/*auto nemesis =*/ ref->getNemesis())
            {
                out << "[not yet implemented]";
                // TODO: describe nemeses
            }
            if (/*auto event =*/ ref->getEvent())
            {
                out << "[not yet implemented]";
                // TODO: describe events
            }
            if (!html)
            {
                out << "\n";
            }
        }
        if (html)
        {
            out << "</li>";
        }
    }
    for (auto & boring : boring_job_count)
    {
        if (html)
        {
            out << "<li>" << html_escape(boring.first) << " &times;" << boring.second << "</li>";
        }
        else
        {
            out << "- " << boring.first << " x" << boring.second << "\n";
        }
    }

    if (html)
    {
        out << "</ul>";
    }
    else
    {
        out << "\n";
    }
}
