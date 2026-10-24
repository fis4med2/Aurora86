-- @name todo
-- @version 1.0
-- @description A persistent task queue: add a task and finish the first one.
local filename = "/home/guest/docs/tasks.txt"
local function load()
    local files, err = aurora.list("/home/guest/docs")
    if not files then return nil, err end
    for _, path in ipairs(files) do
        if path == filename then return aurora.read(filename) end
    end
    return ""
end
local tasks, problem = load()
local function show()
    aurora.text(tasks == nil and problem or tasks == "" and "No tasks. Add your first one below." or tasks)
end
local function save(text)
    local ok, err = aurora.write(filename, text)
    if ok then tasks = text; show() else aurora.text(err) end
    return ok
end
aurora.window {
    title = "Task Queue",
    text = "Loading your tasks...",
    inputs = {{id = "task", label = "New task", value = ""}},
    buttons = {
        {label = "Add task", action = function()
            if tasks == nil then aurora.text(problem); return end
            local task = aurora.input("task")
            if #task == 0 or #task > 120 then aurora.text("Enter a task between 1 and 120 bytes."); return end
            if #tasks + #task > 4000 then aurora.text("Task queue full. Finish a task first."); return end
            if save(tasks .. task .. "\n") then aurora.input("task", "") end
        end},
        {label = "Finish first", action = function()
            if tasks == nil then aurora.text(problem); return end
            if tasks == "" then show(); return end
            local first = #tasks
            for i = 1, #tasks do
                if string.byte(tasks, i) == 10 then first = i; break end
            end
            save(string.sub(tasks, first + 1))
        end},
        {label = "Reload", action = function()
            tasks, problem = load()
            show()
        end}
    }
}
show()
