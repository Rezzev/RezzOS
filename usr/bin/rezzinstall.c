/*
 * rezzinstall.c — single-file GTK3 installer wizard for RezzOS.
 *
 * Contains both the GUI and the non-interactive backend (as a shell
 * script embedded below). On startup the backend is written to
 * /run/rezzinstall-backend.sh with mode 0700, invoked with the collected
 * configuration passed via environment variables (never argv, so
 * passwords never appear in `ps`), and removed on exit.
 *
 * Build:
 *   gcc -O2 -Wall -Wextra -o rezzinstall rezzinstall.c \
 *       $(pkg-config --cflags --libs gtk+-3.0)
 *
 * Must be run as root. Depends at runtime on: sh, cp, dd, sync, sfdisk,
 * grub-install, mkfs.ext4, blkid, mount, umount (mkdosfs only for UEFI),
 * and one of cryptpw/mkpasswd/openssl for password hashing.
 */

#include <gtk/gtk.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ====================================================================== */
/* Embedded backend script                                                */
/* ====================================================================== */

/* NOTE: The only C-escape used is \" for shell double quotes. The shell
 * script itself contains no backslashes — printf-style \n is avoided by
 * using echo, and sed/awk scripts are written without backrefs. */

static const char backend_script[] =
"#!/bin/sh\n"
"set -e\n"
"\n"
"TARGET_MOUNT=/tmp/rezz_install_target\n"
"\n"
"note()   { echo \"==> $*\"; }\n"
"detail() { echo \"    $*\"; }\n"
"die()    { echo \"rezzinstall-backend: ERROR: $*\" >&2; exit 1; }\n"
"\n"
"cleanup() {\n"
"    umount -f \"$TARGET_MOUNT/boot/efi\" 2>/dev/null || true\n"
"    umount -f \"$TARGET_MOUNT\" 2>/dev/null || true\n"
"}\n"
"trap cleanup EXIT\n"
"\n"
"[ \"$(id -u)\" = \"0\" ] || die \"must be run as root\"\n"
"\n"
": \"${CHOSEN_DISK:?CHOSEN_DISK is not set}\"\n"
": \"${CFG_HOSTNAME:?CFG_HOSTNAME is not set}\"\n"
": \"${CFG_ROOT_PASS:?CFG_ROOT_PASS is not set}\"\n"
": \"${CFG_USERNAME:?CFG_USERNAME is not set}\"\n"
": \"${CFG_USER_PASS:?CFG_USER_PASS is not set}\"\n"
": \"${CFG_KEYMAP:?CFG_KEYMAP is not set}\"\n"
": \"${CFG_FONT:?CFG_FONT is not set}\"\n"
"\n"
"[ -b \"$CHOSEN_DISK\" ] || die \"$CHOSEN_DISK is not a block device\"\n"
"\n"
"case \"$DATA_SECTORS\" in\n"
"    ''|*[!0-9]*) die \"DATA_SECTORS must be a non-negative integer (got '$DATA_SECTORS')\" ;;\n"
"esac\n"
"\n"
"missing=\n"
"for cmd in cp dd sync sfdisk grub-install mkfs.ext4 blkid mount umount; do\n"
"    command -v \"$cmd\" >/dev/null 2>&1 || missing=\"$missing $cmd\"\n"
"done\n"
"[ -z \"$missing\" ] || die \"missing essential tools:$missing\"\n"
"\n"
"if [ -d /sys/firmware/efi ]; then\n"
"    command -v mkdosfs >/dev/null 2>&1 || die \"missing mkdosfs (required for UEFI install)\"\n"
"fi\n"
"\n"
"HASH_CMD=\n"
"if command -v cryptpw >/dev/null 2>&1; then HASH_CMD=cryptpw\n"
"elif command -v mkpasswd >/dev/null 2>&1; then HASH_CMD=mkpasswd\n"
"elif command -v openssl  >/dev/null 2>&1 && openssl passwd -6 x >/dev/null 2>&1; then HASH_CMD=openssl\n"
"else die \"no password hashing tool found\"\n"
"fi\n"
"\n"
"hash_password() {\n"
"    case \"$HASH_CMD\" in\n"
"        cryptpw)  echo \"$1\" | cryptpw -m sha-512 2>/dev/null || echo \"$1\" | cryptpw ;;\n"
"        mkpasswd) mkpasswd -m sha-512 \"$1\" ;;\n"
"        openssl)  openssl passwd -6 \"$1\" ;;\n"
"    esac\n"
"}\n"
"\n"
"if [ -d /sys/firmware/efi ]; then FW=uefi; else FW=bios; fi\n"
"note \"Firmware: $FW\"\n"
"\n"
"DATA_SRC=$(awk '$2==\"/mnt/disk\" {print $1; exit}' /proc/mounts 2>/dev/null || true)\n"
"if [ -n \"$DATA_SRC\" ]; then\n"
"    DATA_PARENT=$(echo \"$DATA_SRC\" | sed 's/[0-9]*$//; s/p$//')\n"
"    [ \"$DATA_PARENT\" = \"$CHOSEN_DISK\" ] && die \"/mnt/disk lives on $CHOSEN_DISK. Reinstall from bootable media instead.\"\n"
"fi\n"
"\n"
"TOTAL_SECTORS=$(cat \"/sys/block/$(basename \"$CHOSEN_DISK\")/size\" 2>/dev/null || echo 0)\n"
"[ \"$TOTAL_SECTORS\" -gt 0 ] 2>/dev/null || die \"cannot read size of $CHOSEN_DISK\"\n"
"\n"
"if [ \"$FW\" = \"uefi\" ]; then\n"
"    ESP_SECTORS=524288\n"
"    USED=$((ESP_SECTORS + 2048))\n"
"else\n"
"    ESP_SECTORS=0\n"
"    USED=2048\n"
"fi\n"
"AVAILABLE=$((TOTAL_SECTORS - USED))\n"
"[ \"$AVAILABLE\" -gt 0 ] || die \"disk is too small\"\n"
"[ \"$DATA_SECTORS\" -gt 0 ] 2>/dev/null || DATA_SECTORS=$((AVAILABLE / 2))\n"
"SAFETY_SECTORS=2048\n"
"ROOT_SECTORS=$((AVAILABLE - DATA_SECTORS - SAFETY_SECTORS))\n"
"[ \"$ROOT_SECTORS\" -ge $((2 * 2097152)) ] 2>/dev/null || die \"root partition would be < 2 GB; shrink the data partition\"\n"
"\n"
"case \"$CHOSEN_DISK\" in\n"
"    *[0-9]) SEP=p ;;\n"
"    *)      SEP= ;;\n"
"esac\n"
"\n"
"note \"[1/9] Unmounting anything currently using $CHOSEN_DISK\"\n"
"for mnt_pt in $(awk -v d=\"$CHOSEN_DISK\" 'index($1, d) == 1 {print $2}' /proc/mounts); do\n"
"    umount -f \"$mnt_pt\" 2>/dev/null || true\n"
"done\n"
"for p in \"${CHOSEN_DISK}\"* \"${CHOSEN_DISK}p\"*; do\n"
"    [ -b \"$p\" ] || continue\n"
"    umount -f \"$p\" 2>/dev/null || true\n"
"done\n"
"umount -f \"$TARGET_MOUNT\" 2>/dev/null || true\n"
"mkdir -p \"$TARGET_MOUNT\"\n"
"\n"
"note \"[2/9] Writing partition table to $CHOSEN_DISK\"\n"
"dd if=/dev/zero of=\"$CHOSEN_DISK\" bs=512 count=2048 2>/dev/null || true\n"
"sync\n"
"\n"
"PART_SCRIPT=/tmp/rezz-part.sfdisk\n"
"if [ \"$FW\" = \"uefi\" ]; then\n"
"    cat > \"$PART_SCRIPT\" <<EOF\n"
"label: gpt\n"
"name=esp, size=$ESP_SECTORS, type=U\n"
"name=root, size=$ROOT_SECTORS, type=L\n"
"name=data, type=L\n"
"EOF\n"
"else\n"
"    cat > \"$PART_SCRIPT\" <<EOF\n"
"label: dos\n"
"start=2048, size=$ROOT_SECTORS, type=83, bootable\n"
"type=83\n"
"EOF\n"
"fi\n"
"\n"
"if ! SFD_OUT=$(sfdisk -f \"$CHOSEN_DISK\" < \"$PART_SCRIPT\" 2>&1); then\n"
"    echo \"$SFD_OUT\" >&2\n"
"    echo \"--- partition script was: ---\" >&2\n"
"    cat \"$PART_SCRIPT\" >&2\n"
"    die \"partitioning failed\"\n"
"fi\n"
"sync\n"
"blockdev --rereadpt \"$CHOSEN_DISK\" 2>/dev/null || true\n"
"sleep 1\n"
"timeout 5 udevadm settle 2>/dev/null || true\n"
"\n"
"if [ \"$FW\" = \"uefi\" ]; then\n"
"    ESP_PART=\"${CHOSEN_DISK}${SEP}1\"\n"
"    ROOT_PART=\"${CHOSEN_DISK}${SEP}2\"\n"
"    DATA_PART=\"${CHOSEN_DISK}${SEP}3\"\n"
"else\n"
"    ROOT_PART=\"${CHOSEN_DISK}${SEP}1\"\n"
"    DATA_PART=\"${CHOSEN_DISK}${SEP}2\"\n"
"fi\n"
"\n"
"wait_ready() {\n"
"    n=$1; i=0; sz=\n"
"    while [ \"$i\" -lt 20 ]; do\n"
"        if [ -b \"$n\" ]; then\n"
"            sz=$(cat \"/sys/class/block/$(basename \"$n\")/size\" 2>/dev/null || echo 0)\n"
"            [ \"$sz\" -gt 0 ] 2>/dev/null && return 0\n"
"        fi\n"
"        sleep 0.5; i=$((i + 1))\n"
"    done\n"
"    die \"partition $n never became ready\"\n"
"}\n"
"wait_ready \"$ROOT_PART\"\n"
"wait_ready \"$DATA_PART\"\n"
"[ \"$FW\" = \"uefi\" ] && wait_ready \"$ESP_PART\"\n"
"\n"
"note \"[3/9] Creating filesystems\"\n"
"if ! MKFS_OUT=$(mkfs.ext4 -F -L REZZOS_ROOT \"$ROOT_PART\" 2>&1); then\n"
"    echo \"$MKFS_OUT\" >&2; die \"mkfs.ext4 on $ROOT_PART\"\n"
"fi\n"
"if ! MKFS_OUT=$(mkfs.ext4 -F -L REZZOS_DATA \"$DATA_PART\" 2>&1); then\n"
"    echo \"$MKFS_OUT\" >&2; die \"mkfs.ext4 on $DATA_PART\"\n"
"fi\n"
"if [ \"$FW\" = \"uefi\" ]; then\n"
"    if ! MKFS_OUT=$(mkdosfs -n REZZOS_EFI -F32 \"$ESP_PART\" 2>&1); then\n"
"        echo \"$MKFS_OUT\" >&2; die \"mkdosfs on $ESP_PART\"\n"
"    fi\n"
"fi\n"
"\n"
"note \"[4/9] Mounting target filesystems\"\n"
"mount -t ext4 \"$ROOT_PART\" \"$TARGET_MOUNT\" || die \"mount $ROOT_PART\"\n"
"if [ \"$FW\" = \"uefi\" ]; then\n"
"    mkdir -p \"$TARGET_MOUNT/boot/efi\"\n"
"    mount -t vfat \"$ESP_PART\" \"$TARGET_MOUNT/boot/efi\" || die \"mount $ESP_PART\"\n"
"fi\n"
"\n"
"note \"[5/9] Copying system files to target\"\n"
"for dir in bin sbin usr etc lib lib64 var boot root home opt; do\n"
"    [ -d \"/$dir\" ] || continue\n"
"    detail \"cp -a /$dir\"\n"
"    cp -a \"/$dir\" \"$TARGET_MOUNT/\" 2>/dev/null || die \"copy /$dir\"\n"
"done\n"
"mkdir -p \"$TARGET_MOUNT/dev\" \"$TARGET_MOUNT/proc\" \"$TARGET_MOUNT/sys\"\n"
"mkdir -p \"$TARGET_MOUNT/run\" \"$TARGET_MOUNT/tmp\" \"$TARGET_MOUNT/mnt/disk\"\n"
"mkdir -p \"$TARGET_MOUNT/var/log\" \"$TARGET_MOUNT/mnt/disk/packages\"\n"
"mkdir -p \"$TARGET_MOUNT/mnt/disk/services\"\n"
"chmod 1777 \"$TARGET_MOUNT/tmp\"\n"
"\n"
"if [ -f /init ]; then\n"
"    cp -f /init \"$TARGET_MOUNT/init\"\n"
"    chmod +x \"$TARGET_MOUNT/init\"\n"
"fi\n"
"\n"
"if [ -x /usr/bin/glib-compile-schemas ]; then\n"
"    glib-compile-schemas \"$TARGET_MOUNT/usr/share/glib-2.0/schemas\" >/dev/null 2>&1 || true\n"
"fi\n"
"if [ -x /usr/bin/gdk-pixbuf-query-loaders ]; then\n"
"    gdk-pixbuf-query-loaders > \"$TARGET_MOUNT/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache\" 2>/dev/null || true\n"
"fi\n"
"\n"
"note \"[6/9] Configuring system, users and localization\"\n"
"\n"
"echo \"$CFG_HOSTNAME\" > \"$TARGET_MOUNT/etc/hostname\"\n"
"cat > \"$TARGET_MOUNT/etc/hosts\" <<EOF\n"
"127.0.0.1   localhost $CFG_HOSTNAME\n"
"::1         localhost ip6-localhost ip6-loopback\n"
"EOF\n"
"\n"
"echo \"FONT=$CFG_FONT\" > \"$TARGET_MOUNT/etc/font.conf\"\n"
"cat > \"$TARGET_MOUNT/etc/keymap.conf\" <<EOF\n"
"LAYOUT=\"$CFG_KEYMAP\"\n"
"TOGGLE=\"$CFG_KEYMAP_OPT\"\n"
"EOF\n"
"\n"
"cat > \"$TARGET_MOUNT/etc/passwd\" <<EOF\n"
"root:x:0:0:root:/root:/bin/bash\n"
"$CFG_USERNAME:x:1000:1000:$CFG_USERNAME:/home/$CFG_USERNAME:/bin/bash\n"
"messagebus:x:101:101:messagebus:/nonexistent:/bin/false\n"
"nobody:x:65534:65534:nobody:/:/sbin/nologin\n"
"EOF\n"
"\n"
"cat > \"$TARGET_MOUNT/etc/group\" <<EOF\n"
"root:x:0:root\n"
"wheel:x:10:root,$CFG_USERNAME\n"
"sudo:x:27:root,$CFG_USERNAME\n"
"audio:x:29:$CFG_USERNAME\n"
"video:x:44:$CFG_USERNAME\n"
"input:x:107:$CFG_USERNAME\n"
"messagebus:x:101:\n"
"netdev:x:102:$CFG_USERNAME\n"
"$CFG_USERNAME:x:1000:\n"
"nobody:x:65534:\n"
"EOF\n"
"\n"
"ROOT_HASH=$(hash_password \"$CFG_ROOT_PASS\")\n"
"USER_HASH=$(hash_password \"$CFG_USER_PASS\")\n"
"[ -n \"$ROOT_HASH\" ] && [ -n \"$USER_HASH\" ] || die \"password hashing produced no output\"\n"
"\n"
"cat > \"$TARGET_MOUNT/etc/shadow\" <<EOF\n"
"root:$ROOT_HASH:19800:0:99999:7:::\n"
"$CFG_USERNAME:$USER_HASH:19800:0:99999:7:::\n"
"messagebus:*:19800:0:99999:7:::\n"
"nobody:*:19800:0:99999:7:::\n"
"EOF\n"
"chmod 0600 \"$TARGET_MOUNT/etc/shadow\"\n"
"\n"
"mkdir -p \"$TARGET_MOUNT/home/$CFG_USERNAME\"\n"
"if [ -d \"$TARGET_MOUNT/etc/skel\" ]; then\n"
"    cp -a \"$TARGET_MOUNT/etc/skel/.\" \"$TARGET_MOUNT/home/$CFG_USERNAME/\" 2>/dev/null || true\n"
"fi\n"
"chown -R 1000:1000 \"$TARGET_MOUNT/home/$CFG_USERNAME\" 2>/dev/null || true\n"
"chmod 755 \"$TARGET_MOUNT/home/$CFG_USERNAME\"\n"
"\n"
"if [ \"$FW\" = \"uefi\" ]; then\n"
"    ROOT_ID=$(sfdisk -d \"$CHOSEN_DISK\" 2>/dev/null | awk -v d=\"$ROOT_PART:\" '$1 == d { for (i = 1; i <= NF; i++) if ($i ~ /^uuid=/) { sub(\"uuid=\", \"\", $i); print $i; exit } }')\n"
"    [ -n \"$ROOT_ID\" ] || die \"cannot read GPT PARTUUID of $ROOT_PART\"\n"
"else\n"
"    HEX=$(dd if=\"$CHOSEN_DISK\" bs=1 skip=440 count=4 2>/dev/null | od -An -tx1 | tr -d '[:space:]')\n"
"    [ \"${#HEX}\" -eq 8 ] 2>/dev/null || die \"cannot read MBR disk signature\"\n"
"    ROOT_ID=$(echo \"$HEX\" | awk '{ print substr($0,7,2) substr($0,5,2) substr($0,3,2) substr($0,1,2) \"-01\" }')\n"
"fi\n"
"\n"
"DATA_UUID=$(blkid -o value -s UUID \"$DATA_PART\" 2>/dev/null | head -n1)\n"
"[ -n \"$DATA_UUID\" ] || die \"cannot read filesystem UUID of $DATA_PART\"\n"
"\n"
"cat > \"$TARGET_MOUNT/etc/fstab\" <<EOF\n"
"PARTUUID=$ROOT_ID  /         ext4  rw,noatime   0 1\n"
"UUID=$DATA_UUID    /mnt/disk ext4  rw,noatime   0 2\n"
"EOF\n"
"\n"
"note \"[7/9] Installing GRUB ($FW)\"\n"
"mkdir -p \"$TARGET_MOUNT/boot/grub\"\n"
"cat > \"$TARGET_MOUNT/boot/grub/grub.cfg\" <<EOF\n"
"set default=0\n"
"set timeout=3\n"
"\n"
"menuentry \"RezzOS Linux\" {\n"
"    linux /boot/bzImage root=PARTUUID=$ROOT_ID rw init=/init quiet loglevel=3 console=tty0 console=ttyS0,115200n8 fbcon=font:$CFG_FONT random.trust_cpu=on\n"
"}\n"
"EOF\n"
"\n"
"if [ \"$FW\" = \"bios\" ]; then\n"
"    if ! GOUT=$(grub-install --boot-directory=\"$TARGET_MOUNT/boot\" --target=i386-pc \"$CHOSEN_DISK\" 2>&1); then\n"
"        echo \"$GOUT\" >&2; die \"grub-install (BIOS)\"\n"
"    fi\n"
"else\n"
"    if ! GOUT=$(grub-install --boot-directory=\"$TARGET_MOUNT/boot\" --efi-directory=\"$TARGET_MOUNT/boot/efi\" --target=x86_64-efi --removable --no-nvram \"$CHOSEN_DISK\" 2>&1); then\n"
"        echo \"$GOUT\" >&2; die \"grub-install (UEFI)\"\n"
"    fi\n"
"fi\n"
"\n"
"note \"[8/9] Seeding persistent data partition\"\n"
"DDIR=\"$TARGET_MOUNT/mnt/disk\"\n"
"mkdir -p \"$DDIR/etc\" \"$DDIR/home\" \"$DDIR/packages\" \"$DDIR/services\"\n"
"for f in hostname keymap.conf font.conf passwd group shadow; do\n"
"    cp -f \"$TARGET_MOUNT/etc/$f\" \"$DDIR/etc/$f\" 2>/dev/null || true\n"
"done\n"
"chmod 0600 \"$DDIR/etc/shadow\" 2>/dev/null || true\n"
"\n"
"note \"[9/9] Verifying installation\"\n"
"[ -s \"$TARGET_MOUNT/boot/bzImage\" ]        || die \"verify: kernel image missing\"\n"
"[ -s \"$TARGET_MOUNT/boot/grub/grub.cfg\" ]  || die \"verify: grub.cfg missing\"\n"
"grep -q \"PARTUUID=$ROOT_ID\" \"$TARGET_MOUNT/boot/grub/grub.cfg\" || die \"verify: wrong root= in grub.cfg\"\n"
"[ -x \"$TARGET_MOUNT/init\" ]                || die \"verify: /init missing or not executable\"\n"
"[ -s \"$TARGET_MOUNT/etc/fstab\" ]           || die \"verify: fstab missing\"\n"
"\n"
"if [ \"$FW\" = \"uefi\" ]; then\n"
"    [ -s \"$TARGET_MOUNT/boot/efi/EFI/BOOT/BOOTX64.EFI\" ] || die \"verify: BOOTX64.EFI missing on ESP\"\n"
"else\n"
"    [ -f \"$TARGET_MOUNT/boot/grub/i386-pc/core.img\" ] || die \"verify: GRUB BIOS core.img missing\"\n"
"fi\n"
"\n"
"[ -s \"$DDIR/etc/passwd\" ] || die \"verify: data partition seeding failed\"\n"
"\n"
"note \"Syncing and unmounting target disk\"\n"
"sync\n"
"if [ \"$FW\" = \"uefi\" ]; then\n"
"    umount \"$TARGET_MOUNT/boot/efi\" 2>/dev/null || true\n"
"fi\n"
"umount \"$TARGET_MOUNT\" 2>/dev/null || true\n"
"rmdir \"$TARGET_MOUNT\" 2>/dev/null || true\n"
"\n"
"echo \"\"\n"
"echo \"==============================================================\"\n"
"echo \"        RezzOS has been successfully installed!\"\n"
"echo \"==============================================================\"\n"
"echo \"Installed on: $ROOT_PART (root), $DATA_PART (data)\"\n"
"echo \"Hostname:     $CFG_HOSTNAME\"\n"
"echo \"User account: $CFG_USERNAME\"\n"
"echo \"\"\n"
"echo \"You can now remove the installation media and reboot.\"\n"
"\n"
"exit 0\n"
;

