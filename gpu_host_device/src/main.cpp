#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// esto fue necesario para que el codigo compilara en la jetson
#if __has_include(<filesystem>)
    #include <filesystem>
    namespace fs = std::filesystem;
#elif __has_include(<experimental/filesystem>)
    #include <experimental/filesystem>
    namespace fs = std::experimental::filesystem;
#else
    #error "No se encontró cabecera para filesystem en este compilador."
#endif

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "gpu_render.h" // se incluye el header para poder pasar el kernel de cuda
#include "validation.hpp" // Ejercicio E: volcado de alturas/cuadros para comparar contra la CPU
namespace {

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

// Opciones opcionales de linea de comandos (igual que en la version CPU).
//   --seconds S | --output ruta.mp4 | --dump-dir DIR | --dump-frames a,b,c
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

int index_of(int x, int y, int width) {
    return y * width + x;
}

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

// Si `gray_out` no es nulo, recibe una copia del cuadro en escala de grises (CV_8UC1)
// tomada ANTES de dibujar el texto, para compararla contra la version CPU.
cv::Mat render_frame(const std::vector<float>& height, const Config& cfg, int frame_number,
                     cv::Mat* gray_out = nullptr) {
    cv::Mat image(cfg.height, cfg.width, CV_8UC3);

    const cv::Vec3f light_dir = cv::normalize(cv::Vec3f(-0.35f, -0.55f, 0.76f));
	// llamada a el kernel para renderizar los frames
    gpu_render(
        height.data(),
        image.data,
        cfg.width,
        cfg.height,
        light_dir[0],
        light_dir[1],
        light_dir[2]
    );

    if (gray_out != nullptr) {
        cv::extractChannel(image, *gray_out, 0);
    }

    cv::putText(image,
                "GPU float32 | frame " + std::to_string(frame_number),
                cv::Point(18, 32),
                cv::FONT_HERSHEY_SIMPLEX,
                0.65,
                cv::Scalar(235, 235, 235),
                1,
                cv::LINE_AA);

    return image;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Config cfg;
        parse_args(argc, argv, cfg);
        const int total_frames = static_cast<int>(std::round(cfg.seconds * cfg.fps));
        const std::size_t cells = static_cast<std::size_t>(cfg.width) * static_cast<std::size_t>(cfg.height);

        fs::path output_path(cfg.output);
        if (output_path.has_parent_path()) {
        	fs::create_directories(output_path.parent_path());
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
               gpu_step(previous.data(), current.data(), next.data(), cfg.width, cfg.height, cfg.wave_speed, cfg.damping, cfg.edge_damping);//llamada al kernel de step
                previous.swap(current);
                current.swap(next);
            }

            const bool dump_this = !cfg.dump_dir.empty() &&
                                   std::binary_search(cfg.dump_frames.begin(), cfg.dump_frames.end(), frame);
            cv::Mat gray;
            writer.write(render_frame(current, cfg, frame, dump_this ? &gray : nullptr));

            if (dump_this) {
                gpu_copy_current_to_host(current.data(), cfg.width, cfg.height);
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
