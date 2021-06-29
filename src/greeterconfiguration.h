/*
 * Copyright (C) 2015 - 2018, Sean Davis <smd.seandavis@gmail.com>
 * Copyright (C) 2015 - 2021 Gooroom <gooroom@gooroom.kr>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */


#ifndef _GREETER_CONFIGURATION_H_
#define _GREETER_CONFIGURATION_H_

#include <glib.h>


#define CONFIG_GROUP_DEFAULT            "greeter"
#define CONFIG_KEY_INDICATORS           "indicators"
#define CONFIG_KEY_DEBUGGING            "allow-debugging"
#define CONFIG_KEY_THEME                "theme-name"
#define CONFIG_KEY_ICON_THEME           "icon-theme-name"
#define CONFIG_KEY_FONT                 "font-name"
#define CONFIG_KEY_DPI                  "xft-dpi"
#define CONFIG_KEY_ANTIALIAS            "xft-antialias"
#define CONFIG_KEY_HINT_STYLE           "xft-hintstyle"
#define CONFIG_KEY_RGBA                 "xft-rgba"
#define CONFIG_KEY_KEYBOARD             "keyboard"
#define CONFIG_KEY_BACKGROUND           "background"
#define STATE_SECTION_GREETER           "/greeter"


void config_init                (void);

gchar** config_get_groups       (const gchar* prefix);
gboolean config_has_key         (const gchar* group, const gchar* key);

gchar* config_get_string        (const gchar* group, const gchar* key, const gchar* fallback);
void config_set_string          (const gchar* group, const gchar* key, const gchar* value);
gchar** config_get_string_list  (const gchar* group, const gchar* key, gchar** fallback);
gint config_get_int             (const gchar* group, const gchar* key, gint fallback);
void config_set_int             (const gchar* group, const gchar* key, gint value);
gboolean config_get_bool        (const gchar* group, const gchar* key, gboolean fallback);
void config_set_bool            (const gchar* group, const gchar* key, gboolean value);
gint config_get_enum            (const gchar* group, const gchar* key, gint fallback, const gchar* first_name, ...) G_GNUC_NULL_TERMINATED;

#endif
