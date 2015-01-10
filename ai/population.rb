class DwarfAI
    class Population
        class Citizen
            attr_accessor :id
            def initialize(id)
                @id = id
            end

            def dfunit
                df.unit_find(@id)
            end

            def serialize
                id
            end
        end

        attr_accessor :ai, :citizen, :military, :pet
        attr_accessor :labor_worker, :worker_labor
        attr_accessor :onupdate_handle
        def initialize(ai)
            @ai = ai
            @citizen = {}
            @military = {}
            @idlers = []
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
            when 7; update_caged
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
                next if u.profession == :BABY
                @citizen[u.id] ||= new_citizen(u.id)
                old.delete u.id
            }

            # del those who are no longer here
            old.each { |id, c|
                # u.counters.death_tg.flags.discovered dead/missing
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

        def update_caged
            count = 0
            df.world.items.other[:CAGE].each do |cage|
                next if not cage.flags.on_ground
                cage.general_refs.each do |ref|
                    case ref
                    when DFHack::GeneralRefContainsItemst
                        next if ref.item_tg.flags.dump
                        count += 1
                        ref.item_tg.flags.dump = true

                    when DFHack::GeneralRefContainsUnitst
                        u = ref.unit_tg

                        if df.ui.main.fortress_entity.histfig_ids.include?(u.hist_figure_id)
                            # TODO rescue caged dwarves
                        else
                            u.inventory.each do |it|
                                next if it.item.flags.dump
                                count += 1
                                it.item.flags.dump = true
                            end

                            if u.inventory.empty? and r = ai.plan.find_room(:pitcage) { |_r| _r.dfbuilding } and ai.plan.spiral_search(r.maptile, 1, 1) { |t| df.same_pos?(t, cage) }
                                assign_unit_to_zone(u, r.dfbuilding)
                                @ai.debug "pop: marked #{u.name} for pitting"
                            end
                        end
                    end
                end
            end
            @ai.debug "pop: dumped #{count} items from cages" if count > 0
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
                        so.min_count = 0 if r = ai.plan.find_room(:barracks) { |_r| _r.misc[:squad_id] == sq.id } and r.status != :finished
                    }
                }
            }
        end


        def military_find_commander_or_captain_pos(commander)
            if commander
                df.world.entities.all.binsearch(df.ui.civ_id).entity_raw.positions.each do |a|
                    if a.responsibilities[:MILITARY_STRATEGY] and a.flags[:SITE]
                        return a.code
                    end
                end
            else
                df.world.entities.all.binsearch(df.ui.civ_id).entity_raw.positions.each do |a|
                    if a.flags[:MILITARY_SCREEN_ONLY] and a.flags[:SITE]
                        return a.code
                    end
                end                
            end
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
                    assign_new_noble(military_find_commander_or_captain_pos(true), ns).squad_id = squad_id
                else
                    assign_new_noble(military_find_commander_or_captain_pos(false), ns).squad_id = squad_id
                end
            end

            ns
        end

        # return a squad index with an empty slot
        def military_find_free_squad
            squad_sz = 8
            squad_sz = 6 if @military.length < 4*6
            squad_sz = 4 if @military.length < 3*4

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
                                :item_filter => {:item_type => item_type[t], :material_class => :Armor,
                                    :mattype => -1, :matindex => -1})
                    }
                    pos.uniform[:Weapon][0].item_filter.material_class = :None
                    pos.flags.exact_matches = true
                    pos.unk_118 = pos.unk_11c = -1
                    squad.positions << pos
                }

                if df.ui.main.fortress_entity.squads.length % 3 == 2
                    # ranged squad
                    squad.positions.each { |pos|
                        pos.uniform[:Weapon][0].indiv_choice.ranged = true
                    }
                    squad.ammunition << DFHack::SquadAmmoSpec.cpp_new(:item_filter => { :item_type => :AMMO,
                                        :item_subtype => 0, :material_class => :None}, # subtype = bolts
                                :amount => 500, :flags => { :use_combat => true, :use_training => true })
                else
                    # we don't want all the axes being used up by the military.
                    weapons = df.ui.main.fortress_entity.entity_raw.equipment.weapon_tg.find_all { |idef|
                        next if idef.skill_melee == :MINING
                        next if idef.skill_melee == :AXE
                        next if idef.skill_ranged != :NONE
                        next if idef.flags[:TRAINING]
                        true
                    }
                    if weapons.empty?
                        squad.positions.each { |pos|
                            pos.uniform[:Weapon][0].indiv_choice.melee = true
                        }
                    else
                        squad.positions.each { |pos|
                            pos.uniform[:Weapon][0].item_filter.item_subtype = weapons[rand(weapons.length)].subtype
                        }
                    end
                end

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
        LaborIdle = { :PLANT => true, :HERBALISM => true, :FISH => true, :DETAIL => true }

        LaborMin = Hash.new(2).update :DETAIL => 4, :PLANT => 4, :HERBALISM => 1
        LaborMax = Hash.new(8).update :FISH => 1
        LaborMinPct = Hash.new(0).update :DETAIL => 5, :PLANT => 30, :FISH => 1, :HERBALISM => 10
        LaborMaxPct = Hash.new(0).update :DETAIL => 20, :PLANT => 60, :FISH => 10, :HERBALISM => 50
        LaborList.each { |lb|
            if lb.to_s =~ /HAUL/
                LaborMinPct[lb] = 30
                LaborMaxPct[lb] = 100
            end
        }
        HaulingLaborCount = LaborList.count { |lb|
            lb.to_s =~ /HAUL/
        }
        LaborWontWorkJob = { :AttendParty => true, :Rest => true,
            :UpdateStockpileRecords => true }

        def autolabors(step)
            case step
            when 1
                @workers = []
                @idlers = []
                @labor_needmore = Hash.new(0)
                nonworkers = []
                @medic = {}
                df.ui.main.fortress_entity.assignments_by_type[:HEALTH_MANAGEMENT].each { |n|
                    next unless hf = df.world.history.figures.binsearch(n.histfig)
                    next unless u = df.unit_find(hf.unit_id)
                    @medic[u] = true
                }

                citizen.each_value { |c|
                    next if not u = c.dfunit
                    if u.mood != :None
                        nonworkers << [c, 'has strange mood']
                    elsif u.profession == :CHILD
                        nonworkers << [c, 'is a child']
                    elsif u.profession == :BABY
                        nonworkers << [c, 'is a baby']
                    elsif unit_hasmilitaryduty(u)
                        nonworkers << [c, 'has military duty']
                    elsif u.flags1.caged
                        nonworkers << [c, 'caged']
                    elsif @citizen.length >= 20 and
                        df.world.manager_orders.last and
                        df.world.manager_orders.last.is_validated == 0 and
                        df.unit_entitypositions(u).find { |n| n.responsibilities[:MANAGE_PRODUCTION] }
                        nonworkers << [c, 'validating work orders']
                    elsif u.job.current_job and LaborWontWorkJob[u.job.current_job.job_type]
                        nonworkers << [c, DFHack::JobType::Caption[u.job.current_job.job_type]]
                    elsif u.status.misc_traits.find { |mt| mt.id == :OnBreak }
                        nonworkers << [c, 'on break']
                    elsif u.specific_refs.find { |sr| sr.type == :ACTIVITY }
                        nonworkers << [c, 'has activity']
                    else
                        # TODO filter nobles that will not work
                        @workers << c
                        @idlers << c if not u.job.current_job
                    end
                }

                # free non-workers
                nonworkers.each { |c|
                    u = c[0].dfunit
                    ul = u.status.labors
                    any = false
                    LaborList.each { |lb|
                        if ul[lb]
                            case lb
                            when :DIAGNOSE, :SURGERY, :BONE_SETTING, :SUTURING, :DRESSING_WOUNDS, :FEED_WATER_CIVILIANS
                                # don't unassign medical jobs from the chief medical dwarf
                                next if @medic[u]
                            end
                            # disable everything (may wait meeting)
                            ul[lb] = false
                            u.military.pickup_flags.update = true if LaborTool[lb]
                            any = true
                        end
                    }
                    @ai.debug "unassigned all labors from #{u.name} (non-worker: #{c[1]})" if any
                }

            when 2
                seen_workshop = {}
                df.world.job_list.each { |job|
                    ref_bld = job.general_refs.grep(DFHack::GeneralRefBuildingHolderst).first
                    ref_wrk = job.general_refs.grep(DFHack::GeneralRefUnitWorkerst).first
                    next if ref_bld and seen_workshop[ref_bld.building_id]

                    if not ref_wrk
                        job_labor = DFHack::JobSkill::Labor[DFHack::JobType::Skill[job.job_type]]
                        job_labor = DFHack::JobType::Labor.fetch(job.job_type, job_labor)
                        if job_labor and job_labor != :NONE
                            @labor_needmore[job_labor] += 1

                        else
                            case job.job_type
                            when :ConstructBuilding, :DestroyBuilding
                                # TODO
                                @labor_needmore[:UNKNOWN_BUILDING_LABOR_PLACEHOLDER] += 1
                            when :CustomReaction
                                reac = df.world.raws.reactions.find { |r| r.code == job.reaction_name }
                                if reac and job_labor = DFHack::JobSkill::Labor[reac.skill]
                                    @labor_needmore[job_labor] += 1 if job_labor != :NONE
                                end
                            when :PenSmallAnimal, :PenLargeAnimal
                                @labor_needmore[:HAUL_ANIMAL] += 1
                            when :StoreItemInStockpile, :StoreItemInBag, :StoreItemInHospital,
                                    :StoreItemInChest, :StoreItemInCabinet, :StoreWeapon,
                                    :StoreArmor, :StoreItemInBarrel, :StoreItemInBin, :StoreItemInVehicle
                                LaborList.each { |lb|
                                    if lb.to_s =~ /HAUL/
                                        @labor_needmore[lb] += 1
                                    end
                                }
                            else
                                if job.material_category.wood
                                    @labor_needmore[:CARPENTER] += 1
                                elsif job.material_category.bone
                                    @labor_needmore[:BONE_CARVE] += 1
                                elsif job.material_category.cloth
                                    @labor_needmore[:CLOTHESMAKER] += 1
                                elsif job.material_category.leather
                                    @labor_needmore[:LEATHER] += 1
                                elsif job.mat_type == 0
                                    # XXX metalcraft ?
                                    @labor_needmore[:MASON] += 1
                                elsif $DEBUG
                                    @seen_badwork ||= {}
                                    @ai.debug "unknown labor for #{job.job_type} #{job.inspect}" if not @seen_badwork[job.job_type]
                                    @seen_badwork[job.job_type] = true
                                end
                            end
                        end
                    end

                    if ref_bld
                        case ref_bld.building_tg
                        when DFHack::BuildingFarmplotst, DFHack::BuildingStockpilest
                            # parallel work allowed
                        else
                            seen_workshop[ref_bld.building_id] = true
                        end
                    end

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

                if @workers.length > 15
                    # if one has too many labors, free him up (one per round)
                    lim = 4*LaborList.length/[@workers.length, 1].max
                    lim = 4 if lim < 4
                    lim += HaulingLaborCount
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
                        @ai.debug "unassigned all labors from #{u.name} (too many labors)"
                    end
                end

            when 4
                labormin = LaborMin
                labormax = LaborMax
                laborminpct = LaborMinPct
                labormaxpct = LaborMaxPct

                LaborList.each { |lb|
                    max = labormax[lb]
                    maxpc = labormaxpct[lb] * @workers.length / 100
                    max = maxpc if maxpc > max

                    @labor_needmore.delete lb if @labor_worker[lb].length >= max
                }

                if @labor_needmore.empty? and not @idlers.empty? and @ai.plan.past_initial_phase and (not @last_idle_year or @last_idle_year != df.cur_year)
                    @ai.plan.idleidle
                    @last_idle_year = df.cur_year
                end

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

                # list of dwarves with an exclusive labor
                exclusive = {}
                [
                    [:CARPENTER, lambda { ai.plan.find_room(:workshop) { |r| r.subtype == :Carpenters and r.dfbuilding and not r.dfbuilding.jobs.empty? } }],
                    [:MINE, lambda { ai.plan.digging? }],
                    [:MASON, lambda { ai.plan.find_room(:workshop) { |r| r.subtype == :Masons and r.dfbuilding and not r.dfbuilding.jobs.empty? } }],
                    [:CUTWOOD, lambda { ai.stocks.cutting_trees? }],
                    [:DETAIL, lambda { r = ai.plan.find_room(:cistern) { |_r| _r.subtype == :well } and not r.misc[:channeled] }],
                ].each { |lb, test|
                    if @workers.length > exclusive.length+2 and @labor_needmore[lb] > 0 and test[]
                        # keep last run's choice
                        cid = @labor_worker[lb].sort_by { |i| @worker_labor[i].length }.first
                        next if not cid
                        exclusive[cid] = lb
                        @idlers.delete_if { |_c| _c.id == cid }
                        c = citizen[cid]
                        @worker_labor[cid].dup.each { |llb|
                            next if llb == lb
                            autolabor_unsetlabor(c, llb, "has exclusive labor: #{lb}")
                        }
                    end
                }

                # autolabor!
                LaborList.each { |lb|
                    min = labormin[lb]
                    max = labormax[lb]
                    minpc = laborminpct[lb] * @workers.length / 100
                    maxpc = labormaxpct[lb] * @workers.length / 100
                    min = minpc if minpc > min
                    max = maxpc if maxpc > max
                    max = @workers.length if @labor_needmore.empty? and LaborIdle[lb]
                    max = 0 if lb == :FISH and fishery = ai.plan.find_room(:workshop) { |_r| _r.subtype == :Fishery } and fishery.status == :plan
                    min = max if min > max
                    min = @workers.length if min > @workers.length

                    cnt = @labor_worker[lb].length
                    if cnt > max
                        next if @labor_needmore.empty?
                        sk = LaborSkill[lb]
                        @labor_worker[lb] = @labor_worker[lb].sort_by { |_cid|
                            if sk
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
                            autolabor_unsetlabor(citizen[cid], lb, 'too many dwarves')
                        }

                    elsif cnt < min
                        min += 1 if min < max and not LaborTool[lb]
                        (min-cnt).times {
                            c = @workers.sort_by { |_c|
                                malus = @worker_labor[_c.id].length * 10
                                malus += df.unit_entitypositions(_c.dfunit).length * 40
                                if sk = LaborSkill[lb] and usk = _c.dfunit.status.current_soul.skills.find { |_usk| _usk.id == sk }
                                    malus -= DFHack::SkillRating.int(usk.rating) * 4    # legendary => 15
                                end
                                [malus, rand]
                            }.find { |_c|
                                next if exclusive[_c.id]
                                next if LaborTool[lb] and @worker_labor[_c.id].find { |_lb| LaborTool[_lb] }
                                not @worker_labor[_c.id].include?(lb)
                            }

                            autolabor_setlabor(c, lb, 'not enough dwarves')
                        }

                    elsif not @idlers.empty?
                        more = @labor_needmore[lb]
                        more = max - cnt if @labor_needmore.empty?
                        more.times do
                            break if @labor_worker[lb].length >= max
                            c = @idlers[rand(@idlers.length)]
                            autolabor_setlabor(c, lb, 'idle')
                        end
                    end
                }
            end
        end

        def autolabor_setlabor(c, lb, reason='no reason given')
            return if not c
            return if @worker_labor[c.id].include?(lb)
            @labor_worker[lb] << c.id
            @worker_labor[c.id] << lb
            u = c.dfunit
            if LaborTool[lb]
                LaborTool.keys.each { |_lb| u.status.labors[_lb] = false }
            end
            u.status.labors[lb] = true
            u.military.pickup_flags.update = true if LaborTool[lb]
            @ai.debug "assigning labor #{lb} to #{u.name} (#{reason})"
        end

        def autolabor_unsetlabor(c, lb, reason='no reason given')
            return if not c
            return if not @worker_labor[c.id].include?(lb)
            u = c.dfunit
            case lb
            when :DIAGNOSE, :SURGERY, :BONE_SETTING, :SUTURING, :DRESSING_WOUNDS, :FEED_WATER_CIVILIANS
                # don't unassign medical jobs from the chief medical dwarf
                return if @medic[u]
            end
            @labor_worker[lb].delete c.id
            @worker_labor[c.id].delete lb
            u.status.labors[lb] = false
            u.military.pickup_flags.update = true if LaborTool[lb]
            @ai.debug "unassigning labor #{lb} from #{u.name} (#{reason})"
        end

        def unit_hasmilitaryduty(u)
            return if u.military.squad_id == -1
            squad = df.world.squads.all.binsearch(u.military.squad_id)
            curmonth = squad.schedule[squad.cur_alert_idx][df.cur_year_tick / (1200*28)]
            !curmonth.orders.empty? and !(curmonth.orders.length == 1 and curmonth.orders[0].min_count == 0)
        end

        def unit_totalxp(u)
            u.status.current_soul.skills.inject(0) { |t, sk|
                rat = DFHack::SkillRating.int(sk.rating)
                t + 400*rat + 100*rat*(rat+1)/2 + sk.experience
            }
        end

        def positionCode(responsibility)
            df.world.entities.all.binsearch(df.ui.civ_id).entity_raw.positions.each do |a|
                if a.responsibilities[responsibility]
                    return a.code
                end
            end
        end
        
        def update_nobles
            cz = df.unit_citizens.sort_by { |u| unit_totalxp(u) }.find_all { |u| u.profession != :BABY and u.profession != :CHILD }
            ent = df.ui.main.fortress_entity


            if ent.assignments_by_type[:MANAGE_PRODUCTION].empty? and tg = cz.find { |u|
                u.military.squad_id == -1 and !ent.positions.assignments.find { |a| a.histfig == u.hist_figure_id }
            } || cz.first
                # TODO do check population caps, ...
                assign_new_noble(positionCode(:MANAGE_PRODUCTION), tg)
            end


            if ent.assignments_by_type[:ACCOUNTING].empty? and tg = cz.find { |u|
                u.military.squad_id == -1 and !ent.positions.assignments.find { |a| a.histfig == u.hist_figure_id } and !u.status.labors[:MINE]
            }
                assign_new_noble(positionCode(:ACCOUNTING), tg)
                df.ui.bookkeeper_settings = 4
            end


            if ent.assignments_by_type[:HEALTH_MANAGEMENT].empty? and
                    hosp = ai.plan.find_room(:infirmary) and hosp.status != :plan and tg = cz.find { |u|
                u.military.squad_id == -1 and !ent.positions.assignments.find { |a| a.histfig == u.hist_figure_id }
            }
                assign_new_noble(positionCode(:HEALTH_MANAGEMENT), tg)
            elsif ass = ent.assignments_by_type[:HEALTH_MANAGEMENT].first and hf = df.world.history.figures.binsearch(ass.histfig) and doc = df.unit_find(hf.unit_id)
                # doc => healthcare
                LaborList.each { |lb|
                    case lb
                    when :DIAGNOSE, :SURGERY, :BONE_SETTING, :SUTURING, :DRESSING_WOUNDS, :FEED_WATER_CIVILIANS
                        doc.status.labors[lb] = true
                    end
                }
            end


            if ent.assignments_by_type[:TRADE].empty? and tg = cz.find { |u|
                u.military.squad_id == -1 and !ent.positions.assignments.find { |a| a.histfig == u.hist_figure_id }
            }
                assign_new_noble(positionCode(:TRADE), tg)
            end

            check_noble_appartments
        end

        def check_noble_appartments
            noble_ids = df.ui.main.fortress_entity.assignments_by_type.map { |alist|
                alist.map { |a| a.histfig_tg.unit_id }
            }.flatten.uniq.sort

            noble_ids.delete_if { |id|
                not df.unit_entitypositions(df.unit_find(id)).find { |ep|
                    ep.required_office > 0 or ep.required_dining > 0 or ep.required_tomb > 0
                }
            }

            @ai.plan.attribute_noblerooms(noble_ids)
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
            needmilk = -ai.stocks.find_manager_orders(:MilkCreature).inject(0) { |s, o| s + o.amount_left }
            needshear = -ai.stocks.find_manager_orders(:ShearCreature).inject(0) { |s, o| s + o.amount_left }

            np = @pet.dup
            df.world.units.active.each { |u|
                next if u.civ_id != df.ui.civ_id
                next if u.race == df.ui.race_id
                next if u.flags1.dead or u.flags1.merchant or u.flags1.forest

                if @pet[u.id]
                    if @pet[u.id].include?(:MILKABLE) and u.profession != :BABY and u.profession != :CHILD
                        if not u.status.misc_traits.find { |mt| mt.id == :MilkCounter }
                            needmilk += 1
                        end
                    end

                    if @pet[u.id].include?(:SHEARABLE) and u.profession != :BABY and u.profession != :CHILD
                        if u.caste_tg.shearable_tissue_layer.find { |stl|
                            stl.bp_modifiers_idx.find { |bpi|
                                u.appearance.bp_modifiers[bpi] >= stl.length
                            }
                        }
                            needshear += 1
                        end
                    end

                    np.delete u.id
                    next
                end

                @pet[u.id] = []

                cst = u.caste_tg

                if cst.flags[:MILKABLE]
                    @pet[u.id] << :MILKABLE
                end

                if cst.shearable_tissue_layer.length > 0
                    @pet[u.id] << :SHEARABLE
                end

                if cst.flags[:GRAZER]
                    @pet[u.id] << :GRAZER

                    if bld = @ai.plan.getpasture(u.id)
                        assign_unit_to_zone(u, bld)
                        # TODO monitor grass levels
                    else
                        # TODO slaughter best candidate, keep this one
                        # also avoid killing named pets
                        u.flags2.slaughter = true
                    end
                end

                if cst.flags[:LAYS_EGGS]
                    # TODO nest boxes
                end

                if cst.flags[:ADOPTS_OWNER]
                    # keep only one
                    oi = @pet.find_all { |i, t| t.include?(:ADOPTS_OWNER) }
                    if oi.empty?
                        @pet[u.id] << :ADOPTS_OWNER
                    elsif u.caste != 0 and ou = df.unit_find(oi[0][0]) and ou.caste == 0 and !ou.flags2.slaughter
                        # keep one male if possible
                        @pet[u.id] << :ADOPTS_OWNER
                        ou.flags2.slaughter = true
                    else
                        u.flags2.slaughter = true
                    end
                end
            }

            np.each_key { |id|
                @ai.plan.freepasture(id)
                @pet.delete id
            }

            needmilk = 30 if needmilk > 30
            ai.stocks.add_manager_order(:MilkCreature, needmilk) if needmilk > 0

            needshear = 30 if needshear > 30
            ai.stocks.add_manager_order(:ShearCreature, needshear) if needshear > 0
        end

        def assign_unit_to_zone(u, bld)
            # remove existing zone assignments
            # TODO remove existing chains/cages ?
            while ridx = u.general_refs.index { |ref| ref.kind_of?(DFHack::GeneralRefBuildingCivzoneAssignedst) }
                ref = u.general_refs[ridx]
                cidx = ref.building_tg.assigned_units.index(u.id)
                ref.building_tg.assigned_units.delete_at(cidx)
                u.general_refs.delete_at(ridx)
                df.free(ref._memaddr)
            end

            u.general_refs << DFHack::GeneralRefBuildingCivzoneAssignedst.cpp_new(:building_id => bld.id)
            bld.assigned_units << u.id
        end

        def status
            "#{@citizen.length} citizen, #{@military.length} military, #{@idlers.length} idle, #{@pet.length} pets"
        end

        def serialize
            {
                :citizen  => Hash[@citizen.map{ |k, v| [k, v.serialize] }],
                :military => Hash[@military.map{ |k, v| [k, v.serialize] }],
                :pet      => @pet,
            }
        end
    end
end

# vim: et:sw=4:ts=4
