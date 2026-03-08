#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <libssh/libssh.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libssh/libssh.h>
#include <netdb.h>
#include <arpa/inet.h>

// ========== 兼容性修复 ==========
// 为旧版本 libssh 实现缺失的函数

// 替代 ssh_get_hostname() 函数
static const char *compat_ssh_get_hostname(ssh_session session) {
    static char hostname[256];
    const char *host = NULL;
    
    // 尝试从会话获取主机名
    host = ssh_get_host(session);
    if (host) {
        return host;
    }
    
    // 如果获取不到，尝试从套接字获取
    int sock = ssh_get_fd(session);
    if (sock >= 0) {
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        if (getpeername(sock, (struct sockaddr*)&addr, &len) == 0) {
            struct hostent *he = gethostbyaddr((const char*)&addr.sin_addr, 
                                              sizeof(addr.sin_addr), AF_INET);
            if (he) {
                return he->h_name;
            }
        }
    }
    
    return "unknown-host";
}

// 替代 ssh_get_remote_hostname() 函数
static const char *compat_ssh_get_remote_hostname(ssh_session session) {
    return compat_ssh_get_hostname(session);
}

// 替代 ssh_get_remote_ipaddr() 函数
static const char *compat_ssh_get_remote_ipaddr(ssh_session session) {
    static char ip[INET6_ADDRSTRLEN];
    int sock = ssh_get_fd(session);
    
    if (sock >= 0) {
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        if (getpeername(sock, (struct sockaddr*)&addr, &len) == 0) {
            inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
            return ip;
        }
    }
    
    return "0.0.0.0";
}

// 替代 ssh_get_remote_username() 函数
static const char *compat_ssh_get_remote_username(ssh_session session) {
    // 从会话获取用户名
    const char *user = ssh_get_user(session);
    if (user && strlen(user) > 0) {
        return user;
    }
    
    // 备用方案
    user = getenv("USER");
    if (user) return user;
    
    return "unknown-user";
}

// 使用宏替换所有调用
#define ssh_get_remote_hostname(session) compat_ssh_get_remote_hostname(session)
#define ssh_get_remote_ipaddr(session) compat_ssh_get_remote_ipaddr(session)
#define ssh_get_remote_username(session) compat_ssh_get_remote_username(session)
#define ssh_get_hostname(session) compat_ssh_get_hostname(session)


#define PROGRAM_NAME "come"
#define VERSION "1.0"
#define DEFAULT_SSH_PORT 8022

/* 运行模式 */
typedef enum {
    MODE_ELINKS,    /* 默认：elinks 网页浏览器模式 */
    MODE_SSH        /* --wip：ssh 远程登录模式 */
} run_mode_t;

static run_mode_t mode = MODE_ELINKS;

/* 选项风格 */
typedef enum {
    OPT_ELINKS,     /* 默认：elinks 风格选项 */
    OPT_SSH         /* --wip-opt：ssh 风格选项 */
} opt_style_t;

static opt_style_t opt_style = OPT_ELINKS;

/* elinks 选项 */
static int elinks_auto_submit = 0;        /* -auto-submit */
static char *elinks_bookmark_add = NULL;  /* -bookmark-add */
static char *elinks_bookmark_manager = NULL; /* -bookmark-manager */
static int elinks_anonymous = 0;          /* -anonymous */
static char *elinks_base_session = NULL;  /* -base-session */
static int elinks_cmd_move = 0;           /* -cmd-move */
static int elinks_cmd_scroll = 0;         /* -cmd-scroll */
static char *elinks_config_dir = NULL;    /* -config-dir */
static int elinks_default = 0;            /* -default */
static char *elinks_dump = NULL;          /* -dump */
static int elinks_force_html = 0;         /* -force-html */
static int elinks_g = 0;                  /* -g */
static char *elinks_homepage = NULL;      /* -homepage */
static int elinks_lookup = 0;             /* -lookup */
static int elinks_no_connect = 0;         /* -no-connect */
static int elinks_no_home = 0;            /* -no-home */
static int elinks_no_numbering = 0;       /* -no-numbering */
static int elinks_no_references = 0;      /* -no-references */
static int elinks_remote = 0;             /* -remote */
static char *elinks_session_ring = NULL;  /* -session-ring */
static char *elinks_source = NULL;        /* -source */
static int elinks_stdin = 0;              /* -stdin */
static int elinks_touch_files = 0;        /* -touch-files */
static int elinks_verbose = 0;            /* -verbose */
static int elinks_version = 0;              /* -version */

