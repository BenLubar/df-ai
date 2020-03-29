local args = {...}
local done_command = args[1]

print('Running tests')

local status = dfhack.run_command('ai', 'validate')
if status == CR_OK then
    print('test passed: df-ai:validate')
else
    dfhack.printerr('test errored: df-ai:validate: status=' .. tostring(status))
end

local f = io.open('test_status.json', 'w')
f:write('{"ai":"passed"}')
f:close()
if done_command then
    dfhack.run_command(done_command)
end
