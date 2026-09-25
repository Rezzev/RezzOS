/*
 * rezztop-gui — GTK3 system monitor for RezzOS.
 *
 * Reads /proc directly (no external tools shelled out): overall CPU and
 * memory usage, and a sortable process list with per-process CPU%/RSS,
 * refreshed every 2 seconds. Select a row to send SIGTERM/SIGKILL.
 *
 * Build:
 *   gcc -O2 rezztop-gui.c $(pkg-config --cflags --libs gtk+-3.0) -o rezztop-gui
 */

#include <gtk/gtk.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    COL_PID = 0,
    COL_UID,
    COL_NAME,
    COL_STATE,
    COL_CPU,
    COL_MEM_MB,
    N_PROC_COLS
};

static GtkWidget *window;
static GtkWidget *tree_view;
static GtkListStore *store;
static GtkWidget *cpu_bar, *mem_bar, *swap_bar;
static GtkWidget *cpu_graph;
static GtkWidget *summary_label;

/* Ring buffer of recent overall-CPU samples, for the little history graph. */
#define GRAPH_POINTS 60
static gdouble cpu_history[GRAPH_POINTS];
static int cpu_history_pos = 0;

/* Per-process previous (utime+stime) in clock ticks, keyed by PID, so we
 * can compute a CPU% delta between refreshes instead of a since-boot
 * average that would only ever go down. */
static GHashTable *prev_proc_ticks = NULL;
static guint64 prev_total_ticks = 0;
static gboolean have_prev_total = FALSE;

/* ---------- /proc/stat: system-wide CPU ---------- */

static guint64
read_total_cpu_ticks(gdouble *busy_fraction_out)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f) { *busy_fraction_out = 0.0; return 0; }

    char line[512];
    guint64 user = 0, nice = 0, system_ = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
    if (fgets(line, sizeof(line), f)) {
        sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
            (unsigned long long *)&user, (unsigned long long *)&nice,
            (unsigned long long *)&system_, (unsigned long long *)&idle,
            (unsigned long long *)&iowait, (unsigned long long *)&irq,
            (unsigned long long *)&softirq, (unsigned long long *)&steal);
    }
    fclose(f);

    guint64 idle_all = idle + iowait;
    guint64 busy_all = user + nice + system_ + irq + softirq + steal;
    guint64 total = idle_all + busy_all;

    static guint64 prev_idle = 0, prev_total = 0;
    static gboolean have_prev = FALSE;

    if (have_prev && total > prev_total) {
        guint64 dtotal = total - prev_total;
        guint64 didle = idle_all - prev_idle;
        *busy_fraction_out = dtotal > 0 ? 1.0 - ((gdouble)didle / (gdouble)dtotal) : 0.0;
    } else {
        *busy_fraction_out = 0.0;
    }

    prev_idle = idle_all;
    prev_total = total;
    have_prev = TRUE;

    return total;
}

/* ---------- /proc/meminfo ---------- */

static void
read_meminfo(guint64 *mem_total_kb, guint64 *mem_avail_kb,
             guint64 *swap_total_kb, guint64 *swap_free_kb)
{
    *mem_total_kb = *mem_avail_kb = *swap_total_kb = *swap_free_kb = 0;

    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        guint64 val;
        if (sscanf(line, "MemTotal: %llu", (unsigned long long *)&val) == 1) *mem_total_kb = val;
        else if (sscanf(line, "MemAvailable: %llu", (unsigned long long *)&val) == 1) *mem_avail_kb = val;
        else if (sscanf(line, "SwapTotal: %llu", (unsigned long long *)&val) == 1) *swap_total_kb = val;
        else if (sscanf(line, "SwapFree: %llu", (unsigned long long *)&val) == 1) *swap_free_kb = val;
    }
    fclose(f);
}

/* ---------- per-process info ---------- */

typedef struct {
    int pid;
    int uid;
    char name[256];
    char state;
    guint64 ticks;    /* utime + stime, in clock ticks */
    long rss_kb;
} ProcInfo;

