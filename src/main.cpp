#include "csv_reader.h"
#include "chart_generator.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

void print_usage(const char* prog) {
    std::cout << "Temperature Chart Generator\n"
              << "Usage:\n"
              << "  " << prog << " line    <csv_file> [output.html]   — 2D curve chart\n"
              << "  " << prog << " surface <csv_file> [output.html]   — 3D surface chart\n"
              << "  " << prog << " demo                               — generate demo charts\n"
              << "\nCSV format for 'line' mode:\n"
              << "  Time, Series1, Series2, ...\n"
              << "  2024-01-01, 15.3, 20.1, ...\n"
              << "\nCSV format for 'surface' mode:\n"
              << "  Label/Y, Y1, Y2, Y3, ...\n"
              << "  X1, z11, z12, z13, ...\n"
              << std::endl;
}

TemperatureDataSet generate_math_surface() {
    TemperatureDataSet dataset;
    dataset.title = "Mathematical Temperature Surface";
    dataset.has_surface = true;

    const int nx = 40, ny = 40;
    const double x_min = -3.0, x_max = 3.0;
    const double y_min = -3.0, y_max = 3.0;

    for (int i = 0; i < nx; ++i) {
        double x = x_min + (x_max - x_min) * i / (nx - 1);
        dataset.surface.x_labels.push_back(std::to_string(x).substr(0, 5));
    }
    for (int j = 0; j < ny; ++j) {
        double y = y_min + (y_max - y_min) * j / (ny - 1);
        dataset.surface.y_labels.push_back(std::to_string(y).substr(0, 5));
    }

    for (int i = 0; i < nx; ++i) {
        double x = x_min + (x_max - x_min) * i / (nx - 1);
        std::vector<double> row;
        for (int j = 0; j < ny; ++j) {
            double y = y_min + (y_max - y_min) * j / (ny - 1);
            double r = std::sqrt(x * x + y * y);
            double z = 20.0 + 15.0 * std::sin(r) / (r + 0.001);
            row.push_back(std::round(z * 100) / 100);
        }
        dataset.surface.z_values.push_back(row);
    }

    return dataset;
}

void run_demo(const std::string& output_dir) {
    fs::create_directories(output_dir);

    std::string data_dir = "data";

    // 1. Time series chart from sample data
    try {
        auto ts_data = CsvReader::read_time_series(data_dir + "/temperature_timeseries.csv");
        ts_data.title = "China Major Cities Temperature (2024)";
        auto path = ChartGenerator::generate_line_chart(ts_data, output_dir + "/demo_curves.html");
        std::cout << "[OK] Line chart   -> " << path << "\n";
    } catch (const std::exception& e) {
        std::cerr << "[ERR] Line chart: " << e.what() << "\n";
    }

    // 2. Surface chart from sample data
    try {
        auto sf_data = CsvReader::read_surface(data_dir + "/temperature_surface.csv");
        sf_data.title = "City Daily Temperature Distribution";
        auto path = ChartGenerator::generate_surface_chart(sf_data, output_dir + "/demo_surface.html");
        std::cout << "[OK] Surface chart -> " << path << "\n";
    } catch (const std::exception& e) {
        std::cerr << "[ERR] Surface chart: " << e.what() << "\n";
    }

    // 3. Mathematical surface
    try {
        auto math_data = generate_math_surface();
        auto path = ChartGenerator::generate_surface_chart(math_data, output_dir + "/demo_math_surface.html");
        std::cout << "[OK] Math surface  -> " << path << "\n";
    } catch (const std::exception& e) {
        std::cerr << "[ERR] Math surface: " << e.what() << "\n";
    }

    // 4. Combined chart
    try {
        auto ts_data = CsvReader::read_time_series(data_dir + "/temperature_timeseries.csv");
        auto sf_data = CsvReader::read_surface(data_dir + "/temperature_surface.csv");
        ts_data.title = "Temperature Comprehensive Dashboard";
        ts_data.has_surface = true;
        ts_data.surface = sf_data.surface;
        auto path = ChartGenerator::generate_combined_chart(ts_data, output_dir + "/demo_combined.html");
        std::cout << "[OK] Combined      -> " << path << "\n";
    } catch (const std::exception& e) {
        std::cerr << "[ERR] Combined chart: " << e.what() << "\n";
    }

    std::cout << "\nAll demo charts generated in: " << output_dir << "/\n";
    std::cout << "Open the HTML files in a browser to view interactive charts.\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string mode = argv[1];

    if (mode == "demo") {
        std::string output_dir = (argc >= 3) ? argv[2] : "output";
        run_demo(output_dir);
        return 0;
    }

    if (mode == "line") {
        if (argc < 3) {
            std::cerr << "Error: CSV file path required.\n";
            return 1;
        }
        std::string csv_path = argv[2];
        std::string output = (argc >= 4) ? argv[3] : "temperature_curves.html";
        try {
            auto data = CsvReader::read_time_series(csv_path);
            auto path = ChartGenerator::generate_line_chart(data, output);
            std::cout << "Chart generated: " << path << "\n";
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }

    if (mode == "surface") {
        if (argc < 3) {
            std::cerr << "Error: CSV file path required.\n";
            return 1;
        }
        std::string csv_path = argv[2];
        std::string output = (argc >= 4) ? argv[3] : "temperature_surface.html";
        try {
            auto data = CsvReader::read_surface(csv_path);
            auto path = ChartGenerator::generate_surface_chart(data, output);
            std::cout << "Chart generated: " << path << "\n";
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }

    std::cerr << "Unknown mode: " << mode << "\n";
    print_usage(argv[0]);
    return 1;
}
