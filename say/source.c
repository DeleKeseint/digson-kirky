#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#define PROGRAM_NAME "say"

/* 选项标志 */
static bool do_escape = false;      /* -e 解释转义序列 */
static bool no_newline = false;     /* -n 不输出换行 */
static bool no_escape = false;      /* -E 禁用解释（默认） */

/* 转义序列处理 */
static int process_escape(const char *str, char *out) {
    int i = 0, j = 0;
    while (str[i]) {
        if (str[i] == '\\' && str[i+1]) {
            i++;
            switch (str[i]) {
                case 'a': out[j++] = '\a'; i++; break;  /* 响铃 */
                case 'b': out[j++] = '\b'; i++; break;  /* 退格 */
                case 'c': return -1;                     /* 停止输出 */
                case 'e': out[j++] = '\e'; i++; break;  /* ESC */
                case 'f': out[j++] = '\f'; i++; break;  /* 换页 */
                case 'n': out[j++] = '\n'; i++; break;  /* 换行 */
                case 'r': out[j++] = '\r'; i++; break;  /* 回车 */
                case 't': out[j++] = '\t'; i++; break;  /* 制表符 */
                case 'v': out[j++] = '\v'; i++; break;  /* 垂直制表符 */
                case '\\': out[j++] = '\\'; i++; break; /* 反斜杠 */
                case '0': case '1': case '2': case '3': /* 八进制 */
                case '4': case '5': case '6': case '7': {
                    int val = 0;
                    int count = 0;
                    while (str[i] >= '0' && str[i] <= '7' && count < 3) {
                        val = val * 8 + (str[i] - '0');
                        i++;
                        count++;
                    }
                    out[j++] = (char)val;
                    break;
                }
                case 'x': {  /* 十六进制 */
                    i++;
                    int val = 0;
                    int count = 0;
                    while (count < 2) {
                        if (str[i] >= '0' && str[i] <= '9') {
                            val = val * 16 + (str[i] - '0');
                        } else if (str[i] >= 'a' && str[i] <= 'f') {
                            val = val * 16 + (str[i] - 'a' + 10);
                        } else if (str[i] >= 'A' && str[i] <= 'F') {
                            val = val * 16 + (str[i] - 'A' + 10);
                        } else {
                            break;
                        }
                        i++;
                        count++;
                    }
                    out[j++] = (char)val;
                    break;
                }
                default:
                    out[j++] = '\\';
                    out[j++] = str[i++];
                    break;
            }
        } else {
            out[j++] = str[i++];
        }
    }
    out[j] = '\0';
    return 0;
}

/* 显示帮助 */
static void usage(void) {
    printf("Usage: %s [SHORT-OPTION]... [STRING]...\n", PROGRAM_NAME);
    printf("  or:  %s LONG-OPTION\n", PROGRAM_NAME);
    printf("\n");
    printf("Echo the STRING(s) to standard output.\n");
    printf("\n");
    printf("  -n             do not output the trailing newline\n");
    printf("  -e             enable interpretation of backslash escapes\n");
    printf("  -E             disable interpretation of backslash escapes (default)\n");
    printf("      --help     display this help and exit\n");
    printf("      --version  output version information and exit\n");
    printf("\n");
    printf("If -e is in effect, the following sequences are recognized:\n");
    printf("\n");
    printf("  \\\\      backslash\n");
    printf("  \\a      alert (BEL)\n");
    printf("  \\b      backspace\n");
    printf("  \\c      produce no further output\n");
    printf("  \\e      escape\n");
    printf("  \\f      form feed\n");
    printf("  \\n      new line\n");
    printf("  \\r      carriage return\n");
    printf("  \\t      horizontal tab\n");
    printf("  \\v      vertical tab\n");
    printf("  \\0NNN   byte with octal value NNN (1 to 3 digits)\n");
    printf("  \\xHH    byte with hexadecimal value HH (1 to 2 digits)\n");
    printf("\n");
    printf("NOTE: your shell may have its own version of echo, which usually supersedes\n");
    printf("the version described here.  Please refer to your shell's documentation\n");
    printf("for details about the options it supports.\n");
}

/* 版本信息 */
static void version(void) {
    printf("%s (GNU coreutils compatible) 1.0\n", PROGRAM_NAME);
    printf("Copyright (C) 2024 Free Software Foundation, Inc.\n");
    printf("License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.\n");
    printf("This is free software: you are free to change and redistribute it.\n");
    printf("There is NO WARRANTY, to the extent permitted by law.\n");
}

int main(int argc, char *argv[]) {
    int optind = 1;
    
    /* 解析短选项 */
    while (optind < argc && argv[optind][0] == '-' && argv[optind][1] != '\0') {
        /* 处理长选项 */
        if (strcmp(argv[optind], "--help") == 0) {
            usage();
            return 0;
        }
        if (strcmp(argv[optind], "--version") == 0) {
            version();
            return 0;
        }
        
        /* 处理短选项 */
        char *p = argv[optind] + 1;
        while (*p) {
            switch (*p) {
                case 'n':
                    no_newline = true;
                    p++;
                    break;
                case 'e':
                    do_escape = true;
                    no_escape = false;
                    p++;
                    break;
                case 'E':
                    no_escape = true;
                    do_escape = false;
                    p++;
                    break;
                default:
                    /* 未知选项，当作字符串处理（兼容 POSIX） */
                    goto end_options;
            }
        }
        optind++;
    }
    
end_options:
    
    /* 处理参数 */
    bool first = true;
    bool stop_output = false;
    
    for (int i = optind; i < argc && !stop_output; i++) {
        const char *str = argv[i];
        
        /* 输出分隔符 */
        if (!first) {
            putchar(' ');
        }
        first = false;
        
        /* 处理转义或原样输出 */
        if (do_escape) {
            char *processed = malloc(strlen(str) * 2 + 1);
            if (!processed) {
                perror(PROGRAM_NAME);
                return 1;
            }
            if (process_escape(str, processed) == -1) {
                /* \c 停止输出 */
                stop_output = true;
            }
            fputs(processed, stdout);
            free(processed);
        } else {
            fputs(str, stdout);
        }
    }
    
    /* 输出换行（除非 -n） */
    if (!no_newline && !stop_output) {
        putchar('\n');
    }
    
    return 0;
}
