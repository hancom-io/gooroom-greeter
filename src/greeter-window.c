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
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <ctype.h>

#include <lightdm.h>
#include <upower.h>

#include <libayatana-ido/libayatana-ido.h>
#include <libayatana-indicator/indicator-ng.h>
#include <libayatana-indicator/indicator-object.h>

#include "greeter-window.h"
#include "splash-window.h"
#include "indicator-button.h"
#include "greeterconfiguration.h"
#include "greeter-message-dialog.h"
#include "greeter-password-settings-dialog.h"

#define LOGIN_TIMEOUT 60

enum {
	SYSTEM_SHUTDOWN,
	SYSTEM_RESTART,
	SYSTEM_SUSPEND,
	SYSTEM_HIBERNATE
};

enum
{
	POSITION_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

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


struct _GreeterWindowPrivate
{
	GtkWidget *id_entry;
	GtkWidget *pw_entry;
	GtkWidget *login_button;
	GtkWidget *panel_box;
	GtkWidget *indicator_box;
	GtkWidget *btn_shutdown;
	GtkWidget *btn_restart;
	GtkWidget *btn_suspend;
	GtkWidget *btn_hibernate;
	GtkWidget *pw_dialog;
	GtkWidget *spinner;
	GtkWidget *switch_indicator;

	SplashWindow *splash;

	LightDMGreeter *lightdm;

	GPtrArray *devices;
	UpClient  *up_client;

	gboolean prompted;
	gboolean prompt_active;
	gboolean have_pam_error;
	gboolean changing_password;

	gchar *id;
	gchar *pw;
	gchar *current_session;
	gchar *current_language;

	/* Pending questions */
	GSList *pending_questions;

	guint  splash_timeout_id;

	gint changing_password_step;
};

G_DEFINE_TYPE_WITH_PRIVATE (GreeterWindow, greeter_window, GTK_TYPE_BOX);


static void process_prompts (GreeterWindow *window);
static void login_button_clicked_cb (GtkButton *widget, gpointer user_data);


static gboolean
grab_focus_idle (gpointer user_data)
{
	gtk_widget_grab_focus (GTK_WIDGET (user_data));

	return FALSE;
}

/* Pending questions */
static void
pam_message_finalize (PAMConversationMessage *message)
{
	g_free (message->text);
	g_free (message);
}

static gchar *
get_id (GtkWidget *id_entry)
{
	int i = 0;
	const gchar *text;

	text = gtk_entry_get_text (GTK_ENTRY (id_entry));
	if (strlen (text) == 0)
		return g_strdup ("");

	for (i = 0; text[i] != '\0'; i++)
		if (!isdigit (text[i]))
			return g_strdup (text);

	return g_strdup_printf ("kepco-%s", text); 
}

static gboolean
is_valid_session (GList       *items,
                  const gchar *session)
{
	for (; items; items = g_list_next (items))
		if (g_strcmp0 (session, lightdm_session_get_key (items->data)) == 0)
			return TRUE;

	return FALSE;
}

static void
set_session (GreeterWindow *window, const gchar *session)
{
	GList *sessions = lightdm_get_sessions ();
	GreeterWindowPrivate *priv = window->priv;

	/* Validation */
	if (!session || !is_valid_session (sessions, session)) {
		/* default */
		const gchar* default_session = lightdm_greeter_get_default_session_hint (priv->lightdm);
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

    g_free (priv->current_session);
    priv->current_session = g_strdup (session);
}

static void
set_language (GreeterWindow *window, const gchar *language)
{
	GreeterWindowPrivate *priv = window->priv;

	g_free (priv->current_language);
	priv->current_language = g_strdup (language);
}

static void
start_authentication (GreeterWindow *window, const gchar *username)
{
	GreeterWindowPrivate *priv = window->priv;
	LightDMGreeter *greeter = priv->lightdm;

	priv->prompted = FALSE;
	priv->prompt_active = FALSE;
	priv->have_pam_error = FALSE;

	if (priv->pending_questions)
	{
		g_slist_free_full (priv->pending_questions, (GDestroyNotify) pam_message_finalize);
		priv->pending_questions = NULL;
	}

	if (g_strcmp0 (username, "*other") == 0)
	{
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
			if (!priv->current_session)
				set_session (window, lightdm_user_get_session (user));
			if (!priv->current_language)
				set_language (window, lightdm_user_get_language (user));
		}
		else
		{
			set_session (window, NULL);
			set_language (window, NULL);
		}
#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
		lightdm_greeter_authenticate (greeter, username, NULL);
#else
		lightdm_greeter_authenticate (greeter, username);
#endif
	}
}

static void
password_settings_dialog_response_cb (GtkDialog *dialog,
                                      gint       response,
                                      gpointer   user_data)
{
	GreeterWindow *window = GREETER_WINDOW (user_data);
	GreeterWindowPrivate *priv = window->priv;

	if (response == GTK_RESPONSE_OK) {
		priv->prompt_active = FALSE;

		if (lightdm_greeter_get_in_authentication (priv->lightdm)) {
			const gchar *entry_text = greeter_password_settings_dialog_get_entry_text (GREETER_PASSWORD_SETTINGS_DIALOG (priv->pw_dialog));
#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
			lightdm_greeter_respond (priv->lightdm, entry_text, NULL);
#else
			lightdm_greeter_respond (priv->lightdm, entry_text);
#endif
			/* If we have questions pending, then we continue processing
			 * those, until we are done. (Otherwise, authentication will
			 * not complete.) */
			if (priv->pending_questions)
				process_prompts (window);
		}
		return;
	}

	gtk_widget_destroy (priv->pw_dialog);
	priv->pw_dialog = NULL;
	priv->changing_password = FALSE;
	gtk_entry_set_text (GTK_ENTRY (priv->pw_entry), "");
	gtk_widget_grab_focus (priv->pw_entry);
	start_authentication (window, lightdm_greeter_get_authentication_user (priv->lightdm));
}

static void
login_error_dialog_response_cb (GtkDialog *dialog,
                                gint       response,
                                gpointer   user_data)
{
	GreeterWindow *window = GREETER_WINDOW (user_data);
	GreeterWindowPrivate *priv = window->priv;

	gtk_entry_set_text (GTK_ENTRY (priv->pw_entry), "");
	gtk_widget_grab_focus (priv->pw_entry);
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
show_login_error_dialog (GreeterWindow *window,
                         const gchar      *title,
                         const gchar      *message)
{
	GtkWidget *dialog;

	dialog = greeter_message_dialog_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (window))),
                                         "dialog-warning-symbolic.symbolic",
                                         title,
                                         message ? message : "");

	gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("Ok"), GTK_RESPONSE_OK, NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

	g_signal_connect (G_OBJECT (dialog), "response",
                      G_CALLBACK (login_error_dialog_response_cb), window);

	gtk_widget_show (dialog);

	window->priv->have_pam_error = TRUE;
}

static void
run_warning_dialog (GreeterWindow *window,
                    const gchar      *title,
                    const gchar      *message,
                    const gchar      *data)
{
	GtkWidget *dialog;
	gchar *response = NULL;
	GreeterWindowPrivate *priv = window->priv;

	dialog = greeter_message_dialog_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (window))),
                                         "dialog-warning-symbolic.symbolic",
                                         title,
                                         message);

	gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("Ok"), GTK_RESPONSE_OK, NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	if (data) {
		if (g_str_equal (data, "CHPASSWD_FAILURE_OK")) {
			priv->changing_password = FALSE;
			gtk_entry_set_text (GTK_ENTRY (priv->pw_entry), "");
			gtk_widget_grab_focus (priv->pw_entry);
			start_authentication (window, lightdm_greeter_get_authentication_user (priv->lightdm));
		} else if (g_str_equal (data, "ACCT_EXP_OK")) {
			response = "acct_exp_ok";
		} else if (g_str_equal (data, "DEPT_EXP_OK")) {
			response = "dept_exp_ok";
		} else if (g_str_equal (data, "PASS_EXP_OK")) {
			response = "pass_exp_ok";
		} else if (g_str_equal (data, "DUPLICATE_LOGIN_OK")) {
			response = "duplicate_login_ok";
		} else if (g_str_equal (data, "TRIAL_LOGIN_OK")) {
			response = "trial_login_ok";
		}
	}

	if (response) {
		if (lightdm_greeter_get_in_authentication (priv->lightdm)) {
#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
			lightdm_greeter_respond (priv->lightdm, response, NULL);
#else
			lightdm_greeter_respond (priv->lightdm, response);
#endif
		}
	}

	priv->have_pam_error = TRUE;
}

