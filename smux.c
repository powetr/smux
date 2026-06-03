#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>

#define MAX_MASTERS 8
#define SLAVES_PER_MASTER 4
#define RECONNECT_INTERVAL 5
#define BUFFER_SIZE 256

typedef int (*ini_handler)(void* user, const char* section, const char* name, const char* value);

static char* rstrip(char* s) {
    char* p = s + strlen(s);
    while (p > s && isspace((unsigned char)(*(p - 1)))) {
        p--;
        *p = '\0';
    }
    return s;
}

static char* lskip(const char* s) {
    while (*s && isspace((unsigned char)(*s))) s++;
    return (char*)s;
}

static char* find_chars_or_comment(const char* s, const char* chars) {
    while (*s && (!chars || !strchr(chars, *s)) && *s != ';' && *s != '#') s++;
    return (char*)s;
}

int ini_parse_file(FILE* file, ini_handler handler, void* user, char* line_buf, char* sec_buf, char* prev_buf) {
    char* start;
    char* end;
    char* name;
    char* value;
    int lineno = 0;
    int error = 0;

    while (fgets(line_buf, BUFFER_SIZE, file)) {
        lineno++;
        start = lskip(line_buf);
        if (*start == ';' || *start == '#' || *start == '\0') continue;
        if (*start == '[') {
            end = find_chars_or_comment(start + 1, "]");
            if (*end == ']') {
                *end = '\0';
                strncpy(sec_buf, start + 1, BUFFER_SIZE - 1);
                *prev_buf = '\0';
            } else if (!error) error = lineno;
        } else if (*start) {
            end = find_chars_or_comment(start, "=:");
            if (*end == '=' || *end == ':') {
                *end = '\0';
                name = rstrip(start);
                value = lskip(end + 1);
                end = find_chars_or_comment(value, NULL);
                *end = '\0';
                rstrip(value);
                strncpy(prev_buf, name, BUFFER_SIZE - 1);
                if (!handler(user, sec_buf, name, value) && !error) error = lineno;
            } else if (!error) error = lineno;
        }
    }
    return error;
}

int ini_parse(const char* filename, ini_handler handler, void* user) {
    FILE* file = fopen(filename, "r");
    if (!file) return -1;
    
    char* line_buf = malloc(BUFFER_SIZE);
    char* sec_buf = malloc(BUFFER_SIZE);
    char* prev_buf = malloc(BUFFER_SIZE);
    
    if (!line_buf || !sec_buf || !prev_buf) {
        if (line_buf) free(line_buf);
        if (sec_buf) free(sec_buf);
        if (prev_buf) free(prev_buf);
        fclose(file);
        return -1;
    }
    
    memset(sec_buf, 0, BUFFER_SIZE);
    memset(prev_buf, 0, BUFFER_SIZE);
    
    int res = ini_parse_file(file, handler, user, line_buf, sec_buf, prev_buf);
    
    free(line_buf);
    free(sec_buf);
    free(prev_buf);
    fclose(file);
    return res;
}

typedef struct {
    char master_dev[BUFFER_SIZE];
    int master_fd;
    int baudrate;
    int slave_fds[SLAVES_PER_MASTER];
    char slave_links[SLAVES_PER_MASTER][BUFFER_SIZE];
    int active_slaves_count;
    time_t last_reconnect_attempt;
    int is_dead;
} MasterGroup;

MasterGroup groups[MAX_MASTERS];
int num_groups = 0;

speed_t int_to_baudrate(int speed) {
    switch (speed) {
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:     return B9600;
    }
}

int is_duplicate_path(const char* path, int current_master_idx, int current_slave_idx, int is_master) {
    int check_m;
    int check_s;
    for (check_m = 0; check_m < MAX_MASTERS; check_m++) {
        MasterGroup* mg = groups + check_m;
        if ((!is_master || check_m != current_master_idx) && strcmp(mg->master_dev, path) == 0) return 1;
        for (check_s = 0; check_s < SLAVES_PER_MASTER; check_s++) {
            if ((is_master || check_m != current_master_idx || check_s != current_slave_idx) && strcmp(*(mg->slave_links + check_s), path) == 0) return 1;
        }
    }
    return 0;
}

