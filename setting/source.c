#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ncurses.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/route.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <dirent.h>
#include <pwd.h>
#include <ctype.h>

#define PROGRAM_NAME "setting"
#define VERSION "1.0"
#define WIFI_SCAN_RANGE_SHORT 1000
#define WIFI_SCAN_RANGE_LONG 10000

/* 真实系统状态 */
typedef struct {
    int flight_mode;           /* 飞行模式 - 通过 Android Settings 或 termux-api */
    int nfc;                   /* NFC 状态 - 需要 root 或 termux-api */
    int wifi_enabled;          /* WiFi 是否开启 */
    int bluetooth_enabled;     /* 蓝牙是否开启 */
    int mobile_data_enabled;   /* 移动数据是否开启 */
    int hotspot_enabled;       /* 热点是否开启 */
    int vpn_enabled;           /* VPN 是否连接 */
    char current_ssid[256];    /* 当前 WiFi 名称 */
    char current_bssid[32];    /* 当前 WiFi MAC */
    int wifi_signal;           /* WiFi 信号强度 */
    char local_ip[64];         /* 本地 IP */
    char public_ip[64];        /* 公网 IP */
    char dns1[64];             /* 主 DNS */
    char dns2[64];             /* 备用 DNS */
    char interface_name[32];   /* 当前网络接口 */
    int sim_count;             /* SIM 卡数量 */
    char operator_name[64];    /* 运营商名称 */
    int mobile_signal;         /* 移动信号强度 */
} system_settings_t;

static system_settings_t settings = {0};
static int long_range = 0;

typedef enum {
    MENU_FLIGHT_MODE,
    MENU_WLAN,
    MENU_BLUETOOTH,
    MENU_MOBILE_NETWORK,
    MENU_HOTSPOT,
    MENU_VPN,
    MENU_DNS,
    MENU_COUNT
} menu_item_t;

static const char *menu_names[] = {
    "Flight mode",
    "WLAN settings",
    "Bluetooth setting",
    "Mobile Network",
    "Personal hotspot",
    "Network ladder settings",
    "Private DNS settings"
};

static int current_selection = 0;

/* 执行命令并获取输出 */
static int exec_cmd(const char *cmd, char *output, size_t output_size) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    
    if (fgets(output, output_size, fp) == NULL) {
        pclose(fp);
        return -1;
    }
    
    /* 移除换行符 */
    output[strcspn(output, "\n")] = '\0';
    pclose(fp);
    return 0;
}

/* 检查命令是否存在 */
static int cmd_exists(const char *cmd) {
    char check[256];
    snprintf(check, sizeof(check), "which %s >/dev/null 2>&1", cmd);
    return system(check) == 0;
}

/* 检查 termux-api 是否安装 */
static int check_termux_api(void) {
    return cmd_exists("termux-wifi-connectioninfo") ||
           cmd_exists("termux-battery-status");
}

/* 获取真实 WiFi 状态 - 多种方法 */
static void get_real_wifi_status(void) {
    settings.wifi_enabled = 0;
    settings.current_ssid[0] = '\0';
    settings.wifi_signal = 0;
    
    /* 方法1: termux-api (最准确) */
    if (cmd_exists("termux-wifi-connectioninfo")) {
        FILE *fp = popen("termux-wifi-connectioninfo 2>/dev/null", "r");
        if (fp) {
            char line[512];
            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "\"ssid\":")) {
                    char *start = strchr(line, '"');
                    if (start) {
                        start = strchr(start + 1, '"');
                        if (start) {
                            start++;
                            char *end = strchr(start, '"');
                            if (end) {
                                *end = '\0';
                                strncpy(settings.current_ssid, start, sizeof(settings.current_ssid) - 1);
                                settings.wifi_enabled = 1;
                            }
                        }
                    }
                }
                if (strstr(line, "\"rssi\":")) {
                    int rssi = 0;
                    sscanf(line, "%*[^0-9]%d", &rssi);
                    settings.wifi_signal = rssi;
                }
            }
            pclose(fp);
        }
    }
    
    /* 方法2: 通过 /proc/net/wireless */
    if (!settings.wifi_enabled) {
        FILE *fp = fopen("/proc/net/wireless", "r");
        if (fp) {
            char line[256];
            fgets(line, sizeof(line), fp); /* 跳过标题 */
            fgets(line, sizeof(line), fp); /* 跳过分隔符 */
            if (fgets(line, sizeof(line), fp)) {
                char iface[32];
                int status, link, level, noise;
                if (sscanf(line, "%s %d %d %d %d", iface, &status, &link, &level, &noise) >= 3) {
                    if (link > 0) {
                        settings.wifi_enabled = 1;
                        settings.wifi_signal = level;
                        /* 从接口名去除冒号 */
                        char *colon = strchr(iface, ':');
                        if (colon) *colon = '\0';
                        strncpy(settings.interface_name, iface, sizeof(settings.interface_name) - 1);
                    }
                }
            }
            fclose(fp);
        }
    }
    
    /* 方法3: 检查网络接口状态 */
    if (!settings.wifi_enabled) {
        struct ifaddrs *ifaddr;
        if (getifaddrs(&ifaddr) == 0) {
            for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
                if (ifa->ifa_name && strncmp(ifa->ifa_name, "wlan", 4) == 0) {
                    if (ifa->ifa_addr && ifa->ifa_flags & IFF_UP) {
                        settings.wifi_enabled = 1;
                        strncpy(settings.interface_name, ifa->ifa_name, 
                               sizeof(settings.interface_name) - 1);
                        break;
                    }
                }
            }
            freeifaddrs(ifaddr);
        }
    }
}

