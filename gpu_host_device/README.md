# Optimización 2: reducción de la comunicación host-device

Versión donde las mallas se mantienen en la memoria de la GPU a lo largo de toda la ejecución. Las mallas se trasladan a la GPU solo una vez al principio, y en cada fotograma únicamente la imagen final que requiere el `VideoWriter` es la que se copia al host. Los kernels (`stepKernel` y `frameKernel`) son iguales a los de la línea base GPU (`gpu_baseline/`).

Para esta optimización se utilizaron y aplicaron los principios de capítulo 3 del libro del curso, los cuales son la residencia de datos y la reutilización de buffers (pág 137-138).

Esta optimización se desarrolló sobre la línea base de GPU y no sobre la versión con memoria compartida, ya que esta última fue eliminada por no ser eficiente.

## Problema que intenta resolver

El libro, en la primera parte de la sección 3.4 (pág. 129), advierte que, para un algoritmo de GPU, el trabajo a realizar debe ser más abundante que los datos transferidos para evitar estar subiendo y bajando información entre la GPU y la CPU de manera continua. La línea base, por el contrario, reservaba tres buffers en la GPU en cada etapa de simulación; transfería `previous` y `current` del host al dispositivo; ejecutaba el kernel; devolvía `next` al host y liberaba la memoria. El render seguía el mismo modelo en cada cuadro, con la imagen y la malla. 

Se corroboró el inconveniente a partir del perfilado de la línea base con `nvprof`: 2700 copias host-device, 1800 copias device-host y 4500 llamadas a `cudaMalloc` y `cudaFree` se registraron en 900 cuadros. Los tiempos de estas llamadas en la API de CUDA (22.17 s en `cudaMemcpy`, 6.12 s en `cudaMalloc` y 1.44 s en `cudaFree`) eran mucho mayores que el tiempo total de los dos kernels (10.42 s). Se planteó la hipótesis de que, aunque los kernels no se modificaran, conservar los datos en la unidad de procesamiento gráfico (GPU) disminuiría el tiempo total.

## Cambio realizado

**Reutilización de los buffers (pág. 138).** El libro sugiere la idea de reservar los buffers solo una vez y volver a utilizarlos en todas las iteraciones, en vez de reservarlos y liberarlos cada vez. En `src/frameKernel.cu` y `src/stepKernel.cu`, se declaran como variables estáticas los punteros a la memoria de la GPU, y solamente en la primera llamada (`if (... == nullptr)`) se reserva la memoria, además, se borraron todas las instancias de `cudaFree` que había dentro del ciclo.

**Residencia de los datos (páginas 137-138).** El libro detalla la técnica de pasar los datos a la GPU una vez, enlazar múltiples operaciones dentro de ella y solo transferir el resultado. En esta edición:

- En la primera llamada de `gpu_step`, únicamente se copian las mallas iniciales a la GPU.
- El render y el paso de simulación funcionan sobre los mismos datos en la GPU, tal como lo hacen las operaciones sucesivas de la ecuación 3.24. En vez de recibir la malla copiada desde el host, el kernel de render toma la malla directamente de la memoria de la GPU.
- La única transferencia por cuadro es la imagen final, que es el único dato requerido por la CPU para el `VideoWriter`. Esto se adhiere a la sugerencia de la sección "Memoria del host y transferencias" (pág. 142), que aconseja reducir la frecuencia con la que se realizan las transferencias.

Para poder realizar estos cambios se añadieron tres componentes:

- En vez de duplicar el resultado en el host, se hace un intercambio entre los punteros a las tres mallas (`d_prev - d_cur`, `d_cur - d_next`). Esto consiste en implementar la residencia de datos en una simulación de múltiples fases, y reproducir en la GPU la rotación que previamente realizaba main.cpp con los vectores.
- gpu_current_device(): Facilita la conexión entre el render y la malla actual en la GPU, aunque se encuentren en archivos distintos.
- gpu_copy_to_host: Copia la malla al host únicamente en los cuadros que se emplean para validar el ejercicio E, ya que el vector `current` del host no se actualiza en cada iteración. 

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
./build/drop_simulation --output output/gpu_host_device.mp4 --dump-dir validation/gpu_host_device
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

