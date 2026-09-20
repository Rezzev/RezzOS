/* rsv-gtk.c — RezzOS Runit Service Manager (GTK3) */
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define DISK_SV_DIR "/mnt/disk/services"

enum {
    COL_NAME,
    COL_AUTOSTART,
    COL_STATUS,
    COL_ENABLED,   /* hidden boolean */
    N_COLS
};

typedef struct {
    GtkWidget    *window;
    GtkWidget    *tree;
    GtkListStore *store;
    GtkWidget    *statusbar;
    char         *sv_dir;
    char         *all_sv_dir;
} App;

static App app;

/* ---------- вспомогательные ---------- */

/* Каталог /mnt/disk существует всегда, поэтому проверяем /proc/mounts. */
static gboolean disk_is_mounted(void)
{
    FILE *fp = fopen("/proc/mounts", "r");
    if (!fp) return FALSE;
    char line[2048];
    gboolean found = FALSE;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, " /mnt/disk ")) { found = TRUE; break; }
    }
    fclose(fp);
    return found;
}

static char *run_capture(const char *cmd)
{
    FILE *fp = popen(cmd, "r");
    if (!fp) return g_strdup("");
    GString *out = g_string_new(NULL);
    char buf[1024];
    while (fgets(buf, sizeof(buf), fp))
        g_string_append(out, buf);
    pclose(fp);
    return g_string_free(out, FALSE);
}

/* Не даём экранировать имена сервисов в shell-командах. */
static gboolean valid_name(const char *s)
{
    if (!s || !*s) return FALSE;
    for (const char *p = s; *p; p++)
        if (!g_ascii_isalnum(*p) && *p != '_' && *p != '-' && *p != '.')
            return FALSE;
    return TRUE;
}

static void set_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *s = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    size_t n = strlen(s);
    while (n && (s[n-1] == '\n' || s[n-1] == '\r')) s[--n] = 0;
    gtk_statusbar_pop(GTK_STATUSBAR(app.statusbar), 0);
    gtk_statusbar_push(GTK_STATUSBAR(app.statusbar), 0, s);
    g_free(s);
}

/* ---------- список сервисов ---------- */

static void add_row(const char *name, gboolean enabled)
{
    char status[32] = "[ stopped ]";

    if (enabled) {
        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "sv status '%s/%s' 2>&1", app.sv_dir, name);
        char *out = run_capture(cmd);
        if (g_str_has_prefix(out, "run:"))
            strcpy(status, "[ active ]");
        else if (g_str_has_prefix(out, "down:"))
            strcpy(status, "[ down ]");
        else
            strcpy(status, "[ error ]");
        g_free(out);
    }

    GtkTreeIter it;
    gtk_list_store_append(app.store, &it);
    gtk_list_store_set(app.store, &it,
                       COL_NAME,      name,
                       COL_AUTOSTART, enabled ? "enabled" : "disabled",
                       COL_STATUS,    status,
                       COL_ENABLED,   enabled,
                       -1);
}

static void refresh_list(void)
{
    gtk_list_store_clear(app.store);

    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    /* Все, что есть в /etc/sv */
    DIR *d = opendir(app.all_sv_dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            if (!valid_name(e->d_name)) continue;

            char path[2048], run[2048];
            snprintf(path, sizeof(path), "%s/%s", app.all_sv_dir, e->d_name);
            struct stat st;
            if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
            snprintf(run, sizeof(run), "%s/run", path);
            if (!g_file_test(run, G_FILE_TEST_EXISTS)) continue;

            char svpath[2048];
            snprintf(svpath, sizeof(svpath), "%s/%s", app.sv_dir, e->d_name);
            gboolean enabled = g_file_test(svpath, G_FILE_TEST_EXISTS);

            add_row(e->d_name, enabled);
            g_hash_table_add(seen, g_strdup(e->d_name));
        }
        closedir(d);
    }

    /* Включено, но отсутствует в /etc/sv (ручное вмешательство и т.п.) */
    d = opendir(app.sv_dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            if (g_hash_table_contains(seen, e->d_name)) continue;

            char path[2048], run[2048];
            snprintf(path, sizeof(path), "%s/%s", app.sv_dir, e->d_name);
            struct stat st;
            if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
            snprintf(run, sizeof(run), "%s/run", path);
            if (!g_file_test(run, G_FILE_TEST_EXISTS)) continue;

            add_row(e->d_name, TRUE);
        }
        closedir(d);
    }

    g_hash_table_destroy(seen);
}

