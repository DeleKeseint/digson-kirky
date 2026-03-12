#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <curl/curl.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <ctype.h>

#define PROGRAM_NAME "get"
#define VERSION "1.0"

/* 运行模式 */
typedef enum {
    MODE_WGET,      /* 默认：wget 风格 */
    MODE_CURL       /* --wip：curl 风格 */
} run_mode_t;

static run_mode_t mode = MODE_WGET;

/* wget 风格选项 */
int curl_insecure = 0;
static char *output_document = NULL;    /* -O, --output-document */
static char *output_directory = NULL;   /* -P, --directory-prefix */
static int continue_download = 0;       /* -c, --continue */
static int timestamping = 0;            /* -N, --timestamping */
static int no_clobber = 0;              /* -nc, --no-clobber */
static int recursive = 0;               /* -r, --recursive */
static int level = 5;                   /* -l, --level */
static char *user_agent = NULL;         /* -U, --user-agent */
static int quiet = 0;                   /* -q, --quiet */
static int verbose = 0;                 /* -v, --verbose */
static int show_progress = 1;           /* --progress */
static int timeout = 0;                 /* -T, --timeout */
static int tries = 20;                  /* -t, --tries */
static char *http_user = NULL;          /* --http-user */
static char *http_passwd = NULL;        /* --http-password */
static int spider = 0;                  /* --spider */
static char *post_data = NULL;          /* --post-data */
static char *header = NULL;             /* --header */
static int no_check_certificate = 0;    /* --no-check-certificate */
static char *bind_address = NULL;       /* --bind-address */
static int limit_rate = 0;              /* --limit-rate */

/* curl 风格选项（--wip 模式） */
static int curl_progress_bar = 0;       /* -#, --progress-bar */
static int curl_fail_silently = 0;      /* -f, --fail */
static int curl_include_header = 0;     /* -i, --include */
static int curl_head_only = 0;          /* -I, --head */
static int curl_location = 0;           /* -L, --location */
static int curl_remote_name = 0;        /* -O, --remote-name */
static char *curl_output = NULL;        /* -o, --output */
static int curl_silent = 0;             /* -s, --silent */
static int curl_show_error = 0;         /* -S, --show-error */
static char *curl_upload_file = NULL;   /* -T, --upload-file */
static char *curl_user = NULL;          /* -u, --user */
static char *curl_request = NULL;       /* -X, --request */
static char *curl_data = NULL;          /* -d, --data */
static char *curl_form = NULL;          /* -F, --form */

/* 下载统计 */
static double download_speed = 0;
static double downloaded_size = 0;
static time_t start_time;

/* ==================== Realtak 配置 ==================== */

typedef struct {
    int used_for_get;
    int network_disconnected;
    int find_router;
    int user_confirmed_no_card;
} realtak_config_t;

static realtak_config_t realtak = {0, 0, 0, 0};
static int realtak_config_loaded = 0;

static int cmd_exists(const char *cmd) {
    char check[256];
    snprintf(check, sizeof(check), "which %s >/dev/null 2>&1", cmd);
    return system(check) == 0;
}

static int is_termux(void) {
    return cmd_exists("pkg");
}

static void load_realtak_config(void) {
    if (realtak_config_loaded) return;
    
    char *paths[] = {
        "/data/data/com.termux/files/usr/etc/digson/get/RealtakConfig",
        "/etc/RealtakConfig",
        NULL
    };
    
    FILE *fp = NULL;
    const char *used_path = NULL;
    
    for (int i = 0; paths[i]; i++) {
        fp = fopen(paths[i], "r");
        if (fp) {
            used_path = paths[i];
            break;
        }
    }
    
    if (!fp) {
        if (verbose) {
            fprintf(stderr, "RealtakConfig: No config file found\n");
        }
        return;
    }
    
    if (verbose) {
        fprintf(stderr, "RealtakConfig: Loading from %s\n", used_path);
    }
    
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';
        
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '#' || *p == '\0') continue;
        
        int val;
        if (sscanf(p, "USE_FOR_GET=%d", &val) == 1) {
            realtak.used_for_get = val;
        } else if (sscanf(p, "NETWORK_DISCONNECTED=%d", &val) == 1) {
            realtak.network_disconnected = val;
        } else if (sscanf(p, "FIND_ROUTER=%d", &val) == 1) {
            realtak.find_router = val;
        } else if (sscanf(p, "USER_CONFIRMED_NO_CARD=%d", &val) == 1) {
            realtak.user_confirmed_no_card = val;
        }
    }
    
    fclose(fp);
    realtak_config_loaded = 1;
    
    if (verbose) {
        fprintf(stderr, "RealtakConfig: loaded (used_for_get=%d, network_disconnected=%d, find_router=%d)\n",
                realtak.used_for_get, realtak.network_disconnected, realtak.find_router);
    }
}

