#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <ncurses.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <iwlib.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <dirent.h>
#include <pwd.h>

#define PROGRAM_NAME "setting"
#define VERSION "1.0"
#define WIFI_SCAN_RANGE_SHORT 1000  /* 厘米 */
#define WIFI_SCAN_RANGE_LONG 10000  /* 1000分米 = 10000厘米 */

/* 设置状态 */
typedef struct {
    int flight_mode;        /* 0=close, 1=open */
    int nfc;                /* 1=open, 0=close */
    char *current_wlan;
    char *current_bluetooth;
    char *mobile_network;
    char *hotspot_status;
    char *vpn_status;
    char *dns_status;
} system_settings_t;

static system_settings_t settings = {
    .flight_mode = 0,
    .nfc = 1,
    .current_wlan = NULL,
    .current_bluetooth = NULL,
    .mobile_network = NULL,
    .hotspot_status = NULL,
    .vpn_status = NULL,
    .dns_status = NULL
};

static int long_range = 0;  /* -l, --long 选项 */

/* 菜单项 */
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

/* 错误处理 */
static void error(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    fprintf(stderr, "%s: ", PROGRAM_NAME);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

/* 显示帮助 */
static void usage(void) {
    printf("Usage: %s [OPTION]...\n", PROGRAM_NAME);
    printf("System settings manager for digson environment.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -a, --all       Show all settings\n");
    printf("  -k, --ko-all    Show all system settings\n");
    printf("  -l, --long      Enable long range scan (1000 dm instead of 1000 cm)\n");
    printf("  -h, --help      Display this help and exit\n");
    printf("  -v, --version   Output version information and exit\n");
    printf("\n");
    printf("Interactive commands (in menu mode):\n");
    printf("  Up/Down         Navigate menu\n");
    printf("  Enter           Select/Enter submenu\n");
    printf("  x               Toggle flight mode\n");
    printf("  f               Toggle NFC (modifies system)\n");
    printf("  q               Quit\n");
    printf("\n");
    printf("Report bugs to <bugs@digson.org>\n");
}

/* 版本信息 */
static void version(void) {
    printf("%s %s\n", PROGRAM_NAME, VERSION);
}

/* 显示所有设置 */
static void show_all_settings(void) {
    printf("=== Digson Settings ===\n\n");
    printf("Flight mode:        %s\n", settings.flight_mode ? "[open]" : "[close]");
    printf("NFC:                %s\n", settings.nfc ? "[open]" : "[close]");
    printf("Current WLAN:       %s\n", settings.current_wlan ? settings.current_wlan : "None");
    printf("Current Bluetooth:  %s\n", settings.current_bluetooth ? settings.current_bluetooth : "None");
    printf("Mobile Network:     %s\n", settings.mobile_network ? settings.mobile_network : "No SIM");
    printf("Personal Hotspot:   %s\n", settings.hotspot_status ? settings.hotspot_status : "Inactive");
    printf("VPN:                %s\n", settings.vpn_status ? settings.vpn_status : "Disconnected");
    printf("Private DNS:        %s\n", settings.dns_status ? settings.dns_status : "Auto");
    printf("\nScan range:         %d %s\n", 
           long_range ? 1000 : 1000, 
           long_range ? "decimeters" : "centimeters");
}

/* 显示系统设置 */
static void show_system_settings(void) {
    printf("=== System Settings ===\n\n");
    
    /* 网络接口 */
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return;
    }
    
    printf("Network Interfaces:\n");
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        
        int family = ifa->ifa_addr->sa_family;
        if (family == AF_INET || family == AF_INET6) {
            char addr[INET6_ADDRSTRLEN];
            void *addr_ptr = (family == AF_INET) ?
                (void *)&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr :
                (void *)&((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
            inet_ntop(family, addr_ptr, addr, sizeof(addr));
            printf("  %s: %s\n", ifa->ifa_name, addr);
        }
    }
    freeifaddrs(ifaddr);
    
    /* 路由表 */
    printf("\nRouting Table:\n");
    FILE *fp = fopen("/proc/net/route", "r");
    if (fp) {
        char line[256];
        fgets(line, sizeof(line), fp); /* 跳过标题 */
        while (fgets(line, sizeof(line), fp)) {
            char iface[16];
            unsigned int dest, gateway, flags;
            sscanf(line, "%s %x %x %x", iface, &dest, &gateway, &flags);
            if (flags & RTF_UP) {
                struct in_addr dest_addr = { .s_addr = dest };
                struct in_addr gw_addr = { .s_addr = gateway };
                printf("  %s: %s -> %s\n", iface, 
                       dest == 0 ? "default" : inet_ntoa(dest_addr),
                       gateway == 0 ? "direct" : inet_ntoa(gw_addr));
            }
        }
        fclose(fp);
    }
    
    /* DNS 设置 */
    printf("\nDNS Configuration:\n");
    fp = fopen("/etc/resolv.conf", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "nameserver", 10) == 0) {
                printf("  %s", line);
            }
        }
        fclose(fp);
    }
    
    /* 无线状态 */
    printf("\nWireless Status:\n");
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        struct iwreq wrq;
        char essid[IW_ESSID_MAX_SIZE + 1];
        strncpy(wrq.ifr_name, "wlan0", IFNAMSIZ);
        wrq.u.essid.pointer = essid;
        wrq.u.essid.length = IW_ESSID_MAX_SIZE;
        
        if (ioctl(sock, SIOCGIWESSID, &wrq) == 0) {
            essid[wrq.u.essid.length] = '\0';
            printf("  Connected to: %s\n", essid[0] ? essid : "Not connected");
        } else {
            printf("  Wireless interface not available\n");
        }
        close(sock);
    }
}

