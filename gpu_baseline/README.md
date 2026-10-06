# Ejercicio D: porteo de funciones críticas a GPU
Prototipo GPU en C++,CUDA y OpenCV para simular la caida de una gota sobre un estanque
usando una ecuacion de onda 2D amortiguada.

Esta version inicial del programa en GPU añade  en la carpeta src/ tres archivos fuente extra: frameKernel.cu que es un programa de CUDA que implementa el calculo de los pixeles en paralelo en la GPU,stepKernel.cu que es el kernel que implementa el paso de tiempo de simulacion, y gpu_render.h que es un archivo de cabecera que permite incluir los  wrapper de los kernls implementados (la funcion del host) en el programa main de la simulacion de la gota.Se implementaron estas funciones ya que en el perfilado de CPU se identificaron como las funciones criticas. Tambien se hicieron cambios en el meson file y el programa main.cpp para que se pueda correr en la jetson. Se comparó esta version con la version de solo CPU y se encontró que este programa dura unos 47s mientras que la versión de CPU dura unos 110s (en la jetson) obteniendo un speedup de unas 2.34 veces. Hay optimizaciones que se pueden seguir realizando cuyas recomendaciones estan en el codigo de CUDA. 


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

## Idea fisica

Cada celda guarda la altura de la superficie del agua. La actualizacion usa un
stencil de 5 puntos:

```text
laplaciano = izquierda + derecha + arriba + abajo - 4 * centro

h_next = 2 * h_current - h_previous
         + c^2 * dt^2 * laplaciano
         - damping * (h_current - h_previous)
```

Para visualizar, se calculan normales aproximadas con diferencias finitas y se
aplica iluminacion simple. Esto produce un efecto de agua sin requerir un motor
3D.