static int check_realtak_allowed(void) {
    load_realtak_config();
    
    if (realtak.user_confirmed_no_card) {
        return 1;
    }
    
    if (!realtak.used_for_get) {
        if (!quiet) {
            fprintf(stderr, "get: Realtak network card not configured for use with get command.\n");
            fprintf(stderr, "      Run 'setting' to configure Realtak settings.\n");
        }
        return 0;
    }
    
    if (realtak.network_disconnected) {
        if (!quiet) {
            fprintf(stderr, "get: Network disabled by Realtak configuration (NETWORK_DISCONNECTED=1)\n");
        }
        return 0;
    }
    
    return 1;
}

static void find_nearby_router(void) {
    if (!realtak.find_router) return;
    
    if (!quiet) {
        printf("Realtak: Scanning for nearby routers...\n");
    }
    
    if (system("which iwlist >/dev/null 2>&1") == 0) {
        system("iwlist wlan0 scan 2>/dev/null | grep -E 'Cell|ESSID|Quality' | head -10");
    } else if (system("which termux-wifi-scaninfo >/dev/null 2>&1") == 0) {
        system("termux-wifi-scaninfo 2>/dev/null | head -20");
    }
}

/* ==================== 原有代码 ==================== */

static void error(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    fprintf(stderr, "%s: ", PROGRAM_NAME);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

static void version(void) {
    printf("%s %s (libcurl/%s)\n", PROGRAM_NAME, VERSION, curl_version());
}

static void usage_wget(void) {
    printf("Usage: %s [OPTION]... [URL]...\n\n", PROGRAM_NAME);
    printf("  -h, --help              print this help\n");
    printf("  -V, --version           display version\n");
    printf("  -O,  --output-document=FILE    write documents to FILE\n");
    printf("  -P,  --directory-prefix=PREFIX  save files to PREFIX/...\n");
    printf("  -c,  --continue                resume getting\n");
    printf("  -N,  --timestamping            don't re-retrieve unless newer\n");
    printf("  -nc, --no-clobber              skip existing files\n");
    printf("  -r,  --recursive              recursive download\n");
    printf("  -l,  --level=NUMBER            recursion depth\n");
    printf("  -U,  --user-agent=AGENT        user agent\n");
    printf("  -q,  --quiet                   quiet (no output)\n");
    printf("  -v,  --verbose                 verbose\n");
    printf("  -T,  --timeout=SECONDS         timeout\n");
    printf("  -t,  --tries=NUMBER            retry times\n");
    printf("       --spider                  don't download\n");
    printf("       --post-data=STRING        POST data\n");
    printf("       --header=STRING           custom header\n");
    printf("       --no-check-certificate    skip SSL verify\n");
    printf("       --bind-address=ADDRESS    bind address\n");
    printf("       --limit-rate=RATE         limit rate\n");
    printf("       --wip                     curl mode\n");
}

static void usage_curl(void) {
    printf("Usage: %s [options...] <url>\n", PROGRAM_NAME);
    printf("     --wip  (switch to curl mode)\n\n");
    printf("  -h, --help              this help\n");
    printf("  -V, --version           version\n");
    printf(" -o, --output <file>      write to file\n");
    printf(" -O, --remote-name        use remote filename\n");
    printf(" -L, --location           follow redirects\n");
    printf(" -I, --head               show headers only\n");
    printf(" -i, --include            include headers\n");
    printf(" -s, --silent             silent mode\n");
    printf(" -f, --fail               fail silently\n");
    printf(" -S, --show-error         show error\n");
    printf(" -u, --user <user:pass>  user and password\n");
    printf(" -d, --data <data>        POST data\n");
    printf(" -F, --form <data>        multipart POST\n");
    printf(" -T, --upload-file <file> upload file\n");
    printf(" -X, --request <cmd>      request method\n");
    printf(" -k, --insecure           skip SSL verify\n");
    printf(" -#, --progress-bar       progress bar\n");
}

static int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t ultotal, curl_off_t ulnow) {
    if (!show_progress || quiet || curl_silent) return 0;
    
    if (dltotal > 0) {
        double percent = (double)dlnow / (double)dltotal * 100.0;
        double speed = 0;
        time_t now = time(NULL);
        double elapsed = difftime(now, start_time);
        
        if (elapsed > 0) {
            speed = dlnow / elapsed;
        }
        
        if (mode == MODE_WGET) {
            printf("\r%3.0f%% [", percent);
            int pos = (int)(percent / 2);
            for (int i = 0; i < 50; i++) {
                if (i < pos) printf("=");
                else if (i == pos) printf(">");
                else printf(" ");
            }
            printf("] %ld / %ld", (long)dlnow, (long)dltotal);
            if (speed > 0) {
                printf("  %.2f KB/s", speed / 1024);
            }
            fflush(stdout);
        } else {
            printf("\r%3.0f%%  %ld  %ld  %.2f", percent, (long)dlnow, (long)dltotal, speed);
            fflush(stdout);
        }
    }
    return 0;
}