/* ---------- backend deployment ---------- */

#define BACKEND_TMP_PATH "/run/rezzinstall-backend.sh"
static gchar *backend_path = NULL;

static gboolean
write_backend_script(GError **err)
{
    /* Remove stale file from a previous run, if any. */
    unlink(BACKEND_TMP_PATH);

    int fd = open(BACKEND_TMP_PATH,
                  O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                  0700);
    if (fd < 0) {
        g_set_error(err, G_FILE_ERROR,
                    g_file_error_from_errno(errno),
                    "cannot create %s: %s",
                    BACKEND_TMP_PATH, g_strerror(errno));
        return FALSE;
    }

    const char *p = backend_script;
    size_t remaining = strlen(backend_script);
    while (remaining > 0) {
        ssize_t n = write(fd, p, remaining);
        if (n < 0) {
            if (errno == EINTR) continue;
            int saved = errno;
            close(fd);
            unlink(BACKEND_TMP_PATH);
            g_set_error(err, G_FILE_ERROR,
                        g_file_error_from_errno(saved),
                        "cannot write %s: %s",
                        BACKEND_TMP_PATH, g_strerror(saved));
            return FALSE;
        }
        p += n;
        remaining -= (size_t)n;
    }

    if (close(fd) != 0) {
        int saved = errno;
        unlink(BACKEND_TMP_PATH);
        g_set_error(err, G_FILE_ERROR,
                    g_file_error_from_errno(saved),
                    "cannot close %s: %s",
                    BACKEND_TMP_PATH, g_strerror(saved));
        return FALSE;
    }

    backend_path = g_strdup(BACKEND_TMP_PATH);
    return TRUE;
}

