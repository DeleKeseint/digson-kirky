#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROGRAM_NAME "have"

/* 支持的包管理器检测命令 */
typedef struct {
    const char *name;
    const char *check_cmd;
    const char *query_format;
} pkg_manager_t;

static pkg_manager_t managers[] = {
    {"dpkg", "dpkg -l \"%s\" 2>/dev/null | grep -q \"^ii\"", "dpkg -l \"%s\""},
    {"rpm", "rpm -q \"%s\" >/dev/null 2>&1", "rpm -q \"%s\""},
    {"pacman", "pacman -Q \"%s\" >/dev/null 2>&1", "pacman -Q \"%s\""},
    {"apk", "apk info -e \"%s\" >/dev/null 2>&1", "apk info \"%s\""},
    {"portage", "equery list \"%s\" >/dev/null 2>&1", "equery list \"%s\""},
    {"zypper", "zypper se -i \"%s\" 2>/dev/null | grep -q \"^i\"", "zypper se -i \"%s\""},
    {"xbps", "xbps-query -S \"%s\" >/dev/null 2>&1", "xbps-query \"%s\""},
    {"nix", "nix-env -q \"%s\" 2>/dev/null | grep -q .", "nix-env -q \"%s\""},
    {"pkg", "pkg info -e \"%s\"", "pkg info \"%s\""},
    {"brew", "brew list \"%s\" >/dev/null 2>&1", "brew list \"%s\""},
    {"port", "port installed \"%s\" 2>/dev/null | grep -q \"active\"", "port installed \"%s\""},
    {"pkg_info", "pkg_info -e \"%s\" >/dev/null 2>&1", "pkg_info \"%s\""},
    {"pkgin", "pkgin list 2>/dev/null | grep -q \"^%s-\"", "pkgin list | grep \"%s\""},
    {"slackpkg", "ls /var/log/packages/\"%s\"* >/dev/null 2>&1", "ls /var/log/packages/\"%s\"*"},
    {"command", "command -v \"%s\" >/dev/null 2>&1", "command -v \"%s\""},
    {NULL, NULL, NULL}
};

/* ==================== 新增：硬件检测 ==================== */

static int cmd_exists(const char *cmd) {
    char check[256];
    snprintf(check, sizeof(check), "which %s >/dev/null 2>&1", cmd);
    return system(check) == 0;
}

static int file_exists(const char *path) {
    return access(path, F_OK) == 0;
}

// 检测 Wolfson WM8994
static int has_wm8994(void) {
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
    if (file_exists("/dev/snd/pcmC0D0p") && file_exists("/dev/snd/pcmC0D0c")) {
        return 1;
    }
    return 0;
}

// 检测 Realtek APO
static int has_realtek_apo(void) {
    if (file_exists("/system/lib/librealtekapo.so")) return 1;
    if (file_exists("/vendor/lib/librealtekapo.so")) return 1;
    if (file_exists("/system/etc/realtek_apo.conf")) return 1;
    
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

// 检测 ALSA
static int has_alsa(void) {
    return cmd_exists("alsamixer") || cmd_exists("amixer") || cmd_exists("aplay");
}

// 检测 Galaxy S i9000
static int is_i9000(void) {
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) return 0;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "Hummingbird") || strstr(line, "S5PC110") || strstr(line, "S5PV210")) {
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    
    fp = popen("getprop ro.product.model 2>/dev/null", "r");
    if (fp) {
        char model[256];
        if (fgets(model, sizeof(model), fp)) {
            if (strstr(model, "GT-I9000") || strstr(model, "Galaxy S")) {
                pclose(fp);
                return 1;
            }
        }
        pclose(fp);
    }
    return 0;
}

/* ==================== 原有代码保持不变 ==================== */

