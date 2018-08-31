/*
 * Origianl work Copyright (C) 2010-2011 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * Modified work Copyright (C) 2017 Gooroom Project Team
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include <glib-unix.h>

#include <locale.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <glib.h>
#include <gtk/gtkx.h>
#include <glib/gslist.h>
#include <signal.h>
#include <upower.h>

#include <libindicator/indicator-object.h>

#include <lightdm.h>

#include "greeterconfiguration.h"
#include "greeterbackground.h"
#include "gooroom-greeter-ui.h"


static LightDMGreeter *greeter;

/* Screen window */
static GtkOverlay   *screen_overlay;

/* Login window */
static GtkWidget    *login_win;
static GtkWidget    *login_win_login_image, *login_win_shadow_image;
static GtkWidget    *login_win_username_box;
static GtkLabel     *login_win_username_label, *login_win_msg_label;
static GtkEntry     *login_win_username_entry, *login_win_pw_entry;
static GtkInfoBar   *login_win_infobar;
static GtkButton    *login_win_login_button;

/* Panel */
static GtkWidget    *panel_box;
static GtkWidget    *btn_shutdown, *btn_restart, *btn_suspend, *btn_hibernate;
static GtkWidget    *indicator_menubar;

/* Power window */
static GtkWidget    *cmd_win;
static GtkButton    *cmd_win_ok_button, *cmd_win_cancel_button;
static GtkLabel     *cmd_title_label, *cmd_msg_label;
static GtkImage     *cmd_icon_image;

static GtkWidget    *pw_set_win;
static GtkButton    *pw_set_win_ok_button, *pw_set_win_cancel_button;
static GtkLabel     *pw_set_win_title_label, *pw_set_win_prompt_label, *pw_set_win_msg_label;
static GtkEntry     *pw_set_win_prompt_entry;

static GtkWidget    *ask_win;
static GtkButton    *ask_win_ok_button, *ask_win_cancel_button;
static GtkLabel     *ask_win_msg_label, *ask_win_title_label;

static GtkWidget    *msg_win;
static GtkButton    *msg_win_ok_button;
static GtkLabel     *msg_win_msg_label, *msg_win_title_label;

static GtkWidget    *cur_show_win;
static GtkWidget    *cur_focused_widget;
static gboolean      changing_password;


/* Clock */
static gchar *clock_format;

/* Session */
static gchar *current_session;

/* Sesion language */
static gchar *current_language;

/* Screensaver values */
static int timeout, interval, prefer_blanking, allow_exposures;

/* Handling monitors backgrounds */
static GreeterBackground *greeter_background;

/* Authentication state */
static gboolean prompted = FALSE;
static gboolean prompt_active = FALSE, password_prompted = FALSE;

/* Pending questions */
static GSList *pending_questions = NULL;


static gchar *default_font_name,
             *default_theme_name,
             *default_icon_theme_name;

static const gchar *CMD_WIN_DATA = "cmd-win-data";
static const gchar *MSG_WIN_DATA = "msg-win-data";
static const gchar *ASK_WIN_DATA = "ask-win-data";

//static const gchar *WINDOW_DATA_POSITION = "window-position"; /* <WindowPosition*> */

enum {
    SYSTEM_SHUTDOWN,
    SYSTEM_RESTART,
    SYSTEM_SUSPEND,
    SYSTEM_HIBERNATE
};

/* Handling window position */
typedef struct
{
    gint value;
    /* +0 and -0 */
    gint sign;
    /* interpret 'value' as percentage of screen width/height */
    gboolean percentage;
    /* -1: left/top, 0: center, +1: right,bottom */
    gint anchor;
} DimensionPosition;

typedef struct
{
    DimensionPosition x, y;
    /* Flag to use width and height fileds */
    gboolean use_size;
    DimensionPosition width, height;
} WindowPosition;

typedef struct
{
    gboolean is_prompt;
    union
    {
        LightDMMessageType message;
        LightDMPromptType prompt;
    } type;
    gchar *text;
} PAMConversationMessage;

struct SavedFocusData
{
    GtkWidget *widget;
    gint editable_pos;
};

/* Some default positions */
//static const WindowPosition WINDOW_POS_CENTER   = {.x = { 50, +1, TRUE,   0}, .y = { 50, +1, TRUE,   0}, .use_size = FALSE};



gpointer greeter_save_focus                    (GtkWidget* widget);
void     greeter_restore_focus                 (const gpointer saved_data);

void     login_win_pw_entry_activate_cb        (GtkWidget *widget);
void     pw_set_win_prompt_entry_activate_cb   (GtkWidget *widget);
void     pw_set_win_prompt_entry_changed_cb    (GtkEditable *editable,
                                                gchar       *new_text,
                                                gint         new_text_length,
                                                gpointer     position,
                                                gpointer     user_data);

void     cmd_win_button_clicked_cb             (GtkButton *button, gpointer user_data);
void     ask_win_button_clicked_cb             (GtkButton *button, gpointer user_data);
void     msg_win_button_clicked_cb             (GtkButton *button, gpointer user_data);
void     pw_set_win_button_clicked_cb          (GtkButton *button, gpointer user_data);
void     login_win_login_button_clicked_cb     (GtkButton *button, gpointer user_data);

gboolean login_win_pw_entry_key_press_cb       (GtkWidget *widget, GdkEventKey *event, gpointer user_data);
gboolean login_win_username_entry_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data);
gboolean login_win_username_entry_focus_out_cb (GtkWidget *widget, GdkEvent *event, gpointer user_data);

gboolean cmd_win_key_press_event_cb            (GtkWidget *widget, GdkEventKey *event, gpointer user_data);
gboolean ask_win_key_press_event_cb            (GtkWidget *widget, GdkEventKey *event, gpointer user_data);
gboolean msg_win_key_press_event_cb            (GtkWidget *widget, GdkEventKey *event, gpointer user_data);
gboolean pw_set_win_key_press_event_cb         (GtkWidget *widget, GdkEventKey *event, gpointer user_data);





static void process_prompts (LightDMGreeter *greeter);
static void start_authentication (const gchar *username);

/* Clock */
static gboolean
clock_timeout_thread (gpointer data)
{
    GtkLabel *clock_label = GTK_LABEL (data);

    GDateTime *dt = NULL;

    dt = g_date_time_new_now_local ();
    if (dt) {
        gchar *fm = g_date_time_format (dt, clock_format);
        gchar *markup = g_markup_printf_escaped ("<b><span foreground=\"white\">%s</span></b>", fm);
        gtk_label_set_markup (GTK_LABEL (clock_label), markup);
        g_free (fm);
        g_free (markup);

        g_date_time_unref (dt);
    }

    return TRUE;
}

static void
on_indicator_item_shown_cb (GtkWidget *widget, gpointer data)
{
    GtkWidget *menuitem = GTK_WIDGET (data);

    gtk_widget_show (menuitem);
}

static void
on_indicator_item_hidden_cb (GtkWidget *widget, gpointer data)
{
    GtkWidget *menuitem = GTK_WIDGET (data);

    gtk_widget_hide (menuitem);
}

static void
on_indicator_item_sensitive_cb (GObject *obj, GParamSpec *pspec, gpointer data)
{
    g_return_if_fail (GTK_IS_WIDGET (obj));
    g_return_if_fail (GTK_IS_WIDGET (data));

    gtk_widget_set_sensitive (GTK_WIDGET (data), gtk_widget_get_sensitive (GTK_WIDGET (obj)));
}

static gboolean
find_app_indicators (const gchar *name)
{
    gboolean found = FALSE;

    gchar **app_indicators = config_get_string_list (NULL, "app-indicators", NULL);

    if (app_indicators) {
        guint i;
        for (i = 0; app_indicators[i] != NULL; i++) {
            if (g_strcmp0 (name, app_indicators[i]) == 0) {
                found = TRUE;
                break;
            }
        }
        g_strfreev (app_indicators);
    }

    return found;
}

