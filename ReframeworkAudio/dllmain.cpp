#include <Windows.h>
#include <mutex>
#include <map>
#include <string>

#include "reframework/API.h"

#define SDL_MAIN_HANDLED
#include "SDL2/SDL.h"
#include "SDL2/SDL_mixer.h"

#include <sol/sol.hpp>

// REFramework API 接口函数指针
static const REFrameworkPluginFunctions* g_functions = nullptr;

// 音频数据结构体（使用 Mix_Music 以支持进度查询与跳转）
typedef struct AudioData {
    Mix_Music* music;   // 音频数据指针（流式解码，支持 MP3/OGG/WAV）
    int volume;         // 音量（0-128）
} AudioData;

// 音频列表、互斥锁与当前播放项
static std::mutex g_audioMutex;
static std::map<std::string, AudioData> g_audioList;
static std::string g_currentName;   // 当前正在播放的音频名（Mix_Music 同时只能播一个）

// 日志输出函数
static void logInfo(const std::string& msg) {
    if (g_functions && g_functions->log_info) {
        g_functions->log_info("[LuaAudio] %s", msg.c_str());
    }
}

static void logError(const std::string& msg) {
    if (g_functions && g_functions->log_error) {
        g_functions->log_error("[LuaAudio] %s", msg.c_str());
    }
}

// 加载音频（支持 WAV/OGG/MP3 等 SDL_mixer 已初始化的格式）
static bool loadAudio(std::string name, std::string path) {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    if (g_audioList.count(name) > 0) {
        logError("音频 " + name + " 已存在，无法加载");
        return true;
    }
    AudioData audio;
    audio.music = Mix_LoadMUS(path.c_str());
    if (audio.music == NULL) {
        logError("无法加载音频文件 " + path + "，错误信息：" + std::string(Mix_GetError()));
        return false;
    }
    audio.volume = 128;
    g_audioList[name] = audio;
    return true;
}

// 播放音频
static bool playAudio(std::string name) {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    if (g_audioList.count(name) == 0) {
        logError("音频 " + name + " 不存在，无法播放");
        return false;
    }
    AudioData* audio = &g_audioList[name];
    if (Mix_PlayMusic(audio->music, 0) == -1) {
        logError("无法播放音频 " + name + "，错误信息：" + std::string(Mix_GetError()));
        return false;
    }
    Mix_VolumeMusic(audio->volume);
    g_currentName = name;
    return true;
}

// 暂停播放
static bool pauseAudio(std::string name) {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    if (g_audioList.count(name) == 0) {
        logError("音频 " + name + " 不存在，无法暂停");
        return false;
    }
    if (g_currentName != name) {
        logError("音频 " + name + " 当前未在播放，无法暂停");
        return false;
    }
    Mix_PauseMusic();
    return true;
}

// 恢复音频播放
static bool resumeAudio(std::string name) {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    if (g_audioList.count(name) == 0) {
        logError("音频 " + name + " 不存在，无法恢复");
        return false;
    }
    if (g_currentName != name) {
        logError("音频 " + name + " 当前未在播放，无法恢复");
        return false;
    }
    Mix_ResumeMusic();
    return true;
}

// 控制播放进度（position 单位：秒）
static bool seekAudio(std::string name, double position) {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    if (g_audioList.count(name) == 0) {
        logError("音频 " + name + " 不存在，无法控制进度");
        return false;
    }
    if (g_currentName != name) {
        logError("音频 " + name + " 当前未在播放，无法控制进度");
        return false;
    }
    if (position < 0) {
        logError("位置不合法，无法控制进度");
        return false;
    }
    Mix_SetMusicPosition(position);
    return true;
}

