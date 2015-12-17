#include "ai.h"

#include <sstream>
#include <ctime>

#include "modules/Gui.h"
#include "modules/Maps.h"
#include "modules/Translation.h"
#include "modules/Units.h"

#include "df/creature_interaction_effect_body_transformationst.h"
#include "df/interfacest.h"
#include "df/graphic.h"
#include "df/job.h"
#include "df/job_type.h"
#include "df/job_type_class.h"
#include "df/syndrome.h"
#include "df/ui.h"
#include "df/unit.h"
#include "df/unit_syndrome.h"
#include "df/viewscreen_dwarfmodest.h"
#include "df/world.h"

REQUIRE_GLOBAL(gps);
REQUIRE_GLOBAL(gview);
REQUIRE_GLOBAL(pause_state);
REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

static uint8_t should_show_fps()
{
    return strict_virtual_cast<df::viewscreen_dwarfmodest>(Gui::getCurViewscreen()) ? 1 : 0;
}

Camera::Camera(color_ostream & out, AI *parent) :
    ai(parent),
    following(-1),
    following_prev{-1, -1, -1},
    following_index(-1),
    update_after_ticks(0)
{
    gps->display_frames = should_show_fps();

    if (/* TODO: $RECORD_MOVIE */ true)
    {
        start_recording(out);
    }
}

Camera::~Camera()
{
    gps->display_frames = 0;
}

command_result Camera::status(color_ostream & out)
{
    if (following == -1)
    {
        out << "inactive";
    }
    else
    {
        out << "following ";
        df::unit *unit = df::unit::find(following);
        out << Translation::TranslateName(Units::getVisibleName(unit), false);
        out << ", ";
        out << Units::getProfessionName(unit);
    }

    if (following_index == -1)
    {
        out << "\n";
        return CR_OK;
    }

    out << " (previously: ";
    for (int i = following_index; i < 3; i++)
    {
        if (following_prev[i] == -1)
            continue;

        df::unit *unit = df::unit::find(following_prev[i]);
        out << Translation::TranslateName(Units::getVisibleName(unit), false);
        out << ", ";
        out << Units::getProfessionName(unit);
        if (i != following_index)
        {
            out << "; ";
        }
    }
    for (int i = 0; i < following_index; i++)
    {
        if (following_prev[i] == -1)
            continue;

        df::unit *unit = df::unit::find(following_prev[i]);
        out << Translation::TranslateName(Units::getVisibleName(unit), false);
        out << ", ";
        out << Units::getProfessionName(unit);
        out << "; ";
    }
    out << ")\n";

    return CR_OK;
}

command_result Camera::statechange(color_ostream & out, state_change_event event)
{
    switch (event)
    {
    case SC_VIEWSCREEN_CHANGED:
        gps->display_frames = should_show_fps();
        return CR_OK;
    default:
        return CR_OK;
    }
}

command_result Camera::update(color_ostream & out)
{
    update_after_ticks--;
    if (update_after_ticks > 0)
        return CR_OK;

    update_after_ticks = 100;

    if ((following == -1 && ui->follow_unit != -1) || (following != -1 && following != ui->follow_unit))
    {
        if (ui->follow_unit == -1) {
            following = -1;
        } else {
            following = ui->follow_unit;
        }
        return CR_OK;
    }

    std::vector<df::unit *> targets1;
    std::vector<df::unit *> targets2;
    for (auto unit : world->units.active)
    {
        if (unit->flags1.bits.dead || Maps::getTileDesignation(unit->pos)->bits.hidden)
        {
            continue;
        }

        if (unit->flags1.bits.marauder || unit->flags1.bits.active_invader || unit->flags2.bits.visitor_uninvited || !unit->status.attacker_ids.empty())
        {
            targets1.push_back(unit);
            continue;
        }
        bool found = false;
        for (auto syn : unit->syndromes.active)
        {
            for (auto ce : df::syndrome::find(syn->type)->ce)
            {
                if (virtual_cast<df::creature_interaction_effect_body_transformationst>(ce))
                {
                    targets1.push_back(unit);
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }
        if (found)
            continue;

        if (Units::isCitizen(unit))
        {
            targets2.push_back(unit);
            continue;
        }
    }
    std::shuffle(targets1.begin(), targets1.end(), ai->rng);
    std::shuffle(targets2.begin(), targets2.end(), ai->rng);
    std::sort(targets2.begin(), targets2.end(), compare_view_priority);

    if (following != -1)
    {
        following_index++;
        if (following_index > 3)
        {
            following_index = 0;
        }
        following_prev[following_index] = following;
    }

    df::unit *best_target = nullptr;

    std::size_t targets1_count = targets1.size();
    if (targets1_count > 2)
        targets1_count = 2;
    for (auto unit : targets1)
    {
        if (followed_previously(unit->id))
        {
            targets1_count--;
            if (targets1_count == 0)
                break;
        }
        else if (!best_target || best_target->flags1.bits.caged)
        {
            best_target = unit;
        }
    }

    for (auto unit : targets2)
    {
        if (!best_target || followed_previously(best_target->id) || best_target->flags1.bits.caged)
        {
            best_target = unit;
            if (!followed_previously(unit->id) && !unit->flags1.bits.caged)
                break;
        }
    }

    if (!best_target)
    {
        following = -1;
    }
    else
    {
        following = best_target->id;

        if (!*pause_state)
        {
            Gui::revealInDwarfmodeMap(best_target->pos, true);
            ui->follow_unit = following;
        }
    }

    world->status.flags.bits.combat = 0;
    world->status.flags.bits.hunting = 0;
    world->status.flags.bits.sparring = 0;

    return CR_OK;
}

void Camera::start_recording(color_ostream & out)
{
    if (gview->supermovie_on == 0)
    {
        gview->supermovie_on = 1;
        gview->currentblocksize = 0;
        gview->nextfilepos = 0;
        gview->supermovie_pos = 0;
        gview->supermovie_delayrate = 0;
        gview->first_movie_write = 1;

        std::ostringstream filename;
        filename << "data/movies/df-ai-" << std::time(nullptr) << ".cmv";
        gview->movie_file = filename.str();
    }
}

bool Camera::compare_view_priority(df::unit *a, df::unit *b)
{
    return view_priority(a) < view_priority(b);
}

int Camera::view_priority(df::unit *unit)
{
    if (!unit->job.current_job)
        return 0;

    switch (df::enum_traits<df::job_type>::attrs(unit->job.current_job->job_type).type)
    {
    case job_type_class::Misc:
        return -20;
    case job_type_class::Digging:
        return -50;
    case job_type_class::Building:
        return -20;
    case job_type_class::Hauling:
        return -30;
    case job_type_class::LifeSupport:
        return -10;
    case job_type_class::TidyUp:
        return -20;
    case job_type_class::Leisure:
        return -20;
    case job_type_class::Gathering:
        return -30;
    case job_type_class::Manufacture:
        return -10;
    case job_type_class::Improvement:
        return -10;
    case job_type_class::Crime:
        return -50;
    case job_type_class::LawEnforcement:
        return -30;
    case job_type_class::StrangeMood:
        return -20;
    case job_type_class::UnitHandling:
        return -30;
    case job_type_class::SiegeWeapon:
        return -50;
    case job_type_class::Medicine:
        return -50;
    }
    return 0;
}

bool Camera::followed_previously(int32_t id)
{
    return following_prev[0] == id ||
           following_prev[1] == id ||
           following_prev[2] == id;
}

// vim: et:sw=4:ts=4
