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

#define	COMMAND_MENUITEM_SPACING	6


static LightDMGreeter *greeter;

/* Screen window */
static GtkOverlay   *screen_overlay;

/* Login window */
static GtkWidget    *login_window;
static GtkWidget    *login_image, *login_shadow;
static GtkWidget    *username_box;
static GtkLabel     *username_label;
static GtkEntry     *username_entry, *password_entry;
static GtkLabel     *message_label;
static GtkInfoBar   *info_bar;
static GtkButton    *login_button;

/* Panel */
static GtkWidget    *panel_box;
static GtkWidget    *btn_shutdown, *btn_restart, *btn_suspend, *btn_hibernate;
static GtkWidget    *indicator_menubar;

/* Power window */
static GtkWidget    *power_window;
static GtkButton    *power_ok_button, *power_cancel_button;
static GtkLabel     *power_title, *power_text;
static GtkImage     *power_icon;


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


static const gchar *POWER_WINDOW_DATA_LOOP = "power-window-loop";           /* <GMainLoop*> */
static const gchar *POWER_WINDOW_DATA_RESPONSE = "power-window-response";   /* <GtkResponseType> */

static const gchar *WINDOW_DATA_POSITION = "window-position"; /* <WindowPosition*> */

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
static const WindowPosition WINDOW_POS_CENTER   = {.x = { 50, +1, TRUE,   0}, .y = { 50, +1, TRUE,   0}, .use_size = FALSE};



void     greeter_restore_focus(const gpointer saved_data);
gpointer greeter_save_focus(GtkWidget* widget);

void     power_button_clicked_cb (GtkButton *button, gpointer user_data);

gboolean power_window_key_press_event_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data);
gboolean screen_overlay_get_child_position_cb (GtkWidget *overlay, GtkWidget *widget, GdkRectangle *allocation, gpointer user_data);



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

static void
entry_added (IndicatorObject *io, IndicatorObjectEntry *entry, gpointer user_data)
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

    gdouble percentage;
    g_object_get (device, "percentage", &percentage, NULL);

    gchar *icon = "gooroom-greeter-battery-full-symbolic";
    if (percentage <= 75.0) {
        icon = "gooroom-greeter-battery-good-symbolic";
    }

    if (percentage <= 50.0) {
        icon = "gooroom-greeter-battery-low-symbolic";
    }

    if (percentage <= 25.0) {
        icon = "gooroom-greeter-battery-caution-symbolic";
    }

    gtk_image_set_from_icon_name (battery_image, icon, GTK_ICON_SIZE_BUTTON);
    gtk_image_set_pixel_size (battery_image, 16);
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
       gtk_container_add (GTK_CONTAINER (item), hbox);
       gtk_widget_show (hbox);
   
       GtkWidget *image = gtk_image_new_from_icon_name ("gooroom-greeter-battery-full-symbolic", GTK_ICON_SIZE_BUTTON);
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

/* Power window */
static gboolean
show_power_prompt (const gchar *action, const gchar* icon, const gchar* title, const gchar* message)
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
                                                    logged_in_users),
                                          logged_in_users);
        message = new_message = g_markup_printf_escaped ("<b>%s</b>\n%s", warning, message);
        g_free (warning);
    }

    gchar *dialog_name = g_strconcat (action, "_dialog", NULL);
    gchar *button_name = g_strconcat (action, "_button", NULL);

    gtk_widget_set_name (power_window, dialog_name);
    gtk_widget_set_name (GTK_WIDGET (power_ok_button), button_name);
    gtk_button_set_label (power_ok_button, title);
    gtk_label_set_label (power_title, title);
    gtk_label_set_markup (power_text, message);
    gtk_image_set_from_icon_name (power_icon, icon, GTK_ICON_SIZE_DIALOG);

    g_free (button_name);
    g_free (dialog_name);
    g_free (new_message);

    GMainLoop *loop = g_main_loop_new (NULL, FALSE);
    g_object_set_data (G_OBJECT (power_window), POWER_WINDOW_DATA_LOOP, loop);
    g_object_set_data (G_OBJECT (power_window), POWER_WINDOW_DATA_RESPONSE, GINT_TO_POINTER (GTK_RESPONSE_CANCEL));

    GtkWidget *focused = gtk_window_get_focus (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (screen_overlay))));

    gtk_widget_hide (login_window);
    gtk_widget_show (power_window);
    gtk_widget_grab_focus (GTK_WIDGET (power_ok_button));

    g_main_loop_run (loop);
    GtkResponseType response = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (power_window), POWER_WINDOW_DATA_RESPONSE));
    g_main_loop_unref (loop);

    gtk_widget_hide (power_window);
    gtk_widget_show (login_window);

    if (focused)
        gtk_widget_grab_focus (focused);

    return response == GTK_RESPONSE_YES;
}

