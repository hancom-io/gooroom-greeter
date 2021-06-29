/*
 * Copyright (C) 2015 - 2021 Gooroom <gooroom@gooroom.kr>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef __GREETER_MESSAGE_DIALOG_H__
#define __GREETER_MESSAGE_DIALOG_H__

#include <glib.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GREETER_TYPE_MESSAGE_DIALOG            (greeter_message_dialog_get_type ())
#define GREETER_MESSAGE_DIALOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GREETER_TYPE_MESSAGE_DIALOG, GreeterMessageDialog))
#define GREETER_MESSAGE_DIALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),  GREETER_TYPE_MESSAGE_DIALOG, GreeterMessageDialogClass))
#define GREETER_IS_MESSAGE_DIALOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GREETER_TYPE_MESSAGE_DIALOG))
#define GREETER_IS_MESSAGE_DIALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),  GREETER_TYPE_MESSAGE_DIALOG))
#define GREETER_MESSAGE_DIALOG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),  GREETER_TYPE_MESSAGE_DIALOG, GreeterMessageDialogClass))

typedef struct _GreeterMessageDialog GreeterMessageDialog;
typedef struct _GreeterMessageDialogClass GreeterMessageDialogClass;
typedef struct _GreeterMessageDialogPrivate GreeterMessageDialogPrivate;

struct _GreeterMessageDialog {
	GtkDialog dialog;

	GreeterMessageDialogPrivate *priv;
};

struct _GreeterMessageDialogClass {
	GtkDialogClass parent_class;
};

GType	    greeter_message_dialog_get_type	(void) G_GNUC_CONST;

GtkWidget  *greeter_message_dialog_new		(GtkWindow  *parent,
                                             const char *icon,
                                             const char *title,
                                             const char *message); 

void greeter_message_dialog_set_title       (GreeterMessageDialog *dialog,
                                             const char           *title);
void greeter_message_dialog_set_message     (GreeterMessageDialog *dialog,
                                             const char           *message);
void greeter_message_dialog_set_icon        (GreeterMessageDialog *dialog,
                                             const char           *icon);

G_END_DECLS

#endif /* __GREETER_MESSAGE_DIALOG_H__ */
