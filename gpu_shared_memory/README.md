# Optimización 1: memoria compartida con tiling y halo

Versión de la simulación GPU en la que el kernel de la ecuación de onda carga previamente la malla `current` en memoria compartida antes de realizar el cálculo del laplaciano. Lo que queda del programa (escritura de video, copias host-device y render) es igual a la línea base GPU (`gpu_baseline/`).

## Problema que intenta resolver


El kernel de la línea base, que utiliza un stencil de 5 puntos, computa el laplaciano. Esto significa que cada valor de "current" se lee desde la memoria global hasta cinco veces: una vez como centro y cuatro veces como vecino de las celdas contiguas. El perfilado con `nvprof` de la línea base reveló que había 1 224 962 transacciones globales de lectura por paso, con una ocupación del 86 % y un rendimiento del 85,75 %. La hipótesis era que, debido a que el acceso ya se había coalesced y la ocupación era elevada, la redundancia en las lecturas restringía el rendimiento del kernel.


## Cambio realizado

Únicamente se alteró `src/stepKernel.cu`. El kernel `stepKernel` fue sustituido por `stepKernelShared`.

- Cada bloque de 16 × 16 hilos declara en la memoria compartida un tile de 18 × 18 floats (el bloque más un halo a cada lado que equivale al rango del stencil de cinco puntos).
- Todos los hilos copian la celda de `current` al tile, y los hilos que están en el borde del bloque también copian la celda vecina que se encuentra fuera de él.
- Se garantiza que el tile esté completo antes de realizar los cálculos mediante una barrera `__syncthreads()`.
- Para calcular el laplaciano, se examinan los vecinos a partir del tile y se realizan las mismas operaciones que en la línea base, en el mismo orden.


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
    ./build/drop_simulation --output output/gpu_shared.mp4 --dump-dir validation/gpu_shared
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

Mediciones en la Jetson Nano, con el mismo tamaño de bloque (16 × 16) que la línea base.

| Métrica | Línea base | Memoria compartida |
|---|---:|---:|
| Tiempo total (promedio de 3 corridas) | 47.52 s | 48.71 s |
| Pasos por segundo | 18.94 | 18.48 |
| Speedup respecto a CPU (110 s) | 2.31x | 2.26x |
| Speedup respecto a la versión anterior | — | 0.976x |
| Tiempo total del kernel de la onda (900 llamadas) | 3.212 s | 4.294 s |
| Tiempo promedio por llamada del kernel de la onda | 3.569 ms | 4.772 ms |
| Tiempo total del kernel de render (900 llamadas) | 7.210 s | 7.210 s |
| Tiempo de copias host→device | 4.589 s | 4.589 s |
| Tiempo de copias device→host | 2.415 s | 2.415 s |
| Transacciones de lectura en L2 | 356 765 | 159 447 |
| Instrucciones ejecutadas | 921 600 | 1 241 600 |
| Esperas por `__syncthreads()` | 0 % | 14.87 % |
| Error absoluto máximo respecto a CPU | 5.91e-5 | 5.91e-5 |
| Error relativo máximo respecto a CPU | 1.6e-5 | 1.6e-5 |

**Resultado:** la optimización empeoró el rendimiento. El kernel de la onda fue un 33,7 % más lento (de 3,569 ms a 4,772 ms por llamada) y el tiempo total aumentó un 2,5 % (de 47,52 s a 48,71 s), lo que corresponde a un speedup de 0,976x respecto a la línea base GPU. El aumento del tiempo total (1,19 s) se explica casi por completo por el kernel (1,08 s más en las 900 llamadas), ya que las copias y el render no cambiaron.

La memoria compartida disminuyó en un 55 % el tráfico a la caché L2; sin embargo, el kernel resultó ser un 33.7 % más lento debido a que ejecuta un 34.7 % más de instrucciones (carga del halo, sincronización, índices y escrituras al tile). Como el kernel original no tiene limitaciones de memoria, este ajuste no mejora la eficiencia.

Se llevó a cabo un experimento adicional en el que se utilizaron bloques de 32 × 8. Estos eliminaron los conflictos entre bancos de la versión de 16 × 16 (las transacciones de memoria compartida disminuyeron exactamente a la mitad), sin embargo, no hubo una mejora significativa del kernel, que solo aumentó en un 3,8 %.

## Validación e impacto en la precisión numérica

Al comparar con la línea base GPU a través de `compare_dumps`, los resultados son exactamente iguales en cada bit: error 0 tanto en las alturas como en los cuadros, en los siete cuadros analizados. Esto es porque el kernel ejecuta las mismas operaciones en el mismo orden, pero la única variación es la procedencia de los datos. Por ende, en la precisión numérica no se ve afectada la optimización, y su error con respecto a la CPU es igual al de la línea base (según la validación del Ejercicio E, el máximo error absoluto es de 5.91e-5 y el máximo error relativo es de 1.6e-5).



## Comandos de perfilado

    sudo /usr/local/cuda/bin/nvprof ./build/drop_simulation 2> results/nvprof_shared.txt

    sudo /usr/local/cuda/bin/nvprof --kernels stepKernelShared \
      --metrics gld_transactions,gld_efficiency,shared_load_transactions,achieved_occupancy \
      ./build/drop_simulation --seconds 3 2> results/nvprof_metrics_shared.txt

    sudo /usr/local/cuda/bin/nvprof --kernels stepKernelShared \
      --metrics global_hit_rate,l2_read_transactions,inst_executed,stall_sync \
      ./build/drop_simulation --seconds 3 2> results/nvprof_cache_shared.txt

    ../drop_simulation/compare_dumps ../gpu_baseline/validation/gpu_baseline validation/gpu_shared \
      --csv results/validation_shared_vs_baseline.csv