static void
entry_added (IndicatorObject *io, IndicatorObjectEntry *entry, gpointer user_data)
{
    g_return_if_fail (entry != NULL);

    if ((g_strcmp0 (entry->name_hint, "nm-applet") == 0) ||
        (find_app_indicators (entry->name_hint)))
    {
        gboolean indicator_item_visible = FALSE;
        gboolean indicator_item_sensitive = FALSE;

        GtkWidget *item = gtk_menu_item_new ();

        g_object_set_data (G_OBJECT (item), "indicator-entry", entry);

        GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
        gtk_container_add (GTK_CONTAINER (item), hbox);
        gtk_widget_show (hbox);

        if (entry->image != NULL) {
            gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (entry->image), FALSE, FALSE, 0);
            if (gtk_widget_get_visible (GTK_WIDGET (entry->image))) {
                indicator_item_visible = TRUE;
            }

            if (gtk_widget_get_sensitive (GTK_WIDGET (entry->image))) {
                indicator_item_sensitive = TRUE;
            }

            g_signal_connect (G_OBJECT (entry->image), "show", G_CALLBACK (on_indicator_item_shown_cb), item);
            g_signal_connect (G_OBJECT (entry->image), "hide", G_CALLBACK (on_indicator_item_hidden_cb), item);
            g_signal_connect (G_OBJECT (entry->image), "notify::sensitive", G_CALLBACK (on_indicator_item_sensitive_cb), item);
        }

        if (entry->label != NULL) {
            gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (entry->label), FALSE, FALSE, 0);
            if (gtk_widget_get_visible (GTK_WIDGET (entry->label))) {
                indicator_item_visible = TRUE;
            }

            if (gtk_widget_get_sensitive (GTK_WIDGET (entry->label))) {
                indicator_item_sensitive = TRUE;
            }

            g_signal_connect (G_OBJECT (entry->label), "show", G_CALLBACK (on_indicator_item_shown_cb), item);
            g_signal_connect (G_OBJECT (entry->label), "hide", G_CALLBACK (on_indicator_item_hidden_cb), item);
            g_signal_connect (G_OBJECT (entry->label), "notify::sensitive", G_CALLBACK (on_indicator_item_sensitive_cb), item);
        }

        if (entry->menu != NULL) {
            gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), GTK_WIDGET (entry->menu));
        }

        gtk_menu_shell_append (GTK_MENU_SHELL (indicator_menubar), item);

        if (indicator_item_visible)
            gtk_widget_show (item);

        gtk_widget_set_sensitive (item, indicator_item_sensitive);
    }
}

static void
entry_removed_cb (GtkWidget *widget, gpointer data)
{
    IndicatorObjectEntry *removed_entry = (IndicatorObjectEntry *)data;

    IndicatorObjectEntry *entry = (IndicatorObjectEntry *)g_object_get_data (G_OBJECT (widget), "indicator-entry");

    if (entry != removed_entry)
        return;

    if (entry->label != NULL) {
        g_signal_handlers_disconnect_by_func (G_OBJECT (entry->label), G_CALLBACK (on_indicator_item_shown_cb), widget);
        g_signal_handlers_disconnect_by_func (G_OBJECT (entry->label), G_CALLBACK (on_indicator_item_hidden_cb), widget);
        g_signal_handlers_disconnect_by_func (G_OBJECT (entry->label), G_CALLBACK (on_indicator_item_sensitive_cb), widget);
    }
    if (entry->image != NULL) {
        g_signal_handlers_disconnect_by_func (G_OBJECT (entry->image), G_CALLBACK (on_indicator_item_shown_cb), widget);
        g_signal_handlers_disconnect_by_func (G_OBJECT (entry->image), G_CALLBACK (on_indicator_item_hidden_cb), widget);
        g_signal_handlers_disconnect_by_func (G_OBJECT (entry->image), G_CALLBACK (on_indicator_item_sensitive_cb), widget);
    }

    gtk_container_remove (GTK_CONTAINER (indicator_menubar), widget);

    gtk_widget_destroy (widget);
}

static void
entry_removed (IndicatorObject *io, IndicatorObjectEntry *entry, gpointer user_data)
{
    gtk_container_foreach (GTK_CONTAINER (indicator_menubar), entry_removed_cb, entry);
}

static void
on_power_device_changed_cb (UpDevice *device, GParamSpec *pspec, gpointer data)
{
    GtkImage *battery_image = GTK_IMAGE (data);

    gchar *icon_name;

    g_object_get (device,
                  "icon-name", &icon_name,
                  NULL);

    GdkPixbuf *pix = NULL;
    pix = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                    icon_name, 22,
                                    GTK_ICON_LOOKUP_FORCE_SIZE,
                                    NULL);

    g_free (icon_name);

    if (pix) {
        gtk_image_set_from_pixbuf (battery_image, pix);
        g_object_unref (pix);
    }
}

static void
updevice_added_cb (UpDevice *device)
{
    gboolean is_present = FALSE;
    guint device_type = UP_DEVICE_KIND_UNKNOWN;

    /* hack, this depends on XFPM_DEVICE_TYPE_* being in sync with UP_DEVICE_KIND_* */
    g_object_get (device, "kind", &device_type, NULL);
    g_object_get (device, "is-present", &is_present, NULL);

    if (device_type == UP_DEVICE_KIND_BATTERY && is_present) {
       GtkWidget *item = gtk_separator_menu_item_new ();

       g_object_set_data (G_OBJECT (item), "updevice", device);

       GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
       gtk_container_set_border_width (GTK_CONTAINER (hbox), 5);
       gtk_container_add (GTK_CONTAINER (item), hbox);
       gtk_widget_show (hbox);
   
       GtkWidget *image = gtk_image_new_from_icon_name ("battery-full-symbolic", GTK_ICON_SIZE_BUTTON);
       gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);
       gtk_widget_show (image);
   
       gtk_menu_shell_append (GTK_MENU_SHELL (indicator_menubar), item);
       gtk_widget_show_all (item);

       on_power_device_changed_cb (device, NULL, image);
       g_signal_connect (device, "notify", G_CALLBACK (on_power_device_changed_cb), image);
    }
}

static void
on_power_device_added_cb (UpClient *upclient, UpDevice *device, gpointer data)
{
    updevice_added_cb (device);
}

static void
updevice_removed_cb (GtkWidget *widget, gpointer data)
{
    UpDevice *removed_device = (UpDevice *)data;

    UpDevice *device = (UpDevice*)g_object_get_data (G_OBJECT (widget), "updevice");

    if (device != removed_device)
        return;

    gtk_container_remove (GTK_CONTAINER (indicator_menubar), widget);

    gtk_widget_destroy (widget);
}

static void
on_power_device_removed_cb (UpClient *upclient, UpDevice *device, gpointer data)
{
    gtk_container_foreach (GTK_CONTAINER (indicator_menubar), updevice_removed_cb, device);
}

static void
hide_all_windows (void)
{
    gtk_widget_hide (ask_win);
    gtk_widget_hide (msg_win);
    gtk_widget_hide (cmd_win);
    gtk_widget_hide (login_win);
    gtk_widget_hide (pw_set_win);
}

static gboolean
grab_focus_cb (gpointer data)
{
    GtkWidget *focused = GTK_WIDGET (data);
    gtk_widget_grab_focus (focused);
    cur_focused_widget = focused;

    return FALSE;
}

static void
show_msg_window (const gchar *title, const gchar *msg, const gchar *ok, const gchar *data)
{
    gtk_label_set_label (msg_win_title_label, (title != NULL) ? title : "");
    gtk_label_set_label (msg_win_msg_label, (msg != NULL) ? msg : "");
    gtk_button_set_label (msg_win_ok_button, (ok != NULL) ? ok : "");

    g_object_set_data (G_OBJECT (msg_win), MSG_WIN_DATA, (gpointer)data);

    hide_all_windows ();
    gtk_widget_show (msg_win);

    g_timeout_add (50, grab_focus_cb, msg_win_ok_button);

    cur_show_win = msg_win;
}

static void
show_ask_window (const gchar *title, const gchar *msg, const gchar *ok, const gchar *cancel, const gchar *data)
{
    gtk_label_set_label (ask_win_title_label, (title != NULL) ? title : "");
    gtk_label_set_label (ask_win_msg_label, (msg != NULL) ? msg : "");
    gtk_button_set_label (ask_win_ok_button, (ok != NULL) ? ok : "");
    gtk_button_set_label (ask_win_cancel_button, (cancel != NULL) ? cancel : "");

    g_object_set_data (G_OBJECT (ask_win), ASK_WIN_DATA, (gpointer)data);

    hide_all_windows ();
    gtk_widget_show (ask_win);

    g_timeout_add (50, grab_focus_cb, ask_win_ok_button);

    cur_show_win = ask_win;
}

static void
show_password_settings_window (void)
{
    gtk_entry_set_text (pw_set_win_prompt_entry, "");

    hide_all_windows ();
    gtk_widget_show (pw_set_win);

    g_timeout_add (50, grab_focus_cb, pw_set_win_prompt_entry);

    cur_show_win = pw_set_win;
}

static void
show_power_prompt (const gchar* icon, const gchar* title, const gchar* message, int type)
{
    gchar *new_message = NULL;

    /* Check if there are still users logged in, count them and if so, display a warning */
    gint logged_in_users = 0;
    GList *items = lightdm_user_list_get_users (lightdm_user_list_get_instance ());
    GList *item;
    for (item = items; item; item = item->next)
    {
        LightDMUser *user = item->data;
        if (lightdm_user_get_logged_in (user))
            logged_in_users++;
    }

    if (logged_in_users > 0)
    {
        gchar *warning = g_strdup_printf (ngettext ("Warning: There is still %d user logged in.",
                                                    "Warning: There are still %d users logged in.",
                                                    logged_in_users), logged_in_users);

        new_message = g_markup_printf_escaped ("<b>%s</b>\n%s", warning, message);

        g_free (warning);
    } else {
        new_message = g_strdup (message);
    }

    gtk_button_set_label (cmd_win_ok_button, title);
    gtk_label_set_label (cmd_title_label, title);
    gtk_label_set_markup (cmd_msg_label, new_message);
    gtk_image_set_from_icon_name (cmd_icon_image, icon, GTK_ICON_SIZE_DIALOG);

    g_free (new_message);

    g_object_set_data (G_OBJECT (cmd_win), CMD_WIN_DATA, GINT_TO_POINTER (type));

    hide_all_windows ();
    gtk_widget_show (cmd_win);

    gtk_widget_grab_focus (GTK_WIDGET (cmd_win_cancel_button));
}