static gboolean
show_password_settings_dialog (GreeterWindow *window)
{
	GtkWidget *dialog, *toplevel;

	if (window->priv->pw_dialog)
		return FALSE;

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (window));
	dialog = window->priv->pw_dialog = greeter_password_settings_dialog_new (GTK_WINDOW (toplevel));

	g_signal_connect (G_OBJECT (dialog), "response",
                      G_CALLBACK (password_settings_dialog_response_cb), window);

	gtk_widget_show (dialog);

	return TRUE;
}

static void
run_password_changing_dialog (GreeterWindow *window,
                              const gchar      *title,
                              const gchar      *message,
                              const gchar      *yes,
                              const gchar      *no,
                              const gchar      *data)
{
	gint res;
	GtkWidget *dialog;
	const gchar *yes_text, *no_text;
	GtkWidget *suggested_button;
	GtkStyleContext *style = NULL;
	GreeterWindowPrivate *priv = window->priv;

	dialog = greeter_message_dialog_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (window))),
                                         "dialog-password-symbolic",
                                         title,
                                         message);

	yes_text = (yes) ? yes : _("Ok");
	no_text = (no) ? no : _("Cancel");

	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                            yes_text, GTK_RESPONSE_OK,
                            no_text, GTK_RESPONSE_CANCEL,
                            NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

	gtk_widget_show (dialog);

	suggested_button = gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
	style = gtk_widget_get_style_context (suggested_button);
	gtk_style_context_add_class (style, "suggested-action");
	gtk_widget_queue_draw (dialog);

	res = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	if (res == GTK_RESPONSE_OK) {
		priv->changing_password = TRUE;

		if (g_strcmp0 (data, "req_response") == 0) {
			if (!show_password_settings_dialog (window))
				goto out;

#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
			lightdm_greeter_respond (priv->lightdm, "chpasswd_yes", NULL);
#else
			lightdm_greeter_respond (priv->lightdm, "chpasswd_yes");
#endif
        } else {
			if (!show_password_settings_dialog (window))
				goto out;
		}

		return;
	}

	if (g_strcmp0 (data, "req_response") == 0) {
		if (lightdm_greeter_get_in_authentication (priv->lightdm)) {
#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
			lightdm_greeter_respond (priv->lightdm, "chpasswd_no", NULL);
#else
			lightdm_greeter_respond (priv->lightdm, "chpasswd_no");
#endif
		}
	}

out:
	priv->changing_password = FALSE;
	gtk_entry_set_text (GTK_ENTRY (priv->pw_entry), "");
	gtk_widget_grab_focus (priv->pw_entry);
	start_authentication (window, lightdm_greeter_get_authentication_user (priv->lightdm));
}

static void
hide_splash (GreeterWindow *window)
{
	GreeterWindowPrivate *priv = window->priv;

	gtk_spinner_stop (GTK_SPINNER (priv->spinner));

	if (priv->splash) {
		splash_window_destroy (priv->splash);
		priv->splash = NULL;
	}
}

static void
show_splash (GreeterWindow *window, GtkWidget *parent)
{
	GreeterWindowPrivate *priv = window->priv;

	hide_splash (window);

	gtk_spinner_start (GTK_SPINNER (priv->spinner));

	priv->splash = splash_window_new (GTK_WINDOW (parent));
	splash_window_show (priv->splash);
}

static gboolean
showing_splash_timeout_cb (gpointer user_data)
{   
	GreeterWindow *window = GREETER_WINDOW (user_data);
	GreeterWindowPrivate *priv = window->priv;

	hide_splash (window);

	g_clear_handle_id (&priv->splash_timeout_id, g_source_remove);
	priv->splash_timeout_id = 0;

	return FALSE;
}

static void
post_login (GreeterWindow *window)
{
	GreeterWindowPrivate *priv = window->priv;

	hide_splash (window);

	g_clear_handle_id (&priv->splash_timeout_id, g_source_remove);
	priv->splash_timeout_id = 0;

	gtk_widget_set_sensitive (priv->id_entry, TRUE);
	gtk_widget_set_sensitive (priv->pw_entry, TRUE);
	gtk_widget_set_sensitive (priv->login_button, TRUE);
	gtk_entry_set_text (GTK_ENTRY (priv->pw_entry), "");
	gtk_widget_grab_focus (priv->pw_entry);

	g_signal_handlers_unblock_by_func (window->priv->login_button, login_button_clicked_cb, window);
}

static void
pre_login (GreeterWindow *window)
{
	GtkWidget *toplevel;
	GreeterWindowPrivate *priv = window->priv;

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (window));

	show_splash (window, toplevel);

	g_signal_handlers_block_by_func (priv->login_button, login_button_clicked_cb, window);

	gtk_widget_set_sensitive (priv->id_entry, FALSE);
	gtk_widget_set_sensitive (priv->pw_entry, FALSE);
	gtk_widget_set_sensitive (priv->login_button, FALSE);

	priv->splash_timeout_id = g_timeout_add (LOGIN_TIMEOUT * 1000,
                                             showing_splash_timeout_cb, window);
}

