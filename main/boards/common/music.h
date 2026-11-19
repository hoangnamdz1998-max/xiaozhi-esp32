#ifndef MUSIC_H
#define MUSIC_H

#include <string>

class Music {
public:
    virtual ~Music() = default;  // 添加虚析构函数
    
    virtual bool Download(const std::string& song_name, const std::string& artist_name = "") = 0;
    virtual std::string GetDownloadResult() = 0;
    
    // 新增流式播放相关方法
    virtual bool StartStreaming(const std::string& music_url) = 0;
    virtual bool StartStreamingDirect(const std::string& audio_url, const std::string& song_name, const std::string& artist_name, const std::string& lyric_url = "") {
        (void)audio_url; (void)song_name; (void)artist_name; (void)lyric_url;
        return false;
    }
    virtual bool StopStreaming() = 0;  // 停止流式播放
    virtual size_t GetBufferSize() const = 0;
    virtual bool IsDownloading() const = 0;
    virtual bool IsPlaying() const = 0;
    virtual void Pause() {}
    virtual void Resume() {}
    virtual bool IsPaused() const { return false; }
    virtual uint32_t GetCurrentPlayTimeMs() const { return 0; }
    virtual int16_t* GetAudioData() = 0;
    virtual void GetVisualizerBands(uint8_t bands[16]) {
        for (int i = 0; i < 16; ++i) bands[i] = 0;
    }
};

#endif // MUSIC_H 