/* ssh 选项 */
static char *ssh_user = NULL;             /* -l, -u, --user */
static char *ssh_host = NULL;             /* 主机名 */
static int ssh_port = DEFAULT_SSH_PORT; /* -p, --port (默认 8022) */
static char *ssh_identity = NULL;         /* -i, --identity */
static int ssh_verbose = 0;               /* -v, --verbose */
static int ssh_quiet = 0;                 /* -q, --quiet */
static char *ssh_command = NULL;          /* -c, --command */
static int ssh_forward_agent = 0;         /* -A, --forward-agent */
static int ssh_no_forward_agent = 0;      /* -a, --no-forward-agent */
static char *ssh_forward_port = NULL;       /* -L, --forward */
static char *ssh_remote_forward = NULL;     /* -R, --remote-forward */
static char *ssh_config = NULL;           /* -F, --config */
static int ssh_noshell = 0;               /* -N, --no-shell */
static int ssh_fork = 0;                  /* -f, --fork */
static int ssh_compression = 0;           /* -C, --compression */
static char *ssh_cipher = NULL;           /* -c, --cipher */
static char *ssh_option = NULL;           /* -o, --option */
static int ssh_X11_forward = 0;            /* -X, --forward-x11 */
static int ssh_no_X11_forward = 0;        /* -x, --no-forward-x11 */
static char *ssh_bind_address = NULL;     /* -b, --bind */
static int ssh_background = 0;              /* -B, --background */
static int ssh_escape_char = '~';          /* -e, --escape */
static int ssh_login = 1;                  /* 默认启用登录 */
static char *ssh_host_key_alias = NULL;   /* -host-key-alias */
static int ssh_ipv4 = 0;                   /* -4, --ipv4 */
static int ssh_ipv6 = 0;                   /* -6, --ipv6 */

/* 目标 URL 或主机 */
static char *target = NULL;

