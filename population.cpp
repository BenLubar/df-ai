#include "ai.h"
#include "population.h"
#include "plan.h"
#include "thirdparty/weblegends/weblegends-plugin.h"

#include "modules/Job.h"
#include "modules/Materials.h"
#include "modules/Units.h"

#include "df/caste_raw.h"
#include "df/creature_raw.h"
#include "df/crime.h"
#include "df/general_ref_unit_infantst.h"
#include "df/general_ref_unit_workerst.h"
#include "df/historical_entity.h"
#include "df/historical_figure.h"
#include "df/history_event_hist_figure_diedst.h"
#include "df/incident.h"
#include "df/job.h"
#include "df/job_item.h"
#include "df/occupation.h"
#include "df/reaction.h"
#include "df/reaction_reagent.h"
#include "df/squad.h"
#include "df/squad_order_kill_listst.h"
#include "df/squad_position.h"
#include "df/syndrome.h"
#include "df/unit_health_info.h"
#include "df/unit_relationship_type.h"
#include "df/unit_wound.h"
#include "df/ui.h"
#include "df/world.h"
#include "df/wound_curse_info.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(standing_orders_forbid_used_ammo);
REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

Population::Population(AI & ai) :
    ai(ai),
    citizen(),
    military(),
    pet(),
    pet_check(),
    visitor(),
    resident(),
    military_min(25),
    military_max(75),
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
    onupdate_handle = events.onupdate_register("df-ai pop", 25, 10, [this](color_ostream & out) { update(out); });
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
        if (ai.eventsJson.is_open())
        {
            Json::Value payload(Json::objectValue);
            payload["citizen"] = Json::UInt(citizen.size());
            payload["military"] = Json::UInt(military.size());
            payload["pet"] = Json::UInt(pet.size());
            payload["visitor"] = Json::UInt(visitor.size());
            payload["resident"] = Json::UInt(resident.size());
            ai.event("population", payload);
        }
        break;
    }
}

void Population::new_citizen(color_ostream & out, int32_t id)
{
    citizen.insert(id);
    ai.plan.new_citizen(out, id);
}

