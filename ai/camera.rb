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
        end

        def onupdate_unregister
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