static void
process_prompts (GreeterWindow *window)
{
	const gchar *id;
	GreeterWindowPrivate *priv = window->priv;
	LightDMGreeter *greeter = priv->lightdm;

	if (!priv->pending_questions)
		return;

	/* always allow the user to change username again */
	gtk_widget_set_sensitive (priv->id_entry, TRUE);
	gtk_widget_set_sensitive (priv->pw_entry, TRUE);
	id = gtk_entry_get_text (GTK_ENTRY (priv->id_entry));
	gtk_widget_set_sensitive (priv->login_button, strlen (id) > 0);

	/* Special case: no user selected from list, so PAM asks us for the user
	 * via a prompt. For that case, use the username field */
	if (!priv->prompted && priv->pending_questions && !priv->pending_questions->next &&
        ((PAMConversationMessage *) priv->pending_questions->data)->is_prompt &&
        ((PAMConversationMessage *) priv->pending_questions->data)->type.prompt != LIGHTDM_PROMPT_TYPE_SECRET &&
        gtk_widget_get_visible (priv->id_entry) &&
        lightdm_greeter_get_authentication_user (greeter) == NULL)
	{
		priv->prompted = TRUE;
		priv->prompt_active = TRUE;
		gtk_widget_grab_focus (priv->id_entry);
		return;
	}

	while (priv->pending_questions)
	{
		PAMConversationMessage *message = (PAMConversationMessage *) priv->pending_questions->data;
		priv->pending_questions = g_slist_remove (priv->pending_questions, (gconstpointer) message);

		const gchar *filter_msg_000 = "You are required to change your password immediately";
		const gchar *filter_msg_010 = g_dgettext("Linux-PAM", "You are required to change your password immediately (administrator enforced)");
		const gchar *filter_msg_020 = g_dgettext("Linux-PAM", "You are required to change your password immediately (password expired)");
		const gchar *filter_msg_030 = "Temporary Password";
		const gchar *filter_msg_040 = "Password Maxday Warning";
		const gchar *filter_msg_050 = "Account Expiration Warning";
		const gchar *filter_msg_051 = "Division Expiration Warning";
		const gchar *filter_msg_052 = "Password Expiration Warning";
		const gchar *filter_msg_053 = _("your password will expire in");
		const gchar *filter_msg_060 = "Duplicate Login Notification";
		const gchar *filter_msg_070 = "Authentication Failure";
        const gchar *filter_msg_071 = "Deleted Account";
        const gchar *filter_msg_072 = "Invalid Account";
        const gchar *filter_msg_073 = "No Exist Account";
        const gchar *filter_msg_074 = "Policy Violation Account";
        const gchar *filter_msg_075 = "Not Allowed IP";
		const gchar *filter_msg_080 = "Account Locking";
		const gchar *filter_msg_090 = "Account Expiration";
		const gchar *filter_msg_100 = "Password Expiration";
		const gchar *filter_msg_110 = "Duplicate Login";
		const gchar *filter_msg_120 = "Division Expiration";
		const gchar *filter_msg_130 = "Login Trial Exceed";
		const gchar *filter_msg_140 = "Trial Period Expired";
		const gchar *filter_msg_150 = "DateTime Error";
		const gchar *filter_msg_160 = "Trial Period Warning";

		if ((strstr (message->text, filter_msg_000) != NULL) ||
		    (strstr (message->text, filter_msg_010) != NULL) ||
            (strstr (message->text, filter_msg_020) != NULL)) {
			post_login (window);
			run_password_changing_dialog (window,
                                          NULL,
                                          _("Your password has expired.\n"
                                            "Please change your password immediately."),
                                          _("Changing Password"),
                                          _("Cancel"),
                                          "req_no_response");
			continue;
		} else if (g_str_has_prefix (message->text, filter_msg_030)) {
			post_login (window);
			run_password_changing_dialog (window,
                                          NULL,
                                          _("Your password has been issued temporarily.\n"
                                            "For security reasons, please change your password immediately."),
                                          _("Changing Password"),
                                          _("Cancel"),
                                          "req_no_response");
			continue;
		} else if (g_str_has_prefix (message->text, filter_msg_040)) {
			post_login (window);

			gchar *msg = NULL;
			gchar **tokens = g_strsplit (message->text, ":", -1);
			if (g_strv_length (tokens) > 1) {
				if (g_str_equal (tokens[1], "1")) {
					msg = g_strdup_printf (_("Please change your password for security.\n"
                                             "If you do not change your password within %s day, "
                                             "your password expires.\n"
                                             "You can no longer log in.\n"
                                             "Do you want to change password now?"), tokens[1]);
				} else {
					msg = g_strdup_printf (_("Please change your password for security.\n"
                                             "If you do not change your password within %s days, "
                                             "your password expires.\n"
                                             "You can no longer log in.\n"
                                             "Do you want to change password now?"), tokens[1]);
				}
			} else {
				msg = g_strdup (_("Please change your password for security.\n"
                                  "If you do not change your password within a few days, "
                                  "your password expires.\n"
                                  "You can no longer log in.\n"
                                  "Do you want to change password now?"));
			}
			g_strfreev (tokens);

			run_password_changing_dialog (window, NULL, msg, _("Change now"), _("Later"), "req_response");
			g_free (msg);

			continue;
		} else if (g_str_has_prefix (message->text, filter_msg_050)) {
			post_login (window);

			gchar *msg = NULL;
			gchar **tokens = g_strsplit (message->text, ":", -1);
			if (g_strv_length (tokens) > 2) {
				if (g_str_equal (tokens[1], "1")) {
					msg = g_strdup_printf (_("Your account will not be available after %s.\n"
                                             "Your account will expire in %s day."),
                                           tokens[1], tokens[2]);
				} else {
					msg = g_strdup_printf (_("Your account will not be available after %s.\n"
                                             "Your account will expire in %s days."),
                                           tokens[1], tokens[2]);
				}
			}
			g_strfreev (tokens);

			run_warning_dialog (window, NULL, msg, "ACCT_EXP_OK");
			g_free (msg);

			continue;
		} else if (g_str_has_prefix (message->text, filter_msg_051)) {
			post_login (window);

			gchar *msg = NULL;
			gchar **tokens = g_strsplit (message->text, ":", -1);
			if (g_strv_length (tokens) > 2) {
				if (g_str_equal (tokens[1], "1")) {
					msg = g_strdup_printf (_("Your organization will not be available after %s.\n"
                                             "Your organization will expire in %s day."),
                                           tokens[1], tokens[2]);
				} else {
					msg = g_strdup_printf (_("Your organization will not be available after %s.\n"
                                             "Your organization will expire in %s days."),
                                           tokens[1], tokens[2]);
				}
			}
			g_strfreev (tokens);

			run_warning_dialog (window, NULL, msg, "DEPT_EXP_OK");
			g_free (msg);

			continue;
		} else if (g_str_has_prefix (message->text, filter_msg_052)) {
			post_login (window);

			gchar *msg = NULL;
			gchar **tokens = g_strsplit (message->text, ":", -1);
			if (g_strv_length (tokens) > 2) {
				if (g_str_equal (tokens[1], "1")) {
					msg = g_strdup_printf (_("Your password will not be available after %s.\n"
                                             "Your password will expire in %s day."),
                                           tokens[1], tokens[2]);
				} else {
					msg = g_strdup_printf (_("Your password will not be available after %s.\n"
                                             "Your password will expire in %s days."),
                                           tokens[1], tokens[2]);
				}
			}
			g_strfreev (tokens);

			run_warning_dialog (window, NULL, msg, "PASS_EXP_OK");
			g_free (msg);

			continue;
		} else if ((strstr (message->text, filter_msg_053) != NULL)) {
			run_warning_dialog (window, NULL, message->text, NULL);

			continue;
		} else if (g_str_has_prefix (message->text, filter_msg_060)) {
			post_login (window);

			GString *msg = g_string_new (_("Duplicate logins detected with the same ID."));
			gchar **tokens = g_strsplit (message->text, ":", -1);
			if (tokens[1]) {
				gchar *text = g_strdup_printf ("%s : %s", _("Client ID"), tokens[1]);
				g_string_append_printf (msg, "\n\n%s", text);
				g_free (text);
			}
			if (tokens[2]) {
				gchar *text = g_strdup_printf ("%s : %s", _("Client Name"), tokens[2]);
				g_string_append_printf (msg, "\n%s", text);
				g_free (text);
			}
			if (tokens[3]) {
				gchar *text = g_strdup_printf ("%s : %s", _("IP"), tokens[3]);
				g_string_append_printf (msg, "\n%s", text);
				g_free (text);
			}
			if (tokens[4]) {
				gchar *text = g_strdup_printf ("%s : %s", _("Local IP"), tokens[4]);
				g_string_append_printf (msg, "\n%s", text);
				g_free (text);
			}

			run_warning_dialog (window, NULL, msg->str, "DUPLICATE_LOGIN_OK");
			g_string_free (msg, TRUE);

			continue;
		} else if (g_str_has_prefix (message->text, filter_msg_070)) {
			post_login (window);

			gchar *msg = NULL;
			gchar **tokens = g_strsplit (message->text, ":", -1);
			if (g_strv_length (tokens) > 1) {
				msg = g_strdup_printf (_("Authentication Failure\n"
                                         "You have %s login attempts remaining.\n"
                                         "You can no longer log in when the maximum number of login "
                                         "attempts is exceeded."), tokens[1]);
			} else {
				msg = g_strdup (_("The user could not be authenticated due to an unknown error.\n"
                                  "Please contact the administrator."));
			}
			g_strfreev (tokens);
			show_login_error_dialog (window, NULL, msg);
			g_free (msg);
			break;
		} else if (g_str_has_prefix (message->text, filter_msg_071)) {
			gchar *msg = g_strdup (_("This account has deleted and is no longer available.\n"
                                     "Please contact the administrator."));
			show_login_error_dialog (window, NULL, msg);
			g_free (msg);
			break;
		} else if (g_str_has_prefix (message->text, filter_msg_072)) {
			gchar *msg = g_strdup (_("You attempted to log in from an unregistered device.\n"
                                     "Please contact the administrator."));
			show_login_error_dialog (window, NULL, msg);
			g_free (msg);
			break;
		} else if (g_str_has_prefix (message->text, filter_msg_073)) {
			gchar *msg = g_strdup ( _("Authentication Failure\n"
                                      "Please check the username and password and try again."));
			show_login_error_dialog (window, NULL, msg);
			g_free (msg);
			break;
		} else if (g_str_has_prefix (message->text, filter_msg_074)) {
			gchar *msg = g_strdup (_("Login was denied because "
                                     "it violated the policy set by the GPMS.\n"
                                     "Please contact the administrator."));
			show_login_error_dialog (window, NULL, msg);
			g_free (msg);
			break;
		} else if (g_str_has_prefix (message->text, filter_msg_075)) {
			gchar *msg = g_strdup (_("Login was denied because "
                                     "it violated the policy(Allowed IP) set by the GPMS.\n"
                                     "Please contact the administrator."));
			show_login_error_dialog (window, NULL, msg);
			g_free (msg);
			break;
		} else if (g_str_has_prefix (message->text, filter_msg_080)) {
			post_login (window);

			gchar *msg = g_strdup (_("Your account has been locked because\n"
                                     "you have exceeded the number of login attempts.\n"
                                     "Please try again in a moment."));
			show_login_error_dialog (window, NULL, msg);
			g_free (msg);
			break;
		} else if (g_str_has_prefix (message->text, filter_msg_090)) {
			post_login (window);

			gchar *msg = g_strdup (_("This account has expired and is no longer available.\n"
                                     "Please contact the administrator."));
			show_login_error_dialog (window, NULL, msg);
			g_free (msg);
			break;
		} else if (g_str_has_prefix (message->text, filter_msg_100)) {
			post_login (window);

			gchar *msg = g_strdup (_("The password for your account has expired.\n"
                                     "Please contact the administrator."));
			show_login_error_dialog (window, NULL, msg);
			g_free (msg);
			break;
		} else if (g_str_has_prefix (message->text, filter_msg_110)) {
			post_login (window);

			gchar *msg = g_strdup (_("You are already logged in.\n"
                                     "Log out of the other device and try again.\n"
                                     "If the problem persists, please contact your administrator."));
			show_login_error_dialog (window, NULL, msg);
			g_free (msg);
			break;
		} else if (g_str_has_prefix (message->text, filter_msg_120)) {
			post_login (window);

			gchar *msg = g_strdup (_("Due to the expiration of your organization, "
                                     "this account is no longer available.\n"
                                     "Please contact the administrator."));
			show_login_error_dialog (window, NULL, msg);
			g_free (msg);
			break;
		} else if (g_str_has_prefix (message->text, filter_msg_130)) {
			post_login (window);

			gchar *msg = g_strdup (_("Login attempts exceeded the number of times,\n"
                                     "so you cannot login for a certain period of time.\n"
                                     "Please try again in a moment."));
			show_login_error_dialog (window, NULL, msg);
			g_free (msg);
			break;
		} else if (g_str_has_prefix (message->text, filter_msg_140)) {
			post_login (window);

			gchar *msg = g_strdup (_("Trial period has expired."));
			show_login_error_dialog (window, NULL, msg);
			g_free (msg);
			break;
		} else if (g_str_has_prefix (message->text, filter_msg_150)) {
			post_login (window);

			gchar *msg = g_strdup (_("Time error occurred."));
			show_login_error_dialog (window, NULL, msg);
			g_free (msg);
			break;
		} else if (g_str_has_prefix (message->text, filter_msg_160)) {
			post_login (window);

			gchar *msg = NULL;
			gchar **tokens = g_strsplit (message->text, ":", -1);
			if (g_strv_length (tokens) > 2) {
				if (g_str_equal (tokens[2], "0")) {
					msg = g_strdup_printf (_("The trial period is up to %s days.\n"
                                             "The trial period expires today."), tokens[1]);
				} else if (g_str_equal (tokens[2], "1")){
					msg = g_strdup_printf (_("The trial period is up to %s days.\n"
                                             "%s day left to expire."), tokens[1], tokens[2]);
				} else {
					msg = g_strdup_printf (_("The trial period is up to %s days.\n"
                                             "%s days left to expire."), tokens[1], tokens[2]);
				}
			} else {
				msg = g_strdup (_("The trial period is unknown."));
			}
			g_strfreev (tokens);

			run_warning_dialog (window, NULL, msg, "TRIAL_LOGIN_OK");
			g_free (msg);
			continue;
		}

        if (!message->is_prompt)
        {
			post_login (window);

			/* FIXME: this doesn't show multiple messages, but that was
			 * already the case before. */
			if (priv->changing_password) {
				if (priv->pw_dialog) {
					greeter_password_settings_dialog_set_message_label (GREETER_PASSWORD_SETTINGS_DIALOG (priv->pw_dialog), message->text);
				}
			} else {
				show_login_error_dialog (window, NULL, message->text);
			}
			continue;
        }

        if (priv->changing_password) {
			post_login (window);

			const gchar *title;
			const gchar *prompt_label;

			/* for pam-gooroom and Linux-PAM, libpwquality */
			if ((strstr (message->text, "Current password: ") != NULL) ||
					(strstr (message->text, _("Current password: ")) != NULL)) {
				priv->changing_password_step = 1;
				title = _("Changing Password - [Step 1]");
				prompt_label = _("Enter current password :");
			} else if ((strstr (message->text, "New password: ") != NULL) ||
					(strstr (message->text, _("New password: ")) != NULL)) {
				priv->changing_password_step = 2;
				title = _("Changing Password - [Step 2]");
				prompt_label = _("Enter new password :");
			} else if ((strstr (message->text, "Retype new password: ") != NULL) ||
					(strstr (message->text, _("Retype new password: ")) != NULL)) {
				priv->changing_password_step = 3;
				title = _("Changing Password - [Step 3]");
				prompt_label = _("Retype new password :");
			} else {
				title = NULL;
				prompt_label = NULL;
			}

			greeter_password_settings_dialog_set_title (GREETER_PASSWORD_SETTINGS_DIALOG (priv->pw_dialog), title);
			greeter_password_settings_dialog_set_prompt_label (GREETER_PASSWORD_SETTINGS_DIALOG (priv->pw_dialog), prompt_label);
			greeter_password_settings_dialog_set_entry_text (GREETER_PASSWORD_SETTINGS_DIALOG (priv->pw_dialog), "");
			greeter_password_settings_dialog_grab_entry_focus (GREETER_PASSWORD_SETTINGS_DIALOG (priv->pw_dialog));
		}

		priv->prompted = TRUE;
		priv->prompt_active = TRUE;

        /* If we have more stuff after a prompt, assume that other prompts are pending,
         * so stop here. */
        break;
    }
}

