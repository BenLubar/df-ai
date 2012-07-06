class DwarfAI
    class Plan
        attr_accessor :ai
        attr_accessor :bedroom, :well, :diningroom, :workshop
        def initialize(ai)
            @ai = ai
            @update_counter = 0
            @bedroom = {}
            @well = nil
            @diningroom = nil
            @workshop = {}
        end

        def update
        end

        def new_citizen(c)
        end

        def del_citizen(c)
        end
    end
end