static void
on_command_button_clicked_cb (GtkButton *button, gpointer user_data)
{
    const char *img, *title, *msg;
    int type = GPOINTER_TO_INT (user_data);

    switch (type) {
        case SYSTEM_SHUTDOWN:
            img = "gooroom-greeter-shutdown-symbolic";
            title = _("System Shutdown");
            msg = _("Are you sure you want to close all programs and shut down the computer?");
        break;

        case SYSTEM_RESTART:
            img = "gooroom-greeter-restart-symbolic";
            title = _("System Restart");
            msg = _("Are you sure you want to close all programs and restart the computer?");
        break;

        case SYSTEM_SUSPEND:
            img = "gooroom-greeter-suspend-symbolic";
            title = _("System Suspend");
            msg = _("Are you sure you want to suspend the computer?");
        break;

        case SYSTEM_HIBERNATE:
            img = "gooroom-greeter-hibernate-symbolic";
            title = _("System Hibernate");
            msg = _("Are you sure you want to hibernate the computer?");
        break;

        default:
        return;
    }

    show_power_prompt (img, title, msg, type);
}

static void
load_module (const gchar *name)
{
    gchar                *fullpath;
    IndicatorObject      *io;
    GList                *entries, *entry;
    IndicatorObjectEntry *entrydata;

    g_return_if_fail (name != NULL);

    fullpath = g_build_filename (INDICATOR_DIR, name, NULL);
    io = indicator_object_new_from_file (fullpath);
    g_free (fullpath);

    g_signal_connect (G_OBJECT (io), INDICATOR_OBJECT_SIGNAL_ENTRY_ADDED, G_CALLBACK (entry_added), NULL);
    g_signal_connect (G_OBJECT (io), INDICATOR_OBJECT_SIGNAL_ENTRY_REMOVED, G_CALLBACK (entry_removed), NULL);

    entries = indicator_object_get_entries (io);
    entry = NULL;

    for (entry = entries; entry != NULL; entry = g_list_next(entry)) {
        entrydata = (IndicatorObjectEntry *)entry->data;
        entry_added (io, entrydata, NULL);
    }

    g_list_free (entries);
}

static void
indicator_application_service_start (void)
{
    gchar **argv = NULL;
    const gchar *cmd = "systemctl --user start indicator-application";
    g_shell_parse_argv (cmd, NULL, &argv, NULL);

    g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);

    g_strfreev (argv);
}

static void
network_indicator_application_start (void)
{
    g_spawn_command_line_sync ("/usr/bin/gsettings set org.gnome.nm-applet disable-disconnected-notifications false", NULL, NULL, NULL, NULL);
    g_spawn_command_line_sync ("/usr/bin/gsettings set org.gnome.nm-applet disable-connected-notifications false", NULL, NULL, NULL, NULL);

    /* Make nm-applet hide items the user does not have permissions to interact with */
    g_setenv ("NM_APPLET_HIDE_POLICY_ITEMS", "1", TRUE);

    g_spawn_command_line_async ("nm-applet", NULL);
}

static void
other_indicator_application_start (void)
{
    gchar **app_indicators = config_get_string_list (NULL, "app-indicators", NULL);

    if (!app_indicators)
        return;

    guint i;
    for (i = 0; app_indicators[i] != NULL; i++) {
        g_spawn_command_line_async (app_indicators[i], NULL);
    }

    g_strfreev (app_indicators);
}

static void
load_clock_indicator (void)
{
    GtkWidget *item = gtk_separator_menu_item_new ();

    gtk_menu_item_set_label (GTK_MENU_ITEM (item), "");
    GtkWidget *clock_label = gtk_bin_get_child (GTK_BIN (item));

    gtk_menu_shell_append (GTK_MENU_SHELL (indicator_menubar), item);
    gtk_widget_show_all (item);

    /* update clock */
    clock_timeout_thread (clock_label);
    gdk_threads_add_timeout (1000, (GSourceFunc) clock_timeout_thread, clock_label);
}

static void
load_battery_indicator (void)
{
    guint i;
    UpClient *upower = NULL;
    GPtrArray *array = NULL;

    upower = up_client_new ();

    array = up_client_get_devices (upower);

    if (array) {
        for ( i = 0; i < array->len; i++) {
            UpDevice *device = g_ptr_array_index (array, i);

            updevice_added_cb (device);
        }
        g_ptr_array_free (array, TRUE);
    }

    g_signal_connect (upower, "device-added", G_CALLBACK (on_power_device_added_cb), NULL);
    g_signal_connect (upower, "device-removed", G_CALLBACK (on_power_device_removed_cb), NULL);
}

static void
load_application_indicator (void)
{
    /* load application indicator */
    if (g_file_test (INDICATOR_DIR, (G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))) {
        GDir *dir = g_dir_open (INDICATOR_DIR, 0, NULL);

        const gchar *name;
        while ((name = g_dir_read_name (dir)) != NULL) {
            if (!g_str_has_suffix (name, G_MODULE_SUFFIX))
                continue;
            
            if (!g_str_equal (name, "libapplication.so"))
                continue;

            load_module (name);
        }
    }
}

static void
load_indicators (void)
{
    load_clock_indicator ();
    load_battery_indicator ();
    load_application_indicator ();
}

static void
load_commands (void)
{
    gtk_widget_set_visible (btn_shutdown, lightdm_get_can_shutdown());
    gtk_widget_set_visible (btn_restart, lightdm_get_can_restart());
    gtk_widget_set_visible (btn_suspend, lightdm_get_can_suspend());
    gtk_widget_set_visible (btn_hibernate, lightdm_get_can_hibernate());

    g_signal_connect (G_OBJECT (btn_shutdown), "clicked", G_CALLBACK (on_command_button_clicked_cb), GINT_TO_POINTER (SYSTEM_SHUTDOWN));
    g_signal_connect (G_OBJECT (btn_restart), "clicked", G_CALLBACK (on_command_button_clicked_cb), GINT_TO_POINTER (SYSTEM_RESTART));
    g_signal_connect (G_OBJECT (btn_suspend), "clicked", G_CALLBACK (on_command_button_clicked_cb), GINT_TO_POINTER (SYSTEM_SUSPEND));
    g_signal_connect (G_OBJECT (btn_hibernate), "clicked", G_CALLBACK (on_command_button_clicked_cb), GINT_TO_POINTER (SYSTEM_HIBERNATE));
}

static void
read_monitor_configuration (const gchar *group, const gchar *name)
{
    g_debug ("[Configuration] Monitor configuration found: '%s'", name);

    gchar *background = config_get_string (group, CONFIG_KEY_BACKGROUND, NULL);
    greeter_background_set_monitor_config (greeter_background, name, background,
                                           config_get_bool (group, CONFIG_KEY_USER_BACKGROUND, -1),
                                           config_get_bool (group, CONFIG_KEY_LAPTOP, -1),
                                           config_get_int  (group, CONFIG_KEY_T_DURATION, -1),
                                           config_get_enum (group, CONFIG_KEY_T_TYPE,
                                                            TRANSITION_TYPE_FALLBACK,
                                                            "none",         TRANSITION_TYPE_NONE,
                                                            "linear",       TRANSITION_TYPE_LINEAR,
                                                            "ease-in-out",  TRANSITION_TYPE_EASE_IN_OUT, NULL));
    g_free (background);
}

gpointer
greeter_save_focus (GtkWidget* widget)
{
    GtkWidget *window = gtk_widget_get_toplevel(widget);
    if (!GTK_IS_WINDOW (window))
        return NULL;

    struct SavedFocusData *data = g_new0 (struct SavedFocusData, 1);
    data->widget = gtk_window_get_focus (GTK_WINDOW (window));
    data->editable_pos = GTK_IS_EDITABLE(data->widget) ? gtk_editable_get_position (GTK_EDITABLE (data->widget)) : -1;

    return data;
}

void
greeter_restore_focus (const gpointer saved_data)
{
    struct SavedFocusData *data = saved_data;

    if (!saved_data || !GTK_IS_WIDGET (data->widget))
        return;

    gtk_widget_grab_focus (data->widget);
    if (GTK_IS_EDITABLE(data->widget) && data->editable_pos > -1)
        gtk_editable_set_position(GTK_EDITABLE(data->widget), data->editable_pos);
}

