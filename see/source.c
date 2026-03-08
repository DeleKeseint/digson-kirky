#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <locale.h>
#include <langinfo.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#include <limits.h>

#define PROGRAM_NAME "see"
#define VERSION "1.0"

/* 选项标志 */
static int all_files = 0;           /* -a: 显示隐藏文件 */
static int almost_all = 0;          /* -A: 显示隐藏文件但不显示 . 和 .. */
static int long_format = 0;         /* -l: 长格式 */
static int one_per_line = 0;        /* -1: 每行一个 */
static int recursive = 0;           /* -R: 递归 */
static int sort_reverse = 0;        /* -r: 反向排序 */
static int sort_time = 0;           /* -t: 按时间排序 */
static int sort_size = 0;           /* -S: 按大小排序 */
static int no_sort = 0;             /* -f: 不排序 */
static int human_readable = 0;      /* -h: 人类可读大小 */
static int inode = 0;               /* -i: 显示 inode */
static int directory_only = 0;      /* -d: 目录本身 */
static int color = 0;               /* --color: 颜色输出 */
static int numeric_ids = 0;         /* -n: 数字 UID/GID */
static int no_owner = 0;            /* -g: 不显示所有者 */
static int no_group = 0;            /* -G: 不显示组 */
static int size_blocks = 0;         /* -s: 显示块数 */
static int dereference = 0;         /* -L: 跟随符号链接 */
static int classify = 0;            /* -F: 添加类型指示符 */
static int append_slash = 0;        /* -p: 目录加斜杠 */

/* 文件信息结构 */
typedef struct file_info {
    char *name;
    char *path;
    struct stat statbuf;
    struct file_info *next;
} file_info_t;

