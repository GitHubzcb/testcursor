#pragma once

#include "temperature_data.h"
#include <string>

class CsvReader {
public:
    static TemperatureDataSet read_time_series(const std::string& filepath);
    static TemperatureDataSet read_surface(const std::string& filepath);

private:
    static std::vector<std::string> split_line(const std::string& line, char delimiter = ',');
    static std::string trim(const std::string& str);
};
