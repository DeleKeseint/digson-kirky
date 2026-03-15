#define _GNU_SOURCE
#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

// 简单的 MP3 播放器实现
// 需要链接 -lmpg123 -lao 或类似库

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "digson-audio"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...) printf(__VA_ARGS__); printf("\n")
#define LOGE(...) fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n")
#endif

extern "C" {
#include <mpg123.h>
#include <ao/ao.h>
}

#define PROGRAM_NAME "audio"
#define VERSION "1.0"

class MP3Player {
private:
    mpg123_handle *mh;
    ao_device *dev;
    ao_sample_format format;
    int channels, encoding;
    long rate;
    size_t buffer_size;
    unsigned char *buffer;
    bool initialized;
    
    int audio_volume;
    bool use_wolfson;
    
    void load_audio_config() {
        audio_volume = 50; // 默认值
        use_wolfson = false;
        
        // 检测是否为 Wolfson/WM8994 设备
        FILE *fp = fopen("/proc/asound/cards", "r");
        if (fp) {
            char line[256];
            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "WM8994") || strstr(line, "Herring")) {
                    use_wolfson = true;
                    break;
                }
            }
            fclose(fp);
        }
        
        // 如果不是 WM8994，检查是否为 i9000
        if (!use_wolfson) {
            fp = fopen("/proc/cpuinfo", "r");
            if (fp) {
                char line[256];
                while (fgets(line, sizeof(line), fp)) {
                    if (strstr(line, "Hummingbird") || strstr(line, "S5PC110") || strstr(line, "S5PV210")) {
                        use_wolfson = true;
                        break;
                    }
                }
                fclose(fp);
            }
        }
        
        // 根据芯片类型加载对应配置
        if (use_wolfson) {
            load_wolfson_config();
        } else {
            load_apo_config();
        }
    }
    
    void load_wolfson_config() {
        const char *paths[] = {
            "/data/data/com.termux/files/usr/etc/digson/WolfsonConfig",
            "/etc/WolfsonConfig",
            nullptr
        };
        
        for (int i = 0; paths[i]; i++) {
            std::ifstream file(paths[i]);
            if (file.is_open()) {
                std::string line;
                while (std::getline(file, line)) {
                    if (line[0] == '#') continue;
                    size_t pos = line.find("VOLUME=");
                    if (pos != std::string::npos) {
                        audio_volume = std::atoi(line.c_str() + pos + 7);
                        if (audio_volume < 0) audio_volume = 0;
                        if (audio_volume > 100) audio_volume = 100;
                    }
                }
                file.close();
                LOGI("Loaded Wolfson config from %s, volume=%d", paths[i], audio_volume);
                return;
            }
        }
    }
    
    void load_apo_config() {
        const char *paths[] = {
            "/data/data/com.termux/files/usr/etc/digson/APOConfig",
            "/etc/APOConfig",
            nullptr
        };
        
        for (int i = 0; paths[i]; i++) {
            std::ifstream file(paths[i]);
            if (file.is_open()) {
                std::string line;
                while (std::getline(file, line)) {
                    if (line[0] == '#') continue;
                    size_t pos = line.find("VOLUME=");
                    if (pos != std::string::npos) {
                        audio_volume = std::atoi(line.c_str() + pos + 7);
                        if (audio_volume < 0) audio_volume = 0;
                        if (audio_volume > 100) audio_volume = 100;
                    }
                }
                file.close();
                LOGI("Loaded APO config from %s, volume=%d", paths[i], audio_volume);
                return;
            }
        }
    }
    
    void apply_volume(unsigned char *data, size_t size) {
        // 简单的音量调节（16位样本）
        float vol_factor = audio_volume / 100.0f;
        for (size_t i = 0; i < size; i += 2) {
            if (i + 1 < size) {
                short sample = (short)((data[i+1] << 8) | data[i]);
                sample = (short)(sample * vol_factor);
                data[i] = sample & 0xFF;
                data[i+1] = (sample >> 8) & 0xFF;
            }
        }
    }
    
