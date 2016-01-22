#include "ai.h"
#include "camera.h"

#include <sstream>

#include "modules/Gui.h"
#include "modules/Maps.h"
#include "modules/Screen.h"
#include "modules/Units.h"

#include "df/creature_interaction_effect_body_transformationst.h"
#include "df/graphic.h"
#include "df/interfacest.h"
#include "df/job.h"
#include "df/syndrome.h"
#include "df/ui.h"
#include "df/unit.h"
#include "df/unit_syndrome.h"
#include "df/viewscreen_dwarfmodest.h"
#include "df/viewscreen_movieplayerst.h"
#include "df/world.h"

REQUIRE_GLOBAL(gps);
REQUIRE_GLOBAL(gview);
REQUIRE_GLOBAL(pause_state);
REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

Camera::Camera(AI *ai) :
    ai(ai),
    onupdate_handle(nullptr),
    onstatechange_handle(nullptr),
    following(-1),
    following_prev()
{
}

Camera::~Camera()
{
    events.onupdate_unregister(onupdate_handle);
    events.onstatechange_unregister(onstatechange_handle);
}

command_result Camera::startup(color_ostream & out)
{
    return CR_OK;
}

command_result Camera::onupdate_register(color_ostream & out)
{
    gps->display_frames = 1;
    onupdate_handle = events.onupdate_register("df-ai camera", 1000, 100, [this](color_ostream & out) { update(out); });
    onstatechange_handle = events.onstatechange_register([this](color_ostream & out, state_change_event mode)
            {
                if (mode == SC_VIEWSCREEN_CHANGED)
                {
                    df::viewscreen *view = Gui::getCurViewscreen();
                    if (virtual_cast<df::viewscreen_movieplayerst>(view))
                    {
                        Screen::dismiss(view);
                        view = Gui::getCurViewscreen();
                        check_record_status();
                    }
                    gps->display_frames = virtual_cast<df::viewscreen_dwarfmodest>(view) ? 1 : 0;
                }
            });
    check_record_status();
    return CR_OK;
}

void Camera::check_record_status()
{
    if (RECORD_MOVIE && gview->supermovie_on == 0)
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

command_result Camera::onupdate_unregister(color_ostream & out)
{
    gps->display_frames = 0;
    if (!NO_QUIT && !AI_RANDOM_EMBARK)
    {
        ai->timeout_sameview(60, [](color_ostream & out)
                {
                    Gui::getCurViewscreen()->breakdown_level = interface_breakdown_types::QUIT;
                });
    }
    events.onupdate_unregister(onupdate_handle);
    events.onstatechange_unregister(onstatechange_handle);
    return CR_OK;
}

void Camera::update(color_ostream & out)
{
    if (following != ui->follow_unit)
    {
        following = ui->follow_unit;
        return;
    }

    std::vector<df::unit *> targets1;
    for (df::unit *u : world->units.active)
    {
        if (u->flags1.bits.dead || Maps::getTileDesignation(Units::getPosition(u))->bits.hidden)
            continue;
        if (u->flags1.bits.marauder ||
                u->flags1.bits.active_invader ||
                u->flags2.bits.visitor_uninvited ||
                !u->status.attacker_ids.empty() ||
                std::find_if(u->syndromes.active.begin(), u->syndromes.active.end(), [](df::unit_syndrome *us) -> bool
                    {
                        auto & s = df::syndrome::find(us->type)->ce;
                        return std::find_if(s.begin(), s.end(), [](df::creature_interaction_effect *ce) -> bool
                                {
                                    return virtual_cast<df::creature_interaction_effect_body_transformationst>(ce) != nullptr;
                                }) != s.end();
                    }) != u->syndromes.active.end())
        {
            targets1.push_back(u);
        }
    }
    std::shuffle(targets1.begin(), targets1.end(), ai->rng);
    std::vector<df::unit *> targets2;
    for (df::unit *u : world->units.active)
    {
        if (!u->flags1.bits.dead && Units::isCitizen(u))
        {
            targets2.push_back(u);
        }
    }
    std::shuffle(targets2.begin(), targets2.end(), ai->rng);
    auto score = [](df::unit *u) -> int
    {
        if (!u->job.current_job)
        {
            return 0;
        }
        switch (ENUM_ATTR(job_type, type, u->job.current_job->job_type))
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
    };
    std::sort(targets2.begin(), targets2.end(), [score](df::unit *a, df::unit *b) -> bool { return score(a) < score(b); });

    if (following != -1)
        following_prev.push_back(following);
    if (following_prev.size() > 3)
    {
        following_prev.erase(following_prev.begin(), following_prev.end() - 3);
    }

    size_t targets1_count = targets1.size();
    if (targets1_count > 3)
        targets1_count = 3;
    if (!targets2.empty())
    {
        for (df::unit *u : targets1)
        {
            if (std::find(following_prev.begin(), following_prev.end(), u->id) == following_prev.end())
            {
                targets1_count--;
                if (targets1_count == 0)
                    break;
            }
        }
    }

    targets1.erase(targets1.begin(), targets1.begin() + targets1_count);
    targets1.insert(targets1.end(), targets2.begin(), targets2.end());

    df::unit *following_unit = nullptr;
    following = -1;
    if (!targets1.empty())
    {
        for (df::unit *u : targets1)
        {
            if (std::find(following_prev.begin(), following_prev.end(), u->id) == following_prev.end() && !u->flags1.bits.caged)
            {
                following_unit = u;
                following = u->id;
                break;
            }
        }
        if (following == -1)
        {
            following_unit = targets1[std::uniform_int_distribution<size_t>(0, targets1.size() - 1)(ai->rng)];
            following = following_unit->id;
        }
    }

    if (following != -1 && !*pause_state)
    {
        Gui::revealInDwarfmodeMap(Units::getPosition(following_unit), true);
        ui->follow_unit = following;
    }

    world->status.flags.bits.combat = 0;
    world->status.flags.bits.hunting = 0;
    world->status.flags.bits.sparring = 0;
}

std::string Camera::status()
{
    std::string fp;
    for (int32_t id : following_prev)
    {
        if (!fp.empty())
        {
            fp += "; ";
        }
        fp += AI::describe_unit(df::unit::find(id));
    }
    if (!fp.empty())
    {
        fp = " (previously: " + fp + ")";
    }
    if (following != -1 && ui->follow_unit == following)
    {
        return "following " + AI::describe_unit(df::unit::find(following)) + fp;
    }
    return "inactive" + fp;
}

// vim: et:sw=4:ts=4
