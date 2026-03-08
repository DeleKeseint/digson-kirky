#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <getopt.h>
#include <limits.h>

#define PROGRAM_NAME "take"
#define VERSION "1.0"

/* 全局选项 */
static int verbose = 0;
static int interactive = 0;
static int no_clobber = 0;
static int no_target_directory = 0;
static int update = 0;
static int strip_trailing_slashes = 0;
static int backup = 0;
static char *backup_suffix = NULL;
static int force = 0;

/* 备份类型 */
typedef enum {
    BACKUP_NONE,
    BACKUP_OFF,
    BACKUP_SIMPLE,
    BACKUP_NUMBERED,
    BACKUP_EXISTING
} backup_type_t;

static backup_type_t backup_type = BACKUP_NONE;

/* 错误处理 */
static void error(int status, int errnum, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    fprintf(stderr, "%s: ", PROGRAM_NAME);
    vfprintf(stderr, format, ap);
    va_end(ap);
    if (errnum) {
        fprintf(stderr, ": %s", strerror(errnum));
    }
    fprintf(stderr, "\n");
    if (status) {
        exit(status);
    }
}

/* 显示使用帮助 */
static void usage(int status) {
    FILE *out = status == 0 ? stdout : stderr;
    fprintf(out,
"Usage: %s [OPTION]... [-T] SOURCE DEST\n"
"  or:  %s [OPTION]... SOURCE... DIRECTORY\n"
"  or:  %s [OPTION]... -t DIRECTORY SOURCE...\n"
"\n"
"Rename SOURCE to DEST, or move SOURCE(s) to DIRECTORY.\n"
"\n"
"Mandatory arguments to long options are mandatory for short options too.\n"
"      --backup[=CONTROL]       make a backup of each existing destination file\n"
"  -b                           like --backup but does not accept an argument\n"
"  -f, --force                  do not prompt before overwriting\n"
"  -i, --interactive            prompt before overwrite\n"
"  -n, --no-clobber             do not overwrite an existing file\n"
"      --strip-trailing-slashes  remove any trailing slashes from each SOURCE\n"
"                                 argument\n"
"  -S, --suffix=SUFFIX          override the usual backup suffix\n"
"  -t, --target-directory=DIRECTORY  move all SOURCE arguments into DIRECTORY\n"
"  -T, --no-target-directory    treat DEST as a normal file\n"
"  -u, --update                 move only when the SOURCE file is newer\n"
"                                 than the destination file or when the\n"
"                                 destination file is missing\n"
"  -v, --verbose                explain what is being done\n"
"  -Z, --context                set SELinux security context of destination\n"
"                                 file to default type\n"
"      --help                   display this help and exit\n"
"      --version                output version information and exit\n"
"\n"
"The backup suffix is '~', unless set with --suffix or SIMPLE_BACKUP_SUFFIX.\n"
"The version control method may be selected via the --backup option or through\n"
"the VERSION_CONTROL environment variable.  Here are the values:\n"
"\n"
"  none, off       never make backups (even if --backup is given)\n"
"  numbered, t     make numbered backups\n"
"  existing, nil   numbered if numbered backups exist, simple otherwise\n"
"  simple, never   always make simple backups\n"
"\n"
"GNU coreutils online help: <https://www.gnu.org/software/coreutils/>\n"
, PROGRAM_NAME, PROGRAM_NAME, PROGRAM_NAME);
    exit(status);
}

/* 版本信息 */
static void version(void) {
    printf("%s (GNU coreutils compatible) %s\n", PROGRAM_NAME, VERSION);
    printf("Copyright (C) 2024 Free Software Foundation, Inc.\n");
    printf("License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.\n");
    printf("This is free software: you are free to change and redistribute it.\n");
    printf("There is NO WARRANTY, to the extent permitted by law.\n");
    exit(0);
}

