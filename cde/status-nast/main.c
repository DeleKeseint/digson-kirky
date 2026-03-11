#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ncurses.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/types.h>
#include <dirent.h>
#include <ctype.h>
#include <libgen.h>

#define PROGRAM_NAME "status-nast"
#define VERSION "1.0"
#define DEFAULT_CONFIG_DIR "/data/data/com.termux/files/usr/etc/digson/cde/nastconfig"
#define CONFIG_FILE "Configfile"

typedef struct {
    int disable_all_options;
    int check_gui_default;
    int ctrl_x_list_gui;
    int use_tiled;
    char config_path[1024];
} nast_config_t;

static nast_config_t config = {
    .disable_all_options = 0,
    .check_gui_default = 1,
    .ctrl_x_list_gui = 1,
    .use_tiled = 0,
    .config_path = ""
};

static int gui_mode = 0;

static char* get_config_path(void) {
    if (config.config_path[0]) return config.config_path;
    
    char *hititgit = getenv("HITITGIT");
    if (hititgit) {
        static char path[1024];
        snprintf(path, sizeof(path), "%s/%s", hititgit, CONFIG_FILE);
        return path;
    }
    
    static char default_path[1024];
    snprintf(default_path, sizeof(default_path), "%s/%s", 
             DEFAULT_CONFIG_DIR, CONFIG_FILE);
    return default_path;
}

static void create_dir_recursive(const char *path) {
    char tmp[1024];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    
    size_t len = strlen(tmp);
    if (len == 0) return;
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';
    
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static void save_config(void) {
    char *path = get_config_path();
    
    char dir[1024];
    strncpy(dir, path, sizeof(dir) - 1);
    char *last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        create_dir_recursive(dir);
    }
    
    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "Cannot save config to %s\n", path);
        return;
    }
    
    fprintf(fp, "# status-nast configuration\n");
    fprintf(fp, "DISABLE_ALL_OPTIONS=%d\n", config.disable_all_options);
    fprintf(fp, "CHECK_GUI_DEFAULT=%d\n", config.check_gui_default);
    fprintf(fp, "CTRL_X_LIST_GUI=%d\n", config.ctrl_x_list_gui);
    fprintf(fp, "USE_TILED=%d\n", config.use_tiled);
    
    fclose(fp);
    printf("Config saved to: %s\n", path);
}

static void load_config(void) {
    char *path = get_config_path();
    
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#') continue;
        
        int val;
        if (sscanf(line, "DISABLE_ALL_OPTIONS=%d", &val) == 1) {
            config.disable_all_options = val;
        } else if (sscanf(line, "CHECK_GUI_DEFAULT=%d", &val) == 1) {
            config.check_gui_default = val;
        } else if (sscanf(line, "CTRL_X_LIST_GUI=%d", &val) == 1) {
            config.ctrl_x_list_gui = val;
        } else if (sscanf(line, "USE_TILED=%d", &val) == 1) {
            config.use_tiled = val;
        }
    }
    
    fclose(fp);
}

static int cmd_exists(const char *cmd) {
    char check[256];
    snprintf(check, sizeof(check), "which %s >/dev/null 2>&1", cmd);
    return system(check) == 0;
}

static int check_gui_software(void) {
    const char *gui_apps[] = {
        "i3", "i3wm", "xterm", "uxterm", "firefox", "chromium",
        "lxterminal", "pcmanfm", "thunar", "dmenu", "rofi",
        "polybar", "feh", "compton", "picom", NULL
    };
    
    int found = 0;
    printf("Checking GUI software...\n");
    
    for (int i = 0; gui_apps[i]; i++) {
        if (cmd_exists(gui_apps[i])) {
            printf("  [OK] %s\n", gui_apps[i]);
            found++;
        }
    }
    
    return found;
}

static int gui_check_warning(void) {
    if (!gui_mode && config.check_gui_default) {
        if (check_gui_software() > 0) return 1;
    }
    
    if (gui_mode) {
        int count = check_gui_software();
        if (count == 0) {
            printf("\n\033[1;31mWARNING: No GUI software found!\033[0m\n");
            printf("You need at least: i3wm, xterm\n");
            printf("Install: pkg install i3 xterm\n");
            printf("\nContinue anyway? [y/N]: ");
            
            char response[10];
            if (fgets(response, sizeof(response), stdin)) {
                return (response[0] == 'y' || response[0] == 'Y');
            }
            return 0;
        }
    }
    
    return 1;
}

static void list_all_gui_software(void) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    
    const char *categories[][20] = {
        {"Window Managers", "i3", "openbox", "awesome", NULL},
        {"Terminals", "xterm", "uxterm", "lxterminal", NULL},
        {"Browsers", "firefox", "chromium", NULL},
        {"File Managers", "pcmanfm", "thunar", NULL},
        {"Launchers", "dmenu", "rofi", NULL},
        {NULL}
    };
    
    int selected = 0;
    int total = 0;
    for (int i = 0; categories[i][0]; i++) {
        for (int j = 1; categories[i][j]; j++) total++;
    }
    
    int ch;
    do {
        clear();
        attron(A_BOLD);
        mvprintw(1, 2, "GUI Software List - Press q to quit");
        attroff(A_BOLD);
        
        int y = 3, idx = 0;
        for (int i = 0; categories[i][0]; i++) {
            mvprintw(y++, 2, "%s:", categories[i][0]);
            for (int j = 1; categories[i][j]; j++) {
                if (idx == selected) attron(A_REVERSE);
                
                int installed = cmd_exists(categories[i][j]);
                mvprintw(y++, 4, "[%s] %s", installed ? "OK" : "--", categories[i][j]);
                
                if (idx == selected) attroff(A_REVERSE);
                idx++;
            }
            y++;
        }
        
        refresh();
        ch = getch();
        
        switch (ch) {
            case KEY_UP: if (selected > 0) selected--; break;
            case KEY_DOWN: if (selected < total - 1) selected++; break;
        }
    } while (ch != 'q' && ch != 'Q');
    
    endwin();
}