static gboolean
read_proc_stat_line(int pid, char *name_out, size_t name_sz, char *state_out, guint64 *ticks_out)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return FALSE;

    char buf[1024];
    if (!fgets(buf, sizeof(buf), f)) { fclose(f); return FALSE; }
    fclose(f);

    /* Format: pid (name) state ... utime stime are fields 14,15. The name
     * can itself contain spaces or parentheses, so find the *last* ')' to
     * split reliably rather than assuming a simple space-separated field. */
    char *open_paren = strchr(buf, '(');
    char *close_paren = strrchr(buf, ')');
    if (!open_paren || !close_paren || close_paren < open_paren) return FALSE;

    size_t namelen = (size_t)(close_paren - open_paren - 1);
    if (namelen >= name_sz) namelen = name_sz - 1;
    memcpy(name_out, open_paren + 1, namelen);
    name_out[namelen] = '\0';

    /* Everything after "') '" is space-separated fields starting at state
     * (field 3). utime is field 14 overall, i.e. the 12th field here. */
    char *rest = close_paren + 2;
    char state = 0;
    guint64 utime = 0, stime = 0;
    int field = 3;
    char *tok = strtok(rest, " ");
    while (tok) {
        if (field == 3) state = tok[0];
        else if (field == 14) utime = strtoull(tok, NULL, 10);
        else if (field == 15) { stime = strtoull(tok, NULL, 10); break; }
        tok = strtok(NULL, " ");
        field++;
    }

    *state_out = state;
    *ticks_out = utime + stime;
    return TRUE;
}

static void
read_proc_status_extra(int pid, int *uid_out, long *rss_kb_out)
{
    *uid_out = -1;
    *rss_kb_out = 0;

    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        int uid;
        long rss;
        if (sscanf(line, "Uid: %d", &uid) == 1) *uid_out = uid;
        else if (sscanf(line, "VmRSS: %ld", &rss) == 1) *rss_kb_out = rss;
    }
    fclose(f);
}

/* ---------- refresh cycle ---------- */

static gboolean
draw_cpu_graph(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    (void)data;
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    int w = alloc.width, h = alloc.height;

    cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);
    cairo_paint(cr);

    cairo_set_source_rgb(cr, 0.2, 0.8, 0.3);
    cairo_set_line_width(cr, 1.5);

    gboolean first = TRUE;
    for (int i = 0; i < GRAPH_POINTS; i++) {
        int idx = (cpu_history_pos + i) % GRAPH_POINTS;
        gdouble v = cpu_history[idx];
        gdouble x = (gdouble)i / (GRAPH_POINTS - 1) * w;
        gdouble y = h - (v * h);
        if (first) { cairo_move_to(cr, x, y); first = FALSE; }
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);
    return FALSE;
}

