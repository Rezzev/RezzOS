#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/kd.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: setconsolefont <FONT_NAME>\n");
        return 1;
    }

    const char *font_name = argv[1];

    /* Try sysfs parameter first if available */
    int sys_fd = open("/sys/module/fbcon/parameters/font", O_WRONLY);
    if (sys_fd >= 0) {
        write(sys_fd, font_name, strlen(font_name));
        close(sys_fd);
    }

    /* Try ioctl KDFONTOP on tty devices */
    const char *tty_devs[] = {
        "/dev/tty0",
        "/dev/tty1",
        "/dev/tty",
        "/dev/console",
        NULL
    };

    struct console_font_op op;
    memset(&op, 0, sizeof(op));
    op.op = KD_FONT_OP_SET_DEFAULT; /* 2 */
    op.data = (unsigned char *)font_name;

    int success = 0;
    for (int i = 0; tty_devs[i]; i++) {
        int fd = open(tty_devs[i], O_RDWR);
        if (fd >= 0) {
            if (ioctl(fd, KDFONTOP, &op) == 0) {
                success = 1;
            }
            close(fd);
        }
    }

    return success ? 0 : 1;
}