static void
remove_backend_script(void)
{
    if (backend_path) {
        unlink(backend_path);
        g_free(backend_path);
        backend_path = NULL;
    }
}

/* ====================================================================== */
/* GUI                                                                    */
/* ====================================================================== */

/* ---------- widget references ---------- */

static GtkWidget *window;
static GtkWidget *stack;
static GtkWidget *back_btn, *next_btn, *install_btn;

static GtkWidget *disk_combo;

static GtkWidget *data_size_entry;

static GtkWidget *hostname_entry;
static GtkWidget *root_pw1, *root_pw2;
static GtkWidget *username_entry;
static GtkWidget *user_pw1, *user_pw2;

static GtkWidget *keyboard_combo;
static GtkWidget *font_combo;

static GtkWidget *summary_label;
static GtkWidget *confirm_check;

static GtkWidget *progress_view;
static GtkWidget *progress_bar;
static GtkWidget *status_label;
static GtkWidget *reboot_btn, *close_btn;

/* ---------- runtime state ---------- */

static GtkTextBuffer *progress_buf = NULL;
static GPid    backend_pid  = 0;
static gboolean install_done = FALSE;
static gboolean install_ok   = FALSE;
static guint   pulse_timer   = 0;

static const char *page_names[] = {
    "welcome", "disk", "partition", "users", "locale", "summary", "progress"
};
#define N_PAGES ((int)G_N_ELEMENTS(page_names))
static int current_page = 0;