static pkg_manager_t* detect_package_manager(void) {
    if (access("/usr/bin/dpkg", X_OK) == 0) return &managers[0];
    if (access("/usr/bin/pacman", X_OK) == 0) return &managers[2];
    if (access("/sbin/apk", X_OK) == 0) return &managers[3];
    if (access("/usr/bin/rpm", X_OK) == 0) return &managers[1];
    if (access("/usr/bin/equery", X_OK) == 0 || access("/usr/bin/emerge", X_OK) == 0) return &managers[4];
    if (access("/usr/bin/zypper", X_OK) == 0) return &managers[5];
    if (access("/usr/bin/xbps-query", X_OK) == 0) return &managers[6];
    if (access("/run/current-system/nixos-version", F_OK) == 0 || getenv("NIX_PATH") != NULL) return &managers[7];
    if (access("/usr/sbin/pkg", X_OK) == 0) return &managers[8];
    if (access("/opt/homebrew/bin/brew", X_OK) == 0 || access("/usr/local/bin/brew", X_OK) == 0) return &managers[9];
    if (access("/opt/local/bin/port", X_OK) == 0) return &managers[10];
    if (access("/usr/sbin/pkg_info", X_OK) == 0) return &managers[11];
    if (access("/usr/pkg/bin/pkgin", X_OK) == 0) return &managers[12];
    if (access("/var/log/packages", F_OK) == 0) return &managers[13];
    return &managers[14];
}

static int check_package(const char *package, pkg_manager_t *pm) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), pm->check_cmd, package);
    int ret = system(cmd);
    if (WIFEXITED(ret)) return WEXITSTATUS(ret) == 0;
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <package-name>\n", PROGRAM_NAME);
        fprintf(stderr, "       %s <command-name>\n", PROGRAM_NAME);
        fprintf(stderr, "       %s wm8994      (check for Wolfson WM8994 chip)\n", PROGRAM_NAME);
        fprintf(stderr, "       %s realtek-apo (check for Realtek APO)\n", PROGRAM_NAME);
        fprintf(stderr, "       %s alsa        (check for ALSA tools)\n", PROGRAM_NAME);
        fprintf(stderr, "       %s i9000       (check for Galaxy S i9000)\n", PROGRAM_NAME);
        fprintf(stderr, "\n");
        fprintf(stderr, "Check if a package, command, or hardware component is available.\n");
        fprintf(stderr, "Exit codes: 0=installed, 1=not installed, 2=error\n");
        return 2;
    }
    
    const char *target = argv[1];
    
    /* ==================== 新增：硬件检测 ==================== */
    
    if (strcmp(target, "wm8994") == 0) {
        if (has_wm8994()) {
            printf("Yes, 'wm8994' is installed (detected via /proc/asound/cards)\n");
            return 0;
        } else {
            printf("No, 'wm8994' is not installed\n");
            return 1;
        }
    }
    
    if (strcmp(target, "realtek-apo") == 0) {
        if (has_realtek_apo()) {
            printf("Yes, 'realtek-apo' is installed (detected via system libraries)\n");
            return 0;
        } else {
            printf("No, 'realtek-apo' is not installed\n");
            return 1;
        }
    }
    
    if (strcmp(target, "alsa") == 0) {
        if (has_alsa()) {
            printf("Yes, 'alsa' is installed (detected via command)\n");
            return 0;
        } else {
            printf("No, 'alsa' is not installed\n");
            return 1;
        }
    }
    
    if (strcmp(target, "i9000") == 0) {
        if (is_i9000()) {
            printf("Yes, 'i9000' is installed (detected via /proc/cpuinfo)\n");
            return 0;
        } else {
            printf("No, 'i9000' is not installed\n");
            return 1;
        }
    }
    
    /* ==================== 原有：包管理器检测 ==================== */
    
    pkg_manager_t *pm = detect_package_manager();
    int installed = check_package(target, pm);
    
    if (installed) {
        printf("Yes, '%s' is installed (detected via %s)\n", target, pm->name);
        return 0;
    } else {
        printf("No, '%s' is not installed\n", target);
        if (pm->name[0] != 'c') {
            pkg_manager_t *cmd_pm = &managers[14];
            if (check_package(target, cmd_pm)) {
                printf("But '%s' command is available in PATH\n", target);
                return 0;
            }
        }
        return 1;
    }
}
