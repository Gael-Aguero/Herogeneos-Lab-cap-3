#pragma once

// Utilidades para el Ejercicio E (validacion CPU vs GPU).
//
// Header-only a proposito: la version CPU (main.cpp) y la version CUDA escriben
// EXACTAMENTE el mismo formato, y compare_dumps.cpp lo lee. Asi la comparacion
// no depende de OpenCV ni del codec del video.
//
// Formato de cada archivo .bin (little-endian, x86 y ARM/Jetson lo son):
//   bytes  0..3   magic "DSV1"
//   bytes  4..7   uint32 kind   (1 = malla de alturas float32, 2 = cuadro gris uint8)
//   bytes  8..11  int32  width
//   bytes 12..15  int32  height
//   bytes 16..19  int32  frame  (indice de cuadro, el mismo `frame` del ciclo principal)
//   resto         width*height elementos (float o uint8), fila por fila (idx = y*width + x)
//
// Nombres: height_f000100.bin  y  frame_f000100.bin

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// POSIX en lugar de std::filesystem: el GCC de la Jetson Nano (7.x) no trae un
// <filesystem> completo y ademas exigiria enlazar con -lstdc++fs.
#include <cerrno>
#include <sys/stat.h>
#include <sys/types.h>

namespace validation {

enum class Kind : std::uint32_t { Height = 1, Frame = 2 };

struct Dump {
    Kind kind = Kind::Height;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::int32_t frame = 0;
    std::vector<float> heights;         // valido si kind == Height
    std::vector<unsigned char> pixels;  // valido si kind == Frame
};

inline std::string file_name(Kind kind, int frame) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s_f%06d.bin", kind == Kind::Height ? "height" : "frame", frame);
    return buf;
}

// Equivalente a `mkdir -p`: crea la carpeta y sus padres si no existen.
inline void make_dirs(const std::string& path) {
    for (std::size_t i = 1; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            const std::string part = path.substr(0, i);
            if (::mkdir(part.c_str(), 0777) != 0 && errno != EEXIST) {
                throw std::runtime_error("No se pudo crear la carpeta: " + part);
            }
        }
    }
}

namespace detail {

inline std::ofstream open_out(const std::string& dir, Kind kind, int width, int height, int frame) {
    make_dirs(dir);
    const std::string path = dir + "/" + file_name(kind, frame);
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) {
        throw std::runtime_error("No se pudo escribir: " + path);
    }
    const char magic[4] = {'D', 'S', 'V', '1'};
    const std::uint32_t k = static_cast<std::uint32_t>(kind);
    const std::int32_t dims[3] = {width, height, frame};
    out.write(magic, 4);
    out.write(reinterpret_cast<const char*>(&k), sizeof k);
    out.write(reinterpret_cast<const char*>(dims), sizeof dims);
    return out;
}

}  // namespace detail

// Guarda la malla de alturas (float32, en el host). Si la version GPU guarda en
// half u otro formato, conviertala a float ANTES de llamar a esta funcion.
inline void write_heights(const std::string& dir, int frame, const float* data, int width, int height) {
    auto out = detail::open_out(dir, Kind::Height, width, height, frame);
    out.write(reinterpret_cast<const char*>(data),
              static_cast<std::streamsize>(sizeof(float)) * width * height);
    if (!out) {
        throw std::runtime_error("Fallo al escribir la malla de alturas");
    }
}

// Guarda el cuadro en escala de grises (1 byte por pixel) SIN el texto superpuesto.
inline void write_frame(const std::string& dir, int frame, const unsigned char* gray, int width, int height) {
    auto out = detail::open_out(dir, Kind::Frame, width, height, frame);
    out.write(reinterpret_cast<const char*>(gray), static_cast<std::streamsize>(width) * height);
    if (!out) {
        throw std::runtime_error("Fallo al escribir el cuadro");
    }
}

inline Dump read_dump(const std::string& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        throw std::runtime_error("No se pudo abrir: " + path);
    }
    char magic[4];
    std::uint32_t kind = 0;
    std::int32_t dims[3] = {0, 0, 0};
    in.read(magic, 4);
    in.read(reinterpret_cast<char*>(&kind), sizeof kind);
    in.read(reinterpret_cast<char*>(dims), sizeof dims);
    if (!in || magic[0] != 'D' || magic[1] != 'S' || magic[2] != 'V' || magic[3] != '1' ||
        (kind != 1 && kind != 2) || dims[0] <= 0 || dims[1] <= 0) {
        throw std::runtime_error("Formato invalido: " + path);
    }

    Dump d;
    d.kind = static_cast<Kind>(kind);
    d.width = dims[0];
    d.height = dims[1];
    d.frame = dims[2];
    const std::size_t n = static_cast<std::size_t>(d.width) * static_cast<std::size_t>(d.height);

    if (d.kind == Kind::Height) {
        d.heights.resize(n);
        in.read(reinterpret_cast<char*>(d.heights.data()), static_cast<std::streamsize>(n * sizeof(float)));
    } else {
        d.pixels.resize(n);
        in.read(reinterpret_cast<char*>(d.pixels.data()), static_cast<std::streamsize>(n));
    }
    if (!in) {
        throw std::runtime_error("Archivo truncado: " + path);
    }
    return d;
}

// "0,1,10,100" -> {0,1,10,100} (ordenado y sin repetidos)
inline std::vector<int> parse_frame_list(const std::string& text) {
    std::vector<int> frames;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
            frames.push_back(std::stoi(item));
        }
    }
    std::sort(frames.begin(), frames.end());
    frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
    return frames;
}

}  // namespace validation