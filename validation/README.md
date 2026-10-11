# Validación CPU vs GPU (Ejercicio E)

Este documento describe qué se agregó al código para poder validar la versión GPU contra la CPU,
cómo ejecutar todo el flujo y el análisis de los resultados de la **versión GPU base**.

## 1. Idea general

El programa original solo genera un video (`.mp4`). Como el video usa compresión con pérdidas, no sirve para
comparar resultados numéricamente. La solución fue que cada versión (CPU y GPU) **guarde resultados binarios**
de algunos cuadros, con exactamente el mismo formato, y una herramienta aparte compare dos carpetas de resultados.

Se guardan dos cosas por cuadro:

| Archivo | Contenido | Para qué sirve |
|---|---|---|
| `height_fNNNNNN.bin` | Malla de alturas completa, `float32` (640×640) | Error numérico real de la simulación |
| `frame_fNNNNNN.bin` | Cuadro en escala de grises, `uint8` | Error visual del renderizado |

`NNNNNN` es el índice de cuadro del ciclo principal (por ejemplo `height_f000100.bin`).

## 2. Compilación

```bash
# Referencia CPU
cd drop_simulation
meson setup build
meson compile -C build

# Herramienta de comparación (un solo .cpp, sin dependencias, fuera de Meson)
g++ -O2 -std=c++17 src/compare_dumps.cpp -o compare_dumps

# Versión GPU
cd ../gpu_baseline
meson setup build
meson compile -C build
```

## 3. Ejecución

```bash
# 1. Referencia CPU. 
cd drop_simulation
./build/drop_simulation_cpu --output output/cpu.mp4 --dump-dir validation/cpu
ls validation/cpu            # deben aparecer 14 archivos (7 height + 7 frame)

# 2. Versión GPU 
cd ../gpu_baseline
./build/drop_simulation_cpu --output output/gpu_baseline.mp4 --dump-dir validation/gpu_baseline

# 3. Comparar contra la referencia CPU
../drop_simulation/compare_dumps ../drop_simulation/validation/cpu gpu_baseline/validation/cpu \
    --csv validation/gpu_baseline.csv
```

### 3.1 Métricas que calcula

**Alturas:** MAE, RMSE, error absoluto máximo con su posición `(x,y)`, `max|ref|` (pico de la referencia) y error relativo
al pico. El e### 4.3 Opciones de `compare_dumps`

```
compare_dumps <dir_referencia> <dir_prueba> [--tol-rel X] [--tol-pixel N] [--csv archivo.csv]
```

| Opción | Por defecto | Significado |
|---|---|---|
| `--tol-rel X` | `1e-4` | Umbral de `max\|err\| / max\|ref\|` para las alturas |
| `--tol-pixel N` | `2` | Diferencia máxima permitida por píxel, en niveles de gris (0–255) |
| `--csv archivo` | (ninguno) | Guarda los resultados en CSV |

Código de salida: `0` todo PASS, `1` algún FAIL, `2` error de uso o lectura.rror se normaliza con el pico porque la amplitud de la onda cambia mucho en el tiempo (decae de ~21 a ~1);
un umbral absoluto sería injusto. Cualquier NaN o Inf en la versión de prueba es FAIL automático.

**Cuadros:** MAE, diferencia máxima, porcentaje de píxeles distintos (`pct_diff`), porcentaje con diferencia mayor a 1 nivel
(`pct_gt1`) y PSNR en dB.

**Columnas del CSV:** `kind, frame, mae, rmse, max_abs, max_x, max_y, ref_scale, pct_diff, pct_gt1, psnr_db, status`.
Las columnas que no aplican a un tipo quedan vacías (por ejemplo `ref_scale` en las filas de cuadros).


## 4. Resultados: GPU base vs CPU

Comparación de `validation/cpu` contra `validation/gpu_baseline`, 7 cuadros. Resultado global: **PASS** en todos.

### 4.1 Malla de alturas (float32)

| Cuadro | MAE | RMSE | max\|err\| | max\|ref\| | Error relativo al pico | Posición (x,y) |
|---|---|---|---|---|---|---|
| 0 | 2.1e-12 | 4.2e-10 | 1.19e-7 | 2.339 | 5.1e-8 | (305,327) |
| 1 | 4.4e-12 | 7.3e-10 | 2.38e-7 | 3.668 | 6.5e-8 | (305,327) |
| 10 | 4.1e-10 | 1.8e-8 | 2.86e-6 | 14.88 | 1.9e-7 | (307,300) |
| 100 | 2.4e-7 | 1.8e-6 | 5.91e-5 | 21.04 | 2.8e-6 | (314,311) |
| 300 | 9.5e-7 | 2.8e-6 | 3.05e-5 | 6.387 | 4.8e-6 | (319,380) |
| 600 | 2.2e-6 | 3.6e-6 | 1.82e-5 | 1.973 | 9.2e-6 | (265,277) |
| 899 | 4.5e-6 | 5.7e-6 | 1.88e-5 | 1.198 | 1.6e-5 | (314,312) |

### 4.2 Cuadros (escala de grises)

| Cuadro | MAE | Dif. máx. | Píxeles distintos | Píxeles con dif. > 1 | PSNR (dB) |
|---|---|---|---|---|---|
| 0 | 0 | 0 | 0 % | 0 % | inf |
| 1 | 0 | 0 | 0 % | 0 % | inf |
| 10 | 0 | 0 | 0 % | 0 % | inf |
| 100 | 3.9e-5 | 1 | 0.0039 % (16 px) | 0 % | 92.2 |
| 300 | 1.7e-4 | 1 | 0.0166 % (68 px) | 0 % | 85.9 |
| 600 | 2.6e-4 | 1 | 0.0256 % (105 px) | 0 % | 84.0 |
| 899 | 3.7e-4 | 1 | 0.0371 % (152 px) | 0 % | 82.4 |

Los conteos de píxeles son sobre 409 600 (640×640).

## 5. Análisis

- **La GPU base es equivalente a la CPU dentro de la tolerancia.** El peor error relativo de las alturas es 1.6e-5
  (cuadro 899), unas 6 veces por debajo del umbral de 1e-4. El mayor error absoluto es 5.9e-5, en el cuadro 100, cuando la
  onda tiene su máxima amplitud (~21).
- **No hay NaN ni Inf**, y el error no crece de forma explosiva: el MAE sube de forma gradual, de ~1e-12 en el cuadro 0
  a ~4.5e-6 en el 899. Es el comportamiento esperado de un esquema numéricamente estable (la condición de estabilidad
  se cumple con `c² = 0.2025`) en el que el redondeo de float32 se acumula a lo largo de 900 pasos.
- **El error relativo crece con el tiempo** (de 5e-8 a 1.6e-5) aunque el absoluto máximo no lo haga, porque la amplitud
  de la onda decae por el amortiguamiento y el error se normaliza con ese pico cada vez menor.
- **Visualmente, las versiones son prácticamente indistinguibles.** La diferencia máxima es 1 nivel de gris en
  todos los cuadros, ningún píxel difiere en más de 1, y como mucho 152 píxeles de 409 600 (0.037 %) difieren. El PSNR
  está entre 82 y 92 dB. Los cuadros 0, 1 y 10 son idénticos: ahí los errores de las alturas (≤3e-6) no alcanzan
  a cambiar el valor entero de ningún píxel.
- **Los errores numéricos y visuales son coherentes.** La diferencia de 1 nivel de gris aparece justo cuando el error en
  las alturas crece (cuadro 100 en adelante).