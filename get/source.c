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

#define PROGRAM_NAME "get"
#define VERSION "1.0"

/* 运行模式 */
typedef enum {
    MODE_WGET,      /* 默认：wget 风格 */
    MODE_CURL       /* --wip：curl 风格 */
} run_mode_t;

static run_mode_t mode = MODE_WGET;

/* wget 风格选项 */
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

/* 错误处理 */
static void error(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    fprintf(stderr, "%s: ", PROGRAM_NAME);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

/* 显示 wget 风格帮助 */
static void usage_wget(void) {
    printf("Usage: %s [OPTION]... [URL]...\n\n", PROGRAM_NAME);
    printf("Mandatory arguments to long options are mandatory for short options too.\n\n");
    printf("Startup:\n");
    printf("  -V,  --version           display the version of Wget and exit\n");
    printf("  -h,  --help              print this help\n");
    printf("  -b,  --background        go to background after startup\n");
    printf("  -e,  --execute=COMMAND   execute a `.wgetrc'-style command\n\n");
    printf("Logging and input file:\n");
    printf("  -o,  --output-file=FILE    log messages to FILE\n");
    printf("  -a,  --append-output=FILE  append messages to FILE\n");
    printf("  -d,  --debug               print lots of debugging information\n");
    printf("  -q,  --quiet               quiet (no output)\n");
    printf("  -v,  --verbose             be verbose (this is the default)\n");
    printf("  -nv, --no-verbose          turn off verboseness, without being quiet\n");
    printf("       --report-speed=TYPE   Output bandwidth as TYPE.  TYPE can be bits\n");
    printf("  -i,  --input-file=FILE     download URLs found in local or external FILE\n");
    printf("       --input-encoding=ENC  use encoding ENC for input files\n");
    printf("  -F,  --force-html          treat input file as HTML\n");
    printf("  -B,  --base=URL            resolves HTML input-file links (-i -F)\n");
    printf("       --config=FILE         specify config file to use\n");
    printf("       --no-config           do not read any config file\n");
    printf("       --rejected-log=FILE   log reasons for URL rejection to FILE\n\n");
    printf("Download:\n");
    printf("  -t,  --tries=NUMBER            set number of retries to NUMBER (0 unlimits)\n");
    printf("       --retry-connrefused       retry even if connection is refused\n");
    printf("  -O,  --output-document=FILE    write documents to FILE\n");
    printf("  -nc, --no-clobber              skip downloads that would download to\n");
    printf("                                   existing files (overwriting them)\n");
    printf("  -c,  --continue                resume getting a partially-downloaded file\n");
    printf("       --progress=TYPE           select progress gauge type\n");
    printf("  -N,  --timestamping            don't re-retrieve files unless newer than\n");
    printf("                                   local\n");
    printf("       --no-use-server-timestamps  don't set the local file's timestamp by\n");
    printf("                                   the one on the server\n");
    printf("  -S,  --server-response         print server response\n");
    printf("       --spider                  don't download anything\n");
    printf("  -T,  --timeout=SECONDS         set all timeout values to SECONDS\n");
    printf("       --dns-timeout=SECS        set the DNS lookup timeout to SECS\n");
    printf("       --connect-timeout=SECS    set the connect timeout to SECS\n");
    printf("       --read-timeout=SECS       set the read timeout to SECS\n");
    printf("  -w,  --wait=SECONDS            wait SECONDS between retrievals\n");
    printf("       --waitretry=SECONDS       wait 1..SECONDS between retries of a retrieval\n");
    printf("       --random-wait             wait from 0.5*WAIT...1.5*WAIT secs between retrievals\n");
    printf("       --no-proxy                explicitly turn off proxy\n");
    printf("  -Q,  --quota=NUMBER            set retrieval quota to NUMBER\n");
    printf("       --bind-address=ADDRESS    bind to ADDRESS (hostname or IP) on local host\n");
    printf("       --limit-rate=RATE         limit download rate to RATE\n");
    printf("       --no-dns-cache            disable caching DNS lookups\n");
    printf("       --restrict-file-names=OS  restrict chars in file names to ones OS allows\n");
    printf("       --ignore-case             ignore case when matching files/directories\n");
    printf("  -4,  --inet4-only              connect only to IPv4 addresses\n");
    printf("  -6,  --inet6-only              connect only to IPv6 addresses\n");
    printf("       --prefer-family=FAMILY    connect first to addresses of specified family,\n");
    printf("                                   one of IPv6, IPv4, or none\n");
    printf("       --user=USER               set both ftp and http user to USER\n");
    printf("       --password=PASS           set both ftp and http password to PASS\n");
    printf("       --ask-password            prompt for passwords\n");
    printf("       --no-iri                  turn off IRI support\n");
    printf("       --local-encoding=ENC      use ENC as the local encoding for IRIs\n");
    printf("       --remote-encoding=ENC     use ENC as the default remote encoding\n");
    printf("       --unlink                  remove file before clobber\n\n");
    printf("Directories:\n");
    printf("  -nd, --no-directories           don't create directories\n");
    printf("  -x,  --force-directories        force creation of directories\n");
    printf("  -nH, --no-host-directories      don't create host directories\n");
    printf("       --protocol-directories     use protocol name in directories\n");
    printf("  -P,  --directory-prefix=PREFIX  save files to PREFIX/...\n");
    printf("       --cut-dirs=NUMBER          ignore NUMBER remote directory components\n\n");
    printf("HTTP options:\n");
    printf("       --http-user=USER        set http user to USER\n");
    printf("       --http-password=PASS    set http password to PASS\n");
    printf("       --no-cache              disallow server-cached data\n");
    printf("       --default-page=NAME     change the default page name (normally\n");
    printf("                                 this is 'index.html'.)\n");
    printf("  -E,  --adjust-extension      save HTML/CSS documents with proper extensions\n");
    printf("       --ignore-length         ignore 'Content-Length' header field\n");
    printf("       --header=STRING         insert STRING among the headers\n");
    printf("       --max-redirect          maximum redirections allowed per page\n");
    printf("       --proxy-user=USER       set USER as proxy username\n");
    printf("       --proxy-password=PASS   set PASS as proxy password\n");
    printf("       --referer=URL           include 'Referer: URL' header in HTTP request\n");
    printf("       --save-headers          save the HTTP headers to file\n");
    printf("  -U,  --user-agent=AGENT      identify as AGENT instead of Wget/VERSION\n");
    printf("       --no-http-keep-alive    disable HTTP keep-alive (persistent connections)\n");
    printf("       --no-cookies            don't use cookies\n");
    printf("       --load-cookies=FILE     load cookies from FILE before session\n");
    printf("       --save-cookies=FILE     save cookies to FILE after session\n");
    printf("       --keep-session-cookies  load and save session (non-permanent) cookies\n");
    printf("       --post-data=STRING      use the POST method; send STRING as the data\n");
    printf("       --post-file=FILE        use the POST method; send contents of FILE\n");
    printf("       --content-disposition   honor the Content-Disposition header when\n");
    printf("                                 choosing local file names (EXPERIMENTAL)\n");
    printf("       --content-on-error      output the received content on server errors\n");
    printf("       --auth-no-challenge     send Basic HTTP authentication information\n");
    printf("                                 without first waiting for the server's\n");
    printf("                                 challenge\n\n");
    printf("HTTPS (SSL/TLS) options:\n");
    printf("       --secure-protocol=PR     choose secure protocol, one of auto, SSLv2,\n");
    printf("                                  SSLv3, TLSv1, TLSv1_1 and TLSv1_2\n");
    printf("       --no-check-certificate   don't validate the server's certificate\n");
    printf("       --certificate=FILE       client certificate file\n");
    printf("       --certificate-type=TYPE  client certificate type, PEM or DER\n");
    printf("       --private-key=FILE       private key file\n");
    printf("       --private-key-type=TYPE  private key type, PEM or DER\n");
    printf("       --ca-certificate=FILE    file with the bundle of CA's\n");
    printf("       --ca-directory=DIR       directory where hash list of CA's is stored\n");
    printf("       --random-file=FILE       file with random data for seeding the SSL PRNG\n");
    printf("       --egd-file=FILE          file naming the EGD socket with random data\n\n");
    printf("FTP options:\n");
    printf("       --ftp-user=USER         set ftp user to USER\n");
    printf("       --ftp-password=PASS     set ftp password to PASS\n");
    printf("       --no-remove-listing     don't remove '.listing' files\n");
    printf("       --no-glob               turn off FTP globbing\n");
    printf("       --no-passive-ftp        disable the \"passive\" transfer mode\n");
    printf("       --preserve-permissions  preserve remote file permissions\n");
    printf("       --retr-symlinks         when recursing, get linked-to files (not dir)\n\n");
    printf("WARC options:\n");
    printf("       --warc-file=FILENAME      save request/response data to a .warc.gz file\n");
    printf("       --warc-header=STRING      insert STRING into the warcinfo record\n");
    printf("       --warc-max-size=NUMBER    set maximum size of WARC files to NUMBER\n");
    printf("       --warc-cdx                write CDX index files\n");
    printf("       --warc-dedup=FILENAME     do not store records listed in this CDX file\n");
    printf("       --no-warc-compression     do not gzip WARC files\n");
    printf("       --no-warc-digests         do not calculate SHA1 digests\n");
    printf("       --warc-prefix=PREFIX      set WARC filename prefix to PREFIX\n");
    printf("       --warc-tempdir=DIRECTORY  location for temporary files created by the\n");
    printf("                                   WARC writer\n\n");
    printf("Recursive download:\n");
    printf("  -r,  --recursive          specify recursive download\n");
    printf("  -l,  --level=NUMBER       maximum recursion depth (inf or 0 for infinite)\n");
    printf("       --delete-after       delete files locally after downloading them\n");
    printf("  -k,  --convert-links      make links in downloaded HTML or CSS point to\n");
    printf("                              local files\n");
    printf("       --backups=N          before writing file X, rotate up to N backup files\n");
    printf("  -K,  --backup-converted   before converting file X, back up as X.orig\n");
    printf("  -m,  --mirror             shortcut for -N -r -l inf --no-remove-listing\n");
    printf("  -p,  --page-requisites    get all images, etc. needed to display HTML page\n");
    printf("       --strict-comments    turn on strict (SGML) handling of HTML comments\n\n");
    printf("Recursive accept/reject:\n");
    printf("  -A,  --accept=LIST               comma-separated list of accepted extensions\n");
    printf("  -R,  --reject=LIST               comma-separated list of rejected extensions\n");
    printf("       --accept-regex=REGEX        regex matching accepted URLs\n");
    printf("       --reject-regex=REGEX        regex matching rejected URLs\n");
    printf("       --regex-type=TYPE           regex type (posix|pcre)\n");
    printf("  -D,  --domains=LIST              comma-separated list of accepted domains\n");
    printf("       --exclude-domains=LIST      comma-separated list of rejected domains\n");
    printf("       --follow-ftp                follow FTP links from HTML documents\n");
    printf("       --follow-tags=LIST          comma-separated list of followed HTML tags\n");
    printf("       --ignore-tags=LIST          comma-separated list of ignored HTML tags\n");
    printf("  -H,  --span-hosts                go to foreign hosts when recursive\n");
    printf("  -L,  --relative                  follow relative links only\n");
    printf("  -I,  --include-directories=LIST  list of allowed directories\n");
    printf("       --trust-server-names        use the name specified by the redirection\n");
    printf("                                     URL's last component\n");
    printf("  -X,  --exclude-directories=LIST  list of excluded directories\n");
    printf("  -np, --no-parent                 don't ascend to the parent directory\n\n");
    printf("Mail bug reports and suggestions to <bug-wget@gnu.org>\n");
}

/* 显示 curl 风格帮助 */
static void usage_curl(void) {
    printf("Usage: %s [options...] <url>\n", PROGRAM_NAME);
    printf("     %s [options...] [URL...]\n", PROGRAM_NAME);
    printf("     %s [options...] [URL] [URL...]\n\n", PROGRAM_NAME);
    printf("     --wip  (switch to curl mode)\n\n");
    printf("Options: (H) means HTTP/HTTPS only, (F) means FTP only\n\n");
    printf("     --abstract-unix-socket <path> Connect via abstract Unix domain socket\n");
    printf("     --anyauth       Pick \"any\" authentication method (H)\n");
    printf(" -a, --append        Append to target file when uploading (F/SFTP)\n");
    printf("     --basic         Use HTTP Basic Authentication (H)\n");
    printf("     --cacert <file> CA certificate to verify peer against (SSL)\n");
    printf("     --capath <dir>  CA directory to verify peer against (SSL)\n");
    printf(" -E, --cert <cert[:passwd]> Client certificate file and password (SSL)\n");
    printf("     --cert-status   Verify the status of the server certificate (SSL)\n");
    printf("     --cert-type <type> Certificate file type (DER/PEM/ENG) (SSL)\n");
    printf("     --ciphers <list of ciphers> SSL ciphers to use (SSL)\n");
    printf("     --compressed    Request compressed response (using deflate or gzip)\n");
    printf(" -K, --config <file> Read config from a file\n");
    printf("     --connect-timeout <seconds> Maximum time allowed for connection\n");
    printf("     --connect-to <HOST1:PORT1:HOST2:PORT2> For request to HOST1:PORT1 connect to HOST2:PORT2 instead\n");
    printf(" -C, --continue-at <offset> Resumed transfer offset\n");
    printf(" -b, --cookie <data> Send cookies from string/file (H)\n");
    printf(" -c, --cookie-jar <file> Write cookies to this file after operation (H)\n");
    printf("     --create-dirs   Create necessary local directory hierarchy\n");
    printf("     --crlf          Convert LF to CRLF in upload\n");
    printf("     --crlfile <file> Get a CRL list in PEM format from the given file\n");
    printf(" -d, --data <data>   HTTP POST data (H)\n");
    printf("     --data-ascii <data> HTTP POST ASCII data (H)\n");
    printf("     --data-binary <data> HTTP POST binary data (H)\n");
    printf("     --data-raw <data> HTTP POST data, '@' allowed (H)\n");
    printf("     --data-urlencode <data> HTTP POST data url encoded (H)\n");
    printf("     --delegation <LEVEL> GSS-API delegation permission\n");
    printf("     --digest        Use HTTP Digest Authentication (H)\n");
    printf("     --disable-eprt  Inhibit using EPRT or LPRT (F)\n");
    printf("     --disable-epsv  Inhibit using EPSV (F)\n");
    printf(" -D, --dump-header <file> Write the headers to this file\n");
    printf("     --egd-file <file> EGD socket path for random data (SSL)\n");
    printf("     --engine <name> Crypto engine (use \"--engine list\" for list) (SSL)\n");
    printf("     --expect100-timeout <seconds> How long to wait for 100-continue (H)\n");
    printf(" -f, --fail          Fail silently (no output at all) on HTTP errors (H)\n");
    printf("     --fail-early    Fail on first transfer error, do not continue\n");
    printf("     --false-start   Enable TLS False Start\n");
    printf(" -F, --form <name=content> Specify HTTP multipart POST data (H)\n");
    printf("     --form-string <name=string> Specify HTTP multipart POST data (H)\n");
    printf("     --ftp-account <data> Account data to send when requested by server (F)\n");
    printf("     --ftp-alternative-to-user <cmd> String to replace \"USER [name]\" (F)\n");
    printf("     --ftp-create-dirs Create the remote dirs if not present (F)\n");
    printf("     --ftp-method <method> Control CWD usage (F)\n");
    printf("     --ftp-pasv      Use PASV/EPSV instead of PORT (F)\n");
    printf(" -P, --ftp-port <address> Use PORT with address instead of PASV (F)\n");
    printf("     --ftp-skip-pasv-ip Skip the IP address for PASV (F)\n");
    printf("     --ftp-pret      Send PRET before PASV (for drftpd) (F)\n");
    printf("     --ftp-ssl-ccc   Send CCC after authenticating (F)\n");
    printf("     --ftp-ssl-ccc-mode <active/passive> Set CCC mode (F)\n");
    printf("     --ftp-ssl-control Require SSL/TLS for FTP login, clear for transfer (F)\n");
    printf(" -G, --get           Send the -d data with a HTTP GET (H)\n");
    printf(" -g, --globoff       Disable URL sequences and ranges using {} and []\n");
    printf(" -H, --header <line> Custom header to pass to server (H)\n");
    printf(" -I, --head          Show document info only\n");
    printf(" -h, --help          This help text\n");
    printf("     --hostpubmd5 <md5> Hex-encoded MD5 string of the host public key. (SSH)\n");
    printf(" -0, --http1.0       Use HTTP 1.0 (H)\n");
    printf("     --http1.1       Use HTTP 1.1 (H)\n");
    printf("     --http2         Use HTTP 2 (H)\n");
    printf("     --ignore-content-length  Ignore the HTTP Content-Length header\n");
    printf(" -i, --include       Include the HTTP-header in the output (H)\n");
    printf(" -k, --insecure      Allow connections to SSL sites without certs (H)\n");
    printf("     --interface <name> Use network INTERFACE (or address)\n");
    printf(" -4, --ipv4          Resolve name to IPv4 address\n");
    printf(" -6, --ipv6          Resolve name to IPv6 address\n");
    printf(" -j, --junk-session-cookies Ignore session cookies read from file (H)\n");
    printf("     --keepalive-time <seconds> Interval time for keepalive probes\n");
    printf("     --key <key>     Private key file name (SSL/SSH)\n");
    printf("     --key-type <type> Private key file type (DER/PEM/ENG) (SSL)\n");
    printf("     --krb <level>   Enable Kerberos with security <level> (F)\n");
    printf("     --libcurl <file> Dump libcurl equivalent code of this command line\n");
    printf("     --limit-rate <speed> Limit transfer speed to RATE\n");
    printf(" -l, --list-only     List only mode (F/POP3)\n");
    printf("     --local-port <num>[-num] Force use of RANGE of local port numbers\n");
    printf(" -L, --location      Follow redirects (H)\n");
    printf("     --location-trusted Like --location, and send auth to other hosts (H)\n");
    printf("     --login-options <options> Server login options (IMAP, POP3, SMTP)\n");
    printf("     --mail-auth <address> Originator address of the original email\n");
    printf("     --mail-from <address> Mail from this address\n");
    printf("     --mail-rcpt <address> Mail from this address\n");
    printf(" -M, --manual        Display the full manual\n");
    printf("     --max-filesize <bytes> Maximum file size to download (H/F)\n");
    printf("     --max-redirs <num> Maximum number of redirects allowed (H)\n");
    printf(" -m, --max-time <seconds> Maximum time allowed for the transfer\n");
    printf("     --metalink      Process given URLs as metalinks\n");
    printf("     --negotiate     Use HTTP Negotiate (SPNEGO) authentication (H)\n");
    printf(" -n, --netrc         Must read .netrc for user name and password\n");
    printf("     --netrc-optional Use either .netrc or URL; overrides -n\n");
    printf("     --netrc-file <filename> Specify FILE for netrc\n");
    printf(" -N, --no-buffer     Disable buffering of the output stream\n");
    printf("     --no-keepalive  Disable use of keepalive messages on that connection\n");
    printf("     --no-sessionid  Disable SSL session-ID reusing (SSL)\n");
    printf("     --noproxy       List of hosts which do not use proxy\n");
    printf("     --ntlm          Use HTTP NTLM authentication (H)\n");
    printf("     --ntlm-wb       Use HTTP NTLM authentication with winbind (H)\n");
    printf("     --oauth2-bearer <token> OAuth 2 Bearer Token (IMAP, POP3, SMTP)\n");
    printf(" -o, --output <file> Write to file instead of stdout\n");
    printf("     --pass <phrase> Pass phrase for the private key (SSL/SSH)\n");
    printf("     --path-as-is    Do not squash .. sequences in URL path\n");
    printf("     --pinnedpubkey <hashes> FILE/HASHES Public key to verify peer against (SSL)\n");
    printf("     --post301       Do not switch to GET after following a 301 redirect (H)\n");
    printf("     --post302       Do not switch to GET after following a 302 redirect (H)\n");
    printf("     --post303       Do not switch to GET after following a 303 redirect (H)\n");
    printf("     --preproxy [protocol://]host[:port] Use this proxy first\n");
    printf(" -#, --progress-bar  Display transfer progress as a progress bar\n");
    printf("     --proto <protocols> Enable/disable PROTOCOLS\n");
    printf("     --proto-default <protocol> Use PROTOCOL for any URL missing a scheme\n");
    printf("     --proto-redir <protocols> Enable/disable PROTOCOLS on redirect\n");
    printf(" -x, --proxy [protocol://]host[:port] Use proxy on given port\n");
    printf("     --proxy-anyauth Pick \"any\" proxy authentication method (H)\n");
    printf("     --proxy-basic   Use Basic authentication on the proxy (H)\n");
    printf("     --proxy-cacert <file> CA certificate to verify peer against for proxy connection (SSL)\n");
    printf("     --proxy-capath <dir> CA directory to verify peer against for proxy connection (SSL)\n");
    printf("     --proxy-cert <cert[:passwd]> Client certificate file and password for proxy connection (SSL)\n");
    printf("     --proxy-cert-type <type> Certificate file type (DER/PEM/ENG) for proxy connection (SSL)\n");
    printf("     --proxy-ciphers <list> SSL ciphers to use for proxy connection (SSL)\n");
    printf("     --proxy-crlfile <file> Set a CRL list for proxy connection (SSL)\n");
    printf("     --proxy-digest  Use Digest authentication on the proxy (H)\n");
    printf("     --proxy-header <line> Custom header to pass to proxy (H)\n");
    printf("     --proxy-insecure Allow \"insecure\" SSL connections to proxy (H)\n");
    printf("     --proxy-key <key> Private key file name for proxy connection (SSL)\n");
    printf("     --proxy-key-type <type> Private key file type for proxy connection (SSL)\n");
    printf("     --proxy-negotiate Use HTTP Negotiate (SPNEGO) authentication on the proxy (H)\n");
    printf("     --proxy-ntlm    Use NTLM authentication on the proxy (H)\n");
    printf("     --proxy-pass <phrase> Pass phrase for the private key for proxy connection (SSL)\n");
    printf("     --proxy-service-name <name> SPNEGO proxy service name\n");
    printf("     --proxy-ssl-allow-beast Allow security flaw to improve interop for proxy connection (SSL)\n");
    printf("     --proxy-tlsauthtype <type> TLS authentication type for proxy connection (SSL)\n");
    printf("     --proxy-tlspassword <string> TLS password for proxy connection (SSL)\n");
    printf("     --proxy-tlsuser <name> TLS username for proxy connection (SSL)\n");
    printf("     --proxy-tlsv1   Use TLSv1 for proxy connection (SSL)\n");
    printf(" -U, --proxy-user <user[:password]> Proxy user and password\n");
    printf("     --proxy1.0 <host[:port]> Use HTTP/1.0 proxy on given port\n");
    printf(" -p, --proxytunnel   Operate through an HTTP proxy tunnel (using CONNECT)\n");
    printf("     --pubkey <key>  Public key file name (SSH)\n");
    printf(" -Q, --quote <cmd>   Send command(s) to server before transfer (F/SFTP)\n");
    printf("     --random-file <file> File for reading random data from (SSL)\n");
    printf(" -r, --range <range> Retrieve only the bytes within RANGE\n");
    printf("     --raw           Do HTTP \"raw\"; no transfer decoding (H)\n");
    printf(" -e, --referer <URL> Referer URL (H)\n");
    printf(" -J, --remote-header-name Use the header-provided filename (H)\n");
    printf(" -O, --remote-name   Write output to a file named as the remote file\n");
    printf("     --remote-name-all Use the remote file name for all URLs\n");
    printf(" -R, --remote-time   Set the remote file's time on the local output\n");
    printf(" -X, --request <command> Specify request command to use\n");
    printf("     --resolve <host:port:address> Resolve the host+port to this address\n");
    printf("     --retry <num>   Retry request <num> times if transient problems occur\n");
    printf("     --retry-connrefused Retry on connection refused (use with --retry)\n");
    printf("     --retry-delay <seconds> Wait time between retries\n");
    printf("     --retry-max-time <seconds> Retry only within this period\n");
    printf(" -S, --show-error    Show error. With -s, make curl show errors when they occur\n");
    printf(" -s, --silent        Silent mode (don't output anything)\n");
    printf("     --socks4 <host[:port]> SOCKS4 proxy on given host + port\n");
    printf("     --socks4a <host[:port]> SOCKS4a proxy on given host + port\n");
    printf("     --socks5 <host[:port]> SOCKS5 proxy on given host + port\n");
    printf("     --socks5-hostname <host[:port]> SOCKS5 proxy, pass host name to proxy\n");
    printf("     --socks5-gssapi-nec Compatibility with NEC SOCKS5 server\n");
    printf("     --socks5-gssapi-service <name> SOCKS5 proxy service name for gssapi\n");
    printf(" -Y, --speed-limit <speed> Stop transfers below speed-limit for 'speed-time' secs\n");
    printf(" -y, --speed-time <seconds> Time for trig speed-limit abort. Defaults to 30\n");
    printf("     --ssl           Try SSL/TLS (FTP, IMAP, POP3, SMTP)\n");
    printf("     --ssl-allow-beast Allow security flaw to improve interop (SSL)\n");
    printf("     --ssl-no-revoke Disable cert revocation checks (WinSSL)\n");
    printf("     --ssl-reqd      Require SSL/TLS (FTP, IMAP, POP3, SMTP)\n");
    printf(" -2, --sslv2         Use SSLv2 (SSL)\n");
    printf(" -3, --sslv3         Use SSLv3 (SSL)\n");
    printf("     --ssl-engines   List available SSL engines\n");
    printf("     --stderr        Where to redirect stderr (use \"-\" for stdout)\n");
    printf("     --tcp-nodelay   Use the TCP_NODELAY option\n");
    printf(" -t, --telnet-option <OPT=val> Set telnet option\n");
    printf("     --tftp-blksize <value> Set TFTP BLKSIZE option\n");
    printf(" -z, --time-cond <time> Transfer based on a time condition\n");
    printf(" -1, --tlsv1         Use TLSv1 (SSL)\n");
    printf("     --tlsv1.0       Use TLSv1.0 (SSL)\n");
    printf("     --tlsv1.1       Use TLSv1.1 (SSL)\n");
    printf("     --tlsv1.2       Use TLSv1.2 (SSL)\n");
    printf("     --tlsv1.3       Use TLSv1.3 (SSL)\n");
    printf("     --trace <file>  Write a debug trace to FILE\n");
    printf("     --trace-ascii <file> Like --trace, but without hex output\n");
    printf("     --trace-time    Add time stamps to trace/verbose output\n");
    printf("     --tr-encoding   Request compressed transfer encoding (H)\n");
    printf(" -T, --upload-file <file> Transfer FILE to destination\n");
    printf("     --url <url>     URL to work with\n");
    printf(" -B, --use-ascii     Use ASCII/text transfer\n");
    printf(" -u, --user <user[:password]> Server user and password\n");
    printf("     --tlsuser <name> TLS username\n");
    printf("     --tlspassword <string> TLS password\n");
    printf("     --tlsauthtype <type> TLS authentication type\n");
    printf(" -A, --user-agent <name> Send User-Agent <name> to server (H)\n");
    printf(" -v, --verbose       Make the operation more talkative\n");
    printf(" -V, --version       Show version number and quit\n");
    printf(" -w, --write-out <format> Use output FORMAT after completion\n");
    printf("     --xattr         Store metadata in extended file attributes\n");
    printf(" -q                  If used as the first parameter disables .curlrc\n");
}

/* 显示版本 */
static void version(void) {
    printf("%s %s (libcurl/%s)\n", PROGRAM_NAME, VERSION, curl_version());
}

/* 进度回调 */
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
            /* curl 风格 */
            printf("\r%3.0f%%  %ld  %ld  %.2f", percent, (long)dlnow, (long)dltotal, speed);
            fflush(stdout);
        }
    }
    return 0;
}

