/*
 * RezzOS Alpine Client (apk) - порт shell-скрипта на C.
 *
 * Сборка:   gcc -O2 -Wall -o apk apk.c
 *           (для минимальной системы лучше статически: gcc -O2 -static -o apk apk.c)
 *
 * Внешние программы, которые нужны во время работы: wget, tar, cp (BusyBox ок).
 * Всё остальное (парсинг APKINDEX, зависимости, список установленных,
 * удаление, прогресс-бар) сделано на C.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define CACHE_DIR   "/var/cache/apk"
#define PKG_DIR     "/mnt/disk/apk-packages"
#define DL_DIR      "/tmp/apk_dl"
#define ALPINE_VER  "v3.20"
#define ARCH        "x86_64"
#define REPO_BASE   "https://dl-cdn.alpinelinux.org/alpine/" ALPINE_VER
#define BAR_WIDTH   30
#define DL_LOG      "/tmp/apk_wget.log"

#define C_BLUE   "\033[0;34m"
#define C_GREEN  "\033[0;32m"
#define C_RED    "\033[0;31m"
#define C_YELLOW "\033[1;33m"
#define C_NC     "\033[0m"

static const char *REPO_URL[2]  = { REPO_BASE "/main/" ARCH, REPO_BASE "/community/" ARCH };
static const char *REPO_NAME[2] = { "main", "community" };

static char installed_list[PATH_MAX];

/* ------------------------------------------------------------------ */
/* Утилиты                                                            */
/* ------------------------------------------------------------------ */

static void *xrealloc(void *p, size_t n)
{
    void *r = realloc(p, n);
    if (!r) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return r;
}

static char *xstrdup(const char *s)
{
    char *r = strdup(s);
    if (!r) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return r;
}

static bool ends_with(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}

static bool path_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

static bool is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static long file_size(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 ? (long)st.st_size : 0;
}

/* mkdir -p */
static bool mkdir_p(const char *path)
{
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
    return is_dir(tmp);
}

static char *read_all_fd(int fd)
{
    size_t cap = 4096, len = 0;
    char *b = xrealloc(NULL, cap);
    for (;;) {
        if (len + 1 >= cap) {
            cap *= 2;
            b = xrealloc(b, cap);
        }
        ssize_t n = read(fd, b + len, cap - len - 1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (n == 0)
            break;
        len += (size_t)n;
    }
    b[len] = '\0';
    return b;
}

static char *read_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    char *b = read_all_fd(fd);
    close(fd);
    return b;
}

static bool copy_file(const char *src, const char *dst)
{
    int in = open(src, O_RDONLY);
    if (in < 0)
        return false;
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) {
        close(in);
        return false;
    }
    char buf[65536];
    bool ok = true;
    for (;;) {
        ssize_t n = read(in, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            ok = false;
            break;
        }
        if (n == 0)
            break;
        char *p = buf;
        while (n > 0) {
            ssize_t w = write(out, p, (size_t)n);
            if (w < 0) {
                if (errno == EINTR)
                    continue;
                ok = false;
                goto done;
            }
            p += w;
            n -= w;
        }
    }
done:
    close(in);
    if (close(out) != 0)
        ok = false;
    return ok;
}

static int rm_cb(const char *p, const struct stat *sb, int flag, struct FTW *f)
{
    (void)sb; (void)flag; (void)f;
    remove(p);
    return 0;
}

static void rmtree(const char *p)
{
    nftw(p, rm_cb, 16, FTW_DEPTH | FTW_PHYS);
}

