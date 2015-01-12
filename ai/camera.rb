class DwarfAI
    class Camera
        attr_accessor :ai
        attr_accessor :onupdate_handle
        def initialize(ai)
            @ai = ai
            @following = nil
            @following_prev = []
        end

        def startup
        end

        def onupdate_register
            @onupdate_handle = df.onupdate_register('df-ai camera', 1000, 100) { update }
            unless $DEBUG
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
            ai.timeout_sameview(60) do
                df.curview.breakdown_level = :QUIT
            end unless $NO_QUIT
            df.onupdate_unregister(@onupdate_handle)
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
                u.flags1.marauder or u.flags1.active_invader or u.flags2.visitor_uninvited
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
                u.flags1.dead
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
            if @following and df.ui.follow_unit == @following
                "following #{df.unit_find(@following).name} (previously: #{@following_prev.map{ |u| df.unit_find(u).name }.join(', ')})"
            else
                "inactive (previously: #{@following_prev.map{ |u| df.unit_find(u).name }.join(', ')})"
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

# vim: et:sw=4:ts=4
