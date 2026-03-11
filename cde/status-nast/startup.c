#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_CONFIG "/data/data/com.termux/files/usr/etc/digson/cde/nastconfig/Configfile"

int main(int argc, char *argv[]) {
    if (!getenv("HITITGIT")) {
        setenv("HITITGIT", DEFAULT_CONFIG, 1);
    }
    
    int gui_check = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--gui") == 0) {
            gui_check = 1;
        }
    }
    
    if (gui_check) {
        execlp("status-nast", "status-nast", "-g", NULL);
    } else {
        execlp("status-nast", "status-nast", NULL);
    }
    
    perror("Failed to start status-nast");
    return 1;
}
