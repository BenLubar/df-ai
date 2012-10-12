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

        attr_accessor :ai, :citizen, :military
        attr_accessor :labor_worker, :worker_labor
        attr_accessor :onupdate_handle
        def initialize(ai)
            @ai = ai
            @citizen = {}
            @military = {}
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
            when 4; update_military
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
            @military.delete id
            ai.plan.del_citizen(id)
        end

        def update_citizenlist
            old = @citizen.dup

            # add new fort citizen to our list
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

        def update_military
            # check for new soldiers, allocate barracks
            newsoldiers = []

            df.unit_citizens.each { |u|
                if u.military.squad_index == -1
                    if @military.delete u.id
                        ai.plan.freesoldierbarrack(u.id)
                    end
                else
                    if not @military[u.id]
                        @military[u.id] = u.military.squad_index
                        newsoldiers << u.id
                    end
                end
            }

            # enlist new soldiers if needed
            maydraft = df.unit_citizens.reject { |u| 
                u.profession == :CHILD or
                u.profession == :BABY or
                u.mood != :None
            }
            while @military.length < maydraft.length/5
                ns = military_find_new_soldier(maydraft)
                break if not ns
                @military[ns.id] = ns.military.squad_index
                newsoldiers << ns.id
            end

            newsoldiers.each { |uid|
                ai.plan.getsoldierbarrack(uid)
            }
        end

        # returns an unit newly assigned to a military squad
        def military_find_new_soldier(unitlist)
            ns = unitlist.find_all { |u|
                u.military.squad_index == -1
            }.sort_by { |u|
                unit_totalxp(u)
            }.first
            return if not ns

            squad_id = military_find_free_squad
            return if not squad_id
            squad = df.world.squads.all.binsearch(squad_id)
            pos = squad.positions.index { |p| p.occupant == -1 }
            return if not pos

            squad.positions[pos].occupant = ns.hist_figure_id
            ns.military.squad_index = squad_id
            ns.military.squad_position = pos

            ns
        end

        # return a squad index with an empty slot
        def military_find_free_squad
            if not squad_id = df.ui.main.fortress_entity.squads.find { |sqid| @military.count { |k, v| v == sqid } < 8 }

                # create a new squad from scratch
                # XXX only inferred from looking at data in memory
                squad_id = df.squad_next_id
                df.squad_next_id = squad_id+1

                squad = DFHack::Squad.cpp_new :id => squad_id

                squad.name.first_name = "AI squad #{squad_id}"
                squad.name.unknown = -1
                squad.name.has_name = true

                squad.cur_alert_idx = 0

                10.times {
                    pos = DFHack::SquadPosition.cpp_new
                    %w[BODY HEAD PANTS GLOVES SHOES SHIELD WEAPON].each { |t|
                        idx = t.capitalize.to_sym
                        pos.uniform[idx] << DFHack::SquadUniformSpec.cpp_new(:color => -1,
                                :item_filter => { :item_type => t.to_sym, :material_class => :Metal2,
                                    :mattype => -1, :matindex => -1 })
                    }
                    pos.uniform[:Weapon][0].indiv_choice.melee = true
                    pos.flags.exact_matches = true
                    pos.unk_118 = pos.unk_11c = -1
                    squad.positions << pos
                }

                df.ui.alerts.list.each {
                    squad.schedule << DFHack.malloc(DFHack::SquadScheduleEntry._sizeof*12)
                    12.times { |i|
                        scm = squad.schedule.last[i]
                        scm._cpp_init
                        10.times { scm.order_assignments << -1 }
                        # TODO actual schedule (train, patrol, ...)
                    }
                }
                
                squad.uniform_priority = 2
                squad.carry_food = 2
                squad.carry_water = 2

                df.world.squads.all << squad
                df.ui.squads.list << squad
                df.ui.main.fortress_entity.squads << squad.id
            end
            squad_id
        end


        LaborList = DFHack::UnitLabor::ENUM.sort.transpose[1] - [:NONE]
        LaborTool = { :MINE => true, :CUTWOOD => true, :HUNT => true }

        LaborMin = Hash.new(2).update :DETAIL => 4, :PLANT => 4
        LaborMax = Hash.new(8).update :FISH => 0
        LaborMinPct = Hash.new(10).update :DETAIL => 20, :PLANT => 30
        LaborMaxPct = Hash.new(00).update :DETAIL => 40, :PLANT => 60

        LaborList.each { |lb|
            if lb.to_s =~ /HAUL/
                LaborMinPct[lb] = 40
                LaborMaxPct[lb] = 80
            end
        }

        def autolabors
            workers = []
            nonworkers = []
            citizen.each_value { |c|
                next if not u = c.dfunit
                if df.unit_isworker(u) and
                        !unit_hasmilitaryduty(u) and
                        (!u.job.current_job or u.job.current_job.job_type != :AttendParty) and
                        not u.status.misc_traits.find { |mt| mt.id == :OnBreak } and
                        not u.specific_refs.find { |sr| sr.type == :ACTIVITY }
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
            @labor_worker = LaborList.inject({}) { |h, lb| h.update lb => [] }
            @worker_labor = workers.inject({}) { |h, c| h.update c.id => [] }
            workers.each { |c|
                ul = c.dfunit.status.labors
                LaborList.each { |lb|
                    if ul[lb]
                        @labor_worker[lb] << c.id
                        @worker_labor[c.id] << lb
                    end
                }
            }

            # if one has too many labors, free him up (one per round)
            if cid = @worker_labor.keys.find { |id|
                lim = 4*LaborList.length/workers.length
                lim = 4 if lim < 4
                @worker_labor[id].length > lim
            }
                c = citizen[cid]
                u = c.dfunit
                ul = u.status.labors

                LaborList.each { |lb|
                    if ul[lb]
                        @worker_labor[c.id].delete lb
                        @labor_worker[lb].delete c.id
                        ul[lb] = false
                        u.military.pickup_flags.update = true if LaborTool[lb]
                    end
                }
            end

            labormin = LaborMin
            labormax = LaborMax
            laborminpct = LaborMinPct
            labormaxpct = LaborMaxPct

            # handle low-number of workers + tool labors
            mintool = LaborTool.keys.inject(0) { |s, lb| 
                min = labormin[lb]
                minpc = laborminpct[lb] * workers.length / 100
                min = minpc if minpc > min
                s + min
            }
            if workers.length < mintool
                labormax = labormax.dup
                LaborTool.each_key { |lb| labormax[lb] = 0 }
                case workers.length
                when 0
                    # meh
                when 1
                    # switch mine or cutwood based on time (1/2 dwarf month each)
                    if (df.cur_year_tick / (1200*28/2)) % 2 == 0
                        labormax[:MINE] = 1
                    else
                        labormax[:CUTWOOD] = 1
                    end
                else
                    mintool.times { |i|
                        # divide equally between labors, with priority
                        # to mine, then wood, then hunt
                        # XXX new labortools ?
                        lb = [:MINE, :CUTWOOD, :HUNT][i%3]
                        labormax[lb] += 1
                    }
                end
            end

            # autolabor!
            # TODO use skill ranking for decisions
            # TODO handle nobility
            LaborList.each { |lb|
                min = labormin[lb]
                max = labormax[lb]
                minpc = laborminpct[lb] * workers.length / 100
                maxpc = labormaxpct[lb] * workers.length / 100
                min = minpc if minpc > min
                max = maxpc if maxpc > max
                min = max if min > max
                min = workers.length if min > workers.length

                cnt = @labor_worker[lb].length
                if cnt > max
                    (cnt-max).times {
                        cid = @labor_worker[lb].delete_at(rand(@labor_worker[lb].length))
                        @worker_labor[cid].delete lb
                        u = citizen[cid].dfunit
                        u.status.labors[lb] = false
                        u.military.pickup_flags.update = true if LaborTool[lb]
                    }

                elsif cnt < min
                    (min-cnt).times {
                        c = workers.sort_by { |_c|
                            [@worker_labor[_c.id].length, rand]
                        }.find { |_c|
                            next if LaborTool[lb] and @worker_labor[_c.id].find { |_lb| LaborTool[_lb] }
                            not @worker_labor[_c.id].include? lb
                        } || workers.find { |_c| not @worker_labor[_c.id].include? lb }

                        @labor_worker[lb] << c.id
                        @worker_labor[c.id] << lb
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

        def unit_hasmilitaryduty(u)
            return if u.military.squad_index == -1
            squad = df.world.squads.all.binsearch(u.military.squad_index)
            curmonth = squad.schedule[squad.cur_alert_idx][df.cur_year_tick / (1200*28)]
            !curmonth.orders.empty?
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
                tg = df.unit_citizens.sort_by { |u| unit_totalxp(u) }.find { |u| u.profession != :CHILD and u.profession != :BABY }
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