static void
refresh_all(void)
{
    /* --- overall CPU + graph --- */
    gdouble busy_fraction = 0.0;
    read_total_cpu_ticks(&busy_fraction);

    cpu_history[cpu_history_pos] = busy_fraction;
    cpu_history_pos = (cpu_history_pos + 1) % GRAPH_POINTS;
    gtk_widget_queue_draw(cpu_graph);

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cpu_bar), busy_fraction);
    gchar *cpu_text = g_strdup_printf("CPU: %.0f%%", busy_fraction * 100.0);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(cpu_bar), cpu_text);
    g_free(cpu_text);

    /* --- memory / swap --- */
    guint64 mem_total, mem_avail, swap_total, swap_free;
    read_meminfo(&mem_total, &mem_avail, &swap_total, &swap_free);

    gdouble mem_frac = mem_total > 0 ? 1.0 - ((gdouble)mem_avail / (gdouble)mem_total) : 0.0;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(mem_bar), mem_frac);
    gchar *mem_text = g_strdup_printf("Memory: %.0f%% (%.1f / %.1f GB)",
        mem_frac * 100.0, (mem_total - mem_avail) / 1048576.0, mem_total / 1048576.0);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(mem_bar), mem_text);
    g_free(mem_text);

    gdouble swap_frac = swap_total > 0 ? 1.0 - ((gdouble)swap_free / (gdouble)swap_total) : 0.0;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(swap_bar), swap_total > 0 ? swap_frac : 0.0);
    gchar *swap_text = swap_total > 0
        ? g_strdup_printf("Swap: %.0f%% (%.1f / %.1f GB)",
            swap_frac * 100.0, (swap_total - swap_free) / 1048576.0, swap_total / 1048576.0)
        : g_strdup("Swap: none");
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(swap_bar), swap_text);
    g_free(swap_text);

    /* --- process list --- */
    GHashTable *new_ticks = g_hash_table_new(g_direct_hash, g_direct_equal);
    guint64 total_ticks_now;
    {
        /* Re-read /proc/stat's raw total (without touching the busy-fraction
         * static state above) for the per-process delta denominator. */
        FILE *f = fopen("/proc/stat", "r");
        guint64 u=0,n=0,s=0,idl=0,io=0,irq=0,soft=0,st=0;
        if (f) {
            char line[512];
            if (fgets(line, sizeof(line), f))
                sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                    (unsigned long long *)&u,(unsigned long long *)&n,(unsigned long long *)&s,
                    (unsigned long long *)&idl,(unsigned long long *)&io,(unsigned long long *)&irq,
                    (unsigned long long *)&soft,(unsigned long long *)&st);
            fclose(f);
        }
        total_ticks_now = u+n+s+idl+io+irq+soft+st;
    }
    guint64 dtotal = (have_prev_total && total_ticks_now > prev_total_ticks)
        ? total_ticks_now - prev_total_ticks : 0;

    gtk_list_store_clear(store);

    GDir *dir = g_dir_open("/proc", 0, NULL);
    if (dir) {
        const gchar *name;
        while ((name = g_dir_read_name(dir)) != NULL) {
            gboolean all_digits = *name != '\0';
            for (const gchar *p = name; *p; p++)
                if (!g_ascii_isdigit(*p)) { all_digits = FALSE; break; }
            if (!all_digits) continue;

            int pid = atoi(name);
            char pname[256] = {0};
            char state = '?';
            guint64 ticks = 0;
            if (!read_proc_stat_line(pid, pname, sizeof(pname), &state, &ticks))
                continue;

            int uid = -1;
            long rss_kb = 0;
            read_proc_status_extra(pid, &uid, &rss_kb);

            gpointer prev_val = g_hash_table_lookup(prev_proc_ticks ? prev_proc_ticks : new_ticks,
                                                     GINT_TO_POINTER(pid));
            guint64 prev_ticks = prev_val ? (guint64)GPOINTER_TO_SIZE(prev_val) : ticks;
            guint64 dproc = ticks > prev_ticks ? ticks - prev_ticks : 0;
            gdouble cpu_pct = dtotal > 0 ? (100.0 * (gdouble)dproc / (gdouble)dtotal) : 0.0;

            g_hash_table_insert(new_ticks, GINT_TO_POINTER(pid), GSIZE_TO_POINTER((gsize)ticks));

            GtkTreeIter iter;
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter,
                COL_PID, pid,
                COL_UID, uid,
                COL_NAME, pname,
                COL_STATE, (gchar[]){state, '\0'},
                COL_CPU, cpu_pct,
                COL_MEM_MB, rss_kb / 1024.0,
                -1);
        }
        g_dir_close(dir);
    }

    if (prev_proc_ticks) g_hash_table_destroy(prev_proc_ticks);
    prev_proc_ticks = new_ticks;
    prev_total_ticks = total_ticks_now;
    have_prev_total = TRUE;

    gint n_procs = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(store), NULL);
    gchar *summary = g_strdup_printf("%d processes", n_procs);
    gtk_label_set_text(GTK_LABEL(summary_label), summary);
    g_free(summary);
}

static gboolean
on_timer(gpointer data)
{
    (void)data;
    refresh_all();
    return G_SOURCE_CONTINUE;
}

/* ---------- kill actions ---------- */

static gboolean
get_selected_pid(int *pid_out)
{
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    GtkTreeModel *model;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter))
        return FALSE;
    gtk_tree_model_get(model, &iter, COL_PID, pid_out, -1);
    return TRUE;
}

static void
send_signal_to_selected(int sig, const char *label)
{
    int pid;
    if (!get_selected_pid(&pid)) {
        GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "Select a process first.");
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
        return;
    }

    GtkWidget *confirm = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO, "%s process %d?", label, pid);
    gint r = gtk_dialog_run(GTK_DIALOG(confirm));
    gtk_widget_destroy(confirm);
    if (r != GTK_RESPONSE_YES) return;

    if (kill(pid, sig) != 0) {
        GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Could not signal process %d (permission denied, or it already exited).", pid);
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
    }
    refresh_all();
}

