# Optimización 3: uso de `--use_fast_math`

Versión de la GPU en la que el código CUDA se compila con la opción `--use_fast_math` de `nvcc`. El código fuente (`main.cpp`, `stepKernel` y `frameKernel`) es **idéntico** al de la línea base GPU (`gpu_baseline/`); lo único que cambia es la forma de compilar los archivos `.cu`.

## Cambio realizado

Se agregó `--use_fast_math` a los argumentos de `nvcc` del ejecutable en `meson.build`:

```meson
executable(
  'drop_simulation',
  [ 'src/main.cpp', 'src/frameKernel.cu', 'src/stepKernel.cu' ],
  cuda_args: ['--use_fast_math'],
  dependencies: opencv,
  install: false,
)
```

## Compilar

```bash
meson setup build
meson compile -C build
```

## Ejecutar

```bash
./build/drop_simulation
```

Por defecto genera:

```text
output/drop_simulation.mp4
```

Para guardar los resultados binarios de validación:

```bash
./build/drop_simulation --output output/gpu_fast_math.mp4 --dump-dir validation/gpu_fast_math
```

## Parametros actuales

```text
width             640
height            640
seconds           30
fps               30
steps_per_frame   1
wave_speed        0.45
damping           0.006
edge_damping      0.035
drop_radius       18
drop_strength     1.0
output            output/drop_simulation.mp4
```

Estos valores estan definidos en `src/main.cpp`, dentro de `Config`.

## Resultados

Mediciones en la Jetson Nano con `nvprof`, 900 cuadros. Los valores de la línea base son los de su corrida de `nvprof` sin la opción.

| Métrica | Línea base | `--use_fast_math` | Cambio |
|---|---:|---:|---:|
| Tiempo por llamada de `frameKernel` | 8.0115 ms | 5.9288 ms | −26.0 % (1.35x) |
| Tiempo total de `frameKernel` (900 llamadas) | 7.210 s | 5.336 s | −1.874 s |
| Tiempo por llamada de `stepKernel` | 3.5690 ms | 3.5678 ms | ≈ 0 |
| Tiempo total de `stepKernel` (900 llamadas) | 3.212 s | 3.211 s | ≈ 0 |
| Tiempo total de los dos kernels | 10.42 s | 8.55 s | −18.0 % (1.22x) |
| Copias host-device | 2700 llamadas, 4.589 s | 2700 llamadas, 4.585 s | ≈ 0 |
| Copias device-host | 1800 llamadas, 2.415 s | 1800 llamadas, 2.415 s | ≈ 0 |
| `cudaMemcpy` (API) | 22.17 s | 20.02 s | −2.16 s |
| `cudaMalloc` (API) | 6.12 s | 6.03 s | −0.09 s |
| `cudaFree` (API) | 1.44 s | 1.37 s | −0.07 s |
| Error absoluto máximo respecto a CPU (alturas) | 5.91e-5 | 5.91e-5 | igual |
| Error relativo máximo respecto a CPU (alturas) | 1.6e-5 | 1.4e-5 | igual (mismo orden) |
| Diferencia máxima respecto a CPU (cuadros) | 1 nivel de gris | 1 nivel de gris | igual |
| Tiempo total | 47.52 s (promedio de 3 corridas) | 46.79 s (1 corrida, con `nvprof`)* | −0.73 s |
| Pasos por segundo | 18.94 | 19.24* | |
| Speedup respecto a CPU (110 s) | 2.31x | 2.35x* | |
| Speedup respecto a la versión anterior | — | 1.02x* | |


**Resultado:** el tiempo del kernel de render bajó un 26 %, de 8.01 ms a 5.93 ms por llamada, mientras que el kernel de paso quedó igual. Esto confirma la hipótesis: la mejora se concentra en el kernel que usa `powf`, y `stepKernel` actúa como control, ya que su tiempo cambió solo un 0.03 %. Las copias, las reservas de memoria y el resto de las llamadas a la API no cambiaron de forma apreciable.

Como el tiempo del kernel de render es muy estable entre llamadas (entre su mínimo y su máximo hay menos de 0.7 %), la mejora de 26 % está muy por encima de la variación de una llamada a otra.

El ahorro en los kernels es de 1.88 s. Si el resto del tiempo no cambia, equivale a un ahorro del ~4 % sobre los 47.52 s de la línea base, es decir, un speedup esperado de ≈ 1.04x. Esto es un cálculo a partir de los tiempos de los kernels y no una medición del total. El ahorro en `cudaMemcpy` (2.16 s) es del mismo orden que el de los kernels porque las copias son sincrónicas: la llamada espera a que terminen los kernels, y por eso el tiempo de ejecución de estos aparece también dentro de ella.

La mejora es modesta porque los kernels son una fracción menor del tiempo total. Como el libro describe, la duración de una ejecución en GPU es la suma del lanzamiento, las transferencias, el kernel y la sincronización; esta optimización reduce solo el término del kernel de render, y los demás términos (en la línea base dominados por las transferencias y las reservas de memoria) quedan intactos.