static void
start_session (GreeterWindow *window)
{
	GreeterWindowPrivate *priv = window->priv;
	LightDMGreeter *greeter = priv->lightdm;

	if (priv->current_language)
#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
		lightdm_greeter_set_language (greeter, priv->current_language, NULL);
#else
		lightdm_greeter_set_language (greeter, priv->current_language);
#endif

//	greeter_background_save_xroot (greeter_background);

	if (!lightdm_greeter_start_session_sync (greeter, priv->current_session, NULL)) {
		run_warning_dialog (window, NULL, _("Failed to start session"), NULL);
		start_authentication (window, lightdm_greeter_get_authentication_user (greeter));
	}
}

static void
show_prompt_cb (LightDMGreeter    *greeter,
                const gchar       *text,
                LightDMPromptType  type,
                gpointer           user_data)
{
	GreeterWindow *window = GREETER_WINDOW (user_data);
	GreeterWindowPrivate *priv = window->priv;

	PAMConversationMessage *message_obj = g_new (PAMConversationMessage, 1);
	if (message_obj)
	{
		message_obj->is_prompt = TRUE;
		message_obj->type.prompt = type;
		message_obj->text = g_strdup (text);
		priv->pending_questions = g_slist_append (priv->pending_questions, message_obj);
	}

	if (!priv->prompt_active)
		process_prompts (window);
}

static void
show_message_cb (LightDMGreeter     *greeter,
                 const gchar        *text,
                 LightDMMessageType  type,
                 gpointer            user_data)
{
	GreeterWindow *window = GREETER_WINDOW (user_data);
	GreeterWindowPrivate *priv = window->priv;

    PAMConversationMessage *message_obj = g_new (PAMConversationMessage, 1);
    if (message_obj)
    {
        message_obj->is_prompt = FALSE;
        message_obj->type.message = type;
        message_obj->text = g_strdup (text);
        priv->pending_questions = g_slist_append (priv->pending_questions, message_obj);
    }

    if (!priv->prompt_active)
        process_prompts (window);
}

