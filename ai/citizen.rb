class DwarfAI
    class Citizen
        attr_accessor :ai
        attr_accessor :id, :role, :idlecounter
        def initialize(ai, id)
            @ai = ai
            @id = id
            @idlecounter = 0
        end

        def detach
            # free rooms etc
        end
    end
end

