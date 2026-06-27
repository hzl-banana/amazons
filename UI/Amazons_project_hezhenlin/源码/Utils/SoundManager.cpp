#include "SoundManager.h"
#include <iostream>
#include <sstream>

SoundManager::SoundManager() 
    : soundEffectCounter(0), bgmVolume(1000), soundVolume(1000),  // 改为最大音量
      masterVolume(1000), bgmPlaying(false), bgmPaused(false) {
    bgmAlias = L"BGM_Main";
    std::wcout << L"[初始化] SoundManager 构造，BGM音量=" << bgmVolume << std::endl;
}

SoundManager::~SoundManager() {
    stopAll();
}

bool SoundManager::initialize(const std::string& soundFolder) {
    initializeSoundPaths(soundFolder);
    
    // 可选：预加载BGM
    auto bgmPath = soundPaths.find(SOUND_BGM);
    if (bgmPath != soundPaths.end()) {
        std::wcout << L"音频系统初始化成功，BGM路径: " << stringToWString(bgmPath->second) << std::endl;
    }
    
    return true;
}

void SoundManager::initializeSoundPaths(const std::string& soundFolder) {
    // 映射音频类型到文件路径
    soundPaths[SOUND_BGM] = soundFolder + "background.wav";
    soundPaths[SOUND_SELECT] = soundFolder + "select.wav";
    soundPaths[SOUND_MOVE] = soundFolder + "move.wav";
    soundPaths[SOUND_SHOOT] = soundFolder + "shoot.wav";
    soundPaths[SOUND_COUNT_0] = soundFolder + "count_0.wav";
    soundPaths[SOUND_COUNT_1] = soundFolder + "count_1.wav";
    soundPaths[SOUND_COUNT_2] = soundFolder + "count_2.wav";
    soundPaths[SOUND_COUNT_3] = soundFolder + "count_3.wav";
    soundPaths[SOUND_COUNT_4] = soundFolder + "count_4.wav";
    soundPaths[SOUND_COUNT_5] = soundFolder + "count_5.wav";
    soundPaths[SOUND_COUNT_6] = soundFolder + "count_6.wav";
    soundPaths[SOUND_COUNT_7] = soundFolder + "count_7.wav";
    soundPaths[SOUND_COUNT_8] = soundFolder + "count_8.wav";
    soundPaths[SOUND_COUNT_9] = soundFolder + "count_9.wav";
}

void SoundManager::playBGM() {
    std::wcout << L"[调试] playBGM() 被调用" << std::endl;
    
    if (bgmPlaying && !bgmPaused) {
        std::wcout << L"[调试] BGM已在播放，跳过" << std::endl;
        return; // BGM已经在播放
    }
    
    if (bgmPaused) {
        std::wcout << L"[调试] BGM已暂停，恢复播放" << std::endl;
        resumeBGM();
        return;
    }
    
    auto bgmPath = soundPaths.find(SOUND_BGM);
    if (bgmPath != soundPaths.end()) {
        std::wstring path = stringToWString(bgmPath->second);
        std::wcout << L"[调试] BGM路径: " << path << std::endl;
        std::wcout << L"[调试] 调用 playAudioFile..." << std::endl;
        
        // 先尝试不循环播放，测试是否能听到
        std::wcout << L"[测试] 使用非循环模式播放（测试用）" << std::endl;
        if (playAudioFile(path, bgmAlias, false)) {  // false = 不循环
            bgmPlaying = true;
            bgmPaused = false;
            int finalVolume = bgmVolume * masterVolume / 1000;
            std::wcout << L"[调试] 计算后的音量: bgmVolume=" << bgmVolume 
                       << L", masterVolume=" << masterVolume 
                       << L", 最终音量=" << finalVolume << std::endl;
            setAudioVolume(bgmAlias, finalVolume);
            std::wcout << L"[成功] BGM开始播放！音量=" << finalVolume << std::endl;
            std::wcout << L"[提示] 如果能听到声音但很短，说明文件有效，只是循环模式有问题" << std::endl;
        } else {
            std::wcerr << L"[失败] playAudioFile 返回 false" << std::endl;
        }
    } else {
        std::wcerr << L"[失败] 未找到 BGM 路径配置" << std::endl;
    }
}

void SoundManager::stopBGM() {
    if (bgmPlaying) {
        stopAudio(bgmAlias);
        bgmPlaying = false;
        bgmPaused = false;
        std::wcout << L"BGM已停止" << std::endl;
    }
}

void SoundManager::pauseBGM() {
    if (bgmPlaying && !bgmPaused) {
        std::wstring command = L"pause " + bgmAlias;
        mciSendString(command.c_str(), NULL, 0, NULL);
        bgmPaused = true;
        std::wcout << L"BGM已暂停" << std::endl;
    }
}