/* 获取真实蓝牙状态 */
static void get_real_bluetooth_status(void) {
    settings.bluetooth_enabled = 0;
    
    /* 方法1: bluetoothctl */
    if (cmd_exists("bluetoothctl")) {
        FILE *fp = popen("bluetoothctl show 2>/dev/null | grep Powered", "r");
        if (fp) {
            char line[256];
            if (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "yes")) {
                    settings.bluetooth_enabled = 1;
                }
            }
            pclose(fp);
        }
    }
    
    /* 方法2: 检查 /sys/class/bluetooth */
    if (!settings.bluetooth_enabled) {
        DIR *dir = opendir("/sys/class/bluetooth");
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_name[0] == '.') continue;
                
                char path[256];
                snprintf(path, sizeof(path), "/sys/class/bluetooth/%s/rfkill*/state", entry->d_name);
                
                FILE *fp = fopen(path, "r");
                if (!fp) {
                    /* 尝试另一种路径 */
                    snprintf(path, sizeof(path), "/sys/class/bluetooth/%s/device/rfkill*/state", entry->d_name);
                    fp = fopen(path, "r");
                }
                
                if (fp) {
                    int state;
                    if (fscanf(fp, "%d", &state) == 1 && state == 1) {
                        settings.bluetooth_enabled = 1;
                    }
                    fclose(fp);
                    if (settings.bluetooth_enabled) break;
                }
            }
            closedir(dir);
        }
    }
    
    /* 方法3: hciconfig */
    if (!settings.bluetooth_enabled && cmd_exists("hciconfig")) {
        FILE *fp = popen("hciconfig 2>/dev/null | grep RUNNING", "r");
        if (fp) {
            if (fgets((char[256]){0}, 256, fp)) {
                settings.bluetooth_enabled = 1;
            }
            pclose(fp);
        }
    }
}

/* 获取移动网络状态 */
static void get_real_mobile_status(void) {
    settings.mobile_data_enabled = 0;
    settings.sim_count = 0;
    settings.operator_name[0] = '\0';
    settings.mobile_signal = 0;
    
    /* 方法1: termux-telephony-deviceinfo */
    if (cmd_exists("termux-telephony-deviceinfo")) {
        FILE *fp = popen("termux-telephony-deviceinfo 2>/dev/null", "r");
        if (fp) {
            char line[512];
            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "\"network_operator_name\":")) {
                    char *start = strchr(line, '"');
                    if (start) {
                        start = strchr(start + 1, '"');
                        if (start) {
                            start++;
                            char *end = strchr(start, '"');
                            if (end) {
                                *end = '\0';
                                strncpy(settings.operator_name, start, sizeof(settings.operator_name) - 1);
                                settings.sim_count = 1;
                            }
                        }
                    }
                }
                if (strstr(line, "\"sim_state\":")) {
                    if (strstr(line, "READY") || strstr(line, "LOADED")) {
                        settings.sim_count = 1;
                    }
                }
            }
            pclose(fp);
        }
    }
    
    /* 方法2: 检查 rmnet 接口 (移动数据接口) */
    struct ifaddrs *ifaddr;
    if (getifaddrs(&ifaddr) == 0) {
        for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
            if (ifa->ifa_name && (strncmp(ifa->ifa_name, "rmnet", 5) == 0 ||
                                  strncmp(ifa->ifa_name, "ccmni", 5) == 0)) {
                if (ifa->ifa_flags & IFF_UP) {
                    settings.mobile_data_enabled = 1;
                    if (!settings.interface_name[0]) {
                        strncpy(settings.interface_name, ifa->ifa_name, 
                               sizeof(settings.interface_name) - 1);
                    }
                }
            }
        }
        freeifaddrs(ifaddr);
    }
}

