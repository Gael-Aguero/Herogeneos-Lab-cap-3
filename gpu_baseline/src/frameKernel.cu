#include "gpu_render.h" // se incluye el header
#include <math.h>
#include <cuda_runtime.h>

#define BLOCK_SIZE 16 // se puede jugar con el tamaño de bloque, esto no esta optimizado

// para no tener que sobreescribir todo el codigo, se definen algunas funciones inline que no son reemplazabales en cuda
// __device__ es una directiva que declara funciones matematicas reutilizables dentro del kernel
__device__ inline int index_of(int x, int y, int width){return y*width+x;}
__device__ inline float clampf(float value, float low, float high) {return fmaxf(low,fminf(value,high));} // esta funcion funciona como un "saturador" , por lo que se usara fmax y fmin para implementarlo

// kernel de renderizado 
__global__ void frameKernel(const float*height, uchar3* out, int width, int h, float3 L) {
    // aca se calcula el indice global de cada hilo
    int x = blockIdx.x*blockDim.x + threadIdx.x;
    int y = blockIdx.y*blockDim.y + threadIdx.y;
    
    if (x < width && y < h){ // la idea es que cada hilo haga todo el calculo de cada pixel en paralelo,
    // por lo que se copia el codigo y se reemplazan las funciones por las instrucciones a cuda

            // estos valores son enteros por lo que se usara max() y min()
            // aca se calculan los indices de pixeles vecinos para obtener el gradiente despues
            const int xm = max(0, x - 1);
            const int xp = min(width - 1, x + 1);
            const int ym = max(0, y - 1);
            const int yp = min(h - 1, y + 1);
            //obtencion del gradiente
            const float dx = height[index_of(xm, y, width)] - height[index_of(xp, y, width)];
            const float dy = height[index_of(x, ym, width)] - height[index_of(x, yp, width)];
            
            //aca se calcula el vector normal del pixel a mano ya que no hay funcion en cuda para sacar la normal de un vector
            const float vx = 2.8f * dx;
            const float vy = 2.8f*dy ;
            const float vz = 1.0f ;

            const float inv = rsqrtf(vx*vx + vy*vy + vz*vz); // nota:rsqrtf(x) hace 1/sqrt(x) 

            const float  nx = vx *inv ; // aca se calculan las coordenadas de la normal 
            const float  ny = vy *inv ;
            const float  nz = vz *inv ;
           
            // estos son flotantes por lo que se usa fmaxf
            const float diffuse = fmaxf(0.0f, nx*L.x + ny*L.y + nz*L.z); // calcula cuanta luz recibe el pixel
            const float wave = clampf(0.5f + 1.8f * height[index_of(x, y, width)], 0.0f, 1.0f); // calcula el brilo del pixel segun su altura en la onda
            const float specular = powf(fmaxf(0.0f, diffuse), 24.0f); // calcula reflejos segun la luz

            float intensity = 35.0f + 120.0f * wave; // combina todos los efectos calculados anteriormente
            intensity *= 0.60f + 0.65f * diffuse;
            intensity += 130.0f * specular;

            const unsigned char gray = (unsigned char)(clampf(intensity, 0.0f, 255.0f)); // aca se convierten los valores a pixel con un valor entre 0 y 255
            out[index_of(x,y,width)]= make_uchar3(gray,gray,gray); // aca se escribe el mismo valor en los 3 canales del pixel para obtener la escala de grises
            // nota: make_uchar3(gray,gray,gray) hace un paquete de 3 valores tipo unsigned char
    }
}

extern "C" void gpu_render(const float*height_host, unsigned char* out_gray, int width, int height, float lx, float ly, float lz) { // esta es la funcion que se llamara despues en el programa principal
    //nota: lz,lx y ly son las coordenadas de los vectores de luz que se calculan en main, se pasan asi debido a que sino no se reconocen en el header
    // aca se reserva la memoria en el dispositivo
    const size_t n = (size_t)width*height; // se sacan las dimensiones de la imagen
   float* d_height; // punteros para las variables de height e image en el device
   uchar3*d_image;
   // se reserva memoria en funcion de la dimension de la imagen, este paso de fijo es costoso ya que se hace por cada frame, un punto de optimizacion puede ser aca
    cudaMalloc(&d_height, n*sizeof(float));
    cudaMalloc(&d_image, n*sizeof(uchar3)); 

    // se copia la memoria del host al device, esto tambien es costoso
    cudaMemcpy(d_height, height_host, n*sizeof(float),cudaMemcpyHostToDevice);
    
    dim3 block(BLOCK_SIZE, BLOCK_SIZE);
    dim3 grid((width  + block.x - 1) / block.x,
              (height + block.y - 1) / block.y);
    //Invocación del Kernel
    frameKernel<<<grid, block>>>(d_height, d_image, width,height,make_float3(lx,ly,lz)); // nota: aca se concatena el vector de luz 
    
    cudaMemcpy(out_gray, d_image, n*sizeof(uchar3),cudaMemcpyDeviceToHost); // aca se jalan los resultados del device al host, esto tambien es costoso

    // aca se libera la memoria reservada
    cudaFree(d_height);
    cudaFree(d_image); 
}

