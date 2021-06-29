/*
 * Copyright (C) 2010 - 2011, Robert Ancell <robert.ancell@canonical.com>
 * Copyright (C) 2011, Gunnar Hjalmarsson <ubuntu@gunnar.cc>
 * Copyright (C) 2012 - 2013, Lionel Le Folgoc <mrpouit@ubuntu.com>
 * Copyright (C) 2012, Julien Lavergne <gilir@ubuntu.com>
 * Copyright (C) 2013 - 2015, Simon Steinbeiß <ochosi@shimmerproject.org>
 * Copyright (C) 2013 - 2018, Sean Davis <smd.seandavis@gmail.com>
 * Copyright (C) 2014, Andrew P. <pan.pav.7c5@gmail.com>
 * Copyright (C) 2015 - 2021 Gooroom <gooroom@gooroom.kr>
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


#include <glib.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <glib-unix.h>
#include <signal.h>


#include "greeter-window.h"
#include "greeterbackground.h"
#include "greeterconfiguration.h"


static GtkWidget *greeter_window = NULL;
static GreeterBackground *greeter_background = NULL;

struct SavedFocusData
{
	GtkWidget *widget;
	gint editable_pos;
};

gpointer greeter_save_focus    (GtkWidget* widget);
void     greeter_restore_focus (const gpointer saved_data);



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

static void
sigterm_cb (gpointer user_data)
{
	gboolean is_callback = GPOINTER_TO_INT (user_data);

	if (is_callback)
		g_debug ("SIGTERM received");

	if (is_callback) {
		gtk_main_quit ();
#ifdef KILL_ON_SIGTERM
		/* LP: #1445461 */
		g_debug ("Killing greeter with exit()...");
		exit (EXIT_SUCCESS);
#endif
	}
}

static void
dbus_update_activation_environment (void)
{
	gchar **argv = NULL;
	const gchar *cmd = "/usr/bin/dbus-update-activation-environment --systemd DBUS_SESSION_BUS_ADDRESS DISPLAY XAUTHORITY";
//	const gchar *cmd = "/usr/bin/dbus-update-activation-environment --systemd -all";

	g_shell_parse_argv (cmd, NULL, &argv, NULL);

	g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);

	g_strfreev (argv);
}

static void
notify_service_start (void)
{
	GSettings *settings;
	gchar **argv = NULL, **envp = NULL;

	settings = g_settings_new ("apps.gooroom-notifyd");
	g_settings_set_uint (settings, "notify-location", 2);
	g_settings_set_boolean (settings, "do-not-disturb", TRUE);

	g_shell_parse_argv (GOOROOM_NOTIFYD, NULL, &argv, NULL);

	envp = g_get_environ ();

	g_spawn_async (NULL, argv, envp, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);

	g_strfreev (argv);
	g_strfreev (envp);
	g_object_unref (settings);
}

static void
indicator_application_service_start (void)
{
	gchar **argv = NULL, **envp = NULL;
	const gchar *cmd = "systemctl --user start ayatana-indicator-application";

	g_shell_parse_argv (cmd, NULL, &argv, NULL);

	envp = g_get_environ ();

	g_spawn_async (NULL, argv, envp, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);

	g_strfreev (argv);
}

static void
wm_start (void)
{
	gchar **argv = NULL, **envp = NULL;
	const gchar *cmd = "/usr/bin/metacity";

	GSettings *settings = g_settings_new ("org.gnome.desktop.wm.preferences");
	g_settings_set_enum (settings, "action-right-click-titlebar", 5);
	g_object_unref (settings);

	g_shell_parse_argv (cmd, NULL, &argv, NULL);

	envp = g_get_environ ();

	g_spawn_async (NULL, argv, envp, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);

	g_strfreev (argv);
	g_strfreev (envp);
}