static void
sigterm_cb (gpointer user_data)
{
    gboolean is_callback = GPOINTER_TO_INT (user_data);

    if (is_callback)
        g_debug ("SIGTERM received");

    if (is_callback)
    {
        gtk_main_quit ();
        #ifdef KILL_ON_SIGTERM
        /* LP: #1445461 */
        g_debug ("Killing greeter with exit()...");
        exit (EXIT_SUCCESS);
        #endif
    }
}

static void
set_message_label (LightDMMessageType type, const gchar *text)
{
    if (type == LIGHTDM_MESSAGE_TYPE_INFO)
        gtk_info_bar_set_message_type (login_win_infobar, GTK_MESSAGE_INFO);
    else
        gtk_info_bar_set_message_type (login_win_infobar, GTK_MESSAGE_ERROR);

    const gchar *str = (text != NULL) ? text : "";
    gtk_label_set_text (login_win_msg_label, str);
}

static void
show_login_window (void)
{
    hide_all_windows ();
    gtk_widget_show (login_win);

    cur_show_win = login_win;

    gtk_entry_set_text (login_win_pw_entry, "");
    gtk_widget_grab_focus (GTK_WIDGET (login_win_pw_entry));
    set_message_label (LIGHTDM_MESSAGE_TYPE_INFO, NULL);
}

#if 0
/* Handling window position */
static gboolean
read_position_from_str (const gchar *s, DimensionPosition *x)
{
    DimensionPosition p;
    gchar *end = NULL;
    gchar **parts = g_strsplit (s, ",", 2);
    if (parts[0])
    {
        p.value = g_ascii_strtoll (parts[0], &end, 10);
        p.percentage = end && end[0] == '%';
        p.sign = (p.value < 0 || (p.value == 0 && parts[0][0] == '-')) ? -1 : +1;
        if (p.value < 0)
            p.value *= -1;
        if (g_strcmp0(parts[1], "start") == 0)
            p.anchor = -1;
        else if (g_strcmp0(parts[1], "center") == 0)
            p.anchor = 0;
        else if (g_strcmp0(parts[1], "end") == 0)
            p.anchor = +1;
        else
            p.anchor = p.sign > 0 ? -1 : +1;
        *x = p;
    }
    else
        x = NULL;
    g_strfreev (parts);
    return x != NULL;
}

static WindowPosition*
str_to_position (const gchar *str, const WindowPosition *default_value)
{
    WindowPosition* pos = g_new0 (WindowPosition, 1);
    *pos = *default_value;

    if (str)
    {
        gchar *value = g_strdup (str);
        gchar *x = value;
        gchar *y = strchr (value, ' ');
        if (y)
            (y++)[0] = '\0';

        if (read_position_from_str (x, &pos->x))
            /* If there is no y-part then y = x */
            if (!y || !read_position_from_str (y, &pos->y))
                pos->y = pos->x;

        gchar *size_delim = strchr (y ? y : x, ';');
        if (size_delim)
        {
            gchar *x = size_delim + 1;
            if (read_position_from_str (x, &pos->width))
            {
                y = strchr (x, ' ');
                if (y)
                    (y++)[0] = '\0';
                if (!y || !read_position_from_str (y, &pos->height))
                    if (!default_value->use_size)
                        pos->height = pos->width;
                pos->use_size = TRUE;
            }
        }

        g_free (value);
    }

    return pos;
}

static gint
get_absolute_position (const DimensionPosition *p, gint screen, gint window)
{
    gint x = p->percentage ? (screen*p->value)/100 : p->value;
    x = p->sign < 0 ? screen - x : x;
    if (p->anchor > 0)
        x -= window;
    else if (p->anchor == 0)
        x -= window/2;

    if (x < 0)                     /* Offscreen: left/top */
        return 0;
    else if (x + window > screen)  /* Offscreen: right/bottom */
        return screen - window;
    else
        return x;
}
#endif

/* Message label */
static gboolean
message_label_is_empty (void)
{
    return gtk_label_get_text (login_win_msg_label)[0] == '\0';
}

/* Session */
static gboolean
is_valid_session (GList* items, const gchar* session)
{
    for (; items; items = g_list_next (items))
        if (g_strcmp0 (session, lightdm_session_get_key (items->data)) == 0)
            return TRUE;
    return FALSE;
}

static gchar*
get_session (void)
{
    return g_strdup (current_session);
}

static void
set_session (const gchar *session)
{
    gchar *last_session = NULL;
    GList *sessions = lightdm_get_sessions ();

    /* Validation */
    if (!session || !is_valid_session (sessions, session))
    {
        /* previous session */
        last_session = config_get_string (STATE_SECTION_GREETER, STATE_KEY_LAST_SESSION, NULL);
        if (last_session && g_strcmp0 (session, last_session) != 0 &&
            is_valid_session (sessions, last_session))
            session = last_session;
        else
        {
            /* default */
            const gchar* default_session = lightdm_greeter_get_default_session_hint (greeter);
            if (g_strcmp0 (session, default_session) != 0 &&
                is_valid_session (sessions, default_session))
                session = default_session;
            /* first in the sessions list */
            else if (sessions)
                session = lightdm_session_get_key (sessions->data);
            /* give up */
            else
                session = NULL;
        }
    }

    g_free (current_session);
    current_session = g_strdup (session);
    g_free (last_session);
}

/* Session language */

static gchar*
get_language (void)
{
    /* if the user manually selected a language, use it */
    if (current_language)
        return g_strdup (current_language);

    return NULL;
}

static void
set_language (const gchar *language)
{
    const gchar *default_language = NULL;

    /* If failed to find this language, then try the default */
    if (lightdm_get_language ())
    {
        default_language = lightdm_language_get_code (lightdm_get_language ());
    }

    if (default_language && g_strcmp0 (default_language, language) != 0)
        set_language (default_language);
}

/* Pending questions */
static void
pam_message_finalize (PAMConversationMessage *message)
{
    g_free (message->text);
    g_free (message);
}

