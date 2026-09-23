/*
 * rezzedit — minimal GTK3 code editor for RezzOS.
 *
 * Sidebar file tree (lazy-loaded directories) + tabbed GtkSourceView
 * editors with automatic syntax highlighting by file extension.
 *
 * Hotkeys: Ctrl+N new, Ctrl+O open, Ctrl+Shift+O open folder,
 *          Ctrl+S save, Ctrl+Shift+S save as, Ctrl+W close tab,
 *          Ctrl+Q quit, Ctrl+Z undo, Ctrl+Y redo.
 * (Ctrl+C/V/X/A work out of the box — GtkTextView's own default bindings.)
 *
 * Build:
 *   gcc -O2 rezzedit.c $(pkg-config --cflags --libs gtk+-3.0 gtksourceview-3.0) \
 *       -o rezzedit
 *
 * Needs at runtime: gtksourceview3 (and its -dev package to build).
 */

#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>
#include <string.h>

enum { COL_NAME = 0, COL_PATH, COL_IS_DIR, N_TREE_COLS };

static GtkWidget *window;
static GtkWidget *notebook;
static GtkWidget *file_tree;
static GtkTreeStore *tree_store;
static GtkAccelGroup *accel_group;
static gchar *current_root = NULL;

/* ---------- per-tab state ---------- */

typedef struct {
    GtkWidget *source_view;
    GtkSourceBuffer *buffer;
    gchar *filepath;   /* NULL until saved once */
    GtkWidget *tab_label;
} EditorTab;

static EditorTab *
current_tab(void)
{
    gint page = gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook));
    if (page < 0) return NULL;
    GtkWidget *scroll = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), page);
    return g_object_get_data(G_OBJECT(scroll), "tab");
}

/* ---------- language / highlighting ---------- */

static void
apply_language_for_path(GtkSourceBuffer *buffer, const gchar *path)
{
    GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default();
    GtkSourceLanguage *lang = gtk_source_language_manager_guess_language(lm, path, NULL);
    gtk_source_buffer_set_language(buffer, lang);

    GtkSourceStyleSchemeManager *sm = gtk_source_style_scheme_manager_get_default();
    GtkSourceStyleScheme *scheme = gtk_source_style_scheme_manager_get_scheme(sm, "classic");
    if (scheme) gtk_source_buffer_set_style_scheme(buffer, scheme);
}

/* ---------- tab title helpers ---------- */

static void
update_tab_label(EditorTab *tab)
{
    const gchar *name = tab->filepath ? strrchr(tab->filepath, '/') : NULL;
    name = name ? name + 1 : (tab->filepath ? tab->filepath : "untitled");

    gboolean modified = gtk_text_buffer_get_modified(GTK_TEXT_BUFFER(tab->buffer));
    gchar *text = g_strdup_printf("%s%s", modified ? "*" : "", name);
    gtk_label_set_text(GTK_LABEL(tab->tab_label), text);
    g_free(text);
}

static void
on_buffer_modified_changed(GtkTextBuffer *buf, gpointer data)
{
    (void)buf;
    update_tab_label((EditorTab *)data);
}

/* ---------- opening / creating tabs ---------- */

static EditorTab *
find_open_tab(const gchar *path)
{
    gint n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook));
    for (gint i = 0; i < n; i++) {
        GtkWidget *scroll = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), i);
        EditorTab *tab = g_object_get_data(G_OBJECT(scroll), "tab");
        if (tab->filepath && g_strcmp0(tab->filepath, path) == 0)
            return tab;
    }
    return NULL;
}

static void on_close_tab_clicked(GtkButton *b, gpointer data);

