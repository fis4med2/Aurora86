-- @name notepad
-- @version 1.1
-- @description An editable notebook with Save and Reload on the virtual disk.
local filename = "/home/guest/docs/lua-notes.txt"
local ready = false
local function load()
    local files, err = aurora.list("/home/guest/docs")
    if not files then return nil, err end
    for _, path in ipairs(files) do
        if path == filename then
            local text, problem = aurora.read(filename)
            if text and #text > 4096 then return nil, "Notebook limit: 4096 bytes. Open this file in Writer." end
            return text, problem
        end
    end
    return ""
end
local initial, problem = load()
ready = initial ~= nil
aurora.window {
    title = "Lua Notebook",
    text = problem or "Write a note, then Save. Your text stays on the virtual disk.",
    inputs = {{id = "note", label = "Your notes", value = initial or "", multiline = true}},
    buttons = {
        {label = "Save", action = function()
            if not ready then aurora.text("Reload the notebook before saving."); return end
            local ok, err = aurora.write(filename, aurora.input("note"))
            aurora.text(ok and "Saved to " .. filename or err)
        end},
        {label = "Reload", action = function()
            local text, err = load()
            ready = text ~= nil
            if ready then
                local ok, problem = aurora.input("note", text)
                ready = ok == true
                if not ready then err = problem end
            end
            aurora.text(ready and "Loaded from " .. filename or err)
        end}
    }
}