/* ---------- option tables (must match backend expectations) ---------- */

static const struct { const char *label; const char *keymap; const char *opt; }
kb_options[] = {
    { "US English only",                     "us",    "" },
    { "US / RU (Alt + Shift) [Recommended]", "us,ru", "grp:alt_shift_toggle,grp_led:scroll" },
    { "US / RU (Caps Lock)",                 "us,ru", "grp:caps_toggle,grp_led:scroll" },
    { "US / RU (Ctrl + Shift)",              "us,ru", "grp:ctrl_shift_toggle,grp_led:scroll" },
};

static const struct { const char *label; const char *value; }
font_options[] = {
    { "TER16x32 (Large, high-DPI readable) [Recommended]", "TER16x32" },
    { "SUN12x22 (Medium crisp font)",                      "SUN12x22" },
    { "8x16 (Classic VGA)",                                "8x16" },
};

/* ---------- helpers ---------- */

static void
show_message(GtkMessageType type, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    gchar *msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    GtkWidget *dlg = gtk_message_dialog_new(
        window ? GTK_WINDOW(window) : NULL,
        GTK_DIALOG_MODAL | (window ? GTK_DIALOG_DESTROY_WITH_PARENT : 0),
        type, GTK_BUTTONS_OK, "%s", msg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    g_free(msg);
}

static void
log_append(const char *text)
{
    if (!progress_buf) return;
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(progress_buf, &end);
    gtk_text_buffer_insert(progress_buf, &end, text, -1);
    GtkTextMark *mark = gtk_text_buffer_get_insert(progress_buf);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(progress_view), mark);
}

