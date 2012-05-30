#include <stdbool.h>

#include <gtk/gtk.h>
#include <vte/vte.h>

#if GTK_CHECK_VERSION (2, 90, 7)
#include <gdk/gdkkeysyms-compat.h>
#else
#include <gdk/gdkkeysyms.h>
#endif

#include "config.h"

#ifndef __GNUC__
# define __attribute__(x)
#endif

typedef struct search_dialog_info {
    GtkWidget *vte;
    GtkWidget *entry;
    bool reverse;
    bool open;
} search_dialog_info;

static void search(VteTerminal *vte, const char *pattern, bool reverse) {
    GRegex *regex = vte_terminal_search_get_gregex(vte);
    if (regex) g_regex_unref(regex);
    regex = g_regex_new(pattern, 0, 0, NULL);
    vte_terminal_search_set_gregex(vte, regex);

    if (!reverse) {
        vte_terminal_search_find_next(vte);
    } else {
        vte_terminal_search_find_previous(vte);
    }
    vte_terminal_copy_primary(vte);
}

static void search_response_cb(GtkDialog *dialog, gint response_id, search_dialog_info *info) {
    if (response_id == GTK_RESPONSE_ACCEPT) {
        search(VTE_TERMINAL(info->vte), gtk_entry_get_text(GTK_ENTRY(info->entry)), info->reverse);
    }

    gtk_widget_destroy(GTK_WIDGET(dialog));
    info->open = false;
}

static void open_search_dialog(GtkWidget *vte, bool reverse, search_dialog_info *info) {
    info->reverse = reverse;

    if (info->open) {
        return;
    }

    info->open = true;
    info->entry = gtk_entry_new();

    GtkWidget *dialog, *content_area;
    dialog = gtk_dialog_new_with_buttons("Search",
                                         GTK_WINDOW(gtk_widget_get_toplevel(vte)),
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_STOCK_OK,
                                         GTK_RESPONSE_ACCEPT,
                                         NULL);
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    g_signal_connect(dialog, "response", G_CALLBACK(search_response_cb), info);

    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    gtk_entry_set_activates_default(GTK_ENTRY(info->entry), TRUE);

    gtk_container_add(GTK_CONTAINER(content_area), info->entry);
    gtk_widget_show_all(dialog);
    gtk_widget_grab_focus(GTK_WIDGET(info->entry));
}

static gboolean key_press_cb(GtkWidget *vte, GdkEventKey *event, search_dialog_info *info) {
    const GdkModifierType modifiers = event->state & gtk_accelerator_get_default_mod_mask();
    if (modifiers == (GDK_CONTROL_MASK|GDK_SHIFT_MASK)) {
        switch (gdk_keyval_to_lower(event->keyval)) {
            case GDK_c:
                vte_terminal_copy_clipboard(VTE_TERMINAL(vte));
                return TRUE;
            case GDK_v:
                vte_terminal_paste_clipboard(VTE_TERMINAL(vte));
                return TRUE;
            case GDK_p:
                vte_terminal_search_find_previous(VTE_TERMINAL(vte));
                vte_terminal_copy_primary(VTE_TERMINAL(vte));
                return TRUE;
            case GDK_n:
                vte_terminal_search_find_next(VTE_TERMINAL(vte));
                vte_terminal_copy_primary(VTE_TERMINAL(vte));
                return TRUE;
            case GDK_f:
                open_search_dialog(vte, false, info);
                return TRUE;
            case GDK_b:
                open_search_dialog(vte, true, info);
                return TRUE;
            case GDK_j:
                search(VTE_TERMINAL(vte), url_regex, false);
                return TRUE;
            case GDK_k:
                search(VTE_TERMINAL(vte), url_regex, true);
                return TRUE;
        }
    }
    return FALSE;
}

#ifdef CLICKABLE_URL
static void get_vte_padding(VteTerminal *vte, int *w, int *h) {
    GtkBorder *border = NULL;

    gtk_widget_style_get(GTK_WIDGET(vte), "inner-border", &border, NULL);
    if (border == NULL) {
        g_warning("VTE's inner-border property unavailable");
        *w = *h = 0;
    } else {
        *w = border->left + border->right;
        *h = border->top + border->bottom;
        gtk_border_free(border);
    }
}

static char *check_match(VteTerminal *vte, int event_x, int event_y) {
    int xpad, ypad, tag;
    get_vte_padding(vte, &xpad, &ypad);
    return vte_terminal_match_check(vte,
                                    (event_x - ypad) / vte_terminal_get_char_width(vte),
                                    (event_y - ypad) / vte_terminal_get_char_height(vte),
                                    &tag);
}

static gboolean button_press_cb(VteTerminal *vte, GdkEventButton *event) {
    char *match = check_match(vte, event->x, event->y);
    if (event->button == 1 && event->type == GDK_BUTTON_PRESS && match != NULL) {
        const char *argv[3] = {url_command, match, NULL};
        g_spawn_async(NULL, (char **)argv, NULL, 0, NULL, NULL, NULL, NULL);
        g_free(match);
        return TRUE;
    }
    return FALSE;
}
#endif

