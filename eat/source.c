#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <libgen.h>

#define PROGRAM_NAME "eat"
#define VERSION "1.0"

/* 选项标志 */
static int force = 0;               /* -f, --force */
static int interactive = 0;         /* -i, --interactive */
static int interactive_once = 0;    /* -I, --interactive=once */
static int recursive = 0;           /* -r, -R, --recursive */
static int verbose = 0;             /* -v, --verbose */
static int no_preserve_root = 0;    /* --no-preserve-root */
static int one_file_system = 0;     /* -x, --one-file-system */
static int empty = 0;               /* -d, --dir */
static int is_root = 0;             /* --is-root: 使用 sudo 执行 */

/* 统计信息 */
static int errors = 0;
static int removed_files = 0;
static int removed_dirs = 0;

/* 设备号（用于 -x 选项） */
static dev_t root_dev;

/* 错误处理 */
static void error(int status, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    fprintf(stderr, "%s: ", PROGRAM_NAME);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    
    if (status != 0) {
        exit(status);
    }
    errors++;
}

/* 显示帮助 */
static void usage(int status) {
    FILE *out = status == 0 ? stdout : stderr;
    fprintf(out,
"Usage: %s [OPTION]... [FILE]...\n"
"Remove (unlink) the FILE(s).\n"
"\n"
"  -f, --force           ignore non-existent files and arguments, never prompt\n"
"  -i                    prompt before every removal\n"
"  -I                    prompt once before removing more than three files, or\n"
"                          when removing recursively; less intrusive than -i,\n"
"                          while still giving protection against most mistakes\n"
"      --interactive[=WHEN]  prompt according to WHEN: never, once (-I), or\n"
"                          always (-i); without WHEN, prompt always\n"
"      --one-file-system  when removing a hierarchy recursively, skip any\n"
"                          directory that is on a file system different from\n"
"                          that of the corresponding command line argument\n"
"      --no-preserve-root  do not treat '/' specially\n"
"      --preserve-root[=all]  do not remove '/' (default);\n"
"                              with 'all', reject any command line argument\n"
"                              on a separate device from its parent\n"
"  -r, -R, --recursive   remove directories and their contents recursively\n"
"  -d, --dir             remove empty directories\n"
"  -v, --verbose         explain what is being done\n"
"      --is-root         execute with root privileges (using sudo)\n"
"  -x                    same as --one-file-system\n"
"      --help            display this help and exit\n"
"      --version         output version information and exit\n"
"\n"
"By default, rm does not remove directories.  Use the --recursive (-r or -R)\n"
"option to remove each listed directory, too, along with all of its contents.\n"
"\n"
"To remove a file whose name starts with a '-', for example '-foo',\n"
"use one of these commands:\n"
"  %s -- -foo\n"
"\n"
"  %s ./-foo\n"
"\n"
"Note that if you use rm to remove a file, it might be possible to recover\n"
"some of its contents, given sufficient expertise and/or time.  For greater\n"
"assurance that the contents are truly unrecoverable, consider using shred.\n"
, PROGRAM_NAME, PROGRAM_NAME, PROGRAM_NAME);
    exit(status);
}

/* 版本信息 */
static void version(void) {
    printf("%s (GNU coreutils compatible) %s\n", PROGRAM_NAME, VERSION);
    exit(0);
}

/* 检查是否是 root */
static int check_root(void) {
    return geteuid() == 0;
}

/* 使用 sudo 重新执行 */
static int exec_with_sudo(int argc, char *argv[]) {
    /* 构建新的参数列表，移除 --is-root */
    char **new_argv = malloc((argc + 2) * sizeof(char *));
    if (!new_argv) {
        error(1, "memory allocation failed");
    }
    
    new_argv[0] = "sudo";
    
    int j = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--is-root") == 0) {
            continue;  /* 跳过 --is-root */
        }
        new_argv[j++] = argv[i];
    }
    new_argv[j] = NULL;
    
    execvp("sudo", new_argv);
    error(126, "failed to execute sudo: %s", strerror(errno));
    free(new_argv);
    return 126;
}

/* 确认删除 */
static int confirm(const char *path, const char *type) {
    if (force) {
        return 1;
    }
    
    if (!interactive && !interactive_once) {
        return 1;
    }
    
    if (interactive_once) {
        /* -I 模式：只在特定情况下提示 */
        static int prompted = 0;
        if (prompted) {
            return 1;
        }
        /* 递归或超过3个文件时提示 */
        prompted = 1;
    }
    
    fprintf(stderr, "%s: remove %s '%s'? ", PROGRAM_NAME, type, path);
    
    char response[10];
    if (fgets(response, sizeof(response), stdin) == NULL) {
        return 0;
    }
    
    return (response[0] == 'y' || response[0] == 'Y');
}