static EditorTab *
new_tab(const gchar *path)
{
    if (path) {
        EditorTab *existing = find_open_tab(path);
        if (existing) {
            gint n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook));
            for (gint i = 0; i < n; i++) {
                GtkWidget *scroll = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), i);
                if (g_object_get_data(G_OBJECT(scroll), "tab") == existing) {
                    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), i);
                    return existing;
                }
            }
        }
    }

    EditorTab *tab = g_new0(EditorTab, 1);
    tab->filepath = path ? g_strdup(path) : NULL;

    tab->buffer = gtk_source_buffer_new(NULL);
    tab->source_view = gtk_source_view_new_with_buffer(tab->buffer);
    gtk_source_view_set_show_line_numbers(GTK_SOURCE_VIEW(tab->source_view), TRUE);
    gtk_source_view_set_highlight_current_line(GTK_SOURCE_VIEW(tab->source_view), TRUE);
    gtk_source_view_set_auto_indent(GTK_SOURCE_VIEW(tab->source_view), TRUE);
    gtk_source_view_set_tab_width(GTK_SOURCE_VIEW(tab->source_view), 4);
    gtk_source_buffer_set_highlight_syntax(tab->buffer, TRUE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(tab->source_view), TRUE);

    if (path) {
        gchar *contents = NULL;
        gsize len = 0;
        if (g_file_get_contents(path, &contents, &len, NULL)) {
            gtk_text_buffer_set_text(GTK_TEXT_BUFFER(tab->buffer), contents, len);
            g_free(contents);
        }
        apply_language_for_path(tab->buffer, path);
    }
    gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(tab->buffer), FALSE);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), tab->source_view);
    g_object_set_data(G_OBJECT(scroll), "tab", tab);

    GtkWidget *tab_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    tab->tab_label = gtk_label_new(path ? strrchr(path, '/') + 1 : "untitled");
    GtkWidget *close_btn = gtk_button_new_from_icon_name("window-close", GTK_ICON_SIZE_MENU);
    gtk_button_set_relief(GTK_BUTTON(close_btn), GTK_RELIEF_NONE);
    gtk_box_pack_start(GTK_BOX(tab_box), tab->tab_label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(tab_box), close_btn, FALSE, FALSE, 0);
    gtk_widget_show_all(tab_box);
    g_object_set_data(G_OBJECT(close_btn), "scroll", scroll);
    g_signal_connect(close_btn, "clicked", G_CALLBACK(on_close_tab_clicked), NULL);

    g_signal_connect(tab->buffer, "modified-changed",
        G_CALLBACK(on_buffer_modified_changed), tab);

    gint idx = gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scroll, tab_box);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), idx);
    gtk_widget_show_all(scroll);
    gtk_widget_grab_focus(tab->source_view);

    return tab;
}

/* ---------- save / close ---------- */

static gboolean
save_tab(EditorTab *tab, gboolean force_dialog)
{
    if (!tab) return FALSE;

    gchar *target = tab->filepath;
    if (!target || force_dialog) {
        GtkWidget *dlg = gtk_file_chooser_dialog_new("Save As", GTK_WINDOW(window),
            GTK_FILE_CHOOSER_ACTION_SAVE,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
        if (current_root)
            gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), current_root);

        gboolean picked = FALSE;
        gchar *picked_path = NULL;
        if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
            picked_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
            picked = TRUE;
        }
        gtk_widget_destroy(dlg);
        if (!picked) return FALSE;

        g_free(tab->filepath);
        tab->filepath = picked_path;
        target = tab->filepath;
        apply_language_for_path(tab->buffer, target);
    }

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(tab->buffer), &start, &end);
    gchar *text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(tab->buffer), &start, &end, FALSE);

    GError *err = NULL;
    gboolean ok = g_file_set_contents(target, text, -1, &err);
    g_free(text);

    if (!ok) {
        GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Could not save file: %s", err->message);
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
        g_error_free(err);
        return FALSE;
    }

    gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(tab->buffer), FALSE);
    update_tab_label(tab);
    return TRUE;
}

static gboolean
confirm_discard(EditorTab *tab)
{
    if (!gtk_text_buffer_get_modified(GTK_TEXT_BUFFER(tab->buffer)))
        return TRUE;

    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(window),
        GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
        "This file has unsaved changes.");
    gtk_dialog_add_buttons(GTK_DIALOG(d),
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Discard", GTK_RESPONSE_REJECT,
        "_Save", GTK_RESPONSE_ACCEPT, NULL);
    gint r = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);

    if (r == GTK_RESPONSE_CANCEL) return FALSE;
    if (r == GTK_RESPONSE_ACCEPT) return save_tab(tab, FALSE);
    return TRUE; /* discard */
}

static void
close_tab_widget(GtkWidget *scroll)
{
    EditorTab *tab = g_object_get_data(G_OBJECT(scroll), "tab");
    if (!confirm_discard(tab)) return;

    gint n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook));
    for (gint i = 0; i < n; i++) {
        if (gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), i) == scroll) {
            gtk_notebook_remove_page(GTK_NOTEBOOK(notebook), i);
            break;
        }
    }
    g_free(tab->filepath);
    g_free(tab);

    if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook)) == 0)
        new_tab(NULL);
}

static void
on_close_tab_clicked(GtkButton *b, gpointer d)
{
    (void)d;
    GtkWidget *scroll = g_object_get_data(G_OBJECT(b), "scroll");
    close_tab_widget(scroll);
}

/* ---------- file tree sidebar ---------- */

