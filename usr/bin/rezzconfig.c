/*
 * rezzconfig — GTK3 control center for RezzOS.
 *
 * Merges the original shell "rezzconfig" text menu (network, hostname,
 * font, keyboard, swap, services, users, packages, install, power) with
 * the GTK3 front-end that used to sit on top of it. This is now the only
 * copy of that logic — the standalone shell menu is gone.
 *
 * All shell work goes through capture_shell()/run_cmd(), both of which
 * spawn a real "sh -c <script>", so every pipeline, awk, sed -i and
 * heredoc from the original menu works unmodified. Because that means
 * user-entered text now reaches a real shell (unlike the previous
 * g_spawn_command_line_sync-based run_cmd, which could not interpret
 * shell syntax at all), every field taken from an entry widget is passed
 * through is_safe_token() before it is spliced into a script string.
 *
 * Build (on the Alpine build host, same as rezzbrowser/rezzinstall):
 *   gcc -O2 -Wall -Wextra rezzconfig.c $(pkg-config --cflags --libs gtk+-3.0) \
 *       -o rezzconfig
 */

#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

static GtkWidget *log_view;
static GtkWidget *status_bar_label;
static GtkWidget *notebook;

/* ====================================================================== */
/* Shared plumbing: log panel, shell execution, input validation          */
/* ====================================================================== */

static void
log_append(const char *text)
{
    if (!text) return;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);
    gtk_text_buffer_insert(buf, &end, text, -1);
    gtk_text_buffer_insert(buf, &end, "\n", -1);

    GtkTextMark *mark = gtk_text_buffer_get_insert(buf);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(log_view), mark);
}

static void
log_clear(void)
{
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));
    gtk_text_buffer_set_text(buf, "", -1);
}

/* Runs `script` as a real shell script (sh -c), combining stdout+stderr
 * into the log, exactly like watching the original menu run in a
 * terminal. Unlike g_spawn_command_line_sync, this understands pipes,
 * awk, sed -i, $(...), heredocs, loops — everything the original
 * rezzconfig used. */
static void
run_cmd(const char *script)
{
    GSubprocess *proc;
    GError *error = NULL;
    gchar *out = NULL;

    log_append(g_strdup_printf("$ %s", script));

    proc = g_subprocess_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE,
        &error, "sh", "-c", script, NULL);
    if (!proc) {
        log_append(g_strdup_printf("(failed to start: %s)", error->message));
        g_error_free(error);
        return;
    }

    if (!g_subprocess_communicate_utf8(proc, NULL, NULL, &out, NULL, &error)) {
        log_append(g_strdup_printf("(failed to run: %s)",
                                    error ? error->message : "unknown error"));
        if (error) g_error_free(error);
        g_object_unref(proc);
        return;
    }

    if (out && *out) log_append(out);

    gint code = g_subprocess_get_exit_status(proc);
    if (code != 0 && (!out || !*out))
        log_append(g_strdup_printf("(exited with status %d, no output)", code));

    g_free(out);
    g_object_unref(proc);
}

/* Same as run_cmd but silent (no log lines) and returns stdout, for
 * reading status information (hostname, IP, font, swap) rather than
 * showing a command execution. */
static gchar *
capture_shell(const char *script)
{
    GSubprocess *proc = g_subprocess_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
        NULL, "sh", "-c", script, NULL);
    if (!proc) return NULL;

    gchar *out = NULL;
    g_subprocess_communicate_utf8(proc, NULL, NULL, &out, NULL, NULL);
    g_object_unref(proc);
    if (out) g_strstrip(out);
    return out;
}

/* Whitelist check for anything that gets spliced into a shell script:
 * letters, digits, and the handful of punctuation marks that legitimate
 * hostnames/IPs/interface names/usernames/DNS lists actually use. Blocks
 * ;, |, `, $, (, ), <, >, quotes, backslash, newline — the characters
 * that would otherwise let a form field escape into arbitrary shell. */
static gboolean
is_safe_token(const char *s)
{
    if (!s || !*s) return FALSE;
    for (const char *p = s; *p; p++) {
        if (g_ascii_isalnum((guchar)*p)) continue;
        switch (*p) {
            case '.': case '-': case '_': case ':': case '/': case ' ':
                continue;
            default:
                return FALSE;
        }
    }
    return TRUE;
}

/* Sets a user's password without it ever appearing as a command-line
 * argument (which `ps` on another terminal could otherwise see) — feeds
 * "user:password" to chpasswd's stdin instead. This replaces the shell
 * menu's interactive `passwd` call, which needed a real terminal attached
 * to prompt twice; a GUI has no terminal to hand it, so this is the
 * equivalent for a GUI context rather than a straight port. */
