# GTK3 port status

Branch `cog/gtk3-modernization`, based on HexChat 2.16.2 (`master` @ b544ac33).

## Reference environment

A Debian 13 (trixie) container. `.github/workflows/debian13-gtk3.yml` lists the
exact packages and steps. All local results below come from that environment
(OpenSSL 3.5, GTK 3.24, Xvfb + openbox). GitHub Actions results are listed below.
Windows, macOS, Flatpak and Wayland were not tested.

## Verification commands

```
meson setup build -Dtext-frontend=true -Dtheme-manager=false -Dtls=enabled
ninja -C build                      # builds hexchat, hexchat-text and all six default plugins
meson test -C build                 # unit tests and data validation
ci/tls-irc-test.sh build            # end-to-end TLS test through server.c (loopback only)
ci/gui-test.sh build                # Xvfb GUI harness; run as a non-root user
```

See "Results" at the bottom for what was last run.

## What changed

- **GTK2 to GTK3 in `src/fe-gtk`**: xtext now draws with cairo, plus chanview,
  menu, palette, pixmaps, setup and spell entry updates. `GTK_DISABLE_DEPRECATED`
  is no longer defined, because many deprecated GTK3 calls remain (see
  limitations).
- **OpenSSL 3.0 is now the minimum** (`meson.build`: `openssl >= 3.0`). Code
  uses `TLS_client_method`, `SSL_get1_peer_certificate`, `BN_secure_new`,
  `BN_check_prime` and the provider API (fishlim), which need 3.0. The old
  0.9.8 and 1.0/1.1 compatibility branches and the `HAVE_*` configure probes
  for them were removed. On MSVC `libssl` is still located with
  `find_library`, which does not check a version.
- **TLS minimum version is 1.2** (`HEXCHAT_TLS_MIN_VERSION` in `ssl.h`,
  applied with `SSL_CTX_set_min_proto_version`). SSLv2/v3, compression and
  tickets stay disabled. Servers that only offer TLS 1.0 or 1.1 (deprecated by
  RFC 8996) can no longer be reached over TLS. This is a deliberate behaviour
  change; to relax it, change the one macro. `_SSL_context_init` returns NULL
  (and `server.c` exits) if the context cannot be created or the minimum
  version cannot be set, so it fails closed.
- **Certificate checks are unchanged**: chain verify result, SAN/CN hostname
  match and `accept_invalid_cert` behave as before. Fixed in `server.c`: a
  leaked peer-certificate reference, and a missing peer certificate is now a
  failure.
- **fishlim DH1080** was rewritten on BIGNUM, since `DH_*` is deprecated. Same
  prime, generator and wire format. It uses `BN_mod_exp_mont_consttime` with the
  secret exponent flagged constant-time, secure-heap bignums, and cleansing of
  secrets. Peer keys must satisfy 1 < pk < p-1, input lengths are bounded, and an
  invalid peer key is now an error (previously success with an unset secret).
- **`menu.c`**: guard against a NULL menu item in `menu_set_active_quiet`.
- **Appdata validation is offline**: the meson test runs
  `appstream-util validate-relax --nonet`. Schema and metadata checks all
  still run; only fetching the remote screenshot URLs is skipped.

## Tests

| Test | Covers |
|---|---|
| `meson test`: `TLS Tests` (`src/common/tests/ssl-tests.c`) | `ssl.c` over a socketpair with generated certs: valid chain, untrusted CA, self-signed, expired, hostname mismatch, wildcard, IP SAN, CN fallback, TLS 1.2 minimum |
| `meson test`: `Fishlim Tests` | DH1080 known-answer vectors (computed independently with Python), interop with OpenSSL EVP DH, invalid-key rejection, ECB/CBC, base64 |
| `meson test`: desktop file and appdata validation | offline, see above |
| `ci/tls-irc-test.sh` | `hexchat-text` runs `/server -ssl 127.0.0.1 PORT` against local TLS IRC servers (`ci/tls/tls-irc-server.py`, bound to 127.0.0.1 only; no external server). The test CA is supplied via `SSL_CERT_FILE`. Cases: trusted CA with IP SAN connects and registers; certificate for another hostname is refused; certificate from an unknown CA is refused. Refused cases must also show no IRC registration on the server. Controls: both refused servers connect with `-ssl-noverify`, so the refusals come from verification |
| `ci/gui-test.sh` | 30 observable-state checks: startup, plugins (python, perl, lua, checksum, fishlim, sysinfo), tab creation and switching, scrollback and wheel, selection via PRIMARY, network list and preferences dialogs, no GTK/GLib warnings. It uses no pixel hashes; screenshots are diagnostics only |

The GUI harness needs Xvfb, openbox, xdotool, xclip and x11-apps. HexChat shows
a modal warning as root, so run it as a normal user.