Esta optimización es complementaria a la reducción de comunicación host-device. Si se combinaran, los kernels representarían una fracción mayor del tiempo (10.42 s de los 31.41 s de la versión con residencia de datos), y el ahorro de 1.88 s correspondería a un ~6 % (speedup esperado ≈ 1.06x). Es una estimación a partir de los tiempos individuales; no está medida.

## Validación e impacto en la precisión numérica

La opción `--use_fast_math` sí cambia el resultado numérico, a diferencia de la reducción de comunicación host-device, que dio resultados idénticos. Se comparó contra la CPU con `compare_dumps` en siete cuadros; el resultado global fue **PASS**.

### Malla de alturas (float32) respecto a la CPU

| Cuadro | MAE | RMSE | max\|err\| | max\|ref\| | Error relativo al pico | Posición (x,y) |
|---|---|---|---|---|---|---|
| 0 | 2.1e-12 | 4.2e-10 | 1.19e-7 | 2.339 | 5.1e-8 | (305,327) |
| 1 | 4.4e-12 | 7.3e-10 | 2.38e-7 | 3.668 | 6.5e-8 | (305,327) |
| 10 | 4.1e-10 | 1.8e-8 | 2.86e-6 | 14.88 | 1.9e-7 | (307,300) |
| 100 | 2.4e-7 | 1.8e-6 | 5.91e-5 | 21.04 | 2.8e-6 | (314,311) |
| 300 | 9.5e-7 | 2.8e-6 | 3.05e-5 | 6.387 | 4.8e-6 | (319,380) |
| 600 | 2.3e-6 | 3.7e-6 | 1.98e-5 | 1.973 | 1.0e-5 | (307,375) |
| 899 | 4.4e-6 | 5.6e-6 | 1.73e-5 | 1.198 | 1.4e-5 | (300,252) |

### Cuadros (escala de grises) respecto a la CPU

| Cuadro | MAE | Dif. máx. | Píxeles distintos | Píxeles con dif. > 1 | PSNR (dB) |
|---|---|---|---|---|---|
| 0 | 0 | 0 | 0 % | 0 % | inf |
| 1 | 0 | 0 | 0 % | 0 % | inf |
| 10 | 0 | 0 | 0 % | 0 % | inf |
| 100 | 3.9e-5 | 1 | 0.0039 % (16 px) | 0 % | 92.2 |
| 300 | 1.6e-4 | 1 | 0.0161 % (66 px) | 0 % | 86.1 |
| 600 | 2.7e-4 | 1 | 0.0266 % (109 px) | 0 % | 83.9 |
| 899 | 3.4e-4 | 1 | 0.0344 % (141 px) | 0 % | 82.8 |

Los conteos de píxeles son sobre 409 600 (640×640).

### Análisis

- **El costo en precisión es prácticamente nulo.** El error relativo máximo de las alturas es de 1.4e-5, unas 7 veces por debajo de la tolerancia de 1e-4, y el error absoluto máximo (5.91e-5, en el cuadro 100) es el mismo que el de la línea base. En los cuadros, la diferencia máxima sigue siendo de 1 nivel de gris y ningún píxel difiere en más de 1.
- **Los errores son del mismo orden que los de la línea base, con variaciones en ambos sentidos.** Respecto a la línea base, el error de las alturas es idéntico hasta el cuadro 100 y cambia ligeramente desde el 300 (por ejemplo, MAE en el cuadro 600: 2.22e-6 → 2.31e-6; error máximo en el cuadro 899: 1.88e-5 → 1.73e-5), y el número de píxeles distintos pasa de 68 a 66 (cuadro 300), de 105 a 109 (600) y de 152 a 141 (899). No hay un empeoramiento sistemático; lo que cambia es el patrón del redondeo acumulado.
- **El resultado ya no es idéntico bit a bit a la línea base.** Eso confirma que la opción modificó el código generado, y es coherente con el cambio de las funciones aproximadas, la división y el vaciado de denormales (`--ftz`). No se verificó a nivel de instrucciones cuál de estos factores causa las diferencias de las alturas.
- **Compromiso entre precisión y velocidad.** Se obtuvo una mejora del 26 % en el kernel de render a cambio de errores que quedan dentro de la tolerancia y son del mismo orden que los de la línea base. En este problema el compromiso es favorable, porque la salida es una imagen de 8 bits: las diferencias de coma flotante casi nunca alcanzan a cambiar un píxel (solo el 0.03 % de los píxeles cambia, y en un único nivel de gris).

## Comandos de perfilado y validación

```bash
nvprof ./build-baseline/drop_simulation 2> results/nvprof_baseline_sin_flags.txt
nvprof ./build/drop_simulation 2> results/nvprof_fast_math.txt

../drop_simulation/compare_dumps ../drop_simulation/validation/cpu validation/gpu_fast_math \
    --csv results/validation_fast_math_vs_cpu.csv > results/validation_fast_math_vs_cpu.txt
```