static char *get_selected(void)
{
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(app.tree));
    GtkTreeModel *model;
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(sel, &model, &it))
        return NULL;
    char *name = NULL;
    gtk_tree_model_get(model, &it, COL_NAME, &name, -1);
    return name;
}

/* ---------- действия ---------- */

static void do_control(const char *action)
{
    char *name = get_selected();
    if (!name) { set_status("No service selected."); return; }
    if (!valid_name(name)) { set_status("Invalid service name."); g_free(name); return; }

    char svpath[2048];
    snprintf(svpath, sizeof(svpath), "%s/%s", app.sv_dir, name);
    const char *target = svpath;

    char allpath[2048];
    if (!g_file_test(svpath, G_FILE_TEST_EXISTS)) {
        snprintf(allpath, sizeof(allpath), "%s/%s", app.all_sv_dir, name);
        target = allpath;
    }

    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "sv %s '%s' 2>&1", action, target);
    char *out = run_capture(cmd);
    set_status("sv %s %s: %s", action, name, out);
    g_free(out);
    g_free(name);
    refresh_list();
}

static void do_enable(void)
{
    char *name = get_selected();
    if (!name) { set_status("No service selected."); return; }
    if (!valid_name(name)) { set_status("Invalid service name."); g_free(name); return; }

    char allpath[2048], svpath[2048];
    snprintf(allpath, sizeof(allpath), "%s/%s", app.all_sv_dir, name);
    snprintf(svpath, sizeof(svpath), "%s/%s", app.sv_dir, name);

    if (!g_file_test(allpath, G_FILE_TEST_IS_DIR)) {
        set_status("Service '%s' does not exist in %s.", name, app.all_sv_dir);
        g_free(name); return;
    }
    if (g_file_test(svpath, G_FILE_TEST_EXISTS)) {
        set_status("Service '%s' is already enabled.", name);
        g_free(name); return;
    }

    g_mkdir_with_parents(app.sv_dir, 0755);

    if (symlink(allpath, svpath) != 0) {
        set_status("Failed to create symlink: %s", g_strerror(errno));
        g_free(name); return;
    }

    if (disk_is_mounted()) {
        g_mkdir_with_parents(DISK_SV_DIR, 0755);
        char diskpath[2048];
        snprintf(diskpath, sizeof(diskpath), "%s/%s", DISK_SV_DIR, name);
        FILE *fp = fopen(diskpath, "w");
        if (fp) fclose(fp);
        set_status("Enabled '%s' (persistent).", name);
    } else {
        set_status("Enabled '%s' (until next reboot — no persistent disk).", name);
    }

    g_free(name);
    refresh_list();
}

static void do_disable(void)
{
    char *name = get_selected();
    if (!name) { set_status("No service selected."); return; }
    if (!valid_name(name)) { set_status("Invalid service name."); g_free(name); return; }

    /* Маркер с диска снимаем всегда — иначе init вернёт сервис при ребуте. */
    if (disk_is_mounted()) {
        char diskpath[2048];
        snprintf(diskpath, sizeof(diskpath), "%s/%s", DISK_SV_DIR, name);
        unlink(diskpath);
    }

    char svpath[2048], supervise[2048];
    snprintf(svpath, sizeof(svpath), "%s/%s", app.sv_dir, name);
    snprintf(supervise, sizeof(supervise), "%s/supervise", svpath);

    if (!g_file_test(svpath, G_FILE_TEST_EXISTS)) {
        set_status("Service '%s' is not enabled.", name);
        g_free(name); return;
    }

    if (g_file_test(supervise, G_FILE_TEST_IS_DIR)) {
        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "sv down '%s' 2>/dev/null", svpath);
        system(cmd);
    }

    if (unlink(svpath) != 0)
        set_status("Failed to remove symlink: %s", g_strerror(errno));
    else
        set_status("Disabled '%s'.", name);

    g_free(name);
    refresh_list();
}

