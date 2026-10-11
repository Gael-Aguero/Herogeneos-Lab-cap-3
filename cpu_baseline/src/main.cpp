#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "validation.hpp"

namespace {
/* Agrupa los parametros de tres categorias: 
Formato de video: width, height, seconds, fps, output
Física: wave_speed, damping, edge_damping
Gota: drop_radius, drop_strength

Uso: se crea el objeto Config en main, y luego se pasa como argumento a las funciones add_drop, border_absorption, simulate_step y render_frame.
*/ 
struct Config {
    int width = 640;
    int height = 640;
    double seconds = 30.0;
    int fps = 30;
    int steps_per_frame = 1;
    float wave_speed = 0.45f;
    float damping = 0.006f;
    float edge_damping = 0.035f;
    float drop_radius = 18.0f;
    float drop_strength = 1.0f;
    std::string output = "output/drop_simulation.mp4";

    // --- Validacion (Ejercicio E). Si dump_dir esta vacio no se guarda nada. ---
    std::string dump_dir;
    std::vector<int> dump_frames = {0, 1, 10, 100, 300, 600, 899};
};

// Lee opciones opcionales de linea de comandos. Sin argumentos el programa se
// comporta exactamente igual que antes.
//   --seconds S          duracion de la simulacion (para pruebas rapidas)
//   --output ruta.mp4    video de salida
//   --dump-dir DIR       guarda malla de alturas y cuadro gris en DIR (ver validation.hpp)
//   --dump-frames a,b,c  indices de cuadro a guardar
void parse_args(int argc, char** argv, Config& cfg) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error("Falta el valor de " + arg);
            }
            return argv[++i];
        };
        if (arg == "--seconds") {
            cfg.seconds = std::stod(value());
        } else if (arg == "--output") {
            cfg.output = value();
        } else if (arg == "--dump-dir") {
            cfg.dump_dir = value();
        } else if (arg == "--dump-frames") {
            cfg.dump_frames = validation::parse_frame_list(value());
        } else {
            throw std::runtime_error("Argumento desconocido: " + arg);
        }
    }
}

// Convierte las coordenadas x e y en la posicion correspondiente dentro del vector para ubicar la celda en el vector
int index_of(int x, int y, int width) {
    return y * width + x;
}

// Agrega una gota en el centro y genera un pulso para iniciar las ondas de la simulacion.
void add_drop(std::vector<float>& current, std::vector<float>& previous, const Config& cfg) {
    const float cx = 0.5f * static_cast<float>(cfg.width - 1);
    const float cy = 0.5f * static_cast<float>(cfg.height - 1);
    const float sigma2 = cfg.drop_radius * cfg.drop_radius;

    for (int y = 1; y < cfg.height - 1; ++y) {
        for (int x = 1; x < cfg.width - 1; ++x) {
            const float dx = static_cast<float>(x) - cx;
            const float dy = static_cast<float>(y) - cy;
            const float r2 = dx * dx + dy * dy;
            const float pulse = cfg.drop_strength * std::exp(-r2 / (2.0f * sigma2));
            const int idx = index_of(x, y, cfg.width);
            current[idx] += pulse;
            previous[idx] -= 0.35f * pulse;
        }
    }
}

// Calcula cuanto frenar la onda, según la distancia de los bordes
float border_absorption(int x, int y, const Config& cfg) {
    constexpr int band = 32;
    const int dist = std::min({x, y, cfg.width - 1 - x, cfg.height - 1 - y});
    if (dist >= band) {
        return cfg.damping;
    }

    const float t = 1.0f - static_cast<float>(dist) / static_cast<float>(band);
    return cfg.damping + cfg.edge_damping * t * t;
}

/* Calcula el siguiente estado de la onda comparando cada celda con sus 4 vecinas
y ajustando su altura, mediante la ecuación de onda bidimensional amortiguada: next = 2*current - previous + c²*laplaciano - damping*(current - previous)
donde laplaciano = izq + der + arriba + abajo - 4*centro.
*/
void simulate_step(const std::vector<float>& previous,
                   const std::vector<float>& current,
                   std::vector<float>& next,
                   const Config& cfg) {
    const float c2 = cfg.wave_speed * cfg.wave_speed;

    std::fill(next.begin(), next.end(), 0.0f);
// for anidados para recorrer todas las celdas de la simulacion, excepto los bordes, y calcular la altura de la onda en cada celda.
    for (int y = 1; y < cfg.height - 1; ++y) {
        for (int x = 1; x < cfg.width - 1; ++x) {
            const int idx = index_of(x, y, cfg.width);
            const float laplacian =
                current[idx - 1] + current[idx + 1] +
                current[idx - cfg.width] + current[idx + cfg.width] -
                4.0f * current[idx];
            const float velocity = current[idx] - previous[idx];
            const float local_damping = border_absorption(x, y, cfg);

            next[idx] = 2.0f * current[idx] - previous[idx] +
                        c2 * laplacian -
                        local_damping * velocity;
        }
    }
}

