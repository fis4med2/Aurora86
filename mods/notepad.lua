-- @name notepad
-- @version 1.0
-- @description A small notebook that reads and appends to a virtual document.
local filename = "/home/guest/docs/lua-notes.txt"
local function contents()
    return aurora.read(filename) or "No notes yet."
end
aurora.window {
    title = "Lua Notebook",
    text = contents(),
    buttons = {
        { label = "Add a note", action = function()
            local previous = aurora.read(filename) or ""
            local ok, err = aurora.write(filename, previous .. "A note from my Lua program.\n")
            if ok then aurora.text(contents()) else aurora.text(err) end
        end },
        { label = "Read", action = function() aurora.text(contents()) end }
    }
}