static void
on_command_button_clicked_cb (GtkButton *button, gpointer user_data)
{
    const gchar *name = (const gchar *)user_data;

    if (g_strcmp0 (name, "shutdown") == 0) {
        if (show_power_prompt ("shutdown", "gooroom-greeter-shutdown-symbolic", _("System shutdown"),
                               _("Are you sure you want to close all programs and shut down the computer?")))
            lightdm_shutdown (NULL);
    } else if (g_strcmp0 (name, "restart") == 0) {
        if (show_power_prompt ("restart", "gooroom-greeter-restart-symbolic", _("System restart"),
                               _("Are you sure you want to close all programs and restart the computer?")))
            lightdm_restart (NULL);
    } else if (g_strcmp0 (name, "suspend") == 0) {
        if (show_power_prompt (name, "gooroom-greeter-suspend-symbolic", _("System suspend"),
                               _("Are you sure you want to suspend the computer?")))
            lightdm_suspend (NULL);
    } else if (g_strcmp0 (name, "hibernate") == 0) {
        if (show_power_prompt ("hibernate", "gooroom-greeter-hibernate-symbolic", _("System hibernate"),
                               _("Are you sure you want to hibernate the computer?")))
            lightdm_hibernate (NULL);
    } else {
        return;
    }
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

//    g_object_set_data_full (G_OBJECT (io), "io-name", g_strdup (name), g_free);

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
network_indicator_service_start (void)
{
    gchar **argv = NULL;
    const gchar *cmd = "systemctl --user start indicator-application";
    g_shell_parse_argv (cmd, NULL, &argv, NULL);

    g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);

    g_strfreev (argv);

	g_spawn_command_line_sync ("/usr/bin/gsettings set org.gnome.nm-applet disable-disconnected-notifications true", NULL, NULL, NULL, NULL);

	g_spawn_command_line_sync ("/usr/bin/gsettings set org.gnome.nm-applet disable-connected-notifications true", NULL, NULL, NULL, NULL);

    /* Make nm-applet hide items the user does not have permissions to interact with */
    g_setenv ("NM_APPLET_HIDE_POLICY_ITEMS", "1", TRUE);

    g_spawn_command_line_async ("nm-applet", NULL);
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

    g_signal_connect (G_OBJECT (btn_shutdown), "clicked", G_CALLBACK (on_command_button_clicked_cb), (gpointer)"shutdown");
    g_signal_connect (G_OBJECT (btn_restart), "clicked", G_CALLBACK (on_command_button_clicked_cb), (gpointer)"restart");
    g_signal_connect (G_OBJECT (btn_suspend), "clicked", G_CALLBACK (on_command_button_clicked_cb), (gpointer)"suspend");
    g_signal_connect (G_OBJECT (btn_hibernate), "clicked", G_CALLBACK (on_command_button_clicked_cb), (gpointer)"hibernate");
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
greeter_save_focus(GtkWidget* widget)
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
greeter_restore_focus(const gpointer saved_data)
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

void
power_button_clicked_cb (GtkButton *button, gpointer user_data)
{
    GMainLoop *loop = g_object_get_data (G_OBJECT (power_window), POWER_WINDOW_DATA_LOOP);
    if (g_main_loop_is_running (loop))
        g_main_loop_quit (loop);

    g_object_set_data (G_OBJECT (power_window), POWER_WINDOW_DATA_RESPONSE,
                       GINT_TO_POINTER (button == power_ok_button ? GTK_RESPONSE_YES : GTK_RESPONSE_CANCEL));
}

gboolean
power_window_key_press_event_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    if (event->keyval == GDK_KEY_Escape)
    {
        power_button_clicked_cb (power_cancel_button, NULL);
        return TRUE;
    }
    return FALSE;
}

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

