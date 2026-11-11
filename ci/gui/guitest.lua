hexchat.register("guitest-lua", "1", "GUI test helper")
local f = assert(io.open(hexchat.get_info("configdir") .. "/guitest.out", "a"))
f:write("lua-loaded\n")
f:close()