static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    FILE *fp = (FILE *)userdata;
    if (fp) {
        return fwrite(ptr, size, nmemb, fp);
    }
    return fwrite(ptr, size, nmemb, stdout);
}

static size_t header_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    if (curl_include_header || (mode == MODE_WGET && verbose)) {
        return fwrite(ptr, size, nmemb, stdout);
    }
    return size * nmemb;
}

static char* get_filename(const char *url) {
    const char *last_slash = strrchr(url, '/');
    if (last_slash && *(last_slash + 1)) {
        return strdup(last_slash + 1);
    }
    return strdup("index.html");
}

static int download(const char *url) {
    CURL *curl;
    CURLcode res;
    FILE *fp = NULL;
    char *filename = NULL;
    char *full_path = NULL;
    struct stat st;
    long resume_from = 0;
    
    curl = curl_easy_init();
    if (!curl) {
        error("Failed to initialize curl");
        return 1;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    
    if (mode == MODE_WGET || curl_location) {
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    }
    
    if (user_agent) {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
    } else if (mode == MODE_WGET) {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Wget/1.20.3 (linux-gnu)");
    } else {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl/7.68.0");
    }
    
    if (timeout > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)timeout);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)timeout);
    }
    
    if (no_check_certificate || (mode == MODE_CURL && curl_insecure)) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    
    if (http_user && http_passwd) {
        char auth[512];
        snprintf(auth, sizeof(auth), "%s:%s", http_user, http_passwd);
        curl_easy_setopt(curl, CURLOPT_USERPWD, auth);
    } else if (curl_user) {
        curl_easy_setopt(curl, CURLOPT_USERPWD, curl_user);
    }
    
    if (post_data || curl_data) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data ? post_data : curl_data);
    }
    
    if (header) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, header);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    
    if (bind_address) {
        curl_easy_setopt(curl, CURLOPT_INTERFACE, bind_address);
    }
    
    if (limit_rate > 0) {
        curl_easy_setopt(curl, CURLOPT_MAX_RECV_SPEED_LARGE, (curl_off_t)limit_rate);
    }
    
    if (mode == MODE_WGET) {
        if (output_document) {
            filename = strdup(output_document);
        } else {
            filename = get_filename(url);
            if (output_directory) {
                full_path = malloc(strlen(output_directory) + strlen(filename) + 2);
                sprintf(full_path, "%s/%s", output_directory, filename);
                free(filename);
                filename = full_path;
            }
        }
    } else {
        if (curl_output) {
            filename = strdup(curl_output);
        } else if (curl_remote_name) {
            filename = get_filename(url);
        } else {
            filename = NULL;
        }
    }
    
    if (spider) {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(curl, CURLOPT_HEADER, 1L);
    }
    
    if (curl_head_only) {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    }
    
    if (filename && !spider && !curl_head_only) {
        if (continue_download && stat(filename, &st) == 0) {
            resume_from = st.st_size;
            curl_easy_setopt(curl, CURLOPT_RESUME_FROM, resume_from);
        }
        
        if (no_clobber && stat(filename, &st) == 0) {
            if (!quiet) printf("File '%s' already there; not retrieving.\n", filename);
            curl_easy_cleanup(curl);
            free(filename);
            return 0;
        }
        
        fp = fopen(filename, continue_download ? "ab" : "wb");
        if (!fp) {
            error("Cannot write to '%s': %s", filename, strerror(errno));
            curl_easy_cleanup(curl);
            free(filename);
            return 1;
        }
    }
    
    if (fp || spider || curl_head_only) {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp ? (void *)fp : (void *)stdout);
    }
    
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    
    if (show_progress && !quiet && !curl_silent) {
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    } else {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    }
    
    start_time = time(NULL);
    res = curl_easy_perform(curl);
    
    if (show_progress && !quiet && !curl_silent && fp) {
        printf("\n");
    }
    
    if (res != CURLE_OK) {
        if (!curl_fail_silently || curl_show_error) {
            error("%s", curl_easy_strerror(res));
        }
        if (fp) fclose(fp);
        curl_easy_cleanup(curl);
        free(filename);
        return 1;
    }
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    if (spider) {
        printf("Spider mode enabled. Check: %ld %s\n", http_code, 
               (http_code == 200) ? "OK" : "FAILED");
    }
    
    if (fp) fclose(fp);
    curl_easy_cleanup(curl);
    
    if (filename) {
        if (!quiet && !curl_silent && !spider) {
            if (mode == MODE_WGET) {
                printf("'%s' saved\n", filename);
            }
        }
        free(filename);
    }
    
    return (http_code >= 400) ? 1 : 0;
}