static void
authentication_complete_cb (LightDMGreeter *greeter,
                            gpointer        user_data)
{
	GreeterWindow *window = GREETER_WINDOW (user_data);
	GreeterWindowPrivate *priv = window->priv;

	post_login (window);

	priv->prompt_active = FALSE;

	if (priv->pending_questions) {
		g_slist_free_full (priv->pending_questions, (GDestroyNotify) pam_message_finalize);
		priv->pending_questions = NULL;
	}

	if (lightdm_greeter_get_is_authenticated (greeter)) {
		if (priv->pw_dialog) {
			gtk_widget_destroy (priv->pw_dialog);
			priv->pw_dialog = NULL;
		}
		start_session (window);
	} else {
		if (priv->changing_password) {
			gchar *msg = NULL;

			// remove password settings dialog
			if (priv->pw_dialog) {
				gtk_widget_destroy (priv->pw_dialog);
				priv->pw_dialog = NULL;
			}

			if (priv->changing_password_step == 1) {
				msg = _("Changing password is terminated because\n"
                        "the current password does not match.\n"
                        "Please try again later.");
			} else if (priv->changing_password_step == 2) {
				msg = _("New password violates the security conformity,\n"
                        "so the change of the password is terminated.\n"
                        "Please try again later.");
			} else if (priv->changing_password_step == 3) {
				msg = _("In Confirm New Password, the password did not match,\n"
                        "so the change of password is terminated.\n"
                        "Please try again later.");
			}
			run_warning_dialog (window, NULL, msg, "CHPASSWD_FAILURE_OK");
			return;
		}

		/* If an error message is already printed we do not print it this statement
		 * The error message probably comes from the PAM module that has a better knowledge
		 * of the failure. */
		if (!priv->have_pam_error) {
			show_login_error_dialog (window,
                                     NULL,
                                     _("Authentication Failure\n"
                                       "Please check the username and password and try again."));
		}
	}
}

//static void
//timed_autologin_cb (LightDMGreeter *greeter)
//{
//    /* Don't trigger autologin if user locks screen with light-locker (thanks to Andrew P.). */
//    if (!lightdm_greeter_get_lock_hint (greeter))
//    {
//        if (lightdm_greeter_get_is_authenticated (greeter))
//        {
//            /* Configured autologin user may be already selected in user list. */
//            if (lightdm_greeter_get_authentication_user (greeter))
//                /* Selected user matches configured autologin-user option. */
//                start_session ();
//            else if (lightdm_greeter_get_autologin_guest_hint (greeter))
//                /* "Guest session" is selected and autologin-guest is enabled. */
//                start_session ();
//            else if (lightdm_greeter_get_autologin_user_hint (greeter))
//            {
//                /* "Guest session" is selected, but autologin-user is configured. */
//                start_authentication (lightdm_greeter_get_autologin_user_hint (greeter));
//                prompted = TRUE;
//            }
//        }
//        else
//#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
//            lightdm_greeter_authenticate_autologin (greeter, NULL);
//#else
//            lightdm_greeter_authenticate_autologin (greeter);
//#endif
//    }
//}

static void
try_to_login_system (GreeterWindow *window)
{
	gchar *id = NULL, *pw = NULL;
	GreeterWindowPrivate *priv = window->priv;

	id = get_id (priv->id_entry);
	pw = g_strdup (gtk_entry_get_text (GTK_ENTRY (priv->pw_entry)));

	if (strlen (id) == 0)
		goto out;

	start_authentication (window, id);

	while (!priv->prompted)
		gtk_main_iteration ();

	priv->prompt_active = FALSE;


	if (lightdm_greeter_get_in_authentication (priv->lightdm)) {
#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
		lightdm_greeter_respond (priv->lightdm, pw, NULL);
#else
		lightdm_greeter_respond (priv->lightdm, pw);
#endif
        /* If we have questions pending, then we continue processing
         * those, until we are done. (Otherwise, authentication will
         * not complete.) */
		if (priv->pending_questions) {
			process_prompts (window);
		}
	}

out:
	g_free (id);
	g_free (pw);
}

static void
login_button_clicked_cb (GtkButton *widget,
                         gpointer   user_data)
{
	GreeterWindow *window = GREETER_WINDOW (user_data);
	GreeterWindowPrivate *priv = window->priv;

	pre_login (window);

	g_clear_pointer (&priv->id, g_free);
	g_clear_pointer (&priv->pw, g_free);

	priv->id = g_strdup (gtk_entry_get_text (GTK_ENTRY (priv->id_entry)));
	priv->pw = g_strdup (gtk_entry_get_text (GTK_ENTRY (priv->pw_entry)));

	try_to_login_system (window);
}

static void
pw_entry_activate_cb (GtkWidget *widget,
                      gpointer   user_data)
{
	GreeterWindow *window = GREETER_WINDOW (user_data);

	if (gtk_widget_get_sensitive (window->priv->login_button))
		login_button_clicked_cb (GTK_BUTTON (window->priv->login_button), window);
}

static gboolean
id_entry_key_press_cb (GtkWidget   *widget,
                       GdkEventKey *event,
                       gpointer     user_data)
{
	GreeterWindow *window = GREETER_WINDOW (user_data);
	GreeterWindowPrivate *priv = window->priv;

	/* Enter activates the password entry */
	if ((event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_Tab) &&
         gtk_widget_get_visible (priv->pw_entry))
	{
		gtk_widget_grab_focus (priv->pw_entry);
		return TRUE;
	} else {
		return FALSE;
	}
}

static void
id_entry_changed_cb (GtkWidget *widget,
                     gpointer   user_data)
{
	const gchar *text;
	GreeterWindow *window = GREETER_WINDOW (user_data);
	GreeterWindowPrivate *priv = window->priv;

	text = gtk_entry_get_text (GTK_ENTRY (priv->id_entry));

	gtk_widget_set_sensitive (priv->login_button, strlen (text) > 0);
}

