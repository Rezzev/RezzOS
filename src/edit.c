/*
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

/* Sidebar / paned state */
static GtkWidget *paned_widget = NULL;
static GtkWidget *sidebar_scroll = NULL;
static GtkWidget *sidebar_menu_item = NULL;
static gulong     sidebar_menu_handler_id = 0;

/* ---------- settings state ---------- */

typedef struct {
    gchar *style_scheme;
    gint font_size;
    gboolean dark_ui;
    gboolean search_wrap;
    gboolean search_case;
    gchar *font_family;
    gboolean sidebar_visible;
    gint sidebar_position;
} Settings;

static Settings settings = { NULL, 11, FALSE, TRUE, FALSE, NULL, TRUE, 220 };

/* ---------- configuration file ---------- */

#define CONFIG_FILE "settings.ini"
#define CFG_GROUP   "settings"

static gchar *config_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "rezzedit", CONFIG_FILE, NULL);
}

static void
load_config(void)
{
    gchar *path = config_path();
    GKeyFile *kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        GError *err = NULL;
        gchar *scheme = g_key_file_get_string(kf, CFG_GROUP, "style-scheme", &err);
        if (err) g_clear_error(&err);
        else { g_free(settings.style_scheme); settings.style_scheme = scheme; }

        gint size = g_key_file_get_integer(kf, CFG_GROUP, "font-size", &err);
        if (err) g_clear_error(&err);
        else if (size >= 6 && size <= 48) settings.font_size = size;

        gboolean b = g_key_file_get_boolean(kf, CFG_GROUP, "dark-ui", &err);
        if (err) g_clear_error(&err); else settings.dark_ui = b;

        b = g_key_file_get_boolean(kf, CFG_GROUP, "search-wrap", &err);
        if (err) g_clear_error(&err); else settings.search_wrap = b;

        b = g_key_file_get_boolean(kf, CFG_GROUP, "search-case", &err);
        if (err) g_clear_error(&err); else settings.search_case = b;

        gchar *family = g_key_file_get_string(kf, CFG_GROUP, "font-family", &err);
        if (err) g_clear_error(&err);
        else if (family && *family) { g_free(settings.font_family); settings.font_family = family; }
        else g_free(family);

        b = g_key_file_get_boolean(kf, CFG_GROUP, "sidebar-visible", &err);
        if (err) g_clear_error(&err); else settings.sidebar_visible = b;

        gint sp = g_key_file_get_integer(kf, CFG_GROUP, "sidebar-position", &err);
        if (err) g_clear_error(&err);
        else if (sp >= 50 && sp <= 3000) settings.sidebar_position = sp;

        if (settings.dark_ui) {
            GtkSettings *gtks = gtk_settings_get_default();
            if (gtks)
                g_object_set(gtks, "gtk-application-prefer-dark-theme", TRUE, NULL);
        }
    }
    g_key_file_free(kf);
    g_free(path);
}

static void
save_config(void)
{
    gchar *path = config_path();
    gchar *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);

    GKeyFile *kf = g_key_file_new();
    g_key_file_set_string(kf, CFG_GROUP, "style-scheme",
        settings.style_scheme ? settings.style_scheme : "classic");
    g_key_file_set_integer(kf, CFG_GROUP, "font-size", settings.font_size);
    g_key_file_set_boolean(kf, CFG_GROUP, "dark-ui", settings.dark_ui);
    g_key_file_set_boolean(kf, CFG_GROUP, "search-wrap", settings.search_wrap);
    g_key_file_set_boolean(kf, CFG_GROUP, "search-case", settings.search_case);
    g_key_file_set_string(kf, CFG_GROUP, "font-family",
        settings.font_family ? settings.font_family : "Monospace");
    g_key_file_set_boolean(kf, CFG_GROUP, "sidebar-visible", settings.sidebar_visible);
    g_key_file_set_integer(kf, CFG_GROUP, "sidebar-position", settings.sidebar_position);

    gsize len = 0;
    gchar *data = g_key_file_to_data(kf, &len, NULL);
    g_file_set_contents(path, data, len, NULL);
    g_free(data);
    g_key_file_free(kf);
    g_free(path);
}

/* ---------- per-tab state ---------- */