static gboolean
set_password(const char *user, const char *password)
{
    GSubprocess *proc;
    GError *error = NULL;
    gchar *input;
    gboolean ok;

    proc = g_subprocess_new(
        G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE,
        &error, "chpasswd", NULL);
    if (!proc) {
        log_append(g_strdup_printf("chpasswd failed to start: %s", error->message));
        g_error_free(error);
        return FALSE;
    }

    input = g_strdup_printf("%s:%s\n", user, password);
    ok = g_subprocess_communicate_utf8(proc, input, NULL, NULL, NULL, &error);
    g_free(input);

    if (!ok) {
        log_append(g_strdup_printf("chpasswd error: %s", error ? error->message : "unknown"));
        if (error) g_error_free(error);
        g_object_unref(proc);
        return FALSE;
    }

    gint code = g_subprocess_get_exit_status(proc);
    g_object_unref(proc);

    if (code == 0) {
        log_append(g_strdup_printf("Password updated for %s.", user));
        return TRUE;
    }
    log_append(g_strdup_printf("chpasswd exited with status %d.", code));
    return FALSE;
}

/* Port of the shell menu's detect_iface(): first non-loopback interface
 * that is up and already has an address, so DHCP/static setup works on
 * any naming (eth*, en*, wlan*), not just eth0. */
static gchar *
detect_iface(void)
{
    gchar *out = capture_shell(
        "for i in $(ip -o link show up 2>/dev/null | awk -F': ' '{print $2}'); do "
        "  [ \"$i\" = lo ] && continue; "
        "  if ip addr show \"$i\" 2>/dev/null | grep -q 'inet '; then echo \"$i\"; exit 0; fi; "
        "done; echo eth0");
    if (!out || !*out) { g_free(out); return g_strdup("eth0"); }
    return out;
}

/* Port of the shell menu's header(): hostname / IP / font / swap
 * summary, shown above the tabs and refreshed on every tab switch and
 * after any action that could change one of these values. */
static void
refresh_status_bar(void)
{
    gchar *hn = capture_shell("hostname 2>/dev/null || echo rezzos");
    gchar *ip = capture_shell(
        "IP=$(ip route get 1 2>/dev/null | awk '{print $7; exit}'); "
        "if [ -z \"$IP\" ]; then "
        "  IFACE=$(ip -o link show up 2>/dev/null | awk -F': ' '$2!=\"lo\"{print $2; exit}'); "
        "  IP=$(ifconfig \"$IFACE\" 2>/dev/null | grep 'inet addr' | cut -d: -f2 | awk '{print $1}'); "
        "fi; "
        "[ -z \"$IP\" ] && IP='No Network'; echo \"$IP\"");
    gchar *font = capture_shell(
        "[ -f /etc/font.conf ] && grep -o 'FONT=[^ ]*' /etc/font.conf | cut -d= -f2");
    gchar *swap = capture_shell(
        "free -m 2>/dev/null | grep -i Swap | "
        "awk '{if ($2>0) print $3\"M/\"$2\"M\"; else print \"Disabled\"}'");

    gchar *text = g_strdup_printf(
        "Host: %s    IP: %s    Font: %s    Swap: %s",
        (hn && *hn) ? hn : "rezzos",
        (ip && *ip) ? ip : "No Network",
        (font && *font) ? font : "TER16x32",
        (swap && *swap) ? swap : "Disabled");
    gtk_label_set_text(GTK_LABEL(status_bar_label), text);

    g_free(hn); g_free(ip); g_free(font); g_free(swap); g_free(text);
}

/* ---------- helpers to build simple form rows ---------- */

static GtkWidget *
labeled_entry(GtkWidget *box, const char *label, const char *placeholder)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *lbl = gtk_label_new(label);
    gtk_widget_set_size_request(lbl, 140, -1);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    GtkWidget *entry = gtk_entry_new();
    if (placeholder) gtk_entry_set_placeholder_text(GTK_ENTRY(entry), placeholder);
    gtk_box_pack_start(GTK_BOX(row), lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
    return entry;
}

static GtkWidget *
tab_box(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    return box;
}

/* ================= Network tab ================= */
/* Ports net_menu() from the shell script: view status (1), DHCP (2),
 * static IP (3), DNS-only update (4), ping test (5). */

static GtkWidget *net_iface, *net_ip, *net_mask, *net_gw, *net_dns;

static void
on_net_status(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    log_clear();
    run_cmd(
        "echo '--- Network Interfaces ---'; ifconfig 2>/dev/null || ip addr show; "
        "echo; echo '--- Routing Table ---'; route -n 2>/dev/null || ip route; "
        "echo; echo '--- DNS Servers (/etc/resolv.conf) ---'; cat /etc/resolv.conf 2>/dev/null; "
        "echo; echo '--- Persisted Network Config ---'; "
        "if [ -f /mnt/disk/etc/network.conf ]; then "
        "  . /mnt/disk/etc/network.conf; "
        "  echo 'Mode: static (applied at every boot)'; "
        "  echo \"Interface: ${IFACE:-eth0}\"; "
        "  echo \"IP: ${IP:-} / MASK: ${MASK:-255.255.255.0}\"; "
        "  echo \"Gateway: ${GW:-none}\"; "
        "  echo \"DNS: ${DNS:-none}\"; "
        "else echo 'Mode: DHCP (auto-configured at every boot)'; fi");
}

