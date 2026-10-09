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
cuda_dep = dependency('cuda', required: true)
cpp_comp = meson.get_compiler('cpp')
stdcxx_fs = cpp_comp.find_library('stdc++fs', required: false)

# Obtener el compilador de CUDA para verificar argumentos
cuda_comp = meson.get_compiler('cuda')

# Verificar que --use_fast_math es soportado por esta versión de nvcc
cuda_args = cuda_comp.get_supported_arguments(
  ['--use_fast_math'],
)

executable(
  'drop_simulation',
  [
    'src/main.cpp',
    'src/frameKernel.cu',
    'src/stepKernel.cu',
  ],
  dependencies: [opencv, cuda_dep, stdcxx_fs],
  cuda_args: cuda_args,
  install: false,
)