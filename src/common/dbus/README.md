The scripting API uses GDBus and the existing GIO dependency. The service name
is `org.hexchat.service`, or `io.github.Hexchat` with
`-Ddbus-service-use-appid=true`. Both interfaces are exported at
`/org/hexchat/Remote` and at each client path returned by `Connect`.

`remote-object.xml` supplies runtime introspection. `legacy-api.xml` is the
pre-migration API fixture; keep it independent of implementation changes.
`Connect` returns a string, not an object-path-typed value. Hook and list IDs
belong to the calling client's object. Signals remain broadcast on that path.

Configure with `-Dtext-frontend=true -Ddbus=enabled`, build, then run:

```
meson test -C build --print-errorlogs 'D-Bus scripting API' 'D-Bus GTK single instance'
```

Tests launch real frontends using `dbus-run-session`; GTK tests additionally
require `xvfb-run` and `xauth`. Run as an ordinary user because HexChat displays
a blocking warning when run as root. Each test uses a temporary configuration
and a loopback IRC fixture, without connecting to an external IRC network.
The temporary configuration path is printed on success and retained for diagnosis.
The GTK test covers command, legacy URL, multiple positional URLs and startup
when `--existing` finds no owner. Tests compare every method and signal signature
against the legacy fixture and call every public method. They also verify that a
second instance queues for the service name and takes over when the first exits.
The compiled test launches the bus and display runners so test registration
remains compatible with Meson 0.47.

The Debian workflow also builds and tests the app-id variant and a separate
`-Ddbus=disabled` build. Disabled builds need neither a session bus nor its tools.