static void
on_net_dhcp(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    log_clear();
    run_cmd(
        "IFACE=$(ip -o link show up 2>/dev/null | awk -F': ' '$2!=\"lo\"{print $2; exit}'); "
        "[ -z \"$IFACE\" ] && IFACE=eth0; "
        "echo \"Switching to DHCP on $IFACE (clears any saved static config)...\"; "
        "rm -f /etc/network.conf /mnt/disk/etc/network.conf; "
        "ifconfig \"$IFACE\" up 2>/dev/null; "
        "if udhcpc -b -i \"$IFACE\" -s /usr/share/udhcpc/default.script 2>/dev/null; then "
        "  echo \"DHCP configuration completed on $IFACE\"; "
        "else echo \"DHCP failed or $IFACE not available.\"; fi");
    refresh_status_bar();
}

static void
on_net_static_apply(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    const char *iface = gtk_entry_get_text(GTK_ENTRY(net_iface));
    const char *ip    = gtk_entry_get_text(GTK_ENTRY(net_ip));
    const char *mask  = gtk_entry_get_text(GTK_ENTRY(net_mask));
    const char *gw    = gtk_entry_get_text(GTK_ENTRY(net_gw));
    const char *dns   = gtk_entry_get_text(GTK_ENTRY(net_dns));

    log_clear();

    if (!*ip) { log_append("Enter an IP address first."); return; }
    if (!*iface) iface = "eth0";
    if (!*mask)  mask  = "255.255.255.0";
    if (!*dns)   dns   = "8.8.8.8";

    if (!is_safe_token(iface) || !is_safe_token(ip) || !is_safe_token(mask) ||
        (*gw && !is_safe_token(gw)) || !is_safe_token(dns)) {
        log_append("One of the network fields contains characters that aren't allowed.");
        return;
    }

    gchar *route_part = *gw
        ? g_strdup_printf("route add default gw %s 2>/dev/null; ", gw)
        : g_strdup("");

    gchar *script = g_strdup_printf(
        "ifconfig %s %s netmask %s up; "
        "%s"
        ": > /etc/resolv.conf; for ns in %s; do echo nameserver $ns >> /etc/resolv.conf; done; "
        "cat > /etc/network.conf << NETCONF\n"
        "IFACE=\"%s\"\nIP=\"%s\"\nMASK=\"%s\"\nGW=\"%s\"\nDNS=\"%s\"\n"
        "NETCONF\n"
        "if grep -q ' /mnt/disk ' /proc/mounts; then "
        "  mkdir -p /mnt/disk/etc; cp -f /etc/network.conf /mnt/disk/etc/network.conf; "
        "  echo 'Static config applied and saved (persists across reboots).'; "
        "else "
        "  echo 'No persistent disk mounted - the static config will not survive a reboot.'; "
        "fi",
        iface, ip, mask, route_part, dns, iface, ip, mask, gw, dns);

    run_cmd(script);
    g_free(route_part);
    g_free(script);
    refresh_status_bar();
}

static void
on_net_dns_only(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    const char *dns = gtk_entry_get_text(GTK_ENTRY(net_dns));
    log_clear();

    if (!*dns) dns = "8.8.8.8";
    if (!is_safe_token(dns)) {
        log_append("DNS field contains characters that aren't allowed.");
        return;
    }

    gchar *script = g_strdup_printf(
        ": > /etc/resolv.conf; for ns in %s; do echo nameserver $ns >> /etc/resolv.conf; done; "
        "if [ -f /mnt/disk/etc/network.conf ]; then "
        "  . /mnt/disk/etc/network.conf; "
        "  cat > /etc/network.conf << NETCONF\n"
        "IFACE=\"${IFACE:-eth0}\"\nIP=\"${IP:-}\"\nMASK=\"${MASK:-255.255.255.0}\"\nGW=\"${GW:-}\"\nDNS=\"%s\"\n"
        "NETCONF\n"
        "  cp -f /etc/network.conf /mnt/disk/etc/network.conf; "
        "  echo 'DNS updated and saved with the static config: %s'; "
        "else "
        "  echo 'DNS updated for this session: %s'; "
        "  echo 'Set a static IP first to persist DNS across reboots (DHCP overrides it otherwise).'; "
        "fi",
        dns, dns, dns, dns);

    run_cmd(script);
    g_free(script);
}

static void
on_net_ping(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    log_clear();
    run_cmd("ping -c 4 8.8.8.8");
}