static void do_log(void)
{
    char *name = get_selected();
    if (!name) { set_status("No service selected."); return; }
    if (!valid_name(name)) { set_status("Invalid service name."); g_free(name); return; }

    char p1[2048], p2[2048];
    snprintf(p1, sizeof(p1), "/var/log/%s.log", name);
    snprintf(p2, sizeof(p2), "/var/log/%s/current", name);

    const char *use = NULL;
    if (g_file_test(p1, G_FILE_TEST_EXISTS)) use = p1;
    else if (g_file_test(p2, G_FILE_TEST_EXISTS)) use = p2;

    if (!use) {
        set_status("No log file found for '%s'.", name);
        g_free(name); return;
    }

    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "tail -n 200 '%s' 2>&1", use);
    char *out = run_capture(cmd);

    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        "Service Log", GTK_WINDOW(app.window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close", GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 720, 500);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *view   = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_set_text(buf, out ? out : "", -1);
    gtk_container_add(GTK_CONTAINER(scroll), view);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_add(GTK_CONTAINER(content), scroll);

    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);

    g_free(out);
    g_free(name);
}

static void do_create(GtkWindow *parent)
{
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        "Create Service", parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Create", GTK_RESPONSE_OK, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 480, -1);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);

    GtkWidget *l1 = gtk_label_new("Service name:");
    gtk_widget_set_halign(l1, GTK_ALIGN_START);
    GtkWidget *e1 = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(e1), "myservice");
    gtk_widget_set_hexpand(e1, TRUE);

    GtkWidget *l2 = gtk_label_new("Command:");
    gtk_widget_set_halign(l2, GTK_ALIGN_START);
    GtkWidget *e2 = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(e2), "/usr/bin/mydaemon --flag");
    gtk_widget_set_hexpand(e2, TRUE);

    gtk_grid_attach(GTK_GRID(grid), l1, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), e1, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), l2, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), e2, 1, 1, 1, 1);

    gtk_container_add(GTK_CONTAINER(content), grid);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        const char *name = gtk_entry_get_text(GTK_ENTRY(e1));
        const char *cmd  = gtk_entry_get_text(GTK_ENTRY(e2));

        if (!valid_name(name)) {
            set_status("Invalid service name.");
        } else if (!cmd || !*cmd) {
            set_status("Command must not be empty.");
        } else {
            char dir[2048], path[2048];
            snprintf(dir, sizeof(dir), "%s/%s", app.all_sv_dir, name);
            snprintf(path, sizeof(path), "%s/run", dir);

            if (g_mkdir_with_parents(dir, 0755) != 0) {
                set_status("Failed to create directory: %s", g_strerror(errno));
            } else {
                FILE *fp = fopen(path, "w");
                if (!fp) {
                    set_status("Failed to create run script: %s", g_strerror(errno));
                } else {
                    fprintf(fp,
                        "#!/bin/sh\n"
                        "exec %s >> /var/log/%s.log 2>&1\n",
                        cmd, name);
                    fclose(fp);
                    chmod(path, 0755);
                    set_status("Created service '%s'. Enable it to auto-start.", name);
                }
            }
            refresh_list();
        }
    }

    gtk_widget_destroy(dlg);
}

/* ---------- сигналы кнопок ---------- */

