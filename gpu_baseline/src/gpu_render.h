// header para incluir el programa de cuda en el programa principal
//nota: esto se hace para que el compilador solo proceses el codigo una vez:
#ifndef GPU_RENDER_H
#define GPU_RENDER_H

// se usa extern C para decirle al compilador que no es un arhco de C y asi evitar problemas entre C y CUDA
extern "C" {
	void gpu_render(const float*height_host, unsigned char* out_gray, int width, int height, float lx, float ly, float lz);
}


#endif