static void
log_fmt(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    gchar *s = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    log_append(s);
    g_free(s);
}

/* ---------- size parsing ---------- */

static gboolean
parse_size_to_sectors(const char *input, guint64 *out, GError **err)
{
    *out = 0;
    if (!input || !*input) return TRUE;

    gchar *s = g_strdup(input);
    g_strstrip(s);
    if (!*s) { g_free(s); return TRUE; }

    errno = 0;
    gchar *end = NULL;
    guint64 n = g_ascii_strtoull(s, &end, 10);
    if (end == s || errno == ERANGE) {
        g_set_error_literal(err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
            "Size must begin with a number (e.g. 20G or 512M).");
        g_free(s);
        return FALSE;
    }

    while (*end == ' ' || *end == '\t') end++;
    char unit = g_ascii_toupper((guchar)*end);
    if (unit) end++;
    if (*end == 'B' || *end == 'b') end++;
    while (*end == ' ' || *end == '\t') end++;

    if (*end != '\0') {
        g_set_error_literal(err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
            "Trailing characters after size; use e.g. 20G or 512M.");
        g_free(s);
        return FALSE;
    }

    guint64 mult;
    switch (unit) {
        case 'G': mult = 2097152ULL; break;
        case 'M': mult = 2048ULL;    break;
        case '\0':
            g_set_error_literal(err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                "Size needs a unit: G (GiB) or M (MiB).");
            g_free(s);
            return FALSE;
        default:
            g_set_error(err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                "Unknown size unit '%c'; use G or M.", unit);
            g_free(s);
            return FALSE;
    }

    if (n == 0) {
        g_set_error_literal(err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
            "Size must be greater than zero.");
        g_free(s);
        return FALSE;
    }

    *out = n * mult;
    g_free(s);
    return TRUE;
}

/* ---------- disk enumeration ---------- */

static gchar *
read_first_line(const char *path)
{
    gchar *contents = NULL;
    if (g_file_get_contents(path, &contents, NULL, NULL)) {
        g_strstrip(contents);
        return contents;
    }
    return NULL;
}

static gchar *
disk_size_label(const char *dev_name)
{
    gchar *path = g_strdup_printf("/sys/block/%s/size", dev_name);
    gchar *sectors_str = read_first_line(path);
    g_free(path);

    guint64 sectors = sectors_str ? g_ascii_strtoull(sectors_str, NULL, 10) : 0;
    g_free(sectors_str);
    guint64 bytes = sectors * 512;

    if (bytes >= (guint64)1073741824)
        return g_strdup_printf("%" G_GUINT64_FORMAT " GB", bytes / 1073741824);
    if (bytes >= (guint64)1048576)
        return g_strdup_printf("%" G_GUINT64_FORMAT " MB", bytes / 1048576);
    return g_strdup_printf("%" G_GUINT64_FORMAT " B", bytes);
}

static gchar *
disk_model_label(const char *dev_name)
{
    gchar *path = g_strdup_printf("/sys/block/%s/device/model", dev_name);
    gchar *model = read_first_line(path);
    g_free(path);
    if (!model) model = g_strdup("Disk");
    return model;
}

static void
populate_disks(void)
{
    gchar *prev_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(disk_combo));
    gchar *prev_keep = prev_id ? g_strdup(prev_id) : NULL;

    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(disk_combo));

    GDir *dir = g_dir_open("/sys/block", 0, NULL);
    if (!dir) { g_free(prev_keep); return; }

    const gchar *name;
    int idx = 0, restore = -1;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (g_str_has_prefix(name, "loop") || g_str_has_prefix(name, "ram") ||
            g_str_has_prefix(name, "zram") || g_str_has_prefix(name, "sr")  ||
            g_str_has_prefix(name, "fd"))
            continue;

        gchar *devpath = g_strdup_printf("/dev/%s", name);
        if (g_file_test(devpath, G_FILE_TEST_EXISTS)) {
            gchar *size  = disk_size_label(name);
            gchar *model = disk_model_label(name);
            gchar *label = g_strdup_printf("%s — %s (%s)", devpath, size, model);
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(disk_combo), devpath, label);
            if (prev_keep && g_strcmp0(prev_keep, devpath) == 0)
                restore = idx;
            idx++;
            g_free(size);
            g_free(model);
            g_free(label);
        }
        g_free(devpath);
    }
    g_dir_close(dir);

    gtk_combo_box_set_active(GTK_COMBO_BOX(disk_combo), restore >= 0 ? restore : 0);
    g_free(prev_keep);
}

/* ---------- page building ---------- */

static GtkWidget *
page_container(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 20);
    return box;
}

static GtkWidget *
labeled_row(GtkWidget *parent, const char *label_text)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *lbl = gtk_label_new(label_text);
    gtk_widget_set_size_request(lbl, 170, -1);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_box_pack_start(GTK_BOX(row), lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(parent), row, FALSE, FALSE, 0);
    return row;
}

static GtkWidget *
build_welcome_page(void)
{
    GtkWidget *box = page_container();

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='xx-large' weight='bold'>RezzOS Installer</span>");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 10);

    GtkWidget *desc = gtk_label_new(
        "This wizard will install RezzOS onto a disk of your choice.\n\n"
        "It will walk you through selecting a target disk, setting up an "
        "administrator and user account, and choosing a keyboard layout "
        "and console font.\n\n"
        "All data on the selected disk will be erased.\n\n"
        "Click Next to begin.");
    gtk_label_set_line_wrap(GTK_LABEL(desc), TRUE);
    gtk_label_set_xalign(GTK_LABEL(desc), 0.0);
    gtk_box_pack_start(GTK_BOX(box), desc, FALSE, FALSE, 0);

    return box;
}

static void
on_refresh_disks(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    populate_disks();
}

static GtkWidget *
build_disk_page(void)
{
    GtkWidget *box = page_container();

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='large' weight='bold'>Select Target Disk</span>");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 6);

    GtkWidget *warn = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(warn),
        "<span foreground='red' weight='bold'>"
        "All data on the selected disk will be erased.</span>");
    gtk_label_set_xalign(GTK_LABEL(warn), 0.0);
    gtk_box_pack_start(GTK_BOX(box), warn, FALSE, FALSE, 0);

    disk_combo = gtk_combo_box_text_new();
    gtk_widget_set_margin_top(disk_combo, 6);
    gtk_box_pack_start(GTK_BOX(box), disk_combo, FALSE, FALSE, 6);

    GtkWidget *refresh = gtk_button_new_with_label("Refresh Disk List");
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_refresh_disks), NULL);
    gtk_box_pack_start(GTK_BOX(box), refresh, FALSE, FALSE, 0);

    populate_disks();
    return box;
}