typedef struct {
    GtkWidget *source_view;
    GtkSourceBuffer *buffer;
    gchar *filepath;
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

/* ---------- language / appearance ---------- */

typedef struct {
    const gchar *id;
    const gchar *file;
    const gchar *spec;
} ExtraLang;

static const ExtraLang extra_langs[] = {
    { "zig", "zig.lang",
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<language id=\"zig\" name=\"Zig\" version=\"2.0\" _section=\"Source\">\n"
      "  <metadata>\n"
      "    <property name=\"globs\">*.zig;*.zon</property>\n"
      "    <property name=\"mimetypes\">text/x-zig</property>\n"
      "    <property name=\"line-comment-start\">//</property>\n"
      "  </metadata>\n"
      "  <definitions>\n"
      "    <context id=\"comment\" style-ref=\"def:comment\" end-at-line-end=\"true\">\n"
      "      <start>//</start>\n"
      "      <end>\\n</end>\n"
      "    </context>\n"
      "    <context id=\"string\" style-ref=\"def:string\" end-at-line-end=\"true\">\n"
      "      <start>\"</start>\n"
      "      <end>\"</end>\n"
      "      <include>\n"
      "        <context id=\"escape\" style-ref=\"def:special-char\">\n"
      "          <match>\\\\[\\\\\"'nrt0x]</match>\n"
      "        </context>\n"
      "      </include>\n"
      "    </context>\n"
      "    <context id=\"keyword\" style-ref=\"def:keyword\">\n"
      "      <keyword>align</keyword>\n"
      "      <keyword>allowzero</keyword>\n"
      "      <keyword>and</keyword>\n"
      "      <keyword>anyframe</keyword>\n"
      "      <keyword>anytype</keyword>\n"
      "      <keyword>asm</keyword>\n"
      "      <keyword>async</keyword>\n"
      "      <keyword>await</keyword>\n"
      "      <keyword>break</keyword>\n"
      "      <keyword>catch</keyword>\n"
      "      <keyword>comptime</keyword>\n"
      "      <keyword>const</keyword>\n"
      "      <keyword>continue</keyword>\n"
      "      <keyword>defer</keyword>\n"
      "      <keyword>else</keyword>\n"
      "      <keyword>enum</keyword>\n"
      "      <keyword>errdefer</keyword>\n"
      "      <keyword>error</keyword>\n"
      "      <keyword>export</keyword>\n"
      "      <keyword>extern</keyword>\n"
      "      <keyword>fn</keyword>\n"
      "      <keyword>for</keyword>\n"
      "      <keyword>if</keyword>\n"
      "      <keyword>inline</keyword>\n"
      "      <keyword>linksection</keyword>\n"
      "      <keyword>noalias</keyword>\n"
      "      <keyword>noinline</keyword>\n"
      "      <keyword>nosuspend</keyword>\n"
      "      <keyword>opaque</keyword>\n"
      "      <keyword>or</keyword>\n"
      "      <keyword>orelse</keyword>\n"
      "      <keyword>packed</keyword>\n"
      "      <keyword>pub</keyword>\n"
      "      <keyword>resume</keyword>\n"
      "      <keyword>return</keyword>\n"
      "      <keyword>struct</keyword>\n"
      "      <keyword>suspend</keyword>\n"
      "      <keyword>switch</keyword>\n"
      "      <keyword>test</keyword>\n"
      "      <keyword>threadlocal</keyword>\n"
      "      <keyword>try</keyword>\n"
      "      <keyword>union</keyword>\n"
      "      <keyword>unreachable</keyword>\n"
      "      <keyword>usingnamespace</keyword>\n"
      "      <keyword>var</keyword>\n"
      "      <keyword>volatile</keyword>\n"
      "      <keyword>while</keyword>\n"
      "    </context>\n"
      "    <context id=\"type\" style-ref=\"def:type\">\n"
      "      <keyword>bool</keyword>\n"
      "      <keyword>void</keyword>\n"
      "      <keyword>noreturn</keyword>\n"
      "      <keyword>type</keyword>\n"
      "      <keyword>anyerror</keyword>\n"
      "      <keyword>comptime_int</keyword>\n"
      "      <keyword>comptime_float</keyword>\n"
      "      <keyword>isize</keyword>\n"
      "      <keyword>usize</keyword>\n"
      "      <keyword>u8</keyword>\n"
      "      <keyword>u16</keyword>\n"
      "      <keyword>u32</keyword>\n"
      "      <keyword>u64</keyword>\n"
      "      <keyword>u128</keyword>\n"
      "      <keyword>i8</keyword>\n"
      "      <keyword>i16</keyword>\n"
      "      <keyword>i32</keyword>\n"
      "      <keyword>i64</keyword>\n"
      "      <keyword>i128</keyword>\n"
      "      <keyword>f16</keyword>\n"
      "      <keyword>f32</keyword>\n"
      "      <keyword>f64</keyword>\n"
      "      <keyword>f80</keyword>\n"
      "      <keyword>f128</keyword>\n"
      "    </context>\n"
      "    <context id=\"constant\" style-ref=\"def:constant\">\n"
      "      <keyword>true</keyword>\n"
      "      <keyword>false</keyword>\n"
      "      <keyword>null</keyword>\n"
      "      <keyword>undefined</keyword>\n"
      "    </context>\n"
      "    <context id=\"number\" style-ref=\"def:number\">\n"
      "      <match>\\b0[xX][0-9a-fA-F_]+\\b</match>\n"
      "      <match>\\b0[bB][01_]+\\b</match>\n"
      "      <match>\\b0[oO][0-7_]+\\b</match>\n"
      "      <match>\\b[0-9][0-9_]*(\\.[0-9_]+)?([eE][+-]?[0-9]+)?\\b</match>\n"
      "    </context>\n"
      "    <context id=\"preprocessor\" style-ref=\"def:preprocessor\">\n"
      "      <match>@[a-zA-Z_][a-zA-Z0-9_]*</match>\n"
      "    </context>\n"
      "    <context id=\"operator\" style-ref=\"def:operator\">\n"
      "      <match>[+\\-*/%=!&lt;&gt;&amp;|^~?:;,.(){}\\[\\]]</match>\n"
      "    </context>\n"
      "  </definitions>\n"
      "</language>\n" },

    { "nix", "nix.lang",
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<language id=\"nix\" name=\"Nix\" version=\"2.0\" _section=\"Source\">\n"
      "  <metadata>\n"
      "    <property name=\"globs\">*.nix</property>\n"
      "    <property name=\"mimetypes\">text/x-nix</property>\n"
      "    <property name=\"line-comment-start\">#</property>\n"
      "  </metadata>\n"
      "  <definitions>\n"
      "    <context id=\"comment\" style-ref=\"def:comment\" end-at-line-end=\"true\">\n"
      "      <start>#</start>\n"
      "      <end>\\n</end>\n"
      "    </context>\n"
      "    <context id=\"string\" style-ref=\"def:string\">\n"
      "      <start>''</start>\n"
      "      <end>''</end>\n"
      "    </context>\n"
      "    <context id=\"string-double\" style-ref=\"def:string\" end-at-line-end=\"true\">\n"
      "      <start>\"</start>\n"
      "      <end>\"</end>\n"
      "      <include>\n"
      "        <context id=\"interpolation\" style-ref=\"def:special-char\">\n"
      "          <start>\\$\\{</start>\n"
      "          <end>\\}</end>\n"
      "        </context>\n"
      "        <context id=\"escape\" style-ref=\"def:special-char\">\n"
      "          <match>\\\\[nrt\"\\\\$]</match>\n"
      "        </context>\n"
      "      </include>\n"
      "    </context>\n"
      "    <context id=\"keyword\" style-ref=\"def:keyword\">\n"
      "      <keyword>assert</keyword>\n"
      "      <keyword>else</keyword>\n"
      "      <keyword>if</keyword>\n"
      "      <keyword>in</keyword>\n"
      "      <keyword>inherit</keyword>\n"
      "      <keyword>let</keyword>\n"
      "      <keyword>or</keyword>\n"
      "      <keyword>rec</keyword>\n"
      "      <keyword>then</keyword>\n"
      "      <keyword>with</keyword>\n"
      "    </context>\n"
      "    <context id=\"constant\" style-ref=\"def:constant\">\n"
      "      <keyword>true</keyword>\n"
      "      <keyword>false</keyword>\n"
      "      <keyword>null</keyword>\n"
      "    </context>\n"
      "    <context id=\"number\" style-ref=\"def:number\">\n"
      "      <match>\\b[0-9]+(\\.[0-9]+)?\\b</match>\n"
      "    </context>\n"
      "    <context id=\"path\" style-ref=\"def:string\">\n"
      "      <match>(\\.\\.?|~)?/[a-zA-Z0-9._+\\-/]*</match>\n"
      "    </context>\n"
      "    <context id=\"operator\" style-ref=\"def:operator\">\n"
      "      <match>[@+\\-*/&lt;&gt;=!&amp;|?:;.,(){}\\[\\]]</match>\n"
      "    </context>\n"
      "  </definitions>\n"
      "</language>\n" },

    { "typescript", "typescript.lang",
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<language id=\"typescript\" name=\"TypeScript\" version=\"2.0\" _section=\"Source\">\n"
      "  <metadata>\n"
      "    <property name=\"globs\">*.ts;*.tsx;*.mts;*.cts</property>\n"
      "    <property name=\"mimetypes\">text/typescript;application/x-typescript</property>\n"
      "    <property name=\"line-comment-start\">//</property>\n"
      "    <property name=\"block-comment-start\">/*</property>\n"
      "    <property name=\"block-comment-end\">*/</property>\n"
      "  </metadata>\n"
      "  <definitions>\n"
      "    <context id=\"comment\" style-ref=\"def:comment\" end-at-line-end=\"true\">\n"
      "      <start>//</start>\n"
      "      <end>\\n</end>\n"
      "    </context>\n"
      "    <context id=\"block-comment\" style-ref=\"def:comment\">\n"
      "      <start>/\\*</start>\n"
      "      <end>\\*/</end>\n"
      "    </context>\n"
      "    <context id=\"template-string\" style-ref=\"def:string\">\n"
      "      <start>`</start>\n"
      "      <end>`</end>\n"
      "      <include>\n"
      "        <context id=\"template-interpolation\" style-ref=\"def:special-char\">\n"
      "          <start>\\$\\{</start>\n"
      "          <end>\\}</end>\n"
      "        </context>\n"
      "      </include>\n"
      "    </context>\n"
      "    <context id=\"string\" style-ref=\"def:string\" end-at-line-end=\"true\">\n"
      "      <start>\"</start>\n"
      "      <end>\"</end>\n"
      "      <include>\n"
      "        <context id=\"escape\" style-ref=\"def:special-char\">\n"
      "          <match>\\\\[\\\\\"'nrtbfv0xu]</match>\n"
      "        </context>\n"
      "      </include>\n"
      "    </context>\n"
      "    <context id=\"string-single\" style-ref=\"def:string\" end-at-line-end=\"true\">\n"
      "      <start>'</start>\n"
      "      <end>'</end>\n"
      "      <include>\n"
      "        <context id=\"escape-single\" style-ref=\"def:special-char\">\n"
      "          <match>\\\\[\\\\'nrtbfv0xu]</match>\n"
      "        </context>\n"
      "      </include>\n"
      "    </context>\n"
      "    <context id=\"keyword\" style-ref=\"def:keyword\">\n"
      "      <keyword>as</keyword>\n"
      "      <keyword>async</keyword>\n"
      "      <keyword>await</keyword>\n"
      "      <keyword>break</keyword>\n"
      "      <keyword>case</keyword>\n"
      "      <keyword>catch</keyword>\n"
      "      <keyword>class</keyword>\n"
      "      <keyword>const</keyword>\n"
      "      <keyword>continue</keyword>\n"
      "      <keyword>debugger</keyword>\n"
      "      <keyword>declare</keyword>\n"
      "      <keyword>default</keyword>\n"
      "      <keyword>delete</keyword>\n"
      "      <keyword>do</keyword>\n"
      "      <keyword>else</keyword>\n"
      "      <keyword>enum</keyword>\n"
      "      <keyword>export</keyword>\n"
      "      <keyword>extends</keyword>\n"
      "      <keyword>false</keyword>\n"
      "      <keyword>finally</keyword>\n"
      "      <keyword>for</keyword>\n"
      "      <keyword>from</keyword>\n"
      "      <keyword>function</keyword>\n"
      "      <keyword>get</keyword>\n"
      "      <keyword>if</keyword>\n"
      "      <keyword>implements</keyword>\n"
      "      <keyword>import</keyword>\n"
      "      <keyword>in</keyword>\n"
      "      <keyword>instanceof</keyword>\n"
      "      <keyword>interface</keyword>\n"
      "      <keyword>keyof</keyword>\n"
      "      <keyword>let</keyword>\n"
      "      <keyword>namespace</keyword>\n"
      "      <keyword>new</keyword>\n"
      "      <keyword>null</keyword>\n"
      "      <keyword>of</keyword>\n"
      "      <keyword>private</keyword>\n"
      "      <keyword>protected</keyword>\n"
      "      <keyword>public</keyword>\n"
      "      <keyword>readonly</keyword>\n"
      "      <keyword>return</keyword>\n"
      "      <keyword>satisfies</keyword>\n"
      "      <keyword>set</keyword>\n"
      "      <keyword>static</keyword>\n"
      "      <keyword>super</keyword>\n"
      "      <keyword>switch</keyword>\n"
      "      <keyword>this</keyword>\n"
      "      <keyword>throw</keyword>\n"
      "      <keyword>true</keyword>\n"
      "      <keyword>try</keyword>\n"
      "      <keyword>type</keyword>\n"
      "      <keyword>typeof</keyword>\n"
      "      <keyword>undefined</keyword>\n"
      "      <keyword>var</keyword>\n"
      "      <keyword>void</keyword>\n"
      "      <keyword>while</keyword>\n"
      "      <keyword>with</keyword>\n"
      "      <keyword>yield</keyword>\n"
      "    </context>\n"
      "    <context id=\"type\" style-ref=\"def:type\">\n"
      "      <keyword>any</keyword>\n"
      "      <keyword>bigint</keyword>\n"
      "      <keyword>boolean</keyword>\n"
      "      <keyword>never</keyword>\n"
      "      <keyword>number</keyword>\n"
      "      <keyword>object</keyword>\n"
      "      <keyword>string</keyword>\n"
      "      <keyword>symbol</keyword>\n"
      "      <keyword>unknown</keyword>\n"
      "    </context>\n"
      "    <context id=\"number\" style-ref=\"def:number\">\n"
      "      <match>\\b0[xX][0-9a-fA-F_]+n?\\b</match>\n"
      "      <match>\\b0[bB][01_]+n?\\b</match>\n"
      "      <match>\\b0[oO][0-7_]+n?\\b</match>\n"
      "      <match>\\b[0-9][0-9_]*(\\.[0-9_]+)?([eE][+-]?[0-9]+)?n?\\b</match>\n"
      "    </context>\n"
      "    <context id=\"regex\" style-ref=\"def:string\">\n"
      "      <match>/(?:[^/\\\\\\n]|\\\\.)+/[gimsuy]*</match>\n"
      "    </context>\n"
      "    <context id=\"operator\" style-ref=\"def:operator\">\n"
      "      <match>[+\\-*/%=!&lt;&gt;&amp;|^~?:;,.(){}\\[\\]]</match>\n"
      "    </context>\n"
      "  </definitions>\n"
      "</language>\n" },

    { "dockerfile", "dockerfile.lang",
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<language id=\"dockerfile\" name=\"Dockerfile\" version=\"2.0\" _section=\"Source\">\n"
      "  <metadata>\n"
      "    <property name=\"globs\">Dockerfile;*.dockerfile;Containerfile</property>\n"
      "    <property name=\"mimetypes\">text/x-dockerfile</property>\n"
      "    <property name=\"line-comment-start\">#</property>\n"
      "  </metadata>\n"
      "  <styles>\n"
      "    <style id=\"variable\" name=\"Variable\" map-to=\"def:identifier\"/>\n"
      "  </styles>\n"
      "  <definitions>\n"
      "    <context id=\"comment\" style-ref=\"def:comment\" end-at-line-end=\"true\">\n"
      "      <start>#</start>\n"
      "      <end>\\n</end>\n"
      "    </context>\n"
      "    <context id=\"string\" style-ref=\"def:string\" end-at-line-end=\"true\">\n"
      "      <start>\"</start>\n"
      "      <end>\"</end>\n"
      "      <include>\n"
      "        <context id=\"escape\" style-ref=\"def:special-char\">\n"
      "          <match>\\\\[\\\\\"'nrt$]</match>\n"
      "        </context>\n"
      "      </include>\n"
      "    </context>\n"
      "    <context id=\"string-single\" style-ref=\"def:string\" end-at-line-end=\"true\">\n"
      "      <start>'</start>\n"
      "      <end>'</end>\n"
      "    </context>\n"
      "    <context id=\"keyword\" style-ref=\"def:keyword\">\n"
      "      <keyword>FROM</keyword>\n"
      "      <keyword>RUN</keyword>\n"
      "      <keyword>CMD</keyword>\n"
      "      <keyword>LABEL</keyword>\n"
      "      <keyword>MAINTAINER</keyword>\n"
      "      <keyword>EXPOSE</keyword>\n"
      "      <keyword>ENV</keyword>\n"
      "      <keyword>ADD</keyword>\n"
      "      <keyword>COPY</keyword>\n"
      "      <keyword>ENTRYPOINT</keyword>\n"
      "      <keyword>VOLUME</keyword>\n"
      "      <keyword>USER</keyword>\n"
      "      <keyword>WORKDIR</keyword>\n"
      "      <keyword>ARG</keyword>\n"
      "      <keyword>ONBUILD</keyword>\n"
      "      <keyword>STOPSIGNAL</keyword>\n"
      "      <keyword>HEALTHCHECK</keyword>\n"
      "      <keyword>SHELL</keyword>\n"
      "      <keyword>AS</keyword>\n"
      "    </context>\n"
      "    <context id=\"variable\" style-ref=\"def:variable\">\n"
      "      <match>\\$\\{[a-zA-Z_][a-zA-Z0-9_]*\\}</match>\n"
      "      <match>\\$[a-zA-Z_][a-zA-Z0-9_]*</match>\n"
      "    </context>\n"
      "    <context id=\"number\" style-ref=\"def:number\">\n"
      "      <match>\\b[0-9]+\\b</match>\n"
      "    </context>\n"
      "  </definitions>\n"
      "</language>\n" },

    { "dart", "dart.lang",
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<language id=\"dart\" name=\"Dart\" version=\"2.0\" _section=\"Source\">\n"
      "  <metadata>\n"
      "    <property name=\"globs\">*.dart</property>\n"
      "    <property name=\"mimetypes\">text/x-dart;application/dart</property>\n"
      "    <property name=\"line-comment-start\">//</property>\n"
      "    <property name=\"block-comment-start\">/*</property>\n"
      "    <property name=\"block-comment-end\">*/</property>\n"
      "  </metadata>\n"
      "  <definitions>\n"
      "    <context id=\"comment\" style-ref=\"def:comment\" end-at-line-end=\"true\">\n"
      "      <start>//</start>\n"
      "      <end>\\n</end>\n"
      "    </context>\n"
      "    <context id=\"block-comment\" style-ref=\"def:comment\">\n"
      "      <start>/\\*</start>\n"
      "      <end>\\*/</end>\n"
      "    </context>\n"
      "    <context id=\"annotation\" style-ref=\"def:preprocessor\" end-at-line-end=\"true\">\n"
      "      <start>@</start>\n"
      "      <end>\\s</end>\n"
      "    </context>\n"
      "    <context id=\"triple-string\" style-ref=\"def:string\">\n"
      "      <start>\"\"\"</start>\n"
      "      <end>\"\"\"</end>\n"
      "    </context>\n"
      "    <context id=\"triple-string-single\" style-ref=\"def:string\">\n"
      "      <start>'''</start>\n"
      "      <end>'''</end>\n"
      "    </context>\n"
      "    <context id=\"string\" style-ref=\"def:string\" end-at-line-end=\"true\">\n"
      "      <start>\"</start>\n"
      "      <end>\"</end>\n"
      "      <include>\n"
      "        <context id=\"interpolation\" style-ref=\"def:special-char\">\n"
      "          <start>\\$\\{</start>\n"
      "          <end>\\}</end>\n"
      "        </context>\n"
      "        <context id=\"escape\" style-ref=\"def:special-char\">\n"
      "          <match>\\\\[\\\\\"'nrtbfv0xu$]</match>\n"
      "        </context>\n"
      "      </include>\n"
      "    </context>\n"
      "    <context id=\"string-single\" style-ref=\"def:string\" end-at-line-end=\"true\">\n"
      "      <start>'</start>\n"
      "      <end>'</end>\n"
      "      <include>\n"
      "        <context id=\"escape-single\" style-ref=\"def:special-char\">\n"
      "          <match>\\\\[\\\\'nrtbfv0xu$]</match>\n"
      "        </context>\n"
      "      </include>\n"
      "    </context>\n"
      "    <context id=\"keyword\" style-ref=\"def:keyword\">\n"
      "      <keyword>abstract</keyword>\n"
      "      <keyword>as</keyword>\n"
      "      <keyword>assert</keyword>\n"
      "      <keyword>async</keyword>\n"
      "      <keyword>await</keyword>\n"
      "      <keyword>break</keyword>\n"
      "      <keyword>case</keyword>\n"
      "      <keyword>catch</keyword>\n"
      "      <keyword>class</keyword>\n"
      "      <keyword>const</keyword>\n"
      "      <keyword>continue</keyword>\n"
      "      <keyword>covariant</keyword>\n"
      "      <keyword>default</keyword>\n"
      "      <keyword>deferred</keyword>\n"
      "      <keyword>do</keyword>\n"
      "      <keyword>dynamic</keyword>\n"
      "      <keyword>else</keyword>\n"
      "      <keyword>enum</keyword>\n"
      "      <keyword>export</keyword>\n"
      "      <keyword>extends</keyword>\n"
      "      <keyword>extension</keyword>\n"
      "      <keyword>external</keyword>\n"
      "      <keyword>factory</keyword>\n"
      "      <keyword>final</keyword>\n"
      "      <keyword>finally</keyword>\n"
      "      <keyword>for</keyword>\n"
      "      <keyword>get</keyword>\n"
      "      <keyword>hide</keyword>\n"
      "      <keyword>if</keyword>\n"
      "      <keyword>implements</keyword>\n"
      "      <keyword>import</keyword>\n"
      "      <keyword>in</keyword>\n"
      "      <keyword>interface</keyword>\n"
      "      <keyword>is</keyword>\n"
      "      <keyword>late</keyword>\n"
      "      <keyword>library</keyword>\n"
      "      <keyword>mixin</keyword>\n"
      "      <keyword>new</keyword>\n"
      "      <keyword>on</keyword>\n"
      "      <keyword>operator</keyword>\n"
      "      <keyword>part</keyword>\n"
      "      <keyword>required</keyword>\n"
      "      <keyword>rethrow</keyword>\n"
      "      <keyword>return</keyword>\n"
      "      <keyword>set</keyword>\n"
      "      <keyword>show</keyword>\n"
      "      <keyword>static</keyword>\n"
      "      <keyword>super</keyword>\n"
      "      <keyword>switch</keyword>\n"
      "      <keyword>sync</keyword>\n"
      "      <keyword>this</keyword>\n"
      "      <keyword>throw</keyword>\n"
      "      <keyword>try</keyword>\n"
      "      <keyword>typedef</keyword>\n"
      "      <keyword>var</keyword>\n"
      "      <keyword>void</keyword>\n"
      "      <keyword>while</keyword>\n"
      "      <keyword>with</keyword>\n"
      "      <keyword>yield</keyword>\n"
      "    </context>\n"
      "    <context id=\"type\" style-ref=\"def:type\">\n"
      "      <keyword>bool</keyword>\n"
      "      <keyword>double</keyword>\n"
      "      <keyword>int</keyword>\n"
      "      <keyword>num</keyword>\n"
      "      <keyword>Object</keyword>\n"
      "      <keyword>String</keyword>\n"
      "      <keyword>Future</keyword>\n"
      "      <keyword>Stream</keyword>\n"
      "      <keyword>List</keyword>\n"
      "      <keyword>Map</keyword>\n"
      "      <keyword>Set</keyword>\n"
      "      <keyword>Iterable</keyword>\n"
      "    </context>\n"
      "    <context id=\"constant\" style-ref=\"def:constant\">\n"
      "      <keyword>true</keyword>\n"
      "      <keyword>false</keyword>\n"
      "      <keyword>null</keyword>\n"
      "    </context>\n"
      "    <context id=\"number\" style-ref=\"def:number\">\n"
      "      <match>\\b0[xX][0-9a-fA-F]+\\b</match>\n"
      "      <match>\\b[0-9]+(\\.[0-9]+)?([eE][+-]?[0-9]+)?\\b</match>\n"
      "    </context>\n"
      "    <context id=\"operator\" style-ref=\"def:operator\">\n"
      "      <match>[+\\-*/%=!&lt;&gt;&amp;|^~?:;,.(){}\\[\\]]</match>\n"
      "    </context>\n"
      "  </definitions>\n"
      "</language>\n" },

    { "terraform", "terraform.lang",
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<language id=\"terraform\" name=\"Terraform\" version=\"2.0\" _section=\"Source\">\n"
      "  <metadata>\n"
      "    <property name=\"globs\">*.tf;*.tfvars;*.hcl;*.nomad</property>\n"
      "    <property name=\"mimetypes\">text/x-terraform;text/x-hcl</property>\n"
      "    <property name=\"line-comment-start\">#</property>\n"
      "    <property name=\"block-comment-start\">/*</property>\n"
      "    <property name=\"block-comment-end\">*/</property>\n"
      "  </metadata>\n"
      "  <definitions>\n"
      "    <context id=\"comment\" style-ref=\"def:comment\" end-at-line-end=\"true\">\n"
      "      <start>#</start>\n"
      "      <end>\\n</end>\n"
      "    </context>\n"
      "    <context id=\"comment-slash\" style-ref=\"def:comment\" end-at-line-end=\"true\">\n"
      "      <start>//</start>\n"
      "      <end>\\n</end>\n"
      "    </context>\n"
      "    <context id=\"block-comment\" style-ref=\"def:comment\">\n"
      "      <start>/\\*</start>\n"
      "      <end>\\*/</end>\n"
      "    </context>\n"
      "    <context id=\"heredoc\" style-ref=\"def:string\">\n"
      "      <start>&lt;&lt;-?[A-Za-z_][A-Za-z0-9_]*</start>\n"
      "      <end>^\\s*[A-Za-z_][A-Za-z0-9_]*\\s*$</end>\n"
      "    </context>\n"
      "    <context id=\"string\" style-ref=\"def:string\" end-at-line-end=\"true\">\n"
      "      <start>\"</start>\n"
      "      <end>\"</end>\n"
      "      <include>\n"
      "        <context id=\"interpolation\" style-ref=\"def:special-char\">\n"
      "          <start>\\$\\{</start>\n"
      "          <end>\\}</end>\n"
      "        </context>\n"
      "        <context id=\"escape\" style-ref=\"def:special-char\">\n"
      "          <match>\\\\[\\\\\"nrt$%]</match>\n"
      "        </context>\n"
      "      </include>\n"
      "    </context>\n"
      "    <context id=\"keyword\" style-ref=\"def:keyword\">\n"
      "      <keyword>resource</keyword>\n"
      "      <keyword>data</keyword>\n"
      "      <keyword>variable</keyword>\n"
      "      <keyword>output</keyword>\n"
      "      <keyword>module</keyword>\n"
      "      <keyword>provider</keyword>\n"
      "      <keyword>terraform</keyword>\n"
      "      <keyword>locals</keyword>\n"
      "      <keyword>dynamic</keyword>\n"
      "      <keyword>for_each</keyword>\n"
      "      <keyword>count</keyword>\n"
      "      <keyword>depends_on</keyword>\n"
      "      <keyword>lifecycle</keyword>\n"
      "      <keyword>provisioner</keyword>\n"
      "      <keyword>connection</keyword>\n"
      "      <keyword>backend</keyword>\n"
      "      <keyword>required_providers</keyword>\n"
      "      <keyword>if</keyword>\n"
      "      <keyword>else</keyword>\n"
      "      <keyword>for</keyword>\n"
      "      <keyword>in</keyword>\n"
      "      <keyword>endfor</keyword>\n"
      "      <keyword>endif</keyword>\n"
      "    </context>\n"
      "    <context id=\"constant\" style-ref=\"def:constant\">\n"
      "      <keyword>true</keyword>\n"
      "      <keyword>false</keyword>\n"
      "      <keyword>null</keyword>\n"
      "    </context>\n"
      "    <context id=\"number\" style-ref=\"def:number\">\n"
      "      <match>\\b[0-9]+(\\.[0-9]+)?\\b</match>\n"
      "    </context>\n"
      "    <context id=\"operator\" style-ref=\"def:operator\">\n"
      "      <match>[-+=!&lt;&gt;&amp;|?:.,(){}\\[\\]]</match>\n"
      "    </context>\n"
      "  </definitions>\n"
      "</language>\n" },

    { "graphql", "graphql.lang",
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<language id=\"graphql\" name=\"GraphQL\" version=\"2.0\" _section=\"Source\">\n"
      "  <metadata>\n"
      "    <property name=\"globs\">*.graphql;*.gql;*.graphqls</property>\n"
      "    <property name=\"mimetypes\">application/graphql</property>\n"
      "    <property name=\"line-comment-start\">#</property>\n"
      "  </metadata>\n"
      "  <definitions>\n"
      "    <context id=\"comment\" style-ref=\"def:comment\" end-at-line-end=\"true\">\n"
      "      <start>#</start>\n"
      "      <end>\\n</end>\n"
      "    </context>\n"
      "    <context id=\"block-string\" style-ref=\"def:string\">\n"
      "      <start>\"\"\"</start>\n"
      "      <end>\"\"\"</end>\n"
      "    </context>\n"
      "    <context id=\"string\" style-ref=\"def:string\" end-at-line-end=\"true\">\n"
      "      <start>\"</start>\n"
      "      <end>\"</end>\n"
      "      <include>\n"
      "        <context id=\"escape\" style-ref=\"def:special-char\">\n"
      "          <match>\\\\[\"\\\\/bfnrtu]</match>\n"
      "        </context>\n"
      "      </include>\n"
      "    </context>\n"
      "    <context id=\"keyword\" style-ref=\"def:keyword\">\n"
      "      <keyword>query</keyword>\n"
      "      <keyword>mutation</keyword>\n"
      "      <keyword>subscription</keyword>\n"
      "      <keyword>fragment</keyword>\n"
      "      <keyword>on</keyword>\n"
      "      <keyword>schema</keyword>\n"
      "      <keyword>type</keyword>\n"
      "      <keyword>interface</keyword>\n"
      "      <keyword>union</keyword>\n"
      "      <keyword>enum</keyword>\n"
      "      <keyword>input</keyword>\n"
      "      <keyword>scalar</keyword>\n"
      "      <keyword>directive</keyword>\n"
      "      <keyword>extend</keyword>\n"
      "      <keyword>implements</keyword>\n"
      "      <keyword>repeatable</keyword>\n"
      "    </context>\n"
      "    <context id=\"constant\" style-ref=\"def:constant\">\n"
      "      <keyword>true</keyword>\n"
      "      <keyword>false</keyword>\n"
      "      <keyword>null</keyword>\n"
      "    </context>\n"
      "    <context id=\"type\" style-ref=\"def:type\">\n"
      "      <keyword>Int</keyword>\n"
      "      <keyword>Float</keyword>\n"
      "      <keyword>String</keyword>\n"
      "      <keyword>Boolean</keyword>\n"
      "      <keyword>ID</keyword>\n"
      "    </context>\n"
      "    <context id=\"annotation\" style-ref=\"def:preprocessor\" end-at-line-end=\"true\">\n"
      "      <start>@</start>\n"
      "      <end>\\s</end>\n"
      "    </context>\n"
      "    <context id=\"variable\" style-ref=\"def:special-char\">\n"
      "      <match>\\$[A-Za-z_][A-Za-z0-9_]*</match>\n"
      "    </context>\n"
      "    <context id=\"number\" style-ref=\"def:number\">\n"
      "      <match>-?\\b[0-9]+(\\.[0-9]+)?([eE][+-]?[0-9]+)?\\b</match>\n"
      "    </context>\n"
      "    <context id=\"operator\" style-ref=\"def:operator\">\n"
      "      <match>[=!&lt;&gt;&amp;|:.,(){}\\[\\]]</match>\n"
      "    </context>\n"
      "  </definitions>\n"
      "</language>\n" },
};

static void
install_extra_languages(void)
{
    gchar *dir = g_build_filename(g_get_user_data_dir(),
        "gtksourceview-3.0", "language-specs", NULL);
    g_mkdir_with_parents(dir, 0755);

    for (guint i = 0; i < G_N_ELEMENTS(extra_langs); i++) {
        gchar *path = g_build_filename(dir, extra_langs[i].file, NULL);
        gchar *existing = NULL;
        gboolean same = g_file_get_contents(path, &existing, NULL, NULL) &&
                        g_strcmp0(existing, extra_langs[i].spec) == 0;
        g_free(existing);
        if (!same)
            g_file_set_contents(path, extra_langs[i].spec, -1, NULL);
        g_free(path);
    }
    g_free(dir);
}

static const gchar *
language_id_for_basename(const gchar *basename)
{
    static const struct { const gchar *name; const gchar *id; } table[] = {
        { "Makefile",     "makefile" },
        { "GNUmakefile",  "makefile" },
        { "makefile",     "makefile" },
        { "Dockerfile",   "dockerfile" },
        { "Containerfile","dockerfile" },
        { "CMakeLists.txt","cmake" },
        { "SConstruct",   "python3" },
        { "meson.build",  "meson" },
        { "PKGBUILD",     "sh" },
        { "Rakefile",     "ruby" },
        { "Gemfile",      "ruby" },
    };
    for (guint i = 0; i < G_N_ELEMENTS(table); i++)
        if (g_strcmp0(basename, table[i].name) == 0)
            return table[i].id;
    return NULL;
}

static void
apply_language_for_path(GtkSourceBuffer *buffer, const gchar *path)
{
    GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default();
    GtkSourceLanguage *lang = NULL;

    if (path) {
        const gchar *base = strrchr(path, '/');
        base = base ? base + 1 : path;

        const gchar *forced = language_id_for_basename(base);
        if (forced)
            lang = gtk_source_language_manager_get_language(lm, forced);

        if (!lang)
            lang = gtk_source_language_manager_guess_language(lm, path, NULL);
        if (!lang && !strchr(base, '.')) {
            gchar *contents = NULL;
            if (g_file_get_contents(path, &contents, NULL, NULL)) {
                gchar *first = contents;
                gchar *nl = strchr(contents, '\n');
                if (nl) *nl = '\0';
                lang = gtk_source_language_manager_guess_language(lm, base, first);
                g_free(first);
            }
        }
    }

    gtk_source_buffer_set_language(buffer, lang);
}

static GtkSourceStyleScheme *
current_style_scheme(void)
{
    GtkSourceStyleSchemeManager *sm = gtk_source_style_scheme_manager_get_default();
    GtkSourceStyleScheme *scheme = NULL;
    if (settings.style_scheme)
        scheme = gtk_source_style_scheme_manager_get_scheme(sm, settings.style_scheme);
    if (!scheme)
        scheme = gtk_source_style_scheme_manager_get_scheme(sm, "classic");
    return scheme;
}

static GtkCssProvider *font_provider = NULL;

static void
apply_font_css(void)
{
    if (!font_provider) {
        font_provider = gtk_css_provider_new();
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
            GTK_STYLE_PROVIDER(font_provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    gchar *css = g_strdup_printf(
        "textview { font-family: %s; font-size: %dpt; }",
        settings.font_family ? settings.font_family : "Monospace",
        settings.font_size);
    gtk_css_provider_load_from_data(font_provider, css, -1, NULL);
    g_free(css);
}

static void
apply_appearance(EditorTab *tab)
{
    GtkSourceStyleScheme *scheme = current_style_scheme();
    if (scheme)
        gtk_source_buffer_set_style_scheme(tab->buffer, scheme);
}

static void
apply_style_to_all_tabs(void)
{
    apply_font_css();
    gint n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook));
    for (gint i = 0; i < n; i++) {
        GtkWidget *scroll = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), i);
        EditorTab *tab = g_object_get_data(G_OBJECT(scroll), "tab");
        if (tab) apply_appearance(tab);
    }
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
    apply_appearance(tab);

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
    return TRUE;
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
        if (name[0] == '.') continue;
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