/* 获取热点状态 */
static void get_real_hotspot_status(void) {
    settings.hotspot_enabled = 0;
    
    /* 检查 ap0 或类似接口 */
    struct ifaddrs *ifaddr;
    if (getifaddrs(&ifaddr) == 0) {
        for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
            if (ifa->ifa_name && (strstr(ifa->ifa_name, "ap") ||
                                  strstr(ifa->ifa_name, "hotspot"))) {
                if (ifa->ifa_flags & IFF_UP) {
                    settings.hotspot_enabled = 1;
                }
            }
        }
        freeifaddrs(ifaddr);
    }
    
    /* 方法2: 检查 Android 属性 */
    if (!settings.hotspot_enabled) {
        FILE *fp = popen("getprop wifi.ap.status 2>/dev/null || getprop tethering.status 2>/dev/null", "r");
        if (fp) {
            char status[32];
            if (fgets(status, sizeof(status), fp)) {
                if (strstr(status, "on") || strstr(status, "enabled")) {
                    settings.hotspot_enabled = 1;
                }
            }
            pclose(fp);
        }
    }
}

/* 获取 VPN 状态 */
static void get_real_vpn_status(void) {
    settings.vpn_enabled = 0;
    
    /* 方法1: 检查 tun 接口 */
    struct ifaddrs *ifaddr;
    if (getifaddrs(&ifaddr) == 0) {
        for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
            if (ifa->ifa_name && strncmp(ifa->ifa_name, "tun", 3) == 0) {
                if (ifa->ifa_flags & IFF_UP) {
                    settings.vpn_enabled = 1;
                    strncpy(settings.interface_name, ifa->ifa_name,
                           sizeof(settings.interface_name) - 1);
                }
            }
            if (ifa->ifa_name && strncmp(ifa->ifa_name, "ppp", 3) == 0) {
                if (ifa->ifa_flags & IFF_UP) {
                    settings.vpn_enabled = 1;
                }
            }
        }
        freeifaddrs(ifaddr);
    }
    
    /* 方法2: 检查路由表中的 VPN 路由 */
    if (!settings.vpn_enabled) {
        FILE *fp = fopen("/proc/net/route", "r");
        if (fp) {
            char line[256];
            fgets(line, sizeof(line), fp); /* 跳过标题 */
            while (fgets(line, sizeof(line), fp)) {
                char iface[32];
                unsigned int dest, gateway, flags;
                if (sscanf(line, "%s %x %x %x", iface, &dest, &gateway, &flags) == 4) {
                    if (strncmp(iface, "tun", 3) == 0 && (flags & RTF_UP)) {
                        settings.vpn_enabled = 1;
                        break;
                    }
                }
            }
            fclose(fp);
        }
    }
}

/* 获取 DNS 设置 */
static void get_real_dns_settings(void) {
    settings.dns1[0] = '\0';
    settings.dns2[0] = '\0';
    
    /* 方法1: 读取 resolv.conf */
    FILE *fp = fopen("/data/data/com.termux/files/usr/etc/resolv.conf", "r");
    if (!fp) fp = fopen("/system/etc/resolv.conf", "r");
    if (!fp) fp = fopen("/etc/resolv.conf", "r");
    
    if (fp) {
        char line[256];
        int dns_count = 0;
        while (fgets(line, sizeof(line), fp) && dns_count < 2) {
            char ns[64];
            if (sscanf(line, "nameserver %63s", ns) == 1) {
                if (dns_count == 0) {
                    strncpy(settings.dns1, ns, sizeof(settings.dns1) - 1);
                } else {
                    strncpy(settings.dns2, ns, sizeof(settings.dns2) - 1);
                }
                dns_count++;
            }
        }
        fclose(fp);
    }
    
    /* 方法2: 通过 getprop 获取 Android DNS */
    if (!settings.dns1[0]) {
        FILE *fp = popen("getprop net.dns1 2>/dev/null", "r");
        if (fp) {
            fgets(settings.dns1, sizeof(settings.dns1), fp);
            settings.dns1[strcspn(settings.dns1, "\n")] = '\0';
            pclose(fp);
        }
    }
    if (!settings.dns2[0]) {
        FILE *fp = popen("getprop net.dns2 2>/dev/null", "r");
        if (fp) {
            fgets(settings.dns2, sizeof(settings.dns2), fp);
            settings.dns2[strcspn(settings.dns2, "\n")] = '\0';
            pclose(fp);
        }
    }
}