/* 写入回调 */
static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    FILE *fp = (FILE *)userdata;
    if (fp) {
        return fwrite(ptr, size, nmemb, fp);
    }
    return fwrite(ptr, size, nmemb, stdout);
}

/* 头部回调 */
static size_t header_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    if (curl_include_header || (mode == MODE_WGET && verbose)) {
        return fwrite(ptr, size, nmemb, stdout);
    }
    return size * nmemb;
}

/* 获取文件名 */
static char* get_filename(const char *url) {
    const char *last_slash = strrchr(url, '/');
    if (last_slash && *(last_slash + 1)) {
        return strdup(last_slash + 1);
    }
    return strdup("index.html");
}

/* 下载文件 */
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
    
    /* 设置 URL */
    curl_easy_setopt(curl, CURLOPT_URL, url);
    
    /* 设置跟随重定向 */
    if (mode == MODE_WGET || curl_location) {
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    }
    
    /* 设置 User-Agent */
    if (user_agent) {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
    } else if (mode == MODE_WGET) {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Wget/1.20.3 (linux-gnu)");
    } else {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl/7.68.0");
    }
    
    /* 设置超时 */
    if (timeout > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)timeout);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)timeout);
    }
    
    /* SSL 设置 */
    if (no_check_certificate || (mode == MODE_CURL && curl_insecure)) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    
    /* 设置认证 */
    if (http_user && http_passwd) {
        char auth[512];
        snprintf(auth, sizeof(auth), "%s:%s", http_user, http_passwd);
        curl_easy_setopt(curl, CURLOPT_USERPWD, auth);
    } else if (curl_user) {
        curl_easy_setopt(curl, CURLOPT_USERPWD, curl_user);
    }
    
    /* POST 数据 */
    if (post_data || curl_data) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data ? post_data : curl_data);
    }
    
    /* 自定义头部 */
    if (header) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, header);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    
    /* 绑定地址 */
    if (bind_address) {
        curl_easy_setopt(curl, CURLOPT_INTERFACE, bind_address);
    }
    
    /* 限速 */
    if (limit_rate > 0) {
        curl_easy_setopt(curl, CURLOPT_MAX_RECV_SPEED_LARGE, (curl_off_t)limit_rate);
    }
    
    /* 确定输出文件名 */
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
    } else { /* MODE_CURL */
        if (curl_output) {
            filename = strdup(curl_output);
        } else if (curl_remote_name) {
            filename = get_filename(url);
        } else {
            /* curl 默认输出到 stdout */
            filename = NULL;
        }
    }
    
    /* Spider 模式 */
    if (spider) {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(curl, CURLOPT_HEADER, 1L);
    }
    
    /* 只获取头部 */
    if (curl_head_only) {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    }
    
    /* 打开输出文件 */
    if (filename && !spider && !curl_head_only) {
        /* 检查是否继续下载 */
        if (continue_download && stat(filename, &st) == 0) {
            resume_from = st.st_size;
            curl_easy_setopt(curl, CURLOPT_RESUME_FROM, resume_from);
        }
        
        /* 检查是否不覆盖 */
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
    
    /* 设置回调 */
    if (fp || spider || curl_head_only) {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp ? (void *)fp : (void *)stdout);
    }
    
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    
    /* 进度条 */
    if (show_progress && !quiet && !curl_silent) {
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    } else {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    }
    
    /* 执行 */
    start_time = time(NULL);
    res = curl_easy_perform(curl);
    
    if (show_progress && !quiet && !curl_silent && fp) {
        printf("\n");
    }
    
    /* 检查结果 */
    if (res != CURLE_OK) {
        if (!curl_fail_silently || curl_show_error) {
            error("%s", curl_easy_strerror(res));
        }
        if (fp) fclose(fp);
        curl_easy_cleanup(curl);
        free(filename);
        return 1;
    }
    
    /* 获取 HTTP 状态码 */
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    if (spider) {
        printf("Spider mode enabled. Check: %ld %s\n", http_code, 
               (http_code == 200) ? "OK" : "FAILED");
    }
    
    /* 清理 */
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
        /* wget 长选项 */
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
        
        /* curl 长选项 */
        {"wip", no_argument, 0, 0},  /* 切换到 curl 模式 */
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
        
        /* 通用 */
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'V'},
        {0, 0, 0, 0}
    };
    
    /* 首先检查 --wip 模式（必须在最前面） */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--wip") == 0) {
            mode = MODE_CURL;
            /* 移除 --wip 参数 */
            for (int j = i; j < argc - 1; j++) {
                argv[j] = argv[j + 1];
            }
            argc--;
            break;
        }
    }
    
    /* 解析选项 */
    while ((opt = getopt_long(argc, argv, 
        mode == MODE_WGET ? "O:P:cNqvrU:T:t:l:hV" : "#fiILo:sSu:T:X:d:F:kvhV",
        long_options, &option_index)) != -1) {
        
        switch (opt) {
            /* wget 短选项 */
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
                
            /* curl 短选项 */
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
                /* 处理长选项 */
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
                    /* 解析限速，如 200k, 1M */
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
    
    /* 收集 URL */
    if (optind >= argc) {
        error("Missing URL");
        return 1;
    }
    
    urls = &argv[optind];
    url_count = argc - optind;
    
    /* 下载每个 URL */
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