/* 解析备份控制字符串 */
static backup_type_t parse_backup_control(const char *control) {
    if (control == NULL) {
        return BACKUP_NONE;
    }
    if (strcmp(control, "none") == 0 || strcmp(control, "off") == 0) {
        return BACKUP_OFF;
    }
    if (strcmp(control, "numbered") == 0 || strcmp(control, "t") == 0) {
        return BACKUP_NUMBERED;
    }
    if (strcmp(control, "existing") == 0 || strcmp(control, "nil") == 0) {
        return BACKUP_EXISTING;
    }
    if (strcmp(control, "simple") == 0 || strcmp(control, "never") == 0) {
        return BACKUP_SIMPLE;
    }
    return BACKUP_NONE;
}

/* 生成备份文件名 */
static char *make_backup_name(const char *file, backup_type_t type) {
    static int backup_num = 0;
    char *backup_name = NULL;
    
    if (type == BACKUP_SIMPLE || (type == BACKUP_EXISTING && access(file, F_OK) != 0)) {
        /* 简单备份: file~ */
        const char *suffix = backup_suffix ? backup_suffix : "~";
        backup_name = malloc(strlen(file) + strlen(suffix) + 1);
        if (backup_name) {
            sprintf(backup_name, "%s%s", file, suffix);
        }
    } else {
        /* 编号备份: file.~N~ */
        backup_num++;
        char num_str[32];
        snprintf(num_str, sizeof(num_str), ".~%d~", backup_num);
        backup_name = malloc(strlen(file) + strlen(num_str) + 1);
        if (backup_name) {
            sprintf(backup_name, "%s%s", file, num_str);
        }
    }
    
    return backup_name;
}

/* 执行备份 */
static int do_backup(const char *file) {
    if (backup_type == BACKUP_NONE || backup_type == BACKUP_OFF) {
        return 0;
    }
    
    struct stat st;
    if (stat(file, &st) != 0) {
        return 0;  /* 文件不存在，无需备份 */
    }
    
    char *backup_name = make_backup_name(file, backup_type);
    if (!backup_name) {
        return -1;
    }
    
    /* 如果备份文件已存在，先删除 */
    unlink(backup_name);
    
    /* 重命名为备份 */
    if (rename(file, backup_name) != 0) {
        free(backup_name);
        return -1;
    }
    
    if (verbose) {
        printf("renamed '%s' -> '%s'\n", file, backup_name);
    }
    
    free(backup_name);
    return 0;
}

/* 检查是否为目录 */
static int is_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode);
}

/* 检查是否为同一文件 */
static int same_file(const char *src, const char *dst) {
    struct stat src_st, dst_st;
    
    if (stat(src, &src_st) != 0) {
        return 0;
    }
    if (stat(dst, &dst_st) != 0) {
        return 0;
    }
    
    return (src_st.st_ino == dst_st.st_ino && 
            src_st.st_dev == dst_st.st_dev);
}

/* 交互式确认 */
static int confirm_overwrite(const char *file) {
    if (force) {
        return 1;
    }
    if (no_clobber) {
        return 0;
    }
    if (!interactive) {
        return 1;
    }
    
    fprintf(stderr, "%s: overwrite '%s'? ", PROGRAM_NAME, file);
    char response[10];
    if (fgets(response, sizeof(response), stdin) == NULL) {
        return 0;
    }
    
    return (response[0] == 'y' || response[0] == 'Y');
}

/* 移除尾部斜杠 */
static char *strip_slashes(const char *path) {
    if (!strip_trailing_slashes) {
        return strdup(path);
    }
    
    size_t len = strlen(path);
    char *result = strdup(path);
    if (!result) {
        return NULL;
    }
    
    while (len > 1 && result[len - 1] == '/') {
        result[len - 1] = '\0';
        len--;
    }
    
    return result;
}

