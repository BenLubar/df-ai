#include "ai.h"

#include <sstream>
#include <ctime>

#include "df/interfacest.h"
#include "df/graphic.h"

REQUIRE_GLOBAL(gps);
REQUIRE_GLOBAL(gview);

Camera::Camera(color_ostream & out, AI *parent) :
    ai(parent),
    following(-1),
    following_prev()
{
    gps->display_frames = 1;

    if (/* TODO: $RECORD_MOVIE */ true)
    {
        start_recording(out);
    }
}

Camera::~Camera()
{
    gps->display_frames = 0;
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

/*
class DwarfAI
    class Camera
        attr_accessor :ai
        attr_accessor :onupdate_handle
        attr_accessor :onstatechange_handle
        def initialize(ai)
            @ai = ai
            @following = nil
            @following_prev = []
        end

        def startup
        end

        def onupdate_register
            df.gps.display_frames = 1
            @onupdate_handle = df.onupdate_register('df-ai camera', 1000, 100) { update }
            @onstatechange_handle = df.onstatechange_register { |mode|
                if mode == :VIEWSCREEN_CHANGED
                    df.gps.display_frames = df.curview._raw_rtti_classname == 'viewscreen_dwarfmodest' ? 1 : 0
                end
            }
            if $RECORD_MOVIE and df.gview.supermovie_on == 0
                df.gview.supermovie_on = 1
                df.gview.currentblocksize = 0
                df.gview.nextfilepos = 0
                df.gview.supermovie_pos = 0
                df.gview.supermovie_delayrate = 0
                df.gview.first_movie_write = 1
                df.gview.movie_file = "data/movies/df-ai-#{Time.now.to_i}.cmv"
            end
        end

        def onupdate_unregister
            df.gps.display_frames = 0
            ai.timeout_sameview(60) { df.curview.breakdown_level = :QUIT } unless $NO_QUIT or $AI_RANDOM_EMBARK
            df.onupdate_unregister(@onupdate_handle)
            df.onstatechange_unregister(@onstatechange_handle)
        end

        def update
            if (not @following and df.ui.follow_unit != -1) or (@following and @following != df.ui.follow_unit)
                if df.ui.follow_unit == -1
                    @following = nil
                else
                    @following = df.ui.follow_unit
                end
                return
            end

            targets1 = df.world.units.active.find_all do |u|
                u.flags1.marauder or u.flags1.active_invader or u.flags2.visitor_uninvited or u.syndromes.active.any? { |us|
                    us.type_tg.ce.any? { |ce|
                        ce.kind_of?(DFHack::CreatureInteractionEffectBodyTransformationst)
                    }
                } or not u.status.attacker_ids.empty?
            end.shuffle
            targets2 = df.unit_citizens.shuffle.sort_by do |u|
                unless u.job.current_job
                    0
                else
                    case DFHack::JobType::Type[u.job.current_job.job_type]
                    when :Misc
                        -20
                    when :Digging
                        -50
                    when :Building
                        -20
                    when :Hauling
                        -30
                    when :LifeSupport
                        -10
                    when :TidyUp
                        -20
                    when :Leisure
                        -20
                    when :Gathering
                        -30
                    when :Manufacture
                        -10
                    when :Improvement
                        -10
                    when :Crime
                        -50
                    when :LawEnforcement
                        -30
                    when :StrangeMood
                        -20
                    when :UnitHandling
                        -30
                    when :SiegeWeapon
                        -50
                    when :Medicine
                        -50
                    else
                        0
                    end
                end
            end

            targets1.reject! do |u|
                u.flags1.dead or df.map_tile_at(u).designation.hidden
            end
            targets2.reject! do |u|
                u.flags1.dead
            end

            @following_prev << @following if @following
            if @following_prev.length > 3
                @following_prev = @following_prev[-3, 3]
            end

            targets1_count = [3, targets1.length].min
            unless targets2.empty?
                targets1.each do |u|
                    if @following_prev.include?(u.id)
                        targets1_count -= 1
                        break if targets1_count == 0
                    end
                end
            end

            targets = targets1[0, targets1_count] + targets2

            if targets.empty?
                @following = nil
            else
                while (@following_prev.include?(targets[0].id) or targets[0].flags1.caged) and targets.length > 1
                    targets = targets[1..-1]
                end
                @following = targets[0].id
            end

            if @following and not df.pause_state
                df.center_viewscreen(df.unit_find(@following))
                df.ui.follow_unit = @following
            end

            df.world.status.flags.combat = false
            df.world.status.flags.hunting = false
            df.world.status.flags.sparring = false
        end

        def status
            fp = @following_prev.map { |id|
                DwarfAI::describe_unit(df.unit_find(id))
            }.join('; ')
            if @following and df.ui.follow_unit == @following
                "following #{DwarfAI::describe_unit(df.unit_find(@following))} (previously: #{fp})"
            else
                "inactive (previously: #{fp})"
            end
        end

        def serialize
            {
                :following      => @following,
                :following_prev => @following_prev,
            }
        end
    end
end
*/

// vim: et:sw=4:ts=4