/* ---------- search ---------- */

static GtkWidget *search_entry;
static GtkWidget *search_status;
static GtkWidget *settings_notebook;

static gboolean
do_search(gboolean forward)
{
    EditorTab *tab = current_tab();
    if (!tab) return FALSE;

    const gchar *needle = gtk_entry_get_text(GTK_ENTRY(search_entry));
    if (!needle || !*needle) {
        if (search_status) gtk_label_set_text(GTK_LABEL(search_status), "");
        return FALSE;
    }

    GtkTextBuffer *buf = GTK_TEXT_BUFFER(tab->buffer);
    GtkTextIter start, ms, me;
    GtkTextSearchFlags flags = 0;
    if (!settings.search_case) flags |= GTK_TEXT_SEARCH_CASE_INSENSITIVE;

    GtkTextIter sel_start, sel_end;
    gboolean has_sel = gtk_text_buffer_get_selection_bounds(buf, &sel_start, &sel_end);
    gboolean found = FALSE;

    if (forward) {
        if (has_sel)
            start = sel_end;
        else
            gtk_text_buffer_get_iter_at_mark(buf, &start, gtk_text_buffer_get_insert(buf));

        found = gtk_text_iter_forward_search(&start, needle, flags, &ms, &me, NULL);
        if (!found && settings.search_wrap) {
            gtk_text_buffer_get_start_iter(buf, &start);
            found = gtk_text_iter_forward_search(&start, needle, flags, &ms, &me, NULL);
        }
    } else {
        if (has_sel)
            start = sel_start;
        else
            gtk_text_buffer_get_iter_at_mark(buf, &start, gtk_text_buffer_get_insert(buf));

        found = gtk_text_iter_backward_search(&start, needle, flags, &ms, &me, NULL);
        if (!found && settings.search_wrap) {
            gtk_text_buffer_get_end_iter(buf, &start);
            found = gtk_text_iter_backward_search(&start, needle, flags, &ms, &me, NULL);
        }
    }

    if (found) {
        gtk_text_buffer_select_range(buf, &ms, &me);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(tab->source_view), &ms, 0.0, TRUE, 0.0, 0.5);
        if (search_status) gtk_label_set_text(GTK_LABEL(search_status), "");
    } else if (search_status) {
        gtk_label_set_text(GTK_LABEL(search_status), "Not found");
    }
    return found;
}

