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
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <dirent.h>
#include <pwd.h>
#include <ctype.h>

#ifndef RTF_UP
#define RTF_UP          0x0001
#endif
#ifndef RTF_GATEWAY
#define RTF_GATEWAY     0x0002
#endif

#define PROGRAM_NAME "setting"
#define VERSION "1.0"
#define WIFI_SCAN_RANGE_SHORT 1000
#define WIFI_SCAN_RANGE_LONG 10000

typedef struct {
    int flight_mode;
    int nfc;
    int wifi_enabled;
    int bluetooth_enabled;
    int mobile_data_enabled;
    int hotspot_enabled;
    int vpn_enabled;
    char current_ssid[256];
    char current_bssid[32];
    int wifi_signal;
    char local_ip[64];
    char public_ip[64];
    char dns1[64];
    char dns2[64];
    char interface_name[32];
    int sim_count;
    char operator_name[64];
    int mobile_signal;
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
    MENU_DE,  // 新增：桌面环境设置
    MENU_COUNT
} menu_item_t;

static const char *menu_names[] = {
    "Flight mode",
    "WLAN settings",
    "Bluetooth setting",
    "Mobile Network",
    "Personal hotspot",
    "Network ladder settings",
    "Private DNS settings",
    "DE setting ->"  // 新增
};

static int current_selection = 0;

static void error(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    fprintf(stderr, "%s: ", PROGRAM_NAME);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

static void usage(void) {
    printf("Usage: %s [OPTION]...\n", PROGRAM_NAME);
    printf("  -a, --all       Show all settings\n");
    printf("  -k, --ko-all    Show all system settings\n");
    printf("  -l, --long      Enable long range scan\n");
    printf("  -h, --help      Display help\n");
    printf("  -v, --version   Show version\n");
}

static void version(void) {
    printf("%s %s\n", PROGRAM_NAME, VERSION);
}

static int cmd_exists(const char *cmd) {
    char check[256];
    snprintf(check, sizeof(check), "which %s >/dev/null 2>&1", cmd);
    return system(check) == 0;
}

static int check_termux_api(void) {
    return cmd_exists("termux-wifi-connectioninfo");
}

static void get_real_wifi_status(void) {
    settings.wifi_enabled = 0;
    settings.current_ssid[0] = '\0';
    settings.wifi_signal = 0;
    
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
    
    if (!settings.wifi_enabled) {
        FILE *fp = fopen("/proc/net/wireless", "r");
        if (fp) {
            char line[256];
            fgets(line, sizeof(line), fp);
            fgets(line, sizeof(line), fp);
            if (fgets(line, sizeof(line), fp)) {
                char iface[32];
                int status, link, level, noise;
                if (sscanf(line, "%s %d %d %d %d", iface, &status, &link, &level, &noise) >= 3) {
                    if (link > 0) {
                        settings.wifi_enabled = 1;
                        settings.wifi_signal = level;
                        char *colon = strchr(iface, ':');
                        if (colon) *colon = '\0';
                        strncpy(settings.interface_name, iface, sizeof(settings.interface_name) - 1);
                    }
                }
            }
            fclose(fp);
        }
    }
    
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

static void get_real_bluetooth_status(void) {
    settings.bluetooth_enabled = 0;
    
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
    
    if (!settings.bluetooth_enabled) {
        DIR *dir = opendir("/sys/class/bluetooth");
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_name[0] == '.') continue;
                settings.bluetooth_enabled = 1;
                break;
            }
            closedir(dir);
        }
    }
}

static void get_real_mobile_status(void) {
    settings.mobile_data_enabled = 0;
    settings.sim_count = 0;
    settings.operator_name[0] = '\0';
    
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
            }
            pclose(fp);
        }
    }
    
    struct ifaddrs *ifaddr;
    if (getifaddrs(&ifaddr) == 0) {
        for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
            if (ifa->ifa_name && (strncmp(ifa->ifa_name, "rmnet", 5) == 0 ||
                                  strncmp(ifa->ifa_name, "ccmni", 5) == 0)) {
                if (ifa->ifa_flags & IFF_UP) {
                    settings.mobile_data_enabled = 1;
                }
            }
        }
        freeifaddrs(ifaddr);
    }
}

static void get_real_hotspot_status(void) {
    settings.hotspot_enabled = 0;
    
    struct ifaddrs *ifaddr;
    if (getifaddrs(&ifaddr) == 0) {
        for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
            if (ifa->ifa_name && strstr(ifa->ifa_name, "ap")) {
                if (ifa->ifa_flags & IFF_UP) {
                    settings.hotspot_enabled = 1;
                }
            }
        }
        freeifaddrs(ifaddr);
    }
}

