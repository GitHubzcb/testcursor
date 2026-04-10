#pragma once

#include <string>
#include <vector>

struct TemperatureRecord {
    std::string label;
    double value;
};

struct TemperatureTimeSeries {
    std::string name;
    std::vector<std::string> timestamps;
    std::vector<double> values;
};

struct TemperatureSurface {
    std::vector<std::string> x_labels;
    std::vector<std::string> y_labels;
    std::vector<std::vector<double>> z_values;
};

struct TemperatureDataSet {
    std::string title;
    std::vector<TemperatureTimeSeries> series;
    TemperatureSurface surface;
    bool has_surface = false;
};