static void on_search_activate(GtkEntry *e, gpointer d) { (void)e; (void)d; do_search(TRUE); }
static void on_search_next(GtkButton *b, gpointer d)    { (void)b; (void)d; do_search(TRUE); }
static void on_search_prev(GtkButton *b, gpointer d)    { (void)b; (void)d; do_search(FALSE); }

/* ---------- settings window ---------- */

static GtkWidget *settings_window = NULL;
static GtkWidget *theme_combo;
static GtkWidget *zoom_spin;
static GtkWidget *dark_check;
static GtkWidget *font_button;
static gboolean is_fullscreen = FALSE;

static gboolean
on_window_state(GtkWidget *w, GdkEventWindowState *e, gpointer d)
{
    (void)w; (void)d;
    is_fullscreen = (e->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) != 0;
    return FALSE;
}

static void
toggle_fullscreen(void)
{
    if (is_fullscreen)
        gtk_window_unfullscreen(GTK_WINDOW(window));
    else
        gtk_window_fullscreen(GTK_WINDOW(window));
}

static void
on_font_set(GtkFontButton *b, gpointer d)
{
    (void)d;
    const gchar *name = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(b));
    if (!name || !*name) return;

    PangoFontDescription *fd = pango_font_description_from_string(name);
    const gchar *family = pango_font_description_get_family(fd);
    gint size = pango_font_description_get_size(fd) / PANGO_SCALE;

    if (family) {
        g_free(settings.font_family);
        settings.font_family = g_strdup(family);
    }
    if (size >= 6 && size <= 48) {
        settings.font_size = size;
        if (zoom_spin)
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(zoom_spin), size);
    }
    pango_font_description_free(fd);

    apply_style_to_all_tabs();
    save_config();
}

