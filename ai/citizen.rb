class DwarfAI
    class Citizen
        attr_accessor :ai
        attr_accessor :id, :role, :idlecounter
        def initialize(ai, id)
            @ai = ai
            @id = id
            @idlecounter = 0
            ai.plan.new_citizen(self)
        end

        def detach
            ai.plan.del_citizen(self)
        end
    end
end

