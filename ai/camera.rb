class DwarfAI
    class Camera
        attr_accessor :ai
        attr_accessor :onupdate_handle
        def initialize(ai)
            @ai = ai
            @following = nil
        end

        def startup
        end

        def onupdate_register
            @onupdate_handle = df.onupdate_register('df-ai camera', 1000, 100) { update }
            df.enabler.set_fps(df.gview.original_fps) if (df.gview.original_fps || 0) > 0
            df.gview.supermovie_on = 1
            df.gview.currentblocksize = 0
            df.gview.nextfilepos = 0
            df.gview.supermovie_pos = 0
            df.gview.supermovie_delayrate = 0
            df.gview.first_movie_write = 1
            df.gview.movie_file = "data/movies/df-ai-#{Time.now.to_i}.cmv"
        end

        def onupdate_unregister
            df.curview.breakdown_level = :BREAKDOWN_LEVEL_QUIT
            df.onupdate_unregister(@onupdate_handle)
        end

        def update
            targets = df.unit_citizens
            @following = targets[rand(targets.length)]

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
                "following #{@following.name}"
            else
                "inactive"
            end
        end
    end
end

# vim: et:sw=4:ts=4