/* 颜色定义 */
#define COLOR_RESET   "\033[0m"
#define COLOR_DIR     "\033[1;34m"   /* 蓝色 - 目录 */
#define COLOR_LINK    "\033[1;36m"   /* 青色 - 链接 */
#define COLOR_EXEC    "\033[1;32m"   /* 绿色 - 可执行 */
#define COLOR_ARCHIVE "\033[1;31m"   /* 红色 - 压缩包 */
#define COLOR_IMAGE   "\033[1;35m"   /* 紫色 - 图片 */

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
static void usage(int status) {
    FILE *out = status == 0 ? stdout : stderr;
    fprintf(out,
"Usage: %s [OPTION]... [FILE]...\n"
"List information about the FILEs (the current directory by default).\n"
"Sort entries alphabetically if none of -cftuvSUX nor --sort is specified.\n"
"\n"
"Mandatory arguments to long options are mandatory for short options too.\n"
"  -a, --all                  do not ignore entries starting with .\n"
"  -A, --almost-all           do not list implied . and ..\n"
"      --author               with -l, print the author of each file\n"
"  -b, --escape               print C-style escapes for nongraphic characters\n"
"      --block-size=SIZE      with -l, scale sizes by SIZE when printing them;\n"
"                               e.g., '--block-size=M'; see SIZE format below\n"
"  -B, --ignore-backups       do not list implied entries ending with ~\n"
"  -c                         with -lt: sort by, and show, ctime (time of last\n"
"                               modification of file status information);\n"
"                               with -l: show ctime and sort by name;\n"
"                               otherwise: sort by ctime, newest first\n"
"  -C                         list entries by columns\n"
"      --color[=WHEN]         colorize the output; WHEN can be 'always' (default\n"
"                               if omitted), 'auto', or 'never'; more info below\n"
"  -d, --directory            list directories themselves, not their contents\n"
"  -D, --dired                generate output designed for Emacs' dired mode\n"
"  -f                         do not sort, enable -aU, disable -ls --color\n"
"  -F, --classify             append indicator (one of */=>@|) to entries\n"
"      --file-type            likewise, except do not append '*'\n"
"      --format=WORD          across -x, commas -m, horizontal -x, long -l,\n"
"                               single-column -1, verbose -l, vertical -C\n"
"      --full-time            like -l --time-style=full-iso\n"
"  -g                         like -l, but do not list owner\n"
"      --group-directories-first\n"
"                             group directories before files;\n"
"                               can be augmented with a --sort option, but any\n"
"                               use of --sort=none (-U) disables grouping\n"
"  -G, --no-group             in a long listing, don't print group names\n"
"  -h, --human-readable       with -l and -s, print sizes like 1K 234M 2G etc.\n"
"      --si                   likewise, but use powers of 1000 not 1024\n"
"  -H, --dereference-command-line\n"
"                             follow symbolic links listed on the command line\n"
"      --dereference-command-line-symlink-to-dir\n"
"                             follow each command line symbolic link\n"
"                               that points to a directory\n"
"      --hide=PATTERN         do not list implied entries matching shell PATTERN\n"
"                               (overridden by -a or -A)\n"
"      --hyperlink[=WHEN]     hyperlink file names; WHEN can be 'always'\n"
"                               (default if omitted), 'auto', or 'never'\n"
"      --indicator-style=WORD  append indicator with style WORD to entry names:\n"
"                               none (default), slash (-p),\n"
"                               file-type (--file-type), classify (-F)\n"
"  -i, --inode                print the index number of each file\n"
"  -I, --ignore=PATTERN       do not list implied entries matching shell PATTERN\n"
"  -k, --kibibytes            default to 1024-byte blocks for disk usage;\n"
"                               used only with -s and per directory totals\n"
"  -l                         use a long listing format\n"
"  -L, --dereference          when showing file information for a symbolic\n"
"                               link, show information for the file the link\n"
"                               references rather than for the link itself\n"
"  -m                         fill width with a comma separated list of entries\n"
"  -n, --numeric-uid-gid      like -l, but list numeric user and group IDs\n"
"  -N, --literal              print entry names without quoting\n"
"  -o                         like -l, but do not list group information\n"
"  -p, --indicator-style=slash\n"
"                             append / indicator to directories\n"
"  -q, --hide-control-chars   print ? instead of nongraphic characters\n"
"      --show-control-chars   show nongraphic characters as-is (the default,\n"
"                               unless program is 'ls' and output is a terminal)\n"
"  -Q, --quote-name           enclose entry names in double quotes\n"
"      --quoting-style=WORD   use quoting style WORD for entry names:\n"
"                               literal, locale, shell, shell-always,\n"
"                               shell-escape, shell-escape-always, c, escape\n"
"                               (overrides QUOTING_STYLE environment variable)\n"
"  -r, --reverse              reverse order while sorting\n"
"  -R, --recursive            list subdirectories recursively\n"
"  -s, --size                 print the allocated size of each file, in blocks\n"
"  -S                         sort by file size, largest first\n"
"      --sort=WORD            sort by WORD instead of name: none (-U), size (-S),\n"
"                               time (-t), version (-v), extension (-X)\n"
"      --time=WORD            change the default of using modification times;\n"
"                               access time (-u): atime, access, use;\n"
"                               change time (-c): ctime, status;\n"
"                               birth time: birth, creation;\n"
"                               with -l, WORD determines which time to show;\n"
"                               with --sort=time, sort by WORD (newest first)\n"
"      --time-style=TIME_STYLE  time/date format with -l; see TIME_STYLE below\n"
"  -t                         sort by time, newest first; see --time\n"
"      --tabsize=COLS         assume tab stops at each COLS instead of 8\n"
"  -T, --tabsize=COLS         assume tab stops at each COLS instead of 8\n"
"  -u                         with -lt: sort by, and show, access time;\n"
"                               with -l: show access time and sort by name;\n"
"                               otherwise: sort by access time, newest first\n"
"  -U                         do not sort; list entries in directory order\n"
"  -v                         natural sort of (version) numbers within text\n"
"  -w, --width=COLS           set output width to COLS.  0 means no limit\n"
"  -x                         list entries by lines instead of by columns\n"
"  -X                         sort alphabetically by entry extension\n"
"  -Z, --context              print any security context of each file\n"
"      --zero                 end each output line with NUL, not newline\n"
"  -1                         list one file per line\n"
"      --help     display this help and exit\n"
"      --version  output version information and exit\n"
"\n"
"The SIZE argument is an integer and optional unit (example: 10K is 10*1024).\n"
"Units are K,M,G,T,P,E,Z,Y (powers of 1024) or KB,MB,... (powers of 1000).\n"
"Binary prefixes can be used, too: KiB=K, MiB=M, and so on.\n"
"\n"
"The TIME_STYLE argument can be full-iso, long-iso, iso, locale, or +FORMAT.\n"
"FORMAT is interpreted like in date(1).  If FORMAT is FORMAT1<newline>FORMAT2,\n"
"then FORMAT1 applies to non-recent files and FORMAT2 to recent files.\n"
"TIME_STYLE prefixed with 'posix-' takes effect only outside the POSIX locale.\n"
"Also the TIME_STYLE environment variable sets the default style to use.\n"
"\n"
"Using color to distinguish file types is disabled both by default and\n"
"with --color=never.  With --color=auto, %s emits color codes only when\n"
"standard output is connected to a terminal.  The LS_COLORS environment\n"
"variable can change the settings.  Use the dircolors command to set it.\n"
"\n"
"Exit status:\n"
" 0  if OK,\n"
" 1  if minor problems (e.g., cannot access subdirectory),\n"
" 2  if serious trouble (e.g., cannot access command-line argument).\n"
, PROGRAM_NAME, PROGRAM_NAME);
    exit(status);
}

