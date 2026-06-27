#pragma once
#include <string>
#include <map>
#include <Windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

// 音频类型枚举
enum SoundType {
    SOUND_BGM,           // 背景音乐
    SOUND_SELECT,        // 选择棋子
    SOUND_MOVE,          // 移动棋子
    SOUND_SHOOT,         // 射箭
    SOUND_COUNT_0,       // 倒计时音效 0-9
    SOUND_COUNT_1,
    SOUND_COUNT_2,
    SOUND_COUNT_3,
    SOUND_COUNT_4,
    SOUND_COUNT_5,
    SOUND_COUNT_6,
    SOUND_COUNT_7,
    SOUND_COUNT_8,
    SOUND_COUNT_9
};

class SoundManager {
public:
    SoundManager();
    ~SoundManager();

    // 初始化音频系统，加载所有音频文件
    bool initialize(const std::string& soundFolder = "sound/");

    // BGM控制
    void playBGM();                    // 开始播放BGM（循环）
    void stopBGM();                    // 停止BGM
    void pauseBGM();                   // 暂停BGM
    void resumeBGM();                  // 恢复BGM
    void setBGMVolume(int volume);     // 设置BGM音量 (0-1000)

    // 音效播放（不会打断BGM）
    void playSound(SoundType type);    // 播放指定音效
    void setSoundVolume(int volume);   // 设置音效音量 (0-1000)

    // 全局控制
    void stopAll();                    // 停止所有音频
    void setMasterVolume(int volume);  // 设置主音量 (0-1000)

    // 获取音量
    int getBGMVolume() const { return bgmVolume; }
    int getSoundVolume() const { return soundVolume; }

private:
    // 音频文件路径映射
    std::map<SoundType, std::string> soundPaths;
    
    // BGM设备别名（用于MCI命令）
    std::wstring bgmAlias;
    
    // 音效设备别名计数器（为每个音效创建独立的播放实例）
    int soundEffectCounter;
    
    // 音量设置
    int bgmVolume;
    int soundVolume;
    int masterVolume;
    
    // BGM状态
    bool bgmPlaying;
    bool bgmPaused;
    
    // 初始化音频路径
    void initializeSoundPaths(const std::string& soundFolder);
    
    // 播放音频文件（内部使用）
    bool playAudioFile(const std::wstring& filePath, const std::wstring& alias, bool loop = false);
    
    // 停止指定别名的音频
    void stopAudio(const std::wstring& alias);
    
    // 设置指定别名音频的音量
    void setAudioVolume(const std::wstring& alias, int volume);
    
    // 字符串转换辅助函数
    std::wstring stringToWString(const std::string& str);
    
    // 生成唯一的音效别名
    std::wstring generateSoundEffectAlias();
};
