#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_CONFIG "/data/data/com.termux/files/usr/etc/digson/cde/nastconfig/Configfile"

int main(int argc, char *argv[]) {
    /* 设置 HITITGIT */
    if (!getenv("HITITGIT")) {
        setenv("HITITGIT", DEFAULT_CONFIG, 1);
    }
    
    /* 解析选项 */
    int gui_check = 0;
    int show_config = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--gui") == 0) {
            gui_check = 1;
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            show_config = 1;
        }
    }
    
    /* 构建参数列表 */
    char *args[4];
    int arg_count = 0;
    
    args[arg_count++] = "status-nast";
    
    if (show_config) {
        args[arg_count++] = "-c";  // 配置模式
    } else if (gui_check) {
        args[arg_count++] = "-g";  // GUI检查模式
    }
    
    args[arg_count] = NULL;
    
    /* 执行 status-nast */
    execvp("status-nast", args);
    
    /* 如果失败，尝试直接路径 */
    char path[256];
    snprintf(path, sizeof(path), "/data/data/com.termux/files/usr/bin/status-nast");
    execv(path, args);
    
    perror("Failed to start status-nast");
    fprintf(stderr, "Please install CDE first: cd cde && make install\n");
    return 1;
}