static GtkWidget *
build_partition_page(void)
{
    GtkWidget *box = page_container();

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='large' weight='bold'>Partition Layout</span>");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 6);

    GtkWidget *desc = gtk_label_new(
        "RezzOS uses two partitions: a root partition for the system, and "
        "a data partition (mounted at /mnt/disk) that survives reinstalls "
        "and holds installed packages.");
    gtk_label_set_line_wrap(GTK_LABEL(desc), TRUE);
    gtk_label_set_xalign(GTK_LABEL(desc), 0.0);
    gtk_box_pack_start(GTK_BOX(box), desc, FALSE, FALSE, 0);

    GtkWidget *row = labeled_row(box, "Data partition size");
    data_size_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(data_size_entry),
        "e.g. 20G — leave blank for half the free space");
    gtk_box_pack_start(GTK_BOX(row), data_size_entry, TRUE, TRUE, 0);

    return box;
}

static GtkWidget *
build_users_page(void)
{
    GtkWidget *box = page_container();

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='large' weight='bold'>System &amp; User Setup</span>");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 6);

    GtkWidget *r0 = labeled_row(box, "Hostname");
    hostname_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(hostname_entry), "rezzos");
    gtk_box_pack_start(GTK_BOX(r0), hostname_entry, TRUE, TRUE, 0);

    GtkWidget *r1 = labeled_row(box, "Root password");
    root_pw1 = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(root_pw1), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(root_pw1), GTK_INPUT_PURPOSE_PASSWORD);
    gtk_box_pack_start(GTK_BOX(r1), root_pw1, TRUE, TRUE, 0);

    GtkWidget *r2 = labeled_row(box, "Confirm root password");
    root_pw2 = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(root_pw2), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(root_pw2), GTK_INPUT_PURPOSE_PASSWORD);
    gtk_box_pack_start(GTK_BOX(r2), root_pw2, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(box),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

    GtkWidget *r3 = labeled_row(box, "Username");
    username_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(username_entry), "user");
    gtk_box_pack_start(GTK_BOX(r3), username_entry, TRUE, TRUE, 0);

    GtkWidget *r4 = labeled_row(box, "User password");
    user_pw1 = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(user_pw1), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(user_pw1), GTK_INPUT_PURPOSE_PASSWORD);
    gtk_box_pack_start(GTK_BOX(r4), user_pw1, TRUE, TRUE, 0);

    GtkWidget *r5 = labeled_row(box, "Confirm user password");
    user_pw2 = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(user_pw2), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(user_pw2), GTK_INPUT_PURPOSE_PASSWORD);
    gtk_box_pack_start(GTK_BOX(r5), user_pw2, TRUE, TRUE, 0);

    return box;
}

static GtkWidget *
build_locale_page(void)
{
    GtkWidget *box = page_container();

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='large' weight='bold'>Keyboard &amp; Font</span>");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 6);

    GtkWidget *r1 = labeled_row(box, "Keyboard layout");
    keyboard_combo = gtk_combo_box_text_new();
    for (gsize i = 0; i < G_N_ELEMENTS(kb_options); i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(keyboard_combo),
                                       kb_options[i].label);
    gtk_combo_box_set_active(GTK_COMBO_BOX(keyboard_combo), 1);
    gtk_box_pack_start(GTK_BOX(r1), keyboard_combo, TRUE, TRUE, 0);

    GtkWidget *r2 = labeled_row(box, "Console font");
    font_combo = gtk_combo_box_text_new();
    for (gsize i = 0; i < G_N_ELEMENTS(font_options); i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_combo),
                                       font_options[i].label);
    gtk_combo_box_set_active(GTK_COMBO_BOX(font_combo), 0);
    gtk_box_pack_start(GTK_BOX(r2), font_combo, TRUE, TRUE, 0);

    return box;
}

static void
on_confirm_toggled(GtkToggleButton *btn, gpointer data)
{
    GtkWidget *target = GTK_WIDGET(data);
    gtk_widget_set_sensitive(target, gtk_toggle_button_get_active(btn));
}

static GtkWidget *
build_summary_page(void)
{
    GtkWidget *box = page_container();

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='large' weight='bold'>Ready to Install</span>");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 6);

    summary_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(summary_label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(summary_label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(summary_label), TRUE);
    gtk_box_pack_start(GTK_BOX(box), summary_label, FALSE, FALSE, 0);

    GtkWidget *warn = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(warn),
        "<span foreground='red' weight='bold'>"
        "WARNING: ALL DATA on the selected disk will be erased!</span>");
    gtk_label_set_xalign(GTK_LABEL(warn), 0.0);
    gtk_box_pack_start(GTK_BOX(box), warn, FALSE, FALSE, 10);

    confirm_check = gtk_check_button_new_with_label(
        "I understand this will erase the selected disk");
    g_signal_connect(confirm_check, "toggled",
                     G_CALLBACK(on_confirm_toggled), install_btn);
    gtk_box_pack_start(GTK_BOX(box), confirm_check, FALSE, FALSE, 0);

    return box;
}

static GtkWidget *
build_progress_page(void)
{
    GtkWidget *box = page_container();

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='large' weight='bold'>Installing RezzOS…</span>");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 6);

    progress_bar = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(box), progress_bar, FALSE, FALSE, 0);

    status_label = gtk_label_new("Waiting for backend…");
    gtk_label_set_xalign(GTK_LABEL(status_label), 0.0);
    gtk_box_pack_start(GTK_BOX(box), status_label, FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scroll, TRUE);
    progress_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(progress_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(progress_view), FALSE);
#if GTK_CHECK_VERSION(3, 16, 0)
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(progress_view), TRUE);
#endif
    progress_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(progress_view));
    gtk_container_add(GTK_CONTAINER(scroll), progress_view);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    GtkWidget *btnrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    reboot_btn = gtk_button_new_with_label("Reboot Now");
    close_btn  = gtk_button_new_with_label("Close Installer");
    gtk_widget_set_sensitive(reboot_btn, FALSE);
    gtk_widget_set_sensitive(close_btn,  FALSE);
    gtk_box_pack_start(GTK_BOX(btnrow), reboot_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btnrow), close_btn,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), btnrow, FALSE, FALSE, 6);

    return box;
}

/* ---------- backend I/O ---------- */

typedef struct {
    GIOChannel *channel;
    gboolean    refcounted;
} StreamCtx;

static void
stream_closed(GIOChannel *ch, StreamCtx *ctx)
{
    if (ctx->refcounted)
        g_io_channel_unref(ch);
    g_io_channel_shutdown(ch, FALSE, NULL);
    g_free(ctx);
}

static gboolean
on_backend_output(GIOChannel *channel, GIOCondition cond, gpointer data)
{
    StreamCtx *ctx = data;

    if (cond & (G_IO_HUP | G_IO_ERR)) {
        stream_closed(channel, ctx);
        return G_SOURCE_REMOVE;
    }

    gchar buf[4096];
    gsize bytes_read = 0;
    GError *err = NULL;
    GIOStatus status = g_io_channel_read_chars(channel, buf, sizeof(buf) - 1,
                                               &bytes_read, &err);
    if (err) g_error_free(err);

    if (status == G_IO_STATUS_NORMAL && bytes_read > 0) {
        buf[bytes_read] = '\0';
        log_append(buf);
        return G_SOURCE_CONTINUE;
    }
    if (status == G_IO_STATUS_AGAIN)
        return G_SOURCE_CONTINUE;

    stream_closed(channel, ctx);
    return G_SOURCE_REMOVE;
}