static GtkWidget *
build_network_tab(void)
{
    GtkWidget *box = tab_box();

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *b1 = gtk_button_new_with_label("View Status");
    GtkWidget *b2 = gtk_button_new_with_label("Auto-Configure (DHCP)");
    GtkWidget *b3 = gtk_button_new_with_label("Test Connectivity");
    g_signal_connect(b1, "clicked", G_CALLBACK(on_net_status), NULL);
    g_signal_connect(b2, "clicked", G_CALLBACK(on_net_dhcp), NULL);
    g_signal_connect(b3, "clicked", G_CALLBACK(on_net_ping), NULL);
    gtk_box_pack_start(GTK_BOX(btns), b1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btns), b2, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btns), b3, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), btns, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

    GtkWidget *static_label = gtk_label_new("Static IP Configuration:");
    gtk_label_set_xalign(GTK_LABEL(static_label), 0.0);
    gtk_box_pack_start(GTK_BOX(box), static_label, FALSE, FALSE, 0);

    gchar *detected = detect_iface();
    gchar *iface_hint = g_strdup_printf("detected: %s", detected);
    net_iface = labeled_entry(box, "Interface", iface_hint);
    g_free(iface_hint);
    g_free(detected);

    net_ip   = labeled_entry(box, "IP Address", "e.g. 192.168.1.100 (your real address)");
    net_mask = labeled_entry(box, "Subnet Mask", "e.g. 255.255.255.0");
    net_gw   = labeled_entry(box, "Gateway", "e.g. 192.168.1.1 (your real router)");
    net_dns  = labeled_entry(box, "DNS Servers", "e.g. 8.8.8.8 1.1.1.1");

    GtkWidget *warn = gtk_label_new(
        "Note: the grey text above is just an example format, not your real network.\n"
        "Click \"View Status\" first to see your actual IP/gateway before filling this in.");
    gtk_label_set_xalign(GTK_LABEL(warn), 0.0);
    gtk_box_pack_start(GTK_BOX(box), warn, FALSE, FALSE, 0);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *apply = gtk_button_new_with_label("Apply Static Config");
    GtkWidget *dns_only = gtk_button_new_with_label("Update DNS Only");
    g_signal_connect(apply, "clicked", G_CALLBACK(on_net_static_apply), NULL);
    g_signal_connect(dns_only, "clicked", G_CALLBACK(on_net_dns_only), NULL);
    gtk_box_pack_start(GTK_BOX(actions), apply, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions), dns_only, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), actions, FALSE, FALSE, 4);

    return box;
}

/* ================= Hostname tab ================= */

static GtkWidget *hostname_entry, *hostname_current_label;

static void
refresh_hostname_label(void)
{
    gchar *hn = capture_shell("hostname 2>/dev/null || echo rezzos");
    gchar *text = g_strdup_printf("Current hostname: %s", (hn && *hn) ? hn : "rezzos");
    gtk_label_set_text(GTK_LABEL(hostname_current_label), text);
    g_free(text);
    g_free(hn);
}

static void
on_hostname_apply(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    const char *hn = gtk_entry_get_text(GTK_ENTRY(hostname_entry));
    log_clear();

    if (!*hn) { log_append("Enter a hostname first."); return; }
    for (const char *p = hn; *p; p++) {
        if (!g_ascii_isalnum((guchar)*p) && *p != '.' && *p != '-') {
            log_append("A hostname may only contain letters, digits, dots and hyphens.");
            return;
        }
    }

    gchar *script = g_strdup_printf(
        "hostname %s; echo %s > /etc/hostname; "
        /* Keep 127.0.0.1 pointing at the machine's own name. Leaving the
         * old one there makes anything that looks itself up wait for a
         * lookup that cannot succeed. */
        "sed -i 's/^127\\.0\\.0\\.1 .*/127.0.0.1 %s localhost/' /etc/hosts 2>/dev/null; "
        "if [ -d /mnt/disk ]; then mkdir -p /mnt/disk/etc; echo %s > /mnt/disk/etc/hostname; fi; "
        "echo 'Hostname changed to %s successfully!'",
        hn, hn, hn, hn, hn);

    run_cmd(script);
    g_free(script);
    refresh_hostname_label();
    refresh_status_bar();
}

static GtkWidget *
build_hostname_tab(void)
{
    GtkWidget *box = tab_box();
    hostname_current_label = gtk_label_new("Current hostname: (unknown)");
    gtk_label_set_xalign(GTK_LABEL(hostname_current_label), 0.0);
    gtk_box_pack_start(GTK_BOX(box), hostname_current_label, FALSE, FALSE, 0);

    hostname_entry = labeled_entry(box, "New Hostname", "rezzos");
    GtkWidget *apply = gtk_button_new_with_label("Apply Hostname");
    g_signal_connect(apply, "clicked", G_CALLBACK(on_hostname_apply), NULL);
    gtk_box_pack_start(GTK_BOX(box), apply, FALSE, FALSE, 4);

    refresh_hostname_label();
    return box;
}

