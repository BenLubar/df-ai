class DwarfAI
    class Stocks
        attr_accessor :ai, :needed, :count
        def initialize(ai)
            @ai = ai
            @needed = { :bin => 6, :barrel => 6, :bucket => 4, :bag => 4,
                :food => 30, :drink => 30, :soap => 5 }
        end

        def update
            count_stocks

            # dont buildup stocks too early in the game
            return if ai.plan.tasks.find { |t| t[0] == :wantdig and t[1].type == :bedroom }

            @needed.each { |k, v|
                if @count[k] < v
                    queue(k, v-@count[k])
                end
            }
        end

        def count_stocks
            @count = Hash.new(0)
            @needed.keys.each { |k|
                @count[k] = case k
                when :bin
                    df.world.items.other[:BIN].find_all { |i|
                        i.stockpile.id == -1 and i.itemrefs.empty?
                    }.length
                when :barrel
                    df.world.items.other[:BARREL].find_all { |i|
                        i.stockpile.id == -1 and i.itemrefs.empty?
                    }.length
                when :bag
                    df.world.items.other[:BOX].find_all { |i|
                        df.decode_mat(i).plant and i.itemrefs.reject { |r| r.kind_of?(DFHack::GeneralRefContainedInItemst) }.empty?
                    }.length
                when :bucket
                    df.world.items.other[:BUCKET].find_all { |i|
                        i.itemrefs.empty?
                    }.length
                when :food
                    df.world.items.other[:ANY_GOOD_FOOD].reject { |i|
                        case i
                        when DFHack::ItemSeedsst, DFHack::ItemBoxst, DFHack::ItemFishRawst
                            true
                        end
                    }.inject(0) { |s, i| s + i.stack_size }
                when :drink
                    df.world.items.other[:DRINK].inject(0) { |s, i| s + i.stack_size }
                when :soap
                    df.world.items.other[:BAR].find_all { |i|
                        mat = df.decode_mat(i) and mat.material and mat.material.id == 'SOAP' and i.itemrefs.reject { |r| r.kind_of?(DFHack::GeneralRefContainedInItemst) }.empty?
                    }.length
                else
                    1000000
                end
            }
        end

        def queue(what, amount)
            return if what == :food # XXX fish/hunt ?
            return if what == :soap and ai.plan.rooms.find { |r| r.type == :infirmary and r.status != :finished }

            reaction = DwarfAI::Plan::FurnitureOrder[what]
            ai.plan.find_manager_orders(reaction).each { |o| amount -= o.amount_total }
            return if amount <= 0

            puts "AI: stocks: queue #{amount} #{what}"
            ai.plan.add_manager_order(reaction, amount)
        end

        def status
            @count.inspect
        end
    end
end