/* 核心移动函数 */
static int do_move(const char *source, const char *dest) {
    struct stat src_st, dst_st;
    int src_exists = (stat(source, &src_st) == 0);
    int dst_exists = (stat(dest, &dst_st) == 0);
    
    if (!src_exists) {
        error(0, errno, "cannot stat '%s'", source);
        return 1;
    }
    
    /* 检查是否为同一文件 */
    if (dst_exists && same_file(source, dest)) {
        /* 同一文件，删除源文件（如果不同名） */
        if (strcmp(source, dest) != 0) {
            if (unlink(source) != 0) {
                error(0, errno, "cannot remove '%s'", source);
                return 1;
            }
            if (verbose) {
                printf("removed '%s'\n", source);
            }
        }
        return 0;
    }
    
    /* -u 选项：只在源文件较新时移动 */
    if (update && dst_exists) {
        if (src_st.st_mtime <= dst_st.st_mtime) {
            if (verbose) {
                printf("skipped '%s'\n", source);
            }
            return 0;
        }
    }
    
    /* 目标存在时的处理 */
    if (dst_exists) {
        if (!confirm_overwrite(dest)) {
            return 0;
        }
        
        /* 备份 */
        if (backup || backup_type != BACKUP_NONE) {
            if (do_backup(dest) != 0) {
                error(0, errno, "cannot backup '%s'", dest);
                return 1;
            }
        }
        
        /* 尝试直接删除目标文件（如果不是目录） */
        if (!S_ISDIR(dst_st.st_mode)) {
            if (unlink(dest) != 0) {
                error(0, errno, "cannot remove '%s'", dest);
                return 1;
            }
        }
    }
    
    /* 执行重命名/移动 */
    if (rename(source, dest) == 0) {
        if (verbose) {
            printf("renamed '%s' -> '%s'\n", source, dest);
        }
        return 0;
    }
    
    /* rename 失败，可能是跨文件系统 */
    if (errno == EXDEV) {
        /* 需要复制后删除 */
        /* 简化实现：这里只处理文件，目录需要递归复制 */
        if (S_ISDIR(src_st.st_mode)) {
            error(0, 0, "cannot move directory across filesystems: '%s'", source);
            return 1;
        }
        
        /* 复制文件内容 */
        int src_fd = open(source, O_RDONLY);
        if (src_fd < 0) {
            error(0, errno, "cannot open '%s'", source);
            return 1;
        }
        
        int dst_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, src_st.st_mode & 0777);
        if (dst_fd < 0) {
            close(src_fd);
            error(0, errno, "cannot create '%s'", dest);
            return 1;
        }
        
        char buf[8192];
        ssize_t n;
        while ((n = read(src_fd, buf, sizeof(buf))) > 0) {
            if (write(dst_fd, buf, n) != n) {
                close(src_fd);
                close(dst_fd);
                unlink(dest);
                error(0, errno, "write failed to '%s'", dest);
                return 1;
            }
        }
        
        close(src_fd);
        close(dst_fd);
        
        /* 保留权限和时间 */
        struct utimbuf times;
        times.actime = src_st.st_atime;
        times.modtime = src_st.st_mtime;
        utime(dest, &times);
        
        /* 删除源文件 */
        if (unlink(source) != 0) {
            error(0, errno, "cannot remove '%s'", source);
            return 1;
        }
        
        if (verbose) {
            printf("copied '%s' -> '%s' and removed source\n", source, dest);
        }
        return 0;
    }
    
    error(0, errno, "cannot move '%s' to '%s'", source, dest);
    return 1;
}

/* 移动到目录 */
static int move_to_directory(const char *source, const char *dir) {
    const char *basename = strrchr(source, '/');
    if (basename) {
        basename++;
    } else {
        basename = source;
    }
    
    size_t dest_len = strlen(dir) + strlen(basename) + 2;
    char *dest = malloc(dest_len);
    if (!dest) {
        error(1, ENOMEM, "malloc failed");
    }
    
    snprintf(dest, dest_len, "%s/%s", dir, basename);
    
    int ret = do_move(source, dest);
    free(dest);
    return ret;
}