static gboolean
pulse_cb(gpointer data)
{
    (void)data;
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress_bar));
    return G_SOURCE_CONTINUE;
}

static void
finish_installation(gboolean ok)
{
    install_done = TRUE;
    install_ok   = ok;

    if (pulse_timer) {
        g_source_remove(pulse_timer);
        pulse_timer = 0;
    }

    if (ok) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 1.0);
        gtk_label_set_text(GTK_LABEL(status_label),
            "Installation finished successfully.");
        log_append("\n=== Installation finished successfully ===\n");
        gtk_widget_set_sensitive(reboot_btn, TRUE);
    } else {
        gtk_label_set_text(GTK_LABEL(status_label),
            "Installation FAILED — see the log above.");
        log_append("\n=== Installation FAILED — see the log above ===\n");
    }
    gtk_widget_set_sensitive(close_btn, TRUE);
}

static void
on_backend_exit(GPid pid, gint status, gpointer data)
{
    (void)data;
    gboolean ok = g_spawn_check_exit_status(status, NULL);
    g_spawn_close_pid(pid);
    backend_pid = 0;
    finish_installation(ok);
}

static void
start_installation(void)
{
    const gchar *disk      = gtk_combo_box_get_active_id(GTK_COMBO_BOX(disk_combo));
    const gchar *data_size = gtk_entry_get_text(GTK_ENTRY(data_size_entry));
    const gchar *hostname  = gtk_entry_get_text(GTK_ENTRY(hostname_entry));
    const gchar *root_pass = gtk_entry_get_text(GTK_ENTRY(root_pw1));
    const gchar *username  = gtk_entry_get_text(GTK_ENTRY(username_entry));
    const gchar *user_pass = gtk_entry_get_text(GTK_ENTRY(user_pw1));
    int kb_idx   = gtk_combo_box_get_active(GTK_COMBO_BOX(keyboard_combo));
    int font_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(font_combo));

    guint64 data_sectors = 0;
    GError *err = NULL;
    if (!parse_size_to_sectors(data_size, &data_sectors, &err)) {
        log_fmt("Cannot parse data partition size: %s\n", err->message);
        g_error_free(err);
        finish_installation(FALSE);
        return;
    }

    gchar **envp = g_get_environ();
    envp = g_environ_setenv(envp, "CHOSEN_DISK",  disk ? disk : "", TRUE);
    envp = g_environ_setenv(envp, "CFG_HOSTNAME", hostname,  TRUE);
    envp = g_environ_setenv(envp, "CFG_ROOT_PASS", root_pass, TRUE);
    envp = g_environ_setenv(envp, "CFG_USERNAME",  username,  TRUE);
    envp = g_environ_setenv(envp, "CFG_USER_PASS", user_pass, TRUE);
    envp = g_environ_setenv(envp, "CFG_KEYMAP",     kb_options[kb_idx].keymap, TRUE);
    envp = g_environ_setenv(envp, "CFG_KEYMAP_OPT", kb_options[kb_idx].opt,    TRUE);
    envp = g_environ_setenv(envp, "CFG_FONT",       font_options[font_idx].value, TRUE);
    {
        gchar *sectors_str = g_strdup_printf("%" G_GUINT64_FORMAT, data_sectors);
        envp = g_environ_setenv(envp, "DATA_SECTORS", sectors_str, TRUE);
        g_free(sectors_str);
    }

    gchar *argv[] = { (gchar *)backend_path, NULL };
    GPid pid;
    gint stdout_fd = -1, stderr_fd = -1;
    gboolean ok = g_spawn_async_with_pipes(
        NULL, argv, envp,
        G_SPAWN_DO_NOT_REAP_CHILD,
        NULL, NULL,
        &pid, NULL, &stdout_fd, &stderr_fd,
        &err);
    g_strfreev(envp);

    if (!ok) {
        log_fmt("Failed to start backend: %s\n", err->message);
        g_error_free(err);
        finish_installation(FALSE);
        return;
    }

    backend_pid = pid;

    GIOChannel *out_ch = g_io_channel_unix_new(stdout_fd);
    GIOChannel *err_ch = g_io_channel_unix_new(stderr_fd);
    g_io_channel_set_flags(out_ch, G_IO_FLAG_NONBLOCK, NULL);
    g_io_channel_set_flags(err_ch, G_IO_FLAG_NONBLOCK, NULL);
    g_io_channel_set_encoding(out_ch, NULL, NULL);
    g_io_channel_set_encoding(err_ch, NULL, NULL);
    g_io_channel_set_close_on_unref(out_ch, TRUE);
    g_io_channel_set_close_on_unref(err_ch, TRUE);

    StreamCtx *ctx1 = g_new0(StreamCtx, 1);
    StreamCtx *ctx2 = g_new0(StreamCtx, 1);
    ctx1->channel = out_ch; ctx1->refcounted = TRUE;
    ctx2->channel = err_ch; ctx2->refcounted = TRUE;

    g_io_add_watch(out_ch, G_IO_IN | G_IO_HUP | G_IO_ERR, on_backend_output, ctx1);
    g_io_add_watch(err_ch, G_IO_IN | G_IO_HUP | G_IO_ERR, on_backend_output, ctx2);
    g_child_watch_add(pid, on_backend_exit, NULL);

    gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(progress_bar), 0.05);
    pulse_timer = g_timeout_add(120, pulse_cb, NULL);
    gtk_label_set_text(GTK_LABEL(status_label), "Installation in progress…");
}

/* ---------- summary + navigation ---------- */

static void
refresh_summary(void)
{
    const gchar *disk      = gtk_combo_box_get_active_id(GTK_COMBO_BOX(disk_combo));
    const gchar *data_size = gtk_entry_get_text(GTK_ENTRY(data_size_entry));
    const gchar *hostname  = gtk_entry_get_text(GTK_ENTRY(hostname_entry));
    const gchar *username  = gtk_entry_get_text(GTK_ENTRY(username_entry));
    int kb_idx   = gtk_combo_box_get_active(GTK_COMBO_BOX(keyboard_combo));
    int font_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(font_combo));

    gchar *data_desc;
    if (data_size && *data_size)
        data_desc = g_strdup(data_size);
    else
        data_desc = g_strdup("half of free space (default)");

    gchar *text = g_strdup_printf(
        "<b>Disk:</b> %s\n"
        "<b>Data partition:</b> %s\n"
        "<b>Hostname:</b> %s\n"
        "<b>User account:</b> %s\n"
        "<b>Keyboard:</b> %s\n"
        "<b>Console font:</b> %s",
        disk ? disk : "(none selected)",
        data_desc,
        hostname, username,
        kb_options[kb_idx].label,
        font_options[font_idx].label);
    gtk_label_set_markup(GTK_LABEL(summary_label), text);
    g_free(text);
    g_free(data_desc);
}