/* ================= Font tab ================= */

static GtkWidget *font_entry;

static void
on_font_list(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    log_clear();
    run_cmd("if command -v font >/dev/null 2>&1; then font list; "
            "else echo 'font tool not found'; fi");
}

static void
on_font_apply(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    const char *name = gtk_entry_get_text(GTK_ENTRY(font_entry));
    log_clear();
    if (!*name) { log_append("Enter a font name first."); return; }
    if (!is_safe_token(name)) { log_append("Font name contains characters that aren't allowed."); return; }

    gchar *script = g_strdup_printf("font set %s", name);
    run_cmd(script);
    g_free(script);
    refresh_status_bar();
}

static GtkWidget *
build_font_tab(void)
{
    GtkWidget *box = tab_box();
    GtkWidget *list_btn = gtk_button_new_with_label("List Available Fonts");
    g_signal_connect(list_btn, "clicked", G_CALLBACK(on_font_list), NULL);
    gtk_box_pack_start(GTK_BOX(box), list_btn, FALSE, FALSE, 0);

    font_entry = labeled_entry(box, "Font Name", "TER16x32");
    GtkWidget *apply = gtk_button_new_with_label("Set Font");
    g_signal_connect(apply, "clicked", G_CALLBACK(on_font_apply), NULL);
    gtk_box_pack_start(GTK_BOX(box), apply, FALSE, FALSE, 4);
    return box;
}

/* ================= Keyboard tab ================= */
/* Ports keyboard_menu(): prefer the standalone rezzkeymap tool if it's
 * installed (checked at click time, same pattern as the Install tab's
 * check for rezzinstall), otherwise fall back to the four quick layouts. */

static void
on_kb_layout(GtkButton *b, gpointer data)
{
    (void)b;
    run_cmd((const char *)data);
}

static void
on_kb_launch_external(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    log_clear();
    if (g_file_test("/usr/bin/rezzkeymap", G_FILE_TEST_IS_EXECUTABLE)) {
        GError *error = NULL;
        if (!g_spawn_command_line_async("st -e /usr/bin/rezzkeymap", &error)) {
            log_append(g_strdup_printf("Failed to launch rezzkeymap: %s", error->message));
            g_error_free(error);
        }
    } else {
        log_append("rezzkeymap not found - use the quick layouts below instead.");
    }
}

static GtkWidget *
build_keyboard_tab(void)
{
    GtkWidget *box = tab_box();

    GtkWidget *ext = gtk_button_new_with_label("Launch Full Keyboard Tool (rezzkeymap)");
    g_signal_connect(ext, "clicked", G_CALLBACK(on_kb_launch_external), NULL);
    gtk_box_pack_start(GTK_BOX(box), ext, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

    GtkWidget *lbl = gtk_label_new("Or choose a quick layout:");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);

    struct { const char *label; const char *cmd; } opts[] = {
        {"US / RU (Alt + Shift)",  "setxkbmap -layout us,ru -option grp:alt_shift_toggle,grp_led:scroll"},
        {"US / RU (Caps Lock)",    "setxkbmap -layout us,ru -option grp:caps_toggle,grp_led:scroll"},
        {"US / RU (Ctrl + Shift)", "setxkbmap -layout us,ru -option grp:ctrl_shift_toggle,grp_led:scroll"},
        {"US English only",       "setxkbmap -layout us -option ''"},
    };

    for (size_t i = 0; i < G_N_ELEMENTS(opts); i++) {
        GtkWidget *btn = gtk_button_new_with_label(opts[i].label);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_kb_layout), (gpointer)opts[i].cmd);
        gtk_box_pack_start(GTK_BOX(box), btn, FALSE, FALSE, 0);
    }
    return box;
}

/* ================= Swap tab ================= */

static void
on_swap_status(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    log_clear();
    run_cmd("if command -v swap >/dev/null 2>&1; then swap status; else free -m; fi");
}

static void
on_swap_create(GtkButton *b, gpointer data)
{
    (void)b;
    const char *size = (const char *)data;
    log_clear();
    gchar *script = g_strdup_printf("swap create %s", size);
    run_cmd(script);
    g_free(script);
    refresh_status_bar();
}

static void
on_swap_off(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    log_clear();
    run_cmd("swap off 2>/dev/null || swapoff -a 2>/dev/null; echo 'Swap disabled.'");
    refresh_status_bar();
}