static gboolean
on_key_press(GtkWidget *w, GdkEventKey *e, gpointer d)
{
    (void)w; (void)d;
    if (e->keyval == GDK_KEY_Escape && is_fullscreen) {
        gtk_window_unfullscreen(GTK_WINDOW(window));
        return TRUE;
    }
    return FALSE;
}

static void
on_scheme_changed(GtkComboBox *c, gpointer d)
{
    (void)d;
    const gchar *id = gtk_combo_box_get_active_id(c);
    if (!id) return;
    g_free(settings.style_scheme);
    settings.style_scheme = g_strdup(id);
    apply_style_to_all_tabs();
    save_config();
}

static void
on_zoom_changed(GtkSpinButton *s, gpointer d)
{
    (void)d;
    settings.font_size = gtk_spin_button_get_value_as_int(s);
    apply_style_to_all_tabs();
    save_config();
}

static void
on_dark_toggled(GtkToggleButton *t, gpointer d)
{
    (void)d;
    settings.dark_ui = gtk_toggle_button_get_active(t);
    g_object_set(gtk_settings_get_default(),
        "gtk-application-prefer-dark-theme", settings.dark_ui, NULL);
    save_config();
}

static void
on_search_wrap_toggled(GtkToggleButton *t, gpointer d)
{
    (void)d;
    settings.search_wrap = gtk_toggle_button_get_active(t);
    save_config();
}