static void
gf_start (void)
{
	gchar **argv = NULL, **envp = NULL;
	const gchar *cmd = "/usr/bin/gnome-flashback";

	GSettings *settings = g_settings_new ("org.gnome.gnome-flashback");
	g_settings_set_boolean (settings, "a11y-keyboard", FALSE);
	g_settings_set_boolean (settings, "audio-device-selection", FALSE);
	g_settings_set_boolean (settings, "automount-manager", FALSE);
	g_settings_set_boolean (settings, "clipboard", FALSE);
	g_settings_set_boolean (settings, "desktop", FALSE);
	g_settings_set_boolean (settings, "end-session-dialog", FALSE);
	g_settings_set_boolean (settings, "idle-monitor", FALSE);
	g_settings_set_boolean (settings, "input-settings", FALSE);
	g_settings_set_boolean (settings, "input-sources", FALSE);
	g_settings_set_boolean (settings, "notifications", FALSE);
	g_settings_set_boolean (settings, "polkit", FALSE);
	g_settings_set_boolean (settings, "root-background", FALSE);
	g_settings_set_boolean (settings, "screencast", FALSE);
	g_settings_set_boolean (settings, "screensaver", FALSE);
	g_settings_set_boolean (settings, "screenshot", FALSE);
	g_settings_set_boolean (settings, "shell", FALSE);
	g_settings_set_boolean (settings, "status-notifier-watcher", FALSE);
	g_object_unref (settings);

	g_shell_parse_argv (cmd, NULL, &argv, NULL);

	envp = g_get_environ ();

	g_spawn_async (NULL, argv, envp, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);

	g_strfreev (argv);
	g_strfreev (envp);
}

static void
apply_gtk_config (void)
{
	GError *error = NULL;
	GKeyFile *keyfile = NULL;
	gchar *gtk_settings_ini = NULL;
	gchar *gtk_settings_dir = NULL;

	gtk_settings_dir = g_build_filename (g_get_user_config_dir (), "gtk-3.0", NULL);
	if (g_mkdir_with_parents (gtk_settings_dir, 0775) < 0) {
		g_warning ("Failed to create directory %s", gtk_settings_dir);
		g_free (gtk_settings_dir);
		return;
	}

	gtk_settings_ini = g_build_filename (gtk_settings_dir, "settings.ini", NULL);

	keyfile = g_key_file_new ();

	if (error == NULL)
	{
		gchar *value;

		/* Set GTK+ settings */
		value = config_get_string (NULL, CONFIG_KEY_THEME, NULL);
		if (value)
		{
			g_key_file_set_string (keyfile, "Settings", "gtk-theme-name", value);
			g_free (value);
		}

		value = config_get_string (NULL, CONFIG_KEY_ICON_THEME, NULL);
		if (value)
		{
			g_key_file_set_string (keyfile, "Settings", "gtk-icon-theme-name", value);
			g_free (value);
		}

		value = config_get_string (NULL, CONFIG_KEY_FONT, "Sans 10");
		if (value)
		{
			g_key_file_set_string (keyfile, "Settings", "gtk-font-name", value);
			g_free (value);
		}

		if (config_has_key (NULL, CONFIG_KEY_DPI))
		{
			gint dpi = 1024 * config_get_int (NULL, CONFIG_KEY_DPI, 96);
			g_key_file_set_integer (keyfile, "Settings", "gtk-xft-dpi", dpi);
		}

		if (config_has_key (NULL, CONFIG_KEY_ANTIALIAS))
		{
			gboolean antialias = config_get_bool (NULL, CONFIG_KEY_ANTIALIAS, FALSE);
			g_key_file_set_boolean (keyfile, "Settings", "gtk-xft-antialias", antialias);
		}

		value = config_get_string (NULL, CONFIG_KEY_HINT_STYLE, NULL);
		if (value)
		{
			g_key_file_set_string (keyfile, "Settings", "gtk-xft-hintstyle", value);
			g_free (value);
		}

		value = config_get_string (NULL, CONFIG_KEY_RGBA, NULL);
		if (value)
		{
			g_key_file_set_string (keyfile, "Settings", "gtk-xft-rgba", value);
			g_free (value);
		}
		g_key_file_save_to_file (keyfile, gtk_settings_ini, NULL);
	} else {
		g_error_free (error);
	}

	g_free (gtk_settings_dir);
	g_free (gtk_settings_ini);
	g_key_file_free (keyfile);
}

//static gboolean
//monitors_changed_idle_cb (gpointer user_data)
//{
//	GdkDisplay *display;
//	GdkRectangle geometry;
//	gint m, monitors = 0, real_monitor_num = 0;
//	cairo_region_t *region;
//
//	region = cairo_region_create ();
//	display = gdk_display_get_default ();
//	monitors = gdk_display_get_n_monitors (display);
//
//	for (m = 0; m < monitors; m++) {
//		if (cairo_region_contains_rectangle (region, &geometry) == CAIRO_REGION_OVERLAP_IN) {
//			continue;
//		}
//
//		cairo_region_union_rectangle (region, &geometry);
//		real_monitor_num++;
//	}
//
//	if (greeter_window)
//		greeter_window_set_switch_indicator_visible (greeter_window, real_monitor_num > 1);
//
//	return FALSE;
//}

