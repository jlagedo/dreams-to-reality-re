/*
 * Development control channel - transport (ctl_net.h).
 *
 * Plain sockets: Winsock on Windows, BSD sockets elsewhere (the pinned SDL3
 * has no networking). This is the only file that touches them, and the
 * thread it starts touches nothing of the host or the guest.
 */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET Sock;
#define SOCK_NONE INVALID_SOCKET
#define sock_close closesocket
#define SHUT_SEND SD_SEND
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int Sock;
#define SOCK_NONE (-1)
#define sock_close close
#define SHUT_SEND SHUT_WR
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "ctl_net.h"

#define MAX_LINE 65536           /* a longer request drops the connection */
#define SEND_TIMEOUT_MS 5000     /* a client that stops reading cannot hold the game for longer */

typedef struct Line { struct Line* next; uint32_t connection; char* text; } Line;

static Sock g_listen = SOCK_NONE;
static SDL_Mutex* g_lock;        /* the client socket, its number and the queue */
static Sock g_client = SOCK_NONE;
static uint32_t g_connection, g_connections;
static Line* g_head;
static Line* g_tail;

static void enqueue(uint32_t connection, const char* text, size_t length) {
    Line* line = (Line*)malloc(sizeof *line);
    char* copy = (char*)malloc(length + 1);
    if (!line || !copy) { free(line); free(copy); return; }
    if (length && text[length - 1] == '\r') length--;
    memcpy(copy, text, length);
    copy[length] = 0;
    line->next = NULL; line->connection = connection; line->text = copy;
    SDL_LockMutex(g_lock);
    if (g_tail) g_tail->next = line; else g_head = line;
    g_tail = line;
    SDL_UnlockMutex(g_lock);
}

static int SDLCALL serve(void* unused) {
    static char buffer[MAX_LINE];
    (void)unused;
    for (;;) {
        Sock s = accept(g_listen, NULL, NULL);
        if (s == SOCK_NONE) { SDL_Delay(100); continue; }
        int one = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
#ifdef _WIN32
        DWORD timeout = SEND_TIMEOUT_MS;
#else
        struct timeval timeout = { SEND_TIMEOUT_MS / 1000, 0 };
#endif
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof timeout);
        SDL_LockMutex(g_lock);
        g_client = s;
        uint32_t connection = g_connection = ++g_connections;
        SDL_UnlockMutex(g_lock);

        size_t used = 0;
        while (used < sizeof buffer) {
            int got = (int)recv(s, buffer + used, (int)(sizeof buffer - used), 0);
            if (got <= 0) break;
            size_t start = 0, end = used + (size_t)got;
            for (size_t i = used; i < end; i++)
                if (buffer[i] == '\n') { enqueue(connection, buffer + start, i - start); start = i + 1; }
            memmove(buffer, buffer + start, end - start);
            used = end - start;
        }

        SDL_LockMutex(g_lock);
        g_client = SOCK_NONE;
        g_connection = 0;
        SDL_UnlockMutex(g_lock);
        sock_close(s);
    }
    return 0;
}

int ctl_net_start(int port, char* error, size_t error_size) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) { snprintf(error, error_size, "WSAStartup failed"); return -1; }
#endif
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   /* this machine only */
    address.sin_port = htons((unsigned short)port);
    socklen_t size = sizeof address;
    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen == SOCK_NONE || bind(g_listen, (struct sockaddr*)&address, sizeof address) ||
        listen(g_listen, 1) || getsockname(g_listen, (struct sockaddr*)&address, &size)) {
        snprintf(error, error_size, "cannot listen on 127.0.0.1:%d", port);
        if (g_listen != SOCK_NONE) sock_close(g_listen);
        g_listen = SOCK_NONE;
        return -1;
    }
    g_lock = SDL_CreateMutex();
    SDL_DetachThread(SDL_CreateThread(serve, "wd-ctl", NULL));
    return ntohs(address.sin_port);
}

char* ctl_net_next(uint32_t* connection) {
    char* text = NULL;
    SDL_LockMutex(g_lock);
    Line* line = g_head;
    if (line) {
        g_head = line->next;
        if (!g_head) g_tail = NULL;
        text = line->text;
        *connection = line->connection;
    }
    SDL_UnlockMutex(g_lock);
    free(line);
    return text;
}

uint32_t ctl_net_connection(void) {
    SDL_LockMutex(g_lock);
    uint32_t connection = g_connection;
    SDL_UnlockMutex(g_lock);
    return connection;
}

static void send_all(Sock s, const char* data, size_t length) {
    while (length) {
        int sent = (int)send(s, data, (int)length, MSG_NOSIGNAL);
        if (sent <= 0) return;   /* gone or stalled: the reader thread sees the connection end */
        data += sent;
        length -= (size_t)sent;
    }
}

void ctl_net_send(uint32_t connection, const char* line, size_t length) {
    SDL_LockMutex(g_lock);
    if (connection && connection == g_connection) {
        send_all(g_client, line, length);
        send_all(g_client, "\n", 1);
    }
    SDL_UnlockMutex(g_lock);
}

void ctl_net_finish(void) {
    SDL_LockMutex(g_lock);
    if (g_client != SOCK_NONE) shutdown(g_client, SHUT_SEND);
    SDL_UnlockMutex(g_lock);
    /* The client closes once it has read the last answer; do not wait long for one that does not. */
    for (int i = 0; i < 50 && ctl_net_connection(); i++) SDL_Delay(10);
}
