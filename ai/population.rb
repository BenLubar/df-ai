class DwarfAI
    class Population
        class Citizen
            attr_accessor :id, :role, :idlecounter
            def initialize(id)
                @id = id
                @idlecounter = 0
            end

            def dfunit
                df.unit_find(@id)
            end
        end

        attr_accessor :ai, :citizen
        attr_accessor :onupdate_handle
        def initialize(ai)
            @ai = ai
            @citizen = {}
            @update_counter = 0
        end

        def startup
            df.standing_orders_forbid_used_ammo = 0
        end

        def onupdate_register
            @onupdate_handle = df.onupdate_register(360, 10) { update }
        end

        def onupdate_unregister
            df.onupdate_unregister(@onupdate_handle)
        end

        def update
            @update_counter += 1
            case @update_counter % 10
            when 1; update_citizenlist
            when 2; update_nobles
            when 3; update_jobs
            end
        end

        def new_citizen(id)
            c = Citizen.new(id)
            @citizen[id] = c
            ai.plan.new_citizen(id)
            c
        end

        def del_citizen(id)
            @citizen.delete id
            ai.plan.del_citizen(id)
        end

        def update_citizenlist
            old = @citizen.dup

            # add new fort citizen to our list
            # TODO check berserk ?
            df.unit_citizens.each { |u|
                @citizen[u.id] ||= new_citizen(u.id)
                old.delete u.id
            }

            # del those who are no longer here
            old.each { |id, c|
                del_citizen(id)
            }
        end

        def update_jobs
            df.world.job_list.each { |j|
                j.flags.suspend = false if j.flags.suspend and not j.flags.repeat
            }
            autolabors
        end


        LaborList = DFHack::UnitLabor::ENUM.sort.transpose[1] - [:NONE]
        LaborTool = { :MINE => true, :CUTWOOD => true, :HUNT => true }
        LaborMin = {
            :FISH => 0,
        }
        LaborMax = {
            :FISH => 0,
        }

        def autolabors
            workers = []
            nonworkers = []
            citizen.each_value { |c|
                next if not u = c.dfunit
                if df.unit_maywork(u)
                    workers << c
                else
                    nonworkers << c
                end
            }

            # free non-workers
            nonworkers.each { |c|
                u = c.dfunit
                ul = u.status.labors
                LaborList.each { |lb|
                    if ul[lb]
                        # disable everything (may wait meeting)
                        ul[lb] = false
                        # free pick/axe/crossbow  XXX does it work?
                        u.military.pickup_flags.update = true if LaborTool[lb]
                    end
                }
            }

            # count active labors
            labor_worker = LaborList.inject({}) { |h, lb| h.update lb => [] }
            worker_labor = workers.inject({}) { |h, c| h.update c.id => [] }
            workers.each { |c|
                ul = c.dfunit.status.labors
                LaborList.each { |lb|
                    if ul[lb]
                        labor_worker[lb] << c
                        worker_labor[c.id] << lb
                    end
                }
            }

            # autolabor!
            # TODO use skill ranking for decisions
            # TODO rebalance when 7 dwarves all fulfill the labors requirements and 40 are idling around
            # TODO handle team = 1 dwarf (mine/cuttree), and other LaborTool conflicts
            # TODO handle nobility
            LaborList.each { |lb|
                min = LaborMin[lb] || 2
                max = LaborMax[lb] || 8
                min = max if min > max
                min = workers.length if min > workers.length

                cnt = labor_worker[lb].length
                if cnt > max
                    (cnt-max).times {
                        c = labor_worker[lb].delete_at(rand(labor_worker[lb].length))
                        worker_labor[c.id].delete lb
                        u = c.dfunit
                        u.status.labors[lb] = false
                        u.military.pickup_flags.update = true if LaborTool[lb]
                    }

                elsif cnt < min
                    (min-cnt).times {
                        c = workers.sort_by {
                            [worker_labor[c.id].to_a.length, rand]
                        }.find { |_c|
                            not worker_labor[_c.id].include? lb
                        }

                        labor_worker[lb] << c
                        worker_labor[c.id] << lb
                        u = c.dfunit
                        if LaborTool[lb]
                            LaborTool.keys.each { |_lb| u.status.labors[_lb] = false }
                            u.military.pickup_flags.update = true
                        end
                        u.status.labors[lb] = true
                    }

                else
                    # TODO allocate more workers if needed, from job_list
                end
            }
        end

        def unit_totalxp(u)
            u.status.current_soul.skills.inject(0) { |t, sk|
                rat = DFHack::SkillRating.int(sk.rating)
                t + 400*rat + 100*rat*(rat+1)/2 + sk.experience
            }
        end

        def update_nobles
            ent = df.ui.main.fortress_entity
            if not ent.positions.assignments.find { |a| a.histfig != -1 and ent.positions.own.binsearch(a.position_id).responsibilities[:MANAGE_PRODUCTION] }
                # TODO do not hardcode position name, check population caps, ...
                assign = ent.positions.assignments.find { |a| ent.positions.own.binsearch(a.position_id).code == 'MANAGER' }
                # TODO find a better candidate
                tg = df.unit_citizens.sort_by { |u| unit_totalxp(u) }.first
                office = ai.plan.ensure_workshop(:ManagersOffice)
                ai.plan.set_owner(office, tg.id)

                pos = DFHack::HistfigEntityLinkPositionst.cpp_new(:link_strength => 100, :start_year => df.cur_year)
                pos.entity_id = ent.id
                pos.assignment_id = assign.id
                tg.hist_figure_tg.entity_links << pos
                assign.histfig = tg.hist_figure_id

                ent.assignments_by_type[:MANAGE_PRODUCTION] << assign

                df.add_announcement("AI: new manager: #{tg.name}", 7, false) { |ann| ann.pos = tg.pos }
            end

            if not ent.positions.assignments.find { |a| a.histfig != -1 and ent.positions.own.binsearch(a.position_id).responsibilities[:ACCOUNTING] }
                assign = ent.positions.assignments.find { |a| ent.positions.own.binsearch(a.position_id).code == 'BOOKKEEPER' }
                tg = df.unit_citizens.find { |c| df.unit_entitypositions(c).find { |p| p.responsibilities[:MANAGE_PRODUCTION] } }
                office = ai.plan.ensure_workshop(:BookkeepersOffice)
                ai.plan.set_owner(office, tg.id)

                pos = DFHack::HistfigEntityLinkPositionst.cpp_new(:link_strength => 100, :start_year => df.cur_year)
                pos.entity_id = ent.id
                pos.assignment_id = assign.id
                tg.hist_figure_tg.entity_links << pos
                assign.histfig = tg.hist_figure_id

                ent.assignments_by_type[:ACCOUNTING] << assign

                df.ui.bookkeeper_settings = 4
                df.add_announcement("AI: new bookkeeper: #{tg.name}", 7, false) { |ann| ann.pos = tg.pos }
            end
        end

        def status
            "#{@citizen.length} citizen"
        end
    end
end
