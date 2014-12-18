class DwarfAI
    class Camera
        attr_accessor :ai
        attr_accessor :onupdate_handle
        def initialize(ai)
            @ai = ai
            @update_counter = 0
            @following = nil
        end

        def startup
        end

        def onupdate_register
            @onupdate_handle = df.onupdate_register('df-ai camera') { update }
        end

        def onupdate_unregister
            df.onupdate_unregister(@onupdate_handle)
        end

        def update
            if @update_counter % 1000 == 0
                @following = df.unit_citizens.shuffle.first
            end
            @update_counter += 1

            df.center_viewscreen @following if @following && !df.pause_state
        end

        def status
            "following #{@following} for #{(1000 - @update_counter % 1000) % 1000} more ticks"
        end
    end
end