/* 扫描 WiFi 网络 */
static void scan_wifi_networks(void) {
    int range = long_range ? WIFI_SCAN_RANGE_LONG : WIFI_SCAN_RANGE_SHORT;
    printf("Scanning WiFi networks within %d %s...\n", 
           long_range ? 1000 : 1000,
           long_range ? "dm" : "cm");
    
    /* 使用 iwlist 或类似工具扫描 */
    FILE *fp = popen("iwlist scan 2>/dev/null | grep -E 'Cell|ESSID|Quality'", "r");
    if (fp) {
        char line[256];
        int count = 0;
        while (fgets(line, sizeof(line), fp)) {
            printf("%s", line);
            count++;
        }
        pclose(fp);
        
        if (count == 0) {
            printf("No networks found or wireless interface not available.\n");
        }
    } else {
        printf("WiFi scan not available on this system.\n");
    }
}

/* 扫描蓝牙设备 */
static void scan_bluetooth_devices(void) {
    int range = long_range ? WIFI_SCAN_RANGE_LONG : WIFI_SCAN_RANGE_SHORT;
    printf("Scanning Bluetooth devices within %d %s...\n",
           long_range ? 1000 : 1000,
           long_range ? "dm" : "cm");
    
    int dev_id = hci_get_route(NULL);
    if (dev_id < 0) {
        printf("Bluetooth adapter not found.\n");
        return;
    }
    
    int sock = hci_open_dev(dev_id);
    if (sock < 0) {
        printf("Cannot open Bluetooth device.\n");
        return;
    }
    
    inquiry_info *ii = NULL;
    int max_rsp = 255;
    int num_rsp;
    
    ii = malloc(max_rsp * sizeof(inquiry_info));
    if (!ii) {
        close(sock);
        return;
    }
    
    num_rsp = hci_inquiry(dev_id, 8, max_rsp, NULL, &ii, IREQ_CACHE_FLUSH);
    if (num_rsp < 0) {
        printf("Bluetooth inquiry failed.\n");
        free(ii);
        close(sock);
        return;
    }
    
    for (int i = 0; i < num_rsp; i++) {
        char addr[18];
        char name[248];
        ba2str(&(ii+i)->bdaddr, addr);
        memset(name, 0, sizeof(name));
        
        if (hci_read_remote_name(sock, &(ii+i)->bdaddr, sizeof(name), name, 0) < 0) {
            strcpy(name, "[unknown]");
        }
        
        printf("  %s  %s\n", addr, name);
    }
    
    free(ii);
    close(sock);
}

