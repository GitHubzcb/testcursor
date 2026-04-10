#pragma once

#include "temperature_data.h"
#include <string>

class ChartGenerator {
public:
    static std::string generate_line_chart(const TemperatureDataSet& data,
                                           const std::string& output_path);
    static std::string generate_surface_chart(const TemperatureDataSet& data,
                                              const std::string& output_path);
    static std::string generate_combined_chart(const TemperatureDataSet& data,
                                               const std::string& output_path);

private:
    static std::string html_header(const std::string& title);
    static std::string html_footer();
    static std::string to_json_array(const std::vector<std::string>& v);
    static std::string to_json_array(const std::vector<double>& v);
    static std::string to_json_2d_array(const std::vector<std::vector<double>>& v);
    static std::string escape_html(const std::string& s);
};