/* 版本信息 */
static void version(void) {
    printf("%s (GNU coreutils compatible) %s\n", PROGRAM_NAME, VERSION);
    exit(0);
}

/* 获取文件类型字符 */
static char get_file_type(mode_t mode) {
    if (S_ISREG(mode)) return '-';
    if (S_ISDIR(mode)) return 'd';
    if (S_ISLNK(mode)) return 'l';
    if (S_ISCHR(mode)) return 'c';
    if (S_ISBLK(mode)) return 'b';
    if (S_ISFIFO(mode)) return 'p';
    if (S_ISSOCK(mode)) return 's';
    return '?';
}

/* 获取权限字符串 */
static void get_permissions(mode_t mode, char *perms) {
    perms[0] = get_file_type(mode);
    perms[1] = (mode & S_IRUSR) ? 'r' : '-';
    perms[2] = (mode & S_IWUSR) ? 'w' : '-';
    perms[3] = (mode & S_IXUSR) ? ((mode & S_ISUID) ? 's' : 'x') : ((mode & S_ISUID) ? 'S' : '-');
    perms[4] = (mode & S_IRGRP) ? 'r' : '-';
    perms[5] = (mode & S_IWGRP) ? 'w' : '-';
    perms[6] = (mode & S_IXGRP) ? ((mode & S_ISGID) ? 's' : 'x') : ((mode & S_ISGID) ? 'S' : '-');
    perms[7] = (mode & S_IROTH) ? 'r' : '-';
    perms[8] = (mode & S_IWOTH) ? 'w' : '-';
    perms[9] = (mode & S_IXOTH) ? ((mode & S_ISVTX) ? 't' : 'x') : ((mode & S_ISVTX) ? 'T' : '-');
    perms[10] = '\0';
}

/* 人类可读大小 */
static void format_size(off_t size, char *buf, size_t len) {
    if (!human_readable) {
        snprintf(buf, len, "%ld", (long)size);
        return;
    }
    
    const char *units[] = {"B", "K", "M", "G", "T", "P", "E"};
    int i = 0;
    double dsize = size;
    
    while (dsize >= 1024 && i < 6) {
        dsize /= 1024;
        i++;
    }
    
    if (i == 0) {
        snprintf(buf, len, "%ldB", (long)size);
    } else {
        snprintf(buf, len, "%.1f%s", dsize, units[i]);
    }
}

