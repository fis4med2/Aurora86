-- @name hello
-- @version 1.0
-- @description A first Aurora Lua application with a working button.
local count = 0
aurora.window {
    title = "Hello, Aurora",
    text = "Your Lua program runs inside the A16 virtual computer.",
    buttons = {
        { label = "Say hello", action = function()
            count = count + 1
            aurora.text("Hello from Lua! Button presses: " .. count)
            print("Hello number " .. count)
        end }
    }
}
