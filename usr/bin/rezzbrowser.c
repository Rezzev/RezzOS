/*
 * rezzbrowser — minimal GTK3 + WebKitGTK browser for RezzOS.
 *
 * Forces software rendering through the WebKit API rather than relying on
 * environment variables, since env-based hints did not reliably prevent a
 * blank window on this system's GPU/driver stack.
 *
 * Usage: rezzbrowser <url>
 */

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <stdlib.h>
#include <string.h>

static GtkWidget *back_button;
static GtkWidget *forward_button;
static GtkWidget *url_entry;

static void
on_destroy(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    gtk_main_quit();
}

static void
update_nav_buttons(WebKitWebView *web_view)
{
    gtk_widget_set_sensitive(back_button,
        webkit_web_view_can_go_back(web_view));
    gtk_widget_set_sensitive(forward_button,
        webkit_web_view_can_go_forward(web_view));
}

static void
on_load_changed(WebKitWebView *web_view, WebKitLoadEvent load_event, gpointer data)
{
    GtkWindow *window = GTK_WINDOW(data);
    const gchar *uri = webkit_web_view_get_uri(web_view);
    gchar *title;

    switch (load_event) {
    case WEBKIT_LOAD_STARTED:
        title = g_strdup_printf("Loading... - rezzbrowser");
        gtk_window_set_title(window, title);
        g_free(title);
        break;
    case WEBKIT_LOAD_COMMITTED:
        if (uri) {
            gtk_entry_set_text(GTK_ENTRY(url_entry), uri);
        }
        break;
    case WEBKIT_LOAD_FINISHED:
        title = g_strdup_printf("%s - rezzbrowser", uri ? uri : "");
        gtk_window_set_title(window, title);
        g_free(title);
        break;
    default:
        break;
    }

    update_nav_buttons(web_view);
}

static void
on_back_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    webkit_web_view_go_back(WEBKIT_WEB_VIEW(data));
}

static void
on_forward_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    webkit_web_view_go_forward(WEBKIT_WEB_VIEW(data));
}

static void
on_reload_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    webkit_web_view_reload(WEBKIT_WEB_VIEW(data));
}

/* Load whatever is typed in the address bar. If it doesn't look like a URL
 * (no scheme, no dot), treat it as a search query instead of a raw address,
 * since typing a bare word and getting "could not resolve host" is not
 * useful. */
static void
load_from_entry(WebKitWebView *web_view, const gchar *text)
{
    gchar *uri;

    if (g_str_has_prefix(text, "http://") ||
        g_str_has_prefix(text, "https://") ||
        g_str_has_prefix(text, "file://")) {
        uri = g_strdup(text);
    } else if (strchr(text, ' ') == NULL && strchr(text, '.') != NULL) {
        uri = g_strdup_printf("https://%s", text);
    } else {
        gchar *escaped = g_uri_escape_string(text, NULL, FALSE);
        uri = g_strdup_printf("https://duckduckgo.com/html/?q=%s", escaped);
        g_free(escaped);
    }

    webkit_web_view_load_uri(web_view, uri);
    g_free(uri);
}

static void
on_url_activate(GtkEntry *entry, gpointer data)
{
    load_from_entry(WEBKIT_WEB_VIEW(data), gtk_entry_get_text(entry));
}

int
main(int argc, char *argv[])
{
    const char *start_url = "https://www.google.com";

    if (argc > 1) {
        start_url = argv[1];
    }

    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(window), 1024, 768);
    gtk_window_set_title(GTK_WINDOW(window), "rezzbrowser");
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    /* Force software rendering at the API level. This is more reliable than
     * WEBKIT_DISABLE_COMPOSITING_MODE / LIBGL_ALWAYS_SOFTWARE env vars,
     * which depend on being picked up at the right point during WebKit's
     * process init and did not consistently prevent a blank page here. */
    WebKitSettings *settings = webkit_settings_new();
    webkit_settings_set_hardware_acceleration_policy(
        settings, WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);
    webkit_settings_set_enable_javascript(settings, TRUE);
    webkit_settings_set_enable_developer_extras(settings, TRUE);

    GtkWidget *web_view = webkit_web_view_new_with_settings(settings);

    /* Navigation toolbar: back, forward, reload, address bar. */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(toolbar), 4);

    back_button = gtk_button_new_with_label("<");
    forward_button = gtk_button_new_with_label(">");
    GtkWidget *reload_button = gtk_button_new_with_label("Reload");
    url_entry = gtk_entry_new();

    gtk_widget_set_sensitive(back_button, FALSE);
    gtk_widget_set_sensitive(forward_button, FALSE);
    gtk_entry_set_text(GTK_ENTRY(url_entry), start_url);

    g_signal_connect(back_button, "clicked", G_CALLBACK(on_back_clicked), web_view);
    g_signal_connect(forward_button, "clicked", G_CALLBACK(on_forward_clicked), web_view);
    g_signal_connect(reload_button, "clicked", G_CALLBACK(on_reload_clicked), web_view);
    g_signal_connect(url_entry, "activate", G_CALLBACK(on_url_activate), web_view);

    gtk_box_pack_start(GTK_BOX(toolbar), back_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), forward_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), reload_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), url_entry, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), web_view, TRUE, TRUE, 0);

    g_signal_connect(web_view, "load-changed", G_CALLBACK(on_load_changed), window);

    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(web_view), start_url);

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}

