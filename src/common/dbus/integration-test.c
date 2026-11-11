/* Exercise the public API over an isolated session bus against a real frontend. */
#include <gio/gio.h>
#include <glib/gstdio.h>

static GDBusConnection *bus;
static const char *service;
static guint signals;

static GVariant *
call (const char *path, const char *interface, const char *method, GVariant *args)
{
 GError *error = NULL;
 GVariant *reply = g_dbus_connection_call_sync (bus, service, path, interface,
  method, args, NULL, G_DBUS_CALL_FLAGS_NO_AUTO_START, 5000, NULL, &error);
 g_assert_no_error (error);
 g_assert_nonnull (reply);
 return reply;
}

static GVariant *
plugin (const char *path, const char *method, GVariant *args)
{
 return call (path, "org.hexchat.plugin", method, args);
}

static guint
uint_reply (GVariant *reply)
{
 guint result;
 g_variant_get (reply, "(u)", &result);
 g_variant_unref (reply);
 return result;
}

static char *
connect_client (void)
{
 char *path;
 GVariant *reply = call ("/org/hexchat/Remote", "org.hexchat.connection", "Connect",
  g_variant_new ("(ssss)", "/tmp/regression.py", "regression", "D-Bus regression", "1"));
 g_variant_get (reply, "(s)", &path);
 g_variant_unref (reply);
 return path;
}

static void
signal_cb (GDBusConnection *connection, const char *sender, const char *path,
           const char *interface, const char *name, GVariant *args, gpointer data)
{
 if (g_str_equal (name, "CommandSignal")) {
  char **word, **eol;
  guint hook, context;
  g_variant_get (args, "(^as^asuu)", &word, &eol, &hook, &context);
  if (hook == 1)
   g_assert_cmpstr (word[0], ==, "GDBUSTEST");
  else {
   g_assert_cmpuint (hook, ==, 4);
   g_assert_cmpstr (word[0], ==, "url");
  }
  g_assert_cmpuint (context, >, 0);
  const char *payloads[] = {"payload", "forwarded", "irc://example.invalid/one",
   "irc://example.invalid/two", "ircs://example.invalid/three"};
  guint index = signals == 0 ? 0 : signals - 3;
  g_assert_cmpuint (index, <, G_N_ELEMENTS (payloads));
  g_assert_cmpstr (word[1], ==, payloads[index]);
  g_strfreev (word);
  g_strfreev (eol);
 } else if (g_str_equal (name, "ServerSignal")) {
  char **word, **eol;
  guint hook, context;
  g_variant_get (args, "(^as^asuu)", &word, &eol, &hook, &context);
  g_assert_cmpstr (word[1], ==, "NOTICE");
  g_assert_cmpuint (hook, ==, 3);
  g_strfreev (word); g_strfreev (eol);
 } else if (g_str_equal (name, "PrintSignal")) {
  g_assert_true (g_variant_is_of_type (args, G_VARIANT_TYPE ("(asuu)")));
 } else if (g_str_equal (name, "UnloadSignal")) {
  g_assert_true (g_variant_is_of_type (args, G_VARIANT_TYPE_UNIT));
 } else g_assert_not_reached ();
 signals++;
}

static void
wait_signals (guint count)
{
 gint64 deadline = g_get_monotonic_time () + 5000000;
 while (signals < count && g_get_monotonic_time () < deadline) {
  while (g_main_context_iteration (NULL, FALSE));
  g_usleep (1000);
 }
 g_assert_cmpuint (signals, ==, count);
}

