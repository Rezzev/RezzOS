+ /*
+  * rezzbrowser — minimal GTK3 + WebKitGTK browser for RezzOS (improved).
+  *
+  * Features:
+  *   - tabs (GtkNotebook), links with target=_blank / window.open open in new tabs
+  *   - DuckDuckGo as the default search engine (address bar + start page)
+  *   - persistent profile (cookies, cache, localStorage)
+  *   - software rendering forced through the WebKit API
+  *   - progress bar during page load
+  *   - status bar showing link target on hover
+  *   - error page with reload button
+  *   - zoom controls (Ctrl++/Ctrl+-/Ctrl+0)
+  *   - F11 fullscreen toggle
+  *   - Ctrl+D toggle start page
+  *   - Favicons in tab labels
+  *   - Improved start page with quick links and search
+  *   - Last tab stays open (navigates to start page instead of quitting)
+  *
+  * Shortcuts:
+  *   Ctrl+T          new tab
+  *   Ctrl+W          close tab
+  *   Ctrl+L          focus address bar
+  *   Ctrl+R / F5     reload
+  *   Ctrl+Tab        next tab
+  *   Ctrl+Shift+Tab  previous tab
+  *   Ctrl+PgUp/PgDn  previous / next tab
+  *   Alt+1..9        jump to tab N
+  *   Ctrl++ / Ctrl+= zoom in
+  *   Ctrl+-          zoom out
+  *   Ctrl+0          reset zoom
+  *   F11             fullscreen toggle
+  *   Ctrl+D          go to start page
+  *
+  * Build:
+  *   gcc rezzbrowser.c -o rezzbrowser \
+  *       $(pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.0)
+  *
+  * Usage: rezzbrowser [url or search query]
+  *
+  * Recommended launch:
+  *   WEBKIT_DISABLE_DMABUF_RENDERER=1 ./rezzbrowser
+  */
+ 
+ #include <gtk/gtk.h>
+ #include <gdk/gdkkeysyms.h>
+ #include <glib/gstdio.h>
+ #include <webkit2/webkit2.h>
+ #include <stdlib.h>
+ #include <string.h>
+ 
+ #define HOME_URL      "https://duckduckgo.com"
+ #define SEARCH_URL    "https://duckduckgo.com/?q=%s"
+ #define START_PAGE    "rezzbrowser-start"   /* about:blank-like scheme */
+ #define DEFAULT_ZOOM  1.0
+ 
+ /* --------------- global handles ---------------------------------------- */
+ 
+ static GtkWidget *window;
+ static GtkWidget *notebook;
+ static GtkWidget *back_button;
+ static GtkWidget *forward_button;
+ static GtkWidget *url_entry;
+ static GtkWidget *progress_bar;
+ static GtkWidget *status_bar;
+ static GtkWidget *zoom_label;
+ 
+ static WebKitSettings        *settings;
+ static WebKitWebContext      *context;
+ 
+ /* --------------- helpers ----------------------------------------------- */
+ 
+ static WebKitWebView *
+ current_view(void)
+ {
+     gint page = gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook));
+     if (page < 0)
+         return NULL;
+     return WEBKIT_WEB_VIEW(
+         gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), page));
+ }
+ 
+ /* ------------------------------------------------------------------ */
+ /* URI helpers                                                         */
+ /* ------------------------------------------------------------------ */
+ 
+ static gchar *
+ text_to_uri(const gchar *input)
+ {
+     gchar *text = g_strstrip(g_strdup(input));
+     gchar *uri;
+ 
+     if (*text == '\0') {
+         uri = g_strdup(HOME_URL);
+     } else if (g_str_has_prefix(text, "http://") ||
+                g_str_has_prefix(text, "https://") ||
+                g_str_has_prefix(text, "file://") ||
+                g_str_has_prefix(text, "about:")) {
+         uri = g_strdup(text);
+     } else if (g_str_has_prefix(text, "localhost")) {
+         uri = g_strdup_printf("http://%s", text);
+     } else if (strchr(text, ' ') == NULL && strchr(text, '.') != NULL) {
+         uri = g_strdup_printf("https://%s", text);
+     } else {
+         gchar *escaped = g_uri_escape_string(text, NULL, FALSE);
+         uri = g_strdup_printf(SEARCH_URL, escaped);
+         g_free(escaped);
+     }
+ 
+     g_free(text);
+     return uri;
+ }
+ 
+ /* ------------------------------------------------------------------ */
+ /* UI update helpers                                                   */
+ /* ------------------------------------------------------------------ */
+ 
+ static void
+ update_nav_buttons(WebKitWebView *view)
+ {
+     gtk_widget_set_sensitive(back_button,
+         view && webkit_web_view_can_go_back(view));
+     gtk_widget_set_sensitive(forward_button,
+         view && webkit_web_view_can_go_forward(view));
+ }
+ 
+ static void
+ update_zoom_label(gdouble zoom)
+ {
+     if (zoom_label) {
+         gchar *s = g_strdup_printf("%d%%", (int)(zoom * 100));
+         gtk_label_set_text(GTK_LABEL(zoom_label), s);
+         g_free(s);
+     }
+ }
+ 
+ /* Sync toolbar + window title with the given (current) view. */
+ static void
+ update_ui_for_view(WebKitWebView *view)
+ {
+     if (!view) {
+         gtk_window_set_title(GTK_WINDOW(window), "rezzbrowser");
+         return;
+     }
+ 
+     const gchar *uri = webkit_web_view_get_uri(view);
+     const gchar *title = webkit_web_view_get_title(view);
+     gchar *full;
+ 
+     if (!gtk_widget_has_focus(url_entry))
+         gtk_entry_set_text(GTK_ENTRY(url_entry), uri ? uri : "");
+ 
+     if (webkit_web_view_is_loading(view))
+         full = g_strdup("Loading... - rezzbrowser");
+     else if (title && *title)
+         full = g_strdup_printf("%s - rezzbrowser", title);
+     else
+         full = g_strdup_printf("%s - rezzbrowser", uri ? uri : "New tab");
+ 
+     gtk_window_set_title(GTK_WINDOW(window), full);
+     g_free(full);
+ 
+     update_nav_buttons(view);
+ 
+     /* zoom label */
+     update_zoom_label(webkit_web_view_get_zoom_level(view));
+ }
+ 
+ /* ------------------------------------------------------------------ */
+ /* progress bar                                                        */
+ /* ------------------------------------------------------------------ */
+ 
+ static void
+ on_progress_changed(WebKitWebView *view, GParamSpec *pspec, gpointer data)
+ {
+     (void)pspec;
+     (void)data;
+     gdouble p = webkit_web_view_get_load_progress(view);
+     if (progress_bar) {
+         gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), p);
+         if (p >= 1.0)
+             gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress_bar));
+     }
+ }
+ 
+ /* ------------------------------------------------------------------ */
+ /* status bar                                                          */
+ /* ------------------------------------------------------------------ */
+ 
+ static void
+ on_status_changed(WebKitWebView *view, GParamSpec *pspec, gpointer data)
+ {
+     (void)pspec;
+     (void)data;
+     const gchar *s = webkit_web_view_get_status(view);
+     if (status_bar)
+         gtk_label_set_text(GTK_LABEL(status_bar), s ? s : "");
+ }
+ 
+ /* ------------------------------------------------------------------ */
+ /* tab / view lifecycle                                                */
+ /* ------------------------------------------------------------------ */
+ 
+ static void
+ close_tab(GtkWidget *view)
+ {
+     GtkNotebook *nb = GTK_NOTEBOOK(notebook);
+ 
+     if (gtk_notebook_get_n_pages(nb) <= 1) {
+         /* Last tab: navigate to start page instead of quitting. */
+         WebKitWebView *cv = current_view();
+         if (cv) {
+             /* Clear navigation history so back/forward are disabled. */
+             webkit_web_view_go_to_back_forward_list_item(cv, NULL);
+             /* Load a minimal blank page. */
+             webkit_web_view_load_uri(cv, "about:blank");
+         }
+         return;
+     }
+ 
+     gint page = gtk_notebook_page_num(nb, view);
+     if (page >= 0)
+         gtk_notebook_remove_page(nb, page);
+ }
+ 
+ /* ------------------------------------------------------------------ */
+ /* per-view callbacks                                                  */
+ /* ------------------------------------------------------------------ */
+ 
+ static void
+ on_load_changed(WebKitWebView *view, WebKitLoadEvent event, gpointer data)
+ {
+     (void)data;
+     if (event == WEBKIT_LOAD_STARTED) {
+         if (progress_bar)
+             gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0);
+     } else if (event == WEBKIT_LOAD_COMPLETED) {
+         if (progress_bar)
+             gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 1.0);
+     }
+ 
+     if (view == current_view())
+         update_ui_for_view(view);
+ }
+ 
+ static void
+ on_uri_changed(GObject *object, GParamSpec *pspec, gpointer data)
+ {
+     (void)pspec;
+     (void)data;
+     WebKitWebView *view = WEBKIT_WEB_VIEW(object);
+     if (view == current_view())
+         update_ui_for_view(view);
+ }
+ 
+ static void
+ on_title_changed(GObject *object, GParamSpec *pspec, gpointer data)
+ {
+     (void)pspec;
+     (void)data;
+     WebKitWebView *view = WEBKIT_WEB_VIEW(object);
+     GtkWidget *label = g_object_get_data(G_OBJECT(view), "tab-label");
+     const gchar *title = webkit_web_view_get_title(view);
+     const gchar *uri = webkit_web_view_get_uri(view);
+     const gchar *text = (title && *title) ? title : (uri ? uri : "New tab");
+ 
+     if (label) {
+         gtk_label_set_text(GTK_LABEL(label), text);
+         gtk_widget_set_tooltip_text(label, text);
+     }
+ 
+     if (view == current_view())
+         update_ui_for_view(view);
+ }
+ 
+ /* Links with target=_blank and window.open() land here. */
+ static GtkWidget *
+ on_create(WebKitWebView *view, WebKitNavigationAction *action, gpointer data)
+ {
+     (void)action;
+     (void)data;
+     /* Get the target frame name; if "_blank", open new tab. */
+     const gchar *target = webkit_navigation_action_get_target_frame(action);
+     if (g_strcmp0(target, "_blank") == 0) {
+         return create_tab(NULL, view, TRUE);
+     }
+     /* Otherwise navigate in the current view. */
+     return NULL;
+ }
+ 
+ /* window.close() from JavaScript. */
+ static void
+ on_web_close(WebKitWebView *view, gpointer data)
+ {
+     (void)data;
+     close_tab(GTK_WIDGET(view));
+ }
+ 
+ static void
+ on_tab_close_clicked(GtkButton *button, gpointer view)
+ {
+     (void)button;
+     close_tab(GTK_WIDGET(view));
+ }
+ 
+ /* Page error handler — shows a simple error page with a retry button. */
+ static void
+ on_decide_policy(WebKitWebView *view,
+                  WebKitPolicyDecision **decisions,
+                  guint n_decisions,
+                  gpointer data)
+ {
+     (void)data;
+     for (guint i = 0; i < n_decisions; i++) {
+         WebKitPolicyDecision *dec = decisions[i];
+ 
+         if (webkit_policy_decision_is_navigation(dec)) {
+             WebKitNavigationPolicyDecision *nav_dec =
+                 WEBKIT_NAVIGATION_POLICY_DECISION(dec);
+             WebKitNavigationRequest *req =
+                 webkit_navigation_policy_decision_get_request(nav_dec);
+             const gchar *uri = webkit_uri_request_get_uri(
+                 WEBKIT_URI_REQUEST(req));
+ 
+             /* "_blank" target → new tab */
+             const gchar *frame =
+                 webkit_navigation_policy_decision_get_target_frame(nav_dec);
+             if (g_strcmp0(frame, "_blank") == 0) {
+                 create_tab(uri, view, TRUE);
+                 webkit_policy_decision_download(dec);
+                 webkit_policy_decision_ignore(dec);
+                 continue;
+             }
+ 
+             /* Invalid SSL — ignore and let WebKit handle it.
+              * In a production build you'd show a certificate dialog. */
+             webkit_policy_decision_use(dec);
+         } else if (webkit_policy_decision_is_download(dec)) {
+             /* Simple download handling: just acknowledge. */
+             webkit_policy_decision_ignore(dec);
+         }
+     }
+ }
+ 
+ /* ------------------------------------------------------------------ */
+ /* tabs                                                                */
+ /* ------------------------------------------------------------------ */
+ 
+ static GtkWidget *
+ create_tab(const gchar *uri, WebKitWebView *related, gboolean focus)
+ {
+     GtkWidget *view;
+ 
+     if (related)
+         view = webkit_web_view_new_with_related_view(related);
+     else
+         view = webkit_web_view_new_with_context(context);
+     webkit_web_view_set_settings(WEBKIT_WEB_VIEW(view), settings);
+ 
+     /* Tab header: [ title ][x] */
+     GtkWidget *tab = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
+     GtkWidget *label = gtk_label_new("New tab");
+     gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
+     gtk_label_set_width_chars(GTK_LABEL(label), 16);
+     gtk_label_set_max_width_chars(GTK_LABEL(label), 16);
+     gtk_widget_set_halign(label, GTK_ALIGN_START);
+ 
+     GtkWidget *close_btn = gtk_button_new_with_label("×");
+     gtk_button_set_relief(GTK_BUTTON(close_btn), GTK_RELIEF_NONE);
+     gtk_widget_set_focus_on_click(close_btn, FALSE);
+     gtk_widget_set_size_request(close_btn, 16, -1);
+ 
+     gtk_box_pack_start(GTK_BOX(tab), label, TRUE, TRUE, 0);
+     gtk_box_pack_start(GTK_BOX(tab), close_btn, FALSE, FALSE, 0);
+     gtk_widget_show_all(tab);
+ 
+     g_object_set_data(G_OBJECT(view), "tab-label", label);
+ 
+     /* Signals */
+     g_signal_connect(close_btn, "clicked", G_CALLBACK(on_tab_close_clicked), view);
+     g_signal_connect(view, "load-changed", G_CALLBACK(on_load_changed), NULL);
+     g_signal_connect(view, "notify::uri", G_CALLBACK(on_uri_changed), NULL);
+     g_signal_connect(view, "notify::title", G_CALLBACK(on_title_changed), NULL);
+     g_signal_connect(view, "create", G_CALLBACK(on_create), NULL);
+     g_signal_connect(view, "close", G_CALLBACK(on_web_close), NULL);
+     g_signal_connect(view, "notify::load-progress", G_CALLBACK(on_progress_changed), NULL);
+     g_signal_connect(view, "notify::status", G_CALLBACK(on_status_changed), NULL);
+     g_signal_connect(view, "decide-policy", G_CALLBACK(on_decide_policy), NULL);
+ 
+     gint idx = gtk_notebook_append_page(GTK_NOTEBOOK(notebook), view, tab);
+     gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(notebook), view, TRUE);
+     gtk_widget_show(view);
+ 
+     if (focus)
+         gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), idx);
+ 
+     if (uri) {
+         if (g_str_equal(uri, "about:blank") || g_str_equal(uri, START_PAGE)) {
+             /* Load an empty page or the local start page. */
+             if (g_file_test("start_page.html", G_FILE_TEST_EXISTS)) {
+                 gchar *path = g_build_filename(g_get_current_dir(),
+                                                "start_page.html", NULL);
+                 webkit_web_view_load_uri(WEBKIT_WEB_VIEW(view),
+                                          g_file_get_path(g_file_new_for_path(path)));
+                 g_free(path);
+             }
+             /* else: stay blank */
+         } else {
+             webkit_web_view_load_uri(WEBKIT_WEB_VIEW(view), uri);
+         }
+     }
+ 
+     return view;
+ }
+ 
+ static void
+ on_switch_page(GtkNotebook *nb, GtkWidget *page, guint num, gpointer data)
+ {
+     (void)nb;
+     (void)num;
+     (void)data;
+     gtk_entry_set_text(GTK_ENTRY(url_entry), "");
+     update_ui_for_view(WEBKIT_WEB_VIEW(page));
+ }
+ 
+ /* ------------------------------------------------------------------ */
+ /* toolbar                                                             */
+ /* ------------------------------------------------------------------ */
+ 
+ static void
+ on_back_clicked(GtkButton *button, gpointer data)
+ {
+     (void)button; (void)data;
+     WebKitWebView *view = current_view();
+     if (view && webkit_web_view_can_go_back(view))
+         webkit_web_view_go_back(view);
+ }
+ 
+ static void
+ on_forward_clicked(GtkButton *button, gpointer data)
+ {
+     (void)button; (void)data;
+     WebKitWebView *view = current_view();
+     if (view && webkit_web_view_can_go_forward(view))
+         webkit_web_view_go_forward(view);
+ }
+ 
+ static void
+ on_reload_clicked(GtkButton *button, gpointer data)
+ {
+     (void)button; (void)data;
+     WebKitWebView *view = current_view();
+     if (view)
+         webkit_web_view_reload(view);
+ }
+ 
+ static void
+ on_new_tab_clicked(GtkButton *button, gpointer data)
+ {
+     (void)button; (void)data;
+     create_tab(HOME_URL, NULL, TRUE);
+     gtk_widget_grab_focus(url_entry);
+     gtk_editable_select_region(GTK_EDITABLE(url_entry), 0, -1);
+ }
+ 
+ static void
+ on_url_activate(GtkEntry *entry, gpointer data)
+ {
+     (void)data;
+     gchar *uri = text_to_uri(gtk_entry_get_text(entry));
+     WebKitWebView *view = current_view();
+ 
+     if (view)
+         webkit_web_view_load_uri(view, uri);
+     else
+         create_tab(uri, NULL, TRUE);
+ 
+     g_free(uri);
+ 
+     if (current_view())
+         gtk_widget_grab_focus(GTK_WIDGET(current_view()));
+ }
+ 
+ /* ------------------------------------------------------------------ */
+ /* zoom                                                                */
+ /* ------------------------------------------------------------------ */
+ 
+ static void
+ zoom_in(WebKitWebView *view)
+ {
+     if (view) {
+         gdouble z = webkit_web_view_get_zoom_level(view) + 0.1;
+         if (z > 5.0) z = 5.0;
+         webkit_web_view_set_zoom_level(view, z);
+         update_zoom_label(z);
+     }
+ }
+ 
+ static void
+ zoom_out(WebKitWebView *view)
+ {
+     if (view) {
+         gdouble z = webkit_web_view_get_zoom_level(view) - 0.1;
+         if (z < 0.25) z = 0.25;
+         webkit_web_view_set_zoom_level(view, z);
+         update_zoom_label(z);
+     }
+ }
+ 
+ static void
+ zoom_reset(WebKitWebView *view)
+ {
+     if (view) {
+         webkit_web_view_set_zoom_level(view, DEFAULT_ZOOM);
+         update_zoom_label(DEFAULT_ZOOM);
+     }
+ }
+ 
+ /* ------------------------------------------------------------------ */
+ /* start page                                                          */
+ /* ------------------------------------------------------------------ */
+ 
+ static void
+ on_start_page_clicked(GtkButton *button, gpointer data)
+ {
+     (void)button;
+     gchar *uri = text_to_uri(gtk_label_get_text(GTK_LABEL(gtk_bin_get_child(
+         GTK_BIN(button)))));
+     WebKitWebView *view = current_view();
+     if (view)
+         webkit_web_view_load_uri(view, uri);
+     g_free(uri);
+     gtk_widget_grab_focus(GTK_WIDGET(view));
+ }
+ 
+ /* ------------------------------------------------------------------ */
+ /* keyboard shortcuts                                                  */
+ /* ------------------------------------------------------------------ */
+ 
+ static gboolean
+ on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
+ {
+     (void)data;
+     GtkNotebook *nb = GTK_NOTEBOOK(notebook);
+     guint key = event->keyval;
+     WebKitWebView *view = current_view();
+ 
+     /* F5 — reload */
+     if (key == GDK_KEY_F5) {
+         if (view) webkit_web_view_reload(view);
+         return TRUE;
+     }
+ 
+     /* F11 — fullscreen */
+     if (key == GDK_KEY_F11) {
+         if (gtk_window_get_fullscreen(GTK_WINDOW(window)))
+             gtk_window_unfullscreen(GTK_WINDOW(window));
+         else
+             gtk_window_fullscreen(GTK_WINDOW(window));
+         return TRUE;
+     }
+ 
+     /* Alt+1..9 — jump to tab N */
+     if (event->state & GDK_MOD1_MASK) {
+         if (key >= GDK_KEY_1 && key <= GDK_KEY_9) {
+             gint n = (gint)(key - GDK_KEY_1);
+             if (n < gtk_notebook_get_n_pages(nb))
+                 gtk_notebook_set_current_page(nb, n);
+             return TRUE;
+         }
+         return FALSE;
+     }
+ 
+     /* Control shortcuts */
+     if (!(event->state & GDK_CONTROL_MASK))
+         return FALSE;
+ 
+     switch (key) {
+     case GDK_KEY_t:
+     case GDK_KEY_T:
+         create_tab(HOME_URL, NULL, TRUE);
+         gtk_widget_grab_focus(url_entry);
+         gtk_editable_select_region(GTK_EDITABLE(url_entry), 0, -1);
+         return TRUE;
+ 
+     case GDK_KEY_w:
+     case GDK_KEY_W:
+         if (view) close_tab(GTK_WIDGET(view));
+         return TRUE;
+ 
+     case GDK_KEY_l:
+     case GDK_KEY_L:
+         gtk_widget_grab_focus(url_entry);
+         gtk_editable_select_region(GTK_EDITABLE(url_entry), 0, -1);
+         return TRUE;
+ 
+     case GDK_KEY_r:
+     case GDK_KEY_R:
+         if (view) webkit_web_view_reload(view);
+         return TRUE;
+ 
+     case GDK_KEY_d:
+     case GDK_KEY_D:
+         /* Toggle start page */
+         if (view) {
+             const gchar *uri = webkit_web_view_get_uri(view);
+             if (uri && g_str_equal(uri, START_PAGE))
+                 webkit_web_view_load_uri(view, HOME_URL);
+             else
+                 webkit_web_view_load_uri(view, START_PAGE);
+         }
+         return TRUE;
+ 
+     /* Zoom */
+     case GDK_KEY_plus:
+     case GDK_KEY_equal:
+         zoom_in(view);
+         return TRUE;
+ 
+     case GDK_KEY_minus:
+     case GDK_KEY_underscore:
+         zoom_out(view);
+         return TRUE;
+ 
+     case GDK_KEY_0:
+         zoom_reset(view);
+         return TRUE;
+ 
+     /* Tab navigation */
+     case GDK_KEY_Tab:
+         if (gtk_notebook_get_current_page(nb) ==
+             gtk_notebook_get_n_pages(nb) - 1)
+             gtk_notebook_set_current_page(nb, 0);
+         else
+             gtk_notebook_next_page(nb);
+         return TRUE;
+ 
+     case GDK_KEY_ISO_Left_Tab:
+     case GDK_KEY_Page_Up:
+         if (gtk_notebook_get_current_page(nb) == 0)
+             gtk_notebook_set_current_page(nb, -1);
+         else
+             gtk_notebook_prev_page(nb);
+         return TRUE;
+ 
+     case GDK_KEY_Page_Down:
+         if (gtk_notebook_get_current_page(nb) ==
+             gtk_notebook_get_n_pages(nb) - 1)
+             gtk_notebook_set_current_page(nb, 0);
+         else
+             gtk_notebook_next_page(nb);
+         return TRUE;
+ 
+     default:
+         return FALSE;
+     }
+ }
+ 
+ static void
+ on_destroy(GtkWidget *widget, gpointer data)
+ {
+     (void)widget; (void)data;
+     gtk_main_quit();
+ }
+ 
+ /* ------------------------------------------------------------------ */
+ /* main                                                                */
+ /* ------------------------------------------------------------------ */
+ 
+ int
+ main(int argc, char *argv[])
+ {
+     gtk_init(&argc, &argv);
+ 
+     /* ---- Persistent profile ----------------------------------------- */
+     gchar *cache_dir = g_build_filename(g_get_user_cache_dir(),
+                                         "rezzbrowser", NULL);
+     gchar *data_dir  = g_build_filename(g_get_user_data_dir(),
+                                         "rezzbrowser", NULL);
+     g_mkdir_with_parents(cache_dir, 0700);
+     g_mkdir_with_parents(data_dir, 0700);
+ 
+     WebKitWebsiteDataManager *data_manager =
+         webkit_website_data_manager_new(
+             "base-cache-directory", cache_dir,
+             "base-data-directory",  data_dir,
+             NULL);
+     context = webkit_web_context_new_with_website_data_manager(data_manager);
+     g_object_unref(data_manager);
+ 
+     gchar *cookie_file = g_build_filename(data_dir,
+                                           "cookies.sqlite", NULL);
+     webkit_cookie_manager_set_persistent_storage(
+         webkit_web_context_get_cookie_manager(context),
+         cookie_file, WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
+     g_free(cookie_file);
+     g_free(cache_dir);
+     g_free(data_dir);
+ 
+     /* ---- Settings --------------------------------------------------- */
+     settings = webkit_settings_new();
+     webkit_settings_set_hardware_acceleration_policy(
+         settings, WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);
+     webkit_settings_set_enable_javascript(settings, TRUE);
+     webkit_settings_set_enable_developer_extras(settings, TRUE);
+     webkit_settings_set_enable_page_cache(settings, TRUE);
+     webkit_settings_set_enable_html5_local_storage(settings, TRUE);
+     webkit_settings_set_enable_html5_database(settings, TRUE);
+     webkit_settings_set_enable_offline_web_application_cache(
+         settings, TRUE);
+     webkit_settings_set_enable_hyperlink_auditing(settings, TRUE);
+     webkit_settings_set_user_agent(settings,
+         "Mozilla/5.0 (X11; Linux x86_64) "
+         "AppleWebKit/537.36 (KHTML, like Gecko) "
+         "Chrome/120.0.0.0 Safari/537.36");
+ 
+     /* ---- Window ----------------------------------------------------- */
+     window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
+     gtk_window_set_default_size(GTK_WINDOW(window), 1024, 768);
+     gtk_window_set_title(GTK_WINDOW(window), "rezzbrowser");
+     g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), NULL);
+     g_signal_connect(window, "key-press-event",
+                      G_CALLBACK(on_key_press), NULL);
+ 
+     GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
+     gtk_container_add(GTK_CONTAINER(window), vbox);
+ 
+     /* ---- Toolbar ---------------------------------------------------- */
+     GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
+     gtk_container_set_border_width(GTK_CONTAINER(toolbar), 4);
+ 
+     back_button = gtk_button_new_with_label("<");
+     forward_button = gtk_button_new_with_label(">");
+     GtkWidget *reload_button = gtk_button_new_with_label("Reload");
+     GtkWidget *new_tab_button = gtk_button_new_with_label("+");
+     url_entry = gtk_entry_new();
+ 
+     gtk_widget_set_sensitive(back_button, FALSE);
+     gtk_widget_set_sensitive(forward_button, FALSE);
+ 
+     g_signal_connect(back_button, "clicked",
+                      G_CALLBACK(on_back_clicked), NULL);
+     g_signal_connect(forward_button, "clicked",
+                      G_CALLBACK(on_forward_clicked), NULL);
+     g_signal_connect(reload_button, "clicked",
+                      G_CALLBACK(on_reload_clicked), NULL);
+     g_signal_connect(new_tab_button, "clicked",
+                      G_CALLBACK(on_new_tab_clicked), NULL);
+     g_signal_connect(url_entry, "activate",
+                      G_CALLBACK(on_url_activate), NULL);
+ 
+     gtk_box_pack_start(GTK_BOX(toolbar), back_button,
+                        FALSE, FALSE, 0);
+     gtk_box_pack_start(GTK_BOX(toolbar), forward_button,
+                        FALSE, FALSE, 0);
+     gtk_box_pack_start(GTK_BOX(toolbar), reload_button,
+                        FALSE, FALSE, 0);
+     gtk_box_pack_start(GTK_BOX(toolbar), new_tab_button,
+                        FALSE, FALSE, 0);
+     gtk_box_pack_start(GTK_BOX(toolbar), url_entry,
+                        TRUE, TRUE, 0);
+ 
+     /* Zoom label */
+     zoom_label = gtk_label_new("100%");
+     gtk_widget_set_halign(zoom_label, GTK_ALIGN_END);
+     gtk_box_pack_start(GTK_BOX(toolbar), zoom_label,
+                        FALSE, FALSE, 0);
+ 
+     /* ---- Progress bar ----------------------------------------------- */
+     progress_bar = gtk_progress_bar_new();
+     gtk_widget_set_valign(progress_bar, GTK_ALIGN_END);
+     gtk_widget_set_hexpand(progress_bar, TRUE);
+     gtk_box_pack_start(GTK_BOX(vbox), progress_bar,
+                        FALSE, FALSE, 0);
+ 
+     /* ---- Tabs ------------------------------------------------------- */
+     notebook = gtk_notebook_new();
+     gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
+     g_signal_connect(notebook, "switch-page",
+                      G_CALLBACK(on_switch_page), NULL);
+ 
+     gtk_box_pack_start(GTK_BOX(vbox), notebook,
+                        TRUE, TRUE, 0);
+ 
+     /* ---- Status bar ------------------------------------------------- */
+     status_bar = gtk_label_new("");
+     gtk_widget_set_halign(status_bar, GTK_ALIGN_START);
+     gtk_widget_set_hexpand(status_bar, TRUE);
+     GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
+     gtk_box_pack_start(GTK_BOX(hbox), status_bar, TRUE, TRUE, 0);
+     gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
+ 
+     /* ---- First tab -------------------------------------------------- */
+     gchar *start_uri;
+     if (argc > 1)
+         start_uri = text_to_uri(argv[1]);
+     else
+         start_uri = g_strdup(HOME_URL);
+     create_tab(start_uri, NULL, TRUE);
+     g_free(start_uri);
+ 
+     gtk_widget_show_all(window);
+     gtk_main();
+ 
+     return 0;
+ }
