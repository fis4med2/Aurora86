-- @name theme-switcher
-- @version 1.0
-- @description Change the Aurora monitor theme with Lua.
aurora.window {
    title = "Monitor Workshop",
    text = "Choose a monitor palette. Changes are saved to the virtual computer.",
    buttons = {
        { label = "Green", action = function() aurora.theme("green") end },
        { label = "Amber", action = function() aurora.theme("amber") end },
        { label = "Paper", action = function() aurora.theme("paper") end }
    }
}
