// compare_dumps: compara las salidas numericas de la version de referencia (CPU)
// contra otra version (GPU u optimizacion) para el Ejercicio E.
//
// Uso:
//   compare_dumps <dir_referencia> <dir_prueba> [--tol-rel X] [--tol-pixel N] [--csv archivo.csv]
//
// Compara, para cada cuadro que exista en <dir_referencia>:
//   * height_fNNNNNN.bin : malla de alturas float32 -> MAE, RMSE, error maximo (y donde ocurre)
//   * frame_fNNNNNN.bin  : cuadro en grises uint8   -> MAE, diferencia maxima, % de pixeles distintos, PSNR
//
// Codigo de salida: 0 = todo PASS, 1 = algun FAIL, 2 = error de uso/lectura.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "validation.hpp"

namespace fs = std::filesystem;
using validation::Dump;
using validation::Kind;

namespace {

struct Options {
    std::string ref_dir;
    std::string test_dir;
    double tol_rel = 1e-4;     // max|err| / max|ref| permitido en la malla (float32 vs float32)
    int tol_pixel = 2;         // diferencia maxima permitida en niveles de gris (0..255)
    std::string csv;
};

struct Row {
    std::string kind;
    int frame = 0;
    double mae = 0.0;
    double rmse = 0.0;
    double max_abs = 0.0;
    int max_x = -1;
    int max_y = -1;
    double ref_scale = std::nan("");   // alturas: max |ref|
    double pct_diff = std::nan("");    // cuadros: % pixeles con diferencia != 0
    double pct_gt1 = std::nan("");     // cuadros: % pixeles con diferencia > 1
    double psnr = std::nan("");        // cuadros: PSNR en dB (inf si son identicos)
    std::string status;
};

void usage() {
    std::cerr << "Uso: compare_dumps <dir_referencia> <dir_prueba> "
                 "[--tol-rel X] [--tol-pixel N] [--csv archivo.csv]\n";
}

Row compare_heights(const Dump& ref, const Dump& test, const Options& opt) {
    Row r;
    r.kind = "height";
    r.frame = ref.frame;

    const std::size_t n = ref.heights.size();
    double sum_abs = 0.0;
    double sum_sq = 0.0;
    double ref_scale = 0.0;
    std::size_t max_idx = 0;
    std::size_t non_finite = 0;

    for (std::size_t i = 0; i < n; ++i) {
        const double a = ref.heights[i];
        const double b = test.heights[i];
        if (!std::isfinite(b)) {
            ++non_finite;
            continue;
        }
        const double e = std::fabs(a - b);
        sum_abs += e;
        sum_sq += e * e;
        ref_scale = std::max(ref_scale, std::fabs(a));
        if (e > r.max_abs) {
            r.max_abs = e;
            max_idx = i;
        }
    }

    r.mae = sum_abs / static_cast<double>(n);
    r.rmse = std::sqrt(sum_sq / static_cast<double>(n));
    r.max_x = static_cast<int>(max_idx % static_cast<std::size_t>(ref.width));
    r.max_y = static_cast<int>(max_idx / static_cast<std::size_t>(ref.width));
    r.ref_scale = ref_scale;
    // El error se normaliza con el pico de la referencia: la amplitud de la onda varia
    // mucho (decae con el tiempo), asi que un umbral absoluto no seria justo.
    const double rel = ref_scale > 0.0 ? r.max_abs / ref_scale : r.max_abs;
    r.status = (non_finite == 0 && rel <= opt.tol_rel) ? "PASS" : "FAIL";
    if (non_finite > 0) {
        r.status = "FAIL(NaN/Inf:" + std::to_string(non_finite) + ")";
    }
    return r;
}

Row compare_frames(const Dump& ref, const Dump& test, const Options& opt) {
    Row r;
    r.kind = "frame";
    r.frame = ref.frame;

    const std::size_t n = ref.pixels.size();
    double sum_abs = 0.0;
    double sum_sq = 0.0;
    std::size_t differ = 0;
    std::size_t gt1 = 0;
    int max_diff = 0;
    std::size_t max_idx = 0;

    for (std::size_t i = 0; i < n; ++i) {
        const int d = std::abs(static_cast<int>(ref.pixels[i]) - static_cast<int>(test.pixels[i]));
        sum_abs += d;
        sum_sq += static_cast<double>(d) * d;
        differ += d != 0;
        gt1 += d > 1;
        if (d > max_diff) {
            max_diff = d;
            max_idx = i;
        }
    }

    const double mse = sum_sq / static_cast<double>(n);
    r.mae = sum_abs / static_cast<double>(n);
    r.rmse = std::sqrt(mse);
    r.max_abs = max_diff;
    r.max_x = static_cast<int>(max_idx % static_cast<std::size_t>(ref.width));
    r.max_y = static_cast<int>(max_idx / static_cast<std::size_t>(ref.width));
    r.pct_diff = 100.0 * static_cast<double>(differ) / static_cast<double>(n);
    r.pct_gt1 = 100.0 * static_cast<double>(gt1) / static_cast<double>(n);
    r.psnr = mse == 0.0 ? std::numeric_limits<double>::infinity() : 10.0 * std::log10(255.0 * 255.0 / mse);
    r.status = max_diff <= opt.tol_pixel ? "PASS" : "FAIL";
    return r;
}

std::string num(double v, const char* fmt) {
    if (std::isnan(v)) {
        return "";
    }
    if (std::isinf(v)) {
        return "inf";
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, fmt, v);
    return buf;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Falta el valor de " << name << '\n';
                usage();
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--tol-rel") {
            opt.tol_rel = std::stod(next("--tol-rel"));
        } else if (arg == "--tol-pixel") {
            opt.tol_pixel = std::stoi(next("--tol-pixel"));
        } else if (arg == "--csv") {
            opt.csv = next("--csv");
        } else if (arg == "-h" || arg == "--help") {
            usage();
            return 0;
        } else {
            positional.push_back(arg);
        }
    }
    if (positional.size() != 2) {
        usage();
        return 2;
    }
    opt.ref_dir = positional[0];
    opt.test_dir = positional[1];

    try {
        std::vector<fs::path> ref_files;
        for (const auto& entry : fs::directory_iterator(opt.ref_dir)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("height_f", 0) == 0 || name.rfind("frame_f", 0) == 0) {
                ref_files.push_back(entry.path());
            }
        }
        std::sort(ref_files.begin(), ref_files.end());
        if (ref_files.empty()) {
            std::cerr << "No hay archivos height_f*.bin / frame_f*.bin en " << opt.ref_dir << '\n';
            return 2;
        }

        std::vector<Row> rows;
        bool all_pass = true;

        for (const auto& ref_path : ref_files) {
            const fs::path test_path = fs::path(opt.test_dir) / ref_path.filename();
            const Dump ref = validation::read_dump(ref_path);

            Row row;
            if (!fs::exists(test_path)) {
                row.kind = ref.kind == Kind::Height ? "height" : "frame";
                row.frame = ref.frame;
                row.status = "FAIL(falta archivo)";
            } else {
                const Dump test = validation::read_dump(test_path);
                if (test.kind != ref.kind || test.width != ref.width || test.height != ref.height) {
                    row.kind = ref.kind == Kind::Height ? "height" : "frame";
                    row.frame = ref.frame;
                    row.status = "FAIL(dimensiones/tipo distintos)";
                } else {
                    row = ref.kind == Kind::Height ? compare_heights(ref, test, opt)
                                                   : compare_frames(ref, test, opt);
                }
            }
            all_pass = all_pass && row.status == "PASS";
            rows.push_back(row);
        }

        std::printf("Referencia: %s\nPrueba:     %s\n", opt.ref_dir.c_str(), opt.test_dir.c_str());
        std::printf("Tolerancias: alturas max|err|/max|ref| <= %.3g, cuadros diff <= %d niveles de gris\n\n",
                    opt.tol_rel, opt.tol_pixel);

        std::printf("MALLA DE ALTURAS (float32)\n");
        std::printf("%-7s %-12s %-12s %-12s %-12s %-10s %-12s %s\n",
                    "frame", "MAE", "RMSE", "max|err|", "max|ref|", "(x,y)", "err_rel_max", "estado");
        for (const auto& r : rows) {
            if (r.kind != "height") continue;
            const double rel = (std::isnan(r.ref_scale) || r.ref_scale == 0.0) ? std::nan("") : r.max_abs / r.ref_scale;
            char pos[32];
            std::snprintf(pos, sizeof pos, "(%d,%d)", r.max_x, r.max_y);
            std::printf("%-7d %-12.3e %-12.3e %-12.3e %-12.3e %-10s %-12.3e %s\n",
                        r.frame, r.mae, r.rmse, r.max_abs, std::isnan(r.ref_scale) ? 0.0 : r.ref_scale,
                        pos, std::isnan(rel) ? 0.0 : rel, r.status.c_str());
        }

        std::printf("\nCUADROS (escala de grises, 0-255)\n");
        std::printf("%-7s %-10s %-9s %-10s %-10s %-10s %s\n",
                    "frame", "MAE", "max diff", "% != 0", "% > 1", "PSNR(dB)", "estado");
        for (const auto& r : rows) {
            if (r.kind != "frame") continue;
            std::printf("%-7d %-10.4f %-9.0f %-10.4f %-10.4f %-10s %s\n",
                        r.frame, r.mae, r.max_abs, std::isnan(r.pct_diff) ? 0.0 : r.pct_diff,
                        std::isnan(r.pct_gt1) ? 0.0 : r.pct_gt1, num(r.psnr, "%.2f").c_str(), r.status.c_str());
        }

        std::printf("\nResultado global: %s\n", all_pass ? "PASS" : "FAIL");

        if (!opt.csv.empty()) {
            std::ofstream csv(opt.csv);
            if (!csv) {
                throw std::runtime_error("No se pudo escribir " + opt.csv);
            }
            csv << "kind,frame,mae,rmse,max_abs,max_x,max_y,ref_scale,pct_diff,pct_gt1,psnr_db,status\n";
            for (const auto& r : rows) {
                csv << r.kind << ',' << r.frame << ',' << num(r.mae, "%.6e") << ',' << num(r.rmse, "%.6e") << ','
                    << num(r.max_abs, "%.6e") << ',' << r.max_x << ',' << r.max_y << ','
                    << num(r.ref_scale, "%.6e") << ',' << num(r.pct_diff, "%.6f") << ','
                    << num(r.pct_gt1, "%.6f") << ',' << num(r.psnr, "%.4f") << ',' << r.status << '\n';
            }
            std::printf("CSV escrito en %s\n", opt.csv.c_str());
        }

        return all_pass ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 2;
    }
}