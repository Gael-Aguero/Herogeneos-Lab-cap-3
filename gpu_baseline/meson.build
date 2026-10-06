project(
  'drop-simulation',
  'cpp',
'cuda',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++17',
    'warning_level=3',
    'buildtype=release',
  ],
)

opencv = dependency('opencv4', modules: ['core', 'imgproc', 'imgcodecs', 'videoio'])
cuda_dep   = dependency('cuda', required : true)
cpp_comp = meson.get_compiler('cpp')
stdcxx_fs = cpp_comp.find_library('stdc++fs', required: false)

executable(
  'drop_simulation',
[  'src/main.cpp',
 'src/frameKernel.cu'],
 
 dependencies:[ opencv,cuda_dep,stdcxx_fs,],
  install: false,
)