/* 检查并处理根目录保护 */
static int check_preserve_root(const char *path) {
    if (no_preserve_root) {
        return 0;  /* 允许删除 */
    }
    
    /* 检查是否是根目录 */
    if (strcmp(path, "/") == 0) {
        error(0, "it is dangerous to operate recursively on '/'");
        error(0, "use --no-preserve-root to override this failsafe");
        return 1;
    }
    
    /* 检查是否是挂载点 */
    struct stat st;
    if (stat(path, &st) == 0) {
        struct stat parent_st;
        char *parent = strdup(path);
        char *dir = dirname(parent);
        
        if (stat(dir, &parent_st) == 0) {
            if (st.st_dev != parent_st.st_dev) {
                /* 不同设备，是挂载点 */
                if (one_file_system) {
                    if (verbose) {
                        printf("skipping '%s', different file system\n", path);
                    }
                    free(parent);
                    return 2;  /* 跳过 */
                }
            }
        }
        free(parent);
    }
    
    return 0;
}

/* 删除空目录 */
static int remove_empty_dir(const char *path) {
    int ret = rmdir(path);
    if (ret == 0) {
        if (verbose) {
            printf("removed directory '%s'\n", path);
        }
        removed_dirs++;
    }
    return ret;
}

/* 删除文件 */
static int remove_file(const char *path, const struct stat *st) {
    int ret;
    
    /* 检查设备（-x 选项） */
    if (one_file_system && st->st_dev != root_dev) {
        if (verbose) {
            printf("skipping '%s', different file system\n", path);
        }
        return 0;
    }
    
    /* 确认删除 */
    if (!confirm(path, st->st_mode & S_IFDIR ? "directory" : "regular file")) {
        return 0;
    }
    
    /* 如果是目录且非递归模式，报错 */
    if (S_ISDIR(st->st_mode) && !recursive && !empty) {
        error(0, "cannot remove '%s': Is a directory", path);
        return 1;
    }
    
    /* 执行删除 */
    ret = unlink(path);
    if (ret != 0 && S_ISDIR(st->st_mode)) {
        /* 尝试 rmdir */
        ret = rmdir(path);
    }
    
    if (ret != 0) {
        if (errno == EISDIR) {
            error(0, "cannot remove '%s': Is a directory", path);
        } else if (errno == ENOTEMPTY) {
            error(0, "cannot remove '%s': Directory not empty", path);
        } else if (!force || errno != ENOENT) {
            error(0, "cannot remove '%s': %s", path, strerror(errno));
        }
        return 1;
    }
    
    if (verbose) {
        printf("removed '%s'\n", path);
    }
    removed_files++;
    
    return 0;
}

/* 递归删除目录 */
static int remove_recursive(const char *path) {
    DIR *dir;
    struct dirent *entry;
    int ret = 0;
    
    /* 检查根目录保护 */
    int preserve_check = check_preserve_root(path);
    if (preserve_check == 1) {
        return 1;  /* 错误 */
    }
    if (preserve_check == 2) {
        return 0;  /* 跳过 */
    }
    
    dir = opendir(path);
    if (!dir) {
        if (!force || errno != ENOENT) {
            error(0, "cannot open directory '%s': %s", path, strerror(errno));
        }
        return 1;
    }
    
    /* 读取所有条目 */
    while ((entry = readdir(dir)) != NULL) {
        /* 跳过 . 和 .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        /* 构建完整路径 */
        size_t len = strlen(path) + strlen(entry->d_name) + 2;
        char *full_path = malloc(len);
        if (!full_path) {
            error(0, "memory allocation failed");
            ret = 1;
            continue;
        }
        
        if (path[strlen(path) - 1] == '/') {
            snprintf(full_path, len, "%s%s", path, entry->d_name);
        } else {
            snprintf(full_path, len, "%s/%s", path, entry->d_name);
        }
        
        /* 获取文件信息 */
        struct stat st;
        if (lstat(full_path, &st) != 0) {
            if (!force || errno != ENOENT) {
                error(0, "cannot stat '%s': %s", full_path, strerror(errno));
                ret = 1;
            }
            free(full_path);
            continue;
        }
        
        /* 递归处理目录 */
        if (S_ISDIR(st.st_mode)) {
            if (remove_recursive(full_path) != 0) {
                ret = 1;
            }
            /* 删除空目录 */
            if (rmdir(full_path) != 0) {
                if (!force || errno != ENOENT) {
                    error(0, "cannot remove '%s': %s", full_path, strerror(errno));
                    ret = 1;
                }
            } else {
                if (verbose) {
                    printf("removed directory '%s'\n", full_path);
                }
                removed_dirs++;
            }
        } else {
            /* 删除文件 */
            if (remove_file(full_path, &st) != 0) {
                ret = 1;
            }
        }
        
        free(full_path);
    }
    
    closedir(dir);
    
    /* 删除顶层目录 */
    if (ret == 0) {
        if (rmdir(path) != 0) {
            if (!force || errno != ENOENT) {
                error(0, "cannot remove '%s': %s", path, strerror(errno));
                ret = 1;
            }
        } else {
            if (verbose) {
                printf("removed directory '%s'\n", path);
            }
            removed_dirs++;
        }
    }
    
    return ret;
}