static void
on_indicator_button_toggled_cb (GtkToggleButton *button, gpointer user_data)
{
	if (!gtk_toggle_button_get_active (button))
		return;

	IndicatorObjectEntry *entry;

	XfceIndicatorButton *indicator_button = XFCE_INDICATOR_BUTTON (button);

	entry = xfce_indicator_button_get_entry (indicator_button);

	GList *l = NULL;
	GList *children = gtk_container_get_children (GTK_CONTAINER (entry->menu));
	for (l = children; l; l = l->next) {
		GtkWidget *item = GTK_WIDGET (l->data);
		if (item) {
			g_signal_emit_by_name (item, "activate");
			break;
		}
	}

	g_list_free (children);

	g_signal_handlers_block_by_func (button, on_indicator_button_toggled_cb, user_data);
	gtk_toggle_button_set_active (button, FALSE);
	g_signal_handlers_unblock_by_func (button, on_indicator_button_toggled_cb, user_data);
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
entry_removed (IndicatorObject *io, IndicatorObjectEntry *entry, gpointer user_data)
{
	g_return_if_fail (entry != NULL);

	GList *children, *l = NULL;
	GreeterWindow *window = GREETER_WINDOW (user_data);

	children = gtk_container_get_children (GTK_CONTAINER (window->priv->indicator_box));
	for (l = children; l; l = l->next) {
		XfceIndicatorButton *child = XFCE_INDICATOR_BUTTON (l->data);
		if (child && (xfce_indicator_button_get_entry (child) == entry)) {
			xfce_indicator_button_destroy (child);
			break;
		}
	}

	g_list_free (children);
}

static void
entry_added (IndicatorObject *io, IndicatorObjectEntry *entry, gpointer user_data)
{
	g_return_if_fail (entry != NULL);

	GreeterWindow *window = GREETER_WINDOW (user_data);

	if ((g_strcmp0 (entry->name_hint, "nm-applet") == 0) ||
        (find_app_indicators (entry->name_hint)))
	{
		GtkWidget *button;
		const gchar *io_name;

		io_name = g_object_get_data (G_OBJECT (io), "io-name");

		button = xfce_indicator_button_new (io, io_name, entry);
		gtk_box_pack_start (GTK_BOX (window->priv->indicator_box), button, FALSE, FALSE, 0);

		if (entry->image != NULL)
			xfce_indicator_button_set_image (XFCE_INDICATOR_BUTTON (button), entry->image);

		if (entry->label != NULL)
			xfce_indicator_button_set_label (XFCE_INDICATOR_BUTTON (button), entry->label);

		if (g_strcmp0 (entry->name_hint, "gooroom-notice-applet") == 0) {
			g_signal_connect (G_OBJECT (button), "toggled", G_CALLBACK (on_indicator_button_toggled_cb), user_data);
		} else {
			if (entry->menu != NULL)
				xfce_indicator_button_set_menu (XFCE_INDICATOR_BUTTON (button), entry->menu);
		}

		gtk_widget_show (button);
	}
}

static void
load_module (const gchar *name, GreeterWindow *window)
{
	gchar                *path;
	IndicatorObject      *io;
	GList                *entries, *l = NULL;

	path = g_build_filename (INDICATOR_DIR, name, NULL);
	io = indicator_object_new_from_file (path);
	g_free (path);

	g_object_set_data (G_OBJECT (io), "io-name", g_strdup (name));

	g_signal_connect (G_OBJECT (io),
                      INDICATOR_OBJECT_SIGNAL_ENTRY_ADDED, G_CALLBACK (entry_added), window);
	g_signal_connect (G_OBJECT (io),
                      INDICATOR_OBJECT_SIGNAL_ENTRY_REMOVED, G_CALLBACK (entry_removed), window);

	entries = indicator_object_get_entries (io);

	for (l = entries; l; l = l->next) {
		IndicatorObjectEntry *ioe = (IndicatorObjectEntry *)l->data;
		entry_added (io, ioe, NULL);
	}

	g_list_free (entries);
}

static void
load_application_indicator (GreeterWindow *window)
{
	/* load application indicator */
	if (g_file_test (INDICATOR_DIR, (G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))) {
		GDir *dir = g_dir_open (INDICATOR_DIR, 0, NULL);

		const gchar *name;
		while ((name = g_dir_read_name (dir)) != NULL) {
			if (!name || !g_str_has_suffix (name, G_MODULE_SUFFIX))
				continue;

			if (!g_str_equal (name, "libayatana-application.so"))
				continue;

			load_module (name, window);
		}
		g_dir_close (dir);
	}
}

static gchar *
get_battery_icon_name (double percentage, UpDeviceState state)
{
	gchar *icon_name = NULL;
	const gchar *bat_state;

	switch (state)
	{
		case UP_DEVICE_STATE_CHARGING:
		case UP_DEVICE_STATE_PENDING_CHARGE:
			bat_state = "-charging";
			break;

		case UP_DEVICE_STATE_DISCHARGING:
		case UP_DEVICE_STATE_PENDING_DISCHARGE:
			bat_state = "";
			break;

		case UP_DEVICE_STATE_FULLY_CHARGED:
			bat_state = "-charged";
			break;

		case UP_DEVICE_STATE_EMPTY:
			return g_strdup ("battery-empty");

		default:
			bat_state = NULL;
			break;
	}
	if (!bat_state) {
		return g_strdup ("battery-error");
	}

	if (percentage >= 75) {
		icon_name = g_strdup_printf ("battery-full%s", bat_state);
	} else if (percentage >= 50 && percentage < 75) {
		icon_name = g_strdup_printf ("battery-good%s", bat_state);
	} else if (percentage >= 25 && percentage < 50) {
		icon_name = g_strdup_printf ("battery-medium%s", bat_state);
	} else if (percentage >= 10 && percentage < 25) {
		icon_name = g_strdup_printf ("battery-low%s", bat_state);
	} else {
		icon_name = g_strdup_printf ("battery-caution%s", bat_state);
	}

	return icon_name;
}

static void
on_power_device_changed_cb (UpDevice *device, GParamSpec *pspec, gpointer user_data)
{
	GtkImage *bat_tray = GTK_IMAGE (user_data);

	gchar *icon_name;
	gdouble percentage;
	UpDeviceState state;

	g_object_get (device,
                  "state", &state,
                  "percentage", &percentage,
                  NULL);

/* Sometimes the reported state is fully charged but battery is at 99%,
 * refusing to reach 100%. In these cases, just assume 100%.
 */
	if (state == UP_DEVICE_STATE_FULLY_CHARGED &&
			(100.0 - percentage <= 1.0))
		percentage = 100.0;

	icon_name = get_battery_icon_name (percentage, state);

	gtk_image_set_from_icon_name (bat_tray,
                                  icon_name,
                                  GTK_ICON_SIZE_BUTTON);

	gtk_image_set_pixel_size (bat_tray, 22);

	g_free (icon_name);
}

static void
updevice_added_cb (UpDevice *device, GreeterWindow *window)
{
	gboolean is_present = FALSE;
	guint device_type = UP_DEVICE_KIND_UNKNOWN;

	g_object_get (device, "kind", &device_type, NULL);
	g_object_get (device, "is-present", &is_present, NULL);

	if (device_type == UP_DEVICE_KIND_BATTERY && is_present) {
		GtkWidget *image = gtk_image_new_from_icon_name ("battery-full-symbolic", GTK_ICON_SIZE_BUTTON);
		gtk_box_pack_start (GTK_BOX (window->priv->indicator_box), image, FALSE, FALSE, 0);
		gtk_widget_show (image);

		g_object_set_data (G_OBJECT (image), "updevice", device);

		on_power_device_changed_cb (device, NULL, image);
		g_signal_connect (device, "notify", G_CALLBACK (on_power_device_changed_cb), image);
	}
}

static void
up_client_device_added_cb (UpClient *upclient,
                           UpDevice *device,
                           gpointer  user_data)
{
	updevice_added_cb (device, GREETER_WINDOW (user_data));
}

static void
up_client_device_removed_cb (UpClient *upclient,
                             UpDevice *removed_device,
                             gpointer  user_data)
{
	GList *children, *l;
	GreeterWindow *window = GREETER_WINDOW (user_data);

	if (!removed_device)
		return;

	children = gtk_container_get_children (GTK_CONTAINER (window->priv->indicator_box));

	for (l = children; l; l = l->next) {
		GtkWidget *child = GTK_WIDGET (l->data);
		UpDevice *device = (UpDevice*)g_object_get_data (G_OBJECT (child), "updevice");

		if (device && removed_device && device == removed_device) {
			gtk_container_remove (GTK_CONTAINER (window->priv->indicator_box), child);
			gtk_widget_destroy (child);
			break;
		}
	}
}

static void
load_battery_indicator (GreeterWindow *window)
{
	guint i;
	GreeterWindowPrivate *priv = window->priv;

	priv->up_client = up_client_new ();
	priv->devices = up_client_get_devices2 (priv->up_client);

	if (priv->devices) {
		for (i = 0; i < priv->devices->len; i++) {
			UpDevice *device = g_ptr_array_index (priv->devices, i);
			updevice_added_cb (device, window);
		}
	}

	g_signal_connect (priv->up_client, "device-added",
                      G_CALLBACK (up_client_device_added_cb), window);
	g_signal_connect (priv->up_client, "device-removed",
                      G_CALLBACK (up_client_device_removed_cb), window);
}

static void
menu_size_allocate_cb (GtkWidget     *widget,
                       GtkAllocation *allocation,
                       gpointer       user_data)
{
	gtk_menu_reposition (GTK_MENU (widget));
}

static void
on_menu_item_activated (GtkMenuItem *item, gpointer user_data)
{
	gboolean active;
	GdkMonitor *monitor;
	GdkRectangle geometry;
	GreeterWindow *window = GREETER_WINDOW (user_data);

	monitor = (GdkMonitor *)g_object_get_data (G_OBJECT (item), "monitor");
	gdk_monitor_get_geometry (monitor, &geometry);

	active = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (item));
	if (active) {
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), TRUE);

		g_signal_emit (G_OBJECT (window), signals[POSITION_CHANGED], 0, &geometry);
	}
}

