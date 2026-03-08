#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#define PROGRAM_NAME "give"

/* 权限位定义（与系统一致） */
#define S_ISUID  04000  /* Set user ID */
#define S_ISGID  02000  /* Set group ID */
#define S_ISVTX  01000  /* Save text (sticky bit) */

#define S_IRUSR  00400  /* Read by owner */
#define S_IWUSR  00200  /* Write by owner */
#define S_IXUSR  00100  /* Execute/search by owner */

#define S_IRGRP  00040  /* Read by group */
#define S_IWGRP  00020  /* Write by group */
#define S_IXGRP  00010  /* Execute/search by group */

#define S_IROTH  00004  /* Read by others */
#define S_IWOTH  00002  /* Write by others */
#define S_IXOTH  00001  /* Execute/search by others */

#define MODE_MASK 07777

/* 符号模式操作类型 */
typedef enum {
    OP_ADD,     /* + */
    OP_REMOVE,  /* - */
    OP_SET      /* = */
} op_t;

/* 权限类别 */
typedef enum {
    WHO_USER  = 1,   /* u */
    WHO_GROUP = 2,   /* g */
    WHO_OTHER = 4,   /* o */
    WHO_ALL   = 7    /* a */
} who_t;

/* 解析数字模式 */
static mode_t parse_numeric_mode(const char *str) {
    char *endptr;
    long mode = strtol(str, &endptr, 8);
    
    if (*endptr != '\0' || mode < 0 || mode > 07777) {
        return (mode_t)-1;
    }
    return (mode_t)(mode & MODE_MASK);
}

/* 解析符号模式中的 who 部分 */
static int parse_who(const char **p) {
    int who = 0;
    int has_who = 0;
    
    while (**p) {
        switch (**p) {
            case 'u': who |= WHO_USER;  (*p)++; has_who = 1; break;
            case 'g': who |= WHO_GROUP; (*p)++; has_who = 1; break;
            case 'o': who |= WHO_OTHER; (*p)++; has_who = 1; break;
            case 'a': who |= WHO_ALL;   (*p)++; has_who = 1; break;
            default: goto done;
        }
    }
done:
    return has_who ? who : 0;  /* 返回0表示没有指定，将使用默认 */
}

/* 解析权限位 */
static int parse_perms(const char **p) {
    int perms = 0;
    
    while (**p) {
        switch (**p) {
            case 'r': perms |= 0444; (*p)++; break;
            case 'w': perms |= 0222; (*p)++; break;
            case 'x': perms |= 0111; (*p)++; break;
            case 'X': perms |= 0111; (*p)++; break; /* 特殊处理在调用处 */
            case 's': perms |= 06000; (*p)++; break; /* setuid/setgid */
            case 't': perms |= 01000; (*p)++; break; /* sticky */
            default: return perms;
        }
    }
    return perms;
}

/* 应用符号模式 */
static mode_t apply_mode(mode_t current, const char *mode_str) {
    const char *p = mode_str;
    mode_t new_mode = current;
    int who;
    int perms;
    op_t op;
    
    while (*p) {
        /* 解析 who */
        who = parse_who(&p);
        if (who == 0) who = WHO_ALL;  /* 默认 a */
        
        /* 必须有操作符 */
        if (*p != '+' && *p != '-' && *p != '=') {
            return (mode_t)-1;
        }
        
        switch (*p) {
            case '+': op = OP_ADD; break;
            case '-': op = OP_REMOVE; break;
            case '=': op = OP_SET; break;
            default: return (mode_t)-1;
        }
        p++;
        
        /* 解析权限 */
        perms = parse_perms(&p);
        
        /* 应用操作 */
        if (op == OP_SET) {
            /* = 操作：先清除 who 的权限，再添加 */
            mode_t mask = 0;
            if (who & WHO_USER)  mask |= 04700;
            if (who & WHO_GROUP) mask |= 02070;
            if (who & WHO_OTHER) mask |= 00007;
            new_mode &= ~mask;
            /* 添加新权限 */
            if (who & WHO_USER)  new_mode |= (perms & 0700) | (perms & 06000);
            if (who & WHO_GROUP) new_mode |= ((perms & 0070) << 3) | (perms & 06000);
            if (who & WHO_OTHER) new_mode |= ((perms & 0007) << 6) | (perms & 01000);
        } else if (op == OP_ADD) {
            if (who & WHO_USER)  new_mode |= (perms & 0700) | (perms & 06000);
            if (who & WHO_GROUP) new_mode |= ((perms & 0070) << 3) | (perms & 06000);
            if (who & WHO_OTHER) new_mode |= ((perms & 0007) << 6) | (perms & 01000);
        } else { /* OP_REMOVE */
            if (who & WHO_USER)  new_mode &= ~((perms & 0700) | (perms & 06000));
            if (who & WHO_GROUP) new_mode &= ~(((perms & 0070) << 3) | (perms & 06000));
            if (who & WHO_OTHER) new_mode &= ~(((perms & 0007) << 6) | (perms & 01000));
        }
        
        /* 检查是否有逗号分隔的更多操作 */
        if (*p == ',') {
            p++;
        } else if (*p != '\0') {
            return (mode_t)-1;  /* 非法字符 */
        }
    }
    
    return new_mode & MODE_MASK;
}