static void get_real_vpn_status(void) {
    settings.vpn_enabled = 0;
    
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
        }
        freeifaddrs(ifaddr);
    }
}

static void get_real_dns_settings(void) {
    settings.dns1[0] = '\0';
    settings.dns2[0] = '\0';
    
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
    
    if (!settings.dns1[0]) {
        FILE *fp = popen("getprop net.dns1 2>/dev/null", "r");
        if (fp) {
            fgets(settings.dns1, sizeof(settings.dns1), fp);
            settings.dns1[strcspn(settings.dns1, "\n")] = '\0';
            pclose(fp);
        }
    }
}

static void get_local_ip(void) {
    settings.local_ip[0] = '\0';
    
    struct ifaddrs *ifaddr;
    if (getifaddrs(&ifaddr) == 0) {
        for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
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

static void get_public_ip(void) {
    settings.public_ip[0] = '\0';
    
    const char *services[] = {
        "curl -s --max-time 5 https://api.ipify.org 2>/dev/null",
        "curl -s --max-time 5 http://ifconfig.me 2>/dev/null",
        NULL
    };
    
    for (int i = 0; services[i]; i++) {
        FILE *fp = popen(services[i], "r");
        if (fp) {
            char ip[64];
            if (fgets(ip, sizeof(ip), fp)) {
                ip[strcspn(ip, "\n")] = '\0';
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

static void get_flight_mode_status(void) {
    settings.flight_mode = 0;
    
    FILE *fp = popen("settings get global airplane_mode_on 2>/dev/null", "r");
    if (fp) {
        char status[32];
        if (fgets(status, sizeof(status), fp)) {
            if (atoi(status) == 1) {
                settings.flight_mode = 1;
            }
        }
        pclose(fp);
    }
}

static void get_nfc_status(void) {
    settings.nfc = 0;
    
    FILE *fp = popen("settings get global nfc_on 2>/dev/null", "r");
    if (fp) {
        char status[32];
        if (fgets(status, sizeof(status), fp)) {
            if (atoi(status) == 1) {
                settings.nfc = 1;
            }
        }
        pclose(fp);
    }
}

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
    }
    printf("\n");
    printf("Personal Hotspot:   %s\n", settings.hotspot_enabled ? "ACTIVE" : "inactive");
    printf("VPN:                %s\n", settings.vpn_enabled ? "CONNECTED" : "disconnected");
    printf("\n");
    printf("Interface:          %s\n", settings.interface_name[0] ? settings.interface_name : "None");
    printf("Local IP:           %s\n", settings.local_ip[0] ? settings.local_ip : "Unknown");
    printf("Public IP:          %s\n", settings.public_ip[0] ? settings.public_ip : "Unknown");
    printf("\n");
    printf("DNS 1:              %s\n", settings.dns1[0] ? settings.dns1 : "Not set");
    printf("DNS 2:              %s\n", settings.dns2[0] ? settings.dns2 : "Not set");
}

static void show_system_settings(void) {
    refresh_all_status();
    
    printf("=== System Network Settings ===\n\n");
    
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
    
    printf("\nRouting Table:\n");
    FILE *fp = fopen("/proc/net/route", "r");
    if (fp) {
        char line[256];
        fgets(line, sizeof(line), fp);
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
                    
                    printf("  %-8s %s", iface,
                           dest == 0 ? "default" : inet_ntoa(dest_addr));
                    if (gateway && (flags & RTF_GATEWAY)) {
                        printf(" -> %s", inet_ntoa(gw_addr));
                    }
                    printf("\n");
                }
            }
        }
        fclose(fp);
    }
}

static void scan_wifi_networks(void) {
    printf("=== WiFi Scan ===\n\n");
    printf("Current: %s\n", settings.wifi_enabled ? settings.current_ssid : "Not connected");
    printf("\nScanning...\n");
    
    if (cmd_exists("termux-wifi-scaninfo")) {
        system("termux-wifi-scaninfo 2>/dev/null");
    } else if (cmd_exists("iwlist")) {
        system("iwlist wlan0 scan 2>/dev/null | grep -E 'Cell|ESSID|Quality' | head -20");
    } else {
        printf("No WiFi scanning tool available.\n");
        printf("Install: pkg install wireless-tools termux-api\n");
    }
}

static void scan_bluetooth_devices(void) {
    printf("=== Bluetooth Scan ===\n\n");
    printf("Status: %s\n\n", settings.bluetooth_enabled ? "ENABLED" : "disabled");
    
    if (!settings.bluetooth_enabled) {
        printf("Bluetooth is disabled.\n");
        return;
    }
    
    if (cmd_exists("bluetoothctl")) {
        printf("Paired devices:\n");
        system("bluetoothctl devices 2>/dev/null");
        printf("\nScanning for 5 seconds...\n");
        system("timeout 5 bluetoothctl scan on 2>/dev/null");
        system("bluetoothctl scan off 2>/dev/null");
    } else if (cmd_exists("hcitool")) {
        system("hcitool scan 2>/dev/null");
    } else {
        printf("No Bluetooth tool available.\n");
    }
}

static void check_vpn_software(void) {
    printf("=== VPN Status ===\n\n");
    printf("Current: %s\n\n", settings.vpn_enabled ? "CONNECTED" : "Disconnected");
    
    printf("Installed VPN tools:\n");
    const char *vpn_tools[] = {"openvpn", "ssh", "wg", "v2ray", "clash", NULL};
    for (int i = 0; vpn_tools[i]; i++) {
        if (cmd_exists(vpn_tools[i])) {
            printf("  [OK] %s\n", vpn_tools[i]);
        }
    }
}

// ==================== 新增：DE 设置菜单 ====================

static void show_de_selection_menu(void) {
    initscr();
    start_color();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    
    // 蓝色背景
    init_pair(1, COLOR_WHITE, COLOR_BLUE);
    init_pair(2, COLOR_BLACK, COLOR_WHITE);
    
    bkgd(COLOR_PAIR(1));
    clear();
    
    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(1, 2, "Desktop environment settings");
    attroff(A_BOLD);
    
    mvprintw(3, 2, "Please select the desktop environment you want to adjust:");
    
    attron(A_BOLD);
    mvprintw(5, 4, "status-nast(A desktop environment using i3)");
    attroff(A_BOLD);
    
    mvprintw(7, 2, "Press Enter to configure, q to go back");
    
    refresh();
    
    int ch;
    while ((ch = getch()) != 'q' && ch != 'Q') {
        if (ch == '\n' || ch == KEY_ENTER) {
            endwin();
            // 启动 status-nast 配置菜单
            printf("\nLaunching status-nast configuration...\n");
            int ret = system("startupstatusnast -c 2>/dev/null || status-nast -c 2>/dev/null");
            if (ret != 0) {
                printf("Failed to start status-nast configuration.\n");
                printf("Make sure CDE is installed: make install in cde/\n");
                printf("\nPress Enter to continue...");
                fflush(stdout);
                while (getchar() != '\n');
            }
            // 重新初始化 ncurses
            initscr();
            start_color();
            cbreak();
            noecho();
            keypad(stdscr, TRUE);
            curs_set(0);
            bkgd(COLOR_PAIR(1));
            clear();
            
            attron(COLOR_PAIR(1) | A_BOLD);
            mvprintw(1, 2, "Desktop environment settings");
            attroff(A_BOLD);
            mvprintw(3, 2, "Please select the desktop environment you want to adjust:");
            attron(A_BOLD);
            mvprintw(5, 4, "status-nast(A desktop environment using i3)");
            attroff(A_BOLD);
            mvprintw(7, 2, "Press Enter to configure, q to go back");
            refresh();
        }
    }
    
    endwin();
}

// ===========================================================

static void draw_menu(void) {
    clear();
    refresh_all_status();
    
    attron(A_BOLD | A_UNDERLINE);
    mvprintw(1, 2, "Digson Setting (Real-time)");
    attroff(A_BOLD | A_UNDERLINE);
    
    mvprintw(2, 2, "Press: UP/DOWN=navigate, ENTER=select, x=flight, f=nfc, l=range, q=quit");
    
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
                snprintf(status_buf, sizeof(status_buf), "%s %s",
                        settings.wifi_enabled ? "[ON]" : "[off]",
                        settings.current_ssid[0] ? settings.current_ssid : "");
                status = status_buf;
                break;
            case MENU_BLUETOOTH:
                status = settings.bluetooth_enabled ? "[ON]" : "[off]";
                break;
            case MENU_MOBILE_NETWORK:
                snprintf(status_buf, sizeof(status_buf), "%s %s",
                        settings.mobile_data_enabled ? "[4G]" : "[no]",
                        settings.operator_name[0] ? settings.operator_name : "");
                status = status_buf;
                break;
            case MENU_HOTSPOT:
                status = settings.hotspot_enabled ? "[ON]" : "[off]";
                break;
            case MENU_VPN:
                status = settings.vpn_enabled ? "[VPN]" : "[direct]";
                break;
            case MENU_DNS:
                snprintf(status_buf, sizeof(status_buf), "%s",
                        settings.dns1[0] ? settings.dns1 : "auto");
                status = status_buf;
                break;
            case MENU_DE:  // 新增
                status = "->";
                break;
        }
        
        mvprintw(y, 4, "%-20s %s", menu_names[i], status);
        
        if (i == current_selection) {
            attroff(A_REVERSE);
        }
    }
    
    attron(A_DIM);
    mvprintw(20, 2, "Interface: %s  Local: %s  Public: %s",
            settings.interface_name[0] ? settings.interface_name : "None",
            settings.local_ip[0] ? settings.local_ip : "-",
            settings.public_ip[0] ? settings.public_ip : "-");
    attroff(A_DIM);
    
    attron(A_BOLD);
    mvprintw(22, 2, "NFC: %s  Range: %d%s  API: %s",
             settings.nfc ? "[OPEN]" : "[close]",
             long_range ? 1000 : 1000,
             long_range ? "dm" : "cm",
             check_termux_api() ? "OK" : "no");
    attroff(A_BOLD);
    
    mvprintw(24, 2, "Keys: [↑↓] Navigate  [Enter] Select  [x] Flight  [f] NFC  [l] Range  [q] Quit");
    
    refresh();
}

