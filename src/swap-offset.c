#define SYS_write 1
#define SYS_open 2
#define SYS_close 3
#define SYS_ioctl 16
#define SYS_exit 60

#define O_RDONLY 0
#define FS_IOC_FIEMAP 0xc020660b

struct fiemap_extent {
    unsigned long long fe_logical;
    unsigned long long fe_physical;
    unsigned long long fe_length;
    unsigned long long fe_reserved64[2];
    unsigned int fe_flags;
    unsigned int fe_reserved[3];
};

struct fiemap {
    unsigned long long fm_start;
    unsigned long long fm_length;
    unsigned int fm_flags;
    unsigned int fm_mapped_extents;
    unsigned int fm_extent_count;
    unsigned int fm_reserved;
    struct fiemap_extent fm_extents[1];
};

static long syscall1(long n, long a1) {
    long ret;
    __asm__ __volatile__("syscall" : "=a"(ret) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

static long syscall2(long n, long a1, long a2) {
    long ret;
    __asm__ __volatile__("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return ret;
}

static long syscall3(long n, long a1, long a2, long a3) {
    long ret;
    __asm__ __volatile__("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

static void u64_to_str(unsigned long long val, char *buf) {
    char tmp[32];
    int i = 0;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\n';
        buf[2] = '\0';
        return;
    }
    while (val > 0) {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    }
    int j = 0;
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j++] = '\n';
    buf[j] = '\0';
}

static int str_len(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "/mnt/disk/swapfile";
    long fd = syscall2(SYS_open, (long)path, O_RDONLY);
    if (fd < 0) {
        return 1;
    }

    struct fiemap fm;
    char *p = (char*)&fm;
    for (int i = 0; i < sizeof(fm); i++) p[i] = 0;

    fm.fm_start = 0;
    fm.fm_length = 4096;
    fm.fm_flags = 0;
    fm.fm_extent_count = 1;

    long res = syscall3(SYS_ioctl, fd, FS_IOC_FIEMAP, (long)&fm);
    syscall1(SYS_close, fd);

    if (res == 0 && fm.fm_mapped_extents > 0) {
        unsigned long long phys_offset = fm.fm_extents[0].fe_physical / 4096;
        char out[32];
        u64_to_str(phys_offset, out);
        syscall3(SYS_write, 1, (long)out, str_len(out));
        return 0;
    }

    return 1;
}

__attribute__((naked)) void _start(void) {
    __asm__ __volatile__(
        "mov (%rsp), %rdi\n"
        "lea 8(%rsp), %rsi\n"
        "call main\n"
        "mov %rax, %rdi\n"
        "mov $60, %rax\n"
        "syscall\n"
    );
}
