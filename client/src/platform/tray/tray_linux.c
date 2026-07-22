// Linux tray icon via GtkStatusIcon. GTK3 is already a hard dependency
// (webview's WebKitGTK backend pulls it in), so this adds zero new deps --
// unlike libappindicator3, the more modern alternative.
//
// Known limitation: GtkStatusIcon does not render on stock GNOME Shell
// (X11 or Wayland) without a third-party extension ("AppIndicator and
// KStatusNotifierItem Support", "TopIcons", ...). Works fine on KDE Plasma,
// XFCE, and GNOME with that extension installed.

#include "platform/tray.h"
#include <gtk/gtk.h>

typedef struct {
    tray_result_t result;
} tray_state_t;

static void on_show(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    tray_state_t *state = user_data;
    state->result = TRAY_RESULT_SHOW;
    gtk_main_quit();
}

static void on_quit(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    tray_state_t *state = user_data;
    state->result = TRAY_RESULT_QUIT;
    gtk_main_quit();
}

// Single click on the icon itself is treated as an implicit "Show".
static void on_activate(GtkStatusIcon *status_icon, gpointer user_data)
{
    (void)status_icon;
    on_show(NULL, user_data);
}

static void on_popup_menu(GtkStatusIcon *status_icon, guint button, guint activate_time,
                          gpointer user_data)
{
    (void)status_icon;

    GtkWidget *menu = gtk_menu_new();

    GtkWidget *show_item = gtk_menu_item_new_with_label("Show");
    g_signal_connect(show_item, "activate", G_CALLBACK(on_show), user_data);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), show_item);

    GtkWidget *quit_item = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(quit_item, "activate", G_CALLBACK(on_quit), user_data);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);

    gtk_widget_show_all(menu);
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, button, activate_time);
}

tray_result_t tray_run(void)
{
    // gtk_init_check(), not gtk_init(): the latter calls exit() on failure
    // (e.g. no display available), which would take the whole background
    // daemon down with it rather than just failing this one call.
    if (!gtk_init_check(NULL, NULL)) {
        return TRAY_RESULT_ERROR;
    }

    tray_state_t state = {.result = TRAY_RESULT_ERROR};

    GtkStatusIcon *icon = gtk_status_icon_new_from_icon_name("audio-card");
    gtk_status_icon_set_title(icon, "TinyPad");
    gtk_status_icon_set_visible(icon, TRUE);

    g_signal_connect(icon, "activate", G_CALLBACK(on_activate), &state);
    g_signal_connect(icon, "popup-menu", G_CALLBACK(on_popup_menu), &state);

    gtk_main(); // blocks until gtk_main_quit()

    gtk_status_icon_set_visible(icon, FALSE);
    g_object_unref(icon);

    return state.result;
}