#ifdef URGENT_ON_BEEP
static void beep_handler(__attribute__((unused)) VteTerminal *vte, GtkWindow *window) {
    if (!gtk_window_is_active(window)) {
        gtk_window_set_urgency_hint(window, TRUE);
    }
}
#endif

#ifdef DYNAMIC_TITLE
static void window_title_cb(VteTerminal *vte, GtkWindow *window) {
    const char *t = vte_terminal_get_window_title(vte);
    gtk_window_set_title(window, t ? t : "termite");
}
#endif

int main(int argc, char **argv) {
    GError *error = NULL;

    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    /*gtk_window_set_default_size(GTK_WINDOW(window), 400, 400);*/
    GdkPixbuf *icon = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), "terminal", 48, 0, NULL);
    if (icon) {
        gtk_window_set_icon (GTK_WINDOW (window), icon);
    }

    GtkWidget *vte = vte_terminal_new();

    char **command_argv;
    char *default_argv[2] = {NULL, NULL};

    if (argc > 1) {
        command_argv = &argv[1];
    } else {
        default_argv[0] = vte_get_user_shell();
        if (!default_argv[0]) default_argv[0] = "/bin/sh";
        command_argv = default_argv;
    }

    VtePty *pty = vte_terminal_pty_new(VTE_TERMINAL(vte), 0, &error);

    if (!pty) {
        fprintf(stderr, "Failed to create pty: %s\n", error->message);
        return 1;
    }

    vte_terminal_set_pty_object(VTE_TERMINAL(vte), pty);
    vte_pty_set_term(pty, term);

    GPid ppid;

    if (g_spawn_async(NULL, command_argv, NULL,
                      G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
                      (GSpawnChildSetupFunc)vte_pty_child_setup, pty,
                      &ppid, &error)) {
        vte_terminal_watch_child(VTE_TERMINAL(vte), ppid);
    } else {
        fprintf(stderr, "The new terminal's command failed to run: %s\n", error->message);
        return 1;
    }

    gtk_container_add(GTK_CONTAINER(window), vte);

    g_signal_connect(vte, "child-exited", G_CALLBACK(gtk_main_quit), NULL);

    vte_terminal_set_scrollback_lines(VTE_TERMINAL(vte), scrollback_lines);
    vte_terminal_set_font_from_string(VTE_TERMINAL(vte), font);
    vte_terminal_set_scroll_on_output(VTE_TERMINAL(vte), scroll_on_output);
    vte_terminal_set_scroll_on_keystroke(VTE_TERMINAL(vte), scroll_on_keystroke);
    vte_terminal_set_audible_bell(VTE_TERMINAL(vte), audible_bell);
    vte_terminal_set_visible_bell(VTE_TERMINAL(vte), visible_bell);
    vte_terminal_set_mouse_autohide(VTE_TERMINAL(vte), mouse_autohide);

#ifdef TRANSPARENCY
    GdkScreen *screen = gtk_widget_get_screen(window);
    GdkColormap *colormap = gdk_screen_get_rgba_colormap(screen);
    if (colormap != NULL) {
        gtk_widget_set_colormap(window, colormap);
    }
    vte_terminal_set_background_saturation(VTE_TERMINAL(vte), TRANSPARENCY);
    vte_terminal_set_opacity(VTE_TERMINAL(vte), (guint16)(0xffff * (1 - TRANSPARENCY)));
#endif

    // set colors
    GdkColor foreground, background;
    gdk_color_parse(foreground_color, &foreground);
    gdk_color_parse(background_color, &background);

    GdkColor palette[16];

    for (size_t i = 0; i < 16; i++) {
        gdk_color_parse(colors[i], &palette[i]);
    }

    vte_terminal_set_colors(VTE_TERMINAL(vte), &foreground, &background, palette, 16);

    search_dialog_info info = { .vte = vte, .open = false };

    g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(vte, "key-press-event", G_CALLBACK(key_press_cb), &info);

#ifdef CLICKABLE_URL
    int tmp = vte_terminal_match_add_gregex(VTE_TERMINAL(vte),
                                            g_regex_new(url_regex,
                                                        G_REGEX_CASELESS,
                                                        G_REGEX_MATCH_NOTEMPTY,
                                                        NULL),
                                            0);
    vte_terminal_match_set_cursor_type(VTE_TERMINAL(vte), tmp, GDK_HAND2);
    g_signal_connect(vte, "button-press-event", G_CALLBACK(button_press_cb), NULL);
#endif

#ifdef URGENT_ON_BEEP
    if (g_signal_lookup("beep", G_TYPE_FROM_INSTANCE(G_OBJECT(vte)))) {
        g_signal_connect(vte, "beep", G_CALLBACK(beep_handler), window);
    }
#endif

#ifdef DYNAMIC_TITLE
    window_title_cb(VTE_TERMINAL(vte), GTK_WINDOW(window));
    g_signal_connect(vte, "window-title-changed", G_CALLBACK(window_title_cb), window);
#endif

    gtk_widget_grab_focus(vte);
    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