int main(int argc, char *argv[]) {
    int opt;
    int option_index = 0;
    char **urls = NULL;
    int url_count = 0;
    
    static struct option long_options[] = {
        {"output-document", required_argument, 0, 'O'},
        {"directory-prefix", required_argument, 0, 'P'},
        {"continue", no_argument, 0, 'c'},
        {"timestamping", no_argument, 0, 'N'},
        {"no-clobber", no_argument, 0, 0},
        {"recursive", no_argument, 0, 'r'},
        {"level", required_argument, 0, 'l'},
        {"user-agent", required_argument, 0, 'U'},
        {"quiet", no_argument, 0, 'q'},
        {"verbose", no_argument, 0, 'v'},
        {"timeout", required_argument, 0, 'T'},
        {"tries", required_argument, 0, 't'},
        {"http-user", required_argument, 0, 0},
        {"http-password", required_argument, 0, 0},
        {"spider", no_argument, 0, 0},
        {"post-data", required_argument, 0, 0},
        {"header", required_argument, 0, 0},
        {"no-check-certificate", no_argument, 0, 0},
        {"bind-address", required_argument, 0, 0},
        {"limit-rate", required_argument, 0, 0},
        {"wip", no_argument, 0, 0},
        {"progress-bar", no_argument, 0, '#'},
        {"fail", no_argument, 0, 'f'},
        {"include", no_argument, 0, 'i'},
        {"head", no_argument, 0, 'I'},
        {"location", no_argument, 0, 'L'},
        {"remote-name", no_argument, 0, 'o'},
        {"output", required_argument, 0, 'o'},
        {"silent", no_argument, 0, 's'},
        {"show-error", no_argument, 0, 'S'},
        {"upload-file", required_argument, 0, 'T'},
        {"user", required_argument, 0, 'u'},
        {"request", required_argument, 0, 'X'},
        {"data", required_argument, 0, 'd'},
        {"form", required_argument, 0, 'F'},
        {"insecure", no_argument, 0, 'k'},
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'V'},
        {0, 0, 0, 0}
    };
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--wip") == 0) {
            mode = MODE_CURL;
            for (int j = i; j < argc - 1; j++) {
                argv[j] = argv[j + 1];
            }
            argc--;
            break;
        }
    }
    
    while ((opt = getopt_long(argc, argv, 
        mode == MODE_WGET ? "O:P:cNqvrU:T:t:l:hV" : "#fiILo:sSu:T:X:d:F:kvhV",
        long_options, &option_index)) != -1) {
        
        switch (opt) {
            case 'O':
                output_document = optarg;
                break;
            case 'P':
                output_directory = optarg;
                break;
            case 'c':
                continue_download = 1;
                break;
            case 'N':
                timestamping = 1;
                break;
            case 'q':
                quiet = 1;
                show_progress = 0;
                break;
            case 'v':
                verbose = 1;
                break;
            case 'r':
                recursive = 1;
                break;
            case 'l':
                level = atoi(optarg);
                break;
            case 'U':
                user_agent = optarg;
                break;
            case 'T':
                timeout = atoi(optarg);
                break;
            case 't':
                tries = atoi(optarg);
                break;
            case '#':
                curl_progress_bar = 1;
                break;
            case 'f':
                curl_fail_silently = 1;
                break;
            case 'i':
                curl_include_header = 1;
                break;
            case 'I':
                curl_head_only = 1;
                break;
            case 'L':
                curl_location = 1;
                break;
            case 'o':
                if (mode == MODE_CURL) {
                    curl_output = optarg;
                    curl_remote_name = 0;
                }
                break;
            case 's':
                curl_silent = 1;
                show_progress = 0;
                break;
            case 'S':
                curl_show_error = 1;
                break;
            case 'u':
                curl_user = optarg;
                break;
            case 'T':
                if (mode == MODE_CURL) {
                    curl_upload_file = optarg;
                }
                break;
            case 'X':
                curl_request = optarg;
                break;
            case 'd':
                curl_data = optarg;
                break;
            case 'F':
                curl_form = optarg;
                break;
            case 'k':
                curl_insecure = 1;
                break;
            case 'h':
                if (mode == MODE_WGET) usage_wget();
                else usage_curl();
                return 0;
            case 'V':
                version();
                return 0;
            case 0:
                if (strcmp(long_options[option_index].name, "no-clobber") == 0) {
                    no_clobber = 1;
                } else if (strcmp(long_options[option_index].name, "http-user") == 0) {
                    http_user = optarg;
                } else if (strcmp(long_options[option_index].name, "http-password") == 0) {
                    http_passwd = optarg;
                } else if (strcmp(long_options[option_index].name, "spider") == 0) {
                    spider = 1;
                } else if (strcmp(long_options[option_index].name, "post-data") == 0) {
                    post_data = optarg;
                } else if (strcmp(long_options[option_index].name, "header") == 0) {
                    header = optarg;
                } else if (strcmp(long_options[option_index].name, "no-check-certificate") == 0) {
                    no_check_certificate = 1;
                } else if (strcmp(long_options[option_index].name, "bind-address") == 0) {
                    bind_address = optarg;
                } else if (strcmp(long_options[option_index].name, "limit-rate") == 0) {
                    limit_rate = atoi(optarg);
                    char last = optarg[strlen(optarg) - 1];
                    if (last == 'k' || last == 'K') limit_rate *= 1024;
                    else if (last == 'm' || last == 'M') limit_rate *= 1024 * 1024;
                }
                break;
            default:
                return 1;
        }
    }
    
    /* ==================== Realtak 检查 ==================== */
    
    if (!check_realtak_allowed()) {
        return 1;
    }
    
    find_nearby_router();
    
    /* ==================== 原有下载逻辑 ==================== */
    
    if (optind >= argc) {
        error("Missing URL");
        return 1;
    }
    
    urls = &argv[optind];
    url_count = argc - optind;
    
    int ret = 0;
    for (int i = 0; i < url_count; i++) {
        if (url_count > 1 && !quiet && !curl_silent) {
            printf("\n==> Downloading: %s\n", urls[i]);
        }
        if (download(urls[i]) != 0) {
            ret = 1;
        }
    }
    
    return ret;
}
