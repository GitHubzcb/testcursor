#include "csv_reader.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

std::string CsvReader::trim(const std::string& str) {
    auto start = str.find_first_not_of(" \t\r\n\"");
    auto end = str.find_last_not_of(" \t\r\n\"");
    if (start == std::string::npos) return "";
    return str.substr(start, end - start + 1);
}

std::vector<std::string> CsvReader::split_line(const std::string& line, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(line);
    std::string token;
    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(trim(token));
    }
    return tokens;
}

TemperatureDataSet CsvReader::read_time_series(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filepath);
    }

    TemperatureDataSet dataset;
    dataset.title = "Temperature Data";

    std::string line;

    // First line: header — "Time, Series1, Series2, ..."
    if (!std::getline(file, line)) {
        throw std::runtime_error("Empty CSV file: " + filepath);
    }

    auto headers = split_line(line);
    if (headers.size() < 2) {
        throw std::runtime_error("CSV must have at least 2 columns (time + values)");
    }

    for (size_t i = 1; i < headers.size(); ++i) {
        TemperatureTimeSeries ts;
        ts.name = headers[i];
        dataset.series.push_back(ts);
    }

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        auto fields = split_line(line);
        if (fields.size() < 2) continue;

        std::string timestamp = fields[0];
        for (size_t i = 1; i < fields.size() && (i - 1) < dataset.series.size(); ++i) {
            dataset.series[i - 1].timestamps.push_back(timestamp);
            try {
                dataset.series[i - 1].values.push_back(std::stod(fields[i]));
            } catch (...) {
                dataset.series[i - 1].values.push_back(0.0);
            }
        }
    }

    return dataset;
}

TemperatureDataSet CsvReader::read_surface(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filepath);
    }

    TemperatureDataSet dataset;
    dataset.title = "Temperature Surface Data";
    dataset.has_surface = true;

    std::string line;

    // First line: header — ", Y1, Y2, Y3, ..."  (first cell can be empty or label)
    if (!std::getline(file, line)) {
        throw std::runtime_error("Empty CSV file: " + filepath);
    }

    auto headers = split_line(line);
    for (size_t i = 1; i < headers.size(); ++i) {
        dataset.surface.y_labels.push_back(headers[i]);
    }

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        auto fields = split_line(line);
        if (fields.empty()) continue;

        dataset.surface.x_labels.push_back(fields[0]);
        std::vector<double> row;
        for (size_t i = 1; i < fields.size(); ++i) {
            try {
                row.push_back(std::stod(fields[i]));
            } catch (...) {
                row.push_back(0.0);
            }
        }
        dataset.surface.z_values.push_back(row);
    }

    return dataset;
}