/* 错误处理 */
static void error(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    fprintf(stderr, "%s: ", PROGRAM_NAME);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

/* 显示 elinks 风格帮助 */
static void usage_elinks(void) {
    printf("Usage: %s [OPTION]... [URL|FILE]...\n", PROGRAM_NAME);
    printf("\n");
    printf("Options:\n");
    printf("  -anonymous [0|1]       Restricts %s so that it can only be used for\n", PROGRAM_NAME);
    printf("                          anonymous browsing. Local file browsing,\n");
    printf("                          downloads, bookmarks, history, and all other\n");
    printf("                          non-anonymous features are disabled.\n");
    printf("  -auto-submit [0|1]   Automatically submit the first form.\n");
    printf("  -base-session <id>     Clone session with given ID.\n");
    printf("  -bind-address <address> Bind to specific local address (ssh mode).\n");
    printf("  -bookmark-add <url>    Add a bookmark.\n");
    printf("  -bookmark-manager      Open bookmark manager.\n");
    printf("  -cmd-move [0|1]       Move cursor to document start.\n");
    printf("  -cmd-scroll [0|1]     Scroll document.\n");
    printf("  -config-dir <dir>      Directory for configuration files.\n");
    printf("  -dump [charset]        Dump formatted page to stdout.\n");
    printf("  -force-html           Treat input as HTML.\n");
    printf("  -g                    Run without user interface (same as -dump).\n");
    printf("  -homepage <url>        Set homepage URL.\n");
    printf("  -lookup               Look up host IP address.\n");
    printf("  -no-connect [0|1]     Don't connect at startup.\n");
    printf("  -no-home [0|1]        Disable loading of files from home directory.\n");
    printf("  -no-numbering [0|1]  Disable link numbering in dump output.\n");
    printf("  -no-references [0|1]   Disable link references in dump output.\n");
    printf("  -remote               Control a remote instance.\n");
    printf("  -session-ring <num>    Session ring ID.\n");
    printf("  -source [charset]      Dump page source to stdout.\n");
    printf("  -stdin [file]          Read document from stdin.\n");
    printf("  -touch-files [0|1]    Touch local files.\n");
    printf("  -verbose <num>         Verbose level (0-9).\n");
    printf("  -version              Display version information.\n");
    printf("\n");
    printf("SSH mode options (when using --wip):\n");
    printf("  -l, -u, --user <user>  Login as user.\n");
    printf("  -p, --port <port>      Connect to port (default: %d).\n", DEFAULT_SSH_PORT);
    printf("  -i, --identity <file>   Identity file (private key).\n");
    printf("  -v, --verbose          Verbose mode.\n");
    printf("  -q, --quiet            Quiet mode.\n");
    printf("  -c, --command <cmd>    Execute command instead of shell.\n");
    printf("  -A, --forward-agent     Forward authentication agent.\n");
    printf("  -a, --no-forward-agent Don't forward authentication agent.\n");
    printf("  -L, --forward <spec>   Forward local port to remote.\n");
    printf("  -R, --remote-forward <spec> Forward remote port to local.\n");
    printf("  -F, --config <file>    Configuration file.\n");
    printf("  -N, --no-shell         Don't execute remote commands.\n");
    printf("  -f, --fork             Fork into background.\n");
    printf("  -C, --compression      Enable compression.\n");
    printf("  -X, --forward-x11      Enable X11 forwarding.\n");
    printf("  -x, --no-forward-x11   Disable X11 forwarding.\n");
    printf("  -4, --ipv4             Use IPv4 only.\n");
    printf("  -6, --ipv6             Use IPv6 only.\n");
    printf("\n");
    printf("Mode switching:\n");
    printf("  --wip                  Switch to SSH mode.\n");
    printf("  --wip-opt              Use SSH-style options in elinks mode.\n");
    printf("\n");
    printf("Please report bugs to <bug-elinks@lists.linuxfromscratch.org>.\n");
}

/* 显示 ssh 风格帮助 */
static void usage_ssh(void) {
    printf("usage: %s [-1246AaCfGgKkMNnqsTtVvXxYy] [-b bind_address] [-c cipher_spec]\n", PROGRAM_NAME);
    printf("           [-D [bind_address:]port] [-E log_file] [-e escape_char]\n");
    printf("           [-F configfile] [-I pkcs11] [-i identity_file]\n");
    printf("           [-J [user@]host[:port]] [-L address] [-l login_name] [-m mac_spec]\n");
    printf("           [-O ctl_cmd] [-o option] [-p port] [-Q query_option] [-R address]\n");
    printf("           [-S ctl_path] [-W host:port] [-w local:tunnel] destination\n");
    printf("           [command [argument ...]]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -1                      Forces ssh to try protocol version 1 only.\n");
    printf("  -2                      Forces ssh to try protocol version 2 only.\n");
    printf("  -4                      Forces ssh to use IPv4 addresses only.\n");
    printf("  -6                      Forces ssh to use IPv6 addresses only.\n");
    printf("  -A                      Enables forwarding of the authentication agent.\n");
    printf("  -a                      Disables forwarding of the authentication agent.\n");
    printf("  -b bind_address         Use bind_address on the local machine.\n");
    printf("  -C                      Requests compression of all data.\n");
    printf("  -c cipher_spec        Selects the cipher specification.\n");
    printf("  -D [bind_addr:]port   Specifies a local dynamic application-level port forwarding.\n");
    printf("  -e escape_char        Sets the escape character for sessions (default: ~).\n");
    printf("  -F configfile         Specifies an alternative per-user configuration file.\n");
    printf("  -f                      Requests ssh to go to background.\n");
    printf("  -g                      Allows remote hosts to connect to local forwarded ports.\n");
    printf("  -I pkcs11             Specify the PKCS#11 shared library ssh should use.\n");
    printf("  -i identity_file      Selects a file from which the identity (private key) is read.\n");
    printf("  -J [user@]host[:port] Connect to the target host by first making a ssh connection.\n");
    printf("  -L [bind_addr:]port:host:hostport\n");
    printf("                          Specifies that connections to the given TCP port are forwarded.\n");
    printf("  -l login_name         Specifies the user to log in as on the remote machine.\n");
    printf("  -M                      Places the ssh client into ``master'' mode.\n");
    printf("  -m mac_spec           A comma-separated list of MAC algorithms.\n");
    printf("  -N                      Do not execute a remote command.\n");
    printf("  -n                      Redirects stdin from /dev/null.\n");
    printf("  -O ctl_cmd            Control an active connection multiplexing master process.\n");
    printf("  -o option             Can be used to give options in the format used in ssh_config.\n");
    printf("  -p port               Port to connect to on the remote host (default: %d).\n", DEFAULT_SSH_PORT);
    printf("  -q                      Quiet mode.\n");
    printf("  -R [bind_addr:]port:host:hostport\n");
    printf("                          Specifies that the given port on the remote server is forwarded.\n");
    printf("  -S ctl_path           Specifies the location of a control socket for connection sharing.\n");
    printf("  -s                      May be used to request invocation of a subsystem.\n");
    printf("  -T                      Disable pseudo-terminal allocation.\n");
    printf("  -t                      Force pseudo-terminal allocation.\n");
    printf("  -V                      Display the version number.\n");
    printf("  -v                      Verbose mode.\n");
    printf("  -W host:port          Requests that standard input and output be forwarded to host.\n");
    printf("  -w local:tunnel       Requests tunnel device forwarding.\n");
    printf("  -X                      Enables X11 forwarding.\n");
    printf("  -x                      Disables X11 forwarding.\n");
    printf("  -Y                      Enables trusted X11 forwarding.\n");
    printf("  -y                      Send log information using the syslog module.\n");
    printf("\n");
    printf("Web mode options (when not using --wip):\n");
    printf("  -anonymous             Anonymous browsing mode.\n");
    printf("  -dump                  Dump formatted page to stdout.\n");
    printf("  -source                Dump page source to stdout.\n");
    printf("  -config-dir <dir>      Configuration directory.\n");
    printf("\n");
    printf("Mode switching:\n");
    printf("  --wip                  Switch to web browsing mode (elinks).\n");
    printf("  --wip-opt              Use elinks-style options in ssh mode.\n");
}

/* 版本信息 */
static void version(void) {
    printf("%s %s\n", PROGRAM_NAME, VERSION);
    printf("libssh version: %s\n", ssh_version(0));
}

/* 解析主机字符串 [user@]host[:port] */
static int parse_host(const char *host_str, char **user, char **host, int *port) {
    char *str = strdup(host_str);
    char *at = strchr(str, '@');
    char *colon = strrchr(str, ':');
    
    *user = NULL;
    *host = NULL;
    *port = DEFAULT_SSH_PORT;
    
    if (at) {
        *at = '\0';
        *user = strdup(str);
        str = at + 1;
    }
    
    if (colon) {
        *colon = '\0';
        *port = atoi(colon + 1);
    }
    
    *host = strdup(str);
    free(str);
    return 0;
}

/* 执行 elinks */
static int exec_elinks(void) {
    char *args[256];
    int argc = 0;
    
    args[argc++] = "elinks";
    
    /* 构建 elinks 参数 */
    if (elinks_anonymous) {
        args[argc++] = "-anonymous";
        args[argc++] = "1";
    }
    if (elinks_auto_submit) {
        args[argc++] = "-auto-submit";
        args[argc++] = "1";
    }
    if (elinks_base_session) {
        args[argc++] = "-base-session";
        args[argc++] = elinks_base_session;
    }
    if (elinks_bookmark_add) {
        args[argc++] = "-bookmark-add";
        args[argc++] = elinks_bookmark_add;
    }
    if (elinks_bookmark_manager) {
        args[argc++] = "-bookmark-manager";
    }
    if (elinks_cmd_move) {
        args[argc++] = "-cmd-move";
        args[argc++] = "1";
    }
    if (elinks_cmd_scroll) {
        args[argc++] = "-cmd-scroll";
        args[argc++] = "1";
    }
    if (elinks_config_dir) {
        args[argc++] = "-config-dir";
        args[argc++] = elinks_config_dir;
    }
    if (elinks_default) {
        args[argc++] = "-default";
    }
    if (elinks_dump) {
        args[argc++] = "-dump";
        if (strcmp(elinks_dump, "1") != 0) {
            args[argc++] = elinks_dump;
        }
    }
    if (elinks_force_html) {
        args[argc++] = "-force-html";
    }
    if (elinks_g) {
        args[argc++] = "-g";
    }
    if (elinks_homepage) {
        args[argc++] = "-homepage";
        args[argc++] = elinks_homepage;
    }
    if (elinks_lookup) {
        args[argc++] = "-lookup";
    }
    if (elinks_no_connect) {
        args[argc++] = "-no-connect";
        args[argc++] = "1";
    }
    if (elinks_no_home) {
        args[argc++] = "-no-home";
        args[argc++] = "1";
    }
    if (elinks_no_numbering) {
        args[argc++] = "-no-numbering";
        args[argc++] = "1";
    }
    if (elinks_no_references) {
        args[argc++] = "-no-references";
        args[argc++] = "1";
    }
    if (elinks_remote) {
        args[argc++] = "-remote";
    }
    if (elinks_session_ring) {
        args[argc++] = "-session-ring";
        args[argc++] = elinks_session_ring;
    }
    if (elinks_source) {
        args[argc++] = "-source";
        if (strcmp(elinks_source, "1") != 0) {
            args[argc++] = elinks_source;
        }
    }
    if (elinks_stdin) {
        args[argc++] = "-stdin";
    }
    if (elinks_touch_files) {
        args[argc++] = "-touch-files";
        args[argc++] = "1";
    }
    if (elinks_verbose) {
        args[argc++] = "-verbose";
        char verbose_str[4];
        snprintf(verbose_str, sizeof(verbose_str), "%d", elinks_verbose);
        args[argc++] = verbose_str;
    }
    if (elinks_version) {
        args[argc++] = "-version";
    }
    
    /* 添加目标 URL */
    if (target) {
        args[argc++] = target;
    }
    
    args[argc] = NULL;
    
    execvp("elinks", args);
    error("Failed to execute elinks: %s", strerror(errno));
    return 1;
}

/* 验证 SSH 主机密钥 */
static int verify_knownhost(ssh_session session) {
    enum ssh_known_hosts_e state;
    char *hexa;
    ssh_key server_pubkey;
    int rc;
    
    rc = ssh_get_server_publickey(session, &server_pubkey);
    if (rc < 0) {
        return -1;
    }
    
    rc = ssh_get_publickey_hash(server_pubkey, SSH_PUBLICKEY_HASH_SHA256, &hexa, NULL);
    ssh_key_free(server_pubkey);
    if (rc < 0) {
        return -1;
    }
    
    state = ssh_session_is_known_server(session);
    
    switch (state) {
        case SSH_KNOWN_HOSTS_OK:
            /* OK */
            break;
        case SSH_KNOWN_HOSTS_NOT_FOUND:
        case SSH_KNOWN_HOSTS_UNKNOWN:
            hexa = ssh_get_hexa(hexa, 32);
            fprintf(stderr, "The authenticity of host '%s (%s)' can't be established.\n",
                    ssh_get_remote_hostname(session), ssh_get_remote_ipaddr(session));
            fprintf(stderr, "SHA256 key fingerprint is %s.\n", hexa);
            fprintf(stderr, "Are you sure you want to continue connecting (yes/no)? ");
            
            char response[10];
            if (fgets(response, sizeof(response), stdin) == NULL) {
                return -1;
            }
            if (strncmp(response, "yes", 3) != 0) {
                return -1;
            }
            
            rc = ssh_session_update_known_hosts(session);
            if (rc < 0) {
                error("Failed to add host to known hosts: %s", strerror(errno));
                return -1;
            }
            break;
        case SSH_KNOWN_HOSTS_CHANGED:
            error("WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!");
            return -1;
        case SSH_KNOWN_HOSTS_OTHER:
            error("WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!");
            return -1;
    }
    
    return 0;
}

/* 认证 */
static int authenticate(ssh_session session) {
    char *password;
    int rc;
    
    /* 尝试公钥认证 */
    rc = ssh_userauth_publickey_auto(session, NULL, NULL);
    if (rc == SSH_AUTH_SUCCESS) {
        return 0;
    }
    
    /* 尝试密码认证 */
    ssh_userauth_none(session, NULL);
    int method = ssh_userauth_list(session, NULL);
    
    if (method & SSH_AUTH_METHOD_PASSWORD) {
        printf("%s@%s's password: ", 
               ssh_user ? ssh_user : ssh_get_remote_username(session),
               ssh_get_remote_hostname(session));
        
        /* 禁用回显 */
        struct termios old_term, new_term;
        tcgetattr(STDIN_FILENO, &old_term);
        new_term = old_term;
        new_term.c_lflag &= ~ECHO;
        tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
        
        char passbuf[256];
        if (fgets(passbuf, sizeof(passbuf), stdin) == NULL) {
            tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
            return -1;
        }
        tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
        printf("\n");
        
        /* 移除换行符 */
        passbuf[strcspn(passbuf, "\n")] = '\0';
        
        rc = ssh_userauth_password(session, NULL, passbuf);
        memset(passbuf, 0, sizeof(passbuf));
        
        if (rc == SSH_AUTH_SUCCESS) {
            return 0;
        }
    }
    
    return -1;
}

/* 执行 SSH 会话 */
static int exec_ssh(void) {
    ssh_session my_ssh_session;
    int rc;
    int verbosity = SSH_LOG_NOLOG;
    
    if (ssh_verbose) {
        verbosity = SSH_LOG_FUNCTIONS;
    } else if (ssh_quiet) {
        verbosity = SSH_LOG_NOLOG;
    }
    
    /* 创建 SSH 会话 */
    my_ssh_session = ssh_new();
    if (my_ssh_session == NULL) {
        error("Failed to create SSH session");
        return -1;
    }
    
    /* 设置选项 */
    ssh_options_set(my_ssh_session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
    
    if (ssh_host) {
        ssh_options_set(my_ssh_session, SSH_OPTIONS_HOST, ssh_host);
    } else if (target) {
        char *user = NULL;
        char *host = NULL;
        int port = DEFAULT_SSH_PORT;
        
        parse_host(target, &user, &host, &port);
        
        if (user) {
            ssh_options_set(my_ssh_session, SSH_OPTIONS_USER, user);
            free(user);
        }
        ssh_options_set(my_ssh_session, SSH_OPTIONS_HOST, host);
        ssh_options_set(my_ssh_session, SSH_OPTIONS_PORT, &port);
        free(host);
    } else {
        error("No host specified");
        ssh_free(my_ssh_session);
        return -1;
    }
    
    if (ssh_user) {
        ssh_options_set(my_ssh_session, SSH_OPTIONS_USER, ssh_user);
    }
    
    int port = ssh_port;
    ssh_options_set(my_ssh_session, SSH_OPTIONS_PORT, &port);
    
    if (ssh_identity) {
        ssh_options_set(my_ssh_session, SSH_OPTIONS_IDENTITY, ssh_identity);
    }
    
    if (ssh_bind_address) {
        ssh_options_set(my_ssh_session, SSH_OPTIONS_BINDADDR, ssh_bind_address);
    }
    
    /* 连接 */
    rc = ssh_connect(my_ssh_session);
    if (rc != SSH_OK) {
        error("Error connecting to %s: %s", 
              ssh_get_remote_hostname(my_ssh_session), 
              ssh_get_error(my_ssh_session));
        ssh_free(my_ssh_session);
        return -1;
    }
    
    /* 验证主机密钥 */
    if (verify_knownhost(my_ssh_session) < 0) {
        ssh_disconnect(my_ssh_session);
        ssh_free(my_ssh_session);
        return -1;
    }
    
    /* 认证 */
    if (authenticate(my_ssh_session) < 0) {
        error("Authentication failed");
        ssh_disconnect(my_ssh_session);
        ssh_free(my_ssh_session);
        return -1;
    }
    
    /* 执行命令或启动 shell */
    ssh_channel channel = ssh_channel_new(my_ssh_session);
    if (channel == NULL) {
        error("Failed to create channel");
        ssh_disconnect(my_ssh_session);
        ssh_free(my_ssh_session);
        return -1;
    }
    
    rc = ssh_channel_open_session(channel);
    if (rc != SSH_OK) {
        ssh_channel_free(channel);
        ssh_disconnect(my_ssh_session);
        ssh_free(my_ssh_session);
        return -1;
    }
    
    if (ssh_command) {
        rc = ssh_channel_request_exec(channel, ssh_command);
    } else if (!ssh_noshell) {
        /* 请求伪终端 */
        rc = ssh_channel_request_pty(channel);
        if (rc == SSH_OK) {
            rc = ssh_channel_request_shell(channel);
        }
    }
    
    if (rc != SSH_OK) {
        ssh_channel_close(channel);
        ssh_channel_free(channel);
        ssh_disconnect(my_ssh_session);
        ssh_free(my_ssh_session);
        return -1;
    }
    
    /* 交互式会话循环 */
    if (!ssh_command) {
        struct termios old_term, new_term;
        tcgetattr(STDIN_FILENO, &old_term);
        new_term = old_term;
        new_term.c_lflag &= ~(ICANON | ECHO | ISIG);
        tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
        
        fd_set fds;
        char buffer[4096];
        int nbytes;
        
        while (1) {
            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds);
            FD_SET(ssh_get_fd(my_ssh_session), &fds);
            
            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 100000;
            
            rc = select(ssh_get_fd(my_ssh_session) + 1, &fds, NULL, NULL, &timeout);
            
            if (FD_ISSET(STDIN_FILENO, &fds)) {
                nbytes = read(STDIN_FILENO, buffer, sizeof(buffer));
                if (nbytes > 0) {
                    /* 检查转义字符 */
                    if (nbytes == 1 && buffer[0] == ssh_escape_char) {
                        /* 读取下一个字符 */
                        char next;
                        if (read(STDIN_FILENO, &next, 1) == 1) {
                            if (next == '.') {
                                printf("\nConnection closed.\n");
                                break;
                            } else if (next == ssh_escape_char) {
                                ssh_channel_write(channel, &ssh_escape_char, 1);
                            } else {
                                ssh_channel_write(channel, &ssh_escape_char, 1);
                                ssh_channel_write(channel, &next, 1);
                            }
                        }
                    } else {
                        ssh_channel_write(channel, buffer, nbytes);
                    }
                }
            }
            
            if (FD_ISSET(ssh_get_fd(my_ssh_session), &fds)) {
                nbytes = ssh_channel_read_nonblocking(channel, buffer, sizeof(buffer), 0);
                if (nbytes > 0) {
                    write(STDOUT_FILENO, buffer, nbytes);
                } else if (nbytes == SSH_ERROR) {
                    break;
                }
            }
            
            if (ssh_channel_is_eof(channel)) {
                break;
            }
        }
        
        tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    } else {
        /* 执行命令，读取输出 */
        char buffer[4096];
        int nbytes;
        
        while ((nbytes = ssh_channel_read(channel, buffer, sizeof(buffer), 0)) > 0) {
            write(STDOUT_FILENO, buffer, nbytes);
        }
    }
    
    /* 清理 */
    ssh_channel_send_eof(channel);
    ssh_channel_close(channel);
    ssh_channel_free(channel);
    ssh_disconnect(my_ssh_session);
    ssh_free(my_ssh_session);
    
    return 0;
}

/* 解析 elinks 风格的长选项 */
static int parse_elinks_opt(const char *opt, const char *arg) {
    if (strcmp(opt, "anonymous") == 0) {
        elinks_anonymous = arg ? atoi(arg) : 1;
    } else if (strcmp(opt, "auto-submit") == 0) {
        elinks_auto_submit = arg ? atoi(arg) : 1;
    } else if (strcmp(opt, "base-session") == 0) {
        elinks_base_session = (char *)arg;
    } else if (strcmp(opt, "bookmark-add") == 0) {
        elinks_bookmark_add = (char *)arg;
    } else if (strcmp(opt, "bookmark-manager") == 0) {
        elinks_bookmark_manager = (char *)arg ? (char *)arg : "1";
    } else if (strcmp(opt, "cmd-move") == 0) {
        elinks_cmd_move = arg ? atoi(arg) : 1;
    } else if (strcmp(opt, "cmd-scroll") == 0) {
        elinks_cmd_scroll = arg ? atoi(arg) : 1;
    } else if (strcmp(opt, "config-dir") == 0) {
        elinks_config_dir = (char *)arg;
    } else if (strcmp(opt, "default") == 0) {
        elinks_default = 1;
    } else if (strcmp(opt, "dump") == 0) {
        elinks_dump = arg ? (char *)arg : "1";
    } else if (strcmp(opt, "force-html") == 0) {
        elinks_force_html = 1;
    } else if (strcmp(opt, "homepage") == 0) {
        elinks_homepage = (char *)arg;
    } else if (strcmp(opt, "lookup") == 0) {
        elinks_lookup = 1;
    } else if (strcmp(opt, "no-connect") == 0) {
        elinks_no_connect = arg ? atoi(arg) : 1;
    } else if (strcmp(opt, "no-home") == 0) {
        elinks_no_home = arg ? atoi(arg) : 1;
    } else if (strcmp(opt, "no-numbering") == 0) {
        elinks_no_numbering = arg ? atoi(arg) : 1;
    } else if (strcmp(opt, "no-references") == 0) {
        elinks_no_references = arg ? atoi(arg) : 1;
    } else if (strcmp(opt, "remote") == 0) {
        elinks_remote = 1;
    } else if (strcmp(opt, "session-ring") == 0) {
        elinks_session_ring = (char *)arg;
    } else if (strcmp(opt, "source") == 0) {
        elinks_source = arg ? (char *)arg : "1";
    } else if (strcmp(opt, "stdin") == 0) {
        elinks_stdin = 1;
    } else if (strcmp(opt, "touch-files") == 0) {
        elinks_touch_files = arg ? atoi(arg) : 1;
    } else if (strcmp(opt, "verbose") == 0) {
        elinks_verbose = arg ? atoi(arg) : 1;
    } else if (strcmp(opt, "version") == 0) {
        elinks_version = 1;
    } else {
        return -1;
    }
    return 0;
}

/* 主函数 */
int main(int argc, char *argv[]) {
    int opt;
    int option_index = 0;
    
    /* 首先检查模式切换选项 */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--wip") == 0) {
            mode = MODE_SSH;
            /* 移除该参数 */
            for (int j = i; j < argc - 1; j++) {
                argv[j] = argv[j + 1];
            }
            argc--;
            i--;
        } else if (strcmp(argv[i], "--wip-opt") == 0) {
            opt_style = OPT_SSH;
            /* 移除该参数 */
            for (int j = i; j < argc - 1; j++) {
                argv[j] = argv[j + 1];
            }
            argc--;
            i--;
        }
    }
    
    /* 根据模式和选项风格解析参数 */
    if (mode == MODE_ELINKS) {
        if (opt_style == OPT_ELINKS) {
            /* elinks 风格选项 */
            static struct option long_options[] = {
                {"anonymous", optional_argument, 0, 0},
                {"auto-submit", optional_argument, 0, 0},
                {"base-session", required_argument, 0, 0},
                {"bookmark-add", required_argument, 0, 0},
                {"bookmark-manager", optional_argument, 0, 0},
                {"cmd-move", optional_argument, 0, 0},
                {"cmd-scroll", optional_argument, 0, 0},
                {"config-dir", required_argument, 0, 0},
                {"default", no_argument, 0, 0},
                {"dump", optional_argument, 0, 0},
                {"force-html", no_argument, 0, 0},
                {"homepage", required_argument, 0, 0},
                {"lookup", no_argument, 0, 0},
                {"no-connect", optional_argument, 0, 0},
                {"no-home", optional_argument, 0, 0},
                {"no-numbering", optional_argument, 0, 0},
                {"no-references", optional_argument, 0, 0},
                {"remote", no_argument, 0, 0},
                {"session-ring", required_argument, 0, 0},
                {"source", optional_argument, 0, 0},
                {"stdin", no_argument, 0, 0},
                {"touch-files", optional_argument, 0, 0},
                {"verbose", required_argument, 0, 0},
                {"version", no_argument, 0, 'V'},
                {"help", no_argument, 0, 'h'},
                {0, 0, 0, 0}
            };
            
            while ((opt = getopt_long(argc, argv, "gVh", long_options, &option_index)) != -1) {
                switch (opt) {
                    case 'V':
                        version();
                        return 0;
                    case 'h':
                        usage_elinks();
                        return 0;
                    case 'g':
                        elinks_g = 1;
                        break;
                    case 0:
                        if (parse_elinks_opt(long_options[option_index].name, 
                                            optarg) < 0) {
                            error("Unknown option: %s", long_options[option_index].name);
                            return 1;
                        }
                        break;
                    default:
                        return 1;
                }
            }
        } else {
            /* SSH 风格选项用于 elinks 模式 */
            while ((opt = getopt(argc, argv, "1246AaCfGgKkMNnqsTtVvXxYyb:c:D:e:F:i:J:L:l:m:O:o:p:R:S:W:w:")) != -1) {
                switch (opt) {
                    case 'l':
                    case 'u':
                        elinks_config_dir = optarg; /* 映射到 config-dir */
                        break;
                    case 'p':
                        /* 忽略端口 */
                        break;
                    case 'i':
                        /* 忽略身份文件 */
                        break;
                    case 'v':
                        elinks_verbose = 1;
                        break;
                    case 'q':
                        /* 安静模式 */
                        break;
                    case 'V':
                        version();
                        return 0;
                    case 'h':
                        usage_elinks();
                        return 0;
                    default:
                        /* 忽略其他 ssh 选项 */
                        break;
                }
            }
        }
    } else {
        /* SSH 模式 */
        if (opt_style == OPT_SSH) {
            /* 标准 SSH 风格选项 */
            while ((opt = getopt(argc, argv, "1246AaCfGgKkMNnqsTtVvXxYyb:c:D:e:F:i:J:L:l:m:O:o:p:R:S:W:w:")) != -1) {
                switch (opt) {
                    case '1':
                    case '2':
                        break;
                    case '4':
                        ssh_ipv4 = 1;
                        break;
                    case '6':
                        ssh_ipv6 = 1;
                        break;
                    case 'A':
                        ssh_forward_agent = 1;
                        break;
                    case 'a':
                        ssh_no_forward_agent = 1;
                        break;
                    case 'b':
                        ssh_bind_address = optarg;
                        break;
                    case 'c':
                        ssh_cipher = optarg;
                        break;
                    case 'C':
                        ssh_compression = 1;
                        break;
                    case 'D':
                        ssh_forward_port = optarg;
                        break;
                    case 'e':
                        if (optarg[0] == '^' && optarg[1]) {
                            ssh_escape_char = optarg[1] - '@';
                        } else if (optarg[0] == 'none') {
                            ssh_escape_char = -1;
                        } else {
                            ssh_escape_char = optarg[0];
                        }
                        break;
                    case 'F':
                        ssh_config = optarg;
                        break;
                    case 'f':
                        ssh_fork = 1;
                        break;
                    case 'g':
                        break;
                    case 'i':
                        ssh_identity = optarg;
                        break;
                    case 'J':
                        break;
                    case 'L':
                        ssh_forward_port = optarg;
                        break;
                    case 'l':
                        ssh_user = optarg;
                        break;
                    case 'M':
                        break;
                    case 'm':
                        break;
                    case 'N':
                        ssh_noshell = 1;
                        break;
                    case 'n':
                        break;
                    case 'O':
                        break;
                    case 'o':
                        ssh_option = optarg;
                        break;
                    case 'p':
                        ssh_port = atoi(optarg);
                        break;
                    case 'q':
                        ssh_quiet = 1;
                        break;
                    case 'R':
                        ssh_remote_forward = optarg;
                        break;
                    case 'S':
                        break;
                    case 's':
                        break;
                    case 'T':
                        break;
                    case 't':
                        break;
                    case 'V':
                        version();
                        return 0;
                    case 'v':
                        ssh_verbose++;
                        break;
                    case 'W':
                        break;
                    case 'w':
                        break;
                    case 'X':
                        ssh_X11_forward = 1;
                        break;
                    case 'x':
                        ssh_no_X11_forward = 1;
                        break;
                    case 'Y':
                        ssh_X11_forward = 1;
                        break;
                    case 'y':
                        break;
                    default:
                        usage_ssh();
                        return 1;
                }
            }
        } else {
            /* elinks 风格选项用于 SSH 模式 */
            static struct option long_options[] = {
                {"anonymous", optional_argument, 0, 0},
                {"config-dir", required_argument, 0, 0},
                {"dump", optional_argument, 0, 0},
                {"verbose", required_argument, 0, 0},
                {"version", no_argument, 0, 'V'},
                {"help", no_argument, 0, 'h'},
                {0, 0, 0, 0}
            };
            
            while ((opt = getopt_long(argc, argv, "Vh", long_options, &option_index)) != -1) {
                switch (opt) {
                    case 'V':
                        version();
                        return 0;
                    case 'h':
                        usage_ssh();
                        return 0;
                    case 0:
                        if (strcmp(long_options[option_index].name, "config-dir") == 0) {
                            ssh_config = optarg;
                        } else if (strcmp(long_options[option_index].name, "verbose") == 0) {
                            ssh_verbose = optarg ? atoi(optarg) : 1;
                        }
                        break;
                    default:
                        return 1;
                }
            }
        }
    }
    
    /* 获取目标 */
    if (optind < argc) {
        target = argv[optind];
    }
    
    /* 执行相应模式 */
    if (mode == MODE_ELINKS) {
        return exec_elinks();
    } else {
        return exec_ssh();
    }
}