static void
process_prompts (LightDMGreeter *greeter)
{
    if (!pending_questions) {
        return;
    }

    /* always allow the user to change username again */
    gtk_widget_set_sensitive (GTK_WIDGET (login_win_username_entry), TRUE);
    gtk_widget_set_sensitive (GTK_WIDGET (login_win_pw_entry), TRUE);

    if (changing_password) {
        gtk_widget_set_sensitive (GTK_WIDGET (pw_set_win_prompt_entry), TRUE);
        gtk_widget_set_sensitive (GTK_WIDGET (pw_set_win_ok_button), TRUE);
    }

    /* Special case: no user selected from list, so PAM asks us for the user
     * via a prompt. For that case, use the username field */
    if (!prompted && pending_questions && !pending_questions->next &&
        ((PAMConversationMessage *) pending_questions->data)->is_prompt &&
        ((PAMConversationMessage *) pending_questions->data)->type.prompt != LIGHTDM_PROMPT_TYPE_SECRET &&
        gtk_widget_get_visible ((GTK_WIDGET (login_win_username_entry))) &&
        lightdm_greeter_get_authentication_user (greeter) == NULL)
    {
        prompted = TRUE;
        prompt_active = TRUE;
        gtk_widget_grab_focus (GTK_WIDGET (login_win_username_entry));
        gtk_widget_show (GTK_WIDGET (login_win_pw_entry));
        return;
    }

    while (pending_questions)
    {
        PAMConversationMessage *message = (PAMConversationMessage *) pending_questions->data;
        pending_questions = g_slist_remove (pending_questions, (gconstpointer) message);

        const gchar *filter_msg1 = "You are required to change your password immediately";
        const gchar *filter_msg2 = _("You are required to change your password immediately");
        const gchar *filter_msg3 = "Temporary Password";
        const gchar *filter_msg4 = "Until Password Expiration";
        if ((strstr (message->text, filter_msg1) != NULL) ||
             strstr (message->text, filter_msg2) != NULL) {
            changing_password = TRUE;
            show_ask_window (_("Password Expiration"),
                _("Your password has expired.\nPlease change your password immediately."),
                _("Changing Password"), _("Cancel"), "password_expiration");
            continue;
        } else if (strstr (message->text, filter_msg3) != NULL) {
            changing_password = TRUE;
            show_ask_window (_("Temporary Password Warning"),
                _("Your password has been issued temporarily.\nFor security reasons, please change your password immediately."),
                _("Changing Password"), _("Cancel"), "password_expiration");
            continue;
        } else if (strstr (message->text, filter_msg4) != NULL) {
            gchar *msg = NULL;
            gchar **tokens = g_strsplit (message->text, ":", -1);
            if (tokens[1]) {
                int daysleft = atoi (tokens[1]);
                if (daysleft) {
                    msg = g_strdup_printf (_("Your password will expire in %d day\nDo you want to change password now?"), daysleft);
                } else {
                    msg = g_strdup_printf (_("Your password will expire in %d days\nDo you want to change password now?"), daysleft);
                }
            } else {
                msg = g_strdup (_("Your password will expire soon\nDo you want to change password now?"));
            }
            g_strfreev (tokens);

            show_ask_window (_("Password Expiration Warning"), msg,
                             _("Changing Password"), _("Cancel"), "ask_chpasswd");
            g_free (msg);

            continue;
        }

        if (!message->is_prompt)
        {
            /* FIXME: this doesn't show multiple messages, but that was
             * already the case before. */
            if (changing_password) {
                gtk_label_set_text (pw_set_win_msg_label, message->text);
            } else {
                set_message_label (message->type.message, message->text);
            }
            continue;
        }

        if (changing_password) {
            const gchar *title;
            const gchar *prompt_label;
            if ((strstr (message->text, _("(current) UNIX password")) != 0) ||
                (strstr (message->text, "(current) UNIX password") != 0) ||
                (strstr (message->text, "Enter current password") != 0)) {
                title = _("Changing Password - [Step 1]");
                prompt_label = _("Enter current password :");
            } else if ((strstr (message->text, _("Enter new UNIX password")) != 0) ||
                       (strstr (message->text, "Enter new UNIX password") != 0) ||
                       (strstr (message->text, "Enter new password") != 0)) {
                title = _("Changing Password - [Step 2]");
                prompt_label = _("Enter new password :");
            } else if ((strstr (message->text, _("Retype new UNIX password")) != 0) ||
                       (strstr (message->text, "Retype new UNIX password") != 0) ||
                       (strstr (message->text, "Retype new password") != 0)) {
                title = _("Changing Password - [Step 3]");
                prompt_label = _("Retype new password :");
            } else {
                title = NULL;
                prompt_label = NULL;
            }
            gtk_label_set_text (pw_set_win_title_label, (title != NULL) ? title : "");
            gtk_label_set_text (pw_set_win_prompt_label, (prompt_label != NULL) ? prompt_label : "");
            gtk_entry_set_text (pw_set_win_prompt_entry, "");
            gtk_widget_grab_focus (GTK_WIDGET (pw_set_win_prompt_entry));
        } else {
            gtk_widget_show (GTK_WIDGET (login_win_pw_entry));
            gtk_widget_grab_focus (GTK_WIDGET (login_win_pw_entry));
            gtk_entry_set_text (login_win_pw_entry, "");
            gtk_entry_set_visibility (login_win_pw_entry, message->type.prompt != LIGHTDM_PROMPT_TYPE_SECRET);
#if 0
            if (message_label_is_empty () && password_prompted)
            {
                /* No message was provided beforehand and this is not the
                 * first password prompt, so use the prompt as label,
                 * otherwise the user will be completely unclear of what
                 * is going on. Actually, the fact that prompt messages are
                 * not shown is problematic in general, especially if
                 * somebody uses a custom PAM module that wants to ask
                 * something different. */
                gchar *str = message->text;
                if (g_str_has_suffix (str, ": "))
                    str = g_strndup (str, strlen (str) - 2);
                else if (g_str_has_suffix (str, ":"))
                    str = g_strndup (str, strlen (str) - 1);
                set_message_label (LIGHTDM_MESSAGE_TYPE_INFO, str);
                if (str != message->text)
                    g_free (str);
            }
#endif
            gtk_widget_grab_focus (GTK_WIDGET (login_win_pw_entry));
        }

        prompted = TRUE;
        password_prompted = TRUE;
        prompt_active = TRUE;

        /* If we have more stuff after a prompt, assume that other prompts are pending,
         * so stop here. */
        break;
    }
}

static void
start_authentication (const gchar *username)
{
    prompted = FALSE;
    password_prompted = FALSE;
    prompt_active = FALSE;

    if (pending_questions)
    {
        g_slist_free_full (pending_questions, (GDestroyNotify) pam_message_finalize);
        pending_questions = NULL;
    }

    config_set_string (STATE_SECTION_GREETER, STATE_KEY_LAST_USER, username);

    if (g_strcmp0 (username, "*other") == 0)
    {
        gtk_widget_show (GTK_WIDGET (login_win_username_entry));
#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
        lightdm_greeter_authenticate (greeter, NULL, NULL);
#else
        lightdm_greeter_authenticate (greeter, NULL);
#endif
    }
    else if (g_strcmp0 (username, "*guest") == 0)
    {
#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
        lightdm_greeter_authenticate_as_guest (greeter, NULL);
#else
        lightdm_greeter_authenticate_as_guest (greeter);
#endif
    }
    else
    {
        LightDMUser *user;

        user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);
        if (user)
        {
            if (!current_session)
                set_session (lightdm_user_get_session (user));
            if (!current_language)
                set_language (lightdm_user_get_language (user));
        }
        else
        {
            set_session (NULL);
            set_language (NULL);
        }
#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
        lightdm_greeter_authenticate (greeter, username, NULL);
#else
        lightdm_greeter_authenticate (greeter, username);
#endif
    }
}

static void
start_session (void)
{
    gchar *language;
    gchar *session;

    language = get_language ();
    if (language)
#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
        lightdm_greeter_set_language (greeter, language, NULL);
#else
        lightdm_greeter_set_language (greeter, language);
#endif
    g_free (language);

    session = get_session ();

    /* Remember last choice */
    config_set_string (STATE_SECTION_GREETER, STATE_KEY_LAST_SESSION, session);

    greeter_background_save_xroot (greeter_background);

    if (!lightdm_greeter_start_session_sync (greeter, session, NULL))
    {
        set_message_label (LIGHTDM_MESSAGE_TYPE_ERROR, _("Failed to start session"));
        start_authentication (lightdm_greeter_get_authentication_user (greeter));
    }
    g_free (session);
}

gboolean
login_win_username_entry_focus_out_cb (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    if (!g_strcmp0 (gtk_entry_get_text (login_win_username_entry), "") == 0)
        start_authentication (gtk_entry_get_text (login_win_username_entry));

    return FALSE;
}

gboolean
login_win_username_entry_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    /* Acts as login_win_pw_entry */
    if (event->keyval == GDK_KEY_Up)
        return login_win_pw_entry_key_press_cb (widget, event, user_data);
    /* Enter activates the password entry */
    else if (event->keyval == GDK_KEY_Return && gtk_widget_get_visible (GTK_WIDGET (login_win_pw_entry)))
    {
        gtk_widget_grab_focus (GTK_WIDGET (login_win_pw_entry));
        return TRUE;
    }
    else
        return FALSE;
}

void
login_win_login_button_clicked_cb (GtkButton *button, gpointer user_data)
{
    /* Reset to default screensaver values */
    if (lightdm_greeter_get_lock_hint (greeter))
        XSetScreenSaver (gdk_x11_display_get_xdisplay (gdk_display_get_default ()), timeout, interval, prefer_blanking, allow_exposures);

    gtk_widget_set_sensitive (GTK_WIDGET (login_win_username_entry), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (login_win_pw_entry), FALSE);
    set_message_label (LIGHTDM_MESSAGE_TYPE_INFO, NULL);
    prompt_active = FALSE;

    if (lightdm_greeter_get_is_authenticated (greeter))
        start_session ();
    else if (lightdm_greeter_get_in_authentication (greeter))
    {
#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
        lightdm_greeter_respond (greeter, gtk_entry_get_text (login_win_pw_entry), NULL);
#else
        lightdm_greeter_respond (greeter, gtk_entry_get_text (login_win_pw_entry));
#endif
        /* If we have questions pending, then we continue processing
         * those, until we are done. (Otherwise, authentication will
         * not complete.) */
        if (pending_questions)
            process_prompts (greeter);
    }
    else
        start_authentication (lightdm_greeter_get_authentication_user (greeter));
}

void
login_win_pw_entry_activate_cb (GtkWidget *widget)
{
    login_win_login_button_clicked_cb (login_win_login_button, NULL);
}

gboolean
login_win_pw_entry_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    if ((event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_Down))
    {
        /* Back to login_win_username_entry if it is available */
        if (event->keyval == GDK_KEY_Up &&
            gtk_widget_get_visible (GTK_WIDGET (login_win_username_entry)) && widget == GTK_WIDGET (login_win_pw_entry))
        {
            gtk_widget_grab_focus (GTK_WIDGET (login_win_username_entry));
            return TRUE;
        }

        return TRUE;
    }
    return FALSE;
}

void
pw_set_win_prompt_entry_changed_cb (GtkEditable *editable,
                                    gchar       *new_text,
                                    gint         new_text_length,
                                    gpointer     position,
                                    gpointer     user_data)
{
    if (new_text_length > 0)
        gtk_label_set_text (pw_set_win_msg_label, "");
}

