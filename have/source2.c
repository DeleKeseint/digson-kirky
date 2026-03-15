#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// 检测命令是否存在
int command_exists(const char *cmd) {
    char path[256];
    snprintf(path, sizeof(path), "which %s > /dev/null 2>&1", cmd);
    return system(path) == 0;
}

// 检测文件是否存在
int file_exists(const char *path) {
    return access(path, F_OK) == 0;
}

// 检测 WM8994 音频芯片
int check_wm8994() {
    // 方法1: 检查 /proc/asound/cards
    FILE *fp = fopen("/proc/asound/cards", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "WM8994") || strstr(line, "Herring")) {
                fclose(fp);
                return 1;
            }
        }
        fclose(fp);
    }
    
    // 方法2: 检查 ALSA 设备节点
    if (file_exists("/dev/snd/pcmC0D0p") && file_exists("/dev/snd/pcmC0D0c")) {
        return 1;
    }
    
    return 0;
}

// 检测 Realtek APO
int check_realtek_apo() {
    // 检查 Realtek APO 配置文件或组件
    if (file_exists("/system/lib/soundfx/librealtak.so")) {
        return 1;
    }
    if (file_exists("/vendor/lib/soundfx/librealtak.so")) {
        return 1;
    }
    // 检查属性
    FILE *fp = popen("getprop ro.hardware.audio 2>/dev/null | grep -i realtek", "r");
    if (fp) {
        char result[256];
        if (fgets(result, sizeof(result), fp)) {
            pclose(fp);
            return 1;
        }
        pclose(fp);
    }
    return 0;
}

// 检测 ALSA 工具
int check_alsa() {
    return command_exists("alsamixer") || command_exists("amixer") || command_exists("aplay");
}

// 检测 Galaxy S i9000
int check_i9000() {
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) return 0;
    
    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "Hummingbird") || 
            strstr(line, "S5PC110") ||
            strstr(line, "S5PV210") ||
            strstr(line, "GT-I9000")) {
            found = 1;
            break;
        }
    }
    fclose(fp);
    
    // 额外检查机型属性
    if (!found) {
        fp = popen("getprop ro.product.model 2>/dev/null", "r");
        if (fp) {
            char model[256];
            if (fgets(model, sizeof(model), fp)) {
                if (strstr(model, "GT-I9000") || strstr(model, "Galaxy S")) {
                    found = 1;
                }
            }
            pclose(fp);
        }
    }
    
    return found;
}

// 检测音频命令
int check_audio() {
    return command_exists("audio");
}

// 主函数
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: have <package/command>\n");
        printf("Supported checks:\n");
        printf("  wm8994      - Wolfson WM8994 audio chip\n");
        printf("  realtek-apo - Realtek Audio Processing Object\n");
        printf("  alsa        - ALSA audio utilities\n");
        printf("  i9000       - Samsung Galaxy S i9000 device\n");
        printf("  audio       - audio playback command\n");
        printf("  <command>   - any system command\n");
        return 1;
    }
    
    const char *target = argv[1];
    int found = 0;
    char detection_method[256] = "detected via command";
    
    // 内置检测
    if (strcmp(target, "wm8994") == 0) {
        found = check_wm8994();
        strcpy(detection_method, "detected via /proc/asound/cards");
    }
    else if (strcmp(target, "realtek-apo") == 0) {
        found = check_realtek_apo();
        strcpy(detection_method, "detected via system libraries");
    }
    else if (strcmp(target, "alsa") == 0) {
        found = check_alsa();
        strcpy(detection_method, "detected via command");
    }
    else if (strcmp(target, "i9000") == 0) {
        found = check_i9000();
        strcpy(detection_method, "detected via /proc/cpuinfo");
    }
    else if (strcmp(target, "audio") == 0) {
        found = check_audio();
        strcpy(detection_method, "detected via command");
    }
    else {
        // 通用命令检测
        found = command_exists(target);
        strcpy(detection_method, "detected via command");
    }
    
    if (found) {
        printf("Yes, '%s' is installed (%s)\n", target, detection_method);
        return 0;
    } else {
        printf("No, '%s' is not installed\n", target);
        return 1;
    }
}