/* 获取颜色 */
static const char* get_color(mode_t mode) {
    if (!color) return "";
    
    if (S_ISDIR(mode)) return COLOR_DIR;
    if (S_ISLNK(mode)) return COLOR_LINK;
    if (S_ISREG(mode) && (mode & (S_IXUSR | S_IXGRP | S_IXOTH))) return COLOR_EXEC;
    
    return "";
}

/* 获取分类指示符 */
static char get_classify_char(mode_t mode) {
    if (!classify && !append_slash) return '\0';
    
    if (S_ISDIR(mode)) return '/';
    if (classify) {
        if (S_ISLNK(mode)) return '@';
        if (S_ISFIFO(mode)) return '|';
        if (S_ISSOCK(mode)) return '=';
        if (S_ISREG(mode) && (mode & (S_IXUSR | S_IXGRP | S_IXOTH))) return '*';
    }
    return '\0';
}

/* 打印文件名（带颜色） */
static void print_name(const char *name, mode_t mode) {
    const char *col = get_color(mode);
    char classify_char = get_classify_char(mode);
    
    if (color && *col) printf("%s", col);
    printf("%s", name);
    if (classify_char) putchar(classify_char);
    if (color && *col) printf("%s", COLOR_RESET);
}

/* 长格式输出 */
static void print_long_format(file_info_t *file) {
    char perms[11];
    char size_buf[32];
    char time_buf[64];
    struct passwd *pw;
    struct group *gr;
    
    get_permissions(file->statbuf.st_mode, perms);
    
    /* inode */
    if (inode) {
        printf("%8lu ", (unsigned long)file->statbuf.st_ino);
    }
    
    /* blocks */
    if (size_blocks) {
        printf("%4lu ", (unsigned long)(file->statbuf.st_blocks / 2));
    }
    
    /* permissions */
    printf("%s ", perms);
    
    /* links */
    printf("%2lu ", (unsigned long)file->statbuf.st_nlink);
    
    /* owner */
    if (!no_owner) {
        if (numeric_ids || (pw = getpwuid(file->statbuf.st_uid)) == NULL) {
            printf("%-8d ", file->statbuf.st_uid);
        } else {
            printf("%-8s ", pw->pw_name);
        }
    }
    
    /* group */
    if (!no_group) {
        if (numeric_ids || (gr = getgrgid(file->statbuf.st_gid)) == NULL) {
            printf("%-8d ", file->statbuf.st_gid);
        } else {
            printf("%-8s ", gr->gr_name);
        }
    }
    
    /* size */
    format_size(file->statbuf.st_size, size_buf, sizeof(size_buf));
    printf("%8s ", size_buf);
    
    /* time */
    struct tm *tm = localtime(&file->statbuf.st_mtime);
    strftime(time_buf, sizeof(time_buf), "%b %e %H:%M", tm);
    printf("%s ", time_buf);
    
    /* name */
    print_name(file->name, file->statbuf.st_mode);
    
    /* symlink target */
    if (S_ISLNK(file->statbuf.st_mode)) {
        char link_target[PATH_MAX];
        ssize_t len = readlink(file->path, link_target, sizeof(link_target) - 1);
        if (len != -1) {
            link_target[len] = '\0';
            printf(" -> %s", link_target);
        }
    }
    
    printf("\n");
}

/* 比较函数 */
static int compare_files(const void *a, const void *b) {
    file_info_t *fa = *(file_info_t **)a;
    file_info_t *fb = *(file_info_t **)b;
    int result = 0;
    
    if (no_sort) {
        return 0;
    }
    
    if (sort_time) {
        result = fb->statbuf.st_mtime - fa->statbuf.st_mtime;
    } else if (sort_size) {
        result = (fb->statbuf.st_size > fa->statbuf.st_size) ? 1 : 
                 (fb->statbuf.st_size < fa->statbuf.st_size) ? -1 : 0;
    } else {
        result = strcasecmp(fa->name, fb->name);
    }
    
    return sort_reverse ? -result : result;
}

