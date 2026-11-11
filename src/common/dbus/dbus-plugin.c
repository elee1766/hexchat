/* dbus-plugin.c - hexchat plugin for remote access using D-Bus
 * Copyright (C) 2006 Claessens Xavier
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * Claessens Xavier
 * xclaesse@gmail.com
 */


#include <config.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include "hexchat-plugin.h"
#include "dbus-plugin.h"

#define PNAME _("remote access")
#define PDESC _("plugin for remote access using DBUS")
#define PVERSION ""

#define DBUS_OBJECT_PATH "/org/hexchat"

static hexchat_plugin *ph;
static guint last_context_id = 0;
static GList *contexts = NULL;
static GHashTable *clients = NULL;
static GDBusConnection *connection;

typedef struct RemoteObject RemoteObject;
typedef struct RemoteObjectClass RemoteObjectClass;

GType Remote_object_get_type (void);

struct RemoteObject
{
	GObject parent;

	guint last_hook_id;
	guint last_list_id;
	hexchat_context *context;
	char *dbus_path;
	guint registrations[2];
	char *filename;
	GHashTable *hooks;
	GHashTable *lists;
	void *handle;
};

struct RemoteObjectClass
{
	GObjectClass parent;
};

typedef struct 
{
	guint id;
	int return_value;
	hexchat_hook *hook;
	RemoteObject *obj;
} HookInfo;

typedef struct
{
	guint id;
	hexchat_context *context;
} ContextInfo;


#define REMOTE_TYPE_OBJECT              (remote_object_get_type ())
#define REMOTE_OBJECT(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), REMOTE_TYPE_OBJECT, RemoteObject))
#define REMOTE_OBJECT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), REMOTE_TYPE_OBJECT, RemoteObjectClass))
#define REMOTE_IS_OBJECT(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), REMOTE_TYPE_OBJECT))
#define REMOTE_IS_OBJECT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), REMOTE_TYPE_OBJECT))
#define REMOTE_OBJECT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), REMOTE_TYPE_OBJECT, RemoteObjectClass))
#define REMOTE_OBJECT_ERROR (remote_object_error_quark ())
#define REMOTE_TYPE_ERROR (remote_object_error_get_type ()) 

G_DEFINE_TYPE (RemoteObject, remote_object, G_TYPE_OBJECT)

/* Available services */

static gboolean		remote_object_connect		(RemoteObject *obj,
							 const char *filename,
							 const char *name,
							 const char *desc,
							 const char *version,
							 GDBusMethodInvocation *context);

static gboolean		remote_object_disconnect	(RemoteObject *obj,
							 GDBusMethodInvocation *context);

static gboolean		remote_object_command		(RemoteObject *obj,
							 const char *command,
							 GError **error);

static gboolean		remote_object_print		(RemoteObject *obj,
							 const char *text,
							 GError **error);

static gboolean		remote_object_find_context	(RemoteObject *obj,
							 const char *server,
							 const char *channel,
							 guint *ret_id,
							 GError **error);

static gboolean		remote_object_get_context	(RemoteObject *obj,
							 guint *ret_id,
							 GError **error);

static gboolean		remote_object_set_context	(RemoteObject *obj,
							 guint id,
							 gboolean *ret,
							 GError **error);

static gboolean		remote_object_print		(RemoteObject *obj,
							 const char *text,
							 GError **error);

static gboolean		remote_object_get_info		(RemoteObject *obj,
							 const char *id,
							 char **ret_info,
							 GError **error);

static gboolean		remote_object_get_prefs		(RemoteObject *obj,
							 const char *name,
							 int *ret_type,
							 char **ret_str,
							 int *ret_int,
							 GError **error);

static gboolean		remote_object_hook_command	(RemoteObject *obj,
							 const char *name,
							 int pri,
							 const char *help_text,
							 int return_value,
							 guint *ret_id,
							 GError **error);

static gboolean		remote_object_hook_server	(RemoteObject *obj,
							 const char *name,
							 int pri,
							 int return_value,
							 guint *ret_id,
							 GError **error);

static gboolean		remote_object_hook_print	(RemoteObject *obj,
							 const char *name,
							 int pri,
							 int return_value,
							 guint *ret_id,
							 GError **error);

static gboolean		remote_object_unhook		(RemoteObject *obj,
							 guint id,
							 GError **error);

static gboolean		remote_object_list_get		(RemoteObject *obj,
							 const char *name,
							 guint *ret_id,
							 GError **error);

static gboolean		remote_object_list_next		(RemoteObject *obj,
							 guint id,
							 gboolean *ret,
							 GError **error);

static gboolean		remote_object_list_str		(RemoteObject *obj,
							 guint id,
							 const char *name,
							 char **ret_str,
							 GError **error);