static int config_handler(void* user, const char* section, const char* name, const char* value) {
    (void)user;
    if (strncmp(section, "master", 6) != 0) return 1;
    int parsed_master_idx = atoi(section + 6) - 1;
    if (parsed_master_idx < 0 || parsed_master_idx >= MAX_MASTERS) return 1;
    if (parsed_master_idx >= num_groups) num_groups = parsed_master_idx + 1;
    MasterGroup* current_group_ptr = groups + parsed_master_idx;

    if (strcmp(name, "device") == 0) {
        if (is_duplicate_path(value, parsed_master_idx, -1, 1)) {
            fprintf(stderr, "Config Error: Duplicate path '%s' ignored in [%s]\n", value, section);
            return 1; 
        }
        strncpy(current_group_ptr->master_dev, value, BUFFER_SIZE - 1);
    } 
    else if (strcmp(name, "baudrate") == 0) {
        current_group_ptr->baudrate = atoi(value);
    }
    else if (strncmp(name, "slave", 5) == 0) {
        int parsed_slave_idx = atoi(name + 5) - 1;
        if (parsed_slave_idx >= 0 && parsed_slave_idx < SLAVES_PER_MASTER) {
            if (is_duplicate_path(value, parsed_master_idx, parsed_slave_idx, 0)) {
                fprintf(stderr, "Config Error: Duplicate path '%s' ignored in [%s] -> %s\n", value, section, name);
                return 1;
            }
            strncpy(*(current_group_ptr->slave_links + parsed_slave_idx), value, BUFFER_SIZE - 1);
            if (parsed_slave_idx >= current_group_ptr->active_slaves_count) {
                current_group_ptr->active_slaves_count = parsed_slave_idx + 1;
            }
        }
    }
    return 1;
}

void cleanup(int signaled) {
    (void)signaled;
    int clean_m;
    int clean_s;
    for (clean_m = 0; clean_m < num_groups; clean_m++) {
        MasterGroup* mg = groups + clean_m;
        for (clean_s = 0; clean_s < mg->active_slaves_count; clean_s++) {
            if (strlen(*(mg->slave_links + clean_s)) > 0) unlink(*(mg->slave_links + clean_s));
            if (*(mg->slave_fds + clean_s) >= 0) close(*(mg->slave_fds + clean_s));
        }
        if (mg->master_fd >= 0) close(mg->master_fd);
    }
    exit(0);
}

int configure_terminal(int fd, speed_t speed) {
    struct termios t;
    if (tcgetattr(fd, &t) < 0) return -1;
    cfmakeraw(&t);
    cfsetospeed(&t, speed);
    cfsetispeed(&t, speed);
    return tcsetattr(fd, TCSANOW, &t);
}

