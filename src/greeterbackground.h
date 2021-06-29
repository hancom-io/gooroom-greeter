/*
 * Copyright (C) 2014 - 2015, Sean Davis <smd.seandavis@gmail.com>
 * Copyright (C) 2014, Andrew P. <pan.pav.7c5@gmail.com>
 * Copyright (C) 2015, Simon Steinbeiß <ochosi@shimmerproject.org>
 * Copyright (C) 2015 - 2021 Gooroom <gooroom@gooroom.kr>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */


#ifndef GREETER_BACKGROUND_H
#define GREETER_BACKGROUND_H

#include <glib.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GREETER_BACKGROUND_TYPE             (greeter_background_get_type())
#define GREETER_BACKGROUND(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GREETER_BACKGROUND_TYPE, GreeterBackground))
#define GREETER_BACKGROUND_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GREETER_BACKGROUND_TYPE, GreeterBackgroundClass))
#define GREETER_IS_BACKGROUND(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GREETER_BACKGROUND_TYPE))
#define GREETER_IS_BACKGROUND_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GREETER_BACKGROUND_TYPE))


typedef struct _GreeterBackground           GreeterBackground;
typedef struct _GreeterBackgroundClass      GreeterBackgroundClass;

GType greeter_background_get_type(void) G_GNUC_CONST;

GreeterBackground* greeter_background_new           (GtkWidget* child);

void greeter_background_set_monitor_config          (GreeterBackground* background,
                                                     const gchar*       bg);
void greeter_background_connect                     (GreeterBackground* background,
                                                     GdkScreen* screen);
//void greeter_background_save_xroot                  (GreeterBackground* background);
void greeter_background_add_accel_group             (GreeterBackground* background,
                                                     GtkAccelGroup*     group);
GdkPixbuf *greeter_background_pixbuf_get            (GreeterBackground* background);

const GdkRectangle* greeter_background_get_active_monitor_geometry (GreeterBackground* background);

void  greeter_background_set_active_monitor_from_geometry (GreeterBackground  *background,
                                                           const GdkRectangle *geometry);


G_END_DECLS

#endif // GREETER_BACKGROUND_H