static void
populate_dir(GtkTreeStore *store, GtkTreeIter *parent, const gchar *dirpath)
{
    GDir *dir = g_dir_open(dirpath, 0, NULL);
    if (!dir) return;

    GList *names = NULL;
    const gchar *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (name[0] == '.') continue; /* skip dotfiles/hidden dirs */
        names = g_list_insert_sorted(names, g_strdup(name), (GCompareFunc)g_strcmp0);
    }
    g_dir_close(dir);

    for (GList *l = names; l; l = l->next) {
        gchar *full = g_build_filename(dirpath, (const gchar *)l->data, NULL);
        gboolean is_dir = g_file_test(full, G_FILE_TEST_IS_DIR);

        GtkTreeIter iter;
        gtk_tree_store_append(store, &iter, parent);
        gtk_tree_store_set(store, &iter,
            COL_NAME, (const gchar *)l->data, COL_PATH, full, COL_IS_DIR, is_dir, -1);

        if (is_dir) {
            /* Dummy child so the expander arrow shows; replaced with real
             * children on first expand (lazy loading). */
            GtkTreeIter dummy;
            gtk_tree_store_append(store, &dummy, &iter);
            gtk_tree_store_set(store, &dummy, COL_NAME, "", COL_PATH, "", COL_IS_DIR, FALSE, -1);
        }
        g_free(full);
    }
    g_list_free_full(names, g_free);
}

static void
load_root(const gchar *path)
{
    g_free(current_root);
    current_root = g_strdup(path);
    gtk_tree_store_clear(tree_store);
    populate_dir(tree_store, NULL, path);
}

static void
on_row_expanded(GtkTreeView *tv, GtkTreeIter *iter, GtkTreePath *path, gpointer d)
{
    (void)tv; (void)path; (void)d;
    GtkTreeIter child;
    if (!gtk_tree_model_iter_children(GTK_TREE_MODEL(tree_store), &child, iter))
        return;

    gchar *child_path = NULL;
    gtk_tree_model_get(GTK_TREE_MODEL(tree_store), &child, COL_PATH, &child_path, -1);
    gboolean is_dummy = (child_path == NULL || *child_path == '\0');
    g_free(child_path);
    if (!is_dummy) return;

    gtk_tree_store_remove(tree_store, &child);

    gchar *dirpath = NULL;
    gtk_tree_model_get(GTK_TREE_MODEL(tree_store), iter, COL_PATH, &dirpath, -1);
    populate_dir(tree_store, iter, dirpath);
    g_free(dirpath);
}

static void
on_tree_row_activated(GtkTreeView *tv, GtkTreePath *path, GtkTreeViewColumn *col, gpointer d)
{
    (void)col; (void)d;
    GtkTreeIter iter;
    gtk_tree_model_get_iter(GTK_TREE_MODEL(tree_store), &iter, path);

    gchar *filepath = NULL;
    gboolean is_dir = FALSE;
    gtk_tree_model_get(GTK_TREE_MODEL(tree_store), &iter,
        COL_PATH, &filepath, COL_IS_DIR, &is_dir, -1);

    if (!is_dir && filepath && *filepath)
        new_tab(filepath);
    else if (is_dir) {
        if (gtk_tree_view_row_expanded(tv, path))
            gtk_tree_view_collapse_row(tv, path);
        else
            gtk_tree_view_expand_row(tv, path, FALSE);
    }
    g_free(filepath);
}

/* ---------- menu / hotkey actions ---------- */

static void action_new(GtkMenuItem *m, gpointer d)   { (void)m; (void)d; new_tab(NULL); }

static void
action_open(GtkMenuItem *m, gpointer d)
{
    (void)m; (void)d;
    GtkWidget *dlg = gtk_file_chooser_dialog_new("Open File", GTK_WINDOW(window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
    if (current_root)
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), current_root);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        gchar *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        new_tab(path);
        g_free(path);
    }
    gtk_widget_destroy(dlg);
}

static void
action_open_folder(GtkMenuItem *m, gpointer d)
{
    (void)m; (void)d;
    GtkWidget *dlg = gtk_file_chooser_dialog_new("Open Folder", GTK_WINDOW(window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        gchar *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        load_root(path);
        g_free(path);
    }
    gtk_widget_destroy(dlg);
}

static void action_save(GtkMenuItem *m, gpointer d)    { (void)m; (void)d; save_tab(current_tab(), FALSE); }
static void action_save_as(GtkMenuItem *m, gpointer d) { (void)m; (void)d; save_tab(current_tab(), TRUE); }

static void
action_close_tab(GtkMenuItem *m, gpointer d)
{
    (void)m; (void)d;
    gint page = gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook));
    if (page >= 0)
        close_tab_widget(gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), page));
}