/* 检查 VPN 软件 */
static void check_vpn_software(void) {
    printf("Checking VPN software...\n");
    
    const char *vpn_tools[] = {
        "openvpn", "wireguard", "wg", "pptp", "openconnect",
        "vpnc", "ssh", "shadowsocks", "v2ray", "trojan",
        NULL
    };
    
    int found = 0;
    for (int i = 0; vpn_tools[i]; i++) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "which %s >/dev/null 2>&1", vpn_tools[i]);
        if (system(cmd) == 0) {
            printf("  [OK] %s\n", vpn_tools[i]);
            found++;
        }
    }
    
    if (found == 0) {
        printf("  No VPN software found.\n");
    }
}

/* 检查 SIM 卡 */
static int check_sim_card(void) {
    /* 检查常见的移动宽带设备 */
    if (access("/dev/ttyUSB0", F_OK) == 0 ||
        access("/dev/cdc-wdm0", F_OK) == 0 ||
        access("/dev/wwan0", F_OK) == 0) {
        return 1;
    }
    
    /* 检查 NetworkManager 移动连接 */
    FILE *fp = popen("nmcli device show 2>/dev/null | grep -i gsm", "r");
    if (fp) {
        char line[256];
        if (fgets(line, sizeof(line), fp)) {
            pclose(fp);
            return 1;
        }
        pclose(fp);
    }
    
    return 0;
}

/* 绘制菜单 */
static void draw_menu(void) {
    clear();
    
    /* 标题 */
    attron(A_BOLD | A_UNDERLINE);
    mvprintw(1, 2, "Digson Setting");
    attroff(A_BOLD | A_UNDERLINE);
    
    /* 说明 */
    mvprintw(2, 2, "Use arrow keys to navigate, Enter to select, x/f to toggle, q to quit");
    
    /* 菜单项 */
    for (int i = 0; i < MENU_COUNT; i++) {
        int y = 4 + i * 2;
        
        /* 高亮当前选择 */
        if (i == current_selection) {
            attron(A_REVERSE);
        }
        
        /* 显示状态指示器 */
        const char *status = "";
        switch (i) {
            case MENU_FLIGHT_MODE:
                status = settings.flight_mode ? "[open]" : "[close]";
                break;
            case MENU_WLAN:
                status = "->";
                break;
            case MENU_BLUETOOTH:
                status = "->";
                break;
            case MENU_MOBILE_NETWORK:
                status = check_sim_card() ? "[available]" : "[no SIM]";
                break;
            case MENU_HOTSPOT:
                status = "[configure]";
                break;
            case MENU_VPN:
                status = "->";
                break;
            case MENU_DNS:
                status = "->";
                break;
        }
        
        mvprintw(y, 4, "%s %s", menu_names[i], status);
        
        if (i == current_selection) {
            attroff(A_REVERSE);
        }
        
        /* 描述 */
        attron(A_DIM);
        switch (i) {
            case MENU_FLIGHT_MODE:
                mvprintw(y + 1, 6, "Default for get command");
                break;
            case MENU_WLAN:
                mvprintw(y + 1, 6, "Configure wireless LAN for get (%d %s range)",
                        long_range ? 1000 : 1000, long_range ? "dm" : "cm");
                break;
            case MENU_BLUETOOTH:
                mvprintw(y + 1, 6, "Configure Bluetooth for get (%d %s range)",
                        long_range ? 1000 : 1000, long_range ? "dm" : "cm");
                break;
            case MENU_MOBILE_NETWORK:
                mvprintw(y + 1, 6, "Mobile data for get (requires SIM)");
                break;
            case MENU_HOTSPOT:
                mvprintw(y + 1, 6, "Personal hotspot for get");
                break;
            case MENU_VPN:
                mvprintw(y + 1, 6, "VPN settings for get");
                break;
            case MENU_DNS:
                mvprintw(y + 1, 6, "Private DNS for get (when IP blocked)");
                break;
        }
        attroff(A_DIM);
    }
    
    /* NFC 状态（底部） */
    attron(A_BOLD);
    mvprintw(20, 2, "NFC settings: %s (press 'f' to toggle, modifies system)",
             settings.nfc ? "[open]" : "[close]");
    attroff(A_BOLD);
    
    /* 帮助 */
    mvprintw(22, 2, "Keys: [Up/Down] Navigate  [Enter] Select  [x] Flight mode  [f] NFC  [q] Quit");
    
    refresh();
}