static void
on_search_case_toggled(GtkToggleButton *t, gpointer d)
{
    (void)d;
    settings.search_case = gtk_toggle_button_get_active(t);
    save_config();
}

static gboolean
on_settings_delete(GtkWidget *w, GdkEvent *e, gpointer d)
{
    (void)e; (void)d;
    gtk_widget_hide(w);
    return TRUE;
}

static void
build_settings_window(void)
{
    settings_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(settings_window), "Settings");
    gtk_window_set_transient_for(GTK_WINDOW(settings_window), GTK_WINDOW(window));
    gtk_window_set_default_size(GTK_WINDOW(settings_window), 440, 340);
    g_signal_connect(settings_window, "delete-event", G_CALLBACK(on_settings_delete), NULL);

    settings_notebook = gtk_notebook_new();
    gtk_container_add(GTK_CONTAINER(settings_window), settings_notebook);

    /* --- Appearance --- */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 14);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Color scheme:"), 0, 0, 1, 1);
    theme_combo = gtk_combo_box_text_new();
    GtkSourceStyleSchemeManager *sm = gtk_source_style_scheme_manager_get_default();
    const gchar * const *ids = gtk_source_style_scheme_manager_get_scheme_ids(sm);
    for (gint i = 0; ids && ids[i]; i++) {
        GtkSourceStyleScheme *sch = gtk_source_style_scheme_manager_get_scheme(sm, ids[i]);
        const gchar *label = sch ? gtk_source_style_scheme_get_name(sch) : ids[i];
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme_combo), ids[i], label);
    }
    if (settings.style_scheme) {
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(theme_combo), settings.style_scheme);
    } else {
        GtkSourceStyleScheme *active = current_style_scheme();
        if (active)
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(theme_combo),
                gtk_source_style_scheme_get_id(active));
    }
    g_signal_connect(theme_combo, "changed", G_CALLBACK(on_scheme_changed), NULL);
    gtk_grid_attach(GTK_GRID(grid), theme_combo, 1, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Font:"), 0, 1, 1, 1);
    font_button = gtk_font_button_new_with_font(
        settings.font_family ? settings.font_family : "Monospace");
    gtk_font_button_set_use_font(GTK_FONT_BUTTON(font_button), TRUE);
    gtk_font_button_set_use_size(GTK_FONT_BUTTON(font_button), TRUE);
    gtk_font_button_set_show_style(GTK_FONT_BUTTON(font_button), FALSE);
    gtk_font_button_set_title(GTK_FONT_BUTTON(font_button), "Select editor font");
    g_signal_connect(font_button, "font-set", G_CALLBACK(on_font_set), NULL);
    gtk_grid_attach(GTK_GRID(grid), font_button, 1, 1, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Font size:"), 0, 2, 1, 1);
    zoom_spin = gtk_spin_button_new_with_range(6, 48, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(zoom_spin), settings.font_size);
    g_signal_connect(zoom_spin, "value-changed", G_CALLBACK(on_zoom_changed), NULL);
    gtk_grid_attach(GTK_GRID(grid), zoom_spin, 1, 2, 1, 1);

    dark_check = gtk_check_button_new_with_label("Dark interface theme");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dark_check), settings.dark_ui);
    g_signal_connect(dark_check, "toggled", G_CALLBACK(on_dark_toggled), NULL);
    gtk_grid_attach(GTK_GRID(grid), dark_check, 0, 3, 2, 1);

    gtk_notebook_append_page(GTK_NOTEBOOK(settings_notebook), grid,
        gtk_label_new("Appearance"));

    /* --- Search --- */
    GtkWidget *sbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(sbox), 14);

    search_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry), "Search text");
    g_signal_connect(search_entry, "activate", G_CALLBACK(on_search_activate), NULL);
    gtk_box_pack_start(GTK_BOX(sbox), search_entry, FALSE, FALSE, 0);

    GtkWidget *btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *prev_btn = gtk_button_new_with_label("Previous");
    GtkWidget *next_btn = gtk_button_new_with_label("Next");
    g_signal_connect(prev_btn, "clicked", G_CALLBACK(on_search_prev), NULL);
    g_signal_connect(next_btn, "clicked", G_CALLBACK(on_search_next), NULL);
    gtk_box_pack_start(GTK_BOX(btn_row), prev_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btn_row), next_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sbox), btn_row, FALSE, FALSE, 0);

    GtkWidget *wrap_check = gtk_check_button_new_with_label("Wrap around");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wrap_check), settings.search_wrap);
    g_signal_connect(wrap_check, "toggled", G_CALLBACK(on_search_wrap_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(sbox), wrap_check, FALSE, FALSE, 0);

    GtkWidget *case_check = gtk_check_button_new_with_label("Match case");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(case_check), settings.search_case);
    g_signal_connect(case_check, "toggled", G_CALLBACK(on_search_case_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(sbox), case_check, FALSE, FALSE, 0);

    search_status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(search_status), 0.0);
    gtk_box_pack_start(GTK_BOX(sbox), search_status, FALSE, FALSE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(settings_notebook), sbox,
        gtk_label_new("Search"));
}