/* Запуск программы без shell. Возвращает код выхода или -1. */
static int run(char *const argv[], bool quiet)
{
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        if (quiet) {
            int fd = open("/dev/null", O_WRONLY);
            if (fd >= 0) {
                dup2(fd, 1);
                dup2(fd, 2);
                close(fd);
            }
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    int st;
    while (waitpid(pid, &st, 0) < 0)
        if (errno != EINTR)
            return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* Запуск с перехватом stdout (и stderr, если merge_err). */
static int capture(char *const argv[], bool merge_err, char **out)
{
    int p[2];
    if (pipe(p) < 0) {
        *out = xstrdup("");
        return -1;
    }
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        close(p[0]);
        close(p[1]);
        *out = xstrdup("");
        return -1;
    }
    if (pid == 0) {
        close(p[0]);
        dup2(p[1], 1);
        if (merge_err) {
            dup2(p[1], 2);
        } else {
            int fd = open("/dev/null", O_WRONLY);
            if (fd >= 0) {
                dup2(fd, 2);
                close(fd);
            }
        }
        close(p[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(p[1]);
    *out = read_all_fd(p[0]);
    close(p[0]);
    int st;
    while (waitpid(pid, &st, 0) < 0)
        if (errno != EINTR)
            return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* ------------------------------------------------------------------ */
/* Загрузка файлов с прогресс-баром                                   */
/* ------------------------------------------------------------------ */

static void draw_bar(long cur, long total)
{
    if (total > 0) {
        int percent = (int)(cur * 100 / total);
        if (percent > 100) percent = 100;
        if (percent < 0) percent = 0;
        int filled = percent * BAR_WIDTH / 100;
        int empty = BAR_WIDTH - filled;
        char bar[BAR_WIDTH + 2];
        int k = 0;
        for (int i = 0; i < filled; i++)
            bar[k++] = '=';
        if (filled < BAR_WIDTH) {
            bar[k++] = '>';
            empty--;
        }
        while (empty-- > 0)
            bar[k++] = '-';
        bar[k] = '\0';
        printf("\r[%s] %3d%%", bar, percent);
    } else {
        printf("\rDownloaded: %ld bytes", cur);
    }
    fflush(stdout);
}

static void print_wget_error(void)
{
    char *log = read_file(DL_LOG);
    if (!log)
        return;
    /* последняя непустая "строка" (wget рисует прогресс через \r) */
    char *last = NULL;
    for (char *l = strtok(log, "\n"); l; l = strtok(NULL, "\n")) {
        char *cr = strrchr(l, '\r');
        if (cr)
            l = cr + 1;
        if (*l)
            last = l;
    }
    if (last)
        printf(C_RED "wget: %s" C_NC "\n", last);
    free(log);
}

static bool download_file(const char *url, const char *output)
{
    long total = 0;
    char *hdr;
    char *spider[] = { "wget", "-S", "--spider", "--no-check-certificate", (char *)url, NULL };

    capture(spider, true, &hdr);
    for (char *l = strtok(hdr, "\n"); l; l = strtok(NULL, "\n")) {
        while (isspace((unsigned char)*l))
            l++;
        if (strncasecmp(l, "content-length:", 15) == 0)
            total = atol(l + 15);
    }
    free(hdr);

    unlink(output);

    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0)
        return false;
    if (pid == 0) {
        int fd = open(DL_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            dup2(fd, 1);
            dup2(fd, 2);
            close(fd);
        }
        execlp("wget", "wget", "--no-check-certificate", url, "-O", output, (char *)NULL);
        dprintf(2, "cannot execute wget: %s\n", strerror(errno));
        _exit(127);
    }

    bool ok = false;
    for (;;) {
        int st = 0;
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) {
            ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
            break;
        }
        if (r < 0 && errno == ECHILD) {
            ok = file_size(output) > 0;   /* статус потерян, судим по файлу */
            break;
        }
        if (r < 0 && errno != EINTR)
            break;
        draw_bar(file_size(output), total);
        usleep(250000);
    }

    long cur = file_size(output);
    if (ok && cur > 0) {
        if (total > 0) {
            printf("\r[");
            for (int i = 0; i < BAR_WIDTH; i++)
                putchar('=');
            printf("] 100%%\n");
        } else {
            printf("\rDownloaded: %ld bytes\n", cur);
        }
        return true;
    }

    printf("\n");
    print_wget_error();
    unlink(output);
    return false;
}

/* ------------------------------------------------------------------ */
/* APKINDEX                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    char *P, *V, *T, *U, *L, *A, *S, *I, *m, *D;
} Rec;

typedef struct {
    char *buf;
    Rec *recs;
    size_t n;
    bool tried;
} Index;

static Index IDX[2];

/* Загружает и разбирает индекс (один раз). NULL, если файла нет. */
static Index *idx_get(int i)
{
    Index *x = &IDX[i];
    if (x->tried)
        return x->buf ? x : NULL;
    x->tried = true;

    char path[PATH_MAX];
    snprintf(path, sizeof path, CACHE_DIR "/APKINDEX_%s", REPO_NAME[i]);
    x->buf = read_file(path);
    if (!x->buf)
        return NULL;

    size_t cap = 0;
    char *p = x->buf;
    while (*p) {
        while (*p == '\n')
            p++;
        if (!*p)
            break;

        Rec r;
        memset(&r, 0, sizeof r);
        while (*p && *p != '\n') {
            char *line = p;
            char *e = strchr(p, '\n');
            if (e) {
                *e = '\0';
                p = e + 1;
            } else {
                p += strlen(p);
            }
            size_t ll = strlen(line);
            if (ll && line[ll - 1] == '\r')
                line[ll - 1] = '\0';
            if (line[0] && line[1] == ':') {
                char *v = line + 2;
                switch (line[0]) {
                case 'P': r.P = v; break;
                case 'V': r.V = v; break;
                case 'T': r.T = v; break;
                case 'U': r.U = v; break;
                case 'L': r.L = v; break;
                case 'A': r.A = v; break;
                case 'S': r.S = v; break;
                case 'I': r.I = v; break;
                case 'm': r.m = v; break;
                case 'D': r.D = v; break;
                default: break;
                }
            }
        }
        if (!r.P)
            continue;
        if (x->n == cap) {
            cap = cap ? cap * 2 : 1024;
            x->recs = xrealloc(x->recs, cap * sizeof(Rec));
        }
        x->recs[x->n++] = r;
    }
    return x;
}

static Rec *find_rec(const char *name, int *repo)
{
    for (int i = 0; i < 2; i++) {
        Index *x = idx_get(i);
        if (!x)
            continue;
        for (size_t k = 0; k < x->n; k++) {
            if (strcmp(x->recs[k].P, name) == 0) {
                if (repo)
                    *repo = i;
                return &x->recs[k];
            }
        }
    }
    return NULL;
}

/* name -> "name-ver.apk" */
static bool find_pkg(const char *name, char *out, size_t outsz, int *repo)
{
    for (int i = 0; i < 2; i++) {
        Index *x = idx_get(i);
        if (!x)
            continue;
        for (size_t k = 0; k < x->n; k++) {
            Rec *r = &x->recs[k];
            if (strcmp(r->P, name) == 0 && r->V && *r->V) {
                snprintf(out, outsz, "%s-%s.apk", name, r->V);
                if (repo)
                    *repo = i;
                return true;
            }
        }
    }
    return false;
}

/* Превращает запись зависимости в имя пакета; false - пропустить. */
static bool dep_to_name(const char *dep, const char *self, char *out, size_t sz)
{
    static const char *skip[] = { "so:", "cmd:", "pc:", "dbus:", "/bin/", "/sbin/", "/usr/" };
    for (size_t i = 0; i < sizeof skip / sizeof *skip; i++)
        if (strncmp(dep, skip[i], strlen(skip[i])) == 0)
            return false;

    size_t n = strcspn(dep, "<>=~");
    if (n == 0 || n >= sz)
        return false;
    memcpy(out, dep, n);
    out[n] = '\0';

    if (!isalpha((unsigned char)out[0]))   /* в т.ч. "!conflict" */
        return false;
    if (strchr(out, '/'))
        return false;
    if (strcmp(out, self) == 0)
        return false;
    return true;
}

static void get_deps(const char *name, char ***out, size_t *n)
{
    *out = NULL;
    *n = 0;
    Rec *r = find_rec(name, NULL);
    if (!r || !r->D)
        return;

    size_t cap = 0;
    char *copy = xstrdup(r->D), *sp = NULL;
    for (char *tok = strtok_r(copy, " ", &sp); tok; tok = strtok_r(NULL, " ", &sp)) {
        char dn[256];
        if (!dep_to_name(tok, name, dn, sizeof dn))
            continue;
        if (*n == cap) {
            cap = cap ? cap * 2 : 8;
            *out = xrealloc(*out, cap * sizeof(char *));
        }
        (*out)[(*n)++] = xstrdup(dn);
    }
    free(copy);
}

/* ------------------------------------------------------------------ */
/* Список установленных пакетов                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    char *buf;
    char **v;
    size_t n;
} Lines;

static void split_lines(Lines *L, char *buf)
{
    L->buf = buf;
    L->v = NULL;
    L->n = 0;
    size_t cap = 0;
    char *p = buf;
    while (*p) {
        char *e = strchr(p, '\n');
        char *next;
        if (e) {
            *e = '\0';
            next = e + 1;
        } else {
            next = p + strlen(p);
        }
        size_t ll = strlen(p);
        if (ll && p[ll - 1] == '\r')
            p[ll - 1] = '\0';
        if (*p) {
            if (L->n == cap) {
                cap = cap ? cap * 2 : 64;
                L->v = xrealloc(L->v, cap * sizeof(char *));
            }
            L->v[L->n++] = p;
        }
        p = next;
    }
}

static void free_lines(Lines *L)
{
    free(L->v);
    free(L->buf);
    L->v = NULL;
    L->buf = NULL;
    L->n = 0;
}

static int str_cmp(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static Lines load_list(void)
{
    Lines L;
    char *buf = read_file(installed_list);
    if (!buf)
        buf = xstrdup("");
    split_lines(&L, buf);
    return L;
}

/* "foo-1.2.3-r0.apk" -> "foo" */
static bool apk_name(const char *apk, char *out, size_t sz)
{
    size_t len = strlen(apk);
    if (len < 5 || strcmp(apk + len - 4, ".apk") != 0 || len - 4 >= PATH_MAX)
        return false;

    char tmp[PATH_MAX];
    memcpy(tmp, apk, len - 4);
    tmp[len - 4] = '\0';

    char *r = strrchr(tmp, '-');
    if (!r || r[1] != 'r' || !isdigit((unsigned char)r[2]) ||
        strspn(r + 2, "0123456789") != strlen(r + 2))
        return false;
    *r = '\0';

    char *v = strrchr(tmp, '-');
    if (!v)
        return false;
    *v = '\0';
    if (!*tmp || strlen(tmp) >= sz)
        return false;
    strcpy(out, tmp);
    return true;
}

static bool is_installed(const char *name)
{
    Lines L = load_list();
    bool found = false;
    for (size_t i = 0; i < L.n && !found; i++) {
        char nm[256];
        if (apk_name(L.v[i], nm, sizeof nm) && strcmp(nm, name) == 0)
            found = true;
    }
    free_lines(&L);
    return found;
}

static bool list_contains(const char *apk)
{
    Lines L = load_list();
    bool found = false;
    for (size_t i = 0; i < L.n && !found; i++)
        found = strcmp(L.v[i], apk) == 0;
    free_lines(&L);
    return found;
}

static bool list_append(const char *apk)
{
    FILE *f = fopen(installed_list, "a");
    if (!f)
        return false;
    bool ok = fprintf(f, "%s\n", apk) > 0;
    if (fclose(f) != 0)
        ok = false;
    return ok;
}

static void list_remove(const char *apk)
{
    Lines L = load_list();
    FILE *f = fopen(installed_list, "w");
    if (f) {
        for (size_t i = 0; i < L.n; i++)
            if (strcmp(L.v[i], apk) != 0)
                fprintf(f, "%s\n", L.v[i]);
        fclose(f);
    }
    free_lines(&L);
}

/* ------------------------------------------------------------------ */
/* update                                                             */
/* ------------------------------------------------------------------ */

static void update_index(void)
{
    for (int i = 0; i < 2; i++) {
        printf("Fetching %s index... \n", REPO_NAME[i]);

        char url[1024], gz[PATH_MAX], raw[PATH_MAX], dst[PATH_MAX];
        snprintf(url, sizeof url, "%s/APKINDEX.tar.gz", REPO_URL[i]);
        snprintf(gz, sizeof gz, CACHE_DIR "/APKINDEX_%s.tar.gz", REPO_NAME[i]);
        snprintf(raw, sizeof raw, CACHE_DIR "/APKINDEX");
        snprintf(dst, sizeof dst, CACHE_DIR "/APKINDEX_%s", REPO_NAME[i]);

        bool ok = false;
        if (download_file(url, gz)) {
            char *tarv[] = { "tar", "-xzf", gz, "-C", CACHE_DIR, NULL };
            int trc = run(tarv, false);
            if (rename(raw, dst) == 0) {
                ok = true;
            } else {
                printf(C_RED "tar exit code %d, no APKINDEX extracted (%s)" C_NC "\n",
                       trc, strerror(errno));
            }
        }
        printf(ok ? C_GREEN "OK" C_NC "\n" : C_RED "FAILED" C_NC "\n");
    }
}

/* ------------------------------------------------------------------ */
/* search / info                                                      */
/* ------------------------------------------------------------------ */

static bool search_pkg(const char *q)
{
    if (!q || !*q) {
        printf(C_RED "Specify something to search for." C_NC "\n");
        return false;
    }
    printf(C_YELLOW "Searching for '%s'..." C_NC "\n", q);

    char **lines = NULL;
    size_t n = 0, cap = 0;

    for (int i = 0; i < 2; i++) {
        Index *x = idx_get(i);
        if (!x)
            continue;
        for (size_t k = 0; k < x->n; k++) {
            Rec *r = &x->recs[k];
            if (!strcasestr(r->P, q))
                continue;
            char line[2048];
            snprintf(line, sizeof line, "%-34s %-20s %s", r->P,
                     r->V ? r->V : "", r->T ? r->T : "");
            if (n == cap) {
                cap = cap ? cap * 2 : 64;
                lines = xrealloc(lines, cap * sizeof(char *));
            }
            lines[n++] = xstrdup(line);
        }
    }

    if (n == 0) {
        printf(C_RED "No package matches '%s'." C_NC "\n", q);
        printf("Run 'apk update' first if you have not refreshed the indexes.\n");
        return false;
    }

    qsort(lines, n, sizeof(char *), str_cmp);
    size_t shown = 0;
    for (size_t i = 0; i < n; i++) {
        if (i > 0 && strcmp(lines[i], lines[i - 1]) == 0)
            continue;
        puts(lines[i]);
        shown++;
    }
    printf("\n" C_GREEN "%zu package(s) found." C_NC "\n", shown);

    for (size_t i = 0; i < n; i++)
        free(lines[i]);
    free(lines);
    return true;
}

static void human(const char *s, char *out, size_t sz)
{
    double b = s ? atof(s) : 0;
    if (b <= 0)
        snprintf(out, sz, "unknown");
    else if (b >= 1048576)
        snprintf(out, sz, "%.1f MiB", b / 1048576);
    else if (b >= 1024)
        snprintf(out, sz, "%.1f KiB", b / 1024);
    else
        snprintf(out, sz, "%ld B", (long)b);
}

static void show(const char *label, const char *value)
{
    if (value && *value)
        printf("  %-13s %s\n", label, value);
}

static bool info_pkg(const char *name)
{
    if (!name || !*name) {
        printf(C_RED "Specify a package." C_NC "\n");
        return false;
    }

    int repo = 0;
    Rec *r = find_rec(name, &repo);
    if (!r) {
        printf(C_RED "No package named '%s'." C_NC "\n", name);
        printf("Try 'apk search %s', or 'apk update' if the indexes are stale.\n", name);
        return false;
    }

    char dl[64], inst[64];
    human(r->S, dl, sizeof dl);
    human(r->I, inst, sizeof inst);

    printf("\n  %s %s\n\n", r->P, r->V ? r->V : "");
    show("Description:", r->T);
    show("Repository:", REPO_NAME[repo]);
    show("Architecture:", r->A);
    show("License:", r->L);
    show("Homepage:", r->U);
    show("Maintainer:", r->m);
    show("Download:", dl);
    show("Installed:", inst);

    if (!r->D || !*r->D) {
        printf("  %-13s none\n", "Depends on:");
    } else {
        char line[512] = "";
        const char *label = "Depends on:";
        char *copy = xstrdup(r->D), *sp = NULL;
        for (char *tok = strtok_r(copy, " ", &sp); tok; tok = strtok_r(NULL, " ", &sp)) {
            if (line[0] && strlen(line) + strlen(tok) + 1 > 62) {
                printf("  %-13s %s\n", label, line);
                label = "";
                line[0] = '\0';
            }
            size_t l = strlen(line);
            snprintf(line + l, sizeof line - l, "%s%s", l ? " " : "", tok);
        }
        if (line[0])
            printf("  %-13s %s\n", label, line);
        free(copy);
    }
    printf("\n");
    return true;
}

/* ------------------------------------------------------------------ */
/* install                                                            */
/* ------------------------------------------------------------------ */

static bool install_single_pkg(const char *pkg)
{
    char apk[512];
    int repo = 0;

    if (ends_with(pkg, ".apk")) {
        if (strchr(pkg, '/') || strlen(pkg) >= sizeof apk) {
            printf(C_RED "Invalid package file name: %s" C_NC "\n", pkg);
            return false;
        }
        snprintf(apk, sizeof apk, "%s", pkg);
    } else if (!find_pkg(pkg, apk, sizeof apk, &repo)) {
        printf(C_RED "Not found: %s" C_NC "\n", pkg);
        return false;
    }

    printf(C_YELLOW "Installing %s..." C_NC "\n", apk);
    mkdir_p(PKG_DIR);
    mkdir_p(DL_DIR);

    char dl[PATH_MAX], url[1024];
    snprintf(dl, sizeof dl, DL_DIR "/%s", apk);

    bool ok = false;
    for (int k = 0; k < 2 && !ok; k++) {
        int r = k ? 1 - repo : repo;
        snprintf(url, sizeof url, "%s/%s", REPO_URL[r], apk);
        ok = download_file(url, dl);
    }
    if (!ok || file_size(dl) <= 0) {
        printf(C_RED "Download failed for %s" C_NC "\n", apk);
        return false;
    }

    char tmpl[] = "/tmp/apk_extract.XXXXXX";
    if (!mkdtemp(tmpl)) {
        printf(C_RED "Cannot create temp dir" C_NC "\n");
        return false;
    }

    bool result = false;

    char *tarv[] = { "tar", "-xzf", dl, "-C", tmpl, NULL };
    if (run(tarv, false) != 0) {
        printf(C_RED "Failed to extract %s" C_NC "\n", apk);
        goto cleanup;
    }

    static const char *dirs[] = { "usr", "lib", "bin", "sbin" };
    for (size_t i = 0; i < sizeof dirs / sizeof *dirs; i++) {
        char src[PATH_MAX], dst[PATH_MAX], chk[PATH_MAX];
        snprintf(chk, sizeof chk, "%s/%s", tmpl, dirs[i]);
        if (!is_dir(chk))
            continue;
        snprintf(src, sizeof src, "%s/%s/.", tmpl, dirs[i]);
        snprintf(dst, sizeof dst, "/%s/", dirs[i]);
        mkdir_p(dst);
        char *cpv[] = { "cp", "-a", src, dst, NULL };
        if (run(cpv, false) != 0) {
            printf(C_RED "Failed to install %s files from %s" C_NC "\n", dirs[i], apk);
            goto cleanup;
        }
    }

    char saved[PATH_MAX];
    snprintf(saved, sizeof saved, PKG_DIR "/%s", apk);
    if (!copy_file(dl, saved)) {
        printf(C_RED "Failed to save %s" C_NC "\n", apk);
        goto cleanup;
    }

    if (!list_contains(apk) && !list_append(apk)) {
        printf(C_RED "Failed to update installed package list" C_NC "\n");
        goto cleanup;
    }

    printf(C_GREEN "Done: %s" C_NC "\n", apk);
    result = true;

cleanup:
    rmtree(tmpl);
    unlink(dl);
    return result;
}

static char **visited;
static size_t nvisited;

static bool visited_has(const char *s)
{
    for (size_t i = 0; i < nvisited; i++)
        if (strcmp(visited[i], s) == 0)
            return true;
    return false;
}

static void visited_add(const char *s)
{
    visited = xrealloc(visited, (nvisited + 1) * sizeof(char *));
    visited[nvisited++] = xstrdup(s);
}

static bool install_deps_recursive(const char *name)
{
    char **deps;
    size_t n;
    get_deps(name, &deps, &n);

    bool ok = true;
    for (size_t i = 0; i < n && ok; i++) {
        const char *dep = deps[i];

        if (visited_has(dep) || is_installed(dep))
            continue;
        visited_add(dep);

        char tmp[512];
        if (!find_pkg(dep, tmp, sizeof tmp, NULL)) {
            /* виртуальные/provides-зависимости, которых нет как пакета */
            printf(C_YELLOW "Not in index, skipping dependency: %s" C_NC "\n", dep);
            continue;
        }

        if (!install_deps_recursive(dep)) {
            printf(C_RED "Failed to resolve dependency: %s" C_NC "\n", dep);
            ok = false;
            break;
        }

        printf("Installing dependency: %s\n", dep);
        if (!install_single_pkg(dep)) {
            printf(C_RED "Failed to install dependency: %s" C_NC "\n", dep);
            ok = false;
        }
    }

    for (size_t i = 0; i < n; i++)
        free(deps[i]);
    free(deps);
    return ok;
}

static bool install_with_deps(const char *name)
{
    char tmp[512];
    if (!find_pkg(name, tmp, sizeof tmp, NULL)) {
        printf(C_RED "Not found: %s" C_NC "\n", name);
        return false;
    }

    for (size_t i = 0; i < nvisited; i++)
        free(visited[i]);
    free(visited);
    visited = NULL;
    nvisited = 0;
    visited_add(name);

    if (!install_deps_recursive(name)) {
        printf(C_RED "Failed to install dependencies for %s" C_NC "\n", name);
        return false;
    }
    if (!install_single_pkg(name)) {
        printf(C_RED "Failed to install %s" C_NC "\n", name);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* upgrade                                                            */
/* ------------------------------------------------------------------ */

static bool upgrade_pkgs(void)
{
    if (file_size(installed_list) <= 0) {
        printf("No packages\n");
        return true;
    }
    if (!idx_get(0) && !idx_get(1)) {
        printf(C_RED "No package index. Run 'apk update' first." C_NC "\n");
        return false;
    }

    Lines L = load_list();
    qsort(L.v, L.n, sizeof(char *), str_cmp);

    int checked = 0, upgraded = 0, failed = 0;
    for (size_t i = 0; i < L.n; i++) {
        const char *apk = L.v[i];
        if (i > 0 && strcmp(apk, L.v[i - 1]) == 0)
            continue;

        char name[256], latest[512];
        if (!apk_name(apk, name, sizeof name))
            continue;
        checked++;

        if (!find_pkg(name, latest, sizeof latest, NULL)) {
            printf(C_YELLOW "Not in index, skipping: %s" C_NC "\n", name);
            continue;
        }
        if (strcmp(latest, apk) == 0)
            continue;

        printf(C_YELLOW "%s: %s -> %s" C_NC "\n", name, apk, latest);

        if (install_with_deps(name)) {
            list_remove(apk);
            char old[PATH_MAX];
            snprintf(old, sizeof old, PKG_DIR "/%s", apk);
            unlink(old);
            upgraded++;
        } else {
            failed++;
        }
    }
    free_lines(&L);

    printf("\n");
    if (upgraded == 0 && failed == 0)
        printf(C_GREEN "Everything is up to date (%d checked)" C_NC "\n", checked);
    else
        printf(C_GREEN "Checked %d, upgraded %d, failed %d" C_NC "\n", checked, upgraded, failed);

    return failed == 0;
}

/* ------------------------------------------------------------------ */
/* remove                                                             */
/* ------------------------------------------------------------------ */

static bool owns_removable_path(const char *f)
{
    /* ставим только в usr/ lib/ bin/ sbin/, значит и удаляем только там */
    return strncmp(f, "usr/", 4) == 0 || strncmp(f, "lib/", 4) == 0 ||
           strncmp(f, "bin/", 4) == 0 || strncmp(f, "sbin/", 5) == 0;
}

static bool remove_pkg(const char *pkg)
{
    char apk[512] = "";

    if (ends_with(pkg, ".apk")) {
        if (strchr(pkg, '/') || strlen(pkg) >= sizeof apk) {
            printf(C_RED "Invalid package file name: %s" C_NC "\n", pkg);
            return false;
        }
        snprintf(apk, sizeof apk, "%s", pkg);
    } else {
        Lines L = load_list();
        for (size_t i = 0; i < L.n; i++) {
            char nm[256];
            if (apk_name(L.v[i], nm, sizeof nm) && strcmp(nm, pkg) == 0) {
                snprintf(apk, sizeof apk, "%s", L.v[i]);
                break;
            }
        }
        free_lines(&L);
    }

    if (!apk[0]) {
        printf(C_RED "Not installed: %s" C_NC "\n", pkg);
        return false;
    }

    char apk_file[PATH_MAX];
    snprintf(apk_file, sizeof apk_file, PKG_DIR "/%s", apk);
    if (!path_exists(apk_file)) {
        printf(C_RED "Package file not found: %s" C_NC "\n", apk);
        list_remove(apk);
        return false;
    }

    printf(C_YELLOW "Removing %s..." C_NC "\n", apk);

    /* файлы, принадлежащие остальным установленным пакетам */
    char **kept = NULL;
    size_t nkept = 0, capkept = 0;
    Lines L = load_list();
    qsort(L.v, L.n, sizeof(char *), str_cmp);
    for (size_t i = 0; i < L.n; i++) {
        if (strcmp(L.v[i], apk) == 0)
            continue;
        if (i > 0 && strcmp(L.v[i], L.v[i - 1]) == 0)
            continue;
        char other[PATH_MAX];
        snprintf(other, sizeof other, PKG_DIR "/%s", L.v[i]);
        if (!path_exists(other))
            continue;

        char *tv[] = { "tar", "-tzf", other, NULL };
        char *out;
        capture(tv, false, &out);
        Lines T;
        split_lines(&T, out);   /* буфер намеренно не освобождаем: kept указывает в него */
        for (size_t k = 0; k < T.n; k++) {
            char *f = T.v[k];
            if (f[0] == '.' && f[1] == '/')
                f += 2;
            if (nkept == capkept) {
                capkept = capkept ? capkept * 2 : 1024;
                kept = xrealloc(kept, capkept * sizeof(char *));
            }
            kept[nkept++] = f;
        }
        free(T.v);
    }
    free_lines(&L);
    qsort(kept, nkept, sizeof(char *), str_cmp);

    /* файлы удаляемого пакета */
    char *tv[] = { "tar", "-tzf", apk_file, NULL };
    char *out;
    capture(tv, false, &out);
    Lines O;
    split_lines(&O, out);

    int shared = 0;
    for (size_t i = 0; i < O.n; i++) {
        const char *f = O.v[i];
        if (f[0] == '.' && f[1] == '/')
            f += 2;
        if (!*f || !owns_removable_path(f))
            continue;

        char target[PATH_MAX];
        snprintf(target, sizeof target, "/%s", f);

        struct stat st;
        if (lstat(target, &st) != 0 || S_ISDIR(st.st_mode))
            continue;

        if (nkept && bsearch(&f, kept, nkept, sizeof(char *), str_cmp)) {
            shared++;
            continue;
        }
        unlink(target);
    }
    free_lines(&O);
    free(kept);

    if (shared > 0)
        printf(C_YELLOW "Kept %d file(s) still owned by other packages" C_NC "\n", shared);

    unlink(apk_file);
    list_remove(apk);
    printf(C_GREEN "Removed: %s" C_NC "\n", apk);
    return true;
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */

static void usage(void)
{
    printf(C_BLUE "=== RezzOS Alpine Client (apk) ===" C_NC "\n");
    printf("Downloads packages directly from Alpine's real repositories.\n");
    printf("Usage:\n");
    printf("  apk update                 - Refresh package indexes\n");
    printf("  apk install <package>      - Install package\n");
    printf("  apk upgrade                - Upgrade all installed packages\n");
    printf("  apk search <query>         - Search for packages\n");
    printf("  apk info <package>         - Show package details\n");
    printf("  apk list                   - List installed packages\n");
    printf("  apk remove <package>       - Remove package\n");
}

int main(int argc, char **argv)
{
    /* если родитель оставил SIGCHLD в SIG_IGN, waitpid() вернёт ECHILD */
    signal(SIGCHLD, SIG_DFL);

    /* Где лежит список - решает факт монтирования диска, а не наличие каталога
     * (на свежеотформатированном диске каталога ещё нет). */
    char *mounts = read_file("/proc/mounts");
    if (mounts && strstr(mounts, " /mnt/disk ")) {
        mkdir_p(PKG_DIR);
        snprintf(installed_list, sizeof installed_list, "%s", PKG_DIR "/installed.list");
    } else {
        snprintf(installed_list, sizeof installed_list, "%s", "/var/log/apk-installed.list");
    }
    free(mounts);

    mkdir_p(CACHE_DIR);
    mkdir_p("/tmp/apk");

    const char *cmd = argc > 1 ? argv[1] : "";
    int rc = 0;

    if (strcmp(cmd, "update") == 0) {
        update_index();
    } else if (strcmp(cmd, "install") == 0) {
        if (argc < 3) {
            usage();
        } else {
            for (int i = 2; i < argc; i++)
                if (!install_with_deps(argv[i]))
                    rc = 1;
        }
    } else if (strcmp(cmd, "upgrade") == 0) {
        rc = upgrade_pkgs() ? 0 : 1;
    } else if (strcmp(cmd, "search") == 0) {
        rc = search_pkg(argc > 2 ? argv[2] : NULL) ? 0 : 1;
    } else if (strcmp(cmd, "info") == 0) {
        rc = info_pkg(argc > 2 ? argv[2] : NULL) ? 0 : 1;
    } else if (strcmp(cmd, "list") == 0) {
        char *buf = read_file(installed_list);
        if (buf && *buf)
            fputs(buf, stdout);
        else
            printf("No packages\n");
        free(buf);
    } else if (strcmp(cmd, "remove") == 0) {
        if (argc < 3) {
            printf("Specify package\n");
            rc = 1;
        } else {
            for (int i = 2; i < argc; i++)
                if (!remove_pkg(argv[i]))
                    rc = 1;
        }
    } else {
        usage();
    }
    return rc;
}