static gboolean		remote_object_list_int		(RemoteObject *obj,
							 guint id,
							 const char *name,
							 int *ret_int,
							 GError **error);

static gboolean		remote_object_list_time		(RemoteObject *obj,
							 guint id,
							 const char *name,
							 guint64 *ret_time,
							 GError **error);

static gboolean		remote_object_list_fields	(RemoteObject *obj,
							 const char *name,
							 char ***ret,
							 GError **error);

static gboolean		remote_object_list_free		(RemoteObject *obj,
							 guint id,
							 GError **error);

static gboolean		remote_object_emit_print	(RemoteObject *obj,
							 const char *event_name,
							 const char *args[],
							 gboolean *ret,
							 GError **error);

static gboolean		remote_object_nickcmp		(RemoteObject *obj,
							 const char *nick1,
							 const char *nick2,
							 int *ret,
							 GError **error);

static gboolean		remote_object_strip		(RemoteObject *obj,
							 const char *str,
							 int len,
							 int flag,
							 char **ret_str,
							 GError **error);

static gboolean		remote_object_send_modes	(RemoteObject *obj,
							 const char *targets[],
							 int modes_per_line,
							 char sign,
							 char mode,
							 GError **error);

#include "remote-object-xml.h"

static gboolean register_remote (RemoteObject *obj, GError **error);

/* Useful functions */

static char**		build_list			(char *word[]);
static guint		context_list_find_id		(hexchat_context *context);
static hexchat_context*	context_list_find_context	(guint id);

/* Remote Object */

static void
hook_info_destroy (gpointer data)
{
	HookInfo *info = (HookInfo*)data;

	if (info == NULL) {
		return;
	}
	hexchat_unhook (ph, info->hook);
	g_free (info);
}

static void
list_info_destroy (gpointer data)
{
	hexchat_list_free (ph, (hexchat_list*)data);
}

static void
remote_object_finalize (GObject *obj)
{
	RemoteObject *self = (RemoteObject*)obj;
	guint i;

	for (i = 0; i < G_N_ELEMENTS (self->registrations); i++)
		if (self->registrations[i])
			g_dbus_connection_unregister_object (connection, self->registrations[i]);
	g_hash_table_destroy (self->lists);
	g_hash_table_destroy (self->hooks);
	g_free (self->dbus_path);
	g_free (self->filename);
	if (self->handle)
		hexchat_plugingui_remove (ph, self->handle);

	G_OBJECT_CLASS (remote_object_parent_class)->finalize (obj);
}

static void
remote_object_init (RemoteObject *obj)
{
	obj->hooks =
		g_hash_table_new_full (g_int_hash,
				       g_int_equal,
				       NULL,
				       hook_info_destroy);

	obj->lists =
		g_hash_table_new_full (g_int_hash,
				       g_int_equal,
				       g_free,
				       list_info_destroy);
	obj->dbus_path = NULL;
	obj->filename = NULL;
	obj->last_hook_id = 0;
	obj->last_list_id = 0;
	obj->context = hexchat_get_context (ph);
}

static void
remote_object_class_init (RemoteObjectClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	gobject_class->finalize = remote_object_finalize;


}

/* Implementation of services */