static void
show_settings(gint page_index)
{
    if (!settings_window) build_settings_window();
    gtk_widget_show_all(settings_window);
    gtk_window_present(GTK_WINDOW(settings_window));
    if (page_index >= 0)
        gtk_notebook_set_current_page(GTK_NOTEBOOK(settings_notebook), page_index);
    if (page_index == 1 && search_entry)
        gtk_widget_grab_focus(search_entry);
}

/* ---------- sidebar show / hide ---------- */

static void
set_sidebar_visible(gboolean visible)
{
    settings.sidebar_visible = visible;
    if (!paned_widget || !notebook || !sidebar_scroll) return;

    if (visible) {
        gtk_widget_set_no_show_all(paned_widget, FALSE);

        if (gtk_widget_get_parent(notebook) != paned_widget) {
            g_object_ref(notebook);
            gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(notebook)), notebook);
            gtk_paned_pack2(GTK_PANED(paned_widget), notebook, TRUE, TRUE);
            g_object_unref(notebook);
        }

        gtk_widget_show(sidebar_scroll);
        gtk_widget_show(notebook);
        gtk_widget_show(paned_widget);
        gtk_paned_set_position(GTK_PANED(paned_widget), settings.sidebar_position);
    } else {
        if (gtk_widget_get_visible(paned_widget)) {
            gint pos = gtk_paned_get_position(GTK_PANED(paned_widget));
            if (pos > 0) settings.sidebar_position = pos;
        }

        if (gtk_widget_get_parent(notebook) == paned_widget) {
            GtkWidget *parent = gtk_widget_get_parent(paned_widget);
            g_object_ref(notebook);
            gtk_container_remove(GTK_CONTAINER(paned_widget), notebook);
            gtk_box_pack_start(GTK_BOX(parent), notebook, TRUE, TRUE, 0);
            gtk_box_reorder_child(GTK_BOX(parent), notebook, 1);
            g_object_unref(notebook);
        }

        gtk_widget_show(notebook);
        gtk_widget_set_no_show_all(paned_widget, TRUE);
        gtk_widget_hide(paned_widget);
    }

    if (sidebar_menu_item && sidebar_menu_handler_id) {
        g_signal_handler_block(sidebar_menu_item, sidebar_menu_handler_id);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(sidebar_menu_item), visible);
        g_signal_handler_unblock(sidebar_menu_item, sidebar_menu_handler_id);
    }
}

