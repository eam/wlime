project('wlime', 'c', 'cpp',
  version: '0.1.0',
  default_options: ['cpp_std=c++17', 'warning_level=2'],
)

wayland_client = dependency('wayland-client')
wayland_scanner = find_program('wayland-scanner')
gtk3 = dependency('gtk+-3.0')
gtk_layer_shell = dependency('gtk-layer-shell-0')
xkbcommon = dependency('xkbcommon')
cairo = dependency('cairo')
libpinyin = dependency('libpinyin')

# Protocol code generation
wl_protocol_dir = '/usr/share/wayland-protocols'
wlr_protocol_dir = '/usr/share/wlr-protocols'
local_protocol_dir = meson.project_source_root() / 'protocols'

protocols = {
  'xdg-shell': wl_protocol_dir / 'stable' / 'xdg-shell' / 'xdg-shell.xml',
  'wlr-layer-shell-unstable-v1': wlr_protocol_dir / 'unstable' / 'wlr-layer-shell-unstable-v1.xml',
  'input-method-unstable-v2': local_protocol_dir / 'input-method-unstable-v2.xml',
  'virtual-keyboard-unstable-v1': local_protocol_dir / 'virtual-keyboard-unstable-v1.xml',
}

protocol_sources = []
protocol_headers = []

foreach name, path : protocols
  protocol_sources += custom_target(
    name + '-protocol-code',
    input: path,
    output: name + '-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
  protocol_headers += custom_target(
    name + '-client-header',
    input: path,
    output: name + '-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
endforeach

executable('wlime',
  'src/main.cpp',
  'src/overlay.cpp',
  'src/ime.cpp',
  'src/engine.cpp',
  'src/config.cpp',
  'src/sound.cpp',
  protocol_sources,
  protocol_headers,
  dependencies: [wayland_client, gtk3, gtk_layer_shell, xkbcommon, cairo, libpinyin],
  install: true,
)
