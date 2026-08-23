#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int wait_writable(int fd, int timeout_ms) {
    struct pollfd descriptor = {
        .fd = fd,
        .events = POLLOUT,
    };

    int result = poll(&descriptor, 1, timeout_ms);
    if (result > 0) {
        if (descriptor.revents & POLLOUT) {
            return 0;
        }
        fprintf(stderr, "hidg poll returned unexpected events: 0x%x\n", descriptor.revents);
        return -1;
    }
    if (result == 0) {
        fprintf(stderr, "timed out waiting for the HID endpoint to become writable\n");
        return -1;
    }
    fprintf(stderr, "poll failed: %s\n", strerror(errno));
    return -1;
}

static int write_report(int fd, const uint8_t report[4], const char *label) {
    if (wait_writable(fd, 5000) != 0) {
        return -1;
    }

    ssize_t written = write(fd, report, 4);
    if (written < 0) {
        fprintf(stderr, "%s report write failed: %s\n", label, strerror(errno));
        return -1;
    }
    if (written != 4) {
        fprintf(stderr, "%s report short write: expected 4 bytes, wrote %zd\n", label, written);
        return -1;
    }

    printf("%s report written: 00 %02x 00 00\n", label, report[1]);
    return 0;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/dev/hidg0";
    const uint8_t move_right[4] = {0x00, 0x02, 0x00, 0x00};
    const uint8_t move_left[4] = {0x00, 0xfe, 0x00, 0x00};
    const struct timespec pause = {
        .tv_sec = 1,
        .tv_nsec = 0,
    };
    struct stat status;

    if (stat(path, &status) != 0) {
        fprintf(stderr, "cannot stat %s: %s\n", path, strerror(errno));
        return 1;
    }
    if (!S_ISCHR(status.st_mode)) {
        fprintf(stderr, "%s is not a character device\n", path);
        return 1;
    }

    int fd = open(path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
        return 1;
    }

    if (write_report(fd, move_right, "+2 X") != 0) {
        close(fd);
        return 1;
    }
    if (nanosleep(&pause, NULL) != 0) {
        fprintf(stderr, "report pause failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    if (write_report(fd, move_left, "-2 X") != 0) {
        close(fd);
        return 1;
    }

    if (close(fd) != 0) {
        fprintf(stderr, "close failed: %s\n", strerror(errno));
        return 1;
    }

    puts("HID_REPORT_PAIR_COMPLETE=YES");
    return 0;
}