static GtkWidget *
build_swap_tab(void)
{
    GtkWidget *box = tab_box();
    GtkWidget *status = gtk_button_new_with_label("View Swap Status");
    g_signal_connect(status, "clicked", G_CALLBACK(on_swap_status), NULL);
    gtk_box_pack_start(GTK_BOX(box), status, FALSE, FALSE, 0);

    const char *sizes[] = {"256M", "512M", "1024M"};
    for (size_t i = 0; i < G_N_ELEMENTS(sizes); i++) {
        gchar *label = g_strdup_printf("Create & Enable Swap (%s)", sizes[i]);
        GtkWidget *btn = gtk_button_new_with_label(label);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_swap_create), (gpointer)sizes[i]);
        gtk_box_pack_start(GTK_BOX(box), btn, FALSE, FALSE, 0);
        g_free(label);
    }

    GtkWidget *off = gtk_button_new_with_label("Turn Off Swap");
    g_signal_connect(off, "clicked", G_CALLBACK(on_swap_off), NULL);
    gtk_box_pack_start(GTK_BOX(box), off, FALSE, FALSE, 0);
    return box;
}

/* ================= Services tab ================= */

static GtkWidget *service_entry;

static void
on_svc_list(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    log_clear();
    run_cmd("rsv list");
}

static void
on_svc_action(GtkButton *b, gpointer data)
{
    (void)b;
    const char *action = (const char *)data;
    const char *name = gtk_entry_get_text(GTK_ENTRY(service_entry));
    log_clear();
    if (!*name) { log_append("Enter a service name first."); return; }
    if (!is_safe_token(name)) { log_append("Service name contains characters that aren't allowed."); return; }

    gchar *script = g_strdup_printf("rsv %s %s", action, name);
    run_cmd(script);
    g_free(script);
}

static GtkWidget *
build_services_tab(void)
{
    GtkWidget *box = tab_box();
    GtkWidget *list_btn = gtk_button_new_with_label("List Services Status");
    g_signal_connect(list_btn, "clicked", G_CALLBACK(on_svc_list), NULL);
    gtk_box_pack_start(GTK_BOX(box), list_btn, FALSE, FALSE, 0);

    service_entry = labeled_entry(box, "Service Name", "e.g. network");

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *up = gtk_button_new_with_label("Start");
    GtkWidget *down = gtk_button_new_with_label("Stop");
    GtkWidget *restart = gtk_button_new_with_label("Restart");
    g_signal_connect(up, "clicked", G_CALLBACK(on_svc_action), (gpointer)"up");
    g_signal_connect(down, "clicked", G_CALLBACK(on_svc_action), (gpointer)"down");
    g_signal_connect(restart, "clicked", G_CALLBACK(on_svc_action), (gpointer)"restart");
    gtk_box_pack_start(GTK_BOX(row), up, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row), down, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row), restart, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 4);
    return box;
}

/* ================= Users tab ================= */
/* Password changes use chpasswd via GSubprocess (stdin, not argv) rather
 * than the shell menu's interactive `passwd` — a GUI has no terminal to
 * hand an interactive prompt to, so this is the GUI-appropriate
 * equivalent rather than a literal port. */

static GtkWidget *root_pw1, *root_pw2;
static GtkWidget *user_name_pw, *user_pw1, *user_pw2;
static GtkWidget *new_user_entry;

static void
on_root_pw_apply(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    const char *p1 = gtk_entry_get_text(GTK_ENTRY(root_pw1));
    const char *p2 = gtk_entry_get_text(GTK_ENTRY(root_pw2));
    log_clear();
    if (!*p1 || strcmp(p1, p2) != 0) {
        log_append("Passwords are empty or do not match.");
        return;
    }
    set_password("root", p1);
    gtk_entry_set_text(GTK_ENTRY(root_pw1), "");
    gtk_entry_set_text(GTK_ENTRY(root_pw2), "");
}

static void
on_user_pw_apply(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    const char *user = gtk_entry_get_text(GTK_ENTRY(user_name_pw));
    const char *p1 = gtk_entry_get_text(GTK_ENTRY(user_pw1));
    const char *p2 = gtk_entry_get_text(GTK_ENTRY(user_pw2));
    log_clear();
    if (!*user) { log_append("Enter a username first."); return; }
    if (!is_safe_token(user)) { log_append("Username contains characters that aren't allowed."); return; }
    if (!*p1 || strcmp(p1, p2) != 0) {
        log_append("Passwords are empty or do not match.");
        return;
    }
    set_password(user, p1);
    gtk_entry_set_text(GTK_ENTRY(user_pw1), "");
    gtk_entry_set_text(GTK_ENTRY(user_pw2), "");
}

static void
on_add_user(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    const char *name = gtk_entry_get_text(GTK_ENTRY(new_user_entry));
    log_clear();
    if (!*name) { log_append("Enter a username first."); return; }
    if (!is_safe_token(name)) { log_append("Username contains characters that aren't allowed."); return; }

    /* -D: create without prompting for a password; set one afterwards via
     * the password fields above. */
    gchar *script = g_strdup_printf("adduser -D %s", name);
    run_cmd(script);
    g_free(script);
}

