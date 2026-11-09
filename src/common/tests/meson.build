ssl_tests = executable('ssl_tests', 'ssl-tests.c',
  dependencies: [hexchat_common_dep, libssl_dep],
  include_directories: config_h_include,
)

test('TLS Tests', ssl_tests,
  protocol: 'tap',
  timeout: 120,
)
