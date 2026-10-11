project(
  'drop-simulation',
  'cpp',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++17',
    'warning_level=2',
    'buildtype=release',
  ],
)

# Dependencias para la versión CPU
opencv    = dependency('opencv4', modules: ['core', 'imgproc', 'imgcodecs', 'videoio'])
cpp_comp  = meson.get_compiler('cpp')
stdcxx_fs = cpp_comp.find_library('stdc++fs', required: false)

executable(
  'drop_simulation_cpu',
  [
    'src/main.cpp',
  ],
  dependencies: [
    opencv,
    stdcxx_fs,
  ],
  install: false,
)