/* 获取本地 IP */
static void get_local_ip(void) {
    settings.local_ip[0] = '\0';
    
    struct ifaddrs *ifaddr;
    if (getifaddrs(&ifaddr) == 0) {
        for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
                /* 跳过 loopback */
                if (strncmp(ifa->ifa_name, "lo", 2) == 0) continue;
                
                struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
                inet_ntop(AF_INET, &sin->sin_addr, settings.local_ip, sizeof(settings.local_ip));
                
                if (!settings.interface_name[0]) {
                    strncpy(settings.interface_name, ifa->ifa_name,
                           sizeof(settings.interface_name) - 1);
                }
                break;
            }
        }
        freeifaddrs(ifaddr);
    }
}

/* 获取公网 IP */
static void get_public_ip(void) {
    settings.public_ip[0] = '\0';
    
    /* 使用多种服务获取公网 IP */
    const char *services[] = {
        "curl -s https://api.ipify.org 2>/dev/null",
        "curl -s http://ifconfig.me 2>/dev/null",
        "curl -s http://icanhazip.com 2>/dev/null",
        NULL
    };
    
    for (int i = 0; services[i]; i++) {
        FILE *fp = popen(services[i], "r");
        if (fp) {
            char ip[64];
            if (fgets(ip, sizeof(ip), fp)) {
                ip[strcspn(ip, "\n")] = '\0';
                /* 验证 IP 格式 */
                struct sockaddr_in sa;
                if (inet_pton(AF_INET, ip, &sa.sin_addr) == 1) {
                    strncpy(settings.public_ip, ip, sizeof(settings.public_ip) - 1);
                    pclose(fp);
                    return;
                }
            }
            pclose(fp);
        }
    }
}

/* 获取飞行模式状态 */
static void get_flight_mode_status(void) {
    settings.flight_mode = 0;
    
    /* 方法1: termux-api */
    if (cmd_exists("termux-flight-mode")) {
        /* 这个命令可能不存在，需要检查 */
    }
    
    /* 方法2: 检查 Android 设置 */
    FILE *fp = popen("getprop ro.airplane.mode 2>/dev/null || settings get global airplane_mode_on 2>/dev/null", "r");
    if (fp) {
        char status[32];
        if (fgets(status, sizeof(status), fp)) {
            if (atoi(status) == 1 || strstr(status, "true")) {
                settings.flight_mode = 1;
            }
        }
        pclose(fp);
    }
    
    /* 方法3: 如果 WiFi 和移动数据都关闭，可能是飞行模式 */
    if (!settings.wifi_enabled && !settings.mobile_data_enabled && 
        !settings.bluetooth_enabled && !settings.hotspot_enabled) {
        /* 可能是飞行模式，但不确定 */
    }
}

/* 获取 NFC 状态 */
static void get_nfc_status(void) {
    settings.nfc = 0;
    
    /* 检查 Android NFC 设置 */
    FILE *fp = popen("settings get global nfc_on 2>/dev/null || getprop nfc.enabled 2>/dev/null", "r");
    if (fp) {
        char status[32];
        if (fgets(status, sizeof(status), fp)) {
            if (atoi(status) == 1 || strstr(status, "true") || strstr(status, "1")) {
                settings.nfc = 1;
            }
        }
        pclose(fp);
    }
}

/* 刷新所有状态 */
static void refresh_all_status(void) {
    get_flight_mode_status();
    get_nfc_status();
    get_real_wifi_status();
    get_real_bluetooth_status();
    get_real_mobile_status();
    get_real_hotspot_status();
    get_real_vpn_status();
    get_real_dns_settings();
    get_local_ip();
    get_public_ip();
}