public:
    MP3Player() : mh(nullptr), dev(nullptr), buffer(nullptr), 
                  initialized(false), audio_volume(50), use_wolfson(false) {
        load_audio_config();
    }
    
    ~MP3Player() {
        cleanup();
    }
    
    bool initialize() {
        int err;
        
        err = mpg123_init();
        if (err != MPG123_OK) {
            LOGE("mpg123_init failed: %s", mpg123_plain_strerror(err));
            return false;
        }
        
        mh = mpg123_new(nullptr, &err);
        if (!mh) {
            LOGE("mpg123_new failed: %s", mpg123_plain_strerror(err));
            mpg123_exit();
            return false;
        }
        
        ao_initialize();
        
        buffer_size = mpg123_outblock(mh);
        buffer = new unsigned char[buffer_size];
        
        initialized = true;
        LOGI("Audio player initialized (%s, volume: %d%%)", 
             use_wolfson ? "Wolfson WM8994" : "Realtek APO", audio_volume);
        return true;
    }
    
    bool play(const char *filename) {
        if (!initialized) {
            LOGE("Player not initialized");
            return false;
        }
        
        int err = mpg123_open(mh, filename);
        if (err != MPG123_OK) {
            LOGE("Cannot open %s: %s", filename, mpg123_strerror(mh));
            return false;
        }
        
        mpg123_getformat(mh, &rate, &channels, &encoding);
        
        int driver = ao_default_driver_id();
        
        memset(&format, 0, sizeof(format));
        format.bits = mpg123_encsize(encoding) * 8;
        format.channels = channels;
        format.rate = rate;
        format.byte_format = AO_FMT_NATIVE;
        
        dev = ao_open_live(driver, &format, nullptr);
        if (!dev) {
            LOGE("Cannot open audio device");
            mpg123_close(mh);
            return false;
        }
        
        LOGI("Playing: %s (%ldHz, %dch)", filename, rate, channels);
        
        size_t done;
        bool playing = true;
        
        while (playing && mpg123_read(mh, buffer, buffer_size, &done) == MPG123_OK) {
            if (done > 0) {
                // 应用音量
                apply_volume(buffer, done);
                ao_play(dev, (char*)buffer, done);
            }
        }
        
        LOGI("Playback finished");
        return true;
    }
    
    void cleanup() {
        if (dev) {
            ao_close(dev);
            dev = nullptr;
        }
        if (mh) {
            mpg123_close(mh);
            mpg123_delete(mh);
            mh = nullptr;
        }
        if (buffer) {
            delete[] buffer;
            buffer = nullptr;
        }
        ao_shutdown();
        mpg123_exit();
        initialized = false;
    }
};

void print_usage(const char *prog) {
    std::cout << "Usage: " << prog << " <mp3-file>\n";
    std::cout << "       " << prog << " --version\n";
    std::cout << "       " << prog << " --help\n";
    std::cout << "\n";
    std::cout << "MP3 player with audio configuration support\n";
    std::cout << "Auto-detects Wolfson WM8994 (Galaxy S i9000) or Realtek APO\n";
    std::cout << "Reads volume from WolfsonConfig or APOConfig\n";
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    if (strcmp(argv[1], "--version") == 0) {
        std::cout << PROGRAM_NAME " version " VERSION "\n";
        return 0;
    }
    
    if (strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }
    
    const char *filename = argv[1];
    
    // 检查文件
    struct stat st;
    if (stat(filename, &st) != 0) {
        LOGE("Cannot access file: %s", filename);
        return 1;
    }
    
    MP3Player player;
    if (!player.initialize()) {
        LOGE("Failed to initialize audio player");
        return 1;
    }
    
    if (!player.play(filename)) {
        return 1;
    }
    
    return 0;
}