// Crea una imagen en escala de grises a partir de la altura de la onda. 
// Si `gray_out` no es nulo, recibe una copia del cuadro en escala de grises (CV_8UC1)
// tomada ANTES de dibujar el texto, para poder compararla contra la version GPU.
cv::Mat render_frame(const std::vector<float>& height, const Config& cfg, int frame_number,
                     cv::Mat* gray_out = nullptr) {
    cv::Mat image(cfg.height, cfg.width, CV_8UC3);

    const cv::Vec3f light_dir = cv::normalize(cv::Vec3f(-0.35f, -0.55f, 0.76f));

    for (int y = 0; y < cfg.height; ++y) {
        for (int x = 0; x < cfg.width; ++x) {
            const int xm = std::max(0, x - 1);
            const int xp = std::min(cfg.width - 1, x + 1);
            const int ym = std::max(0, y - 1);
            const int yp = std::min(cfg.height - 1, y + 1);

            const float dx = height[index_of(xm, y, cfg.width)] - height[index_of(xp, y, cfg.width)];
            const float dy = height[index_of(x, ym, cfg.width)] - height[index_of(x, yp, cfg.width)];
            const cv::Vec3f normal = cv::normalize(cv::Vec3f(2.8f * dx, 2.8f * dy, 1.0f));

            const float diffuse = std::max(0.0f, normal.dot(light_dir));
            const float wave = std::clamp(0.5f + 1.8f * height[index_of(x, y, cfg.width)], 0.0f, 1.0f);
            const float specular = std::pow(std::max(0.0f, diffuse), 24.0f);

            float intensity = 35.0f + 120.0f * wave;
            intensity *= 0.60f + 0.65f * diffuse;
            intensity += 130.0f * specular;

            const auto gray = static_cast<unsigned char>(std::clamp(intensity, 0.0f, 255.0f));
            image.at<cv::Vec3b>(y, x) = cv::Vec3b(gray, gray, gray);
        }
    }

    if (gray_out != nullptr) {
        cv::extractChannel(image, *gray_out, 0);
    }

    cv::putText(image,
                "CPU float32 | frame " + std::to_string(frame_number),
                cv::Point(18, 32),
                cv::FONT_HERSHEY_SIMPLEX,
                0.65,
                cv::Scalar(235, 235, 235),
                1,
                cv::LINE_AA);

    return image;
}

}  // namespace

// Prepara la simulacion, genera sus frames y los guarda en un video, lo hace cfg.seconds * cfg.fps es decir 900 veces.
int main(int argc, char** argv) {
    try {
        Config cfg;
        parse_args(argc, argv, cfg);
        const int total_frames = static_cast<int>(std::round(cfg.seconds * cfg.fps));
        const std::size_t cells = static_cast<std::size_t>(cfg.width) * static_cast<std::size_t>(cfg.height);

        std::filesystem::path output_path(cfg.output);
        if (output_path.has_parent_path()) {
            std::filesystem::create_directories(output_path.parent_path());
        }

        std::vector<float> previous(cells, 0.0f);
        std::vector<float> current(cells, 0.0f);
        std::vector<float> next(cells, 0.0f);

        add_drop(current, previous, cfg);

        cv::VideoWriter writer(
            cfg.output,
            cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
            static_cast<double>(cfg.fps),
            cv::Size(cfg.width, cfg.height));

        if (!writer.isOpened()) {
            throw std::runtime_error("No se pudo abrir el archivo de salida: " + cfg.output);
        }

        const auto start = std::chrono::steady_clock::now();

        for (int frame = 0; frame < total_frames; ++frame) {
            for (int step = 0; step < cfg.steps_per_frame; ++step) {
                simulate_step(previous, current, next, cfg);
                previous.swap(current);
                current.swap(next);
            }

            const bool dump_this = !cfg.dump_dir.empty() &&
                                   std::binary_search(cfg.dump_frames.begin(), cfg.dump_frames.end(), frame);
            cv::Mat gray;
            writer.write(render_frame(current, cfg, frame, dump_this ? &gray : nullptr));

            if (dump_this) {
                validation::write_heights(cfg.dump_dir, frame, current.data(), cfg.width, cfg.height);
                validation::write_frame(cfg.dump_dir, frame, gray.data, cfg.width, cfg.height);
            }

            if (frame % std::max(1, total_frames / 10) == 0) {
                std::cout << "Frame " << frame << " / " << total_frames << '\n';
            }
        }

        const auto end = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(end - start).count();
        const double simulated_steps = static_cast<double>(total_frames) * cfg.steps_per_frame;

        std::cout << "Video generado: " << cfg.output << '\n';
        std::cout << "Tiempo: " << elapsed << " s\n";
        std::cout << "Pasos simulados: " << simulated_steps << '\n';
        std::cout << "Rendimiento: " << simulated_steps / elapsed << " pasos/s\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}