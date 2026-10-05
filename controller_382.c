/* ============================================================
 *  RemoteOps Controller - controller_382.c
 *  IE3090 Network Programming
 *  Registration Number: IT24200382
 *
 *  Connects to Agent on 127.0.0.1:9420
 *  Personalised: SID 2830, token OPS-0382
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define AGENT_IP   "127.0.0.1"
#define AGENT_PORT 9420
#define AUTH_TOKEN "OPS-0382"
#define BUF        4096

static int send_all(int fd, const void *buf, size_t len)
{
    const char *p = buf;
    size_t s = 0;
    while (s < len) {
        ssize_t n = send(fd, p + s, len - s, 0);
        if (n <= 0) return -1;
        s += (size_t)n;
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

static int recv_all(int fd, void *buf, size_t len)
{
    char *p = buf;
    size_t g = 0;
    while (g < len) {
        ssize_t n = recv(fd, p + g, len - g, 0);
        if (n <= 0) return -1;
        g += (size_t)n;
    }
    return 0;
}

static void send_line(int fd, const char *line)
{
    send_all(fd, line, strlen(line));
    send_all(fd, "\n", 1);
}

/* -------- UDP listener (runs in a forked child) -------- */
static void run_udp_listener(int port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("udp socket"); return; }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port        = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
        perror("udp bind"); close(fd); return;
    }

    printf("[UDP] listening on port %d (Ctrl+C to stop)\n", port);
    fflush(stdout);

    while (1) {
        char buf[1024];
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr *)&from, &fl);
        if (n <= 0) continue;
        buf[n] = '\0';
        printf("[UDP %s:%d] %s\n",
               inet_ntoa(from.sin_addr), ntohs(from.sin_port), buf);
        fflush(stdout);
    }
}

/* -------- PUT: upload local file to agent -------- */
static void do_put(int sock, const char *local, const char *remote)
{
    struct stat st;
    if (stat(local, &st) != 0) {
        perror("stat");
        return;
    }
    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "Not a regular file: %s\n", local);
        return;
    }

    char line[512];
    snprintf(line, sizeof(line), "PUT %s %ld", remote, (long)st.st_size);
    send_line(sock, line);

    FILE *f = fopen(local, "rb");
    if (!f) { perror("fopen"); return; }

    char buf[BUF];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        send_all(sock, buf, n);
    fclose(f);

    char resp[512];
    recv_line(sock, resp, sizeof(resp));
    printf("Server: %s\n", resp);
}

/* -------- GET: download file from agent -------- */
static void do_get(int sock, const char *remote, const char *local)
{
    char line[512];
    snprintf(line, sizeof(line), "GET %s", remote);
    send_line(sock, line);

    char resp[512];
    recv_line(sock, resp, sizeof(resp));
    printf("Server: %s\n", resp);

    if (strncmp(resp, "OK FILE_SEND", 12) != 0) return;

    long fsize = 0;
    char name[256] = {0};
    sscanf(resp, "OK FILE_SEND %255s %ld", name, &fsize);

    FILE *f = fopen(local, "wb");
    if (!f) { perror("fopen"); return; }

    char buf[BUF];
    long remain = fsize;
    while (remain > 0) {
        size_t chunk = (remain > (long)sizeof(buf))
                       ? sizeof(buf) : (size_t)remain;
        if (recv_all(sock, buf, chunk) < 0) break;
        fwrite(buf, 1, chunk, f);
        remain -= (long)chunk;
    }
    fclose(f);
    printf("Saved %ld bytes to %s\n", fsize, local);
}

/* -------- menu -------- */
static void print_menu(void)
{
    printf("\n===== RemoteOps Controller =====\n");
    printf(" 1) SYSINFO\n");
    printf(" 2) LISTPROC\n");
    printf(" 3) EXEC DATE\n");
    printf(" 4) EXEC UPTIME\n");
    printf(" 5) EXEC DISKFREE\n");
    printf(" 6) EXEC HOSTNAME\n");
    printf(" 7) EXEC WHOAMI\n");
    printf(" 8) PUT  (upload a local file)\n");
    printf(" 9) GET  (download a file)\n");
    printf("10) MONITOR START\n");
    printf("11) MONITOR STOP\n");
    printf(" 0) QUIT\n");
    printf("Choice: ");
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port   = htons(AGENT_PORT);
    inet_pton(AF_INET, AGENT_IP, &a.sin_addr);

    if (connect(sock, (struct sockaddr *)&a, sizeof(a)) < 0) {
        perror("connect");
        fprintf(stderr, "Is the agent running? (./agent_382)\n");
        return 1;
    }
    printf("Connected to Agent %s:%d\n", AGENT_IP, AGENT_PORT);

    char buf[BUF];
    char auth[128];
    snprintf(auth, sizeof(auth), "AUTH %s", AUTH_TOKEN);
    send_line(sock, auth);
    recv_line(sock, buf, sizeof(buf));
    printf("Auth: %s\n", buf);

    if (strncmp(buf, "OK AUTHENTICATED", 16) != 0) {
        fprintf(stderr, "Authentication failed — check token.\n");
        close(sock);
        return 1;
    }

    int running = 1;
    while (running) {
        print_menu();
        int c;
        if (scanf("%d", &c) != 1) break;
        getchar();   /* consume newline */

        switch (c) {
        case 1: case 2: {
            const char *cmd = (c == 1) ? "SYSINFO" : "LISTPROC";
            send_line(sock, cmd);
            recv_line(sock, buf, sizeof(buf));
            printf("Server: %s\n", buf);
            break;
        }
        case 3: case 4: case 5: case 6: case 7: {
            const char *names[] = { "DATE", "UPTIME", "DISKFREE",
                                    "HOSTNAME", "WHOAMI" };
            char line[64];
            snprintf(line, sizeof(line), "EXEC %s", names[c - 3]);
            send_line(sock, line);
            recv_line(sock, buf, sizeof(buf));
            printf("Server: %s\n", buf);
            break;
        }
        case 8: {
            char local[256], remote[256];
            printf("Local file  : "); scanf("%255s", local);
            printf("Remote name : "); scanf("%255s", remote);
            do_put(sock, local, remote);
            break;
        }
        case 9: {
            char remote[256], local[256];
            printf("Remote name : "); scanf("%255s", remote);
            printf("Save as     : "); scanf("%255s", local);
            do_get(sock, remote, local);
            break;
        }
        case 10: {
            int p;
            printf("Local UDP port to listen on: ");
            scanf("%d", &p);
            char line[64];
            snprintf(line, sizeof(line), "MONITOR START %d", p);
            send_line(sock, line);
            recv_line(sock, buf, sizeof(buf));
            printf("Server: %s\n", buf);

            pid_t pid = fork();
            if (pid == 0) {
                run_udp_listener(p);
                _exit(0);
            }
            printf("UDP listener forked (pid=%d)\n", (int)pid);
            break;
        }
        case 11:
            send_line(sock, "MONITOR STOP");
            recv_line(sock, buf, sizeof(buf));
            printf("Server: %s\n", buf);
            break;
        case 0:
            send_line(sock, "QUIT");
            recv_line(sock, buf, sizeof(buf));
            printf("Server: %s\n", buf);
            running = 0;
            break;
        default:
            printf("Invalid choice.\n");
        }
    }

    close(sock);
    return 0;
}