//static void
//monitors_changed_cb (GdkScreen *screen, gpointer user_data)
//{
//	g_timeout_add (300, (GSourceFunc)monitors_changed_idle_cb, user_data);
//}

static gboolean
active_monitor_changed_idle_cb (gpointer user_data)
{
	GdkDisplay *display;
	GdkRectangle geometry;
	gint m, monitors = 0, real_monitor_num = 0;
	cairo_region_t *region;

	region = cairo_region_create ();
	display = gdk_display_get_default ();
	monitors = gdk_display_get_n_monitors (display);

	for (m = 0; m < monitors; m++) {
		GdkMonitor *monitor = gdk_display_get_monitor (display, m);
		gdk_monitor_get_geometry (monitor, &geometry);

		if (cairo_region_contains_rectangle (region, &geometry) == CAIRO_REGION_OVERLAP_IN)
			continue;

		cairo_region_union_rectangle (region, &geometry);
		real_monitor_num++;
	}

	if (greeter_window)
		greeter_window_set_switch_indicator_visible (GREETER_WINDOW (greeter_window),
                                                     real_monitor_num > 1);

	return FALSE;
}

static void
active_monitor_changed_cb (GreeterBackground *background, gpointer user_data)
{
	g_timeout_add (300, (GSourceFunc) active_monitor_changed_idle_cb, NULL);
}

static void
greeter_window_active_monitor_changed_cb (GreeterWindow *window,
                                          GdkRectangle  *geometry,
                                          gpointer       user_data)
{
	if (greeter_background)
		greeter_background_set_active_monitor_from_geometry (greeter_background, geometry);
}


int
main (int argc, char **argv)
{
	int ret = EXIT_SUCCESS;
	GdkScreen *screen = NULL;
	gchar *background = NULL;
//	gulong monitors_changed_id = 0;
	GtkCssProvider *provider = NULL;

	/* LP: #1024482 */
	g_setenv ("GDK_CORE_DEVICE_EVENTS", "1", TRUE);
	g_setenv ("GTK_MODULES", "atk-bridge", FALSE);

	/* Make nm-applet hide items the user does not have permissions to interact with */
	g_setenv ("NM_APPLET_HIDE_POLICY_ITEMS", "1", TRUE);

	dbus_update_activation_environment ();

	g_unix_signal_add (SIGTERM, (GSourceFunc)sigterm_cb, /* is_callback */ GINT_TO_POINTER (TRUE));

	/* Initialize i18n */
	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* init gtk */
	gtk_init (&argc, &argv);

	config_init ();
	apply_gtk_config ();

	/* Starting window manager */
	wm_start ();

	/* Starting gnome-flashback */
	gf_start ();

	notify_service_start ();
	indicator_application_service_start ();

	screen = gdk_screen_get_default ();

	/* Set default cursor */
	gdk_window_set_cursor (gdk_get_default_root_window (),
                           gdk_cursor_new_for_display (gdk_display_get_default (),
                           GDK_LEFT_PTR));

	greeter_window = greeter_window_new ();

	greeter_background = greeter_background_new (greeter_window);
	background = config_get_string (CONFIG_GROUP_DEFAULT, CONFIG_KEY_BACKGROUND, NULL);
	greeter_background_set_monitor_config (greeter_background, background);
	greeter_background_connect (greeter_background, screen);
	g_free (background);

	provider = gtk_css_provider_new ();
	gtk_css_provider_load_from_resource (provider, "/kr/gooroom/greeter/theme.css");
	gtk_style_context_add_provider_for_screen (screen,
                                               GTK_STYLE_PROVIDER (provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	g_clear_object (&provider);

	gtk_widget_show (greeter_window);

	active_monitor_changed_cb (greeter_background, NULL);
	g_signal_connect (G_OBJECT (greeter_background), "active-monitor-changed",
                      G_CALLBACK (active_monitor_changed_cb), NULL);

	g_signal_connect (greeter_window, "position-changed",
                      G_CALLBACK (greeter_window_active_monitor_changed_cb), NULL);


//	monitors_changed_cb (screen, NULL);
//	g_signal_connect (G_OBJECT (screen), "monitors-changed",
//                    G_CALLBACK (monitors_changed_cb), NULL);

	gtk_main ();

	sigterm_cb (GINT_TO_POINTER (FALSE));

	return ret;
}
