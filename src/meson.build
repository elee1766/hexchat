subdir('common')

if get_option('gtk-frontend')
  subdir('fe-gtk')
endif

if get_option('text-frontend')
  subdir('fe-text')
endif

if get_option('theme-manager')
  subdir('htm')
endif

if dbus_enabled
  dbus_runner = find_program('dbus-run-session', required: true)
  dbus_test = executable('dbus-integration-test', 'common/dbus/integration-test.c',
    dependencies: libgio_dep, override_options: ['c_std=c99'])
  # Keep test() executable and args compatible with Meson 0.47.
  dbus_xml = join_paths(meson.current_source_dir(), 'common/dbus/legacy-api.xml')
  if get_option('text-frontend')
    test('D-Bus scripting API', dbus_test,
      args: ['--session', dbus_runner.path(), '', hexchat_text.full_path(),
             dbus_service_name, dbus_xml, 'text'],
      depends: hexchat_text,
      timeout: 60)
  endif
  if get_option('gtk-frontend')
    xvfb_runner = find_program('xvfb-run', required: false)
    if xvfb_runner.found()
      test('D-Bus GTK single instance', dbus_test,
        args: ['--session', dbus_runner.path(), xvfb_runner.path(),
               hexchat_gtk.full_path(), dbus_service_name, dbus_xml, 'gtk'],
        depends: hexchat_gtk, timeout: 60)
    endif
  endif
endif