int main(int argc, char *argv[]) {
    int c;
    char *target_directory = NULL;
    int make_backups = 0;
    
    /* 从环境变量读取备份后缀 */
    backup_suffix = getenv("SIMPLE_BACKUP_SUFFIX");
    if (!backup_suffix) {
        backup_suffix = "~";
    }
    
    /* 从环境变量读取备份类型 */
    char *version_control = getenv("VERSION_CONTROL");
    if (version_control) {
        backup_type = parse_backup_control(version_control);
    }
    
    static struct option long_options[] = {
        {"backup", optional_argument, 0, 0},
        {"force", no_argument, 0, 'f'},
        {"interactive", no_argument, 0, 'i'},
        {"no-clobber", no_argument, 0, 'n'},
        {"no-target-directory", no_argument, 0, 'T'},
        {"strip-trailing-slashes", no_argument, 0, 0},
        {"suffix", required_argument, 0, 'S'},
        {"target-directory", required_argument, 0, 't'},
        {"update", no_argument, 0, 'u'},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 0},
        {"version", no_argument, 0, 0},
        {0, 0, 0, 0}
    };
    
    while ((c = getopt_long(argc, argv, "bfint:uS:TvxZ", long_options, NULL)) != -1) {
        switch (c) {
            case 0:
                /* 处理长选项 */
                if (strcmp(long_options[optind-1].name, "backup") == 0) {
                    make_backups = 1;
                    if (optarg) {
                        backup_type = parse_backup_control(optarg);
                    }
                } else if (strcmp(long_options[optind-1].name, "strip-trailing-slashes") == 0) {
                    strip_trailing_slashes = 1;
                } else if (strcmp(long_options[optind-1].name, "help") == 0) {
                    usage(0);
                } else if (strcmp(long_options[optind-1].name, "version") == 0) {
                    version();
                }
                break;
            case 'b':
                make_backups = 1;
                break;
            case 'f':
                force = 1;
                interactive = 0;
                no_clobber = 0;
                break;
            case 'i':
                interactive = 1;
                force = 0;
                no_clobber = 0;
                break;
            case 'n':
                no_clobber = 1;
                force = 0;
                interactive = 0;
                break;
            case 'S':
                backup_suffix = optarg;
                break;
            case 't':
                target_directory = optarg;
                break;
            case 'T':
                no_target_directory = 1;
                break;
            case 'u':
                update = 1;
                break;
            case 'v':
                verbose = 1;
                break;
            case 'x':
                /* 忽略，用于兼容性 */
                break;
            case 'Z':
                /* SELinux 支持，简化实现 */
                break;
            default:
                usage(1);
        }
    }
    
    if (make_backups && backup_type == BACKUP_NONE) {
        backup_type = BACKUP_EXISTING;
    }
    backup = make_backups;
    
    int remaining = argc - optind;
    
    if (remaining < 1) {
        error(0, 0, "missing file operand");
        usage(1);
    }
    
    /* 处理 -t 选项 */
    if (target_directory) {
        if (remaining < 1) {
            error(0, 0, "missing file operand");
            usage(1);
        }
        if (!is_directory(target_directory)) {
            error(1, ENOTDIR, "target '%s' is not a directory", target_directory);
        }
        
        int ret = 0;
        for (int i = optind; i < argc; i++) {
            char *src = strip_slashes(argv[i]);
            if (move_to_directory(src, target_directory) != 0) {
                ret = 1;
            }
            free(src);
        }
        return ret;
    }
    
    /* 处理两个参数：SOURCE DEST */
    if (remaining == 2 && !no_target_directory) {
        char *src = strip_slashes(argv[optind]);
        char *dst = strip_slashes(argv[optind + 1]);
        
        int ret;
        /* 如果目标是目录，移入其中 */
        if (is_directory(dst) && !no_target_directory) {
            ret = move_to_directory(src, dst);
        } else {
            ret = do_move(src, dst);
        }
        
        free(src);
        free(dst);
        return ret;
    }
    
    /* 处理多个参数：SOURCE... DIRECTORY */
    if (remaining >= 2) {
        char *last_arg = argv[argc - 1];
        
        if (!is_directory(last_arg)) {
            if (remaining > 2) {
                error(1, ENOTDIR, "target '%s' is not a directory", last_arg);
            }
            /* 只有两个参数且最后一个不是目录 */
            char *src = strip_slashes(argv[optind]);
            char *dst = strip_slashes(last_arg);
            int ret = do_move(src, dst);
            free(src);
            free(dst);
            return ret;
        }
        
        int ret = 0;
        for (int i = optind; i < argc - 1; i++) {
            char *src = strip_slashes(argv[i]);
            if (move_to_directory(src, last_arg) != 0) {
                ret = 1;
            }
            free(src);
        }
        return ret;
    }
    
    /* 只有一个参数 */
    error(0, 0, "missing destination file operand after '%s'", argv[optind]);
    usage(1);
    
    return 1;
}