gboolean
screen_overlay_get_child_position_cb (GtkWidget *overlay, GtkWidget *widget, GdkRectangle *allocation, gpointer user_data)
{
    const WindowPosition *pos = g_object_get_data (G_OBJECT (widget), WINDOW_DATA_POSITION);
    if (!pos)
        return FALSE;

    gint screen_width = gtk_widget_get_allocated_width (overlay);
    gint screen_height = gtk_widget_get_allocated_height (overlay);

    if (pos->use_size)
    {
        allocation->width = get_absolute_position (&pos->width, screen_width, 0);
        allocation->height = get_absolute_position (&pos->height, screen_height, 0);
    }
    else
    {
        gtk_widget_get_preferred_width (widget, NULL, &allocation->width);
        gtk_widget_get_preferred_height (widget, NULL, &allocation->height);
    }

    allocation->x = get_absolute_position (&pos->x, screen_width, allocation->width);
    allocation->y = get_absolute_position (&pos->y, screen_height, allocation->height);

    return TRUE;
}

/* Message label */
static gboolean
message_label_is_empty (void)
{
    return gtk_label_get_text (message_label)[0] == '\0';
}

static void
set_message_label (LightDMMessageType type, const gchar *text)
{
    if (type == LIGHTDM_MESSAGE_TYPE_INFO)
        gtk_info_bar_set_message_type (info_bar, GTK_MESSAGE_INFO);
    else
        gtk_info_bar_set_message_type (info_bar, GTK_MESSAGE_ERROR);

    const gchar *str = (text != NULL) ? text : "";
    gtk_label_set_text (message_label, str);
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
    if (!pending_questions)
        return;

    /* always allow the user to change username again */
    gtk_widget_set_sensitive (GTK_WIDGET (username_entry), TRUE);
    gtk_widget_set_sensitive (GTK_WIDGET (password_entry), TRUE);

    /* Special case: no user selected from list, so PAM asks us for the user
     * via a prompt. For that case, use the username field */
    if (!prompted && pending_questions && !pending_questions->next &&
        ((PAMConversationMessage *) pending_questions->data)->is_prompt &&
        ((PAMConversationMessage *) pending_questions->data)->type.prompt != LIGHTDM_PROMPT_TYPE_SECRET &&
        gtk_widget_get_visible ((GTK_WIDGET (username_entry))) &&
        lightdm_greeter_get_authentication_user (greeter) == NULL)
    {
        prompted = TRUE;
        prompt_active = TRUE;
        gtk_widget_grab_focus (GTK_WIDGET (username_entry));
        gtk_widget_show (GTK_WIDGET (password_entry));
        return;
    }

    while (pending_questions)
    {
        PAMConversationMessage *message = (PAMConversationMessage *) pending_questions->data;
        pending_questions = g_slist_remove (pending_questions, (gconstpointer) message);

        if (!message->is_prompt)
        {
            /* FIXME: this doesn't show multiple messages, but that was
             * already the case before. */
            set_message_label (message->type.message, message->text);
            continue;
        }

        gtk_widget_show (GTK_WIDGET (password_entry));
        gtk_widget_grab_focus (GTK_WIDGET (password_entry));
        gtk_entry_set_text (password_entry, "");
        gtk_entry_set_visibility (password_entry, message->type.prompt != LIGHTDM_PROMPT_TYPE_SECRET);
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
        gtk_widget_grab_focus (GTK_WIDGET (password_entry));
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
        gtk_widget_show (GTK_WIDGET (username_entry));
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
password_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data);
G_MODULE_EXPORT
gboolean
password_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    if ((event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_Down))
    {
        /* Back to username_entry if it is available */
        if (event->keyval == GDK_KEY_Up &&
            gtk_widget_get_visible (GTK_WIDGET (username_entry)) && widget == GTK_WIDGET (password_entry))
        {
            gtk_widget_grab_focus (GTK_WIDGET (username_entry));
            return TRUE;
        }

        return TRUE;
    }
    return FALSE;
}

