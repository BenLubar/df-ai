local args = {...}
local done_command = args[1]

function set_test_stage(stage)
    local f = io.open('test_stage.txt', 'w')
    f:write(stage)
    f:close()
end

print('Running tests')

local status = dfhack.run_command('ai', 'validate')
if status == CR_OK then
    print('test passed: df-ai:validate')
else
    dfhack.printerr('test errored: df-ai:validate: status=' .. tostring(status))
end

set_test_stage('done')
if done_command then
    dfhack.run_command(done_command)
end
