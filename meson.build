project('frame', 'c',
  version: '1.2.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
    'werror=false',
  ]
)

# Dependencies
cc = meson.get_compiler('c')

sdl3_dep = dependency('sdl3')
sdl3_image_dep = dependency('sdl3-image')
sdl3_ttf_dep = dependency('sdl3-ttf')
libexif_dep = dependency('libexif')
thread_dep = dependency('threads')
m_dep = cc.find_library('m', required: false)

sources = [
  'src/main.c',
  'src/utils.c',
  'src/app.c',
  'src/fileops.c',
  'src/loader.c',
  'src/cache.c',
  'src/viewer.c',
  'src/input.c',
  'src/overlay.c',
  'src/anim.c',
  'src/exif.c',
  'src/prefetch.c',
]

executable('frame',
  sources,
  dependencies: [sdl3_dep, sdl3_image_dep, sdl3_ttf_dep, libexif_dep, thread_dep, m_dep],
  install: true,
)
