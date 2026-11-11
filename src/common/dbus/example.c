/* example.c - program to demonstrate some D-BUS stuffs.
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

/* Compile with: cc example.c $(pkg-config --cflags --libs gio-2.0) -o example */
#include <gio/gio.h>

static void
signal_cb (GDBusProxy *proxy, const char *sender, const char *name,
           GVariant *parameters, gpointer data)
{
 if (g_str_equal (name, "UnloadSignal"))
  g_main_loop_quit (data);
 else {
  char *text = g_variant_print (parameters, TRUE);
  g_print ("%s: %s\n", name, text);
  g_free (text);
 }
}

int
main (int argc, char **argv)
{
 GError *error = NULL;
 const char *service = argc > 1 ? argv[1] : "org.hexchat.service";
 GDBusConnection *bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
 if (!bus) goto fail;
 GVariant *reply = g_dbus_connection_call_sync (bus, service,
  "/org/hexchat/Remote", "org.hexchat.connection", "Connect",
  g_variant_new ("(ssss)", argv[0], "example", "Example D-Bus client", "1.0"),
  G_VARIANT_TYPE ("(s)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
 if (!reply) goto fail;
 const char *path;
 g_variant_get (reply, "(&s)", &path);
 GDBusProxy *proxy = g_dbus_proxy_new_sync (bus, G_DBUS_PROXY_FLAGS_NONE,
  NULL, service, path, "org.hexchat.plugin", NULL, &error);
 g_variant_unref (reply);
 if (!proxy) goto fail;
 GMainLoop *loop = g_main_loop_new (NULL, FALSE);
 g_signal_connect (proxy, "g-signal", G_CALLBACK (signal_cb), loop);
 reply = g_dbus_proxy_call_sync (proxy, "HookCommand",
  g_variant_new ("(sisi)", "test", 0, "D-Bus example", 1),
  G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
 if (!reply) goto fail;
 g_variant_unref (reply);
 reply = g_dbus_proxy_call_sync (proxy, "HookServer",
  g_variant_new ("(sii)", "RAW LINE", 0, 0),
  G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
 if (!reply) goto fail;
 g_variant_unref (reply);
 g_main_loop_run (loop);
 reply = g_dbus_connection_call_sync (bus, service, g_dbus_proxy_get_object_path (proxy),
  "org.hexchat.connection", "Disconnect", NULL, NULL,
  G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
 if (reply) g_variant_unref (reply);
 g_main_loop_unref (loop);
 g_object_unref (proxy);
 g_object_unref (bus);
 return 0;
fail:
 g_printerr ("%s\n", error->message);
 g_clear_error (&error);
 return 1;
}