// 控制播放音量（0-128）
static bool setVolume(std::string name, int volume) {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    if (g_audioList.count(name) == 0) {
        logError("音频 " + name + " 不存在，无法控制音量");
        return false;
    }
    if (volume < 0 || volume > 128) {
        logError("音量不合法，无法控制音量");
        return false;
    }
    AudioData* audio = &g_audioList[name];
    audio->volume = volume;
    if (g_currentName == name) {
        Mix_VolumeMusic(volume);
    }
    return true;
}

// 获取当前播放进度（返回秒数）
static double getCurrentPosition(std::string name) {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    if (g_audioList.count(name) == 0) {
        logError("音频 " + name + " 不存在，无法获取进度");
        return -1.0;
    }
    if (g_currentName != name) {
        return 0.0;
    }
    return Mix_GetMusicPosition(g_audioList[name].music);
}

// 获取音频总时长（返回秒数）
static double getAudioSize(std::string name) {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    if (g_audioList.count(name) == 0) {
        logError("音频 " + name + " 不存在，无法获取音频大小");
        return -1.0;
    }
    return Mix_MusicDuration(g_audioList[name].music);
}

// 释放音频资源
static void freeAudio(std::string name) {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    if (g_audioList.count(name) == 0) {
        return;
    }
    if (g_currentName == name) {
        Mix_HaltMusic();
        g_currentName.clear();
    }
    Mix_FreeMusic(g_audioList[name].music);
    g_audioList.erase(name);
}

// Lua 状态创建回调 - 在此注册音频函数到 Lua 全局表
static void onLuaStateCreated(lua_State* L) {
    sol::state_view lua(L);
    lua.set_function("loadAudio", loadAudio);
    lua.set_function("playAudio", playAudio);
    lua.set_function("pauseAudio", pauseAudio);
    lua.set_function("resumeAudio", resumeAudio);
    lua.set_function("seekAudio", seekAudio);
    lua.set_function("setVolume", setVolume);
    lua.set_function("getCurrentPosition", getCurrentPosition);
    lua.set_function("getAudioSize", getAudioSize);
    lua.set_function("deleteAudio", freeAudio);
    logInfo("Lua 音频函数注册完成");
}

// REFramework 插件版本声明
extern "C" __declspec(dllexport) void reframework_plugin_required_version(REFrameworkPluginVersion* version) {
    version->major = REFRAMEWORK_PLUGIN_VERSION_MAJOR;
    version->minor = REFRAMEWORK_PLUGIN_VERSION_MINOR;
    version->patch = REFRAMEWORK_PLUGIN_VERSION_PATCH;
    version->game_name = nullptr;
}

// REFramework 插件初始化
extern "C" __declspec(dllexport) bool reframework_plugin_initialize(const REFrameworkPluginInitializeParam* param) {
    g_functions = param->functions;

    // 初始化 SDL 音频子系统
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        logError(std::string("SDL 初始化失败：") + SDL_GetError());
        return false;
    }

    // 初始化 SDL_mixer
    if (Mix_OpenAudio(22050, MIX_DEFAULT_FORMAT, 2, 4096) < 0) {
        logError(std::string("SDL_mixer 初始化失败：") + Mix_GetError());
        return false;
    }
    // 初始化 OGG 与 MP3 解码器（MP3 需 sdl2-mixer 启用 mpg123 特性）
    int mixFlags = MIX_INIT_OGG | MIX_INIT_MP3;
    int inited = Mix_Init(mixFlags);
    if ((inited & MIX_INIT_OGG) == 0) {
        logError(std::string("OGG 解码器初始化失败：") + Mix_GetError());
    }
    if ((inited & MIX_INIT_MP3) == 0) {
        logError(std::string("MP3 解码器初始化失败（sdl2-mixer 需启用 mpg123 特性）：") + Mix_GetError());
    }

    // 注册 Lua 状态创建回调
    if (!g_functions->on_lua_state_created(onLuaStateCreated)) {
        logError("注册 on_lua_state_created 回调失败");
        return false;
    }

    logInfo("LuaAudio 插件初始化完成");
    return true;
}
