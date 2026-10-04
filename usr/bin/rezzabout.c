/*
 * rezzabout — small "About System" window for RezzOS (GTK3).
 *
 * Shows: the RezzOS title, the latest release, and two links
 * (Telegram, GitHub).
 *
 * >>> Fill in the three values below, nothing else needs editing. <<<
 * An empty value is shown as a dimmed "not set" line instead of a
 * broken link, so the window always looks right.
 *
 * Build:
 *   gcc -O2 -Wall -Wextra rezzabout.c $(pkg-config --cflags --libs gtk+-3.0) \
 *       -o rezzabout
 */

#include <gtk/gtk.h>

/* ====================== EDIT THESE ====================== */
#define RELEASE_VERSION "1.7.3 (2026-10-5)" /* Строка должна быть в кавычках */
#define TELEGRAM_URL    "https://t.me/Losk149" /* Добавьте вашу ссылку в кавычках */
#define GITHUB_URL      "https://github.com/Rezzev/RezzOS" /* Добавьте вашу ссылку в кавычках */

/* Optional: a browser to open links with (e.g. "/usr/bin/rezzbrowser" ).
 * Должен быть строкой в кавычках. Оставьте "", чтобы использовать браузер по умолчанию. */
#define BROWSER_CMD     "/usr/bin/rezzbrowser" 
/* ========================================================= */

static GtkWidget *main_window;
static GtkWidget *status_label;

static gboolean
on_activate_link(GtkLinkButton *btn, gpointer data)
{
    (void)data;
    const char *uri = gtk_link_button_get_uri(btn);
    GError *error = NULL;
    gboolean ok;

    if (*BROWSER_CMD) {
        gchar *argv[] = { (gchar *)BROWSER_CMD, (gchar *)uri, NULL };
        ok = g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                           NULL, NULL, NULL, &error);
    } else {
        ok = gtk_show_uri_on_window(GTK_WINDOW(main_window), uri,
                                    GDK_CURRENT_TIME, &error);
    }

    if (!ok) {
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), uri, -1);
        gtk_label_set_text(GTK_LABEL(status_label),
                           "Could not open a browser - link copied to clipboard.");
        gtk_widget_show(status_label);
    }
    g_clear_error(&error);
    return TRUE;   /* we handled it, skip GTK's default */
}

/* One "caption   value" row. */
static void
add_caption(GtkWidget *grid, int row, const char *caption)
{
    GtkWidget *lbl = gtk_label_new(caption);
    gtk_widget_set_halign(lbl, GTK_ALIGN_END);
    gtk_widget_set_valign(lbl, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "dim-label");
    gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
}

static GtkWidget *
make_value_label(const char *text)
{
    GtkWidget *lbl;
    if (text && *text) {
        lbl = gtk_label_new(text);
        gtk_label_set_selectable(GTK_LABEL(lbl), TRUE);
    } else {
        lbl = gtk_label_new("not set");
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "dim-label");
    }
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(lbl), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(lbl), 32);
    gtk_widget_set_hexpand(lbl, TRUE);
    return lbl;
}

static GtkWidget *
make_link(const char *url)
{
    if (!url || !*url)
        return make_value_label(NULL);

    GtkWidget *btn = gtk_link_button_new_with_label(url, url);
    gtk_widget_set_halign(btn, GTK_ALIGN_START);
    gtk_widget_set_hexpand(btn, TRUE);
    gtk_widget_set_tooltip_text(btn, url);
    g_signal_connect(btn, "activate-link", G_CALLBACK(on_activate_link), NULL);

    /* Long URLs get an ellipsis in the middle instead of stretching the window. */
    GtkWidget *child = gtk_bin_get_child(GTK_BIN(btn));
    if (GTK_IS_LABEL(child)) {
        gtk_label_set_ellipsize(GTK_LABEL(child), PANGO_ELLIPSIZE_MIDDLE);
        gtk_label_set_max_width_chars(GTK_LABEL(child), 32);
        gtk_label_set_xalign(GTK_LABEL(child), 0.0);
    }
    return btn;
}

static gboolean
on_key(GtkWidget *w, GdkEventKey *ev, gpointer data)
{
    (void)w; (void)data;
    if (ev->keyval == GDK_KEY_Escape) {
        gtk_main_quit();
        return TRUE;
    }
    return FALSE;
}

int
main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_window), "About RezzOS");
    gtk_window_set_position(GTK_WINDOW(main_window), GTK_WIN_POS_CENTER);
    gtk_window_set_resizable(GTK_WINDOW(main_window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(main_window), 360, -1);
    g_signal_connect(main_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(main_window, "key-press-event", G_CALLBACK(on_key), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 20);
    gtk_container_add(GTK_CONTAINER(main_window), vbox);

    /* Title */
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
                         "<span size='xx-large' weight='bold'>RezzOS</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox),
                       gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    /* Info rows */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_box_pack_start(GTK_BOX(vbox), grid, FALSE, FALSE, 0);

    add_caption(grid, 0, "Latest release");
    gtk_grid_attach(GTK_GRID(grid), make_value_label(RELEASE_VERSION), 1, 0, 1, 1);

    add_caption(grid, 1, "Telegram");
    gtk_grid_attach(GTK_GRID(grid), make_link(TELEGRAM_URL), 1, 1, 1, 1);

    add_caption(grid, 2, "GitHub");
    gtk_grid_attach(GTK_GRID(grid), make_link(GITHUB_URL), 1, 2, 1, 1);

    /* Feedback line, hidden until a link fails to open */
    status_label = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(status_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(status_label), 40);
    gtk_style_context_add_class(gtk_widget_get_style_context(status_label), "dim-label");
    gtk_box_pack_start(GTK_BOX(vbox), status_label, FALSE, FALSE, 0);

    /* Close button */
    GtkWidget *close_btn = gtk_button_new_with_label("Close");
    gtk_widget_set_halign(close_btn, GTK_ALIGN_END);
    g_signal_connect(close_btn, "clicked", G_CALLBACK(gtk_main_quit), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), close_btn, FALSE, FALSE, 0);

    gtk_widget_show_all(main_window);
    gtk_widget_hide(status_label);   /* show_all made it visible; hide until needed */
    gtk_main();
    return 0;
}