void
pw_set_win_button_clicked_cb (GtkButton *button, gpointer user_data)
{
    if (button == pw_set_win_cancel_button) {
        changing_password = FALSE;
        gtk_label_set_text (pw_set_win_msg_label, "");
        gtk_entry_set_text (pw_set_win_prompt_entry, "");
        show_login_window ();
        start_authentication (lightdm_greeter_get_authentication_user (greeter));
        return;
    }

    if (button == pw_set_win_ok_button) {
        gtk_widget_set_sensitive (GTK_WIDGET (pw_set_win_prompt_entry), FALSE);
        gtk_widget_set_sensitive (GTK_WIDGET (pw_set_win_ok_button), FALSE);
        prompt_active = FALSE;

        if (lightdm_greeter_get_in_authentication (greeter)) {
#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
            lightdm_greeter_respond (greeter, gtk_entry_get_text (pw_set_win_prompt_entry), NULL);
#else
            lightdm_greeter_respond (greeter, gtk_entry_get_text (pw_set_win_prompt_entry));
#endif
            /* If we have questions pending, then we continue processing
             * those, until we are done. (Otherwise, authentication will
             * not complete.) */
            if (pending_questions)
                process_prompts (greeter);
        }
    }
}

void
pw_set_win_prompt_entry_activate_cb (GtkWidget *widget)
{
    pw_set_win_button_clicked_cb (pw_set_win_ok_button, NULL);
}

gboolean
pw_set_win_key_press_event_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    if (event->keyval == GDK_KEY_Escape) {
        pw_set_win_button_clicked_cb (pw_set_win_cancel_button, NULL);
        return TRUE;
    }

    return FALSE;
}

void
cmd_win_button_clicked_cb (GtkButton *button, gpointer user_data)
{
    if (button == cmd_win_cancel_button) {
        gtk_widget_hide (cmd_win);
        gtk_widget_show (cur_show_win);

        g_timeout_add (50, grab_focus_cb, cur_focused_widget);

        return;
    }

    int type = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (cmd_win), CMD_WIN_DATA));

    if (button == cmd_win_ok_button) {
        switch (type)
        {
            case SYSTEM_SHUTDOWN:
                lightdm_shutdown (NULL);
                break;

            case SYSTEM_RESTART:
                lightdm_restart (NULL);
                break;

            case SYSTEM_SUSPEND:
                lightdm_suspend (NULL);
                break;

            case SYSTEM_HIBERNATE:
                lightdm_hibernate (NULL);
                break;

            default:
                return;
        }
    }
}

gboolean
cmd_win_key_press_event_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    if (event->keyval == GDK_KEY_Escape)
    {
        cmd_win_button_clicked_cb (cmd_win_cancel_button, NULL);
        return TRUE;
    }
    return FALSE;
}

void
ask_win_button_clicked_cb (GtkButton *button, gpointer user_data)
{
    const gchar *data = g_object_get_data (G_OBJECT (ask_win), ASK_WIN_DATA);

    if (button == ask_win_cancel_button) {
        if (g_strcmp0 (data, "ask_chpasswd") == 0) {
            if (lightdm_greeter_get_in_authentication (greeter)) {
                changing_password = FALSE;

#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
                lightdm_greeter_respond (greeter, "chpasswd_no", NULL);
#else
                lightdm_greeter_respond (greeter, "chpasswd_no");
#endif
                return;
            }
        }

        changing_password = FALSE;
        gtk_label_set_text (pw_set_win_msg_label, "");
        gtk_entry_set_text (pw_set_win_prompt_entry, "");
        show_login_window ();
        start_authentication (lightdm_greeter_get_authentication_user (greeter));

        return;
    }

    if (button == ask_win_ok_button) {
        if (g_strcmp0 (data, "password_expiration") == 0) {
            show_password_settings_window ();
        }

        if (g_strcmp0 (data, "ask_chpasswd") == 0) {
            gtk_label_set_text (pw_set_win_msg_label, "");
            gtk_entry_set_text (pw_set_win_prompt_entry, "");

            if (lightdm_greeter_get_in_authentication (greeter)) {
                changing_password = TRUE;

#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
                lightdm_greeter_respond (greeter, "chpasswd_yes", NULL);
#else
                lightdm_greeter_respond (greeter, "chpasswd_yes");
#endif
                show_password_settings_window ();
            } else {
                changing_password = FALSE;

                show_login_window ();
                start_authentication (lightdm_greeter_get_authentication_user (greeter));
            }
        }
    }
}

gboolean
ask_win_key_press_event_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    if (event->keyval == GDK_KEY_Escape) {
        ask_win_button_clicked_cb (ask_win_cancel_button, NULL);
        return TRUE;
    }

    return FALSE;
}

void
msg_win_button_clicked_cb (GtkButton *button, gpointer user_data)
{
    const gchar *data = g_object_get_data (G_OBJECT (msg_win), MSG_WIN_DATA);

    if (g_strcmp0 (data, "FAILURE_CHPASSWD") == 0) {
        changing_password = FALSE;
        gtk_label_set_text (pw_set_win_msg_label, "");
        gtk_entry_set_text (pw_set_win_prompt_entry, "");
        show_login_window ();
        start_authentication (lightdm_greeter_get_authentication_user (greeter));
    }
}

gboolean
msg_win_key_press_event_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    if (event->keyval == GDK_KEY_Escape) {
        msg_win_button_clicked_cb (msg_win_ok_button, NULL);
        return TRUE;
    }

    return FALSE;
}

static void
show_prompt_cb (LightDMGreeter *greeter, const gchar *text, LightDMPromptType type)
{
    PAMConversationMessage *message_obj = g_new (PAMConversationMessage, 1);
    if (message_obj)
    {
        message_obj->is_prompt = TRUE;
        message_obj->type.prompt = type;
        message_obj->text = g_strdup (text);
        pending_questions = g_slist_append (pending_questions, message_obj);
    }

    if (!prompt_active)
        process_prompts (greeter);
}

static void
show_message_cb (LightDMGreeter *greeter, const gchar *text, LightDMMessageType type)
{
    PAMConversationMessage *message_obj = g_new (PAMConversationMessage, 1);
    if (message_obj)
    {
        message_obj->is_prompt = FALSE;
        message_obj->type.message = type;
        message_obj->text = g_strdup (text);
        pending_questions = g_slist_append (pending_questions, message_obj);
    }

    if (!prompt_active)
        process_prompts (greeter);
}

static void
timed_autologin_cb (LightDMGreeter *greeter)
{
    /* Don't trigger autologin if user locks screen with light-locker (thanks to Andrew P.). */
    if (!lightdm_greeter_get_lock_hint (greeter))
    {
        if (lightdm_greeter_get_is_authenticated (greeter))
        {
            /* Configured autologin user may be already selected in user list. */
            if (lightdm_greeter_get_authentication_user (greeter))
                /* Selected user matches configured autologin-user option. */
                start_session ();
            else if (lightdm_greeter_get_autologin_guest_hint (greeter))
                /* "Guest session" is selected and autologin-guest is enabled. */
                start_session ();
            else if (lightdm_greeter_get_autologin_user_hint (greeter))
            {
                /* "Guest session" is selected, but autologin-user is configured. */
                start_authentication (lightdm_greeter_get_autologin_user_hint (greeter));
                prompted = TRUE;
            }
        }
        else
#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
            lightdm_greeter_authenticate_autologin (greeter, NULL);
#else
            lightdm_greeter_authenticate_autologin (greeter);
#endif
    }
}

static void
authentication_complete_cb (LightDMGreeter *greeter)
{
    prompt_active = FALSE;
    gtk_entry_set_text (login_win_pw_entry, "");

    if (pending_questions)
    {
        g_slist_free_full (pending_questions, (GDestroyNotify) pam_message_finalize);
        pending_questions = NULL;
    }

    if (lightdm_greeter_get_is_authenticated (greeter))
    {
        if (prompted)
            start_session ();
        else
        {
            gtk_widget_hide (GTK_WIDGET (login_win_pw_entry));
        }
    }
    else
    {
        /* If an error message is already printed we do not print it this statement
         * The error message probably comes from the PAM module that has a better knowledge
         * of the failure. */
        gboolean have_pam_error = !message_label_is_empty () &&
                                  gtk_info_bar_get_message_type (login_win_infobar) != GTK_MESSAGE_ERROR;
        if (prompted)
        {
            if (!have_pam_error)
                set_message_label (LIGHTDM_MESSAGE_TYPE_ERROR, _("Incorrect password, please try again"));
            start_authentication (lightdm_greeter_get_authentication_user (greeter));
        }
        else
        {
            g_warning ("Failed to authenticate");
            if (!have_pam_error)
                set_message_label (LIGHTDM_MESSAGE_TYPE_ERROR, _("Failed to authenticate"));
        }

        if (changing_password) {
            show_msg_window (_("Failure Of Changing Password"),
                             _("Failed to change password.\nPlease try again."),
                             _("Ok"),
                             "FAILURE_CHPASSWD");
        }
    }
}

