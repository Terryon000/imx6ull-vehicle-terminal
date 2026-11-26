#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_PORT 9000
#define DHT11_DATA_SIZE 5

static volatile sig_atomic_t running = 1;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    running = 0;
}

static void print_usage(const char *program_name)
{
    fprintf(stderr,
            "Usage: %s [-p port] [-d dht11_device] [-i input_event_device]\n"
            "Example: %s -p 9000 -d /dev/dht11 -i /dev/input/event1\n",
            program_name, program_name);
}

static int send_line(int *client_fd, const char *line)
{
    size_t remaining = strlen(line);
    const char *cursor = line;

    if (*client_fd < 0) {
        return 0;
    }

    while (remaining > 0) {
        ssize_t sent = send(*client_fd, cursor, remaining, MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent <= 0) {
            fprintf(stderr, "Client disconnected: %s\n",
                    sent == 0 ? "connection closed" : strerror(errno));
            close(*client_fd);
            *client_fd = -1;
            return -1;
        }
        cursor += sent;
        remaining -= (size_t)sent;
    }

    return 0;
}

static int read_dht11(int fd, unsigned char data[DHT11_DATA_SIZE])
{
    ssize_t count;

    do {
        count = read(fd, data, DHT11_DATA_SIZE);
    } while (count < 0 && errno == EINTR);

    if (count != DHT11_DATA_SIZE) {
        return -1;
    }

    if ((unsigned char)(data[0] + data[1] + data[2] + data[3]) != data[4]) {
        errno = EIO;
        return -1;
    }

    return 0;
}

static struct timespec add_milliseconds(struct timespec value, long milliseconds)
{
    value.tv_sec += milliseconds / 1000;
    value.tv_nsec += (milliseconds % 1000) * 1000000L;
    if (value.tv_nsec >= 1000000000L) {
        value.tv_sec++;
        value.tv_nsec -= 1000000000L;
    }
    return value;
}

static long milliseconds_until(struct timespec deadline, struct timespec now)
{
    long long nanoseconds = (long long)(deadline.tv_sec - now.tv_sec) * 1000000000LL
                         + deadline.tv_nsec - now.tv_nsec;
    if (nanoseconds <= 0) {
        return 0;
    }
    return (long)((nanoseconds + 999999LL) / 1000000LL);
}

static void process_input_events(int *input_fd, int *client_fd)
{
    struct input_event event;

    for (;;) {
        ssize_t count = read(*input_fd, &event, sizeof(event));
        if (count == (ssize_t)sizeof(event)) {
            if (event.type == EV_KEY) {
                char line[128];
                snprintf(line, sizeof(line),
                         "{\"type\":\"key\",\"code\":%u,\"value\":%d}\n",
                         (unsigned int)event.code, event.value);
                send_line(client_fd, line);
            }
            continue;
        }

        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        if (count == 0) {
            fprintf(stderr, "Input device reached EOF; key events disabled\n");
        } else {
            fprintf(stderr, "Input device read failed: %s; key events disabled\n",
                    strerror(errno));
        }
        close(*input_fd);
        *input_fd = -1;
        break;
    }
}

