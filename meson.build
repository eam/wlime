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
libhangul = dependency('libhangul', required: false)
anthy = dependency('anthy', required: false)
librime = dependency('rime', required: false)

extra_cpp_args = []
if libhangul.found()
  extra_cpp_args += '-DHAVE_HANGUL'
endif
if anthy.found()
  extra_cpp_args += '-DHAVE_ANTHY'
endif
if librime.found()
  extra_cpp_args += '-DHAVE_RIME'
endif

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

all_deps = [wayland_client, gtk3, gtk_layer_shell, xkbcommon, cairo, libpinyin]
if libhangul.found()
  all_deps += libhangul
endif
if anthy.found()
  all_deps += anthy
endif
if librime.found()
  all_deps += librime
endif

executable('wlime',
  'src/main.cpp',
  'src/overlay.cpp',
  'src/ime.cpp',
  'src/engine.cpp',
  'src/engine_pinyin.cpp',
  'src/engine_hangul.cpp',
  'src/engine_anthy.cpp',
  'src/engine_rime.cpp',
  'src/config.cpp',
  'src/sound.cpp',
  'src/control.cpp',
  protocol_sources,
  protocol_headers,
  cpp_args: extra_cpp_args,
  dependencies: all_deps,
  install: true,
)