static GdkFilterReturn
wm_window_filter (GdkXEvent *gxevent, GdkEvent *event, gpointer  data)
{
    XEvent *xevent = (XEvent*)gxevent;
    if (xevent->type == MapNotify)
    {
        GdkDisplay *display = gdk_x11_lookup_xdisplay (xevent->xmap.display);
        GdkWindow *win = gdk_x11_window_foreign_new_for_display (display, xevent->xmap.window);
        GdkWindowTypeHint win_type = gdk_window_get_type_hint (win);

        if (win_type != GDK_WINDOW_TYPE_HINT_COMBO &&
            win_type != GDK_WINDOW_TYPE_HINT_TOOLTIP &&
            win_type != GDK_WINDOW_TYPE_HINT_NOTIFICATION)
        /*
        if (win_type == GDK_WINDOW_TYPE_HINT_DESKTOP ||
            win_type == GDK_WINDOW_TYPE_HINT_DIALOG)
        */
            gdk_window_focus (win, GDK_CURRENT_TIME);
    }
    else if (xevent->type == UnmapNotify)
    {
        Window xwin;
        int revert_to = RevertToNone;

        XGetInputFocus (xevent->xunmap.display, &xwin, &revert_to);
        if (revert_to == RevertToNone)
            gdk_window_lower (gtk_widget_get_window (gtk_widget_get_toplevel (GTK_WIDGET (screen_overlay))));
    }

    return GDK_FILTER_CONTINUE;
}