static void
on_sidebar_toggle(GtkCheckMenuItem *item, gpointer d)
{
    (void)d;
    set_sidebar_visible(gtk_check_menu_item_get_active(item));
}

static void
on_paned_position_changed(GObject *gobject, GParamSpec *pspec, gpointer data)
{
    (void)pspec; (void)data;
    if (!settings.sidebar_visible) return;
    gint pos = gtk_paned_get_position(GTK_PANED(gobject));
    if (pos > 0) settings.sidebar_position = pos;
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

static void
sync_zoom_spin(void)
{
    if (zoom_spin)
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(zoom_spin), settings.font_size);
    if (font_button)
        gtk_font_chooser_set_font(GTK_FONT_CHOOSER(font_button),
            settings.font_family ? settings.font_family : "Monospace");
}

static void
action_zoom_in(GtkMenuItem *m, gpointer d)
{
    (void)m; (void)d;
    if (settings.font_size >= 48) return;
    settings.font_size++;
    apply_style_to_all_tabs();
    sync_zoom_spin();
    save_config();
}

static void
action_zoom_out(GtkMenuItem *m, gpointer d)
{
    (void)m; (void)d;
    if (settings.font_size <= 6) return;
    settings.font_size--;
    apply_style_to_all_tabs();
    sync_zoom_spin();
    save_config();
}

static void
action_zoom_reset(GtkMenuItem *m, gpointer d)
{
    (void)m; (void)d;
    settings.font_size = 11;
    apply_style_to_all_tabs();
    sync_zoom_spin();
    save_config();
}

static void action_find(GtkMenuItem *m, gpointer d)      { (void)m; (void)d; show_settings(1); }
static void action_settings(GtkMenuItem *m, gpointer d)  { (void)m; (void)d; show_settings(0); }
static void action_find_next(GtkMenuItem *m, gpointer d) { (void)m; (void)d; do_search(TRUE); }
static void action_find_prev(GtkMenuItem *m, gpointer d) { (void)m; (void)d; do_search(FALSE); }

static void
action_fullscreen(GtkMenuItem *m, gpointer d)
{
    (void)m; (void)d;
    toggle_fullscreen();
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
menu_item_toggle(const char *label, guint key, GdkModifierType mods, gboolean active)
{
    GtkWidget *item = gtk_check_menu_item_new_with_label(label);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), active);
    gtk_widget_add_accelerator(item, "activate", accel_group, key, mods, GTK_ACCEL_VISIBLE);
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
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu),
        menu_item_with_accel("Find...", GDK_KEY_f, GDK_CONTROL_MASK, action_find));
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu),
        menu_item_with_accel("Find Next", GDK_KEY_F3, 0, action_find_next));
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu),
        menu_item_with_accel("Find Previous", GDK_KEY_F3, GDK_SHIFT_MASK, action_find_prev));
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu),
        menu_item_with_accel("Settings...", GDK_KEY_comma, GDK_CONTROL_MASK, action_settings));

    GtkWidget *view_menu = gtk_menu_new();
    GtkWidget *view_item = gtk_menu_item_new_with_label("View");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(view_item), view_menu);

    sidebar_menu_item = menu_item_toggle("Show Sidebar", GDK_KEY_b,
        GDK_CONTROL_MASK, settings.sidebar_visible);
    sidebar_menu_handler_id = g_signal_connect(sidebar_menu_item, "toggled",
        G_CALLBACK(on_sidebar_toggle), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), sidebar_menu_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), gtk_separator_menu_item_new());

    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu),
        menu_item_with_accel("Zoom In", GDK_KEY_plus, GDK_CONTROL_MASK, action_zoom_in));
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu),
        menu_item_with_accel("Zoom Out", GDK_KEY_minus, GDK_CONTROL_MASK, action_zoom_out));
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu),
        menu_item_with_accel("Zoom Reset", GDK_KEY_0, GDK_CONTROL_MASK, action_zoom_reset));
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu),
        menu_item_with_accel("Fullscreen", GDK_KEY_F11, 0, action_fullscreen));

    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), file_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), edit_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), view_item);
    return menubar;
}

/* ---------- window close ---------- */

static gboolean
on_delete_event(GtkWidget *w, GdkEvent *e, gpointer d)
{
    (void)w; (void)e; (void)d;
    gint n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook));
    for (gint i = 0; i < n; i++) {
        GtkWidget *scroll = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), i);
        EditorTab *tab = g_object_get_data(G_OBJECT(scroll), "tab");
        if (!confirm_discard(tab)) return TRUE;
    }
    if (paned_widget && settings.sidebar_visible) {
        gint pos = gtk_paned_get_position(GTK_PANED(paned_widget));
        if (pos > 0) settings.sidebar_position = pos;
    }
    save_config();
    return FALSE;
}

/* ---------- main ---------- */

int
main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    load_config();
    install_extra_languages();
    apply_font_css();
    save_config();

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "rezzedit");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 680);
    g_signal_connect(window, "delete-event", G_CALLBACK(on_delete_event), NULL);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), NULL);
    g_signal_connect(window, "window-state-event", G_CALLBACK(on_window_state), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    gtk_box_pack_start(GTK_BOX(vbox), build_menu_bar(), FALSE, FALSE, 0);

    GtkWidget *content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(vbox), content_box, TRUE, TRUE, 0);

    paned_widget = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(content_box), paned_widget, TRUE, TRUE, 0);
    g_signal_connect(paned_widget, "notify::position",
        G_CALLBACK(on_paned_position_changed), NULL);

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

    sidebar_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(sidebar_scroll, 220, -1);
    gtk_container_add(GTK_CONTAINER(sidebar_scroll), file_tree);
    gtk_paned_pack1(GTK_PANED(paned_widget), sidebar_scroll, FALSE, TRUE);

    /* Editor area */
    notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
    gtk_paned_pack2(GTK_PANED(paned_widget), notebook, TRUE, TRUE);

    const gchar *home = g_get_home_dir();
    load_root(home ? home : "/");
    new_tab(NULL);

    gtk_paned_set_position(GTK_PANED(paned_widget), settings.sidebar_position);

    gtk_widget_show_all(window);

    if (!settings.sidebar_visible) {
        set_sidebar_visible(FALSE);
    }

    gtk_main();

    return 0;
}