static void on_kill_clicked(GtkButton *b, gpointer d)       { (void)b; (void)d; send_signal_to_selected(SIGTERM, "Terminate"); }
static void on_force_kill_clicked(GtkButton *b, gpointer d) { (void)b; (void)d; send_signal_to_selected(SIGKILL, "Force-kill"); }

/* ---------- sorting ---------- */

static gint
sort_by_double_col(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer col_ptr)
{
    gint col = GPOINTER_TO_INT(col_ptr);
    gdouble va, vb;
    gtk_tree_model_get(model, a, col, &va, -1);
    gtk_tree_model_get(model, b, col, &vb, -1);
    return (va > vb) - (va < vb);
}

static void
on_destroy(GtkWidget *w, gpointer d)
{
    (void)w; (void)d;
    gtk_main_quit();
}

int
main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "RezzOS System Monitor");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 640);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    /* --- top: bars + graph --- */
    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(vbox), top, FALSE, FALSE, 0);

    GtkWidget *bars = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    cpu_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(cpu_bar), TRUE);
    mem_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(mem_bar), TRUE);
    swap_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(swap_bar), TRUE);
    gtk_box_pack_start(GTK_BOX(bars), cpu_bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bars), mem_bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bars), swap_bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), bars, TRUE, TRUE, 0);

    cpu_graph = gtk_drawing_area_new();
    gtk_widget_set_size_request(cpu_graph, 220, 70);
    g_signal_connect(cpu_graph, "draw", G_CALLBACK(draw_cpu_graph), NULL);
    gtk_box_pack_start(GTK_BOX(top), cpu_graph, FALSE, FALSE, 0);

    /* --- process list --- */
    store = gtk_list_store_new(N_PROC_COLS,
        G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_DOUBLE, G_TYPE_DOUBLE);

    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);

    struct { const char *title; int col; const char *fmt_type; } cols[] = {
        { "PID",  COL_PID,    "int" },
        { "UID",  COL_UID,    "int" },
        { "Name", COL_NAME,   "str" },
        { "State",COL_STATE,  "str" },
        { "CPU %",COL_CPU,    "pct" },
        { "MEM (MB)", COL_MEM_MB, "mb" },
    };
    for (size_t i = 0; i < G_N_ELEMENTS(cols); i++) {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *c;
        if (strcmp(cols[i].fmt_type, "pct") == 0) {
            c = gtk_tree_view_column_new_with_attributes(cols[i].title, r, "text", cols[i].col, NULL);
            gtk_tree_view_column_set_cell_data_func(c, r,
                (GtkTreeCellDataFunc)(void (*)(void))
                (void *)NULL, NULL, NULL); /* placeholder removed below */
        } else {
            c = gtk_tree_view_column_new_with_attributes(cols[i].title, r, "text", cols[i].col, NULL);
        }
        gtk_tree_view_column_set_resizable(c, TRUE);
        gtk_tree_view_column_set_sort_column_id(c, cols[i].col);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), c);
    }

    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store), COL_CPU,
        sort_by_double_col, GINT_TO_POINTER(COL_CPU), NULL);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store), COL_MEM_MB,
        sort_by_double_col, GINT_TO_POINTER(COL_MEM_MB), NULL);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), COL_CPU, GTK_SORT_DESCENDING);

    GtkWidget *list_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(list_scroll), tree_view);
    gtk_box_pack_start(GTK_BOX(vbox), list_scroll, TRUE, TRUE, 0);

    /* --- bottom bar --- */
    GtkWidget *bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *kill_btn = gtk_button_new_with_label("Terminate (SIGTERM)");
    GtkWidget *force_kill_btn = gtk_button_new_with_label("Force Kill (SIGKILL)");
    g_signal_connect(kill_btn, "clicked", G_CALLBACK(on_kill_clicked), NULL);
    g_signal_connect(force_kill_btn, "clicked", G_CALLBACK(on_force_kill_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(bottom), kill_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bottom), force_kill_btn, FALSE, FALSE, 0);
    summary_label = gtk_label_new("");
    gtk_box_pack_end(GTK_BOX(bottom), summary_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), bottom, FALSE, FALSE, 0);

    refresh_all();
    g_timeout_add_seconds(2, on_timer, NULL);

    gtk_widget_show_all(window);
    gtk_main();

    if (prev_proc_ticks) g_hash_table_destroy(prev_proc_ticks);
    return 0;
}
