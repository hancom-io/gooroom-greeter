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
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "greeter-message-dialog.h"

struct _GreeterMessageDialogPrivate {
	GtkWidget *icon_image;
	GtkWidget *title_box;
	GtkWidget *title_label;
	GtkWidget *message_label;
};

G_DEFINE_TYPE_WITH_PRIVATE (GreeterMessageDialog, greeter_message_dialog, GTK_TYPE_DIALOG);


//static void
//greeter_message_dialog_close (GtkDialog *dialog)
//{
//}

static void
greeter_message_dialog_finalize (GObject *object)
{
	G_OBJECT_CLASS (greeter_message_dialog_parent_class)->finalize (object);
}

static void
greeter_message_dialog_init (GreeterMessageDialog *dialog)
{
	dialog->priv = greeter_message_dialog_get_instance_private (dialog);

	gtk_widget_init_template (GTK_WIDGET (dialog));

	gtk_window_set_decorated (GTK_WINDOW (dialog), FALSE);
	gtk_window_set_skip_taskbar_hint (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_skip_pager_hint (GTK_WINDOW (dialog), TRUE);
	gtk_widget_set_app_paintable (GTK_WIDGET (dialog), TRUE);

	GdkScreen *screen = gtk_window_get_screen (GTK_WINDOW (dialog));
	if (gdk_screen_is_composited (screen)) {
		GdkVisual *visual = gdk_screen_get_rgba_visual (screen);
		if (visual == NULL)
			visual = gdk_screen_get_system_visual (screen);

		gtk_widget_set_visual (GTK_WIDGET (dialog), visual);
	}

//	PangoAttrList *attrs;
//	PangoAttribute *attr;
//	attrs = pango_attr_list_new ();
//	attr = pango_attr_rise_new (1000);
//	pango_attr_list_insert (attrs, attr);
//	gtk_label_set_attributes (GTK_LABEL (dialog->priv->message_label), attrs);
}

static void
greeter_message_dialog_class_init (GreeterMessageDialogClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
//	GtkDialogClass *dialog_class = GTK_DIALOG_CLASS (klass);

	gobject_class->finalize = greeter_message_dialog_finalize;

//	dialog_class->close = greeter_message_dialog_close;

	gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass),
                                                 "/kr/gooroom/greeter/greeter-message-dialog.ui");

	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GreeterMessageDialog, icon_image);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GreeterMessageDialog, title_box);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GreeterMessageDialog, title_label);
	gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GreeterMessageDialog, message_label);
}

GtkWidget *
greeter_message_dialog_new (GtkWindow *parent,
                            const char *icon,
                            const char *title,
                            const char *message)
{
	GreeterMessageDialog *dialog;

	dialog = GREETER_MESSAGE_DIALOG (g_object_new (GREETER_TYPE_MESSAGE_DIALOG,
                                                   "use-header-bar", FALSE,
                                                   "transient-for", parent,
                                                   NULL));

	greeter_message_dialog_set_icon (dialog, icon);
	greeter_message_dialog_set_title (dialog, title);
	greeter_message_dialog_set_message (dialog, message);

	return GTK_WIDGET (dialog);
}

void
greeter_message_dialog_set_title (GreeterMessageDialog *dialog,
                                  const char           *title)
{
	if (title) {
		gtk_widget_show (dialog->priv->title_label);
		gtk_label_set_text (GTK_LABEL (dialog->priv->title_label), title);
	} else {
		gtk_widget_hide (dialog->priv->title_label);
	}
}

void
greeter_message_dialog_set_message (GreeterMessageDialog *dialog,
                                    const char           *message)
{
	if (message)
		gtk_label_set_text (GTK_LABEL (dialog->priv->message_label), message);
}

void
greeter_message_dialog_set_icon (GreeterMessageDialog *dialog,
                                 const char           *icon)
{
	if (icon) {
		gtk_image_set_from_icon_name (GTK_IMAGE (dialog->priv->icon_image),
                                      icon, GTK_ICON_SIZE_DIALOG);

		gtk_widget_show (dialog->priv->icon_image);
	}
}