static void start_i3(void) {
    printf("Starting i3...\n");
    
    char i3_config[1024];
    snprintf(i3_config, sizeof(i3_config), "%s/.config/i3/config", getenv("HOME"));
    
    if (access(i3_config, F_OK) != 0) {
        system("mkdir -p ~/.config/i3");
        system("echo 'exec xterm' > ~/.config/i3/config");
    }
    
    execlp("i3", "i3", NULL);
    perror("Failed to start i3");
}

static void show_de_settings_menu(void) {
    initscr();
    start_color();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    
    init_pair(1, COLOR_WHITE, COLOR_RED);
    init_pair(2, COLOR_BLACK, COLOR_WHITE);
    init_pair(3, COLOR_YELLOW, COLOR_RED);
    
    int selected = 0;
    const char *options[] = {
        "Disable all options start only",
        "Checks all GUI software by default",
        "On ctrl + X lists all gui software",
        "Do you use tiled",
        "Save to folder"
    };
    
    int ch;
    do {
        bkgd(COLOR_PAIR(1));
        clear();
        
        attron(COLOR_PAIR(1) | A_BOLD);
        mvprintw(1, 2, "Desktop environment settings for status-nast");
        attroff(A_BOLD);
        
        if (config.disable_all_options) {
            attron(COLOR_PAIR(3));
            mvprintw(2, 2, "!!! ALL OPTIONS DISABLED !!!");
            attroff(COLOR_PAIR(3));
        }
        
        for (int i = 0; i < 5; i++) {
            int y = 4 + i * 2;
            if (i == selected) attron(COLOR_PAIR(2));
            else attron(COLOR_PAIR(1));
            
            const char *status = "";
            switch (i) {
                case 0: status = config.disable_all_options ? "[open]" : "[close]"; break;
                case 1: status = config.check_gui_default ? "[open]" : "[close]"; break;
                case 2: status = config.ctrl_x_list_gui ? "[open]" : "[close]"; break;
                case 3: status = config.use_tiled ? "[open]" : "[close]"; break;
                case 4: status = "[press Enter]"; break;
            }
            
            mvprintw(y, 4, "%s %s", options[i], status);
            
            if (i == selected) attroff(COLOR_PAIR(2));
            else attroff(COLOR_PAIR(1));
        }
        
        attron(COLOR_PAIR(1));
        mvprintw(16, 2, "Keys: [x]Disable [u]GUI check [q]Ctrl+X [t]Tiled [s]Save [Q]Quit");
        
        refresh();
        ch = getch();
        
        switch (ch) {
            case KEY_UP: if (selected > 0) selected--; break;
            case KEY_DOWN: if (selected < 4) selected++; break;
            case 'x':
                config.disable_all_options = !config.disable_all_options;
                if (config.disable_all_options) {
                    config.check_gui_default = 0;
                    config.ctrl_x_list_gui = 0;
                    config.use_tiled = 0;
                }
                break;
            case 'u':
                if (!config.disable_all_options)
                    config.check_gui_default = !config.check_gui_default;
                break;
            case 'q':
                if (selected == 2) {
                    if (!config.disable_all_options)
                        config.ctrl_x_list_gui = !config.ctrl_x_list_gui;
                } else {
                    endwin();
                    return;
                }
                break;
            case 't':
                if (!config.disable_all_options)
                    config.use_tiled = !config.use_tiled;
                break;
            case 's':
            case '\n':
                if (selected == 4 || ch == 's') save_config();
                break;
            case 'Q':
                endwin();
                return;
        }
    } while (1);
}

static void usage(void) {
    printf("Usage: %s [OPTION]...\n", PROGRAM_NAME);
    printf("A desktop environment using i3.\n");
    printf("\nOptions:\n");
    printf("  -g, --gui         Check GUI software\n");
    printf("  -c, --config      Show config menu\n");
    printf("  -h, --help        Display help\n");
    printf("  -v, --version     Show version\n");
}

static void version(void) {
    printf("%s %s\n", PROGRAM_NAME, VERSION);
}

int main(int argc, char *argv[]) {
    int opt;
    int show_config = 0;
    
    while ((opt = getopt(argc, argv, "gchv")) != -1) {
        switch (opt) {
            case 'g': gui_mode = 1; break;
            case 'c': show_config = 1; break;
            case 'h': usage(); return 0;
            case 'v': version(); return 0;
            default: usage(); return 1;
        }
    }
    
    load_config();
    
    if (show_config) {
        show_de_settings_menu();
        return 0;
    }
    
    if (config.ctrl_x_list_gui && !config.disable_all_options) {
        printf("Tip: Press Ctrl+X to list GUI software\n");
    }
    
    if (!gui_check_warning()) return 1;
    
    if (config.disable_all_options) {
        printf("Start only mode...\n");
        start_i3();
        return 0;
    }
    
    printf("Tiled mode: %s\n", config.use_tiled ? "ON" : "OFF");
    start_i3();
    
    return 0;
}