static void
check_introspection (const char *path, GDBusNodeInfo *expected)
{
 GVariant *reply = call (path, "org.freedesktop.DBus.Introspectable", "Introspect", NULL);
 const char *xml;
 g_variant_get (reply, "(&s)", &xml);
 GDBusNodeInfo *actual = g_dbus_node_info_new_for_xml (xml, NULL);
 for (guint i = 0; expected->interfaces[i]; i++) {
  GDBusInterfaceInfo *want = expected->interfaces[i];
  GDBusInterfaceInfo *have = g_dbus_node_info_lookup_interface (actual, want->name);
  g_assert_nonnull (have);
  for (guint j = 0; want->methods && want->methods[j]; j++) {
   GDBusMethodInfo *wm = want->methods[j];
   GDBusMethodInfo *hm = g_dbus_interface_info_lookup_method (have, wm->name);
   g_assert_nonnull (hm);
   GDBusArgInfo **wa[] = {wm->in_args, wm->out_args};
   GDBusArgInfo **ha[] = {hm->in_args, hm->out_args};
   for (guint k = 0; k < 2; k++) {
    guint n = 0;
    for (; wa[k] && wa[k][n]; n++) {
     g_assert_nonnull (ha[k]); g_assert_nonnull (ha[k][n]);
     g_assert_cmpstr (wa[k][n]->signature, ==, ha[k][n]->signature);
    }
    g_assert_true (!ha[k] || !ha[k][n]);
   }
  }
  for (guint j = 0; want->signals && want->signals[j]; j++) {
   GDBusSignalInfo *ws = want->signals[j];
   GDBusSignalInfo *hs = g_dbus_interface_info_lookup_signal (have, ws->name);
   g_assert_nonnull (hs);
   guint n = 0;
   for (; ws->args && ws->args[n]; n++) {
    g_assert_nonnull (hs->args); g_assert_nonnull (hs->args[n]);
    g_assert_cmpstr (ws->args[n]->signature, ==, hs->args[n]->signature);
   }
   g_assert_true (!hs->args || !hs->args[n]);
  }
 }
 g_dbus_node_info_unref (actual);
 g_variant_unref (reply);
}

static gpointer
irc_server (gpointer data)
{
 GSocketListener *listener = data;
 GSocketConnection *connection = g_socket_listener_accept (listener, NULL, NULL, NULL);
 g_assert_nonnull (connection);
 GDataInputStream *input = g_data_input_stream_new (g_io_stream_get_input_stream (G_IO_STREAM (connection)));
 char *line;
 while ((line = g_data_input_stream_read_line (input, NULL, NULL, NULL))) {
  if (g_str_has_prefix (line, "USER ")) {
   const char *welcome = ":localhost 001 tester :Welcome\r\n";
   g_output_stream_write_all (g_io_stream_get_output_stream (G_IO_STREAM (connection)),
    welcome, strlen (welcome), NULL, NULL, NULL);
  }
  g_free (line);
 }
 g_object_unref (input); g_object_unref (connection);
 return NULL;
}

