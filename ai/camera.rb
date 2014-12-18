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
            @following = df.unit_citizens.shuffle.first

            if @following && !df.pause_state
                df.center_viewscreen @following
                df.ui.follow_unit = @following.id
            end
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
