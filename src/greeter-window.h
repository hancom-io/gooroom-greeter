/*
 * Copyright (C) 2015 - 2021 Gooroom <gooroom@gooroom.kr>.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 *
 */

#ifndef __GREETER_WINDOW_H__
#define __GREETER_WINDOW_H__

#include <glib.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GREETER_TYPE_WINDOW         (greeter_window_get_type ())
#define GREETER_WINDOW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GREETER_TYPE_WINDOW, GreeterWindow))
#define GREETER_WINDOW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GREETER_TYPE_WINDOW, GreeterWindowClass))
#define GREETER_IS_WINDOW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GREETER_TYPE_WINDOW))
#define GREETER_IS_WINDOW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GREETER_TYPE_WINDOW))
#define GREETER_WINDOW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GREETER_TYPE_WINDOW, GreeterWindowClass))

typedef struct _GreeterWindow GreeterWindow;
typedef struct _GreeterWindowClass GreeterWindowClass;
typedef struct _GreeterWindowPrivate GreeterWindowPrivate;

struct _GreeterWindow {
	GtkBox __parent__;

	GreeterWindowPrivate *priv;
};

struct _GreeterWindowClass {
	GtkBoxClass __parent_class__;

	void (*position_changed) (GreeterWindow *window, GdkRectangle *geometry);
};

GType       greeter_window_get_type                     (void); G_GNUC_CONST

GtkWidget  *greeter_window_new                          (void);

void        greeter_window_set_switch_indicator_visible (GreeterWindow *window,
                                                         gboolean       visible);

G_END_DECLS

#endif /* __GREETER_WINDOW_H__ */