## Appdata network check (separate, manual)

The offline test does not prove that the screenshot URLs in
`data/misc/io.github.Hexchat.appdata.xml.in` are reachable. The URL is an
`http://i.imgur.com/...` link. To check availability, run this on a networked
host:

```
appstream-util validate-relax build/data/misc/io.github.Hexchat.appdata.xml
```

This is expected to fail while that URL is unreachable. It needs a
metadata fix, not a code change.

## Workflows

- `debian13-gtk3.yml`: runs on push to `master` and `cog/**` and on PRs to
  `master`. Read-only token, no secrets, no artifacts. It builds, runs
  `meson test`, the TLS IRC test and the GUI test.
- `ubuntu-build.yml`: updated to ubuntu-24.04 and GTK3. Its test step includes
  the offline appdata validation.
- `windows-build.yml`, `msys-build.yml`, `flatpak-build.yml`: inherited GTK2-era
  workflows. **Not ported.** Automatic triggers are removed; only
  `workflow_dispatch` remains, and each name says "GTK3 port pending, manual
  only". Nothing publishes automatically. A manual Windows run would still
  upload workflow artifacts, using the unsupported `upload-artifact@v2`, and
  downloads Inno Setup over plain http.

## Provenance and licensing

- All original copyright headers are preserved. These include Peter Zelezny,
  Berke Viktor, libsexy (Christian Hammond, LGPL-2+), gtkspell (Evan Martin)
  and the Conspire search bar (William Pitcock). The project is
  GPL-2.0-or-later.
- The GTK3 `src/fe-gtk` changes are an original implementation. The only
  external reference consulted was ZoiteChat at
  <https://github.com/ZoiteChat/zoitechat/tree/5952d8f>, and only its headers
  (the `xtext.h` diff and the meson GTK3 dependency line). No function bodies
  were copied from it.
- All other changes (TLS, fishlim, tests, `ci/`, workflows, this document) were
  made in this tree.
- `ci/dev-shell.nix` was removed; the Debian 13 environment above is the only
  reference.

## Limitations and open work

- Remaining GTK3 deprecation warnings in `src/fe-gtk` (build still succeeds):
  `GtkStock`, `gtk_table_*` (to `GtkGrid`), `gtk_misc_set_alignment`,
  `gtk_hbox_new`/`gtk_vbox_new`, `gtk_hbutton_box_new`, `*_from_stock`,
  `gtk_adjustment_changed`. No warnings in `common` or the plugins.
- `src/common/dbus` still uses dbus-glib (unmaintained; builds without
  warnings). A GDBus port needs a session-bus test of every method and was not
  attempted.
- Constant-time behaviour of DH1080 relies on OpenSSL's documented contract and
  was not measured.
- Multi-line selection highlight starts about 7px left of the text indent on
  middle lines. It was not compared with the GTK2 build.
- The TLS IRC test covers the connect and verify path with one minimal server
  that only registers a client. It does not cover SASL, client certificates or
  reconnects.
- Only X11/Xvfb was tested, not Wayland.

## Results

Run in the Debian 13 container after the final edits (rebuilt with `ninja`, no errors):

- `meson test -C build`: 5 of 5 OK (desktop, appdata offline, translations, TLS
  Tests 9 subtests, Fishlim Tests 12 subtests).
- `ci/tls-irc-test.sh build`: 5 of 5 OK (trusted connects; hostname mismatch and
  unknown CA refused; both `-ssl-noverify` controls connect).
- `ci/gui-test.sh build`: 30 of 30 OK. One earlier run reported failures because
  an overlapping run was still using the same X display (`:97`); a clean rerun
  passed. Do not run two instances at once.
- `git diff --check`: clean.
- GitHub Actions run 42551913547 (PR #1): build, all Meson tests and the TLS
  integration test passed; the GUI test passed 29 of 30. The failure was the
  strict "no GTK/GLib warnings" check: `Gtk-WARNING: Could not load a pixbuf
  from /org/gtk/libgtk/theme/Adwaita/assets/check-symbolic.svg`. Cause: the
  workflow installs with `--no-install-recommends`, which omits the SVG
  gdk-pixbuf loader. Fix: the workflow now installs `librsvg2-common` (the
  package that ships the loader in Debian 13). Reproduced in a `debian:13`
  container: without the package the same single check fails with the same
  warning; with it, `ci/gui-test.sh build` reports 0 failures. The warning
  assertion is unchanged and unfiltered. The fixed workflow has not yet been
  run on GitHub Actions.
- The workflow also uses `actions/checkout@v6` (Node 24 runtime, tag verified).
- Not run: a clean-tree configure with the final `meson.build` on a second
  machine, and any non-Debian platform.