void SoundManager::resumeBGM() {
    if (bgmPlaying && bgmPaused) {
        std::wstring command = L"resume " + bgmAlias;
        mciSendString(command.c_str(), NULL, 0, NULL);
        bgmPaused = false;
        std::wcout << L"BGM已恢复" << std::endl;
    }
}

void SoundManager::setBGMVolume(int volume) {
    bgmVolume = volume;
    if (bgmPlaying) {
        setAudioVolume(bgmAlias, bgmVolume * masterVolume / 1000);
    }
}

void SoundManager::playSound(SoundType type) {
    // BGM不通过这个方法播放
    if (type == SOUND_BGM) {
        playBGM();
        return;
    }
    
    auto soundPath = soundPaths.find(type);
    if (soundPath != soundPaths.end()) {
        std::wstring path = stringToWString(soundPath->second);
        std::wstring alias = generateSoundEffectAlias();
        
        // 播放音效（不循环）
        if (playAudioFile(path, alias, false)) {
            setAudioVolume(alias, soundVolume * masterVolume / 1000);
            // 注意：音效播放完后会自动释放，不需要手动管理
        }
    }
}

void SoundManager::setSoundVolume(int volume) {
    soundVolume = volume;
}

void SoundManager::stopAll() {
    stopBGM();
    // 音效会自动播放完毕，不需要额外停止
}

void SoundManager::setMasterVolume(int volume) {
    masterVolume = volume;
    if (bgmPlaying) {
        setAudioVolume(bgmAlias, bgmVolume * masterVolume / 1000);
    }
}

bool SoundManager::playAudioFile(const std::wstring& filePath, const std::wstring& alias, bool loop) {
    std::wcout << L"[调试] playAudioFile: " << filePath << std::endl;
    std::wcout << L"[调试] alias: " << alias << L", loop: " << (loop ? L"是" : L"否") << std::endl;
    
    // 先关闭已存在的同名设备
    stopAudio(alias);
    
    // 打开音频文件
    std::wstring openCommand = L"open \"" + filePath + L"\" type waveaudio alias " + alias;
    std::wcout << L"[调试] MCI命令: " << openCommand << std::endl;
    
    MCIERROR error = mciSendString(openCommand.c_str(), NULL, 0, NULL);
    
    if (error != 0) {
        wchar_t errorMsg[256];
        mciGetErrorString(error, errorMsg, 256);
        std::wcerr << L"[错误] 无法打开音频文件: " << filePath << L", 错误码: " << error << L", 错误: " << errorMsg << std::endl;
        return false;
    }
    
    std::wcout << L"[成功] 文件打开成功" << std::endl;
    
    // 播放音频
    std::wstring playCommand = L"play " + alias;
    if (loop) {
        playCommand += L" repeat";
        std::wcout << L"[调试] 使用循环模式" << std::endl;
    }
    
    std::wcout << L"[调试] 播放命令: " << playCommand << std::endl;
    error = mciSendString(playCommand.c_str(), NULL, 0, NULL);
    
    if (error != 0) {
        wchar_t errorMsg[256];
        mciGetErrorString(error, errorMsg, 256);
        std::wcerr << L"[错误] 无法播放音频: " << alias << L", 错误码: " << error << L", 错误: " << errorMsg << std::endl;
        stopAudio(alias);
        return false;
    }
    
    std::wcout << L"[成功] 播放命令执行成功！" << std::endl;
    return true;
}

void SoundManager::stopAudio(const std::wstring& alias) {
    std::wstring stopCommand = L"stop " + alias;
    mciSendString(stopCommand.c_str(), NULL, 0, NULL);
    
    std::wstring closeCommand = L"close " + alias;
    mciSendString(closeCommand.c_str(), NULL, 0, NULL);
}

void SoundManager::setAudioVolume(const std::wstring& alias, int volume) {
    // MCI音量范围0-1000
    if (volume < 0) volume = 0;
    if (volume > 1000) volume = 1000;
    
    std::wcout << L"[调试] setAudioVolume: alias=" << alias << L", volume=" << volume << std::endl;
    
    std::wstringstream ss;
    ss << L"setaudio " << alias << L" volume to " << volume;
    
    std::wcout << L"[调试] MCI音量命令: " << ss.str() << std::endl;
    MCIERROR error = mciSendString(ss.str().c_str(), NULL, 0, NULL);
    
    if (error != 0) {
        wchar_t errorMsg[256];
        mciGetErrorString(error, errorMsg, 256);
        std::wcerr << L"[错误] 设置音量失败: " << errorMsg << std::endl;
    } else {
        std::wcout << L"[成功] 音量设置成功" << std::endl;
    }
}

std::wstring SoundManager::stringToWString(const std::string& str) {
    if (str.empty()) return std::wstring();
    
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring wstrTo(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstrTo[0], sizeNeeded);
    
    return wstrTo;
}

std::wstring SoundManager::generateSoundEffectAlias() {
    std::wstringstream ss;
    ss << L"SFX_" << soundEffectCounter++;
    return ss.str();
}
