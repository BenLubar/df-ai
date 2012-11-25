class DwarfAI
    class Population
        class Citizen
            attr_accessor :id, :role, :idlecounter, :entitypos
            def initialize(id)
                @id = id
                @idlecounter = 0
                @entitypos = []
            end

            def dfunit
                df.unit_find(@id)
            end
        end

        attr_accessor :ai, :citizen, :military, :pet
        attr_accessor :labor_worker, :worker_labor
        attr_accessor :onupdate_handle
        def initialize(ai)
            @ai = ai
            @citizen = {}
            @military = {}
            @pet = {}
            @update_counter = 0
        end

        def startup
            df.standing_orders_forbid_used_ammo = 0
        end

        def onupdate_register
            @onupdate_handle = df.onupdate_register('df-ai pop', 360, 10) { update }
        end

        def onupdate_unregister
            df.onupdate_unregister(@onupdate_handle)
        end

        def update
            @update_counter += 1
            @onupdate_handle.description = "df-ai pop #{@update_counter % 10}"
            case @update_counter % 10
            when 1; update_citizenlist
            when 2; update_nobles
            when 3; update_jobs
            when 4; update_military
            when 5; update_pets
            when 6; update_deads
            end
            @onupdate_handle.description = "df-ai pop"

            i = 0
            bga = df.onupdate_register('df-ai pop autolabors', 3) {
                bga.description = "df-ai pop autolabors #{i}"
                autolabors(i)
                df.onupdate_unregister(bga) if i > 10
                i += 1
            } if @update_counter % 3 == 0
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
                @citizen[u.id].entitypos = df.unit_entitypositions(u)
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
        end

        def update_deads
            # TODO engrave slabs for ghosts
        end

        def update_military
            # check for new soldiers, allocate barracks
            newsoldiers = []

            df.unit_citizens.each { |u|
                if u.military.squad_id == -1
                    if @military.delete u.id
                        ai.plan.freesoldierbarrack(u.id)
                    end
                else
                    if not @military[u.id]
                        @military[u.id] = u.military.squad_id
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
                @military[ns.id] = ns.military.squad_id
                newsoldiers << ns.id
            end

            newsoldiers.each { |uid|
                ai.plan.getsoldierbarrack(uid)
            }

            df.ui.main.fortress_entity.squads_tg.each { |sq|
                soldier_count = sq.positions.find_all { |sp| sp.occupant != -1 }.length
                sq.schedule[1].each { |sc|
                    sc.orders.each { |so|
                        next unless so.order.kind_of?(DFHack::SquadOrderTrainst)
                        so.min_count = (soldier_count > 3 ? soldier_count-1 : soldier_count)
                    }
                }
            }
        end

        # returns an unit newly assigned to a military squad
        def military_find_new_soldier(unitlist)
            ns = unitlist.find_all { |u|
                u.military.squad_id == -1
            }.sort_by { |u|
                unit_totalxp(u) + 5000*df.unit_entitypositions(u).length
            }.first
            return if not ns

            squad_id = military_find_free_squad
            return if not squad_id
            squad = df.world.squads.all.binsearch(squad_id)
            pos = squad.positions.index { |p| p.occupant == -1 }
            return if not pos

            squad.positions[pos].occupant = ns.hist_figure_id
            ns.military.squad_id = squad_id
            ns.military.squad_position = pos
            
            ent = df.ui.main.fortress_entity
            if !ent.positions.assignments.find { |a| a.squad_id == squad_id }
                if ent.assignments_by_type[:MILITARY_STRATEGY].empty?
                    assign_new_noble('MILITIA_COMMANDER', ns).squad_id = squad_id
                else
                    assign_new_noble('MILITIA_CAPTAIN', ns).squad_id = squad_id
                end
            end

            ns
        end

        # return a squad index with an empty slot
        def military_find_free_squad
            squad_sz = 8
            squad_sz = 6 if @military.length < 3*6
            squad_sz = 4 if @military.length < 2*4

            if not squad_id = df.ui.main.fortress_entity.squads.find { |sqid| @military.count { |k, v| v == sqid } < squad_sz }

                # create a new squad from scratch
                squad_id = df.squad_next_id
                df.squad_next_id = squad_id+1

                squad = DFHack::Squad.cpp_new :id => squad_id

                squad.name.first_name = "AI squad #{squad_id}"
                squad.name.unknown = -1
                squad.name.has_name = true

                squad.cur_alert_idx = 1     # train
                squad.uniform_priority = 2
                squad.carry_food = 2
                squad.carry_water = 2

                item_type = { :Body => :ARMOR, :Head => :HELM, :Pants => :PANTS,
                        :Gloves => :GLOVES, :Shoes => :SHOES, :Shield => :SHIELD,
                        :Weapon => :WEAPON }
                # uniform
                10.times {
                    pos = DFHack::SquadPosition.cpp_new
                    [:Body, :Head, :Pants, :Gloves, :Shoes, :Shield, :Weapon].each { |t|
                        pos.uniform[t] << DFHack::SquadUniformSpec.cpp_new(:color => -1,
                                :item_filter => {:item_type => item_type[t], :material_class => :Metal2,
                                    :mattype => -1, :matindex => -1})
                    }
                    pos.uniform[:Weapon][0].indiv_choice.melee = true
                    pos.uniform[:Weapon][0].item_filter.material_class = :None
                    pos.flags.exact_matches = true
                    pos.unk_118 = pos.unk_11c = -1
                    squad.positions << pos
                }

                # schedule
                df.ui.alerts.list.each { |alert|
                    # squad.schedule.index = alerts.list.index (!= alert.id)
                    squad.schedule << DFHack.malloc(DFHack::SquadScheduleEntry._sizeof*12)
                    12.times { |i|
                        scm = squad.schedule.last[i]
                        scm._cpp_init
                        10.times { scm.order_assignments << -1 }

                        case squad.schedule.length  # currently definied alert + 1
                        when 2
                            # train for 2 month, free the 3rd
                            next if i % 3 == df.ui.main.fortress_entity.squads.length % 3
                            scm.orders << DFHack::SquadScheduleOrder.cpp_new(:min_count => 0,
                                    :positions => [false]*10, :order => DFHack::SquadOrderTrainst.cpp_new)
                        end
                    }
                }
                
                # link squad into world
                df.world.squads.all << squad
                df.ui.squads.list << squad
                df.ui.main.fortress_entity.squads << squad.id
            end

            squad_id
        end


        LaborList = DFHack::UnitLabor::ENUM.sort.transpose[1] - [:NONE]
        LaborTool = { :MINE => true, :CUTWOOD => true, :HUNT => true }
        LaborSkill = DFHack::JobSkill::Labor.invert

        LaborMin = Hash.new(2).update :DETAIL => 4, :PLANT => 4
        LaborMax = Hash.new(8).update :FISH => 0
        LaborMinPct = Hash.new(10).update :DETAIL => 20, :PLANT => 30, :FISH => 1
        LaborMaxPct = Hash.new(00).update :DETAIL => 40, :PLANT => 60, :FISH => 3
        LaborList.each { |lb|
            if lb.to_s =~ /HAUL/
                LaborMinPct[lb] = 40
                LaborMaxPct[lb] = 80
            end
        }

        def autolabors(step)
            case step
            when 2
                @workers = []
                nonworkers = []

                citizen.each_value { |c|
                    next if not u = c.dfunit
                    if u.mood == :None and
                            u.profession != :CHILD and
                            u.profession != :BABY and
                            !unit_hasmilitaryduty(u) and
                            (!u.job.current_job or u.job.current_job.job_type != :AttendParty) and
                            not u.status.misc_traits.find { |mt| mt.id == :OnBreak } and
                            not u.specific_refs.find { |sr| sr.type == :ACTIVITY }
                            # TODO filter nobles that will not work
                        @workers << c
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

            when 3
                # count active labors
                @labor_worker = LaborList.inject({}) { |h, lb| h.update lb => [] }
                @worker_labor = @workers.inject({}) { |h, c| h.update c.id => [] }
                @workers.each { |c|
                    ul = c.dfunit.status.labors
                    LaborList.each { |lb|
                        if ul[lb]
                            @labor_worker[lb] << c.id
                            @worker_labor[c.id] << lb
                        end
                    }
                }

                # if one has too many labors, free him up (one per round)
                lim = 4*LaborList.length/[@workers.length, 1].max
                lim = 4 if lim < 4
                if cid = @worker_labor.keys.find { |id| @worker_labor[id].length > lim }
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

            when 4
                labormin = LaborMin
                labormax = LaborMax
                laborminpct = LaborMinPct
                labormaxpct = LaborMaxPct

                # handle low-number of workers + tool labors
                mintool = LaborTool.keys.inject(0) { |s, lb| 
                    min = labormin[lb]
                    minpc = laborminpct[lb] * @workers.length / 100
                    min = minpc if minpc > min
                    s + min
                }
                if @workers.length < mintool
                    labormax = labormax.dup
                    LaborTool.each_key { |lb| labormax[lb] = 0 }
                    case @workers.length
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
                        @workers.length.times { |i|
                            # divide equally between labors, with priority
                            # to mine, then wood, then hunt
                            # XXX new labortools ?
                            lb = [:MINE, :CUTWOOD, :HUNT][i%3]
                            labormax[lb] += 1
                        }
                    end
                end

                # autolabor!
                LaborList.each { |lb|
                    min = labormin[lb]
                    max = labormax[lb]
                    minpc = laborminpct[lb] * @workers.length / 100
                    maxpc = labormaxpct[lb] * @workers.length / 100
                    min = minpc if minpc > min
                    max = maxpc if maxpc > max
                    min = max if min > max
                    min = @workers.length if min > @workers.length

                    cnt = @labor_worker[lb].length
                    if cnt > max
                        @labor_worker[lb] = @labor_worker[lb].sort_by { |_cid|
                            if sk = LaborSkill[lb]
                                if usk = df.unit_find(_cid).status.current_soul.skills.find { |_usk| _usk.id == sk }
                                    DFHack::SkillRating.int(usk.rating)
                                else 
                                    0
                                end
                            else
                                rand
                            end
                        }
                        (cnt-max).times {
                            cid = @labor_worker[lb].shift
                            @worker_labor[cid].delete lb
                            u = citizen[cid].dfunit
                            u.status.labors[lb] = false
                            u.military.pickup_flags.update = true if LaborTool[lb]
                        }

                    elsif cnt < min
                        min += 1 if min < max and not LaborTool[lb]
                        (min-cnt).times {
                            c = @workers.sort_by { |_c|
                                malus = @worker_labor[_c.id].length * 10
                                malus += _c.entitypos.length * 40
                                if sk = LaborSkill[lb] and usk = _c.dfunit.status.current_soul.skills.find { |_usk| _usk.id == sk }
                                    malus -= DFHack::SkillRating.int(usk.rating) * 4    # legendary => 15
                                end
                                [malus, rand]
                            }.find { |_c|
                                next if LaborTool[lb] and @worker_labor[_c.id].find { |_lb| LaborTool[_lb] }
                                not @worker_labor[_c.id].include? lb
                            } || @workers.find { |_c| not @worker_labor[_c.id].include? lb }

                            next if not c
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
        end

        def unit_hasmilitaryduty(u)
            return if u.military.squad_id == -1
            squad = df.world.squads.all.binsearch(u.military.squad_id)
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
            cz = df.unit_citizens.sort_by { |u| unit_totalxp(u) }.find_all { |u| u.profession != :BABY and u.profession != :CHILD }
            ent = df.ui.main.fortress_entity


            if ent.assignments_by_type[:MANAGE_PRODUCTION].empty? and tg = cz.find { |u|
                u.military.squad_id == -1 and !ent.positions.assignments.find { |a| a.histfig == u.hist_figure_id }
            } || cz.first
                # TODO do not hardcode position name, check population caps, ...
                assign_new_noble('MANAGER', tg)
                office = ai.plan.ensure_workshop(:ManagersOffice)
                ai.plan.set_owner(office, tg.id)
                df.add_announcement("AI: new manager: #{tg.name}", 7, false) { |ann| ann.pos = tg.pos }
            end


            if ent.assignments_by_type[:ACCOUNTING].empty? and tg = cz.find { |u|
                u.military.squad_id == -1 and !ent.positions.assignments.find { |a| a.histfig == u.hist_figure_id } and !u.status.labors[:MINE]
            }
                assign_new_noble('BOOKKEEPER', tg)
                df.ui.bookkeeper_settings = 4
                office = ai.plan.ensure_workshop(:BookkeepersOffice)
                ai.plan.set_owner(office, tg.id)
            end


            if ent.assignments_by_type[:HEALTH_MANAGEMENT].empty? and
                    hosp = ai.plan.find_room(:infirmary) and hosp.status != :plan and tg = cz.find { |u|
                u.military.squad_id == -1 and !ent.positions.assignments.find { |a| a.histfig == u.hist_figure_id }
            }
                assign_new_noble('CHIEF_MEDICAL_DWARF', tg)
            elsif ass = ent.assignments_by_type[:HEALTH_MANAGEMENT].first and hf = df.world.history.figures.binsearch(ass.histfig) and doc = df.unit_find(hf.unit_id)
                # doc => healthcare
                LaborList.each { |lb|
                    doc.status.labors[lb] = case lb
                    when :DIAGNOSE, :SURGERY, :BONE_SETTING, :SUTURING, :DRESSING_WOUNDS, :FEED_WATER_CIVILIANS
                        true
                    end
                }
            end


            if ent.assignments_by_type[:TRADE].empty? and tg = cz.find { |u|
                u.military.squad_id == -1 and !ent.positions.assignments.find { |a| a.histfig == u.hist_figure_id }
            }
                assign_new_noble('BROKER', tg)
            end
        end

        def assign_new_noble(pos_code, unit)
            ent = df.ui.main.fortress_entity

            pos = ent.positions.own.find { |p| p.code == pos_code }
            raise "no noble position #{pos_code}" if not pos

            if not assign = ent.positions.assignments.find { |a| a.position_id == pos.id and a.histfig == -1 }
                a_id = ent.positions.next_assignment_id
                ent.positions.next_assignment_id = a_id+1
                assign = DFHack::EntityPositionAssignment.cpp_new(:id => a_id, :position_id => pos.id)
                assign.flags.resize(ent.positions.assignments.first.flags.length/8)   # XXX
                assign.flags[0] = true  # XXX
                ent.positions.assignments << assign
            end

            poslink = DFHack::HistfigEntityLinkPositionst.cpp_new(:link_strength => 100, :start_year => df.cur_year)
            poslink.entity_id = df.ui.main.fortress_entity.id
            poslink.assignment_id = assign.id

            unit.hist_figure_tg.entity_links << poslink
            assign.histfig = unit.hist_figure_id
            
            pos.responsibilities.each_with_index { |r, k|
                ent.assignments_by_type[k] << assign if r
            }

            assign
        end
            
        def update_pets
            np = @pet.dup
            df.world.units.active.each { |u|
                next if u.civ_id != df.ui.civ_id
                next if u.race == df.ui.race_id
                next if u.flags1.dead or u.flags1.merchant or u.flags1.forest

                if @pet[u.id]
                    if @pet[u.id].include?(:MILKABLE)
                        # TODO check counters etc
                    end

                    np.delete u.id
                    next
                end

                @pet[u.id] = []

                cst = u.race_tg.caste[u.caste]

                if cst.flags[:MILKABLE]
                    @pet[u.id] << :MILKABLE
                end
                # TODO shear

                if cst.flags[:GRAZER]
                    @pet[u.id] << :GRAZER

                    if bld = @ai.plan.getpasture(u.id)
                        u.general_refs << DFHack::GeneralRefBuildingCivzoneAssignedst.cpp_new(:building_id => bld.id)
                        bld.assigned_creature << u.id
                        # TODO monitor grass levels
                    else
                        # TODO slaughter best candidate, keep this one
                        # also avoid killing named pets
                        u.flags2.slaughter = true
                    end
                end

                if cst.flags[:LAYS_EGGS]
                    # TODO
                end
            }

            np.each_key { |id|
                @ai.plan.freepasture(id)
                @pet.delete id
            }
        end

        def status
            "#{@citizen.length} citizen, #{@pet.length} pets"
        end
    end
end