/* 列出目录内容 */
static int list_directory(const char *dir_path) {
    DIR *dir;
    struct dirent *entry;
    file_info_t *files = NULL;
    file_info_t **file_array = NULL;
    int count = 0;
    int capacity = 100;
    
    if (directory_only) {
        /* 只显示目录本身 */
        struct stat st;
        if (stat(dir_path, &st) == 0) {
            if (long_format) {
                file_info_t fi;
                fi.name = (char*)dir_path;
                fi.path = (char*)dir_path;
                fi.statbuf = st;
                print_long_format(&fi);
            } else {
                print_name(dir_path, st.st_mode);
                printf("\n");
            }
        }
        return 0;
    }
    
    dir = opendir(dir_path);
    if (!dir) {
        error("cannot access '%s': %s", dir_path, strerror(errno));
        return 1;
    }
    
    files = malloc(capacity * sizeof(file_info_t));
    if (!files) {
        closedir(dir);
        return 1;
    }
    
    while ((entry = readdir(dir)) != NULL) {
        /* 隐藏文件处理 */
        if (entry->d_name[0] == '.') {
            if (!all_files && !almost_all) continue;
            if (almost_all && (strcmp(entry->d_name, ".") == 0 || 
                               strcmp(entry->d_name, "..") == 0)) continue;
        }
        
        if (count >= capacity) {
            capacity *= 2;
            files = realloc(files, capacity * sizeof(file_info_t));
            if (!files) {
                closedir(dir);
                return 1;
            }
        }
        
        files[count].name = strdup(entry->d_name);
        
        size_t path_len = strlen(dir_path) + strlen(entry->d_name) + 2;
        files[count].path = malloc(path_len);
        snprintf(files[count].path, path_len, "%s/%s", dir_path, entry->d_name);
        
        /* 获取文件状态 */
        if (dereference) {
            stat(files[count].path, &files[count].statbuf);
        } else {
            lstat(files[count].path, &files[count].statbuf);
        }
        
        count++;
    }
    
    closedir(dir);
    
    /* 排序 */
    if (!no_sort && count > 0) {
        file_array = malloc(count * sizeof(file_info_t *));
        for (int i = 0; i < count; i++) {
            file_array[i] = &files[i];
        }
        qsort(file_array, count, sizeof(file_info_t *), compare_files);
    } else {
        file_array = malloc(count * sizeof(file_info_t *));
        for (int i = 0; i < count; i++) {
            file_array[i] = &files[i];
        }
    }
    
    /* 输出 */
    if (long_format) {
        for (int i = 0; i < count; i++) {
            print_long_format(file_array[i]);
        }
    } else if (one_per_line) {
        for (int i = 0; i < count; i++) {
            print_name(file_array[i]->name, file_array[i]->statbuf.st_mode);
            printf("\n");
        }
    } else {
        /* 多列输出（简化版） */
        for (int i = 0; i < count; i++) {
            print_name(file_array[i]->name, file_array[i]->statbuf.st_mode);
            if (i < count - 1) {
                printf("  ");
            }
        }
        if (count > 0) printf("\n");
    }
    
    /* 递归处理 */
    if (recursive) {
        for (int i = 0; i < count; i++) {
            if (S_ISDIR(file_array[i]->statbuf.st_mode) &&
                strcmp(file_array[i]->name, ".") != 0 &&
                strcmp(file_array[i]->name, "..") != 0) {
                printf("\n%s:\n", file_array[i]->path);
                list_directory(file_array[i]->path);
            }
        }
    }
    
    /* 清理 */
    for (int i = 0; i < count; i++) {
        free(files[i].name);
        free(files[i].path);
    }
    free(files);
    free(file_array);
    
    return 0;
}

/* 列出单个文件 */
static int list_file(const char *path, const char *name) {
    struct stat st;
    
    if (dereference ? stat(path, &st) : lstat(path, &st) != 0) {
        error("cannot access '%s': %s", path, strerror(errno));
        return 1;
    }
    
    if (long_format) {
        file_info_t fi;
        fi.name = (char*)name;
        fi.path = (char*)path;
        fi.statbuf = st;
        print_long_format(&fi);
    } else {
        print_name(name, st.st_mode);
        printf("\n");
    }
    
    /* 如果是目录且递归，继续处理 */
    if (recursive && S_ISDIR(st.st_mode)) {
        printf("\n%s:\n", path);
        list_directory(path);
    }
    
    return 0;
}

