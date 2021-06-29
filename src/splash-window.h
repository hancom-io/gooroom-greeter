/*
 * Copyright (C) 2015 - 2021 Gooroom <gooroom@gooroom.kr>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef __SPLASH_WINDOW_H__
#define __SPLASH_WINDOW_H__

#include <glib.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SPLASH_TYPE_WINDOW         (splash_window_get_type ())
#define SPLASH_WINDOW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), SPLASH_TYPE_WINDOW, SplashWindow))
#define SPLASH_WINDOW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), SPLASH_TYPE_WINDOW, SplashWindowClass))
#define SPLASH_IS_WINDOW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), SPLASH_TYPE_WINDOW))
#define SPLASH_IS_WINDOW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), SPLASH_TYPE_WINDOW))
#define SPLASH_WINDOW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), SPLASH_TYPE_WINDOW, SplashWindowClass))

typedef struct _SplashWindow SplashWindow;
typedef struct _SplashWindowClass SplashWindowClass;
typedef struct _SplashWindowPrivate SplashWindowPrivate;

struct _SplashWindow {
	GtkWindow __parent__;

	SplashWindowPrivate *priv;
};

struct _SplashWindowClass {
	GtkWindowClass __parent_class__;
};

GType          splash_window_get_type          (void); G_GNUC_CONST

SplashWindow  *splash_window_new               (GtkWindow *parent);

void           splash_window_show              (SplashWindow *window);
void           splash_window_destroy           (SplashWindow *window);
void           splash_window_set_message_label (SplashWindow *window,
                                                const char   *message);

G_END_DECLS

#endif /* __SPLASH_WINDOW_H__ */
