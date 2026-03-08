#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROGRAM_NAME "have"

/* 支持的包管理器检测命令 */
typedef struct {
    const char *name;           /* 包管理器名称 */
    const char *check_cmd;      /* 检查命令模板 */
    const char *query_format;   /* 查询格式 */
} pkg_manager_t;

/* 各种系统的包管理器 */
static pkg_manager_t managers[] = {
    /* Debian/Ubuntu */
    {"dpkg", "dpkg -l \"%s\" 2>/dev/null | grep -q \"^ii\"", "dpkg -l \"%s\""},
    
    /* RHEL/CentOS/Fedora (旧版) */
    {"rpm", "rpm -q \"%s\" >/dev/null 2>&1", "rpm -q \"%s\""},
    
    /* Arch Linux */
    {"pacman", "pacman -Q \"%s\" >/dev/null 2>&1", "pacman -Q \"%s\""},
    
    /* Alpine */
    {"apk", "apk info -e \"%s\" >/dev/null 2>&1", "apk info \"%s\""},
    
    /* Gentoo */
    {"portage", "equery list \"%s\" >/dev/null 2>&1", "equery list \"%s\""},
    
    /* openSUSE */
    {"zypper", "zypper se -i \"%s\" 2>/dev/null | grep -q \"^i\"", "zypper se -i \"%s\""},
    
    /* Void Linux */
    {"xbps", "xbps-query -S \"%s\" >/dev/null 2>&1", "xbps-query \"%s\""},
    
    /* Nix */
    {"nix", "nix-env -q \"%s\" 2>/dev/null | grep -q .", "nix-env -q \"%s\""},
    
    /* FreeBSD */
    {"pkg", "pkg info -e \"%s\"", "pkg info \"%s\""},
    
    /* macOS Homebrew */
    {"brew", "brew list \"%s\" >/dev/null 2>&1", "brew list \"%s\""},
    
    /* macOS MacPorts */
    {"port", "port installed \"%s\" 2>/dev/null | grep -q \"active\"", "port installed \"%s\""},
    
    /* OpenBSD */
    {"pkg_info", "pkg_info -e \"%s\" >/dev/null 2>&1", "pkg_info \"%s\""},
    
    /* NetBSD */
    {"pkgin", "pkgin list 2>/dev/null | grep -q \"^%s-\"", "pkgin list | grep \"%s\""},
    
    /* Slackware */
    {"slackpkg", "ls /var/log/packages/\"%s\"* >/dev/null 2>&1", "ls /var/log/packages/\"%s\"*"},
    
    /* 通用：检查命令是否存在 */
    {"command", "command -v \"%s\" >/dev/null 2>&1", "command -v \"%s\""},
    
    {NULL, NULL, NULL}
};

/* 检测当前系统使用的包管理器 */
static pkg_manager_t* detect_package_manager(void) {
    /* 按优先级检测 */
    
    /* Debian/Ubuntu */
    if (access("/usr/bin/dpkg", X_OK) == 0) {
        return &managers[0];
    }
    
    /* Arch */
    if (access("/usr/bin/pacman", X_OK) == 0) {
        return &managers[2];
    }
    
    /* Alpine */
    if (access("/sbin/apk", X_OK) == 0) {
        return &managers[3];
    }
    
    /* Fedora/RHEL (新版用 dnf，但 rpm 通用) */
    if (access("/usr/bin/rpm", X_OK) == 0) {
        return &managers[1];
    }
    
    /* Gentoo */
    if (access("/usr/bin/equery", X_OK) == 0 || access("/usr/bin/emerge", X_OK) == 0) {
        return &managers[4];
    }
    
    /* openSUSE */
    if (access("/usr/bin/zypper", X_OK) == 0) {
        return &managers[5];
    }
    
    /* Void */
    if (access("/usr/bin/xbps-query", X_OK) == 0) {
        return &managers[6];
    }
    
    /* Nix */
    if (access("/run/current-system/nixos-version", F_OK) == 0 || 
        getenv("NIX_PATH") != NULL) {
        return &managers[7];
    }
    
    /* FreeBSD */
    if (access("/usr/sbin/pkg", X_OK) == 0) {
        return &managers[8];
    }
    
    /* macOS Homebrew */
    if (access("/opt/homebrew/bin/brew", X_OK) == 0 || 
        access("/usr/local/bin/brew", X_OK) == 0) {
        return &managers[9];
    }
    
    /* macOS MacPorts */
    if (access("/opt/local/bin/port", X_OK) == 0) {
        return &managers[10];
    }
    
    /* OpenBSD */
    if (access("/usr/sbin/pkg_info", X_OK) == 0) {
        return &managers[11];
    }
    
    /* NetBSD */
    if (access("/usr/pkg/bin/pkgin", X_OK) == 0) {
        return &managers[12];
    }
    
    /* Slackware */
    if (access("/var/log/packages", F_OK) == 0) {
        return &managers[13];
    }
    
    /* 默认使用 command -v 检查命令是否存在 */
    return &managers[14];
}

/* 检查包是否安装 */
static int check_package(const char *package, pkg_manager_t *pm) {
    char cmd[512];
    
    snprintf(cmd, sizeof(cmd), pm->check_cmd, package);
    
    int ret = system(cmd);
    
    /* system() 返回值需要处理 */
    if (WIFEXITED(ret)) {
        return WEXITSTATUS(ret) == 0;
    }
    
    return 0;
}

/* 显示查询命令（调试用） */
static void show_query_command(const char *package, pkg_manager_t *pm) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), pm->query_format, package);
    printf("# Query with: %s\n", cmd);
}

int main(int argc, char *argv[]) {
    /* 无参数时显示用法 */
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <package-name>\n", PROGRAM_NAME);
        fprintf(stderr, "       %s <command-name>\n", PROGRAM_NAME);
        fprintf(stderr, "\n");
        fprintf(stderr, "Check if a package or command is installed on the system.\n");
        fprintf(stderr, "Automatically detects the package manager.\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Exit codes:\n");
        fprintf(stderr, "  0  Package/command is installed\n");
        fprintf(stderr, "  1  Package/command is not installed\n");
        fprintf(stderr, "  2  Error occurred\n");
        return 2;
    }
    
    const char *target = argv[1];
    
    /* 检测包管理器 */
    pkg_manager_t *pm = detect_package_manager();
    
    /* 检查包 */
    int installed = check_package(target, pm);
    
    /* 输出结果 */
    if (installed) {
        printf("Yes, '%s' is installed (detected via %s)\n", target, pm->name);
        return 0;
    } else {
        printf("No, '%s' is not installed\n", target);
        
        /* 尝试用 command -v 作为后备检查 */
        if (pm->name[0] != 'c') {  /* 如果不是已经在用 command */
            pkg_manager_t *cmd_pm = &managers[14];
            if (check_package(target, cmd_pm)) {
                printf("But '%s' command is available in PATH\n", target);
                return 0;
            }
        }
        
        return 1;
    }
}