static void on_refresh(GtkWidget *w, gpointer d) { (void)w; (void)d; refresh_list(); set_status("List refreshed."); }
static void on_up     (GtkWidget *w, gpointer d) { (void)w; (void)d; do_control("up"); }
static void on_down   (GtkWidget *w, gpointer d) { (void)w; (void)d; do_control("down"); }
static void on_restart(GtkWidget *w, gpointer d) { (void)w; (void)d; do_control("restart"); }
static void on_enable (GtkWidget *w, gpointer d) { (void)w; (void)d; do_enable(); }
static void on_disable(GtkWidget *w, gpointer d) { (void)w; (void)d; do_disable(); }
static void on_log    (GtkWidget *w, gpointer d) { (void)w; (void)d; do_log(); }
static void on_create (GtkWidget *w, gpointer d) { (void)w; (void)d; do_create(GTK_WINDOW(app.window)); }

static void on_row_activated(GtkTreeView *tv, GtkTreePath *path,
                             GtkTreeViewColumn *col, gpointer d)
{
    (void)tv; (void)path; (void)col; (void)d;
    do_log();
}

/* ---------- построение интерфейса ---------- */

static GtkWidget *make_button(const char *label, const char *tip, GCallback cb)
{
    GtkWidget *b = gtk_button_new_with_label(label);
    if (tip) gtk_widget_set_tooltip_text(b, tip);
    g_signal_connect(b, "clicked", cb, NULL);
    return b;
}

static void build_ui(void)
{
    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "RezzOS Runit Service Manager");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 760, 500);
    g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(app.window), vbox);

    /* Панель кнопок */
    GtkWidget *tb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(tb), 6);

    gtk_box_pack_start(GTK_BOX(tb),
        make_button("Refresh", "Reload service list", G_CALLBACK(on_refresh)),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tb),
        gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tb),
        make_button("Start",   "sv up <service>",      G_CALLBACK(on_up)),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tb),
        make_button("Stop",    "sv down <service>",    G_CALLBACK(on_down)),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tb),
        make_button("Restart", "sv restart <service>", G_CALLBACK(on_restart)),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tb),
        gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tb),
        make_button("Enable",  "Auto-start at boot",   G_CALLBACK(on_enable)),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tb),
        make_button("Disable", "Remove from autostart",G_CALLBACK(on_disable)),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tb),
        gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tb),
        make_button("Log",     "View service log",     G_CALLBACK(on_log)),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tb),
        make_button("Create…", "Create a new service", G_CALLBACK(on_create)),
        FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), tb, FALSE, FALSE, 0);

    /* Таблица сервисов */
    app.store = gtk_list_store_new(N_COLS,
                                   G_TYPE_STRING, G_TYPE_STRING,
                                   G_TYPE_STRING, G_TYPE_BOOLEAN);
    app.tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app.store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(app.tree), TRUE);

    const char *titles[] = { "Service", "Autostart", "Status" };
    for (int i = 0; i < 3; i++) {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        if (i == 2)
            g_object_set(r, "family", "monospace", NULL);
        GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(
            titles[i], r, "text", i, NULL);
        gtk_tree_view_column_set_resizable(c, TRUE);
        if (i == 0) gtk_tree_view_column_set_expand(c, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(app.tree), c);
    }

    g_signal_connect(app.tree, "row-activated",
                     G_CALLBACK(on_row_activated), NULL);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), app.tree);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    /* Строка состояния */
    app.statusbar = gtk_statusbar_new();
    gtk_box_pack_start(GTK_BOX(vbox), app.statusbar, FALSE, FALSE, 0);
    set_status("Ready. %s",
               disk_is_mounted()
                   ? "Persistent disk mounted — enable/disable survives reboot."
                   : "No persistent disk — enable/disable lasts until reboot.");
}

int main(int argc, char **argv)
{
    const char *e;

    e = g_getenv("SERVICE_DIR");
    app.sv_dir = (e && *e) ? g_strdup(e) : g_strdup("/etc/runit/runsvdir/default");

    e = g_getenv("ALL_SERVICE_DIR");
    app.all_sv_dir = (e && *e) ? g_strdup(e) : g_strdup("/etc/sv");

    gtk_init(&argc, &argv);
    build_ui();
    refresh_list();
    gtk_widget_show_all(app.window);
    gtk_main();

    g_free(app.sv_dir);
    g_free(app.all_sv_dir);
    return 0;
}