Mediciones en la Jetson Nano.

| Métrica | Línea base | Reducción host-device |
|---|---:|---:|
| Tiempo total (promedio de 3 corridas) | 47.52 s | 31.41 s |
| Pasos por segundo | 18.94 | 28.65 |
| Speedup respecto a CPU (110 s) | 2.31x | 3.50x |
| Speedup respecto a la versión anterior | — | 1.51x |
| Tiempo total del kernel de la onda (900 llamadas) | 3.212 s | 3.211 s |
| Tiempo total del kernel de render (900 llamadas) | 7.210 s | 7.211 s |
| Copias host-device | 2700 llamadas, 4.589 s | 2 llamadas, 0.003 s |
| Copias device-host | 1800 llamadas, 2.415 s | 900 llamadas, 1.037 s |
| Llamadas a `cudaMalloc` | 4500 (6.121 s) | 4 (0.470 s) |
| Llamadas a `cudaFree` | 4500 (1.441 s) | 0 |
| Error absoluto máximo respecto a CPU | 5.91e-5 | 5.91e-5 |
| Error relativo máximo respecto a CPU | 1.6e-5 | 1.6e-5 |

**Resultado:** El funcionamiento mejoró gracias a la optimización. El tiempo total disminuyó de 47.52 s a 31.41 s (−33.9 %), lo que equivale a un speedup del 1.51x en comparación con la línea base GPU y del 3.50x en comparación con la CPU.

El libro describe la duración de una ejecución en GPU como la suma del lanzamiento, las transferencias, el kernel y la sincronización. Esta optimización disminuye el término de las transferencias, conservando el del kernel: los tiempos de `stepKernel` y `frameKernel` son iguales a los de la línea base, y la mejora se debe completamente a la administración de memoria y la comunicación. Además, la CPU dedicaba la mayor parte de su tiempo a esperar las llamadas para gestionar la memoria y comunicarse:
| Llamada a la API | Línea base | Reducción host-device | Ahorro |
|---|---:|---:|---:|
| `cudaMemcpy` | 22.17 s | 13.04 s | 9.13 s |
| `cudaMalloc` | 6.12 s | 0.47 s | 5.65 s |
| `cudaFree` | 1.44 s | 0 s | 1.44 s |
| **Total** | | | **16.22 s** |

La disminución del tiempo total (16.11 s) es igual al ahorro total en la API (16.22 s). La memoria del host convencional no puede utilizarse directamente para realizar transferencias eficientes, por lo tanto, como el driver primero tiene que mover los datos hacia un buffer intermedio cuando se copian desde la memoria paginable, este gasto por parte de la CPU se eliminó junto con las copias. Por ello, el ahorro en "cudaMemcpy" es superior al tiempo que se reduce en las copias realizadas en la GPU. Los 13.04 segundos que quedan en `cudaMemcpy` son, sobre todo, tiempo de espera, ya que, como la copia de la imagen es sincrónica, se debe esperar a que acaben los dos kernels de cada cuadro. 


## Validación e impacto en la precisión numérica

Los resultados son exactamente iguales a los de la línea base de GPU, tal como se ha verificado con `compare_dumps`: error 0 en las alturas y en los cuadros, para cada uno de los siete cuadros examinados. Esto se explica porque los kernels efectúan exactamente las mismas operaciones, solo varía el sitio donde se almacenan los datos entre pasos. La residencia de los datos no altera ningún cálculo, por lo que no existe una compensación entre rapidez y precisión, en contraste con las optimizaciones de matemática aproximada o de precisión reducida que se explican en el libro (páginas 158-166). Conforme a la validación del ejercicio E, el error relacionado con la CPU es el mismo que el de la línea base: un error absoluto máximo de 5.91e-5 y un error relativo máximo de 1.6e-5.

## Comandos de perfilado

```bash
sudo /usr/local/cuda/bin/nvprof ./build/drop_simulation 2> results/nvprof_host_device.txt

../drop_simulation/compare_dumps ../gpu_baseline/validation/gpu_baseline validation/gpu_host_device \
    --csv results/validation_host_device_vs_baseline.csv > results/validation_host_device_vs_baseline.txt
```
