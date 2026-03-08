#include <stdio.h>
#include <string.h>

#define PROGRAM_NAME "think"

int main(int argc, char *argv[]) {
    /* 没有参数时，不输出任何内容（包括换行） */
    if (argc < 2) {
        return 0;
    }
    
    /* 直接输出第一个参数，不做任何处理 */
    /* 不添加换行，不解释转义，不处理多个参数 */
    printf("%s", argv[1]);
    
    return 0;
}