void Population::del_citizen(color_ostream & out, int32_t id)
{
    citizen.erase(id);
    military.erase(id);
    ai.plan.del_citizen(out, id);
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

                if (ai.eventsJson.is_open())
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
                    ai.event("new citizen", payload);
                }
            }
        }
        else if (Units::isCitizen(u) && Units::isBaby(u))
        {
            auto mother = df::unit::find(u->relationship_ids[unit_relationship_type::Mother]);
            if (mother && Units::isAlive(mother) && Units::isSane(mother) && u->relationship_ids[unit_relationship_type::RiderMount] == -1 && mother->job.current_job == nullptr)
            {
                // http://www.bay12games.com/dwarves/mantisbt/view.php?id=5551
                ai.debug(out, "[DF Bug 5551] reuniting mother (" + AI::describe_unit(mother) + ") with infant (" + AI::describe_unit(u) + ")");
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
        else if (u->flags1.bits.inactive || u->flags1.bits.merchant || u->flags1.bits.diplomat || u->flags1.bits.forest || u->flags2.bits.slaughter)
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

        if (ai.eventsJson.is_open())
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
            ai.event("del citizen", payload);
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
    return stl_sprintf("%zu citizen, %zu military, %zu pet, %zu visitor, %zu resident", citizen.size(), military.size(), pet.size(), visitor.size(), resident.size());
}

void Population::report(std::ostream & out, bool html)
{
    // Single Unit lookup
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

        // Name & Profession
        out << "<b>" << AI::describe_unit(u, html) << "</b>";

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

        // Occupation
        if (resident.count(u->id))
        {
            for (auto occ : u->occupations)
            {
                if (occ->site_id == ui->site_id)
                {
                    switch (occ->type)
                    {
                    case occupation_type::TAVERN_KEEPER:
                        out << " (tavern keeper)";
                        break;
                    case occupation_type::PERFORMER:
                        out << " (performer)";
                        break;
                    case occupation_type::SCHOLAR:
                        out << " (scholar)";
                        break;
                    case occupation_type::MERCENARY:
                        out << " (mercenary)";
                        break;
                    case occupation_type::MONSTER_SLAYER:
                        out << " (monster slayer)";
                        break;
                    case occupation_type::SCRIBE:
                        out << " (scribe)";
                        break;
                    case occupation_type::MESSENGER:
                        out << " (messenger)";
                        break;
                    }
                }
            }
        }

        // Age
        int32_t age = days_since(u->birth_year, u->birth_time);
        out << " (age " << (age / 12 / 28) << "y" << ((age / 28) % 12) << "m" << (age % 28) << "d)";

        if (room *r = ai.find_room_at(Units::getPosition(u)))
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

        // Current Job
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

    // Citizens
    if (html)
    {
        out << "<h2 id=\"Population_Citizens\">Citizens</h2><ul>";
    }
    else
    {
        out << "## Citizens\n";
    }
    // Output all citizens
    for (auto u : citizen)
    {
        do_unit(u);
    }

    // Military
    if (html)
    {
        out << "</ul><h2 id=\"Population_Military\">Military</h2>";
    }
    else
    {
        out << "\n## Military\n";
    }

    // Output all Miltary Squads, their Members, and their target
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

        // Members
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

        // Target
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
            if (auto kill = virtual_cast<df::squad_order_kill_listst>(o))
            {
                for (auto unit_id : kill->units)
                {
                    auto target = df::unit::find(unit_id);
                    if (!target || Units::isDead(target))
                    {
                        continue;
                    }
                    out << (html ? "<li>" : "- ");
                    out << AI::describe_unit(target, html);
                    out << (html ? "</li>" : "\n");
                }
            }
            else
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

    // Pets
    if (html)
    {
        out << "<h2 id=\"Population_Pets\">Pets</h2><ul>";

        if (pet.empty())
        {
            out << "<li><i>(none)</i></li>";
        }
    }
    else
    {
        out << "\n## Pets\n";
    }
    // Output all pets
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
            out << maybe_escape(name, html);
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

    // Visitors
    if (html)
    {
        out << "</ul><h2 id=\"Population_Visitors\">Visitors</h2><ul>";

        if (visitor.empty())
        {
            out << "<li><i>(none)</i></li>";
        }
    }
    else
    {
        out << "\n## Visitors\n";
    }
    for (auto it : visitor)
    {
        do_unit(it);
    }

    // Residents
    if (html)
    {
        out << "</ul><h2 id=\"Population_Residents\">Residents</h2><ul>";

        if (resident.empty())
        {
            out << "<li><i>(none)</i></li>";
        }
    }
    else
    {
        out << "\n## Residents\n";
    }
    for (auto it : resident)
    {
        do_unit(it);
    }

    // Crimes
    if (html)
    {
        out << "</ul><h2 id=\"Population_Crimes\">Crimes</h2><ul>";
    }
    else
    {
        out << "\n## Crimes\n";
    }
    // Output all Crimes
    bool any_crimes = false;
    for (auto crime : world->crimes.all)
    {
        if (crime->site != ui->site_id)
        {
            continue;
        }

        any_crimes = true;

        auto convicted = df::unit::find(crime->convict_data.convicted);
        auto criminal = df::unit::find(crime->criminal);
        auto victim = df::unit::find(crime->victim_data.victim);

        out << (html ? "<li>" : "- ");
        out << "[" << AI::timestamp(crime->event_year, crime->event_time) << "] ";
        out << (html ? "<b>" : "");
        using crime_type = df::crime::T_mode;
        switch (crime->mode)
        {
        case crime_type::ProductionOrderViolation:
            out << AI::describe_unit(criminal, html) << " violated a production mandate set by " << AI::describe_unit(victim, html) << ".";
            break;
        case crime_type::ExportViolation:
            out << AI::describe_unit(criminal, html) << " violated an export ban set by " << AI::describe_unit(victim, html) << ".";
            break;
        case crime_type::JobOrderViolation:
            out << AI::describe_unit(criminal, html) << " violated a job order set by " << AI::describe_unit(victim, html) << ".";
            break;
        case crime_type::ConspiracyToSlowLabor:
            out << AI::describe_unit(criminal, html) << " committed conspiracy to slow labor";
            if (victim)
            {
                out << " against " << AI::describe_unit(victim, html);
            }
            out << ".";
            break;
        case crime_type::Murder:
            out << AI::describe_unit(criminal, html) << " murdered " << AI::describe_unit(victim, html) << ".";
            break;
        case crime_type::DisorderlyBehavior:
            out << AI::describe_unit(criminal, html) << " assaulted " << AI::describe_unit(victim, html) << ".";
            break;
        case crime_type::BuildingDestruction:
            out << AI::describe_unit(criminal, html) << " destroyed a building";
            if (victim)
            {
                out << " owned by " << AI::describe_unit(victim, html);
            }
            out << ".";
            break;
        case crime_type::Vandalism:
            out << AI::describe_unit(criminal, html) << " vandalized furniture";
            if (victim)
            {
                out << " owned by " << AI::describe_unit(victim, html);
            }
            out << ".";
            break;
        case crime_type::Theft:
            out << AI::describe_unit(criminal, html) << " stole an item from " << AI::describe_unit(victim, html) << ".";
            break;
        case crime_type::Robbery:
            out << AI::describe_unit(criminal, html) << " robbed " << AI::describe_unit(victim, html) << ".";
            break;
        case crime_type::BloodDrinking:
            out << AI::describe_unit(criminal, html) << " is a vampire who drank the blood of " << AI::describe_unit(victim, html) << ".";
            break;
        case crime_type::Embezzlement:
            out << AI::describe_unit(criminal, html) << " [FIXME:PLACEHOLDER:" << enum_item_key_str(crime->mode) << "] " << AI::describe_unit(victim, html);
            break;
        case crime_type::AttemptedMurder:
            out << AI::describe_unit(criminal, html) << " attempted to murder " << AI::describe_unit(victim, html) << ".";
            break;
        case crime_type::Kidnapping:
            out << AI::describe_unit(criminal, html) << " abducted " << AI::describe_unit(victim, html) << ".";
            break;
        case crime_type::AttemptedKidnapping:
            out << AI::describe_unit(criminal, html) << " attempted to abduct " << AI::describe_unit(victim, html) << ".";
            break;
        case crime_type::AttemptedTheft:
            out << AI::describe_unit(criminal, html) << " attempted to steal an item from " << AI::describe_unit(victim, html) << ".";
            break;
        case crime_type::Treason:
        case crime_type::Espionage:
        case crime_type::Bribery:
            out << AI::describe_unit(criminal, html) << " [FIXME:PLACEHOLDER:" << enum_item_key_str(crime->mode) << "] " << AI::describe_unit(victim, html);
            break;
        }

        out << (html ? "</b><br/>" : "\n  ");

        if (crime->flags.bits.discovered)
        {
            out << "Crime discovered: " << AI::timestamp(crime->discovered_year, crime->discovered_time);
        }
        else
        {
            out << (html ? "<i>Crime undiscovered</i>" : "Crime undiscovered");
        }

        if (convicted)
        {
            out << (html ? "<br/>" : "\n  ");
            out << "Convicted: " << AI::describe_unit(convicted, html);
            if (crime->flags.bits.sentenced)
            {
                if (crime->punishment.give_beating || crime->punishment.hammerstrikes || crime->punishment.prison_time)
                {
                    out << (html ? "<br/>" : "\n  ");
                    out << "Sentenced to";
                    if (crime->punishment.give_beating)
                    {
                        out << " a beating";
                    }
                    if (crime->punishment.hammerstrikes)
                    {
                        if (crime->punishment.give_beating)
                        {
                            if (crime->punishment.prison_time)
                            {
                                out << ", ";
                            }
                            else
                            {
                                out << " and";
                            }
                        }
                        out << " hammer strikes";
                    }
                    if (crime->punishment.prison_time)
                    {
                        if (crime->punishment.give_beating && crime->punishment.hammerstrikes)
                        {
                            out << ",";
                        }
                        if (crime->punishment.give_beating || crime->punishment.hammerstrikes)
                        {
                            out << " and";
                        }
                        out << " prison time";
                    }
                    out << ".";

                    if (html)
                    {
                        out << "<!--" << crime->punishment.give_beating << "," << crime->punishment.hammerstrikes << "," << crime->punishment.prison_time << "-->";
                    }
                }
            }
            else
            {
                out << (html ? "<br/>" : "\n  ");
                out << (html ? "<i>Awaiting sentencing</i>" : "Awaiting sentencing");
            }
        }
        else if (crime->flags.bits.needs_trial && crime->flags.bits.discovered)
        {
            out << (html ? "<br/>" : "\n  ");
            out << (html ? "<i>Awaiting trial</i>" : "Awaiting trial");
        }
        out << (html ? "</li>" : "\n");
    }

    if (html && !any_crimes)
    {
        out << "<li><i>(none)</i></li>";
    }

    // Health
    if (html)
    {
        out << "</ul><h2 id=\"Population_Health\">Health</h2><ul>";
    }
    else
    {
        out << "\n## Health\n";
    }
    auto is_interesting_wound = [](df::unit_wound *wound) -> bool
    {
        auto syn = df::syndrome::find(wound->syndrome_id);
        if (syn && syn->syn_name == "inebriation")
        {
            return false;
        }

        return (wound->flags.whole &~df::unit_wound::T_flags::mask_diagnosed) || syn || wound->dizziness || wound->fever || wound->nausea || wound->numbness || wound->pain || wound->paralysis || !wound->parts.empty();
    };
    bool any_interesting_wounds = false;
    for (auto u : world->units.active)
    {
        bool is_dead = Units::isDead(u);
        bool any_wounds = std::find_if(u->body.wounds.begin(), u->body.wounds.end(), is_interesting_wound) != u->body.wounds.end();

        if (!u || !u->health || (u->health->op_history.empty() && (!any_wounds || is_dead)))
        {
            continue;
        }

        // skip unimportant units like wild animals
        if (u->hist_figure_id == -1 && u->civ_id == -1)
        {
            continue;
        }

        auto race = df::creature_raw::find(u->race);
        auto caste = race ? race->caste.at(u->caste) : nullptr;
        if (!caste)
        {
            continue;
        }

        any_interesting_wounds = true;

        out << (html ? "<li><b>" : "- ");
        out << AI::describe_unit(u, html);
        out << (html ? "</b>" : "");
        if (is_dead)
        {
            out << " (deceased)";
        }
        else
        {
            if (u->health->flags.whole)
            {
                std::string before = html ? "<br/>" : "\n  ";
                before += "Needs healthcare: ";
                if (u->health->flags.bits.needs_recovery)
                {
                    out << before;
                    before = ", ";
                    out << "unable to walk to hospital";
                }
                if (u->job.current_job && u->job.current_job->job_type == job_type::Rest)
                {
                    out << before;
                    before = ", ";
                    out << "resting";
                }
                if (u->health->flags.bits.needs_healthcare)
                {
                    out << before;
                    before = ", ";
                    out << "waiting for doctor";
                }
                if (u->health->flags.bits.rq_cleaning)
                {
                    out << before;
                    before = ", ";
                    out << "needs cleaning";
                }
                if (u->health->flags.bits.rq_crutch)
                {
                    out << before;
                    before = ", ";
                    out << "needs crutch";
                }
                if (u->health->flags.bits.rq_diagnosis)
                {
                    out << before;
                    before = ", ";
                    out << "needs diagnosis";
                }
                if (u->health->flags.bits.rq_immobilize)
                {
                    out << before;
                    before = ", ";
                    out << "needs cast or splint";
                }
                if (u->health->flags.bits.rq_surgery)
                {
                    out << before;
                    before = ", ";
                    out << "needs surgery";
                }
                if (u->health->flags.bits.rq_suture)
                {
                    out << before;
                    before = ", ";
                    out << "needs sutures";
                }
                if (u->health->flags.bits.rq_traction)
                {
                    out << before;
                    before = ", ";
                    out << "needs immobilization in traction bench";
                }
            }
            for (size_t i = 0; i < u->health->body_part_flags.size(); i++)
            {
                auto & flags = u->health->body_part_flags.at(i);
                auto & status = u->body.components.body_part_status.at(i);
                if (!flags.whole && !(status.whole &~df::body_part_status::mask_grime))
                {
                    continue;
                }

                auto part = caste->body_info.body_parts.at(i);
                std::string before = html ? "<br/>" : "\n  ";
                before += *part->name_singular.at(0) + ": ";

                if (status.bits.missing)
                {
                    if (part->con_part_id == -1 || !u->body.components.body_part_status.at(part->con_part_id).bits.missing)
                    {
                        out << before << "missing";
                    }
                    continue;
                }
                if (flags.bits.inoperable_rot)
                {
                    out << before;
                    before = ", ";
                    out << "inoperable rot";
                }
                if (status.bits.has_bandage || (!flags.bits.rq_dressing && flags.bits.needs_bandage))
                {
                    out << before;
                    before = ", ";
                    out << "has bandage";
                }
                if (status.bits.has_splint)
                {
                    out << before;
                    before = ", ";
                    out << "splint applied";
                }
                else if (status.bits.has_plaster_cast)
                {
                    out << before;
                    before = ", ";
                    out << "plaster cast applied";
                }
                else if (!flags.bits.rq_immobilize && flags.bits.needs_cast)
                {
                    out << before;
                    before = ", ";
                    out << "cast or splint applied";
                }
                if (flags.bits.rq_cleaning)
                {
                    out << before;
                    before = ", ";
                    out << "needs cleaning";
                }
                if (flags.bits.rq_dressing)
                {
                    out << before;
                    before = ", ";
                    out << "needs dressing";
                }
                if (flags.bits.rq_immobilize)
                {
                    out << before;
                    before = ", ";
                    out << "needs splint or cast";
                }
                if (flags.bits.rq_setting)
                {
                    out << before;
                    before = ", ";
                    out << "bone needs setting";
                }
                if (flags.bits.rq_surgery)
                {
                    out << before;
                    before = ", ";
                    out << "needs surgery";
                }
                if (flags.bits.rq_suture)
                {
                    out << before;
                    before = ", ";
                    out << "needs sutures";
                }
                if (flags.bits.rq_traction)
                {
                    out << before;
                    before = ", ";
                    out << "needs immobilization in traction bench";
                }
                if (status.bits.on_fire)
                {
                    out << before;
                    before = ", ";
                    out << "on fire";
                }
                if (status.bits.organ_loss)
                {
                    out << before;
                    before = ", ";
                    out << "organ lost";
                }
                else if (status.bits.organ_damage)
                {
                    out << before;
                    before = ", ";
                    out << "organ damaged";
                }
                if (status.bits.muscle_loss)
                {
                    out << before;
                    before = ", ";
                    out << "muscle lost";
                }
                else if (status.bits.muscle_damage)
                {
                    out << before;
                    before = ", ";
                    out << "muscle damaged";
                }
                if (status.bits.bone_loss)
                {
                    out << before;
                    before = ", ";
                    out << "bone lost";
                }
                else if (status.bits.bone_damage)
                {
                    out << before;
                    before = ", ";
                    out << "bone damaged";
                }
                if (status.bits.skin_damage)
                {
                    out << before;
                    before = ", ";
                    out << "skin damaged";
                }
                if (status.bits.motor_nerve_severed)
                {
                    out << before;
                    before = ", ";
                    out << "motor nerve severed";
                }
                if (status.bits.sensory_nerve_severed)
                {
                    out << before;
                    before = ", ";
                    out << "sensory nerve severed";
                }
                if (status.bits.spilled_guts)
                {
                    out << before;
                    before = ", ";
                    out << "guts spilled";
                }
                if (status.bits.grime)
                {
                    out << before;
                    before = ", ";
                    switch (status.bits.grime)
                    {
                    case 1:
                        out << "a little dirty";
                        break;
                    case 2:
                        out << "somewhat dirty";
                        break;
                    case 3:
                        out << "dirty";
                        break;
                    case 4:
                        out << "very dirty";
                        break;
                    case 5:
                        out << "extremely dirty";
                        break;
                    case 6:
                        out << "grimy";
                        break;
                    case 7:
                        out << "filthy";
                        break;
                    }
                }
            }
            if (any_wounds)
            {
                out << (html ? "<br/>Wounds:<ul>" : "\n  Wounds:");
                for (auto wound : u->body.wounds)
                {
                    if (!is_interesting_wound(wound))
                    {
                        continue;
                    }

                    out << (html ? "<li>" : "\n  - ");
                    if (!wound->flags.bits.diagnosed)
                    {
                        out << "[undiagnosed] ";
                    }

                    out << (wound->flags.whole ? "[" : "");
                    out << bitfield_to_string(wound->flags, ",");
                    out << (wound->flags.whole ? "] " : "");
                    /* TODO:
                    wound->flags.bits.infection;
                    wound->flags.bits.mortal_wound;
                    wound->flags.bits.severed_part;
                    wound->flags.bits.stuck_weapon;
                    wound->flags.bits.sutured;
                    */

                    if (auto syndrome = df::syndrome::find(wound->syndrome_id))
                    {
                        out << syndrome->syn_name;
                    }

                    if (auto attacker = df::unit::find(wound->attacker_unit_id))
                    {
                        out << " (inflicted by " << AI::describe_unit(attacker, html) << ")";
                    }
                    else if (auto attacker = df::historical_figure::find(wound->attacker_hist_figure_id))
                    {
                        if (html)
                        {
                            out << " (inflicted by <a href=\"fig-" << attacker->id << "\">" << html_escape(AI::describe_name(attacker->name)) << "</a>)";
                        }
                        else
                        {
                            out << " (inflicted by " << AI::describe_name(attacker->name) << ")";
                        }
                    }

#define FIELD(base, name) \
                    if (base->name) \
                    { \
                        out << " [" #name ":" << (base->name) << "]"; \
                    }

                    int32_t age_year = *cur_year;
                    int32_t age_tick = *cur_year_tick;
                    age_tick -= wound->age;
                    while (age_tick < 0)
                    {
                        age_year--;
                        age_tick += 12 * 28 * 1200;
                    }
                    out << " [age:" << wound->age << ":" << AI::timestamp(age_year, age_tick) << "]";

                    FIELD(wound, dizziness);
                    FIELD(wound, fever);
                    FIELD(wound, nausea);
                    FIELD(wound, numbness);
                    FIELD(wound, pain);
                    FIELD(wound, paralysis);

                    /* TODO:
                    if (wound->curse)
                    {
                        int debug = 1;
                    }
                    */

                    if (!wound->parts.empty())
                    {
                        out << (html ? "<ul>" : "");
                        for (auto part : wound->parts)
                        {
                            out << (html ? "<li>" : "\n    - ");
                            auto bp = vector_get(caste->body_info.body_parts, static_cast<unsigned int>(part->body_part_id));
                            out << *bp->name_singular.at(0);

                            for (size_t i = 0; i < part->effect_type.size(); i++)
                            {
                                auto effect = part->effect_type.at(i);
                                auto perc1 = part->effect_perc1.at(i);
                                auto perc2 = part->effect_perc2.at(i);
                                out << "[effect:" << enum_item_key(effect) << ":" << perc1 << ":" << perc2 << "]";
                            }

                            out << (part->flags1.whole || part->flags2.whole ? " [" : "");
                            out << bitfield_to_string(part->flags1, ",");
                            out << (part->flags1.whole && part->flags2.whole ? "," : "");
                            out << bitfield_to_string(part->flags2, ",");
                            out << (part->flags1.whole || part->flags2.whole ? "]" : "");
                            /* TODO:
                            part->flags1.bits.cut;
                            part->flags1.bits.smashed;
                            part->flags1.bits.scar_cut; // straight scar
                            part->flags1.bits.scar_smashed; // huge dent
                            part->flags1.bits.tendon_bruised;
                            part->flags1.bits.tendon_strained;
                            part->flags1.bits.tendon_torn;
                            part->flags1.bits.ligament_bruised;
                            part->flags1.bits.ligament_sprained;
                            part->flags1.bits.ligament_torn;
                            part->flags1.bits.motor_nerve_severed;
                            part->flags1.bits.sensory_nerve_severed;
                            part->flags1.bits.edged_damage;
                            part->flags1.bits.smashed_apart; // ?
                            part->flags1.bits.major_artery;
                            part->flags1.bits.guts_spilled;
                            part->flags1.bits.edged_shake1;
                            part->flags1.bits.scar_edged_shake1; // jagged scar
                            part->flags1.bits.edged_shake2;
                            part->flags1.bits.broken;
                            part->flags1.bits.scar_broken; // huge dent
                            part->flags1.bits.gouged;
                            part->flags1.bits.blunt_shake1;
                            part->flags1.bits.scar_blunt_shake1; // jagged scar
                            part->flags1.bits.blunt_shake2;
                            part->flags1.bits.joint_bend1;
                            part->flags1.bits.scar_joint_bend1; // jagged scar
                            part->flags1.bits.joint_bend2;
                            part->flags1.bits.compound_fracture;
                            part->flags1.bits.diagnosed;
                            part->flags1.bits.artery;
                            part->flags1.bits.overlapping_fracture;
                            part->flags2.bits.needs_setting;
                            part->flags2.bits.entire_surface;
                            part->flags2.bits.gelded;
                            */

                            FIELD(part, bleeding);
                            FIELD(part, pain);
                            FIELD(part, nausea);
                            FIELD(part, dizziness);
                            FIELD(part, paralysis);
                            FIELD(part, numbness);
                            FIELD(part, swelling);
                            FIELD(part, impaired);

                            FIELD(part, cur_penetration_perc);
                            FIELD(part, max_penetration_perc);
                            FIELD(part, contact_area);
                            FIELD(part, edged_curve_perc);
                            FIELD(part, surface_perc);
#undef FIELD

                            out << (html ? "</li>" : "");
                        }
                        out << (html ? "</ul>" : "");
                    }
                }
                out << (html ? "</ul>" : "");
            }
        }
        if (!u->health->op_history.empty())
        {
            out << (html ? (any_wounds && !is_dead ? "Operation history:<ul>" : "<br/>Operation history:<ul>") : "\n  Operation history:");
            for (auto op : u->health->op_history)
            {
                out << (html ? "<li>" : "\n  - ");
                out << "[" << AI::timestamp(op->year, op->year_time) << "] ";
                out << AI::describe_unit(df::unit::find(op->doctor_id), html) << ": ";
                switch (op->job_type)
                {
                case job_type::RecoverWounded:
                {
                    room *r = nullptr;
                    furniture *f = nullptr;
                    if (ai.plan.find_building(df::building::find(op->info.bed_id), r, f))
                    {
                        out << "Brought to rest in " << AI::describe_furniture(f, html) << " in " << AI::describe_room(r, html) << ".";
                    }
                    else
                    {
                        out << "Brought to rest in bed.";
                    }
                    break;
                }
                case job_type::DiagnosePatient:
                {
                    out << "Diagnosed injuries.";
                    break;
                }
                case job_type::ImmobilizeBreak:
                {
                    auto part = vector_get(caste->body_info.body_parts, static_cast<unsigned int>(op->info.bandage.body_part_id));
                    out << "Immobilized broken " << (part ? *part->name_singular.at(0) : "") << " bone with ";
                    if (auto item = df::item::find(op->info.bandage.item_id))
                    {
                        auto desc = AI::describe_item(item);
                        out << maybe_escape(desc, html);
                    }
                    else
                    {
                        out << MaterialInfo(&op->info.bandage).toString() << " splint";
                    }
                    out << ".";
                    break;
                }
                case job_type::DressWound:
                {
                    auto part = vector_get(caste->body_info.body_parts, static_cast<unsigned int>(op->info.bandage.body_part_id));
                    out << "Dressed " << (part ? *part->name_singular.at(0) : "") << " wound with ";
                    if (auto item = df::item::find(op->info.bandage.item_id))
                    {
                        auto desc = AI::describe_item(item);
                        out << maybe_escape(desc, html);
                    }
                    else
                    {
                        out << MaterialInfo(&op->info.bandage).toString() << " cloth";
                    }
                    out << ".";
                    break;
                }
                case job_type::CleanPatient:
                {
                    auto part = vector_get(caste->body_info.body_parts, static_cast<unsigned int>(op->info.bandage.body_part_id));
                    out << "Cleaned";
                    if (part)
                    {
                        out << " " << *part->name_singular.at(0);
                    }
                    if (auto item = df::item::find(op->info.bandage.item_id))
                    {
                        auto desc = AI::describe_item(item);
                        out << " with " << maybe_escape(desc, html);
                    }
                    else
                    {
                        MaterialInfo soap(&op->info.bandage);
                        if (soap.material)
                        {
                            out << " with " << soap.toString();
                        }
                    }
                    out << ".";
                    break;
                }
                case job_type::Surgery:
                {
                    auto part = vector_get(caste->body_info.body_parts, static_cast<unsigned int>(op->info.bandage.body_part_id));
                    out << "Performed surgery";
                    if (part)
                    {
                        out << " on " << *part->name_singular.at(0);
                    }
                    out << ".";
                    break;
                }
                case job_type::Suture:
                {
                    auto part = vector_get(caste->body_info.body_parts, static_cast<unsigned int>(op->info.bandage.body_part_id));
                    out << "Applied ";
                    if (auto item = df::item::find(op->info.bandage.item_id))
                    {
                        auto desc = AI::describe_item(item);
                        out << maybe_escape(desc, html);
                    }
                    else
                    {
                        out << MaterialInfo(&op->info.bandage).toString();
                    }
                    out << " sutures";
                    if (part)
                    {
                        out << " to " << *part->name_singular.at(0);
                    }
                    out << ".";
                    break;
                }
                case job_type::SetBone:
                {
                    auto part = vector_get(caste->body_info.body_parts, static_cast<unsigned int>(op->info.bed_id));
                    out << "Set bone";
                    if (part)
                    {
                        out << " in " << *part->name_singular.at(0);
                    }
                    out << ".";
                    break;
                }
                case job_type::PlaceInTraction:
                {
                    room *r = nullptr;
                    furniture *f = nullptr;
                    if (ai.plan.find_building(df::building::find(op->info.bed_id), r, f))
                    {
                        out << "Immobilized in " << AI::describe_furniture(f, html) << " in " << AI::describe_room(r, html) << ".";
                    }
                    else
                    {
                        out << "Immobilized in traction bench.";
                    }
                    break;
                }
                case job_type::BringCrutch:
                {
                    out << "Brought ";
                    if (auto item = df::item::find(op->info.crutch.item_id))
                    {
                        auto desc = AI::describe_item(item);
                        out << maybe_escape(desc, html);
                    }
                    else
                    {
                        out << MaterialInfo(&op->info.crutch).toString() << " " << ItemTypeInfo(df::item_type(op->info.crutch.item_type), int16_t(op->info.crutch.item_subtype)).toString();
                    }
                    out << ".";
                    break;
                }
                case job_type::ApplyCast:
                {
                    auto part = vector_get(caste->body_info.body_parts, static_cast<unsigned int>(op->info.bandage.body_part_id));
                    out << "Applied ";
                    if (auto item = df::item::find(op->info.bandage.item_id))
                    {
                        auto desc = AI::describe_item(item);
                        out << maybe_escape(desc, html);
                    }
                    else
                    {
                        out << MaterialInfo(&op->info.bandage).toString() << " cast";
                    }
                    if (part)
                    {
                        out << " to " << *part->name_singular.at(0);
                    }
                    out << ".";
                    break;
                }
                default:
                {
                    out << enum_item_key(op->job_type);
                    break;
                }
                }
                if (html)
                {
                    out << "</li>";
                }
            }
            out << (html ? "</ul>" : "");
        }
        out << (html ? "</li>" : "\n");
    }

    if (html && !any_interesting_wounds)
    {
        out << "<li><i>(no injuries)</i></li>";
    }

    // Deaths
    if (html)
    {
        out << "</ul><h2 id=\"Population_Deaths\">Deaths</h2><ul>";
    }
    else
    {
        out << "\n## Deaths\n";
    }
    bool any_deaths = false;
    for (auto e : world->history.events_death)
    {
        auto d = virtual_cast<df::history_event_hist_figure_diedst>(e);

        if (!d || d->site != ui->site_id)
        {
            continue;
        }

        any_deaths = true;

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
    if (html && !any_deaths)
    {
        out << "<li><i>(no deaths&mdash;yet)</i></li>";
    }

    auto write_job = [&](df::job_list_link *j)
    {
        if (html)
        {
            out << "<li><b>" << html_escape(AI::describe_job(j->item)) << "</b>";
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
            out << "item";
            if (item->role == df::job_item_ref::T_role::Reagent)
            {
                auto ji = vector_get(j->item->job_items, item->job_item_idx);
                auto reaction = ji != nullptr ? df::reaction::find(ji->reaction_id) : nullptr;
                auto reagent = reaction != nullptr ? vector_get(reaction->reagents, ji->reagent_index) : nullptr;
                if (reagent)
                {
                    out << " (" << reagent->code << ")";
                }
                else
                {
                    out << " (" << enum_item_key(item->role) << ")";
                }
            }
            else if (item->role != 0)
            {
                out << " (" << enum_item_key(item->role) << ")";
            }
            out << ": " << maybe_escape(AI::describe_item(item->item), html);
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
                out << maybe_escape(mat.toString(), html) << " ";
            }
            if (typ.isValid())
            {
                out << maybe_escape(typ.toString(), html) << " ";
            }
            if (!item->has_material_reaction_product.empty())
            {
                out << "(has product: " << maybe_escape(item->has_material_reaction_product, html) << ") ";
            }
            if (item->has_tool_use != tool_uses::NONE)
            {
                out << "(has tool use: " << enum_item_key(item->has_tool_use) << ") ";
            }
            if (auto ore = df::inorganic_raw::find(item->metal_ore))
            {
                out << "(ore of " << maybe_escape(ore->material.state_name[matter_state::Solid], html) << ") ";
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
                out << maybe_escape(AI::describe_item(item), html);
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
                if (ai.plan.find_building(building, r, f))
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
    };

    // Jobs - Active
    if (html)
    {
        out << "</ul><h2 id=\"Population_Jobs\">Jobs</h2><h3 id=\"Population_Jobs_Active\">Active</h3><ul>";
    }
    else
    {
        out << "\n## Jobs\n### Active\n";
    }

    for (auto j = world->jobs.list.next; j; j = j->next)
    {
        if (Job::getWorker(j->item))
        {
            write_job(j);
        }
    }

    // Jobs - Waiting
    if (html)
    {
        out << "</ul><h3 id=\"Population_Jobs_Waiting\">Waiting</h3><ul>";
    }
    else
    {
        out << "\n### Waiting\n";
    }

    std::map<std::string, size_t> boring_job_count;
    for (auto j = world->jobs.list.next; j; j = j->next)
    {
        if (j->item->items.empty() && j->item->job_items.empty() && j->item->general_refs.empty())
        {
            boring_job_count[AI::describe_job(j->item)]++;
            continue;
        }

        if (!Job::getWorker(j->item))
        {
            write_job(j);
        }
    }

    for (auto & boring : boring_job_count)
    {
        if (html)
        {
            out << "<li><b>" << html_escape(boring.first) << "</b> &times;" << boring.second << "</li>";
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
