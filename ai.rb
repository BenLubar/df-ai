# a dwarf fortress autonomous artificial intelligence (more or less)

case $script_args[0]
when 'start'
    Dir['hack/scripts/ai/*.rb'].each { |f| load f }

    $dwarfAI = DwarfAI.new
    $dwarfAI.onupdate_register

when 'end', 'stop'
    $dwarfAI.onupdate_unregister
    puts "removed onupdate"

when 'patch', 'update'
    Dir['hack/scripts/ai/*.rb'].each { |f| load f }

else
    if $dwarfAI
        puts $dwarfAI.status
    else
        puts "AI not started"
    end
end