int main(int argc, char *argv[]) {
    int c;
    int optind = 1;
    int files_start = -1;
    
    /* 检查是否为终端（自动颜色） */
    color = isatty(STDOUT_FILENO) ? 1 : 0;
    
    /* 解析选项 */
    while (optind < argc && argv[optind][0] == '-' && argv[optind][1] != '\0') {
        /* 长选项 */
        if (strcmp(argv[optind], "--help") == 0) {
            usage(0);
        }
        if (strcmp(argv[optind], "--version") == 0) {
            version();
        }
        if (strcmp(argv[optind], "--color") == 0) {
            color = 1;
            optind++;
            continue;
        }
        if (strncmp(argv[optind], "--color=", 8) == 0) {
            if (strcmp(argv[optind] + 8, "never") == 0) color = 0;
            else if (strcmp(argv[optind] + 8, "always") == 0) color = 1;
            else if (strcmp(argv[optind] + 8, "auto") == 0) color = isatty(STDOUT_FILENO);
            optind++;
            continue;
        }
        
        /* 短选项 */
        char *p = argv[optind] + 1;
        while (*p) {
            switch (*p) {
                case 'a': all_files = 1; p++; break;
                case 'A': almost_all = 1; p++; break;
                case 'l': long_format = 1; p++; break;
                case '1': one_per_line = 1; p++; break;
                case 'R': recursive = 1; p++; break;
                case 'r': sort_reverse = 1; p++; break;
                case 't': sort_time = 1; p++; break;
                case 'S': sort_size = 1; p++; break;
                case 'f': no_sort = 1; all_files = 1; p++; break;
                case 'h': human_readable = 1; p++; break;
                case 'i': inode = 1; p++; break;
                case 'd': directory_only = 1; p++; break;
                case 'n': long_format = 1; numeric_ids = 1; p++; break;
                case 'g': long_format = 1; no_owner = 1; p++; break;
                case 'G': no_group = 1; p++; break;
                case 's': size_blocks = 1; p++; break;
                case 'L': dereference = 1; p++; break;
                case 'F': classify = 1; p++; break;
                case 'p': append_slash = 1; p++; break;
                case 'C': one_per_line = 0; p++; break;
                case 'x': p++; break; /* 忽略，简化实现 */
                case 'm': one_per_line = 1; p++; break; /* 简化实现 */
                case 'q': p++; break; /* 忽略 */
                case 'Q': p++; break; /* 忽略 */
                case 'U': no_sort = 1; p++; break;
                case 'v': p++; break; /* 忽略，简化 */
                case 'X': p++; break; /* 忽略，简化 */
                case 'Z': p++; break; /* 忽略 SELinux */
                case '-': 
                    /* -- 结束选项 */
                    optind++;
                    files_start = optind;
                    goto end_parse;
                default:
                    error("invalid option -- '%c'", *p);
                    usage(1);
            }
        }
        optind++;
    }
    
end_parse:
    
    if (files_start < 0) {
        files_start = optind;
    }
    
    int remaining = argc - files_start;
    int ret = 0;
    
    /* 没有参数，列出当前目录 */
    if (remaining == 0) {
        return list_directory(".");
    }
    
    /* 处理多个参数 */
    if (remaining == 1) {
        const char *path = argv[files_start];
        struct stat st;
        
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode) && !directory_only) {
            return list_directory(path);
        } else {
            return list_file(path, path);
        }
    }
    
    /* 多个文件/目录 */
    for (int i = files_start; i < argc; i++) {
        const char *path = argv[i];
        struct stat st;
        
        if (i > files_start) printf("\n");
        
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            printf("%s:\n", path);
            if (list_directory(path) != 0) ret = 1;
        } else {
            if (list_file(path, path) != 0) ret = 1;
        }
    }
    
    return ret;
}
