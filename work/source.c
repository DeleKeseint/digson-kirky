#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <errno.h>
#include <stdarg.h>

#define PROGRAM_NAME "work"
#define VERSION "1.0"

/* 选项标志 */
static int use_root = 0;        /* --ko, --root, -r: 以管理员身份执行 */
static int no_sudo = 0;         /* --no-sudo, --no-root, -n: 禁止 sudo */

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
    printf("Usage: %s [OPTION]... COMMAND [ARG]...\n", PROGRAM_NAME);
    printf("Execute COMMAND with optional privilege control.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -r, --ko, --root        Execute COMMAND as root (using sudo)\n");
    printf("  -n, --no-sudo, --no-root  Prevent COMMAND from running with sudo\n");
    printf("  -h, --help              Display this help and exit\n");
    printf("  -v, --version           Output version information and exit\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s ls -la                  Execute 'ls -la'\n", PROGRAM_NAME);
    printf("  %s -r apt update           Execute 'sudo apt update'\n", PROGRAM_NAME);
    printf("  %s -n some-script.sh       Execute script, fail if it uses sudo\n", PROGRAM_NAME);
    printf("  %s -r -n command           Invalid: cannot use both flags\n", PROGRAM_NAME);
    printf("\n");
    printf("Exit status:\n");
    printf("  0   Success\n");
    printf("  1   General error\n");
    printf("  126 COMMAND is found but cannot be invoked\n");
    printf("  127 COMMAND cannot be found\n");
}

/* 版本信息 */
static void version(void) {
    printf("%s %s\n", PROGRAM_NAME, VERSION);
}

/* 检查命令是否以 sudo 开头 */
static int check_sudo_in_command(char **argv, int start_idx) {
    /* 检查命令名 */
    if (strcmp(argv[start_idx], "sudo") == 0) {
        return 1;
    }
    
    /* 检查路径中的命令，如 /usr/bin/sudo */
    if (strstr(argv[start_idx], "sudo") != NULL) {
        /* 更严格的检查：确保是 sudo 命令 */
        const char *cmd = argv[start_idx];
        size_t len = strlen(cmd);
        if (len >= 4 && strcmp(cmd + len - 4, "sudo") == 0) {
            return 1;
        }
        if (strncmp(cmd, "sudo", 4) == 0 && (cmd[4] == '\0' || cmd[4] == '.')) {
            return 1;
        }
    }
    
    return 0;
}

/* 检查命令脚本内容是否包含 sudo（简单检查） */
static int check_script_for_sudo(const char *cmd) {
    /* 检查常见脚本后缀 */
    size_t len = strlen(cmd);
    int is_script = 0;
    
    if (len > 3 && strcmp(cmd + len - 3, ".sh") == 0) is_script = 1;
    if (len > 4 && strcmp(cmd + len - 4, ".bash") == 0) is_script = 1;
    if (len > 3 && strcmp(cmd + len - 3, ".py") == 0) is_script = 1;
    if (len > 3 && strcmp(cmd + len - 3, ".pl") == 0) is_script = 1;
    if (len > 3 && strcmp(cmd + len - 3, ".rb") == 0) is_script = 1;
    
    if (!is_script) return 0;
    
    /* 尝试打开文件检查内容 */
    FILE *fp = fopen(cmd, "r");
    if (!fp) return 0;
    
    char line[1024];
    int found_sudo = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        /* 检查行中是否包含 sudo（作为独立命令） */
        if (strstr(line, "sudo ") != NULL || strstr(line, "sudo\t") != NULL ||
            strstr(line, "sudo\n") != NULL || strstr(line, "sudo;") != NULL ||
            strcmp(line, "sudo\n") == 0) {
            found_sudo = 1;
            break;
        }
        /* 检查 $(sudo ...) 或 `sudo ...` */
        if (strstr(line, "$(sudo") != NULL || strstr(line, "`sudo") != NULL) {
            found_sudo = 1;
            break;
        }
    }
    
    fclose(fp);
    return found_sudo;
}

/* 检查当前是否已经是 root */
static int is_root(void) {
    return geteuid() == 0;
}

/* 获取 sudo 路径 */
static const char* get_sudo_path(void) {
    static const char *paths[] = {
        "/usr/bin/sudo",
        "/bin/sudo",
        "/usr/local/bin/sudo",
        "/sbin/sudo",
        "/usr/sbin/sudo",
        NULL
    };
    
    for (int i = 0; paths[i]; i++) {
        if (access(paths[i], X_OK) == 0) {
            return paths[i];
        }
    }
    
    return "sudo"; /* 回退到 PATH 搜索 */
}