/* 显示所有设置 */
static void show_all_settings(void) {
    refresh_all_status();
    
    printf("=== Digson Real Settings ===\n\n");
    printf("Flight mode:        %s\n", settings.flight_mode ? "[OPEN]" : "[close]");
    printf("NFC:                %s\n", settings.nfc ? "[OPEN]" : "[close]");
    printf("\n");
    printf("WiFi:               %s\n", settings.wifi_enabled ? "ENABLED" : "disabled");
    if (settings.wifi_enabled) {
        printf("  SSID:             %s\n", settings.current_ssid[0] ? settings.current_ssid : "Unknown");
        printf("  Signal:           %d dBm\n", settings.wifi_signal);
    }
    printf("\n");
    printf("Bluetooth:          %s\n", settings.bluetooth_enabled ? "ENABLED" : "disabled");
    printf("\n");
    printf("Mobile Network:     %s\n", settings.mobile_data_enabled ? "ACTIVE" : "inactive");
    if (settings.sim_count > 0) {
        printf("  Operator:         %s\n", settings.operator_name[0] ? settings.operator_name : "Unknown");
        printf("  SIM:              %d card(s)\n", settings.sim_count);
    }
    printf("\n");
    printf("Personal Hotspot:   %s\n", settings.hotspot_enabled ? "ACTIVE" : "inactive");
    printf("VPN:                %s\n", settings.vpn_enabled ? "CONNECTED" : "disconnected");
    printf("\n");
    printf("Network Interface:  %s\n", settings.interface_name[0] ? settings.interface_name : "None");
    printf("Local IP:           %s\n", settings.local_ip[0] ? settings.local_ip : "Unknown");
    printf("Public IP:          %s\n", settings.public_ip[0] ? settings.public_ip : "Unknown");
    printf("\n");
    printf("DNS 1:              %s\n", settings.dns1[0] ? settings.dns1 : "Not set");
    printf("DNS 2:              %s\n", settings.dns2[0] ? settings.dns2 : "Not set");
    printf("\nScan range:         %d %s\n", 
           long_range ? 1000 : 1000, 
           long_range ? "decimeters" : "centimeters");
}

/* 显示系统设置 */
static void show_system_settings(void) {
    refresh_all_status();
    
    printf("=== System Network Settings ===\n\n");
    
    /* 所有网络接口 */
    struct ifaddrs *ifaddr;
    if (getifaddrs(&ifaddr) == 0) {
        printf("Interfaces:\n");
        for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr) continue;
            
            int family = ifa->ifa_addr->sa_family;
            if (family == AF_INET || family == AF_INET6) {
                char addr[INET6_ADDRSTRLEN];
                void *addr_ptr = (family == AF_INET) ?
                    (void *)&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr :
                    (void *)&((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
                inet_ntop(family, addr_ptr, addr, sizeof(addr));
                
                printf("  %-10s %-5s %s %s\n",
                       ifa->ifa_name,
                       family == AF_INET ? "IPv4" : "IPv6",
                       addr,
                       (ifa->ifa_flags & IFF_UP) ? "[UP]" : "[DOWN]");
            }
        }
        freeifaddrs(ifaddr);
    }
    
    /* 路由表 */
    printf("\nRouting Table:\n");
    FILE *fp = fopen("/proc/net/route", "r");
    if (fp) {
        char line[256];
        fgets(line, sizeof(line), fp); /* 跳过标题 */
        while (fgets(line, sizeof(line), fp)) {
            char iface[32];
            unsigned int dest, gateway, flags;
            int ref, use, metric, mask, mtu, window, irtt;
            if (sscanf(line, "%s %x %x %x %d %d %d %x %d %d %d",
                      iface, &dest, &gateway, &flags, &ref, &use, &metric,
                      &mask, &mtu, &window, &irtt) >= 4) {
                if (flags & RTF_UP) {
                    struct in_addr dest_addr = { .s_addr = dest };
                    struct in_addr gw_addr = { .s_addr = gateway };
                    struct in_addr mask_addr = { .s_addr = mask };
                    
                    printf("  %-8s %s/%s", iface,
                           dest == 0 ? "default" : inet_ntoa(dest_addr),
                           inet_ntoa(mask_addr));
                    if (gateway && !(flags & RTF_GATEWAY)) {
                        printf(" -> %s", inet_ntoa(gw_addr));
                    }
                    printf(" %s\n", (flags & RTF_GATEWAY) ? "[GATEWAY]" : "");
                }
            }
        }
        fclose(fp);
    }
    
    /* ARP 表 */
    printf("\nARP Table:\n");
    fp = fopen("/proc/net/arp", "r");
    if (fp) {
        char line[256];
        fgets(line, sizeof(line), fp); /* 跳过标题 */
        while (fgets(line, sizeof(line), fp)) {
            char ip[64], hw_type[16], flags[16], hw_addr[32], mask[16], device[32];
            if (sscanf(line, "%s %s %s %s %s %s", ip, hw_type, flags, hw_addr, mask, device) == 6) {
                if (strcmp(hw_addr, "00:00:00:00:00:00") != 0) {
                    printf("  %-15s %s %s\n", ip, hw_addr, device);
                }
            }
        }
        fclose(fp);
    }
}