/* 处理单个文件/目录 */
static int process_path(const char *path) {
    struct stat st;
    
    /* 获取文件信息 */
    if (lstat(path, &st) != 0) {
        if (force && errno == ENOENT) {
            return 0;
        }
        error(0, "cannot remove '%s': %s", path, strerror(errno));
        return 1;
    }
    
    /* 检查根目录保护 */
    int preserve_check = check_preserve_root(path);
    if (preserve_check == 1) {
        return 1;
    }
    if (preserve_check == 2) {
        return 0;
    }
    
    /* 记录根设备（用于 -x 选项） */
    if (one_file_system && root_dev == 0) {
        root_dev = st.st_dev;
    }
    
    /* 处理目录 */
    if (S_ISDIR(st.st_mode)) {
        if (recursive) {
            /* 确认递归删除 */
            if (interactive_once && !confirm(path, "directory")) {
                return 0;
            }
            return remove_recursive(path);
        } else if (empty) {
            /* 只删除空目录 */
            if (!confirm(path, "directory")) {
                return 0;
            }
            if (rmdir(path) != 0) {
                error(0, "failed to remove '%s': %s", path, strerror(errno));
                return 1;
            }
            if (verbose) {
                printf("removed directory '%s'\n", path);
            }
            removed_dirs++;
            return 0;
        } else {
            error(0, "cannot remove '%s': Is a directory", path);
            return 1;
        }
    }
    
    /* 处理普通文件、符号链接等 */
    return remove_file(path, &st);
}

int main(int argc, char *argv[]) {
    int opt;
    int opt_index = 0;
    
    static struct option long_options[] = {
        {"force", no_argument, 0, 'f'},
        {"interactive", optional_argument, 0, 0},
        {"interactive=once", no_argument, 0, 'I'},
        {"one-file-system", no_argument, 0, 'x'},
        {"no-preserve-root", no_argument, 0, 0},
        {"preserve-root", optional_argument, 0, 0},
        {"recursive", no_argument, 0, 'r'},
        {"dir", no_argument, 0, 'd'},
        {"verbose", no_argument, 0, 'v'},
        {"is-root", no_argument, 0, 0},
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'V'},
        {0, 0, 0, 0}
    };
    
    /* 解析选项 */
    while ((opt = getopt_long(argc, argv, "dfiIrvxR", long_options, &opt_index)) != -1) {
        switch (opt) {
            case 'd':
                empty = 1;
                break;
            case 'f':
                force = 1;
                interactive = 0;
                interactive_once = 0;
                break;
            case 'i':
                interactive = 1;
                interactive_once = 0;
                break;
            case 'I':
                interactive_once = 1;
                interactive = 0;
                break;
            case 'r':
            case 'R':
                recursive = 1;
                break;
            case 'v':
                verbose = 1;
                break;
            case 'x':
                one_file_system = 1;
                break;
            case 'h':
                usage(0);
                break;
            case 'V':
                version();
                break;
            case 0:
                /* 处理长选项 */
                if (strcmp(long_options[opt_index].name, "interactive") == 0) {
                    if (optarg == NULL || strcmp(optarg, "always") == 0) {
                        interactive = 1;
                    } else if (strcmp(optarg, "once") == 0) {
                        interactive_once = 1;
                        interactive = 0;
                    } else if (strcmp(optarg, "never") == 0) {
                        interactive = 0;
                        interactive_once = 0;
                    }
                } else if (strcmp(long_options[opt_index].name, "no-preserve-root") == 0) {
                    no_preserve_root = 1;
                } else if (strcmp(long_options[opt_index].name, "preserve-root") == 0) {
                    no_preserve_root = 0;
                } else if (strcmp(long_options[opt_index].name, "is-root") == 0) {
                    is_root = 1;
                }
                break;
            default:
                usage(1);
        }
    }
    
    /* 检查 --is-root */
    if (is_root && !check_root()) {
        return exec_with_sudo(argc, argv);
    }
    
    /* 检查是否有文件参数 */
    if (optind >= argc) {
        if (force) {
            return 0;
        }
        error(0, "missing operand");
        usage(1);
    }
    
    /* 处理每个文件 */
    int ret = 0;
    int file_count = argc - optind;
    
    /* -I 选项：多个文件时提示一次 */
    if (interactive_once && file_count > 3) {
        fprintf(stderr, "%s: remove %d arguments? ", PROGRAM_NAME, file_count);
        char response[10];
        if (fgets(response, sizeof(response), stdin) == NULL ||
            (response[0] != 'y' && response[0] != 'Y')) {
            return 0;
        }
    }
    
    for (int i = optind; i < argc; i++) {
        if (process_path(argv[i]) != 0) {
            ret = 1;
        }
    }
    
    return ret;
}