/* 显示使用帮助 */
static void usage(int status) {
    fprintf(status == 0 ? stdout : stderr,
        "Usage: %s [OPTION]... MODE[,MODE]... FILE...\n"
        "  or:  %s [OPTION]... OCTAL-MODE FILE...\n"
        "  or:  %s [OPTION]... --reference=RFILE FILE...\n"
        "\n"
        "Change the mode of each FILE to MODE.\n"
        "\n"
        "  -c, --changes          like verbose but report only when a change is made\n"
        "  -f, --silent, --quiet  suppress most error messages\n"
        "  -v, --verbose          output a diagnostic for every file processed\n"
        "      --no-preserve-root  do not treat '/' specially (the default)\n"
        "      --preserve-root    fail to operate recursively on '/'\n"
        "      --reference=RFILE  use RFILE's mode instead of MODE values\n"
        "  -R, --recursive        change files and directories recursively\n"
        "      --help             display this help and exit\n"
        "      --version          output version information and exit\n"
        "\n"
        "Each MODE is of the form '[ugoa]*([-+=]([rwxXst]*|[ugo]))+'.\n"
        "\n", PROGRAM_NAME, PROGRAM_NAME, PROGRAM_NAME);
    exit(status);
}

/* 版本信息 */
static void version(void) {
    printf("%s (GNU coreutils compatible) 1.0\n", PROGRAM_NAME);
    exit(0);
}

/* 递归修改权限 */
static int recursive_chmod(const char *path, mode_t mode, 
                          int verbose, int changes, int quiet) {
    /* 简化版：实际应使用 nftw() 或 opendir/readdir */
    fprintf(stderr, "%s: recursive mode not fully implemented\n", PROGRAM_NAME);
    return chmod(path, mode);
}