int
main (int argc, char **argv)
{
    GtkBuilder *builder;
    const GList *items, *item;
    gchar *value;
    GtkCssProvider *css_provider;
    GError *error = NULL;
    int ret = EXIT_SUCCESS;

    /* Prevent memory from being swapped out, as we are dealing with passwords */
    mlockall (MCL_CURRENT | MCL_FUTURE);

    /* LP: #1024482 */
    g_setenv ("GDK_CORE_DEVICE_EVENTS", "1", TRUE);

    /* LP: #1366534 */
    g_setenv ("NO_AT_BRIDGE", "1", TRUE);

    /* Initialize i18n */
    setlocale (LC_ALL, "");
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    g_unix_signal_add (SIGTERM, (GSourceFunc)sigterm_cb, /* is_callback */ GINT_TO_POINTER (TRUE));

    config_init ();

    g_setenv ("GTK_MODULES", "atk-bridge", FALSE);

//    GPid pid = 0;
//    gchar **arr_cmd = NULL;
//    gchar *cmd = "/usr/lib/at-spi2-core/at-spi-bus-launcher --launch-immediately";
//
//    g_shell_parse_argv (cmd, NULL, &arr_cmd, NULL);
//
//    g_spawn_async (NULL, arr_cmd, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &pid, NULL);
//
//    g_strfreev (arr_cmd);
//    arr_cmd = NULL;

    /* init gtk */
    gtk_init (&argc, &argv);

    greeter = lightdm_greeter_new ();
    g_signal_connect (greeter, "show-prompt", G_CALLBACK (show_prompt_cb), NULL);
    g_signal_connect (greeter, "show-message", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (greeter, "authentication-complete", G_CALLBACK (authentication_complete_cb), NULL);
    g_signal_connect (greeter, "autologin-timer-expired", G_CALLBACK (timed_autologin_cb), NULL);
    if (!lightdm_greeter_connect_sync (greeter, NULL)) {
        ret = EXIT_FAILURE;
        goto done;
    }

    /* Set default cursor */
    gdk_window_set_cursor (gdk_get_default_root_window (), gdk_cursor_new_for_display (gdk_display_get_default (), GDK_LEFT_PTR));

    /* Make the greeter behave a bit more like a screensaver if used as un/lock-screen by blanking the screen */
    if (lightdm_greeter_get_lock_hint (greeter))
    {
        Display *display = gdk_x11_display_get_xdisplay (gdk_display_get_default ());
        XGetScreenSaver (display, &timeout, &interval, &prefer_blanking, &allow_exposures);
        XForceScreenSaver (display, ScreenSaverActive);
        XSetScreenSaver (display, config_get_int (NULL, CONFIG_KEY_SCREENSAVER_TIMEOUT, 60), 0,
                         ScreenSaverActive, DefaultExposures);
    }

    /* Set GTK+ settings */
    value = config_get_string (NULL, CONFIG_KEY_THEME, NULL);
    if (value)
    {
        g_debug ("[Configuration] Changing GTK+ theme to '%s'", value);
        g_object_set (gtk_settings_get_default (), "gtk-theme-name", value, NULL);
        g_free (value);
    }
    g_object_get (gtk_settings_get_default (), "gtk-theme-name", &default_theme_name, NULL);
    g_debug ("[Configuration] GTK+ theme: '%s'", default_theme_name);

    value = config_get_string (NULL, CONFIG_KEY_ICON_THEME, NULL);
    if (value)
    {
        g_debug ("[Configuration] Changing icons theme to '%s'", value);
        g_object_set (gtk_settings_get_default (), "gtk-icon-theme-name", value, NULL);
        g_free (value);
    }
    g_object_get (gtk_settings_get_default (), "gtk-icon-theme-name", &default_icon_theme_name, NULL);
    g_debug ("[Configuration] Icons theme: '%s'", default_icon_theme_name);

    value = config_get_string (NULL, CONFIG_KEY_FONT, "Sans 10");
    if (value)
    {
        g_debug ("[Configuration] Changing font to '%s'", value);
        g_object_set (gtk_settings_get_default (), "gtk-font-name", value, NULL);
        g_free (value);
    }
    g_object_get (gtk_settings_get_default (), "gtk-font-name", &default_font_name, NULL);
    g_debug ("[Configuration] Font: '%s'", default_font_name);

    if (config_has_key (NULL, CONFIG_KEY_DPI))
        g_object_set (gtk_settings_get_default (), "gtk-xft-dpi", 1024*config_get_int (NULL, CONFIG_KEY_DPI, 96), NULL);

    if (config_has_key (NULL, CONFIG_KEY_ANTIALIAS))
        g_object_set (gtk_settings_get_default (), "gtk-xft-antialias", config_get_bool (NULL, CONFIG_KEY_ANTIALIAS, FALSE), NULL);

    value = config_get_string (NULL, CONFIG_KEY_HINT_STYLE, NULL);
    if (value)
    {
        g_object_set (gtk_settings_get_default (), "gtk-xft-hintstyle", value, NULL);
        g_free (value);
    }

    value = config_get_string (NULL, CONFIG_KEY_RGBA, NULL);
    if (value)
    {
        g_object_set (gtk_settings_get_default (), "gtk-xft-rgba", value, NULL);
        g_free (value);
    }

    builder = gtk_builder_new ();
    if (!gtk_builder_add_from_string (builder, gooroom_greeter_ui, gooroom_greeter_ui_length, &error))
    {
        g_warning ("Error loading UI: %s", error->message);
        ret = EXIT_FAILURE;
        goto done;
    }
    g_clear_error (&error);

    /* Screen window */
    screen_overlay = GTK_OVERLAY (gtk_builder_get_object (builder, "screen_overlay"));

    /* Login window */
    login_win = GTK_WIDGET (gtk_builder_get_object (builder, "login_win"));
    login_win_login_image = GTK_WIDGET (gtk_builder_get_object (builder, "login_win_login_image"));
    login_win_shadow_image = GTK_WIDGET (gtk_builder_get_object (builder, "login_win_shadow_image"));
    login_win_username_box = GTK_WIDGET (gtk_builder_get_object (builder, "login_win_username_box"));
    login_win_username_label = GTK_LABEL (gtk_builder_get_object (builder, "login_win_username_label"));
    login_win_username_entry = GTK_ENTRY (gtk_builder_get_object (builder, "login_win_username_entry"));
    login_win_pw_entry = GTK_ENTRY (gtk_builder_get_object (builder, "login_win_pw_entry"));
    login_win_infobar = GTK_INFO_BAR (gtk_builder_get_object (builder, "login_win_infobar"));
    login_win_msg_label = GTK_LABEL (gtk_builder_get_object (builder, "login_win_msg_label"));
    login_win_login_button = GTK_BUTTON (gtk_builder_get_object (builder, "login_win_login_button"));

    /* Bottom panel */
    panel_box = GTK_WIDGET (gtk_builder_get_object (builder, "panel_box"));

    btn_shutdown = GTK_WIDGET (gtk_builder_get_object (builder, "btn_shutdown"));
    btn_restart = GTK_WIDGET (gtk_builder_get_object (builder, "btn_restart"));
    btn_suspend = GTK_WIDGET (gtk_builder_get_object (builder, "btn_suspend"));
    btn_hibernate = GTK_WIDGET (gtk_builder_get_object (builder, "btn_hibernate"));

    /* Right menu in panel */
    indicator_menubar = GTK_WIDGET (gtk_builder_get_object (builder, "indicator_menubar"));

    /* Power dialog */
    cmd_win = GTK_WIDGET (gtk_builder_get_object (builder, "cmd_win"));
    cmd_win_ok_button = GTK_BUTTON (gtk_builder_get_object (builder, "cmd_win_ok_button"));
    cmd_win_cancel_button = GTK_BUTTON (gtk_builder_get_object (builder, "cmd_win_cancel_button"));
    cmd_title_label = GTK_LABEL (gtk_builder_get_object (builder, "cmd_title_label"));
    cmd_msg_label = GTK_LABEL (gtk_builder_get_object (builder, "cmd_msg_label"));
    cmd_icon_image = GTK_IMAGE (gtk_builder_get_object (builder, "cmd_icon_image"));

    /* Password Settings Window */
    pw_set_win = GTK_WIDGET (gtk_builder_get_object (builder, "pw_set_win"));
    pw_set_win_ok_button = GTK_BUTTON (gtk_builder_get_object (builder, "pw_set_win_ok_button"));
    pw_set_win_cancel_button = GTK_BUTTON (gtk_builder_get_object (builder, "pw_set_win_cancel_button"));
    pw_set_win_title_label = GTK_LABEL (gtk_builder_get_object (builder, "pw_set_win_title_label"));
    pw_set_win_msg_label = GTK_LABEL (gtk_builder_get_object (builder, "pw_set_win_msg_label"));
    pw_set_win_prompt_label = GTK_LABEL (gtk_builder_get_object (builder, "pw_set_win_prompt_label"));
    pw_set_win_prompt_entry = GTK_ENTRY (gtk_builder_get_object (builder, "pw_set_win_prompt_entry"));

    /* Question Window */
    ask_win = GTK_WIDGET (gtk_builder_get_object (builder, "ask_win"));
    ask_win_ok_button = GTK_BUTTON (gtk_builder_get_object (builder, "ask_win_ok_button"));
    ask_win_cancel_button = GTK_BUTTON (gtk_builder_get_object (builder, "ask_win_cancel_button"));
    ask_win_title_label = GTK_LABEL (gtk_builder_get_object (builder, "ask_win_title_label"));
    ask_win_msg_label = GTK_LABEL (gtk_builder_get_object (builder, "ask_win_msg_label"));

    msg_win = GTK_WIDGET (gtk_builder_get_object (builder, "msg_win"));
    msg_win_ok_button = GTK_BUTTON (gtk_builder_get_object (builder, "msg_win_ok_button"));
    msg_win_title_label = GTK_LABEL (gtk_builder_get_object (builder, "msg_win_title_label"));
    msg_win_msg_label = GTK_LABEL (gtk_builder_get_object (builder, "msg_win_msg_label"));

    gtk_overlay_add_overlay (screen_overlay, login_win);
    gtk_overlay_add_overlay (screen_overlay, cmd_win);
    gtk_overlay_add_overlay (screen_overlay, pw_set_win);
    gtk_overlay_add_overlay (screen_overlay, ask_win);
    gtk_overlay_add_overlay (screen_overlay, msg_win);
    gtk_overlay_add_overlay (screen_overlay, panel_box);

    clock_format = config_get_string (NULL, CONFIG_KEY_CLOCK_FORMAT, "%F        %p %I:%M");
    css_provider = gtk_css_provider_new ();
    gchar *css_path = g_build_filename (PKGDATA_DIR, "themes", "gooroom-greeter.css", NULL);
    gtk_css_provider_load_from_file (css_provider, g_file_new_for_path (css_path), NULL);
    g_free (css_path);

    gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
            GTK_STYLE_PROVIDER (css_provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    gchar *user_img_path = g_build_filename (PKGDATA_DIR, "images", "username.png", NULL);
    gchar *pass_img_path = g_build_filename (PKGDATA_DIR, "images", "password.png", NULL);
    gchar *symbol_img_path = g_build_filename (PKGDATA_DIR, "images", "gooroom-greeter.png", NULL);
    gchar *shadow_img_path = g_build_filename (PKGDATA_DIR, "images", "shadow.png", NULL);

    GdkPixbuf *user_pixbuf = gdk_pixbuf_new_from_file (user_img_path, NULL);
    GdkPixbuf *pass_pixbuf = gdk_pixbuf_new_from_file (pass_img_path, NULL);

    gtk_entry_set_icon_from_pixbuf (GTK_ENTRY (login_win_username_entry), GTK_ENTRY_ICON_PRIMARY, user_pixbuf);
    gtk_entry_set_icon_from_pixbuf (GTK_ENTRY (login_win_pw_entry), GTK_ENTRY_ICON_PRIMARY, pass_pixbuf);

    gtk_image_set_from_file (GTK_IMAGE (login_win_login_image), symbol_img_path);
    gtk_image_set_from_file (GTK_IMAGE (login_win_shadow_image), shadow_img_path);

    g_free (user_img_path);
    g_free (pass_img_path);
    g_free (symbol_img_path);
    g_free (shadow_img_path);

    g_object_unref (user_pixbuf);
    g_object_unref (pass_pixbuf);

    /* Background */
    greeter_background = greeter_background_new (GTK_WIDGET (screen_overlay));

    value = config_get_string (NULL, CONFIG_KEY_ACTIVE_MONITOR, NULL);
    greeter_background_set_active_monitor_config (greeter_background, value ? value : "#cursor");
    g_free (value);

    read_monitor_configuration (CONFIG_GROUP_DEFAULT, GREETER_BACKGROUND_DEFAULT);

    gchar **config_group;
    gchar **config_groups = config_get_groups (CONFIG_GROUP_MONITOR);
    for (config_group = config_groups; *config_group; ++config_group)
    {
        const gchar *name = *config_group + sizeof (CONFIG_GROUP_MONITOR);
        while (*name && g_ascii_isspace (*name))
            ++name;

        read_monitor_configuration (*config_group, name);
    }
    g_strfreev (config_groups);

    greeter_background_connect (greeter_background, gdk_screen_get_default ());

    const gchar *user_name;
    gboolean logged_in = FALSE;

    items = lightdm_user_list_get_users (lightdm_user_list_get_instance ());
    for (item = items; item; item = item->next)
    {
        LightDMUser *user = item->data;
        logged_in = lightdm_user_get_logged_in (user);
        if (logged_in) {
          user_name = lightdm_user_get_name (user);
          break;
        }
    }

    if (logged_in)
    {
        gtk_label_set_text (login_win_username_label, user_name);
        gtk_widget_show (GTK_WIDGET (login_win_username_box));
        gtk_widget_hide (GTK_WIDGET (login_win_username_entry));
        gtk_button_set_label (login_win_login_button, _("Unlock"));
        start_authentication (user_name);
    }
    else
    {
        gtk_label_set_text (login_win_username_label, NULL);
        gtk_widget_show (GTK_WIDGET (login_win_username_entry));
        gtk_widget_hide (GTK_WIDGET (login_win_username_box));
        gtk_button_set_label (login_win_login_button, _("Log In"));
        start_authentication ("*other");
    }

    /* Windows positions */
//    value = config_get_string (NULL, CONFIG_KEY_POSITION, NULL);
//    g_object_set_data_full (G_OBJECT (login_win), WINDOW_DATA_POSITION, str_to_position (value, &WINDOW_POS_CENTER), g_free);
//    g_free (value);

    gtk_widget_set_halign (login_win, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (login_win, GTK_ALIGN_CENTER);
    gtk_widget_set_halign (cmd_win, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (cmd_win, GTK_ALIGN_CENTER);
    gtk_widget_set_halign (pw_set_win, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (pw_set_win, GTK_ALIGN_CENTER);
    gtk_widget_set_halign (ask_win, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (ask_win, GTK_ALIGN_CENTER);
    gtk_widget_set_halign (msg_win, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (msg_win, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (panel_box, GTK_ALIGN_END);

    gtk_builder_connect_signals (builder, greeter);

    set_message_label (LIGHTDM_MESSAGE_TYPE_INFO, NULL);

    load_commands ();
    load_indicators ();

    /* There is no window manager, so we need to implement some of its functionality */
    GdkWindow* root_window = gdk_get_default_root_window ();
    gdk_window_set_events (root_window, gdk_window_get_events (root_window) | GDK_SUBSTRUCTURE_MASK);
    gdk_window_add_filter (root_window, wm_window_filter, NULL);

    gtk_widget_show (GTK_WIDGET (screen_overlay));

    cur_show_win = login_win;
    changing_password = FALSE;

    /* Start the indicator applications service */
    indicator_application_service_start ();
    network_indicator_application_start ();
    other_indicator_application_start ();

    g_debug ("Run Gtk loop...");
    gtk_main ();
    g_debug ("Gtk loop exits");

    sigterm_cb (/* is_callback */ GINT_TO_POINTER (FALSE));

done:
#if 0
    if (pid != 0) {
        kill (pid, SIGKILL);
        waitpid (pid, NULL, 0);
        pid = 0;
    }
#endif

    return ret;
}
