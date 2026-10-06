#include "gpu_render.h" // se incluye el header
#include <math.h>
#include <cuda_runtime.h>

#define BLOCK_SIZE 16 // se puede jugar con el tamaño de bloque, esto no esta optimizado

// para no tener que sobreescribir todo el codigo, se definen algunas funciones inline que no son reemplazabales en cuda
// __device__ es una directiva que declara funciones matematicas reutilizables dentro del kernel
__device__ inline int index_of(int x, int y, int width){return y*width+x;}

// kernel de step
__global__ void stepKernel(const float* previous, const float* current, float* next, int width, int height, float c2, float damping, float edge_damping) {
    // aca se calcula el indice global de cada hilo
    int x = blockIdx.x*blockDim.x + threadIdx.x;
    int y = blockIdx.y*blockDim.y + threadIdx.y;
    
    if (x < width && y < height){ // misma idea que con el  calculo de pixeles, se traslada el calculo a varios hilos en paralelo
        const int idx = index_of(x, y, width);
     if (x>0 && x< width -1 && y>0 && y< height -1){      // para evitar que se terminen procesando datos fuera del borde de la imagen,se añade esta condicion
           // los calculos del laplacion y velocidad se mantienen como en el programa original
            const float laplacian = current[idx - 1] + current[idx + 1] + current[idx - width] + current[idx + width] - 4.0f * current[idx];
            const float velocity = current[idx] - previous[idx];
            // lo siguiente es un implementacion de la funcion border_absorption ya que esta no existe en la gpu
            const int dist = min(min(x,y),min(width - 1 - x,height - 1 - y));
            const int band = 32;
            float local_damping = damping;
            if (dist<band){
                    const float t = 1.0f - static_cast<float>(dist) / static_cast<float>(band);
                    local_damping = damping +edge_damping*t*t;
            } 

            next[idx] = 2.0f * current[idx] - previous[idx] + c2 * laplacian - local_damping * velocity;
     } else {
        next[idx] = 0.0f; // los valores del borde valen 0
     }
    }
}

extern "C" void gpu_step(const float* h_prev, const float* h_cur, float* next_h,int width, int height, float wave_speed, float damping, float edge_damping) { // esta es la funcion que se llamara despues en el programa principal
    // aca se reserva la memoria en el dispositivo
    const size_t n = (size_t)width*height; // se sacan las dimensiones de la malla
  
   float* d_prev; // punteros para 3 buffers en el device
   float* d_cur;
   float* d_next;
   // se reserva memoria en funcion de la dimension de la imagen, este paso de fijo es costoso
    cudaMalloc(&d_prev, n*sizeof(float));
    cudaMalloc(&d_cur, n*sizeof(float)); 
    cudaMalloc(&d_next, n*sizeof(float)); 

    // se copia la memoria del host al device, esto tambien es costoso
    // solo se copia previous y current ya que next los calcula el kernel
    // se bajara despues cuando se tenga el resultado
    cudaMemcpy(d_prev, h_prev, n*sizeof(float),cudaMemcpyHostToDevice);
    cudaMemcpy(d_cur, h_cur, n*sizeof(float),cudaMemcpyHostToDevice);
    
    dim3 block(BLOCK_SIZE, BLOCK_SIZE);
    dim3 grid((width  + block.x - 1) / block.x,
              (height + block.y - 1) / block.y);

    //Invocación del Kernel
    stepKernel<<<grid, block>>>(d_prev,d_cur,d_next, width, height,wave_speed*wave_speed,damping, edge_damping); // nota wave_speed*wave_speed = c2 solo se calcula una vez en el host


    cudaMemcpy(next_h, d_next, n*sizeof(float),cudaMemcpyDeviceToHost); // aca se jalan los resultados del device al host, esto tambien es costoso

    // aca se libera la memoria reservada
    cudaFree(d_prev);
    cudaFree(d_cur); 
    cudaFree(d_next); 
}