static void handle_menu_select(void) {
    switch (current_selection) {
        case MENU_FLIGHT_MODE:
            if (cmd_exists("settings")) {
                system("settings put global airplane_mode_on 1 2>/dev/null");
            }
            settings.flight_mode = !settings.flight_mode;
            break;
            
        case MENU_WLAN: {
            endwin();
            printf("\n");
            scan_wifi_networks();
            printf("\nPress Enter to return...");
            fflush(stdout);
            while (getchar() != '\n');
            initscr();
            break;
        }
        
        case MENU_BLUETOOTH: {
            endwin();
            printf("\n");
            scan_bluetooth_devices();
            printf("\nPress Enter to return...");
            fflush(stdout);
            while (getchar() != '\n');
            initscr();
            break;
        }
        
        case MENU_MOBILE_NETWORK: {
            endwin();
            printf("\n=== Mobile Network ===\n");
            printf("Status: %s\n", settings.mobile_data_enabled ? "Active" : "Inactive");
            printf("Operator: %s\n", settings.operator_name[0] ? settings.operator_name : "Unknown");
            printf("\nPress Enter to return...");
            fflush(stdout);
            while (getchar() != '\n');
            initscr();
            break;
        }
        
        case MENU_HOTSPOT: {
            endwin();
            printf("\n=== Personal Hotspot ===\n");
            printf("Status: %s\n", settings.hotspot_enabled ? "ACTIVE" : "Inactive");
            printf("\nConfigure in Android Settings.\n");
            printf("\nPress Enter to return...");
            fflush(stdout);
            while (getchar() != '\n');
            initscr();
            break;
        }
        
        case MENU_VPN: {
            endwin();
            printf("\n");
            check_vpn_software();
            printf("\nPress Enter to return...");
            fflush(stdout);
            while (getchar() != '\n');
            initscr();
            break;
        }
        
        case MENU_DNS: {
            endwin();
            printf("\n=== DNS Settings ===\n");
            printf("DNS1: %s\n", settings.dns1[0] ? settings.dns1 : "Auto");
            printf("DNS2: %s\n", settings.dns2[0] ? settings.dns2 : "Auto");
            printf("\nPress Enter to return...");
            fflush(stdout);
            while (getchar() != '\n');
            initscr();
            break;
        }
        
        // ==================== 新增：DE 设置 ====================
        case MENU_DE: {
            show_de_selection_menu();  // 显示蓝色菜单
            break;
        }
        // =====================================================
    }
}

static void run_menu(void) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    
    draw_menu();
    
    int ch;
    while ((ch = getch()) != 'q' && ch != 'Q') {
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
            case ' ':
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
            case 'L':
                long_range = !long_range;
                break;
        }
        draw_menu();
    }
    
    endwin();
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