/* 扫描 WiFi - 真实扫描 */
static void scan_wifi_networks(void) {
    int range = long_range ? WIFI_SCAN_RANGE_LONG : WIFI_SCAN_RANGE_SHORT;
    printf("=== WiFi Scan (%d %s range) ===\n\n", 
           long_range ? 1000 : 1000,
           long_range ? "dm" : "cm");
    
    printf("Current Status:\n");
    printf("  WiFi:     %s\n", settings.wifi_enabled ? "ENABLED" : "disabled");
    if (settings.wifi_enabled && settings.current_ssid[0]) {
        printf("  Connected: %s\n", settings.current_ssid);
        printf("  Signal:    %d dBm\n", settings.wifi_signal);
    }
    printf("\n");
    
    /* 尝试扫描 */
    printf("Available Networks:\n");
    
    /* 方法1: termux-wifi-scaninfo */
    if (cmd_exists("termux-wifi-scaninfo")) {
        FILE *fp = popen("termux-wifi-scaninfo 2>/dev/null", "r");
        if (fp) {
            char line[512];
            int count = 0;
            while (fgets(line, sizeof(line), fp)) {
                printf("  %s", line);
                count++;
            }
            pclose(fp);
            if (count > 0) return;
        }
    }
    
    /* 方法2: iwlist */
    if (cmd_exists("iwlist")) {
        FILE *fp = popen("iwlist wlan0 scan 2>/dev/null | grep -E 'Cell|ESSID|Quality|Signal' | head -30", "r");
        if (fp) {
            char line[256];
            int found = 0;
            while (fgets(line, sizeof(line), fp)) {
                printf("  %s", line);
                found++;
            }
            pclose(fp);
            if (found > 0) return;
        }
    }
    
    /* 方法3: wpa_cli */
    if (cmd_exists("wpa_cli")) {
        FILE *fp = popen("wpa_cli scan_results 2>/dev/null | tail -n +2", "r");
        if (fp) {
            char line[256];
            int found = 0;
            while (fgets(line, sizeof(line), fp)) {
                printf("  %s", line);
                found++;
            }
            pclose(fp);
            if (found > 0) return;
        }
    }
    
    printf("  [Unable to scan - may need root access or termux-api]\n");
    printf("  Install: pkg install termux-api\n");
}

/* 扫描蓝牙 */
static void scan_bluetooth_devices(void) {
    int range = long_range ? WIFI_SCAN_RANGE_LONG : WIFI_SCAN_RANGE_SHORT;
    printf("=== Bluetooth Scan (%d %s range) ===\n\n",
           long_range ? 1000 : 1000,
           long_range ? "dm" : "cm");
    
    printf("Current Status:\n");
    printf("  Bluetooth: %s\n\n", settings.bluetooth_enabled ? "ENABLED" : "disabled");
    
    if (!settings.bluetooth_enabled) {
        printf("Bluetooth is disabled. Enable it in Android settings.\n");
        return;
    }
    
    printf("Paired/Found Devices:\n");
    
    /* 方法1: bluetoothctl */
    if (cmd_exists("bluetoothctl")) {
        system("bluetoothctl power on 2>/dev/null");
        FILE *fp = popen("bluetoothctl devices 2>/dev/null", "r");
        if (fp) {
            char line[256];
            int found = 0;
            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "Device")) {
                    printf("  %s", line);
                    found++;
                }
            }
            pclose(fp);
            if (found > 0) {
                printf("\nScanning for new devices (10 seconds)...\n");
                system("timeout 10 bluetoothctl scan on 2>/dev/null");
                return;
            }
        }
    }
    
    /* 方法2: hcitool */
    if (cmd_exists("hcitool")) {
        printf("Scanning with hcitool...\n");
        FILE *fp = popen("hcitool scan 2>/dev/null", "r");
        if (fp) {
            char line[256];
            fgets(line, sizeof(line), fp); /* 跳过 "Scanning ..." */
            while (fgets(line, sizeof(line), fp)) {
                printf("  %s", line);
            }
            pclose(fp);
            return;
        }
    }
    
    printf("  [Unable to scan - may need root access]\n");
}