static gboolean
validate_page(int page)
{
    switch (page) {
    case 1:
        if (!gtk_combo_box_get_active_id(GTK_COMBO_BOX(disk_combo))) {
            show_message(GTK_MESSAGE_ERROR, "No disk selected.");
            return FALSE;
        }
        return TRUE;

    case 2: {
        const gchar *s = gtk_entry_get_text(GTK_ENTRY(data_size_entry));
        guint64 sectors;
        GError *err = NULL;
        if (!parse_size_to_sectors(s, &sectors, &err)) {
            show_message(GTK_MESSAGE_ERROR,
                "Invalid data partition size: %s", err->message);
            g_error_free(err);
            return FALSE;
        }
        return TRUE;
    }

    case 3: {
        const gchar *hn  = gtk_entry_get_text(GTK_ENTRY(hostname_entry));
        const gchar *rp1 = gtk_entry_get_text(GTK_ENTRY(root_pw1));
        const gchar *rp2 = gtk_entry_get_text(GTK_ENTRY(root_pw2));
        const gchar *un  = gtk_entry_get_text(GTK_ENTRY(username_entry));
        const gchar *up1 = gtk_entry_get_text(GTK_ENTRY(user_pw1));
        const gchar *up2 = gtk_entry_get_text(GTK_ENTRY(user_pw2));
        const gchar *msg = NULL;

        if (!*hn)                                 msg = "Enter a hostname.";
        else if (!*rp1 || strcmp(rp1, rp2) != 0)  msg = "Root passwords are empty or do not match.";
        else if (!*un || strcmp(un, "root") == 0) msg = "Enter a valid, non-root username.";
        else if (!*up1 || strcmp(up1, up2) != 0)  msg = "User passwords are empty or do not match.";

        if (msg) {
            show_message(GTK_MESSAGE_ERROR, "%s", msg);
            return FALSE;
        }
        return TRUE;
    }

    default:
        return TRUE;
    }
}

static void
show_page(int page)
{
    current_page = page;
    gtk_stack_set_visible_child_name(GTK_STACK(stack), page_names[page]);

    gtk_widget_set_visible(back_btn,    page > 0 && page < N_PAGES - 1);
    gtk_widget_set_visible(next_btn,    page < N_PAGES - 2);
    gtk_widget_set_visible(install_btn, page == N_PAGES - 2);

    if (page == 5) {
        refresh_summary();
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(confirm_check), FALSE);
        gtk_widget_set_sensitive(install_btn, FALSE);
    }
}

static void
on_back_clicked(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    if (current_page > 0 && !install_done)
        show_page(current_page - 1);
}

static void
on_next_clicked(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    if (!validate_page(current_page)) return;
    if (current_page < N_PAGES - 1) show_page(current_page + 1);
}

static void
on_install_clicked(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    if (install_done) return;
    show_page(N_PAGES - 1);
    start_installation();
}

static void
on_reboot_clicked(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    if (!install_done || !install_ok) return;

    GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(window),
        GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
        "Reboot now?");
    gint r = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    if (r != GTK_RESPONSE_YES) return;

    if (!g_spawn_command_line_async("systemctl reboot", NULL))
        g_spawn_command_line_async("/sbin/reboot", NULL);
}

static void
on_close_clicked(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    gtk_main_quit();
}

static gboolean
on_delete_event(GtkWidget *w, GdkEvent *e, gpointer d)
{
    (void)w; (void)e; (void)d;

    if (!install_done && backend_pid > 0) {
        GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
            "Installation is in progress. Closing the installer now may "
            "leave the target disk in an inconsistent state.\n\n"
            "Close anyway?");
        gint r = gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        if (r != GTK_RESPONSE_YES)
            return TRUE;

        kill(backend_pid, SIGTERM);
        g_spawn_close_pid(backend_pid);
        backend_pid = 0;
    }
    gtk_main_quit();
    return FALSE;
}

/* ---------- main ---------- */

int
main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    if (getuid() != 0) {
        GtkWidget *dlg = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "rezzinstall must be run as root.");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        return 1;
    }

    GError *err = NULL;
    if (!write_backend_script(&err)) {
        GtkWidget *dlg = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Cannot deploy installer backend: %s", err->message);
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        g_error_free(err);
        return 1;
    }
    atexit(remove_backend_script);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "RezzOS Installer");
    gtk_window_set_default_size(GTK_WINDOW(window), 680, 560);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    g_signal_connect(window, "delete-event", G_CALLBACK(on_delete_event), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack),
        GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_widget_set_vexpand(stack, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), stack, TRUE, TRUE, 0);

    /* Button bar must be built before build_summary_page(): the summary
     * page wires confirm_check to install_btn. */
    GtkWidget *btnbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(btnbar), 10);
    back_btn    = gtk_button_new_with_label("Back");
    next_btn    = gtk_button_new_with_label("Next");
    install_btn = gtk_button_new_with_label("Install");
    g_signal_connect(back_btn,    "clicked", G_CALLBACK(on_back_clicked),    NULL);
    g_signal_connect(next_btn,    "clicked", G_CALLBACK(on_next_clicked),    NULL);
    g_signal_connect(install_btn, "clicked", G_CALLBACK(on_install_clicked), NULL);
    gtk_box_pack_end(GTK_BOX(btnbar), next_btn,    FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(btnbar), install_btn, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(btnbar), back_btn,    FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), btnbar, FALSE, FALSE, 0);

    gtk_stack_add_named(GTK_STACK(stack), build_welcome_page(),   page_names[0]);
    gtk_stack_add_named(GTK_STACK(stack), build_disk_page(),      page_names[1]);
    gtk_stack_add_named(GTK_STACK(stack), build_partition_page(), page_names[2]);
    gtk_stack_add_named(GTK_STACK(stack), build_users_page(),     page_names[3]);
    gtk_stack_add_named(GTK_STACK(stack), build_locale_page(),    page_names[4]);
    gtk_stack_add_named(GTK_STACK(stack), build_summary_page(),   page_names[5]);
    gtk_stack_add_named(GTK_STACK(stack), build_progress_page(),  page_names[6]);

    g_signal_connect(reboot_btn, "clicked", G_CALLBACK(on_reboot_clicked), NULL);
    g_signal_connect(close_btn,  "clicked", G_CALLBACK(on_close_clicked),  NULL);

    /* Order matters: show_all first, then enforce the initial page's
     * button visibility — otherwise show_all un-hides Back/Install. */
    gtk_widget_show_all(window);
    show_page(0);

    gtk_main();
    return 0;
}