static gboolean
on_delete_event(GtkWidget *w, GdkEvent *e, gpointer d)
{
    (void)w; (void)e; (void)d;
    gint n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook));
    for (gint i = 0; i < n; i++) {
        GtkWidget *scroll = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), i);
        EditorTab *tab = g_object_get_data(G_OBJECT(scroll), "tab");
        if (!confirm_discard(tab)) return TRUE; /* cancel closing the window */
    }
    return FALSE;
}

static void action_quit(GtkMenuItem *m, gpointer d) { (void)m; (void)d; gtk_widget_destroy(window); }

static void
action_undo(GtkMenuItem *m, gpointer d)
{
    (void)m; (void)d;
    EditorTab *tab = current_tab();
    if (tab && gtk_source_buffer_can_undo(tab->buffer))
        gtk_source_buffer_undo(tab->buffer);
}

static void
action_redo(GtkMenuItem *m, gpointer d)
{
    (void)m; (void)d;
    EditorTab *tab = current_tab();
    if (tab && gtk_source_buffer_can_redo(tab->buffer))
        gtk_source_buffer_redo(tab->buffer);
}

/* ---------- menu bar with accelerators ---------- */

static GtkWidget *
menu_item_with_accel(const char *label, guint key, GdkModifierType mods,
                      void (*cb)(GtkMenuItem *, gpointer))
{
    GtkWidget *item = gtk_menu_item_new_with_label(label);
    gtk_widget_add_accelerator(item, "activate", accel_group, key, mods, GTK_ACCEL_VISIBLE);
    g_signal_connect(item, "activate", G_CALLBACK(cb), NULL);
    return item;
}

static GtkWidget *
build_menu_bar(void)
{
    GtkWidget *menubar = gtk_menu_bar_new();

    GtkWidget *file_menu = gtk_menu_new();
    GtkWidget *file_item = gtk_menu_item_new_with_label("File");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_item), file_menu);

    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu),
        menu_item_with_accel("New", GDK_KEY_n, GDK_CONTROL_MASK, action_new));
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu),
        menu_item_with_accel("Open File...", GDK_KEY_o, GDK_CONTROL_MASK, action_open));
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu),
        menu_item_with_accel("Open Folder...", GDK_KEY_o, GDK_CONTROL_MASK | GDK_SHIFT_MASK, action_open_folder));
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu),
        menu_item_with_accel("Save", GDK_KEY_s, GDK_CONTROL_MASK, action_save));
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu),
        menu_item_with_accel("Save As...", GDK_KEY_s, GDK_CONTROL_MASK | GDK_SHIFT_MASK, action_save_as));
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu),
        menu_item_with_accel("Close Tab", GDK_KEY_w, GDK_CONTROL_MASK, action_close_tab));
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu),
        menu_item_with_accel("Quit", GDK_KEY_q, GDK_CONTROL_MASK, action_quit));

    GtkWidget *edit_menu = gtk_menu_new();
    GtkWidget *edit_item = gtk_menu_item_new_with_label("Edit");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(edit_item), edit_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu),
        menu_item_with_accel("Undo", GDK_KEY_z, GDK_CONTROL_MASK, action_undo));
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu),
        menu_item_with_accel("Redo", GDK_KEY_y, GDK_CONTROL_MASK, action_redo));

    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), file_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), edit_item);
    return menubar;
}

int
main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "rezzedit");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 680);
    g_signal_connect(window, "delete-event", G_CALLBACK(on_delete_event), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    gtk_box_pack_start(GTK_BOX(vbox), build_menu_bar(), FALSE, FALSE, 0);

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), paned, TRUE, TRUE, 0);

    /* Sidebar */
    tree_store = gtk_tree_store_new(N_TREE_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
    file_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(tree_store));
    g_object_unref(tree_store);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(file_tree), FALSE);
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Name", renderer, "text", COL_NAME, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(file_tree), col);
    g_signal_connect(file_tree, "row-expanded", G_CALLBACK(on_row_expanded), NULL);
    g_signal_connect(file_tree, "row-activated", G_CALLBACK(on_tree_row_activated), NULL);

    GtkWidget *sidebar_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(sidebar_scroll, 220, -1);
    gtk_container_add(GTK_CONTAINER(sidebar_scroll), file_tree);
    gtk_paned_pack1(GTK_PANED(paned), sidebar_scroll, FALSE, TRUE);

    /* Editor area */
    notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
    gtk_paned_pack2(GTK_PANED(paned), notebook, TRUE, TRUE);

    const gchar *home = g_get_home_dir();
    load_root(home ? home : "/");
    new_tab(NULL);

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