/* 检查 VPN */
static void check_vpn_software(void) {
    printf("=== VPN Status ===\n\n");
    
    printf("Current: %s\n\n", settings.vpn_enabled ? "CONNECTED" : "Disconnected");
    
    if (settings.vpn_enabled) {
        printf("Active Interface: %s\n", settings.interface_name[0] ? 
               settings.interface_name : "Unknown");
    }
    
    printf("Installed VPN Software:\n");
    
    const char *vpn_tools[] = {
        "openvpn", "wireguard-go", "wg", "ssh", "ss-local", 
        "v2ray", "trojan-go", "clash", "sing-box", NULL
    };
    
    int found = 0;
    for (int i = 0; vpn_tools[i]; i++) {
        if (cmd_exists(vpn_tools[i])) {
            printf("  [OK] %s\n", vpn_tools[i]);
            found++;
        }
    }
    
    if (found == 0) {
        printf("  None found\n");
        printf("\nInstall with:\n");
        printf("  pkg install openssh\n");
        printf("  pkg install wireguard-tools\n");
    }
}

/* 绘制菜单 */
static void draw_menu(void) {
    clear();
    
    /* 刷新状态 */
    refresh_all_status();
    
    attron(A_BOLD | A_UNDERLINE);
    mvprintw(1, 2, "Digson Setting (Real-time)");
    attroff(A_BOLD | A_UNDERLINE);
    
    mvprintw(2, 2, "Press x=toggle flight, f=toggle NFC, Enter=details, q=quit");
    
    for (int i = 0; i < MENU_COUNT; i++) {
        int y = 4 + i * 2;
        
        if (i == current_selection) {
            attron(A_REVERSE);
        }
        
        const char *status = "";
        char status_buf[64];
        switch (i) {
            case MENU_FLIGHT_MODE:
                status = settings.flight_mode ? "[OPEN]" : "[close]";
                break;
            case MENU_WLAN:
                snprintf(status_buf, sizeof(status_buf), "%s%s",
                        settings.wifi_enabled ? "[ON]" : "[off]",
                        settings.current_ssid[0] ? settings.current_ssid : "");
                status = status_buf;
                break;
            case MENU_BLUETOOTH:
                status = settings.bluetooth_enabled ? "[ON]" : "[off]";
                break;
            case MENU_MOBILE_NETWORK:
                snprintf(status_buf, sizeof(status_buf), "%s %s",
                        settings.mobile_data_enabled ? "[4G/5G]" : "[no]",
                        settings.operator_name[0] ? settings.operator_name : "");
                status = status_buf;
                break;
            case MENU_HOTSPOT:
                status = settings.hotspot_enabled ? "[ACTIVE]" : "[inactive]";
                break;
            case MENU_VPN:
                status = settings.vpn_enabled ? "[VPN]" : "[direct]";
                break;
            case MENU_DNS:
                snprintf(status_buf, sizeof(status_buf), "%s/%s",
                        settings.dns1[0] ? settings.dns1 : "auto",
                        settings.dns2[0] ? settings.dns2 : "auto");
                status = status_buf;
                break;
        }
        
        mvprintw(y, 4, "%-20s %s", menu_names[i], status);
        
        if (i == current_selection) {
            attroff(A_REVERSE);
        }
    }
    
    /* 底部信息 */
    attron(A_DIM);
    mvprintw(18, 2, "Interface: %s  Local: %s  Public: %s",
            settings.interface_name[0] ? settings.interface_name : "None",
            settings.local_ip[0] ? settings.local_ip : "-",
            settings.public_ip[0] ? settings.public_ip : "-");
    attroff(A_DIM);
    
    attron(A_BOLD);
    mvprintw(20, 2, "NFC: %s  Range: %d%s  termux-api: %s",
             settings.nfc ? "[OPEN]" : "[close]",
             long_range ? 1000 : 1000,
             long_range ? "dm" : "cm",
             check_termux_api() ? "OK" : "not installed");
    attroff(A_BOLD);
    
    mvprintw(22, 2, "Keys: [↑↓] Navigate  [Enter] Details  [x] Flight  [f] NFC  [q] Quit");
    
    refresh();
}