int main(int argc, char **argv)
{
    const char *dht11_path = "/dev/dht11";
    const char *input_path = NULL;
    int port = DEFAULT_PORT;
    int option;
    int dht11_fd = -1;
    int input_fd = -1;
    int server_fd = -1;
    int client_fd = -1;
    int reuse_address = 1;
    struct sockaddr_in address;
    struct timespec next_sample;

    while ((option = getopt(argc, argv, "p:d:i:h")) != -1) {
        switch (option) {
        case 'p': {
            char *end = NULL;
            long parsed_port = strtol(optarg, &end, 10);
            if (*optarg == '\0' || *end != '\0' || parsed_port < 1 || parsed_port > 65535) {
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
            port = (int)parsed_port;
            break;
        }
        case 'd':
            dht11_path = optarg;
            break;
        case 'i':
            input_path = optarg;
            break;
        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        default:
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    dht11_fd = open(dht11_path, O_RDONLY);
    if (dht11_fd < 0) {
        fprintf(stderr, "Cannot open %s: %s\n", dht11_path, strerror(errno));
        goto cleanup;
    }

    if (input_path != NULL) {
        input_fd = open(input_path, O_RDONLY | O_NONBLOCK);
        if (input_fd < 0) {
            fprintf(stderr, "Cannot open %s; continuing without key events: %s\n",
                    input_path, strerror(errno));
        }
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        goto cleanup;
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse_address, sizeof(reuse_address)) < 0) {
        perror("setsockopt");
        goto cleanup;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((unsigned short)port);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        goto cleanup;
    }
    if (listen(server_fd, 1) < 0) {
        perror("listen");
        goto cleanup;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);
    clock_gettime(CLOCK_MONOTONIC, &next_sample);
    next_sample = add_milliseconds(next_sample, 1000);

    printf("Listening on TCP port %d; DHT11: %s\n", port, dht11_path);
    if (input_fd >= 0) {
        printf("Key events: %s\n", input_path);
    }

    while (running) {
        fd_set read_fds;
        struct timeval timeout;
        struct timespec now;
        int max_fd = server_fd;
        long timeout_ms;
        int ready;

        clock_gettime(CLOCK_MONOTONIC, &now);
        timeout_ms = milliseconds_until(next_sample, now);
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;

        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        if (input_fd >= 0) {
            FD_SET(input_fd, &read_fds);
            if (input_fd > max_fd) max_fd = input_fd;
        }
        if (client_fd >= 0) {
            FD_SET(client_fd, &read_fds);
            if (client_fd > max_fd) max_fd = client_fd;
        }

        ready = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready < 0 && errno != EINTR) {
            perror("select");
            break;
        }

        if (ready > 0 && FD_ISSET(server_fd, &read_fds)) {
            int accepted_fd = accept(server_fd, NULL, NULL);
            if (accepted_fd >= 0) {
                if (client_fd >= 0) close(client_fd);
                client_fd = accepted_fd;
                printf("Client connected\n");
            } else if (errno != EINTR) {
                perror("accept");
            }
        }

        if (ready > 0 && input_fd >= 0 && FD_ISSET(input_fd, &read_fds)) {
            process_input_events(&input_fd, &client_fd);
        }

        if (ready > 0 && client_fd >= 0 && FD_ISSET(client_fd, &read_fds)) {
            char discard[64];
            ssize_t count = recv(client_fd, discard, sizeof(discard), 0);
            if (count == 0) {
                printf("Client disconnected\n");
                close(client_fd);
                client_fd = -1;
            } else if (count < 0 && errno != EINTR && errno != EAGAIN) {
                close(client_fd);
                client_fd = -1;
            }
        }

        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > next_sample.tv_sec
                || (now.tv_sec == next_sample.tv_sec && now.tv_nsec >= next_sample.tv_nsec)) {
            unsigned char data[DHT11_DATA_SIZE];
            char line[128];

            if (read_dht11(dht11_fd, data) == 0) {
                snprintf(line, sizeof(line),
                         "{\"type\":\"sensor\",\"humidity\":%u.%u,\"temperature\":%u.%u}\n",
                         (unsigned int)data[0], (unsigned int)data[1],
                         (unsigned int)data[2], (unsigned int)data[3]);
            } else {
                fprintf(stderr, "DHT11 read failed: %s\n", strerror(errno));
                snprintf(line, sizeof(line), "{\"type\":\"sensor\",\"error\":\"read_failed\"}\n");
            }
            send_line(&client_fd, line);
            next_sample = add_milliseconds(now, 1000);
        }
    }

cleanup:
    if (client_fd >= 0) close(client_fd);
    if (server_fd >= 0) close(server_fd);
    if (input_fd >= 0) close(input_fd);
    if (dht11_fd >= 0) close(dht11_fd);
    return running ? EXIT_FAILURE : EXIT_SUCCESS;
}