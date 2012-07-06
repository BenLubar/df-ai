# a dwarf fortress autonomous artificial intelligence (more or less)

case $script_args[0]
when 'start'
    Dir['hack/scripts/ai/*.rb'].each { |f| load f }

    $dwarfAI = DwarfAI.new
    $dwarfAI.onupdate_register

when 'end'
    $dwarfAI.onupdate_unregister
    puts "removed onupdate"

when 'patch'
    Dir['hack/scripts/ai/*.rb'].each { |f| load f }

else
    $dwarfAI.info_status
end
