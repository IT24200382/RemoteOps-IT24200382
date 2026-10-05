/* ============================================================
 *  RemoteOps Agent  -  agent_382.c
 *  IE3090 Network Programming
 *  Registration Number: IT24200382
 *
 *  Personalised values:
 *     Listening port : 9420
 *     SID tag        : 2830
 *     Auth token     : OPS-0382
 *     Log file       : remoteops_IT24200382.log
 *     Storage path   : ./agentfiles/IT24200382/
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define AGENT_PORT       9420
#define SID_TAG          "2830"
#define AUTH_TOKEN       "OPS-0382"
#define LOG_FILE         "remoteops_IT24200382.log"
#define STORAGE_DIR      "./agentfiles/IT24200382/"
#define MAX_FILE_SIZE    (10L * 1024L * 1024L)
#define RECV_BUF         4096
#define MONITOR_INTERVAL 2

static FILE            *g_log = NULL;
static pthread_mutex_t  g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int   sock;
    char  peer_ip[INET_ADDRSTRLEN];
    int   peer_port;
    int   authenticated;
    volatile int monitor_active;
    int   monitor_udp_port;
    char  monitor_ip[INET_ADDRSTRLEN];
    pthread_t monitor_tid;
} session_t;

static void log_event(const char *fmt, ...)
{
    va_list ap;
    time_t  t;
    struct tm tm;
    char    ts[32];

    pthread_mutex_lock(&g_log_mutex);
    if (g_log) {
        t = time(NULL);
        localtime_r(&t, &tm);
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
        fprintf(g_log, "[%s] ", ts);
        va_start(ap, fmt);
        vfprintf(g_log, fmt, ap);
        va_end(ap);
        fprintf(g_log, "\n");
        fflush(g_log);
    }
    pthread_mutex_unlock(&g_log_mutex);
}

static int send_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int send_line(int fd, const char *fmt, ...)
{
    char    buf[8192];
    va_list ap;
    int     n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    if (n > (int)sizeof(buf) - 2) n = (int)sizeof(buf) - 2;
    buf[n]     = '\n';
    buf[n + 1] = '\0';
    return send_all(fd, buf, (size_t)n + 1);
}

static int recv_all(int fd, void *buf, size_t len)
{
    char  *p   = (char *)buf;
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(fd, p + got, len - got, 0);
        if (n <= 0) return -1;
        got += (size_t)n;
    }
    return 0;
}

static int recv_line(int fd, char *buf, size_t cap)
{
    size_t i = 0;
    while (i + 1 < cap) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n <= 0) return -1;
        if (c == '\n') { buf[i] = '\0'; return (int)i; }
        buf[i++] = c;
    }
    buf[cap - 1] = '\0';
    return (int)cap - 1;
}

static double get_cpu_load(void)
{
    FILE  *f = fopen("/proc/loadavg", "r");
    double v = 0.0;
    if (f) { if (fscanf(f, "%lf", &v) != 1) v = 0.0; fclose(f); }
    return v;
}

static double get_uptime_sec(void)
{
    FILE  *f = fopen("/proc/uptime", "r");
    double v = 0.0;
    if (f) { if (fscanf(f, "%lf", &v) != 1) v = 0.0; fclose(f); }
    return v;
}

static long get_mem_used_mb(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    long total = 0, avail = 0;
    char key[64]; long val; char unit[16];
    if (!f) return 0;
    while (fscanf(f, "%63s %ld %15s", key, &val, unit) == 3) {
        if (!strcmp(key, "MemTotal:"))     total = val;
        if (!strcmp(key, "MemAvailable:")) avail = val;
        if (total && avail) break;
    }
    fclose(f);
    if (total == 0) return 0;
    return (total - avail) / 1024;
}

static void *monitor_thread(void *arg)
{
    session_t *s  = (session_t *)arg;
    int        fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { log_event("MONITOR socket() failed"); return NULL; }

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = htons((uint16_t)s->monitor_udp_port);
    inet_pton(AF_INET, s->monitor_ip, &dst.sin_addr);

    log_event("MONITOR start -> %s:%d every %ds",
              s->monitor_ip, s->monitor_udp_port, MONITOR_INTERVAL);

    while (s->monitor_active) {
        char line[256];
        int n = snprintf(line, sizeof(line),
                         "SYSINFO %.2f %ld %.0f SID:%s",
                         get_cpu_load(), get_mem_used_mb(),
                         get_uptime_sec(), SID_TAG);
        sendto(fd, line, (size_t)n, 0,
               (struct sockaddr *)&dst, sizeof(dst));
        sleep(MONITOR_INTERVAL);
    }

    close(fd);
    log_event("MONITOR stop -> %s:%d", s->monitor_ip, s->monitor_udp_port);
    return NULL;
}

static int is_whitelisted(const char *name)
{
    const char *wl[] = { "DATE", "UPTIME", "DISKFREE",
                         "HOSTNAME", "WHOAMI", NULL };
    for (int i = 0; wl[i]; i++)
        if (!strcmp(name, wl[i])) return 1;
    return 0;
}

static void run_whitelisted(int fd, const char *name)
{
    const char *cmd = NULL;
    if      (!strcmp(name, "DATE"))     cmd = "date";
    else if (!strcmp(name, "UPTIME"))   cmd = "uptime";
    else if (!strcmp(name, "DISKFREE")) cmd = "df -h";
    else if (!strcmp(name, "HOSTNAME")) cmd = "hostname";
    else if (!strcmp(name, "WHOAMI"))   cmd = "whoami";

    char output[4096] = {0};
    FILE *p = popen(cmd, "r");
    if (p) {
        size_t got = fread(output, 1, sizeof(output) - 1, p);
        output[got] = '\0';
        pclose(p);
    }
    for (char *q = output; *q; q++) if (*q == '\n') *q = ' ';

    send_line(fd, "OK EXEC_RESULT %s SID:%s", output, SID_TAG);
    log_event("EXEC %s -> sent result", name);
}

static void cmd_auth(session_t *s, const char *token)
{
    if (!strcmp(token, AUTH_TOKEN)) {
        s->authenticated = 1;
        send_line(s->sock, "OK AUTHENTICATED SID:%s", SID_TAG);
        log_event("AUTH success from %s:%d", s->peer_ip, s->peer_port);
    } else {
        send_line(s->sock, "ERR 001 AUTH_FAILED SID:%s", SID_TAG);
        log_event("AUTH failure from %s:%d", s->peer_ip, s->peer_port);
    }
}

static void cmd_sysinfo(session_t *s)
{
    send_line(s->sock, "OK SYSINFO %.2f %ld %.0f SID:%s",
              get_cpu_load(), get_mem_used_mb(),
              get_uptime_sec(), SID_TAG);
    log_event("SYSINFO sent");
}

static void cmd_listproc(session_t *s)
{
    char buf[4096] = {0};
    FILE *p = popen("ps -e -o pid=,comm= | head -n 30", "r");
    if (p) {
        size_t got = fread(buf, 1, sizeof(buf) - 1, p);
        buf[got] = '\0';
        pclose(p);
    }
    for (char *q = buf; *q; q++) {
        if (*q == '\n') *q = ',';
        if (*q == ' ')  *q = ':';
    }
    send_line(s->sock, "OK PROCs %s SID:%s", buf, SID_TAG);
    log_event("LISTPROC sent");
}

static void cmd_exec(session_t *s, const char *name)
{
    if (!is_whitelisted(name)) {
        send_line(s->sock, "ERR 002 COMMAND_NOT_ALLOWED SID:%s", SID_TAG);
        log_event("EXEC %s rejected (not whitelisted)", name);
        return;
    }
    run_whitelisted(s->sock, name);
}

static void cmd_put(session_t *s, const char *fname, long fsize)
{
    if (fsize < 0 || fsize > MAX_FILE_SIZE) {
        send_line(s->sock, "ERR 004 FILE_TOO_LARGE SID:%s", SID_TAG);
        log_event("PUT %s rejected (size=%ld)", fname, fsize);
        return;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s%s", STORAGE_DIR, fname);

    FILE *f = fopen(path, "wb");
    if (!f) {
        send_line(s->sock, "ERR 004 FILE_TOO_LARGE SID:%s", SID_TAG);
        log_event("PUT %s fopen failed: %s", path, strerror(errno));
        return;
    }

    char  buf[RECV_BUF];
    long  remain = fsize;
    while (remain > 0) {
        size_t chunk = (remain > (long)sizeof(buf)) ? sizeof(buf) : (size_t)remain;
        if (recv_all(s->sock, buf, chunk) < 0) {
            fclose(f);
            log_event("PUT %s aborted mid-stream", fname);
            return;
        }
        fwrite(buf, 1, chunk, f);
        remain -= (long)chunk;
    }
    fclose(f);

    send_line(s->sock, "OK FILE_RECEIVED %s SID:%s", fname, SID_TAG);
    log_event("PUT %s (%ld bytes) stored", fname, fsize);
}

static void cmd_get(session_t *s, const char *fname)
{
    char path[512];
    snprintf(path, sizeof(path), "%s%s", STORAGE_DIR, fname);

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        send_line(s->sock, "ERR 005 FILE_NOT_FOUND SID:%s", SID_TAG);
        log_event("GET %s -> not found", fname);
        return;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        send_line(s->sock, "ERR 005 FILE_NOT_FOUND SID:%s", SID_TAG);
        return;
    }

    send_line(s->sock, "OK FILE_SEND %s %ld SID:%s",
              fname, (long)st.st_size, SID_TAG);

    char buf[RECV_BUF];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        if (send_all(s->sock, buf, n) < 0) break;
    fclose(f);

    log_event("GET %s (%ld bytes) sent", fname, (long)st.st_size);
}

static void cmd_monitor_start(session_t *s, int udp_port)
{
    if (s->monitor_active) {
        send_line(s->sock, "OK MONITOR_STARTED SID:%s", SID_TAG);
        return;
    }
    s->monitor_udp_port = udp_port;
    strncpy(s->monitor_ip, s->peer_ip, sizeof(s->monitor_ip) - 1);
    s->monitor_ip[sizeof(s->monitor_ip) - 1] = '\0';
    s->monitor_active = 1;
    if (pthread_create(&s->monitor_tid, NULL, monitor_thread, s) != 0) {
        s->monitor_active = 0;
        send_line(s->sock, "ERR 006 MONITOR_FAILED SID:%s", SID_TAG);
        return;
    }
    pthread_detach(s->monitor_tid);
    send_line(s->sock, "OK MONITOR_STARTED SID:%s", SID_TAG);
}

static void cmd_monitor_stop(session_t *s)
{
    s->monitor_active = 0;
    send_line(s->sock, "OK MONITOR_STOPPED SID:%s", SID_TAG);
}

static void *client_thread(void *arg)
{
    session_t *s = (session_t *)arg;
    char       line[1024];

    log_event("Connection opened from %s:%d", s->peer_ip, s->peer_port);

    while (1) {
        int n = recv_line(s->sock, line, sizeof(line));
        if (n < 0) {
            log_event("Connection closed (recv error/EOF) from %s:%d",
                      s->peer_ip, s->peer_port);
            break;
        }
        if (n == 0) continue;

        log_event("CMD from %s:%d : %s", s->peer_ip, s->peer_port, line);

        char cmd[64] = {0};
        sscanf(line, "%63s", cmd);

        if (!strcmp(cmd, "AUTH")) {
            char tok[128] = {0};
            if (sscanf(line, "AUTH %127s", tok) != 1) {
                send_line(s->sock, "ERR 001 AUTH_FAILED SID:%s", SID_TAG);
            } else {
                cmd_auth(s, tok);
            }
            continue;
        }

        if (!s->authenticated) {
            send_line(s->sock, "ERR 001 AUTH_FAILED SID:%s", SID_TAG);
            log_event("Unauthenticated command rejected: %s", cmd);
            continue;
        }

        if      (!strcmp(cmd, "SYSINFO"))  cmd_sysinfo(s);
        else if (!strcmp(cmd, "LISTPROC")) cmd_listproc(s);
        else if (!strcmp(cmd, "EXEC")) {
            char name[128] = {0};
            sscanf(line, "EXEC %127s", name);
            cmd_exec(s, name);
        }
        else if (!strcmp(cmd, "PUT")) {
            char  fname[256] = {0};
            long  fsize = 0;
            if (sscanf(line, "PUT %255s %ld", fname, &fsize) == 2)
                cmd_put(s, fname, fsize);
            else
                send_line(s->sock, "ERR 004 FILE_TOO_LARGE SID:%s", SID_TAG);
        }
        else if (!strcmp(cmd, "GET")) {
            char fname[256] = {0};
            sscanf(line, "GET %255s", fname);
            cmd_get(s, fname);
        }
        else if (!strcmp(cmd, "MONITOR")) {
            char sub[32] = {0};
            sscanf(line, "MONITOR %31s", sub);
            if (!strcmp(sub, "START")) {
                int p = 0;
                sscanf(line, "MONITOR START %d", &p);
                cmd_monitor_start(s, p);
            } else if (!strcmp(sub, "STOP")) {
                cmd_monitor_stop(s);
            } else {
                send_line(s->sock, "ERR 002 COMMAND_NOT_ALLOWED SID:%s", SID_TAG);
            }
        }
        else if (!strcmp(cmd, "QUIT")) {
            send_line(s->sock, "OK BYE SID:%s", SID_TAG);
            log_event("QUIT from %s:%d", s->peer_ip, s->peer_port);
            break;
        }
        else {
            send_line(s->sock, "ERR 002 COMMAND_NOT_ALLOWED SID:%s", SID_TAG);
        }
    }

    s->monitor_active = 0;
    close(s->sock);
    log_event("Connection closed from %s:%d", s->peer_ip, s->peer_port);
    free(s);
    return NULL;
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);

    g_log = fopen(LOG_FILE, "a");
    if (!g_log) {
        fprintf(stderr, "Cannot open log file %s\n", LOG_FILE);
        return 1;
    }
    log_event("=== Agent starting on port %d ===", AGENT_PORT);

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(AGENT_PORT);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(srv, 16) < 0) { perror("listen"); return 1; }

    printf("RemoteOps Agent listening on port %d (SID:%s)\n",
           AGENT_PORT, SID_TAG);
    printf("Log file: %s\n", LOG_FILE);
    printf("Storage : %s\n", STORAGE_DIR);
    fflush(stdout);

    while (1) {
        struct sockaddr_in cli;
        socklen_t clilen = sizeof(cli);
        int c = accept(srv, (struct sockaddr *)&cli, &clilen);
        if (c < 0) { perror("accept"); continue; }

        session_t *s = calloc(1, sizeof(session_t));
        if (!s) { close(c); continue; }
        s->sock      = c;
        s->peer_port = ntohs(cli.sin_port);
        inet_ntop(AF_INET, &cli.sin_addr, s->peer_ip, sizeof(s->peer_ip));

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, s) != 0) {
            perror("pthread_create");
            close(c);
            free(s);
            continue;
        }
        pthread_detach(tid);
    }

    fclose(g_log);
    return 0;
}