static GtkWidget *
build_users_tab(void)
{
    GtkWidget *box = tab_box();

    GtkWidget *rlabel = gtk_label_new("Change Root Password:");
    gtk_label_set_xalign(GTK_LABEL(rlabel), 0.0);
    gtk_box_pack_start(GTK_BOX(box), rlabel, FALSE, FALSE, 0);
    root_pw1 = labeled_entry(box, "New Password", NULL);
    gtk_entry_set_visibility(GTK_ENTRY(root_pw1), FALSE);
    root_pw2 = labeled_entry(box, "Confirm Password", NULL);
    gtk_entry_set_visibility(GTK_ENTRY(root_pw2), FALSE);
    GtkWidget *rapply = gtk_button_new_with_label("Set Root Password");
    g_signal_connect(rapply, "clicked", G_CALLBACK(on_root_pw_apply), NULL);
    gtk_box_pack_start(GTK_BOX(box), rapply, FALSE, FALSE, 4);

    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

    GtkWidget *ulabel = gtk_label_new("Change User Password:");
    gtk_label_set_xalign(GTK_LABEL(ulabel), 0.0);
    gtk_box_pack_start(GTK_BOX(box), ulabel, FALSE, FALSE, 0);
    user_name_pw = labeled_entry(box, "Username", NULL);
    user_pw1 = labeled_entry(box, "New Password", NULL);
    gtk_entry_set_visibility(GTK_ENTRY(user_pw1), FALSE);
    user_pw2 = labeled_entry(box, "Confirm Password", NULL);
    gtk_entry_set_visibility(GTK_ENTRY(user_pw2), FALSE);
    GtkWidget *uapply = gtk_button_new_with_label("Set User Password");
    g_signal_connect(uapply, "clicked", G_CALLBACK(on_user_pw_apply), NULL);
    gtk_box_pack_start(GTK_BOX(box), uapply, FALSE, FALSE, 4);

    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

    GtkWidget *nlabel = gtk_label_new("Add New User:");
    gtk_label_set_xalign(GTK_LABEL(nlabel), 0.0);
    gtk_box_pack_start(GTK_BOX(box), nlabel, FALSE, FALSE, 0);
    new_user_entry = labeled_entry(box, "Username", NULL);
    GtkWidget *nadd = gtk_button_new_with_label("Add User");
    g_signal_connect(nadd, "clicked", G_CALLBACK(on_add_user), NULL);
    gtk_box_pack_start(GTK_BOX(box), nadd, FALSE, FALSE, 4);

    return box;
}

/* ================= Packages tab ================= */

static GtkWidget *pkg_search_entry, *pkg_install_entry;

static void
on_pkg_update(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    log_clear();
    run_cmd("pkg update");
}

static void
on_pkg_search(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    const char *q = gtk_entry_get_text(GTK_ENTRY(pkg_search_entry));
    log_clear();
    if (!*q) { log_append("Enter a search query first."); return; }
    if (!is_safe_token(q)) { log_append("Search query contains characters that aren't allowed."); return; }

    gchar *script = g_strdup_printf("pkg search %s", q);
    run_cmd(script);
    g_free(script);
}

static void
on_pkg_install(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    const char *p = gtk_entry_get_text(GTK_ENTRY(pkg_install_entry));
    log_clear();
    if (!*p) { log_append("Enter a package name first."); return; }
    if (!is_safe_token(p)) { log_append("Package name contains characters that aren't allowed."); return; }

    gchar *script = g_strdup_printf("pkg install %s", p);
    run_cmd(script);
    g_free(script);
}

static void
on_pkg_list(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    log_clear();
    run_cmd("pkg list");
}

static GtkWidget *
build_packages_tab(void)
{
    GtkWidget *box = tab_box();

    GtkWidget *update_btn = gtk_button_new_with_label("Update Package Indexes");
    g_signal_connect(update_btn, "clicked", G_CALLBACK(on_pkg_update), NULL);
    gtk_box_pack_start(GTK_BOX(box), update_btn, FALSE, FALSE, 0);

    pkg_search_entry = labeled_entry(box, "Search Query", NULL);
    GtkWidget *search_btn = gtk_button_new_with_label("Search");
    g_signal_connect(search_btn, "clicked", G_CALLBACK(on_pkg_search), NULL);
    gtk_box_pack_start(GTK_BOX(box), search_btn, FALSE, FALSE, 0);

    pkg_install_entry = labeled_entry(box, "Package Name", NULL);
    GtkWidget *install_btn = gtk_button_new_with_label("Install");
    g_signal_connect(install_btn, "clicked", G_CALLBACK(on_pkg_install), NULL);
    gtk_box_pack_start(GTK_BOX(box), install_btn, FALSE, FALSE, 0);

    GtkWidget *list_btn = gtk_button_new_with_label("List Installed Packages");
    g_signal_connect(list_btn, "clicked", G_CALLBACK(on_pkg_list), NULL);
    gtk_box_pack_start(GTK_BOX(box), list_btn, FALSE, FALSE, 4);

    return box;
}