int main(int argc, char *argv[]) {
    int opt;
    int verbose = 0;
    int changes = 0;
    int quiet = 0;
    int recursive = 0;
    int preserve_root = 0;
    char *reference = NULL;
    mode_t new_mode;
    int mode_specified = 0;
    int ret = 0;
    
    /* 解析长选项（简化版，实际需要 getopt_long） */
    for (int i = 1; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            usage(0);
        } else if (strcmp(argv[i], "--version") == 0) {
            version();
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--changes") == 0) {
            changes = verbose = 1;
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--silent") == 0 
                   || strcmp(argv[i], "--quiet") == 0) {
            quiet = 1;
        } else if (strcmp(argv[i], "-R") == 0 || strcmp(argv[i], "--recursive") == 0) {
            recursive = 1;
        } else if (strcmp(argv[i], "--preserve-root") == 0) {
            preserve_root = 1;
        } else if (strcmp(argv[i], "--no-preserve-root") == 0) {
            preserve_root = 0;
        } else if (strncmp(argv[i], "--reference=", 12) == 0) {
            reference = argv[i] + 12;
        } else if (argv[i][1] != '-') {
            /* 短选项组合 */
            for (char *p = argv[i] + 1; *p; p++) {
                switch (*p) {
                    case 'v': verbose = 1; break;
                    case 'c': changes = verbose = 1; break;
                    case 'f': quiet = 1; break;
                    case 'R': recursive = 1; break;
                    default:
                        fprintf(stderr, "%s: invalid option -- '%c'\n", 
                                PROGRAM_NAME, *p);
                        usage(1);
                }
            }
        }
    }
    
    /* 找到第一个非选项参数（模式） */
    int mode_idx = 1;
    while (mode_idx < argc && argv[mode_idx][0] == '-' && 
           strcmp(argv[mode_idx], "--") != 0) {
        if (strncmp(argv[mode_idx], "--reference=", 12) == 0) {
            reference = argv[mode_idx] + 12;
            mode_idx++;
            continue;
        }
        if (argv[mode_idx][0] == '-' && argv[mode_idx][1] != '-') {
            /* 跳过短选项 */
            mode_idx++;
        } else {
            mode_idx++;
        }
    }
    
    if (reference) {
        /* 从参考文件获取模式 */
        struct stat st;
        if (stat(reference, &st) < 0) {
            if (!quiet) {
                fprintf(stderr, "%s: failed to get attributes of '%s': %s\n",
                        PROGRAM_NAME, reference, strerror(errno));
            }
            return 1;
        }
        new_mode = st.st_mode & MODE_MASK;
        mode_specified = 1;
    } else {
        /* 需要模式参数 */
        if (mode_idx >= argc) {
            fprintf(stderr, "%s: missing operand\n", PROGRAM_NAME);
            usage(1);
        }
        
        const char *mode_str = argv[mode_idx];
        
        /* 判断是数字模式还是符号模式 */
        if (mode_str[0] >= '0' && mode_str[0] <= '7') {
            new_mode = parse_numeric_mode(mode_str);
            if (new_mode == (mode_t)-1) {
                fprintf(stderr, "%s: invalid mode: '%s'\n", PROGRAM_NAME, mode_str);
                return 1;
            }
        } else {
            /* 符号模式 - 需要文件已存在来获取当前权限 */
            /* 这里简化处理，实际应该对每个文件单独计算 */
            new_mode = (mode_t)-1;  /* 标记为符号模式 */
        }
        mode_specified = 1;
        mode_idx++;
    }
    
    /* 检查是否有文件参数 */
    if (mode_idx >= argc) {
        fprintf(stderr, "%s: missing operand after '%s'\n", 
                PROGRAM_NAME, argv[mode_idx-1]);
        usage(1);
    }
    
    /* 处理每个文件 */
    for (int i = mode_idx; i < argc; i++) {
        const char *file = argv[i];
        mode_t final_mode = new_mode;
        
        /* 跳过 -- */
        if (strcmp(file, "--") == 0) continue;
        
        /* 符号模式：需要获取当前权限 */
        if (final_mode == (mode_t)-1) {
            struct stat st;
            if (stat(file, &st) < 0) {
                if (!quiet) {
                    fprintf(stderr, "%s: cannot access '%s': %s\n",
                            PROGRAM_NAME, file, strerror(errno));
                }
                ret = 1;
                continue;
            }
            final_mode = apply_mode(st.st_mode, argv[mode_idx-1]);
            if (final_mode == (mode_t)-1) {
                fprintf(stderr, "%s: invalid mode: '%s'\n", 
                        PROGRAM_NAME, argv[mode_idx-1]);
                return 1;
            }
        }
        
        /* 根目录保护 */
        if (preserve_root && strcmp(file, "/") == 0) {
            fprintf(stderr, "%s: it is dangerous to operate recursively on /\n", 
                    PROGRAM_NAME);
            fprintf(stderr, "%s: use --no-preserve-root to override this failsafe\n",
                    PROGRAM_NAME);
            ret = 1;
            continue;
        }
        
        /* 修改权限 */
        if (recursive) {
            if (recursive_chmod(file, final_mode, verbose, changes, quiet) < 0) {
                if (!quiet) {
                    fprintf(stderr, "%s: changing permissions of '%s': %s\n",
                            PROGRAM_NAME, file, strerror(errno));
                }
                ret = 1;
            }
        } else {
            struct stat old_st;
            int had_stat = (stat(file, &old_st) == 0);
            mode_t old_mode = had_stat ? (old_st.st_mode & MODE_MASK) : 0;
            
            if (chmod(file, final_mode) < 0) {
                if (!quiet) {
                    fprintf(stderr, "%s: changing permissions of '%s': %s\n",
                            PROGRAM_NAME, file, strerror(errno));
                }
                ret = 1;
                continue;
            }
            
            /* 输出信息 */
            if (verbose || (changes && old_mode != final_mode)) {
                printf("mode of '%s' changed from %04o (%s) to %04o (%s)\n",
                       file, old_mode, 
                       had_stat ? "rwx" : "???",
                       final_mode, "rwx");
            }
        }
    }
    
    return ret;
}