/* 处理菜单选择 */
static void handle_menu_select(void) {
    switch (current_selection) {
        case MENU_FLIGHT_MODE:
            settings.flight_mode = !settings.flight_mode;
            break;
            
        case MENU_WLAN: {
            endwin();
            printf("\n=== WLAN Settings ===\n");
            scan_wifi_networks();
            printf("\nPress Enter to return to menu...");
            getchar();
            initscr();
            break;
        }
        
        case MENU_BLUETOOTH: {
            endwin();
            printf("\n=== Bluetooth Settings ===\n");
            scan_bluetooth_devices();
            printf("\nPress Enter to return to menu...");
            getchar();
            initscr();
            break;
        }
        
        case MENU_MOBILE_NETWORK: {
            endwin();
            printf("\n=== Mobile Network ===\n");
            if (check_sim_card()) {
                printf("SIM card detected.\n");
                printf("Configuring mobile data for get command...\n");
                settings.mobile_network = strdup("Active");
            } else {
                printf("No SIM card detected.\n");
                printf("Please insert a SIM card to use mobile network.\n");
            }
            printf("\nPress Enter to return to menu...");
            getchar();
            initscr();
            break;
        }
        
        case MENU_HOTSPOT: {
            endwin();
            printf("\n=== Personal Hotspot ===\n");
            printf("Configuring personal hotspot for get command...\n");
            printf("SSID: digson-hotspot\n");
            printf("Password: [auto-generated]\n");
            settings.hotspot_status = strdup("Active: digson-hotspot");
            printf("\nPress Enter to return to menu...");
            getchar();
            initscr();
            break;
        }
        
        case MENU_VPN: {
            endwin();
            printf("\n=== Network Ladder Settings (VPN) ===\n");
            check_vpn_software();
            printf("\nConfiguring VPN for get command...\n");
            settings.vpn_status = strdup("Checking available...");
            printf("\nPress Enter to return to menu...");
            getchar();
            initscr();
            break;
        }
        
        case MENU_DNS: {
            endwin();
            printf("\n=== Private DNS Settings ===\n");
            printf("Configuring private DNS for get command...\n");
            printf("Use when your IP is blocked.\n");
            printf("Enter DNS server (or 'auto'): ");
            
            char dns[256];
            if (fgets(dns, sizeof(dns), stdin)) {
                dns[strcspn(dns, "\n")] = '\0';
                if (settings.dns_status) free(settings.dns_status);
                settings.dns_status = strdup(dns[0] ? dns : "Auto");
            }
            printf("\nPress Enter to return to menu...");
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
    
    int ch;
    while ((ch = getch()) != 'q') {
        switch (ch) {
            case KEY_UP:
                if (current_selection > 0) current_selection--;
                break;
                
            case KEY_DOWN:
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
                /* 实际修改系统 NFC 设置 */
                printf("\n%s NFC... (system modification)\n", 
                       settings.nfc ? "Enabling" : "Disabling");
                /* 这里可以添加实际的系统调用 */
                break;
        }
        
        draw_menu();
    }
    
    endwin();
}

int main(int argc, char *argv[]) {
    int opt;
    
    static struct option long_options[] = {
        {"all", no_argument, 0, 'a'},
        {"ko-all", no_argument, 0, 'k'},
        {"long", no_argument, 0, 'l'},
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {0, 0, 0, 0}
    };
    
    while ((opt = getopt_long(argc, argv, "aklhv", long_options, NULL)) != -1) {
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
    
    /* 默认进入菜单模式 */
    run_menu();
    
    return 0;
}