static gboolean
remote_object_connect (RemoteObject *obj,
		       const char *filename,
		       const char *name,
		       const char *desc,
		       const char *version,
		       GDBusMethodInvocation *context)
{
	static guint count = 0;
	char *sender, *path;
	RemoteObject *remote_object;
	gchar count_buffer[15];
	
	sender = g_strdup (g_dbus_method_invocation_get_sender (context));
	remote_object = g_hash_table_lookup (clients, sender);
	if (remote_object != NULL) {
		g_dbus_method_invocation_return_value (context, g_variant_new ("(s)", remote_object->dbus_path));
		g_free (sender);
		return TRUE;
	}
	g_snprintf(count_buffer, sizeof(count_buffer), "%u", count++);
	path = g_build_filename (DBUS_OBJECT_PATH, count_buffer, NULL);
	remote_object = g_object_new (REMOTE_TYPE_OBJECT, NULL);
	remote_object->dbus_path = path;
	remote_object->filename = g_path_get_basename (filename);
	remote_object->handle = hexchat_plugingui_add (ph,
						     remote_object->filename,
						     name,
						     desc,
						     version, NULL);
	if (!register_remote (remote_object, NULL))
	{
		g_object_unref (remote_object);
		g_free (sender);
		g_dbus_method_invocation_return_error (context, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not register client");
		return TRUE;
	}

	g_hash_table_insert (clients,
			     sender,
			     remote_object);

	g_dbus_method_invocation_return_value (context, g_variant_new ("(s)", path));
	return TRUE;
}

static gboolean
remote_object_disconnect (RemoteObject *obj,
			  GDBusMethodInvocation *context)
{
	char *sender;
	
	sender = g_strdup (g_dbus_method_invocation_get_sender (context));
	g_hash_table_remove (clients, sender);
	g_free (sender);

	g_dbus_method_invocation_return_value (context, NULL);
	return TRUE;
}

static gboolean
remote_object_command (RemoteObject *obj,
		       const char *command,
		       GError **error)
{
	if (hexchat_set_context (ph, obj->context)) {
		hexchat_command (ph, command);
	}
	return TRUE;
}

static gboolean
remote_object_print (RemoteObject *obj,
		     const char *text,
		     GError **error)
{
	if (hexchat_set_context (ph, obj->context)) {
		hexchat_print (ph, text);
	}
	return TRUE;
}

static gboolean
remote_object_find_context (RemoteObject *obj,
			    const char *server,
			    const char *channel,
			    guint *ret_id,
			    GError **error)
{
	hexchat_context *context;

	if (*server == '\0') {
		server = NULL;
	}
	if (*channel == '\0') {
		channel = NULL;
	}
	context = hexchat_find_context (ph, server, channel);
	*ret_id = context_list_find_id (context);

	return TRUE;
}

static gboolean
remote_object_get_context (RemoteObject *obj,
			   guint *ret_id,
			   GError **error)
{
	*ret_id = context_list_find_id (obj->context);
	return TRUE;
}

static gboolean
remote_object_set_context (RemoteObject *obj,
			   guint id,
			   gboolean *ret,
			   GError **error)
{
	hexchat_context *context;
	
	context = context_list_find_context (id);
	if (context == NULL) {
		*ret = FALSE;
		return TRUE;
	}
	obj->context = context;
	*ret = TRUE;

	return TRUE;
}

static gboolean
remote_object_get_info (RemoteObject *obj,
			const char *id,
			char **ret_info,
			GError **error)
{
	/* win_ptr is a GtkWindow* casted to char* and will crash
	 * D-Bus if we send it as a string */
	if (!hexchat_set_context (ph, obj->context) ||
	    g_str_equal (id, "win_ptr")) {
		*ret_info = NULL;
		return TRUE;
	}
	*ret_info = g_strdup (hexchat_get_info (ph, id));
	return TRUE;
}

static gboolean
remote_object_get_prefs (RemoteObject *obj,
			 const char *name,
			 int *ret_type,
			 char **ret_str,
			 int *ret_int,
			 GError **error)
{
	const char *str = NULL;

	if (!hexchat_set_context (ph, obj->context)) {
		*ret_type = 0;
		return TRUE;
	}
	*ret_type = hexchat_get_prefs (ph, name, &str, ret_int);
	*ret_str = g_strdup (str);

	return TRUE;
}

static int
server_hook_cb (char *word[],
		char *word_eol[],
		void *userdata)
{
	HookInfo *info = (HookInfo*)userdata;
	char **arg1;
	char **arg2;

	arg1 = build_list (word + 1);
	arg2 = build_list (word_eol + 1);
	info->obj->context = hexchat_get_context (ph);
	g_dbus_connection_emit_signal (connection, NULL, info->obj->dbus_path,
		"org.hexchat.plugin", "ServerSignal",
		g_variant_new ("(^as^asuu)", arg1, arg2, info->id,
		context_list_find_id (info->obj->context)), NULL);
	g_strfreev (arg1);
	g_strfreev (arg2);
  
	return info->return_value;
}

static int
command_hook_cb (char *word[],
		 char *word_eol[],
		 void *userdata)
{
	HookInfo *info = (HookInfo*)userdata;
	char **arg1;
	char **arg2;

	arg1 = build_list (word + 1);
	arg2 = build_list (word_eol + 1);
	info->obj->context = hexchat_get_context (ph);
	g_dbus_connection_emit_signal (connection, NULL, info->obj->dbus_path,
		"org.hexchat.plugin", "CommandSignal",
		g_variant_new ("(^as^asuu)", arg1, arg2, info->id,
		context_list_find_id (info->obj->context)), NULL);
	g_strfreev (arg1);
	g_strfreev (arg2);
  
	return info->return_value;
}

static int
print_hook_cb (char *word[],
	       void *userdata)
{
	HookInfo *info = (HookInfo*)userdata;
	char **arg1;

	arg1 = build_list (word + 1);
	info->obj->context = hexchat_get_context (ph);
	g_dbus_connection_emit_signal (connection, NULL, info->obj->dbus_path,
		"org.hexchat.plugin", "PrintSignal",
		g_variant_new ("(^asuu)", arg1, info->id,
		context_list_find_id (info->obj->context)), NULL);
	g_strfreev (arg1);
  
	return info->return_value;
}

static gboolean
remote_object_hook_command (RemoteObject *obj,
			    const char *name,
			    int priority,
			    const char *help_text,
			    int return_value,
			    guint *ret_id,
			    GError **error)
{
	HookInfo *info;

	info = g_new0 (HookInfo, 1);
	info->obj = obj;
	info->return_value = return_value;
	info->id = ++obj->last_hook_id;
	info->hook = hexchat_hook_command (ph,
					 name,
					 priority,
					 command_hook_cb,
					 help_text,
					 info);
	g_hash_table_insert (obj->hooks, &info->id, info);
	*ret_id = info->id;

	return TRUE;
}

static gboolean
remote_object_hook_server (RemoteObject *obj,
			   const char *name,
			   int priority,
			   int return_value,
			   guint *ret_id,
			   GError **error)
{
	HookInfo *info;

	info = g_new0 (HookInfo, 1);
	info->obj = obj;
	info->return_value = return_value;
	info->id = ++obj->last_hook_id;
	info->hook = hexchat_hook_server (ph,
					name,
					priority,
					server_hook_cb,
					info);
	g_hash_table_insert (obj->hooks, &info->id, info);
	*ret_id = info->id;

	return TRUE;
}

static gboolean
remote_object_hook_print (RemoteObject *obj,
			  const char *name,
			  int priority,
			  int return_value,
			  guint *ret_id,
			  GError **error)
{
	HookInfo *info;

	info = g_new0 (HookInfo, 1);
	info->obj = obj;
	info->return_value = return_value;
	info->id = ++obj->last_hook_id;
	info->hook = hexchat_hook_print (ph,
				       name,
				       priority,
				       print_hook_cb,
				       info);
	g_hash_table_insert (obj->hooks, &info->id, info);
	*ret_id = info->id;

	return TRUE;
}

static gboolean
remote_object_unhook (RemoteObject *obj,
		      guint id,
		      GError **error)
{
	g_hash_table_remove (obj->hooks, &id);
	return TRUE;
}

static gboolean
remote_object_list_get (RemoteObject *obj,
			const char *name,
			guint *ret_id,
			GError **error)
{
	hexchat_list *xlist;
	guint *id;

	if (!hexchat_set_context (ph, obj->context)) {
		*ret_id = 0;
		return TRUE;
	}
	xlist = hexchat_list_get (ph, name);
	if (xlist == NULL) {
		*ret_id = 0;
		return TRUE;
	}
	id = g_new (guint, 1);
	*id = ++obj->last_list_id;
	*ret_id = *id;
	g_hash_table_insert (obj->lists,
			     id,
			     xlist);

	return TRUE;
}

static gboolean
remote_object_list_next	(RemoteObject *obj,
			 guint id,
			 gboolean *ret,
			 GError **error)
{
	hexchat_list *xlist;
	
	xlist = g_hash_table_lookup (obj->lists, &id);
	if (xlist == NULL) {
		*ret = FALSE;
		return TRUE;
	}
	*ret = hexchat_list_next (ph, xlist);

	return TRUE;
}			 

static gboolean
remote_object_list_str (RemoteObject *obj,
			guint id,
			const char *name,
			char **ret_str,
			GError **error)
{
	hexchat_list *xlist;
	
	xlist = g_hash_table_lookup (obj->lists, &id);
	if (xlist == NULL && !hexchat_set_context (ph, obj->context)) {
		*ret_str = NULL;
		return TRUE;
	}
	if (g_str_equal (name, "context")) {
		*ret_str = NULL;
		return TRUE;
	}
	*ret_str = g_strdup (hexchat_list_str (ph, xlist, name));

	return TRUE;
}

static gboolean
remote_object_list_int (RemoteObject *obj,
			guint id,
			const char *name,
			int *ret_int,
			GError **error)
{
	hexchat_list *xlist;
	
	xlist = g_hash_table_lookup (obj->lists, &id);
	if (xlist == NULL && !hexchat_set_context (ph, obj->context)) {
		*ret_int = -1;
		return TRUE;
	}
	if (g_str_equal (name, "context")) {
		hexchat_context *context;
		context = (hexchat_context*)hexchat_list_str (ph, xlist, name);
		*ret_int = context_list_find_id (context);
	} else {
		*ret_int = hexchat_list_int (ph, xlist, name);
	}

	return TRUE;
}

static gboolean
remote_object_list_time (RemoteObject *obj,
			 guint id,
			 const char *name,
			 guint64 *ret_time,
			 GError **error)
{
	hexchat_list *xlist;
	
	xlist = g_hash_table_lookup (obj->lists, &id);
	if (xlist == NULL) {
		*ret_time = (guint64) -1;
		return TRUE;
	}
	*ret_time = hexchat_list_time (ph, xlist, name);
	
	return TRUE;
}

static gboolean
remote_object_list_fields (RemoteObject *obj,
			   const char *name,
			   char ***ret,
			   GError **error)
{
	*ret = g_strdupv ((char**)hexchat_list_fields (ph, name));
	if (*ret == NULL) {
		*ret = g_new0 (char*, 1);
	}
	return TRUE;
}

static gboolean
remote_object_list_free (RemoteObject *obj,
			 guint id,
			 GError **error)
{
	g_hash_table_remove (obj->lists, &id);
	return TRUE;
}

static gboolean
remote_object_emit_print (RemoteObject *obj,
			  const char *event_name,
			  const char *args[],
			  gboolean *ret,
			  GError **error)
{
	const char *argv[4] = {NULL, NULL, NULL, NULL};
	int i;
	
	for (i = 0; i < 4 && args[i] != NULL; i++) {
		argv[i] = args[i];
	}

	*ret = hexchat_set_context (ph, obj->context);
	if (*ret) {
		*ret = hexchat_emit_print (ph, event_name, argv[0], argv[1],
							 argv[2], argv[3]);
	}

	return TRUE;
}

static gboolean
remote_object_nickcmp (RemoteObject *obj,
		       const char *nick1,
		       const char *nick2,
		       int *ret,
		       GError **error)
{
	hexchat_set_context (ph, obj->context);
	*ret = hexchat_nickcmp (ph, nick1, nick2);
	return TRUE;
}

static gboolean
remote_object_strip (RemoteObject *obj,
		     const char *str,
		     int len,
		     int flag,
		     char **ret_str,
		     GError **error)
{
	*ret_str = hexchat_strip (ph, str, len, flag);
	return TRUE;
}

static gboolean
remote_object_send_modes (RemoteObject *obj,
			  const char *targets[],
			  int modes_per_line,
			  char sign,
			  char mode,
			  GError **error)
{
	if (hexchat_set_context (ph, obj->context)) {
		hexchat_send_modes (ph, targets,
				  g_strv_length ((char**)targets),
				  modes_per_line,
				  sign, mode);
	}
	return TRUE;
}

static void
method_call (GDBusConnection *bus, const char *sender, const char *path,
             const char *interface, const char *method, GVariant *parameters,
             GDBusMethodInvocation *invocation, gpointer user_data)
{
	RemoteObject *obj = g_object_ref (user_data);
	if (g_str_equal (interface, "org.hexchat.connection") && g_str_equal (method, "Connect")) {
		char *filename = NULL;
		char *name = NULL;
		char *desc = NULL;
		char *version = NULL;
		g_variant_get (parameters, "(ssss)", &filename, &name, &desc, &version);
		remote_object_connect (obj, filename, name, desc, version, invocation);
		g_free (filename);
		g_free (name);
		g_free (desc);
		g_free (version);
		g_object_unref (obj);
		return;
	}
	if (g_str_equal (interface, "org.hexchat.connection") && g_str_equal (method, "Disconnect")) {
		remote_object_disconnect (obj, invocation);
		g_object_unref (obj);
		return;
	}
	if (g_str_equal (interface, "org.hexchat.plugin") && g_str_equal (method, "Command")) {
		char *command = NULL;
		g_variant_get (parameters, "(s)", &command);
		remote_object_command (obj, command, NULL);
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
		g_free (command);
		g_object_unref (obj);
		return;
	}
	if (g_str_equal (interface, "org.hexchat.plugin") && g_str_equal (method, "Print")) {
		char *text = NULL;
		g_variant_get (parameters, "(s)", &text);
		remote_object_print (obj, text, NULL);
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
		g_free (text);
		g_object_unref (obj);
		return;
	}
	if (g_str_equal (interface, "org.hexchat.plugin") && g_str_equal (method, "FindContext")) {
		char *server = NULL;
		char *channel = NULL;
		guint ret_id = 0;
		g_variant_get (parameters, "(ss)", &server, &channel);
		remote_object_find_context (obj, server, channel, &ret_id, NULL);
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(u)", ret_id));
		g_free (server);
		g_free (channel);
		g_object_unref (obj);
		return;
	}
	if (g_str_equal (interface, "org.hexchat.plugin") && g_str_equal (method, "GetContext")) {
		guint ret_id = 0;
		remote_object_get_context (obj, &ret_id, NULL);
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(u)", ret_id));
		g_object_unref (obj);
		return;
	}
	if (g_str_equal (interface, "org.hexchat.plugin") && g_str_equal (method, "SetContext")) {
		guint id = 0;
		gboolean ret = 0;
		g_variant_get (parameters, "(u)", &id);
		remote_object_set_context (obj, id, &ret, NULL);
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(b)", ret));
		g_object_unref (obj);
		return;
	}
	if (g_str_equal (interface, "org.hexchat.plugin") && g_str_equal (method, "GetInfo")) {
		char *id = NULL;
		char *ret_info = NULL;
		g_variant_get (parameters, "(s)", &id);
		remote_object_get_info (obj, id, &ret_info, NULL);
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", ret_info ? ret_info : ""));
		g_free (id);
		g_free (ret_info);
		g_object_unref (obj);
		return;
	}
	if (g_str_equal (interface, "org.hexchat.plugin") && g_str_equal (method, "GetPrefs")) {
		char *name = NULL;
		gint ret_type = 0;
		char *ret_str = NULL;
		gint ret_int = 0;
		g_variant_get (parameters, "(s)", &name);
		remote_object_get_prefs (obj, name, &ret_type, &ret_str, &ret_int, NULL);
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(isi)", ret_type, ret_str ? ret_str : "", ret_int));
		g_free (name);
		g_free (ret_str);
		g_object_unref (obj);
		return;
	}
	if (g_str_equal (interface, "org.hexchat.plugin") && g_str_equal (method, "HookCommand")) {
		char *name = NULL;
		gint priority = 0;
		char *help_text = NULL;
		gint return_value = 0;
		guint ret_id = 0;
		g_variant_get (parameters, "(sisi)", &name, &priority, &help_text, &return_value);
		remote_object_hook_command (obj, name, priority, help_text, return_value, &ret_id, NULL);
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(u)", ret_id));
		g_free (name);
		g_free (help_text);
		g_object_unref (obj);
		return;
	}
	if (g_str_equal (interface, "org.hexchat.plugin") && g_str_equal (method, "HookServer")) {
		char *name = NULL;
		gint priority = 0;
		gint return_value = 0;
		guint ret_id = 0;
		g_variant_get (parameters, "(sii)", &name, &priority, &return_value);
		remote_object_hook_server (obj, name, priority, return_value, &ret_id, NULL);
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(u)", ret_id));
		g_free (name);
		g_object_unref (obj);
		return;
	}
	if (g_str_equal (interface, "org.hexchat.plugin") && g_str_equal (method, "HookPrint")) {
		char *name = NULL;
		gint priority = 0;
		gint return_value = 0;
		guint ret_id = 0;
		g_variant_get (parameters, "(sii)", &name, &priority, &return_value);
		remote_object_hook_print (obj, name, priority, return_value, &ret_id, NULL);
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(u)", ret_id));
		g_free (name);
		g_object_unref (obj);
		return;
	}
	if (g_str_equal (interface, "org.hexchat.plugin") && g_str_equal (method, "Unhook")) {
		guint id = 0;
		g_variant_get (parameters, "(u)", &id);
		remote_object_unhook (obj, id, NULL);
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
		g_object_unref (obj);
		return;
	}
	if (g_str_equal (interface, "org.hexchat.plugin") && g_str_equal (method, "ListGet")) {
		char *name = NULL;
		guint ret_id = 0;
		g_variant_get (parameters, "(s)", &name);
		remote_object_list_get (obj, name, &ret_id, NULL);
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(u)", ret_id));
		g_free (name);
		g_object_unref (obj);
		return;
	}
	if (g_str_equal (interface, "org.hexchat.plugin") && g_str_equal (method, "ListNext")) {
		guint id = 0;
		gboolean ret = 0;
		g_variant_get (parameters, "(u)", &id);
		remote_object_list_next (obj, id, &ret, NULL);
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(b)", ret));
		g_object_unref (obj);
		return;
	}
	if (g_str_equal (interface, "org.hexchat.plugin") && g_str_equal (method, "ListStr")) {
		guint id = 0;
		char *name = NULL;
		char *ret_str = NULL;
		g_variant_get (parameters, "(us)", &id, &name);
		remote_object_list_str (obj, id, name, &ret_str, NULL);
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", ret_str ? ret_str : ""));
		g_free (name);
		g_free (ret_str);
		g_object_unref (obj);
		return;
	}
	if (g_str_equal (interface, "org.hexchat.plugin") && g_str_equal (method, "ListInt")) {
		guint id = 0;
		char *name = NULL;
		gint ret_int = 0;
		g_variant_get (parameters, "(us)", &id, &name);
		remote_object_list_int (obj, id, name, &ret_int, NULL);
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(i)", ret_int));
		g_free (name);
		g_object_unref (obj);
		return;
	}
	if (g_str_equal (interface, "org.hexchat.plugin") && g_str_equal (method, "ListTime")) {
		guint id = 0;
		char *name = NULL;
		guint64 ret_time = 0;
		g_variant_get (parameters, "(us)", &id, &name);
		remote_object_list_time (obj, id, name, &ret_time, NULL);
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(t)", ret_time));
		g_free (name);
		g_object_unref (obj);
		return;
	}
	if (g_str_equal (interface, "org.hexchat.plugin") && g_str_equal (method, "ListFields")) {
		char *name = NULL;
		char **ret = NULL;
		g_variant_get (parameters, "(s)", &name);
		remote_object_list_fields (obj, name, &ret, NULL);
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(^as)", ret));
		g_free (name);
		g_strfreev (ret);
		g_object_unref (obj);
		return;
	}
	if (g_str_equal (interface, "org.hexchat.plugin") && g_str_equal (method, "ListFree")) {
		guint id = 0;
		g_variant_get (parameters, "(u)", &id);
		remote_object_list_free (obj, id, NULL);
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
		g_object_unref (obj);
		return;
	}
	if (g_str_equal (interface, "org.hexchat.plugin") && g_str_equal (method, "EmitPrint")) {
		char *event_name = NULL;
		char **args = NULL;
		gboolean ret = 0;
		g_variant_get (parameters, "(s^as)", &event_name, &args);
		remote_object_emit_print (obj, event_name, (const char **)args, &ret, NULL);
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(b)", ret));
		g_free (event_name);
		g_strfreev (args);
		g_object_unref (obj);
		return;
	}
	if (g_str_equal (interface, "org.hexchat.plugin") && g_str_equal (method, "Nickcmp")) {
		char *nick1 = NULL;
		char *nick2 = NULL;
		gint ret = 0;
		g_variant_get (parameters, "(ss)", &nick1, &nick2);
		remote_object_nickcmp (obj, nick1, nick2, &ret, NULL);
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(i)", ret));
		g_free (nick1);
		g_free (nick2);
		g_object_unref (obj);
		return;
	}
	if (g_str_equal (interface, "org.hexchat.plugin") && g_str_equal (method, "Strip")) {
		char *str = NULL;
		gint len = 0;
		gint flag = 0;
		char *ret_str = NULL;
		g_variant_get (parameters, "(sii)", &str, &len, &flag);
		remote_object_strip (obj, str, len, flag, &ret_str, NULL);
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", ret_str ? ret_str : ""));
		g_free (str);
		g_free (ret_str);
		g_object_unref (obj);
		return;
	}
	if (g_str_equal (interface, "org.hexchat.plugin") && g_str_equal (method, "SendModes")) {
		char **targets = NULL;
		gint modes_per_line = 0;
		guchar sign = 0;
		guchar mode = 0;
		g_variant_get (parameters, "(^asiyy)", &targets, &modes_per_line, &sign, &mode);
		remote_object_send_modes (obj, (const char **)targets, modes_per_line, sign, mode, NULL);
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
		g_strfreev (targets);
		g_object_unref (obj);
		return;
	}
	g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method");
	g_object_unref (obj);
}

static gboolean
register_remote (RemoteObject *obj, GError **error)
{
	guint i;
	static GDBusNodeInfo *info;
	static const GDBusInterfaceVTable vtable = { method_call, NULL, NULL, { 0 } };
	if (!info)
		info = g_dbus_node_info_new_for_xml (remote_object_xml, NULL);
	for (i = 0; i < 2; i++) {
		obj->registrations[i] = g_dbus_connection_register_object (connection,
			obj->dbus_path, info->interfaces[i], &vtable, obj, NULL, error);
		if (!obj->registrations[i]) return FALSE;
	}
	return TRUE;
}

static void
name_owner_changed (GDBusConnection *bus, const char *sender, const char *path,
                    const char *interface, const char *signal,
                    GVariant *parameters, gpointer data)
{
	const char *name, *old_owner, *new_owner;
	g_variant_get (parameters, "(&s&s&s)", &name, &old_owner, &new_owner);
	if (!*new_owner) g_hash_table_remove (clients, name);
}

static gboolean
init_dbus (void)
{
	RemoteObject *remote;
	GError *error = NULL;
	GVariant *reply;
	guint request_name_result;
	connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
	if (!connection) goto fail;
	reply = g_dbus_connection_call_sync (connection, "org.freedesktop.DBus",
		"/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
		g_variant_new ("(su)", DBUS_SERVICE, 1u), G_VARIANT_TYPE ("(u)"),
		G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
	if (!reply) goto fail;
	g_variant_get (reply, "(u)", &request_name_result);
	g_variant_unref (reply);
	/* Keep queued instances exported so they can serve when the owner exits. */
	if (request_name_result != 1 && request_name_result != 2 && request_name_result != 4) {
		g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
			"Could not acquire %s (RequestName result %u)", DBUS_SERVICE, request_name_result);
		goto fail;
	}
	g_dbus_connection_signal_subscribe (connection, "org.freedesktop.DBus",
		"org.freedesktop.DBus", "NameOwnerChanged", "/org/freedesktop/DBus",
		NULL, G_DBUS_SIGNAL_FLAGS_NONE, name_owner_changed, NULL, NULL);
	remote = g_object_new (REMOTE_TYPE_OBJECT, NULL);
	remote->dbus_path = g_strdup (DBUS_OBJECT_PATH "/Remote");
	if (!register_remote (remote, &error)) {
		g_object_unref (remote);
		goto fail;
	}
	return TRUE;
fail:
	hexchat_printf (ph, _("Couldn't initialize session bus: %s\n"), error->message);
	g_clear_error (&error);
	g_clear_object (&connection);
	return FALSE;
}

/* hexchat_plugin stuffs */

static char**
build_list (char *word[])
{
	guint i;
	guint num = 0;
	char **result;

	if (word == NULL) {
		return NULL;
	}
  
	while (word[num] && word[num][0]) {
		num++;
	}
	
	result = g_new0 (char*, num + 1);
	for (i = 0; i < num; i++) {
		result[i] = g_strdup (word[i]);
	}

	return result;
}

static guint
context_list_find_id (hexchat_context *context)
{
	GList *l = NULL;

	for (l = contexts; l != NULL; l = l->next) {
		if (((ContextInfo*)l->data)->context == context) {
			return ((ContextInfo*)l->data)->id;
		}
	}

	return 0;
}

static hexchat_context*
context_list_find_context (guint id)
{
	GList *l = NULL;

	for (l = contexts; l != NULL; l = l->next) {
		if (((ContextInfo*)l->data)->id == id) {
			return ((ContextInfo*)l->data)->context;
		}
	}

	return NULL;
}

static int
open_context_cb (char *word[],
		 void *userdata)
{
	ContextInfo *info;
	
	info = g_new0 (ContextInfo, 1);
	info->id = ++last_context_id;
	info->context = hexchat_get_context (ph);
	contexts = g_list_prepend (contexts, info);

	return HEXCHAT_EAT_NONE;
}

static int
close_context_cb (char *word[],
		  void *userdata)
{
	GList *l;
	hexchat_context *context = hexchat_get_context (ph);

	for (l = contexts; l != NULL; l = l->next) {
		if (((ContextInfo*)l->data)->context == context) {
			g_free (l->data);
			contexts = g_list_delete_link (contexts, l);
			break;
		}
	}

	return HEXCHAT_EAT_NONE;
}

static gboolean
clients_find_filename_foreach (gpointer key,
			       gpointer value,
			       gpointer user_data)
{
	RemoteObject *obj = REMOTE_OBJECT (value);
	return g_str_equal (obj->filename, (char*)user_data);
}

static int
unload_plugin_cb (char *word[], char *word_eol[], void *userdata)
{
	RemoteObject *obj;
	
	obj = g_hash_table_find (clients,
				 clients_find_filename_foreach,
				 word[2]);
	if (obj != NULL) {
		g_dbus_connection_emit_signal (connection, NULL, obj->dbus_path,
		"org.hexchat.plugin", "UnloadSignal", NULL, NULL);
		return HEXCHAT_EAT_ALL;
	}
	
	return HEXCHAT_EAT_NONE;
}

int
dbus_plugin_init (hexchat_plugin *plugin_handle,
		  char **plugin_name,
		  char **plugin_desc,
		  char **plugin_version,
		  char *arg)
{
	ph = plugin_handle;
	*plugin_name = PNAME;
	*plugin_desc = PDESC;
	*plugin_version = PVERSION;

	if (init_dbus()) {
		/*hexchat_printf (ph, _("%s loaded successfully!\n"), PNAME);*/

		clients = g_hash_table_new_full (g_str_hash,
						 g_str_equal,
						 g_free,
						 g_object_unref);

		hexchat_hook_print (ph, "Open Context",
				  HEXCHAT_PRI_NORM,
				  open_context_cb,
				  NULL);

		hexchat_hook_print (ph, "Close Context",
				  HEXCHAT_PRI_NORM,
				  close_context_cb,
				  NULL);

		hexchat_hook_command (ph, "unload",
				    HEXCHAT_PRI_HIGHEST,
				    unload_plugin_cb, NULL, NULL);
	}

	return TRUE; 
}