/* ================= Install tab ================= */

static void
on_launch_installer(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    log_clear();
    if (!g_file_test("/usr/bin/rezzinstall", G_FILE_TEST_IS_EXECUTABLE)) {
        log_append("rezzinstall not found in /usr/bin!");
        return;
    }
    /* rezzinstall is its own GTK3 window, not a console tool, so it is
     * launched directly and asynchronously - no terminal wrapper, and no
     * blocking the control center's event loop while it runs. */
    GError *error = NULL;
    if (!g_spawn_command_line_async("/usr/bin/rezzinstall", &error)) {
        log_append(g_strdup_printf("Failed to launch rezzinstall: %s", error->message));
        g_error_free(error);
    }
}

static GtkWidget *
build_install_tab(void)
{
    GtkWidget *box = tab_box();
    GtkWidget *lbl = gtk_label_new(
        "Launch the interactive installer to put RezzOS onto a hard drive, SSD or USB.\n"
        "It opens in its own window.");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);

    GtkWidget *btn = gtk_button_new_with_label("Launch rezzinstall");
    g_signal_connect(btn, "clicked", G_CALLBACK(on_launch_installer), NULL);
    gtk_box_pack_start(GTK_BOX(box), btn, FALSE, FALSE, 4);
    return box;
}

/* ================= Power tab ================= */

static void
confirm_and_run(GtkWindow *parent, const char *question, const char *cmd)
{
    GtkWidget *dialog = gtk_message_dialog_new(parent,
        GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
        "%s", question);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response == GTK_RESPONSE_YES) {
        run_cmd(cmd);
    }
}

static void
on_reboot(GtkButton *b, gpointer window)
{
    (void)b;
    confirm_and_run(GTK_WINDOW(window), "Reboot the system now?", "reboot");
}

static void
on_poweroff(GtkButton *b, gpointer window)
{
    (void)b;
    confirm_and_run(GTK_WINDOW(window), "Power off the system now?", "poweroff");
}

static GtkWidget *
build_power_tab(GtkWidget *window)
{
    GtkWidget *box = tab_box();
    GtkWidget *reboot_btn = gtk_button_new_with_label("Reboot System");
    GtkWidget *poweroff_btn = gtk_button_new_with_label("Power Off System");
    g_signal_connect(reboot_btn, "clicked", G_CALLBACK(on_reboot), window);
    g_signal_connect(poweroff_btn, "clicked", G_CALLBACK(on_poweroff), window);
    gtk_box_pack_start(GTK_BOX(box), reboot_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), poweroff_btn, FALSE, FALSE, 0);
    return box;
}

/* ================= main window ================= */

static void
on_destroy(GtkWidget *w, gpointer d)
{
    (void)w; (void)d;
    gtk_main_quit();
}

static void
on_switch_page(GtkNotebook *nb, GtkWidget *page, guint num, gpointer d)
{
    (void)nb; (void)page; (void)num; (void)d;
    refresh_status_bar();
}

int
main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "RezzOS Control Center");
    gtk_window_set_default_size(GTK_WINDOW(window), 780, 640);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    /* Status bar: hostname / IP / font / swap, mirrors the shell menu's
     * header() banner that used to redraw on every screen. */
    status_bar_label = gtk_label_new("");
    gtk_widget_set_halign(status_bar_label, GTK_ALIGN_START);
    gtk_container_set_border_width(GTK_CONTAINER(status_bar_label), 0);
    gtk_widget_set_margin_start(status_bar_label, 10);
    gtk_widget_set_margin_top(status_bar_label, 8);
    gtk_widget_set_margin_bottom(status_bar_label, 4);
    gtk_box_pack_start(GTK_BOX(vbox), status_bar_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_network_tab(),  gtk_label_new("Network"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_hostname_tab(), gtk_label_new("Hostname"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_font_tab(),     gtk_label_new("Font"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_keyboard_tab(), gtk_label_new("Keyboard"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_swap_tab(),     gtk_label_new("Swap"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_services_tab(),gtk_label_new("Services"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_users_tab(),    gtk_label_new("Users"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_packages_tab(),gtk_label_new("Packages"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_install_tab(), gtk_label_new("Install"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_power_tab(window), gtk_label_new("Power"));

    g_signal_connect(notebook, "switch-page", G_CALLBACK(on_switch_page), NULL);

    /* Shared output log, visible under every tab. */
    GtkWidget *log_frame = gtk_frame_new("Output");
    gtk_box_pack_start(GTK_BOX(vbox), log_frame, FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scroll, -1, 180);
    gtk_container_add(GTK_CONTAINER(log_frame), scroll);

    log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(log_view), TRUE);
    gtk_container_add(GTK_CONTAINER(scroll), log_view);

    refresh_status_bar();
    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