int try_open_master(MasterGroup* target_group_ptr, int master_index) {
    target_group_ptr->master_fd = open(target_group_ptr->master_dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    speed_t speed = int_to_baudrate(target_group_ptr->baudrate);
    
    if (target_group_ptr->master_fd < 0 || configure_terminal(target_group_ptr->master_fd, speed) < 0) {
        if (target_group_ptr->master_fd >= 0) { close(target_group_ptr->master_fd); target_group_ptr->master_fd = -1; }
        return -1;
    }
    target_group_ptr->is_dead = 0;
    fprintf(stderr, "Success: Master [master%d] (%s) initialized at %d baud.\n", master_index + 1, target_group_ptr->master_dev, target_group_ptr->baudrate);
    return 0;
}

void safe_fd_zero(fd_set* set_ptr) {
    FD_ZERO(set_ptr);
}

void safe_fd_set(int fd, fd_set* set_ptr) {
    FD_SET(fd, set_ptr);
}

int safe_fd_isset(int fd, fd_set* set_ptr) {
    return FD_ISSET(fd, set_ptr);
}

int main(int argc, char *argv[]) {
    char* config_file = "/etc/smux.conf";
    if (argc > 1) config_file = *(argv + (argc - 1));

    int init_m;
    int init_s;
    for (init_m = 0; init_m < MAX_MASTERS; init_m++) {
        MasterGroup* mg = groups + init_m;
        memset(mg->master_dev, 0, BUFFER_SIZE);
        mg->master_fd = -1;
        mg->baudrate = 9600;
        mg->is_dead = 1;
        mg->last_reconnect_attempt = 0;
        for (init_s = 0; init_s < SLAVES_PER_MASTER; init_s++) {
            *(mg->slave_fds + init_s) = -1;
            memset(*(mg->slave_links + init_s), 0, BUFFER_SIZE);
        }
        mg->active_slaves_count = 0;
    }

    if (ini_parse(config_file, config_handler, NULL) < 0) {
        fprintf(stderr, "Error: Cannot load config file %s\n", config_file);
        return 1;
    }

    signal(SIGTERM, cleanup);
    signal(SIGINT, cleanup);

    int loop_m;
    int loop_s;
    for (loop_m = 0; loop_m < num_groups; loop_m++) {
        MasterGroup* mg = groups + loop_m;
        if (strlen(mg->master_dev) == 0) continue;

        for (loop_s = 0; loop_s < mg->active_slaves_count; loop_s++) {
            if (strlen(*(mg->slave_links + loop_s)) == 0) continue;
            *(mg->slave_fds + loop_s) = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
            if (*(mg->slave_fds + loop_s) < 0 || grantpt(*(mg->slave_fds + loop_s)) < 0 || unlockpt(*(mg->slave_fds + loop_s)) < 0) cleanup(0);
            
            char* pts_name = ptsname(*(mg->slave_fds + loop_s));
            if (!pts_name || configure_terminal(*(mg->slave_fds + loop_s), B9600) < 0) cleanup(0);

            unlink(*(mg->slave_links + loop_s));
            if (symlink(pts_name, *(mg->slave_links + loop_s)) < 0) cleanup(0);
            chmod(*(mg->slave_links + loop_s), 0666);
        }
        try_open_master(mg, loop_m);
    }

    fd_set rfds;
    char buf;
    struct timeval tv;

    while (1) {
        time_t now = time(NULL);
        safe_fd_zero(&rfds);
        int max_fd = -1;

        int select_m;
        int select_s;
        for (select_m = 0; select_m < num_groups; select_m++) {
            MasterGroup* mg = groups + select_m;
            if (strlen(mg->master_dev) == 0) continue;

            if (mg->is_dead) {
                if (now - mg->last_reconnect_attempt >= RECONNECT_INTERVAL) {
                    mg->last_reconnect_attempt = now;
                    try_open_master(mg, select_m);
                }
            }
if (mg->is_dead == 0 && mg->master_fd >= 0) {safe_fd_set(mg->master_fd, &rfds);if (mg->master_fd > max_fd) max_fd = mg->master_fd;}
	for (select_s = 0; select_s < mg->active_slaves_count; select_s++) {int s_fd = *(mg->slave_fds + select_s);if (s_fd >= 0) {safe_fd_set(s_fd, &rfds);if (s_fd > max_fd) max_fd = s_fd;}}}
	tv.tv_sec = 1;tv.tv_usec = 0;
if (select(max_fd + 1, &rfds, NULL, NULL, &tv) < 0) continue;
int io_m;int io_s;
for (io_m = 0; io_m < num_groups; io_m++) {MasterGroup* mg = groups + io_m;if (strlen(mg->master_dev) == 0) continue;if (mg->is_dead == 0 && mg->master_fd >= 0 && safe_fd_isset(mg->master_fd, &rfds)) {int n = read(mg->master_fd, &buf, 1);if (n > 0) {for (io_s = 0; io_s < mg->active_slaves_count; io_s++) {int s_fd = *(mg->slave_fds + io_s);if (s_fd >= 0) {write(s_fd, &buf, n);}}} else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {fprintf(stderr, "Warning: Master [master%d] (%s) disconnected.\n", io_m + 1, mg->master_dev);close(mg->master_fd);mg->master_fd = -1;mg->is_dead = 1;mg->last_reconnect_attempt = now;}}for (io_s = 0; io_s < mg->active_slaves_count; io_s++) {int s_fd = *(mg->slave_fds + io_s);if (s_fd >= 0 && safe_fd_isset(s_fd, &rfds)) {int n = read(s_fd, &buf, 1);if (n > 0 && mg->is_dead == 0 && mg->master_fd >= 0) {write(mg->master_fd, &buf, n);}}}}}return 0;}

