/*
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

#include "splash-window.h"


struct _SplashWindowPrivate
{
	GtkWidget *spinner;
};

G_DEFINE_TYPE_WITH_PRIVATE (SplashWindow, splash_window, GTK_TYPE_WINDOW);


static void
splash_window_finalize (GObject *object)
{
	G_OBJECT_CLASS (splash_window_parent_class)->finalize (object);
}

static void
splash_window_init (SplashWindow *window)
{
	window->priv = splash_window_get_instance_private (window);

	gtk_widget_init_template (GTK_WIDGET (window));

	gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
	gtk_window_set_skip_taskbar_hint (GTK_WINDOW (window), TRUE);
	gtk_window_set_skip_pager_hint (GTK_WINDOW (window), TRUE);
	gtk_widget_set_app_paintable (GTK_WIDGET (window), TRUE);

	GdkScreen *screen = gtk_window_get_screen (GTK_WINDOW (window));
	if (gdk_screen_is_composited (screen)) {
		GdkVisual *visual = gdk_screen_get_rgba_visual (screen);
		if (visual == NULL)
			visual = gdk_screen_get_system_visual (screen);

		gtk_widget_set_visual (GTK_WIDGET(window), visual);
	}
}

static void
splash_window_class_init (SplashWindowClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass),
                                                 "/kr/gooroom/greeter/splash-window.ui");

	object_class->finalize = splash_window_finalize;

	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), SplashWindow, spinner);
}


SplashWindow *
splash_window_new (GtkWindow *parent)
{
	GObject *result;

	result = g_object_new (SPLASH_TYPE_WINDOW,
                           "transient-for", parent,
                           NULL);

	return SPLASH_WINDOW (result);
}

void
splash_window_show (SplashWindow *window)
{
	gint m, monitors = 0;
	GdkDisplay *display;
	GdkRectangle geometry;

	g_return_if_fail (SPLASH_IS_WINDOW (window));

	display = gdk_display_get_default ();
	monitors = gdk_display_get_n_monitors (display);
	for (m = 0; m < monitors; m++) {
		GdkMonitor *monitor = gdk_display_get_monitor (display, m);
		if (gdk_monitor_is_primary (monitor)) {
			gdk_monitor_get_geometry (monitor, &geometry);
			gtk_widget_set_size_request (GTK_WIDGET (window), geometry.width, geometry.height);
			gtk_window_move (GTK_WINDOW (window), geometry.x, geometry.y);
			break;
		}
	}

//	gtk_spinner_start (GTK_SPINNER (window->priv->spinner));

	gtk_window_set_keep_above (GTK_WINDOW (window), TRUE);
	gtk_widget_show_all (GTK_WIDGET (window));
}

void
splash_window_destroy (SplashWindow *window)
{
	g_return_if_fail (SPLASH_IS_WINDOW (window));

//	gtk_spinner_stop (GTK_SPINNER (window->priv->spinner));

	gtk_widget_destroy (GTK_WIDGET (window));
}