int main(int argc, char *argv[]) {
    int opt;
    int cmd_start = 1;  /* 命令在 argv 中的起始位置 */
    
    /* 解析选项 */
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        
        /* 停止解析选项 */
        if (strcmp(arg, "--") == 0) {
            cmd_start = i + 1;
            break;
        }
        
        /* 长选项 */
        if (strcmp(arg, "--help") == 0) {
            usage();
            return 0;
        }
        if (strcmp(arg, "--version") == 0) {
            version();
            return 0;
        }
        if (strcmp(arg, "--ko") == 0 || strcmp(arg, "--root") == 0) {
            use_root = 1;
            if (cmd_start == i) cmd_start = i + 1;
            continue;
        }
        if (strcmp(arg, "--no-sudo") == 0 || strcmp(arg, "--no-root") == 0) {
            no_sudo = 1;
            if (cmd_start == i) cmd_start = i + 1;
            continue;
        }
        
        /* 短选项 */
        if (arg[0] == '-' && arg[1] != '\0' && arg[2] == '\0') {
            switch (arg[1]) {
                case 'h':
                    usage();
                    return 0;
                case 'v':
                    version();
                    return 0;
                case 'r':
                    use_root = 1;
                    if (cmd_start == i) cmd_start = i + 1;
                    continue;
                case 'n':
                    no_sudo = 1;
                    if (cmd_start == i) cmd_start = i + 1;
                    continue;
                default:
                    error("invalid option -- '%c'", arg[1]);
                    usage();
                    return 1;
            }
        }
        
        /* 非选项参数，即为命令开始 */
        if (arg[0] != '-' || (arg[0] == '-' && arg[1] == '\0')) {
            if (cmd_start <= i) {
                cmd_start = i;
            }
            break;
        }
    }
    
    /* 检查冲突选项 */
    if (use_root && no_sudo) {
        error("cannot use both --root and --no-sudo options");
        return 1;
    }
    
    /* 检查是否有命令 */
    if (cmd_start >= argc) {
        error("missing command");
        usage();
        return 1;
    }
    
    char **cmd_argv = &argv[cmd_start];
    const char *cmd = cmd_argv[0];
    
    /* 检查 --no-sudo: 禁止 sudo */
    if (no_sudo) {
        /* 检查命令本身是否是 sudo */
        if (check_sudo_in_command(cmd_argv, 0)) {
            error("command contains 'sudo' which is not allowed with --no-sudo");
            return 1;
        }
        
        /* 如果是脚本，检查内容 */
        if (access(cmd, F_OK) == 0 && check_script_for_sudo(cmd)) {
            error("script '%s' appears to contain sudo commands", cmd);
            return 1;
        }
        
        /* 设置环境变量标记，供子进程检测 */
        setenv("WORK_NO_SUDO", "1", 1);
    }
    
    /* 检查 --root: 使用 sudo 执行 */
    if (use_root) {
        /* 如果已经是 root，直接执行 */
        if (is_root()) {
            execvp(cmd, cmd_argv);
            error("failed to execute '%s': %s", cmd, strerror(errno));
            return 126;
        }
        
        /* 构建 sudo 命令 */
        const char *sudo_path = get_sudo_path();
        
        /* 计算新参数数组大小 */
        int cmd_argc = 0;
        while (cmd_argv[cmd_argc]) cmd_argc++;
        
        /* 分配新参数数组: sudo [options] cmd args... */
        char **new_argv = malloc((cmd_argc + 3) * sizeof(char *));
        if (!new_argv) {
            error("memory allocation failed");
            return 1;
        }
        
        new_argv[0] = (char *)sudo_path;
        new_argv[1] = (char *)cmd;  /* sudo 会自动处理 -E 等选项 */
        
        /* 复制原参数 */
        for (int i = 1; i < cmd_argc; i++) {
            new_argv[i + 1] = cmd_argv[i];
        }
        new_argv[cmd_argc + 1] = NULL;
        
        /* 执行 sudo */
        execv(sudo_path, new_argv);
        
        /* 如果失败，尝试通过 PATH 查找 */
        execvp("sudo", new_argv);
        
        error("failed to execute sudo: %s", strerror(errno));
        free(new_argv);
        return 126;
    }
    
    /* 普通执行 */
    execvp(cmd, cmd_argv);
    
    /* 执行失败 */
    int saved_errno = errno;
    error("failed to execute '%s': %s", cmd, strerror(saved_errno));
    
    if (saved_errno == ENOENT) {
        return 127;
    }
    return 126;
}