static void
switch_indicator_button_clicked_cb (GtkButton *button,
                                    gpointer   user_data)
{
	GSList *group = NULL;
	GdkDisplay *display;
	GdkRectangle geometry;
	gint m, monitors = 0;
	gint x, y, width, height;
	GtkWidget *toplevel, *menu, *menuitem;
	cairo_region_t *region;
	GreeterWindow *window = GREETER_WINDOW (user_data);

	region = cairo_region_create ();
	display = gdk_display_get_default ();
	monitors = gdk_display_get_n_monitors (display);
	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (window));

	gtk_widget_get_size_request (GTK_WIDGET (toplevel), &width, &height);
	gdk_window_get_origin (gtk_widget_get_window (GTK_WIDGET (toplevel)), &x, &y);

	menu = gtk_menu_new ();

	for (m = 0; m < monitors; m++) {
		GdkMonitor *monitor;
		gchar *print_name = NULL;
		const gchar *name, *monitor_name;

		monitor = gdk_display_get_monitor (display, m);
		gdk_monitor_get_geometry (monitor, &geometry);
		name = gdk_monitor_get_model (monitor);

		if (cairo_region_contains_rectangle (region, &geometry) == CAIRO_REGION_OVERLAP_IN)
			continue;

		cairo_region_union_rectangle (region, &geometry);
		monitor_name = name ? name : "<unknown>";

		print_name = g_strdup_printf ("%d.   %s (%d X %d)", m+1, monitor_name,
                                      geometry.width, geometry.height);

		menuitem = gtk_radio_menu_item_new_with_label (group, print_name);
		group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (menuitem));
		g_object_set_data (G_OBJECT (menuitem), "monitor", monitor);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
		gtk_widget_show_all (menuitem);

		if (x == geometry.x && y == geometry.y &&
            width == geometry.width && height == geometry.height) {
			gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (menuitem), TRUE);
		}

		g_signal_connect (menuitem, "activate", G_CALLBACK (on_menu_item_activated), window);

		g_clear_pointer (&print_name, g_free);
	}

	gtk_menu_popup_at_widget (GTK_MENU (menu),
                              GTK_WIDGET (button),
                              GDK_GRAVITY_NORTH_WEST,
                              GDK_GRAVITY_SOUTH_WEST,
                              gtk_get_current_event ());

	g_signal_connect (G_OBJECT (menu), "size-allocate",
                      G_CALLBACK (menu_size_allocate_cb), NULL);
}

static void
load_switch_greeter_window_indicator (GreeterWindow *window)
{
	GtkWidget *icon;
	GtkStyleContext *style;
	GreeterWindowPrivate *priv = window->priv;

	priv->switch_indicator = gtk_button_new ();
	gtk_widget_set_can_focus (GTK_WIDGET (priv->switch_indicator), FALSE);
	gtk_widget_set_can_default (GTK_WIDGET (priv->switch_indicator), FALSE);
	gtk_button_set_relief (GTK_BUTTON (priv->switch_indicator), GTK_RELIEF_NONE);
	gtk_widget_set_focus_on_click (GTK_WIDGET (priv->switch_indicator), FALSE);

	icon = gtk_image_new_from_icon_name ("view-paged-symbolic", GTK_ICON_SIZE_BUTTON);
	gtk_container_add (GTK_CONTAINER (priv->switch_indicator), icon);

	gtk_box_pack_start (GTK_BOX (priv->indicator_box), priv->switch_indicator, FALSE, FALSE, 0);

	style = gtk_widget_get_style_context (GTK_WIDGET (priv->switch_indicator));
	gtk_style_context_add_class (style, "indicator-button");

	g_signal_connect (G_OBJECT (priv->switch_indicator), "clicked",
                      G_CALLBACK (switch_indicator_button_clicked_cb), window);

	gtk_widget_show_all (priv->switch_indicator);
}

/*
 * Translate @str according to the locale defined by LC_TIME; unlike
 * dcgettext(), the translation is still taken from the LC_MESSAGES
 * catalogue and not the LC_TIME one.
 */
static const gchar *
translate_time_format_string (const char *str)
{
	const char *locale = g_getenv ("LC_TIME");
	const char *res;
	char *sep;
	locale_t old_loc;
	locale_t loc = (locale_t)0;

	if (locale)
		loc = newlocale (LC_MESSAGES_MASK, locale, (locale_t)0);

	old_loc = uselocale (loc);

	sep = strchr (str, '\004');
	res = g_dpgettext (GETTEXT_PACKAGE, str, sep ? sep - str + 1 : 0);

	uselocale (old_loc);

	if (loc != (locale_t)0)
		freelocale (loc);

	return res;
}

/* Clock */
static gboolean
clock_timeout_thread (gpointer data)
{
	GtkLabel *clock_label = GTK_LABEL (data);

	GDateTime *dt = NULL;

	dt = g_date_time_new_now_local ();
	if (dt) {
		gchar *fm = g_date_time_format (dt, translate_time_format_string (N_("%B %-d %Y  %l:%M %p")));
		gchar *markup = g_markup_printf_escaped ("<b><span foreground=\"white\">%s</span></b>", fm);
		gtk_label_set_markup (GTK_LABEL (clock_label), markup);
		g_free (fm);
		g_free (markup);

		g_date_time_unref (dt);
	}

	return TRUE;
}

static void
load_clock_indicator (GreeterWindow *window)
{
	GtkStyleContext *style;
	GtkWidget *clock_label;
	GreeterWindowPrivate *priv = window->priv;

	clock_label = gtk_label_new ("");
	style = gtk_widget_get_style_context (GTK_WIDGET (clock_label));
	gtk_style_context_add_class (style, "clock-label");
	gtk_box_pack_start (GTK_BOX (priv->indicator_box), clock_label, FALSE, FALSE, 0);
	gtk_widget_show_all (clock_label);

	/* update clock */
	clock_timeout_thread (clock_label);
	gdk_threads_add_timeout (1000, (GSourceFunc) clock_timeout_thread, clock_label);
}

static void
network_indicator_application_start (void)
{
	const gchar *cmd;
	gchar **argv = NULL, **envp = NULL;

	cmd = "/usr/bin/gsettings set org.gnome.nm-applet disable-connected-notifications true";
	g_spawn_command_line_sync (cmd, NULL, NULL, NULL, NULL);

	cmd = "/usr/bin/gsettings set org.gnome.nm-applet disable-disconnected-notifications true";
	g_spawn_command_line_sync (cmd, NULL, NULL, NULL, NULL);

	cmd = "/usr/bin/gsettings set org.gnome.nm-applet suppress-wireless-networks-available true";
	g_spawn_command_line_sync (cmd, NULL, NULL, NULL, NULL);

	cmd = "nm-applet --indicator";
	g_shell_parse_argv (cmd, NULL, &argv, NULL);

	envp = g_get_environ ();

	g_spawn_async (NULL, argv, envp, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);

	g_strfreev (argv);
	g_strfreev (envp);
}