int
main (int argc, char **argv)
{
 GError *error = NULL;
 if (argc == 8 && g_str_equal (argv[1], "--session")) {
  const char *text_args[] = {argv[2], "--", argv[0], argv[4], argv[5], argv[6], argv[7], NULL};
  const char *gtk_args[] = {argv[2], "--", argv[3], "-a", argv[0], argv[4], argv[5], argv[6], argv[7], NULL};
  GSubprocess *runner = g_subprocess_newv (*argv[3] ? gtk_args : text_args,
   G_SUBPROCESS_FLAGS_NONE, &error);
  g_assert_no_error (error);
  g_assert_true (g_subprocess_wait_check (runner, NULL, &error));
  g_assert_no_error (error);
  g_object_unref (runner);
  return 0;
 }
 g_assert_cmpint (argc, ==, 5);
 service = argv[2];
 char *dir = g_dir_make_tmp ("hexchat-dbus-XXXXXX", &error);
 g_assert_no_error (error);
 char *config = g_build_filename (dir, "hexchat.conf", NULL);
 g_assert_true (g_file_set_contents (config, "gui_slist_skip = 1\nirc_nick1 = tester\n", -1, &error));
 g_assert_no_error (error); g_free (config);
 GSubprocess *app = g_subprocess_new (G_SUBPROCESS_FLAGS_STDIN_PIPE,
  &error, argv[1], "--no-auto", "--no-plugins", "--cfgdir", dir, NULL);
 g_assert_no_error (error);
 bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
 g_assert_no_error (error);
 gboolean ready = FALSE;
 for (guint i = 0; i < 500; i++) {
  GVariant *r = g_dbus_connection_call_sync (bus, service, "/org/hexchat/Remote",
   "org.hexchat.plugin", "GetContext", NULL, NULL,
   G_DBUS_CALL_FLAGS_NO_AUTO_START, 100, NULL, NULL);
  if (r) {g_variant_unref (r); ready = TRUE; break;}
  g_usleep (10000);
 }
 g_assert_true (ready);
 char *xml;
 g_assert_true (g_file_get_contents (argv[3], &xml, NULL, &error));
 GDBusNodeInfo *expected = g_dbus_node_info_new_for_xml (xml, &error);
 g_assert_no_error (error);
 check_introspection ("/org/hexchat/Remote", expected);
 char *path = connect_client ();
 char *again = connect_client ();
 g_assert_cmpstr (path, ==, again); g_free (again);
 check_introspection (path, expected);
 guint context = uint_reply (plugin (path, "GetContext", NULL));
 g_assert_cmpuint (context, >, 0);
 GVariant *r = plugin (path, "SetContext", g_variant_new ("(u)", context));
 gboolean ok; g_variant_get (r, "(b)", &ok); g_assert_true (ok); g_variant_unref (r);
 r = plugin (path, "SetContext", g_variant_new ("(u)", 0u));
 g_variant_get (r, "(b)", &ok); g_assert_false (ok); g_variant_unref (r);
 guint list = uint_reply (plugin (path, "ListGet", g_variant_new ("(s)", "channels")));
 g_assert_cmpuint (list, >, 0);
 r = plugin (path, "ListNext", g_variant_new ("(u)", list));
 g_variant_get (r, "(b)", &ok); g_assert_true (ok); g_variant_unref (r);
 r = plugin (path, "ListInt", g_variant_new ("(us)", list, "context"));
 gint id; g_variant_get (r, "(i)", &id); g_assert_cmpuint (id, ==, context); g_variant_unref (r);
 r = plugin (path, "Strip", g_variant_new ("(sii)", "\00304hello", -1, 3));
 const char *str; g_variant_get (r, "(&s)", &str); g_assert_cmpstr (str, ==, "hello"); g_variant_unref (r);
 g_assert_cmpuint (uint_reply (plugin (path, "FindContext", g_variant_new ("(ss)", "", ""))), ==, context);
 g_variant_unref (plugin (path, "Print", g_variant_new ("(s)", "D-Bus regression")));
 r = plugin (path, "Nickcmp", g_variant_new ("(ss)", "Nick", "nick"));
 g_variant_get (r, "(i)", &id); g_assert_cmpint (id, ==, 0); g_variant_unref (r);
 g_variant_unref (plugin (path, "ListStr", g_variant_new ("(us)", list, "channel")));
 r = plugin (path, "ListTime", g_variant_new ("(us)", 0u, "time"));
 guint64 stamp; g_variant_get (r, "(t)", &stamp); g_assert_cmpuint (stamp, ==, G_MAXUINT64); g_variant_unref (r);
 g_variant_unref (plugin (path, "ListFields", g_variant_new ("(s)", "channels")));
 const char *targets[] = {"tester", NULL};
 g_variant_unref (plugin (path, "SendModes", g_variant_new ("(^asiyy)", targets, 1, '+', 'o')));
 g_variant_unref (plugin (path, "GetPrefs", g_variant_new ("(s)", "does_not_exist")));
 g_variant_unref (plugin (path, "GetInfo", g_variant_new ("(s)", "win_ptr")));
 guint sub = g_dbus_connection_signal_subscribe (bus, service, "org.hexchat.plugin",
  NULL, path, NULL, G_DBUS_SIGNAL_FLAGS_NONE, signal_cb, NULL, NULL);
 g_assert_cmpuint (uint_reply (plugin (path, "HookCommand", g_variant_new ("(sisi)", "GDBUSTEST", 0, "", 3))), ==, 1);
 g_variant_unref (plugin (path, "Command", g_variant_new ("(s)", "GDBUSTEST payload")));
 wait_signals (1);
 g_assert_cmpuint (uint_reply (plugin (path, "HookPrint", g_variant_new ("(sii)", "Channel Message", 0, 0))), ==, 2);
 const char *args[] = {"nick", "message", "", "", NULL};
 g_variant_unref (plugin (path, "EmitPrint", g_variant_new ("(s^as)", "Channel Message", args)));
 wait_signals (2);
 g_assert_cmpuint (uint_reply (plugin (path, "HookServer", g_variant_new ("(sii)", "NOTICE", 0, 0))), ==, 3);
 GSocketListener *listener = g_socket_listener_new ();
 guint16 port = g_socket_listener_add_any_inet_port (listener, NULL, &error);
 g_assert_no_error (error);
 GThread *thread = g_thread_new ("irc-fixture", irc_server, listener);
 char *command = g_strdup_printf ("server -insecure 127.0.0.1 %u", port);
 g_variant_unref (plugin (path, "Command", g_variant_new ("(s)", command))); g_free (command);
 gboolean connected = FALSE;
 for (guint i = 0; i < 500; i++) {
  r = plugin (path, "GetInfo", g_variant_new ("(s)", "server"));
  g_variant_get (r, "(&s)", &str);
  connected = *str != 0;
  g_variant_unref (r);
  if (connected) break;
  g_usleep (10000);
 }
 g_assert_true (connected);
 g_variant_unref (plugin (path, "Command", g_variant_new ("(s)", "recv :someone!u@host NOTICE tester :regression")));
 wait_signals (3);
 g_variant_unref (plugin (path, "Command", g_variant_new ("(s)", "unload regression.py")));
 wait_signals (4);
 if (g_str_equal (argv[4], "gtk")) {
  GSubprocess *cli = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
   G_SUBPROCESS_FLAGS_STDERR_SILENCE, &error, argv[1], "--existing",
   "--command", "GDBUSTEST forwarded", NULL);
  g_assert_no_error (error);
  g_assert_true (g_subprocess_wait_check (cli, NULL, &error)); g_assert_no_error (error);
  g_object_unref (cli); wait_signals (5);
  g_assert_cmpuint (uint_reply (plugin (path, "HookCommand", g_variant_new ("(sisi)", "URL", 0, "", 3))), ==, 4);
  cli = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
   &error, argv[1], "--existing", "--url", "irc://example.invalid/one", NULL);
  g_assert_no_error (error);
  g_assert_true (g_subprocess_wait_check (cli, NULL, &error)); g_assert_no_error (error);
  g_object_unref (cli); wait_signals (6);
  cli = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
   &error, argv[1], "--existing", "irc://example.invalid/two", "ircs://example.invalid/three", NULL);
  g_assert_no_error (error);
  g_assert_true (g_subprocess_wait_check (cli, NULL, &error)); g_assert_no_error (error);
  g_object_unref (cli); wait_signals (8);
 }
 g_variant_unref (plugin (path, "Unhook", g_variant_new ("(u)", 2u)));
 g_variant_unref (plugin (path, "ListFree", g_variant_new ("(u)", list)));
 r = plugin (path, "ListNext", g_variant_new ("(u)", list));
 g_variant_get (r, "(b)", &ok); g_assert_false (ok); g_variant_unref (r);
 g_variant_unref (plugin (path, "Command", g_variant_new ("(s)", "newserver -noconnect other.invalid")));
 /* A second live owner cannot advance the first owner's list. */
 GDBusConnection *first = bus;
 bus = g_dbus_connection_new_for_address_sync (g_getenv ("DBUS_SESSION_BUS_ADDRESS"),
  G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
  NULL, NULL, &error); g_assert_no_error (error);
 char *second = connect_client ();
 g_assert_cmpstr (second, !=, path);
 guint second_context = uint_reply (plugin (second, "GetContext", NULL));
 g_assert_cmpuint (second_context, >, 0);
 g_assert_cmpuint (second_context, !=, context);
 g_assert_cmpuint (uint_reply (plugin (path, "GetContext", NULL)), ==, context);
 g_variant_unref (plugin (second, "SetContext", g_variant_new ("(u)", context)));
 g_variant_unref (plugin (path, "SetContext", g_variant_new ("(u)", second_context)));
 g_assert_cmpuint (uint_reply (plugin (second, "GetContext", NULL)), ==, context);
 g_variant_unref (plugin (path, "Command", g_variant_new ("(s)", "close")));
 r = plugin (path, "SetContext", g_variant_new ("(u)", second_context));
 g_variant_get (r, "(b)", &ok); g_assert_false (ok); g_variant_unref (r);
 g_variant_unref (plugin (path, "SetContext", g_variant_new ("(u)", context)));
 r = plugin (second, "ListNext", g_variant_new ("(u)", list));
 g_variant_get (r, "(b)", &ok); g_assert_false (ok); g_variant_unref (r);
 g_assert_cmpuint (uint_reply (plugin (second, "ListGet", g_variant_new ("(s)", "channels"))), ==, 1);
 g_variant_unref (plugin (second, "SetContext", g_variant_new ("(u)", 0u)));
 g_assert_true (g_dbus_connection_close_sync (bus, NULL, &error)); g_assert_no_error (error);
 g_object_unref (bus); bus = first; g_free (second);
 g_assert_cmpuint (uint_reply (plugin (path, "GetContext", NULL)), ==, context);
 g_variant_unref (call (path, "org.hexchat.connection", "Disconnect", NULL));
 r = g_dbus_connection_call_sync (bus, service, path, "org.hexchat.plugin", "GetContext",
  NULL, NULL, G_DBUS_CALL_FLAGS_NONE, 1000, NULL, &error);
 g_assert_null (r); g_assert_nonnull (error); g_clear_error (&error);
 g_dbus_connection_signal_unsubscribe (bus, sub);
 g_free (path);
 /* A fresh unique bus owner must get fresh hook/list IDs; vanishing removes its object. */
 for (guint i = 0; i < 2; i++) {
  GDBusConnection *shared = bus;
  bus = g_dbus_connection_new_for_address_sync (g_getenv ("DBUS_SESSION_BUS_ADDRESS"),
   G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
   NULL, NULL, &error); g_assert_no_error (error);
  path = connect_client ();
  g_assert_cmpuint (uint_reply (plugin (path, "HookServer", g_variant_new ("(sii)", "NOTICE", 0, 0))), ==, 1);
  g_assert_cmpuint (uint_reply (plugin (path, "ListGet", g_variant_new ("(s)", "channels"))), ==, 1);
  g_assert_true (g_dbus_connection_close_sync (bus, NULL, &error)); g_assert_no_error (error);
  g_object_unref (bus); bus = shared;
  gboolean removed = FALSE;
  for (guint j = 0; j < 200; j++) {
   r = g_dbus_connection_call_sync (bus, service, path, "org.hexchat.plugin", "GetContext",
    NULL, NULL, G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL);
   if (!r) {removed = TRUE; break;} g_variant_unref (r); g_usleep (10000);
  }
  g_assert_true (removed); g_free (path);
 }
 /* A second instance queues its name and must serve after the first exits. */
 char *queued_dir = g_dir_make_tmp ("hexchat-dbus-queued-XXXXXX", &error);
 g_assert_no_error (error);
 char *queued_config = g_build_filename (queued_dir, "hexchat.conf", NULL);
 g_assert_true (g_file_set_contents (queued_config, "gui_slist_skip = 1\n", -1, &error));
 g_assert_no_error (error); g_free (queued_config);
 GSubprocess *queued = g_subprocess_new (G_SUBPROCESS_FLAGS_STDIN_PIPE,
  &error, argv[1], "--no-auto", "--no-plugins", "--cfgdir", queued_dir, NULL);
 g_assert_no_error (error);
 char *queued_owner = NULL;
 for (guint i = 0; i < 500; i++) {
  r = g_dbus_connection_call_sync (bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
   "org.freedesktop.DBus", "ListQueuedOwners", g_variant_new ("(s)", service),
   G_VARIANT_TYPE ("(as)"), G_DBUS_CALL_FLAGS_NONE, 1000, NULL, &error);
  g_assert_no_error (error);
  char **owners;
  g_variant_get (r, "(^as)", &owners);
  if (g_strv_length (owners) == 2) queued_owner = g_strdup (owners[1]);
  g_strfreev (owners); g_variant_unref (r);
  if (queued_owner) break;
  g_usleep (10000);
 }
 g_assert_nonnull (queued_owner);
 g_subprocess_force_exit (app); g_subprocess_wait (app, NULL, NULL); g_object_unref (app);
 gboolean takeover = FALSE;
 for (guint i = 0; i < 500; i++) {
  r = g_dbus_connection_call_sync (bus, service, "/org/hexchat/Remote",
   "org.hexchat.plugin", "GetContext", NULL, NULL,
   G_DBUS_CALL_FLAGS_NO_AUTO_START, 100, NULL, NULL);
  if (r) { g_variant_unref (r); takeover = TRUE; break; }
  g_usleep (10000);
 }
 g_assert_true (takeover);
 r = g_dbus_connection_call_sync (bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
  "org.freedesktop.DBus", "GetNameOwner", g_variant_new ("(s)", service),
  G_VARIANT_TYPE ("(s)"), G_DBUS_CALL_FLAGS_NONE, 1000, NULL, &error);
 g_assert_no_error (error);
 const char *owner;
 g_variant_get (r, "(&s)", &owner);
 g_assert_cmpstr (owner, ==, queued_owner);
 g_variant_unref (r); g_free (queued_owner);
 g_subprocess_force_exit (queued); g_subprocess_wait (queued, NULL, NULL);
 g_object_unref (queued); g_free (queued_dir);
 g_thread_join (thread); g_object_unref (listener);
 if (g_str_equal (argv[4], "gtk")) {
  /* Without an owner, --existing must continue normal startup. */
  GSubprocess *fallback = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
   G_SUBPROCESS_FLAGS_STDERR_SILENCE, &error, argv[1], "--no-auto", "--no-plugins",
   "--cfgdir", dir, "--existing", "--command", "echo fallback", NULL);
  g_assert_no_error (error);
  gboolean started = FALSE;
  for (guint i = 0; i < 500; i++) {
   r = g_dbus_connection_call_sync (bus, service, "/org/hexchat/Remote",
    "org.hexchat.plugin", "GetContext", NULL, NULL, G_DBUS_CALL_FLAGS_NO_AUTO_START,
    100, NULL, NULL);
   if (r) { g_variant_unref (r); started = TRUE; break; }
   g_usleep (10000);
  }
  g_assert_true (started);
  g_subprocess_force_exit (fallback); g_subprocess_wait (fallback, NULL, NULL);
  g_object_unref (fallback);
 }
 g_object_unref (bus); g_dbus_node_info_unref (expected); g_free (xml);
 /* The temporary config is intentionally retained for failure diagnosis. */
 g_print ("D-Bus integration passed; config: %s\n", dir); g_free (dir);
 return 0;
}