/* 处理菜单选择 */
static void handle_menu_select(void) {
    switch (current_selection) {
        case MENU_FLIGHT_MODE:
            /* 尝试切换飞行模式 */
            if (cmd_exists("settings")) {
                system("settings put global airplane_mode_on 1 2>/dev/null");
                settings.flight_mode = !settings.flight_mode;
            } else {
                endwin();
                printf("\nCannot toggle flight mode without root/system access.\n");
                printf("Enable in Android Settings.\n");
                printf("\nPress Enter...");
                getchar();
                initscr();
            }
            break;
            
        case MENU_WLAN: {
            endwin();
            scan_wifi_networks();
            printf("\nPress Enter to return...");
            getchar();
            initscr();
            break;
        }
        
        case MENU_BLUETOOTH: {
            endwin();
            scan_bluetooth_devices();
            printf("\nPress Enter to return...");
            getchar();
            initscr();
            break;
        }
        
        case MENU_MOBILE_NETWORK: {
            endwin();
            printf("\n=== Mobile Network ===\n");
            printf("Status: %s\n", settings.mobile_data_enabled ? "Active" : "Inactive");
            printf("Operator: %s\n", settings.operator_name[0] ? settings.operator_name : "Unknown");
            printf("Signal: %d dBm\n", settings.mobile_signal);
            if (!settings.sim_count) {
                printf("\nNo SIM card detected.\n");
            }
            if (!cmd_exists("termux-telephony-deviceinfo")) {
                printf("\nInstall termux-api for more info:\n");
                printf("  pkg install termux-api\n");
            }
            printf("\nPress Enter to return...");
            getchar();
            initscr();
            break;
        }
        
        case MENU_HOTSPOT: {
            endwin();
            printf("\n=== Personal Hotspot ===\n");
            printf("Status: %s\n", settings.hotspot_enabled ? "ACTIVE" : "Inactive");
            if (settings.hotspot_enabled) {
                printf("Configure in Android Settings > Hotspot\n");
            } else {
                printf("Enable in Android Settings > Network & Internet > Hotspot\n");
            }
            printf("\nPress Enter to return...");
            getchar();
            initscr();
            break;
        }
        
        case MENU_VPN: {
            endwin();
            check_vpn_software();
            printf("\nPress Enter to return...");
            getchar();
            initscr();
            break;
        }
        
        case MENU_DNS: {
            endwin();
            printf("\n=== Private DNS Settings ===\n");
            printf("Current DNS:\n");
            printf("  Primary:   %s\n", settings.dns1[0] ? settings.dns1 : "Auto");
            printf("  Secondary: %s\n", settings.dns2[0] ? settings.dns2 : "Auto");
            printf("\nEnter new DNS (or 'auto'): ");
            char dns[256];
            if (fgets(dns, sizeof(dns), stdin)) {
                dns[strcspn(dns, "\n")] = '\0';
                /* 这里可以实际修改 DNS 设置 */
            }
            printf("\nPress Enter to return...");
            getchar();
            initscr();
            break;
        }
    }
}

/* 主菜单循环 */
static void run_menu(void) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(3000); /* 3秒刷新 */
    
    int ch;
    while ((ch = getch()) != 'q') {
        switch (ch) {
            case KEY_UP:
            case 'k':
                if (current_selection > 0) current_selection--;
                break;
            case KEY_DOWN:
            case 'j':
                if (current_selection < MENU_COUNT - 1) current_selection++;
                break;
            case '\n':
            case KEY_ENTER:
                handle_menu_select();
                break;
            case 'x':
            case 'X':
                settings.flight_mode = !settings.flight_mode;
                break;
            case 'f':
            case 'F':
                settings.nfc = !settings.nfc;
                break;
            case 'l':
                long_range = !long_range;
                break;
            case ERR: /* 超时，刷新显示 */
                break;
        }
        draw_menu();
    }
    
    endwin();
}

static void usage(void) {
    printf("Usage: %s [OPTION]...\n", PROGRAM_NAME);
    printf("Real system settings manager for digson environment.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -a, --all       Show all real settings\n");
    printf("  -k, --ko-all    Show all system network settings\n");
    printf("  -l, --long      Enable long range scan (1000 dm)\n");
    printf("  -h, --help      Display help\n");
    printf("  -v, --version   Show version\n");
}

static void version(void) {
    printf("%s %s\n", PROGRAM_NAME, VERSION);
}

int main(int argc, char *argv[]) {
    int opt;
    
    while ((opt = getopt(argc, argv, "aklhv")) != -1) {
        switch (opt) {
            case 'a':
                show_all_settings();
                return 0;
            case 'k':
                show_system_settings();
                return 0;
            case 'l':
                long_range = 1;
                break;
            case 'h':
                usage();
                return 0;
            case 'v':
                version();
                return 0;
            default:
                usage();
                return 1;
        }
    }
    
    run_menu();
    return 0;
}
