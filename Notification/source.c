#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <pwd.h>

#define PROGRAM_NAME "notification"
#define VERSION "1.0"

/* 通知目标 */
typedef enum {
    TARGET_DEFAULT,
    TARGET_SYSTEM,
    TARGET_USER
} target_t;

static target_t target = TARGET_DEFAULT;
static int user_id = -1;  /* -u 指定的用户ID */

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
    printf("Usage: %s [OPTION] [TEXT]\n", PROGRAM_NAME);
    printf("Send notification to system or specific user.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -s, --system         Send notification to system (Android system UI)\n");
    printf("  -u, --user UID       Send notification to specific user by ID\n");
    printf("  -h, --help           Display this help and exit\n");
    printf("  -v, --version        Output version information and exit\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s \"Hello World\"              Send notification (default method)\n", PROGRAM_NAME);
    printf("  %s -s \"System alert\"          Send system notification\n", PROGRAM_NAME);
    printf("  %s -u 0 \"Root message\"        Send to root user\n", PROGRAM_NAME);
    printf("  %s -u 1000 \"User message\"     Send to user 1000\n", PROGRAM_NAME);
    printf("\n");
    printf("Note: On Android, this requires termux-api or root access.\n");
}

/* 版本信息 */
static void version(void) {
    printf("%s %s\n", PROGRAM_NAME, VERSION);
}

/* 检查命令是否存在 */
static int cmd_exists(const char *cmd) {
    char check[256];
    snprintf(check, sizeof(check), "which %s >/dev/null 2>&1", cmd);
    return system(check) == 0;
}

/* 发送系统通知 (Android) */
static int send_system_notification(const char *text) {
    /* 方法1: termux-notification (推荐) */
    if (cmd_exists("termux-notification")) {
        char cmd[4096];
        snprintf(cmd, sizeof(cmd), "termux-notification --title 'Notification' --content '%s'", text);
        return system(cmd);
    }
    
    /* 方法2: am (Activity Manager) */
    if (cmd_exists("am")) {
        char cmd[4096];
        /* 使用 Android Toast */
        snprintf(cmd, sizeof(cmd), 
            "am startservice -n com.termux/com.termux.app.TermuxService "
            "--es com.termux.execute.argument 'echo \"%s\"' 2>/dev/null || "
            "am broadcast -a android.intent.action.BOOT_COMPLETED 2>/dev/null",
            text);
        system(cmd);
        
        /* 尝试直接 Toast */
        snprintf(cmd, sizeof(cmd),
            "am broadcast -a android.intent.action.VIEW "
            "-n com.android.shell/.BugreportNotificationService "
            "--es android.intent.extra.TEXT '%s' 2>/dev/null",
            text);
        return system(cmd);
    }
    
    /* 方法3: notify-send (Linux桌面) */
    if (cmd_exists("notify-send")) {
        char cmd[4096];
        snprintf(cmd, sizeof(cmd), "notify-send 'Notification' '%s'", text);
        return system(cmd);
    }
    
    /* 方法4: wall (系统广播) */
    if (cmd_exists("wall")) {
        char cmd[4096];
        snprintf(cmd, sizeof(cmd), "echo '%s' | wall 2>/dev/null", text);
        return system(cmd);
    }
    
    error("No notification method available");
    error("Install termux-api: pkg install termux-api");
    return 1;
}

/* 发送给用户通知 */
static int send_user_notification(int uid, const char *text) {
    /* 方法1: termux-notification --id (如果可以指定用户) */
    if (cmd_exists("termux-notification")) {
        char cmd[4096];
        snprintf(cmd, sizeof(cmd), 
            "termux-notification --id user%d --title 'User %d' --content '%s'",
            uid, uid, text);
        return system(cmd);
    }
    
    /* 方法2: 使用 write 发送给用户的终端 */
    struct passwd *pw = getpwuid(uid);
    if (pw && pw->pw_name) {
        /* 查找用户的终端 */
        char tty_cmd[256];
        snprintf(tty_cmd, sizeof(tty_cmd), 
            "who | grep '^%s ' | head -1 | awk '{print $2}'", pw->pw_name);
        
        FILE *fp = popen(tty_cmd, "r");
        char tty[32] = "";
        if (fp) {
            fgets(tty, sizeof(tty), fp);
            tty[strcspn(tty, "\n")] = '\0';
            pclose(fp);
        }
        
        if (tty[0]) {
            char path[64];
            snprintf(path, sizeof(path), "/dev/%s", tty);
            FILE *ftty = fopen(path, "w");
            if (ftty) {
                fprintf(ftty, "\n\007Message from %s to user %d:\n%s\n\n", 
                        PROGRAM_NAME, uid, text);
                fclose(ftty);
                return 0;
            }
        }
    }
    
    /* 方法3: 使用 write 命令 */
    if (cmd_exists("write")) {
        char cmd[4096];
        snprintf(cmd, sizeof(cmd), "echo '%s' | write %s 2>/dev/null", 
                text, pw ? pw->pw_name : "");
        if (system(cmd) == 0) return 0;
    }
    
    /* 方法4: 使用 notify-send 指定用户 (需要 DBUS) */
    if (cmd_exists("notify-send")) {
        char cmd[4096];
        snprintf(cmd, sizeof(cmd), 
            "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/%d/bus "
            "notify-send 'Notification' '%s' 2>/dev/null",
            uid, text);
        return system(cmd);
    }
    
    error("Cannot send notification to user %d", uid);
    return 1;
}

/* 发送默认通知 */
static int send_default_notification(const char *text) {
    /* 尝试系统通知 */
    return send_system_notification(text);
}

int main(int argc, char *argv[]) {
    int opt;
    
    static struct option long_options[] = {
        {"system", no_argument, 0, 's'},
        {"user", required_argument, 0, 'u'},
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {0, 0, 0, 0}
    };
    
    while ((opt = getopt_long(argc, argv, "su:hv", long_options, NULL)) != -1) {
        switch (opt) {
            case 's':
                target = TARGET_SYSTEM;
                break;
            case 'u':
                target = TARGET_USER;
                user_id = atoi(optarg);
                if (user_id < 0) {
                    error("invalid user ID: %s", optarg);
                    return 1;
                }
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
    
    /* 获取通知文本 */
    char text[4096] = "";
    
    if (optind >= argc) {
        /* 从 stdin 读取 */
        if (fgets(text, sizeof(text), stdin) == NULL) {
            error("missing notification text");
            usage();
            return 1;
        }
        text[strcspn(text, "\n")] = '\0';
    } else {
        /* 从参数拼接 */
        int first = 1;
        for (int i = optind; i < argc; i++) {
            if (!first) strncat(text, " ", sizeof(text) - strlen(text) - 1);
            strncat(text, argv[i], sizeof(text) - strlen(text) - 1);
            first = 0;
        }
    }
    
    if (strlen(text) == 0) {
        error("empty notification text");
        return 1;
    }
    
    /* 发送通知 */
    int ret = 0;
    switch (target) {
        case TARGET_SYSTEM:
            ret = send_system_notification(text);
            break;
        case TARGET_USER:
            if (user_id < 0) {
                error("user ID not specified");
                return 1;
            }
            ret = send_user_notification(user_id, text);
            break;
        default:
            ret = send_default_notification(text);
            break;
    }
    
    return (ret == 0) ? 0 : 1;
}
