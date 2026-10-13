--[[
    testAudio.lua — ReframeworkAudio 插件测试脚本
    尝试加载并播放 reframework/music/testAudio.mp3

    加载成功后自动播放一次；
    REFramework UI -> Script Generated UI -> testAudio 里有控制按钮。

    注意：插件文档声明只支持 ogg/wav，mp3 不一定可用，
    日志里会显示每个路径的加载结果。
]]

local AUDIO_ID = 'test_audio'

-- 依次尝试的相对路径（覆盖插件可能使用的基准目录）
local CANDIDATE_PATHS = {
    'reframework/music/testAudio.mp3', -- 游戏根目录为基准
    'music/testAudio.mp3',             -- reframework 目录为基准
    '../music/testAudio.mp3',          -- reframework/data 为基准
}

-- 插件会向 Lua 注册全局函数，若不存在说明插件未加载
if loadAudio == nil then
    log.info('[testAudio] ReframeworkAudio 插件未加载：找不到全局函数 loadAudio')
    return
end

log.info('[testAudio] ReframeworkAudio 插件已加载，开始测试')

-- ===== 1. 加载音频（多路径尝试） =====
local loadedPath = nil
for _, path in ipairs(CANDIDATE_PATHS) do
    local r = loadAudio(AUDIO_ID, path)
    log.info(string.format('[testAudio] loadAudio("%s", "%s") -> %s', AUDIO_ID, path, tostring(r)))
    if r then
        loadedPath = path
        break
    end
end

-- ===== 2. 加载成功则设置音量并自动播放 =====
if loadedPath then
    log.info('[testAudio] 加载成功，使用路径: ' .. loadedPath)

    local vr = setVolume(AUDIO_ID, 96) -- 取值 0-128
    log.info('[testAudio] setVolume(96) -> ' .. tostring(vr))

    local pr = playAudio(AUDIO_ID)
    log.info('[testAudio] playAudio -> ' .. tostring(pr))
else
    log.warn('[testAudio] 所有路径加载失败：mp3 可能不被支持（文档称仅支持 ogg/wav），或路径基准目录不在候选列表中')
end

-- ===== 3. Script Generated UI 控制面板 =====
re.on_draw_ui(function()
    imgui.text('ReframeworkAudio test: ' .. (loadedPath or 'NOT LOADED'))

    if imgui.button('Play') then
        local r = playAudio(AUDIO_ID)
        log.info('[testAudio] playAudio -> ' .. tostring(r))
    end
    if imgui.button('Pause') then
        local r = pauseAudio(AUDIO_ID)
        log.info('[testAudio] pauseAudio -> ' .. tostring(r))
    end
    if imgui.button('Resume') then
        local r = resumeAudio(AUDIO_ID)
        log.info('[testAudio] resumeAudio -> ' .. tostring(r))
    end
    if imgui.button('Seek +5s') then
        local pos = getCurrentPosition(AUDIO_ID)
        local r = seekAudio(AUDIO_ID, (tonumber(pos) or 0) + 5)
        log.info('[testAudio] seekAudio -> ' .. tostring(r))
    end
    if imgui.button('Volume 128') then
        local r = setVolume(AUDIO_ID, 128)
        log.info('[testAudio] setVolume(128) -> ' .. tostring(r))
    end
    if imgui.button('Volume 64') then
        local r = setVolume(AUDIO_ID, 64)
        log.info('[testAudio] setVolume(64) -> ' .. tostring(r))
    end
    if imgui.button('Delete (释放音频)') then
        local r = deleteAudio(AUDIO_ID)
        loadedPath = nil
        log.info('[testAudio] deleteAudio -> ' .. tostring(r))
    end

    -- 实时显示播放位置
    local pos = getCurrentPosition(AUDIO_ID)
    imgui.text('position: ' .. tostring(pos))
end)