static void
other_indicator_application_start (void)
{
	gchar **envp = NULL;
	gchar **app_indicators = config_get_string_list (NULL, "app-indicators", NULL);

	if (!app_indicators)
		return;

	envp = g_get_environ ();

	guint i;
	for (i = 0; app_indicators[i] != NULL; i++) {
		gchar **argv = NULL;
		g_shell_parse_argv (app_indicators[i], NULL, &argv, NULL);
		g_spawn_async (NULL, argv, envp, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
		g_strfreev (argv);
	}

	g_strfreev (envp);
	g_strfreev (app_indicators);
}

static void
load_indicators (GreeterWindow *window)
{
	load_clock_indicator (window);
	load_battery_indicator (window);
	load_application_indicator (window);
	load_switch_greeter_window_indicator (window);

	network_indicator_application_start ();
	other_indicator_application_start ();
}

static void
show_command_dialog (GtkWidget *parent,
                     const gchar* icon,
                     const gchar* title,
                     const gchar* message,
                     int          type)
{
	GList *items, *l = NULL;
	gint res, logged_in_users = 0;
	gchar *new_message = NULL;
	GtkWidget *dialog, *toplevel;

	items = lightdm_user_list_get_users (lightdm_user_list_get_instance ());
	for (l = items; l; l = l->next) {
		LightDMUser *user = l->data;
		if (lightdm_user_get_logged_in (user))
			logged_in_users++;
	}

	/* Check if there are still users logged in, count them and if so, display a warning */
	if (logged_in_users > 0) {
		gchar *warning = g_strdup_printf (ngettext ("Warning: There is still %d user logged in.",
                                          "Warning: There are still %d users logged in.",
                                          logged_in_users), logged_in_users);

		new_message = g_strdup_printf ("%s\n%s", warning, message);
		g_free (warning);
	} else {
		new_message = g_strdup (message);
	}

	toplevel = gtk_widget_get_toplevel (parent);

	dialog = greeter_message_dialog_new (GTK_WINDOW (toplevel),
                                         icon,
                                         title,
                                         new_message);

	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                            _("Ok"), GTK_RESPONSE_OK,
                            _("Cancel"), GTK_RESPONSE_CANCEL,
                            NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);

	gtk_widget_show (dialog);
	res = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	g_free (new_message);

	if (res != GTK_RESPONSE_OK)
		return;

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

static void
shutdown_button_clicked_cb (GtkButton *button, gpointer user_data)
{
	const char *img, *title, *msg;
	GreeterWindow *window = GREETER_WINDOW (user_data);

	img = "system-shutdown-symbolic";
	title = _("System Shutdown");
	msg = _("Are you sure you want to close all programs and shut down the computer?");

	show_command_dialog (GTK_WIDGET (window), img, title, msg, SYSTEM_SHUTDOWN);
}

static void
restart_button_clicked_cb (GtkButton *button, gpointer user_data)
{
	const char *img, *title, *msg;
	GreeterWindow *window = GREETER_WINDOW (user_data);

	img = "system-restart-symbolic";
	title = _("System Restart");
	msg = _("Are you sure you want to close all programs and restart the computer?");

	show_command_dialog (GTK_WIDGET (window), img, title, msg, SYSTEM_RESTART);
}

static void
suspend_button_clicked_cb (GtkButton *button, gpointer user_data)
{
	const char *img, *title, *msg;
	GreeterWindow *window = GREETER_WINDOW (user_data);

	img = "system-suspend-symbolic";
	title = _("System Suspend");
	msg = _("Are you sure you want to suspend the computer?");

	show_command_dialog (GTK_WIDGET (window), img, title, msg, SYSTEM_SUSPEND);
}

static void
hibernate_button_clicked_cb (GtkButton *button, gpointer user_data)
{
	const char *img, *title, *msg;
	GreeterWindow *window = GREETER_WINDOW (user_data);

	img = "system-hibernate-symbolic";
	title = _("System Hibernate");
	msg = _("Are you sure you want to hibernate the computer?");

	show_command_dialog (GTK_WIDGET (window), img, title, msg, SYSTEM_HIBERNATE);
}

static void
load_power_command (GreeterWindow *window)
{
	GreeterWindowPrivate *priv = window->priv;

	gtk_widget_set_visible (priv->btn_shutdown, lightdm_get_can_shutdown ());
	gtk_widget_set_visible (priv->btn_restart, lightdm_get_can_restart ());
	gtk_widget_set_visible (priv->btn_suspend, lightdm_get_can_suspend ());
	gtk_widget_set_visible (priv->btn_hibernate, lightdm_get_can_hibernate ());

	g_signal_connect (G_OBJECT (priv->btn_shutdown), "clicked",
                      G_CALLBACK (shutdown_button_clicked_cb), window);
	g_signal_connect (G_OBJECT (priv->btn_restart), "clicked",
                      G_CALLBACK (restart_button_clicked_cb), window);
	g_signal_connect (G_OBJECT (priv->btn_suspend), "clicked",
                      G_CALLBACK (suspend_button_clicked_cb), window);
	g_signal_connect (G_OBJECT (priv->btn_hibernate), "clicked",
                      G_CALLBACK (hibernate_button_clicked_cb), window);
}

static void
lightdm_greeter_init (GreeterWindow *window)
{
	GreeterWindowPrivate *priv = window->priv;

	priv->lightdm = lightdm_greeter_new ();

	g_signal_connect (priv->lightdm, "show-prompt", G_CALLBACK (show_prompt_cb), window);
	g_signal_connect (priv->lightdm, "show-message", G_CALLBACK (show_message_cb), window);
	g_signal_connect (priv->lightdm, "authentication-complete",
                      G_CALLBACK (authentication_complete_cb), window);
//	g_signal_connect (greeter, "autologin-timer-expired", G_CALLBACK (timed_autologin_cb), window);

	/* set default session */
	set_session (window, lightdm_greeter_get_default_session_hint (priv->lightdm));

	lightdm_greeter_connect_sync (priv->lightdm, NULL);
}

static void
greeter_window_finalize (GObject *object)
{
	GreeterWindow *window = GREETER_WINDOW (object);
	GreeterWindowPrivate *priv = window->priv;
 
	g_clear_pointer (&priv->devices, g_ptr_array_unref);
	g_clear_object (&priv->up_client);

	g_clear_pointer (&priv->id, g_free);
	g_clear_pointer (&priv->pw, g_free);
	g_clear_pointer (&priv->current_session, g_free);
	g_clear_pointer (&priv->current_language, g_free);

	if (priv->pending_questions) {
		g_slist_free_full (priv->pending_questions, (GDestroyNotify) pam_message_finalize);
		priv->pending_questions = NULL;
	}

	G_OBJECT_CLASS (greeter_window_parent_class)->finalize (object);
}

static void
greeter_window_init (GreeterWindow *window)
{
	GreeterWindowPrivate *priv;
	priv = window->priv = greeter_window_get_instance_private (window);

	gtk_widget_init_template (GTK_WIDGET (window));

	priv->prompted = FALSE;
	priv->prompt_active = FALSE;
	priv->have_pam_error = FALSE;
	priv->changing_password = FALSE;
	priv->pending_questions = NULL;
	priv->current_session = NULL;
	priv->current_language = NULL;
	priv->id = NULL;
	priv->pw = NULL;
	priv->devices = NULL;
	priv->up_client = NULL;
	priv->changing_password_step = 0;

	lightdm_greeter_init (window);

	load_power_command (window);
	load_indicators (window);

	gtk_widget_set_sensitive (priv->login_button, FALSE);

	g_signal_connect (priv->id_entry, "changed", G_CALLBACK (id_entry_changed_cb), window);
	g_signal_connect (priv->id_entry, "key-press-event", G_CALLBACK (id_entry_key_press_cb), window);
	g_signal_connect (priv->pw_entry, "activate", G_CALLBACK (pw_entry_activate_cb), window);
	g_signal_connect (priv->login_button, "clicked", G_CALLBACK (login_button_clicked_cb), window);

	g_idle_add ((GSourceFunc)grab_focus_idle, priv->id_entry);
}

static void
greeter_window_class_init (GreeterWindowClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass),
                                                 "/kr/gooroom/greeter/greeter-window.ui");

	object_class->finalize = greeter_window_finalize;

	signals[POSITION_CHANGED] =
		g_signal_new ("position-changed",
                      G_TYPE_FROM_CLASS(object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (GreeterWindowClass, position_changed),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1,
                      GDK_TYPE_RECTANGLE);

	gtk_widget_class_bind_template_child_private (widget_class, GreeterWindow, spinner);
	gtk_widget_class_bind_template_child_private (widget_class, GreeterWindow, id_entry);
	gtk_widget_class_bind_template_child_private (widget_class, GreeterWindow, pw_entry);
	gtk_widget_class_bind_template_child_private (widget_class, GreeterWindow, login_button);
	gtk_widget_class_bind_template_child_private (widget_class, GreeterWindow, panel_box);
	gtk_widget_class_bind_template_child_private (widget_class, GreeterWindow, indicator_box);
	gtk_widget_class_bind_template_child_private (widget_class, GreeterWindow, btn_shutdown);
	gtk_widget_class_bind_template_child_private (widget_class, GreeterWindow, btn_restart);
	gtk_widget_class_bind_template_child_private (widget_class, GreeterWindow, btn_suspend);
	gtk_widget_class_bind_template_child_private (widget_class, GreeterWindow, btn_hibernate);
}

GtkWidget *
greeter_window_new (void)
{
	GObject *result;

	result = g_object_new (GREETER_TYPE_WINDOW, NULL);

	return GTK_WIDGET (result);
}

void
greeter_window_set_switch_indicator_visible (GreeterWindow *window, gboolean visible)
{
	if (visible) {
		gtk_widget_show_all (window->priv->switch_indicator);
	} else {
		gtk_widget_hide (window->priv->switch_indicator);
	}
}
