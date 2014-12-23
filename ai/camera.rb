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
            df.gview.supermovie_on = 1
            df.gview.currentblocksize = 0
            df.gview.nextfilepos = 0
            df.gview.supermovie_pos = 0
            df.gview.supermovie_delayrate = 0
            df.gview.first_movie_write = 1
            df.gview.movie_file = "data/movies/df-ai-#{Time.now.to_i}.cmv"
        end

        def onupdate_unregister
            df.curview.breakdown_level = :QUIT
            df.onupdate_unregister(@onupdate_handle)
        end

        def update
            targets = df.unit_hostiles.shuffle.select do |u|
                u.flags1.active_invader or u.flags2.visitor_uninvited
            end + df.unit_citizens.shuffle.sort_by do |u|
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

            @following_prev << @following if @following and not @following_prev.include? @following
            if @following_prev.length > 3
                @following_prev = @following_prev[-3, 3]
            end

            if targets.empty?
                @following = nil
            else
                while @following_prev.include? targets[0] and targets.length > 1
                    targets = targets[1..-1]
                end
                @following = targets[0]
            end

            if @following && !df.pause_state
                df.center_viewscreen @following
                df.ui.follow_unit = @following.id
            end

            df.world.status.flags.combat = false
            df.world.status.flags.hunting = false
            df.world.status.flags.sparring = false
        end

        def status
            if @following && df.ui.follow_unit == @following.id
                "following #{@following.name} (previously: #{@following_prev.map(&:name).join(', ')})"
            else
                "inactive (previously: #{@following_prev.map(&:name).join(', ')})"
            end
        end
    end
end

# vim: et:sw=4:ts=4