gboolean
username_focus_out_cb (GtkWidget *widget, GdkEvent *event, gpointer user_data);
G_MODULE_EXPORT
gboolean
username_focus_out_cb (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    if (!g_strcmp0(gtk_entry_get_text (username_entry), "") == 0)
        start_authentication (gtk_entry_get_text (username_entry));
    return FALSE;
}

gboolean
username_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data);
G_MODULE_EXPORT
gboolean
username_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    /* Acts as password_entry */
    if (event->keyval == GDK_KEY_Up)
        return password_key_press_cb (widget, event, user_data);
    /* Enter activates the password entry */
    else if (event->keyval == GDK_KEY_Return && gtk_widget_get_visible (GTK_WIDGET (password_entry)))
    {
        gtk_widget_grab_focus (GTK_WIDGET (password_entry));
        return TRUE;
    }
    else
        return FALSE;
}

void login_cb (GtkWidget *widget);
G_MODULE_EXPORT
void
login_cb (GtkWidget *widget)
{
    /* Reset to default screensaver values */
    if (lightdm_greeter_get_lock_hint (greeter))
        XSetScreenSaver (gdk_x11_display_get_xdisplay (gdk_display_get_default ()), timeout, interval, prefer_blanking, allow_exposures);

    gtk_widget_set_sensitive (GTK_WIDGET (username_entry), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (password_entry), FALSE);
    set_message_label (LIGHTDM_MESSAGE_TYPE_INFO, NULL);
    prompt_active = FALSE;

    if (lightdm_greeter_get_is_authenticated (greeter))
        start_session ();
    else if (lightdm_greeter_get_in_authentication (greeter))
    {
#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
        lightdm_greeter_respond (greeter, gtk_entry_get_text (password_entry), NULL);
#else
        lightdm_greeter_respond (greeter, gtk_entry_get_text (password_entry));
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
    gtk_entry_set_text (password_entry, "");

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
            gtk_widget_hide (GTK_WIDGET (password_entry));
        }
    }
    else
    {
        /* If an error message is already printed we do not print it this statement
         * The error message probably comes from the PAM module that has a better knowledge
         * of the failure. */
        gboolean have_pam_error = !message_label_is_empty () &&
                                  gtk_info_bar_get_message_type (info_bar) != GTK_MESSAGE_ERROR;
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

    g_message ("Starting %s (%s, %s)", PACKAGE_STRING, __DATE__, __TIME__);

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

#if 0
    g_setenv ("GTK_MODULES", "atk-bridge", FALSE);

    GPid pid = 0;
    gchar **arr_cmd = NULL;
    gchar *cmd = "/usr/lib/at-spi2-core/at-spi-bus-launcher --launch-immediately";

    g_shell_parse_argv (cmd, NULL, &arr_cmd, NULL);

    g_spawn_async (NULL, arr_cmd, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &pid, NULL);

    g_strfreev (arr_cmd);
    arr_cmd = NULL;
#endif

    /* init gtk */
    gtk_init (&argc, &argv);

#if 1
    greeter = lightdm_greeter_new ();
    g_signal_connect (greeter, "show-prompt", G_CALLBACK (show_prompt_cb), NULL);
    g_signal_connect (greeter, "show-message", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (greeter, "authentication-complete", G_CALLBACK (authentication_complete_cb), NULL);
    g_signal_connect (greeter, "autologin-timer-expired", G_CALLBACK (timed_autologin_cb), NULL);
    if (!lightdm_greeter_connect_sync (greeter, NULL)) {
        ret = EXIT_FAILURE;
        goto done;
    }
#endif

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
    login_window = GTK_WIDGET (gtk_builder_get_object (builder, "login_window"));
    login_image = GTK_WIDGET (gtk_builder_get_object (builder, "login_image"));
    login_shadow = GTK_WIDGET (gtk_builder_get_object (builder, "login_shadow"));
    username_box = GTK_WIDGET (gtk_builder_get_object (builder, "username_box"));
    username_label = GTK_LABEL (gtk_builder_get_object (builder, "username_label"));
    username_entry = GTK_ENTRY (gtk_builder_get_object (builder, "username_entry"));
    password_entry = GTK_ENTRY (gtk_builder_get_object (builder, "password_entry"));
    info_bar = GTK_INFO_BAR (gtk_builder_get_object (builder, "greeter_infobar"));
    message_label = GTK_LABEL (gtk_builder_get_object (builder, "message_label"));
    login_button = GTK_BUTTON (gtk_builder_get_object (builder, "login_button"));

    /* Bottom panel */
    panel_box = GTK_WIDGET (gtk_builder_get_object (builder, "panel_box"));

    btn_shutdown = GTK_WIDGET (gtk_builder_get_object (builder, "btn_shutdown"));
    btn_restart = GTK_WIDGET (gtk_builder_get_object (builder, "btn_restart"));
    btn_suspend = GTK_WIDGET (gtk_builder_get_object (builder, "btn_suspend"));
    btn_hibernate = GTK_WIDGET (gtk_builder_get_object (builder, "btn_hibernate"));

    /* Right menu in panel */
    indicator_menubar = GTK_WIDGET (gtk_builder_get_object (builder, "indicator_menubar"));

    /* Power dialog */
    power_window = GTK_WIDGET (gtk_builder_get_object (builder, "power_window"));
    power_ok_button = GTK_BUTTON (gtk_builder_get_object (builder, "power_ok_button"));
    power_cancel_button = GTK_BUTTON (gtk_builder_get_object (builder, "power_cancel_button"));
    power_title = GTK_LABEL (gtk_builder_get_object (builder, "power_title"));
    power_text = GTK_LABEL (gtk_builder_get_object (builder, "power_text"));
    power_icon = GTK_IMAGE (gtk_builder_get_object (builder, "power_icon"));

    gtk_overlay_add_overlay (screen_overlay, login_window);
    gtk_overlay_add_overlay (screen_overlay, power_window);
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

    gtk_entry_set_icon_from_pixbuf (GTK_ENTRY (username_entry), GTK_ENTRY_ICON_PRIMARY, user_pixbuf);
    gtk_entry_set_icon_from_pixbuf (GTK_ENTRY (password_entry), GTK_ENTRY_ICON_PRIMARY, pass_pixbuf);

    gtk_image_set_from_file (GTK_IMAGE (login_image), symbol_img_path);
    gtk_image_set_from_file (GTK_IMAGE (login_shadow), shadow_img_path);

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
        gtk_label_set_text (username_label, user_name);
        gtk_widget_show (GTK_WIDGET (username_box));
        gtk_widget_hide (GTK_WIDGET (username_entry));
        gtk_button_set_label (login_button, _("Unlock"));
        start_authentication (user_name);
    }
    else
    {
        gtk_label_set_text (username_label, NULL);
        gtk_widget_show (GTK_WIDGET (username_entry));
        gtk_widget_hide (GTK_WIDGET (username_box));
        gtk_button_set_label (login_button, _("Log In"));
        start_authentication ("*other");
    }

    /* Windows positions */
    value = config_get_string (NULL, CONFIG_KEY_POSITION, NULL);
    g_object_set_data_full (G_OBJECT (login_window), WINDOW_DATA_POSITION, str_to_position (value, &WINDOW_POS_CENTER), g_free);
    g_free (value);

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

    /* Start the indicator services */
    network_indicator_service_start ();

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
