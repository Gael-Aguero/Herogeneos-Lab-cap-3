## Ejercicio C - Perfilado del código en CPU

### Resultados de perf record

| Grupo | Funciones incluidas | % del tiempo |
|---|---|---:|
| Renderizado | `render_frame`, `cv::normalize`, `cv::norm`, `std::clamp`, `std::max`, operaciones de `cv::Vec` | ≈ 55,6 % |
| Potencia `powf` | `powf` de `libm` | 15,2 % |
| Absorción de bordes | `border_absorption`, `std::min(initializer_list)`, `std::__min_element` | ≈ 13,6 % |
| Actualización de la onda | `simulate_step` (stencil de 5 puntos) y acceso a los vectores | ≈ 5,8 % |
| Codificación de video | `libavcodec`, `libswscale` (usadas por `cv::VideoWriter`) | ≈ 6,7 % |
| Otros | inicialización, `memcpy`, kernel | ≈ 3 % |


### Funciones más críticas

El perfilado indica que el programa dedica la mayor parte de su tiempo a renderizar cada cuadro. Aproximadamente el 55,6 % del tiempo de ejecución se utiliza en la función `render_frame`, así como las operaciones que esta llama para determinar la normal de la superficie en cada píxel. Las operaciones vectoriales, `std::max`, `std::clamp`, `cv::norm` y `cv::normalize` son ejemplos de estas últimas. La función `powf`, perteneciente a la biblioteca matemática, representa un 15,2 % más.

La segunda función esencial es la actualización de la malla, `simulate_step`, que emplea aproximadamente el 19,4 % en conjunto con `border_absorption`. Es notable que en ese grupo el cálculo del factor de absorción en los bordes (≈13,6 %) es más del doble que el stencil de la ecuación de onda (≈5,8 %). Esto se debe a que la distancia mínima a los cuatro bordes se calcula usando `std::min` sobre una lista de inicialización para cada celda y cada paso.

La codificación del video con OpenCV y FFmpeg (`libavcodec` y `libswscale`) representa apenas un 6,7 % del tiempo.

### Porcentajes en el build optimizado

Los porcentajes mencionados previamente se derivan de un build compilado con `-fno-inline`, el cual facilita asignar tiempo a cada función, pero sobreestima el costo de funciones pequeñas auxiliares como `std::clamp` o `std::max`, dado que cada invocación tiene un costo. En el build optimizado sin esta bandera, el compilador incorpora la totalidad del cálculo en `main` (56,4 %), mientras que `powf` llega al 27 % del tiempo. Esto valida que calcular la potencia en punto flotante es una de las operaciones más costosas del programa.

### Resultados deGoogle Performance Tools 

La segunda herramienta empleada fue Google Performance Tools (gperftools), la cual se conectó con `libprofiler` y se ejecutó utilizando la variable `CPUPROFILE`. Para la simulación completa (900 pasos) y para perf, se utilizó la misma configuración de compilación (`-O2 -g -fno-omit-frame-pointer -fno-inline`). El perfil se examinó utilizando `google-pprof --text`.

| Función | % propio | % acumulado |
|---|---:|---:|
| `render_frame` | 13,6 % | 56,3 % |
| `simulate_step` | 5,4 % | 19,0 % |
| `border_absorption` (llamada por `simulate_step`) | 3,5 % | 12,2 % |
| `powf` (con sus rutinas internas `exp2`, `log2`, `vrndn_f64`) | — | 15,5 % |

### Comparación entre herramientas

| Grupo | perf | gperftools |
|---|---:|---:|
| Renderizado (`render_frame` y funciones que invoca) | ≈ 55,6 % | 56,3 % |
| Potencia `powf` | 15,2 % | 15,5 % |
| Absorción de bordes | ≈ 13,6 % | 12,2 % |
| Actualización de la onda (stencil) | ≈ 5,8 % | ≈ 6,8 % |
| Codificación de video (FFmpeg) | ≈ 6,7 % | ≈ 8,7 % |

Tanto el stencil de la ecuación de onda como la codificación del video tienen un peso menor que las otras herramientas, y estas dos últimas coinciden en que el renderizado es la parte más cara del programa, con `powf` y la absorción de bordes.

### Función a portar primero a GPU

`render_frame` es la función que resulta más conveniente portar primero, ya que requiere mucho tiempo y su operación es independiente por píxel, cada uno de los píxeles de salida depende únicamente de su celda y de sus vecinos más cercanos, lo cual la convierte en perfecta para asignar un hilo de CUDA por cada píxel.

No obstante, portar únicamente una de las dos funciones limita el beneficio. La Ley de Amdahl establece que, si solo se lleva `simulate_step` (cerca del 19 %), el speedup máximo quedaría en 1/(1 − 0,19) ≈ 1,24x. En cambio, si se portan la actualización de la malla y el render simultáneamente, esto abarcaría aproximadamente el 90 % del tiempo y tendría un speedup teórico máximo próximo a 10x. Asimismo, si únicamente una de las dos funciones se llevara a cabo en la GPU, habría que duplicar la malla entre el sistema host y el dispositivo en cada etapa. Por eso se aconseja portar las dos, empezando por el render, y mantener la malla residente en la memoria de la GPU.


### Funciones que no conviene portar

- **Escritura del video:** Utiliza solamente el 6,7 % del tiempo. Depende de las bibliotecas de FFmpeg y OpenCV que funcionan en la CPU, y el cuadro final tiene que estar en la memoria del host para ser entregado al `VideoWriter`.
- **Inicialización de la gota:** Se lleva a cabo una única vez y su coste es mínimo (inferior al 0,4 %).
- **Ciclo principal y configuración:**  son secuenciales, sin paralelismo que se pueda aprovechar.





