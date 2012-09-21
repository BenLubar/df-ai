class DwarfAI
    class Stocks
        attr_accessor :ai, :needed, :needed_perdwarf, :watch_stock, :count
        def initialize(ai)
            @ai = ai
            @needed = { :bin => 6, :barrel => 6, :bucket => 4, :bag => 4,
                :food => 20, :drink => 20, :soap => 5, :logs => 10 }
            @needed_perdwarf = { :food => 1, :drink => 2 }
            @watch_stock = { :roughgem => 4, :pigtails => 10, :thread => 10 }
        end

        def update
            count_stocks

            # dont buildup stocks too early in the game
            return if ai.plan.tasks.find { |t| t[0] == :wantdig and t[1].type == :bedroom }

            pop = ai.pop.citizen.length
            @needed.each { |what, amount|
                amount += pop * @needed_perdwarf[what].to_i
                if @count[what] < amount
                    queue_need(what, amount-@count[what])
                end
            }

            @watch_stock.each { |what, amount|
                if @count[what] > amount
                    queue_use(what, @count[what]-amount)
                end
            }
        end

        def count_stocks
            @count = {}

            (@needed.keys | @watch_stock.keys).each { |k|
                @count[k] = case k
                when :bin
                    df.world.items.other[:BIN].find_all { |i|
                        i.stockpile.id == -1 and !i.flags.trader and i.itemrefs.empty?
                    }.length
                when :barrel
                    df.world.items.other[:BARREL].find_all { |i|
                        i.stockpile.id == -1 and !i.flags.trader and i.itemrefs.empty?
                    }.length
                when :bag
                    df.world.items.other[:BOX].find_all { |i|
                        !i.flags.trader and df.decode_mat(i).plant and i.itemrefs.reject { |r|
                            r.kind_of?(DFHack::GeneralRefContainedInItemst)
                        }.empty?
                    }.length
                when :bucket
                    df.world.items.other[:BUCKET].find_all { |i|
                        !i.flags.trader and i.itemrefs.empty?
                    }.length
                when :food
                    df.world.items.other[:ANY_GOOD_FOOD].reject { |i|
                        i.flags.trader or
                        case i
                        when DFHack::ItemSeedsst, DFHack::ItemBoxst, DFHack::ItemFishRawst
                            true
                        end
                    }.inject(0) { |s, i| s + i.stack_size }
                when :drink
                    df.world.items.other[:DRINK].inject(0) { |s, i| s + (i.flags.trader ? 0 : i.stack_size) }
                when :soap
                    df.world.items.other[:BAR].find_all { |i|
                        # bars are stocked in bins, ignore that (infirmary stocks have BuildingHolder)
                        !i.flags.trader and mat = df.decode_mat(i) and mat.material and mat.material.id == 'SOAP' and i.itemrefs.reject { |r| r.kind_of?(DFHack::GeneralRefContainedInItemst) }.empty?
                    }.length
                when :logs
                    df.world.items.other[:WOOD].find_all { |i| !i.flags.trader and i.itemrefs.empty? }.length
                when :roughgem
                    df.world.items.other[:ROUGH].find_all { |i| !i.flags.trader and i.itemrefs.empty? }.length
                else
                    @needed[k] ? 1000000 : -1
                end
            }
        end

        def queue_need(what, amount)
            case what
            when :soap
                return if ai.plan.rooms.find { |r|
                    r.type == :infirmary and r.status != :finished
                }
            when :food
                # XXX fish/hunt/cook ?
                @last_warn_food ||= Time.now-610    # warn every 10mn
                if @last_warn_food < Time.now-600
                    puts "AI: need #{amount} more food"
                    @last_warn_food = Time.now
                end
                return
            when :drink
                amount = 5 if amount < 5
                amount /= 5     # accounts for brewer yield, but not for input stack size
            when :logs
                # XXX check reachability
                queued = 0
                df.each_tree(:any) { |t| queued += 1 if df.map_tile_at(t).designation.dig == :Default }
                amount -= queued
                # XXX cuttrees 'near entrance' ?
                df.cuttrees(:any, amount, true) if amount > 0
                return
            end

            amount = 30 if amount > 30

            reaction = DwarfAI::Plan::FurnitureOrder[what]
            ai.plan.find_manager_orders(reaction).each { |o| amount -= o.amount_total }
            return if amount <= 2

            puts "AI: stocks: queue #{amount} #{reaction}" if $DEBUG
            ai.plan.add_manager_order(reaction, amount)
        end

        def queue_use(what, amount)
        end

        def status
            @count.inspect
        end
    end
end
