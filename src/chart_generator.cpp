#include "chart_generator.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

std::string ChartGenerator::escape_html(const std::string& s) {
    std::string result;
    for (char c : s) {
        switch (c) {
            case '&':  result += "&amp;";  break;
            case '<':  result += "&lt;";   break;
            case '>':  result += "&gt;";   break;
            case '"':  result += "&quot;"; break;
            case '\'': result += "&#39;";  break;
            default:   result += c;        break;
        }
    }
    return result;
}

std::string ChartGenerator::to_json_array(const std::vector<std::string>& v) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << v[i] << "\"";
    }
    oss << "]";
    return oss.str();
}

std::string ChartGenerator::to_json_array(const std::vector<double>& v) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i > 0) oss << ",";
        oss << v[i];
    }
    oss << "]";
    return oss.str();
}

std::string ChartGenerator::to_json_2d_array(const std::vector<std::vector<double>>& v) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i > 0) oss << ",";
        oss << to_json_array(v[i]);
    }
    oss << "]";
    return oss.str();
}

std::string ChartGenerator::html_header(const std::string& title) {
    std::ostringstream oss;
    oss << R"(<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>)" << escape_html(title) << R"(</title>
    <script src="https://cdn.plot.ly/plotly-2.32.0.min.js"></script>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto,
                         'Helvetica Neue', Arial, sans-serif;
            background: linear-gradient(135deg, #0f0c29, #302b63, #24243e);
            min-height: 100vh;
            color: #e0e0e0;
        }
        .container {
            max-width: 1400px;
            margin: 0 auto;
            padding: 30px 20px;
        }
        h1 {
            text-align: center;
            font-size: 2.2em;
            margin-bottom: 10px;
            background: linear-gradient(90deg, #ff6b6b, #feca57, #48dbfb, #ff9ff3);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            background-clip: text;
        }
        .subtitle {
            text-align: center;
            color: #888;
            margin-bottom: 30px;
            font-size: 1.1em;
        }
        .chart-card {
            background: rgba(255, 255, 255, 0.05);
            border-radius: 16px;
            padding: 24px;
            margin-bottom: 30px;
            backdrop-filter: blur(10px);
            border: 1px solid rgba(255, 255, 255, 0.1);
            box-shadow: 0 8px 32px rgba(0, 0, 0, 0.3);
        }
        .chart-card h2 {
            font-size: 1.4em;
            margin-bottom: 16px;
            color: #48dbfb;
        }
        .chart-container {
            width: 100%;
            min-height: 500px;
        }
        .stats-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 16px;
            margin-bottom: 30px;
        }
        .stat-card {
            background: rgba(255, 255, 255, 0.08);
            border-radius: 12px;
            padding: 20px;
            text-align: center;
            border: 1px solid rgba(255, 255, 255, 0.05);
        }
        .stat-value {
            font-size: 2em;
            font-weight: 700;
            color: #feca57;
        }
        .stat-label {
            font-size: 0.9em;
            color: #aaa;
            margin-top: 4px;
        }
        .footer {
            text-align: center;
            color: #555;
            margin-top: 30px;
            font-size: 0.85em;
        }
    </style>
</head>
<body>
<div class="container">
)";
    return oss.str();
}

std::string ChartGenerator::html_footer() {
    return R"(
    <div class="footer">Temperature Chart Generator &mdash; C++ &amp; Plotly.js</div>
</div>
</body>
</html>
)";
}

std::string ChartGenerator::generate_line_chart(const TemperatureDataSet& data,
                                                const std::string& output_path) {
    std::ofstream file(output_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot write to: " + output_path);
    }

    file << html_header(data.title);

    file << "    <h1>" << escape_html(data.title) << "</h1>\n";
    file << "    <p class=\"subtitle\">Temperature Time Series Visualization</p>\n\n";

    // Stats
    if (!data.series.empty()) {
        file << "    <div class=\"stats-grid\">\n";
        for (const auto& ts : data.series) {
            if (ts.values.empty()) continue;
            double min_v = ts.values[0], max_v = ts.values[0], sum = 0;
            for (double v : ts.values) {
                if (v < min_v) min_v = v;
                if (v > max_v) max_v = v;
                sum += v;
            }
            double avg = sum / static_cast<double>(ts.values.size());

            file << "    <div class=\"stat-card\">"
                 << "<div class=\"stat-value\">" << avg << "&deg;</div>"
                 << "<div class=\"stat-label\">" << escape_html(ts.name) << " Avg</div></div>\n";
            file << "    <div class=\"stat-card\">"
                 << "<div class=\"stat-value\">" << max_v << "&deg;</div>"
                 << "<div class=\"stat-label\">" << escape_html(ts.name) << " Max</div></div>\n";
            file << "    <div class=\"stat-card\">"
                 << "<div class=\"stat-value\">" << min_v << "&deg;</div>"
                 << "<div class=\"stat-label\">" << escape_html(ts.name) << " Min</div></div>\n";
        }
        file << "    </div>\n\n";
    }

    // Line chart
    file << "    <div class=\"chart-card\">\n";
    file << "        <h2>Temperature Curve</h2>\n";
    file << "        <div id=\"lineChart\" class=\"chart-container\"></div>\n";
    file << "    </div>\n\n";

    // Area chart
    file << "    <div class=\"chart-card\">\n";
    file << "        <h2>Temperature Area Chart</h2>\n";
    file << "        <div id=\"areaChart\" class=\"chart-container\"></div>\n";
    file << "    </div>\n\n";

    // Bar chart
    file << "    <div class=\"chart-card\">\n";
    file << "        <h2>Temperature Bar Chart</h2>\n";
    file << "        <div id=\"barChart\" class=\"chart-container\"></div>\n";
    file << "    </div>\n\n";

    file << "<script>\n";

    // Color palette
    file << "var colors = ['#ff6b6b','#48dbfb','#feca57','#ff9ff3','#54a0ff','#5f27cd',\n"
         << "              '#01a3a4','#f368e0','#ee5a24','#009432'];\n\n";

    // Plotly dark layout
    file << R"(var darkLayout = {
    paper_bgcolor: 'rgba(0,0,0,0)',
    plot_bgcolor: 'rgba(0,0,0,0)',
    font: { color: '#ccc', family: 'sans-serif' },
    xaxis: {
        gridcolor: 'rgba(255,255,255,0.08)',
        title: { text: 'Time' }
    },
    yaxis: {
        gridcolor: 'rgba(255,255,255,0.08)',
        title: { text: 'Temperature (°C)' }
    },
    legend: { bgcolor: 'rgba(0,0,0,0)', font: { color: '#ccc' } },
    margin: { l: 60, r: 30, t: 40, b: 60 },
    hovermode: 'x unified'
};
var config = { responsive: true, displayModeBar: true };

)";

    // Line chart traces
    file << "var lineTraces = [\n";
    for (size_t i = 0; i < data.series.size(); ++i) {
        const auto& ts = data.series[i];
        file << "  {\n";
        file << "    x: " << to_json_array(ts.timestamps) << ",\n";
        file << "    y: " << to_json_array(ts.values) << ",\n";
        file << "    name: '" << ts.name << "',\n";
        file << "    type: 'scatter', mode: 'lines+markers',\n";
        file << "    line: { color: colors[" << (i % 10) << "], width: 2.5, shape: 'spline' },\n";
        file << "    marker: { size: 5 }\n";
        file << "  }" << (i + 1 < data.series.size() ? "," : "") << "\n";
    }
    file << "];\n";
    file << "Plotly.newPlot('lineChart', lineTraces, "
         << "Object.assign({}, darkLayout, {title:{text:'Temperature Curves',font:{color:'#48dbfb'}}}), config);\n\n";

    // Area chart traces
    file << "var areaTraces = [\n";
    for (size_t i = 0; i < data.series.size(); ++i) {
        const auto& ts = data.series[i];
        file << "  {\n";
        file << "    x: " << to_json_array(ts.timestamps) << ",\n";
        file << "    y: " << to_json_array(ts.values) << ",\n";
        file << "    name: '" << ts.name << "',\n";
        file << "    type: 'scatter', fill: 'tozeroy',\n";
        file << "    line: { color: colors[" << (i % 10) << "], width: 1.5 },\n";
        file << "    fillcolor: 'rgba(" << ((i * 80 + 255) % 256) << ","
             << ((i * 120 + 100) % 256) << ","
             << ((i * 50 + 200) % 256) << ",0.15)'\n";
        file << "  }" << (i + 1 < data.series.size() ? "," : "") << "\n";
    }
    file << "];\n";
    file << "Plotly.newPlot('areaChart', areaTraces, "
         << "Object.assign({}, darkLayout, {title:{text:'Temperature Area',font:{color:'#48dbfb'}}}), config);\n\n";

    // Bar chart traces
    file << "var barTraces = [\n";
    for (size_t i = 0; i < data.series.size(); ++i) {
        const auto& ts = data.series[i];
        file << "  {\n";
        file << "    x: " << to_json_array(ts.timestamps) << ",\n";
        file << "    y: " << to_json_array(ts.values) << ",\n";
        file << "    name: '" << ts.name << "',\n";
        file << "    type: 'bar',\n";
        file << "    marker: { color: colors[" << (i % 10)
             << "], opacity: 0.85, line: { color: 'rgba(255,255,255,0.2)', width: 1 } }\n";
        file << "  }" << (i + 1 < data.series.size() ? "," : "") << "\n";
    }
    file << "];\n";
    file << "Plotly.newPlot('barChart', barTraces, "
         << "Object.assign({}, darkLayout, {barmode:'group', title:{text:'Temperature Bars',font:{color:'#48dbfb'}}}), config);\n\n";

    file << "</script>\n";
    file << html_footer();
    file.close();

    return output_path;
}

std::string ChartGenerator::generate_surface_chart(const TemperatureDataSet& data,
                                                   const std::string& output_path) {
    std::ofstream file(output_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot write to: " + output_path);
    }

    file << html_header(data.title);

    file << "    <h1>" << escape_html(data.title) << "</h1>\n";
    file << "    <p class=\"subtitle\">3D Temperature Surface Visualization</p>\n\n";

    // 3D Surface chart
    file << "    <div class=\"chart-card\">\n";
    file << "        <h2>3D Surface Plot</h2>\n";
    file << "        <div id=\"surfaceChart\" class=\"chart-container\" style=\"min-height:600px\"></div>\n";
    file << "    </div>\n\n";

    // Heatmap
    file << "    <div class=\"chart-card\">\n";
    file << "        <h2>Heatmap</h2>\n";
    file << "        <div id=\"heatmapChart\" class=\"chart-container\"></div>\n";
    file << "    </div>\n\n";

    // Contour
    file << "    <div class=\"chart-card\">\n";
    file << "        <h2>Contour Map</h2>\n";
    file << "        <div id=\"contourChart\" class=\"chart-container\"></div>\n";
    file << "    </div>\n\n";

    file << "<script>\n";

    file << "var zData = " << to_json_2d_array(data.surface.z_values) << ";\n";
    file << "var xLabels = " << to_json_array(data.surface.x_labels) << ";\n";
    file << "var yLabels = " << to_json_array(data.surface.y_labels) << ";\n\n";

    // 3D Surface
    file << R"(var surfaceTrace = [{
    z: zData,
    x: xLabels,
    y: yLabels,
    type: 'surface',
    colorscale: 'Portland',
    colorbar: { title: '°C', tickfont: { color: '#ccc' }, titlefont: { color: '#ccc' } },
    contours: {
        z: { show: true, usecolormap: true, highlightcolor: '#fff', project: { z: true } }
    },
    lighting: { ambient: 0.6, diffuse: 0.7, specular: 0.4, roughness: 0.5 }
}];
var surfaceLayout = {
    paper_bgcolor: 'rgba(0,0,0,0)',
    font: { color: '#ccc' },
    scene: {
        xaxis: { title: 'X', gridcolor: 'rgba(255,255,255,0.1)', color: '#ccc' },
        yaxis: { title: 'Y', gridcolor: 'rgba(255,255,255,0.1)', color: '#ccc' },
        zaxis: { title: 'Temperature (°C)', gridcolor: 'rgba(255,255,255,0.1)', color: '#ccc' },
        bgcolor: 'rgba(0,0,0,0)',
        camera: { eye: { x: 1.5, y: 1.5, z: 1.2 } }
    },
    margin: { l: 10, r: 10, t: 40, b: 10 },
    title: { text: '3D Temperature Surface', font: { color: '#48dbfb' } }
};
Plotly.newPlot('surfaceChart', surfaceTrace, surfaceLayout, { responsive: true });

)";

    // Heatmap
    file << R"(var heatmapTrace = [{
    z: zData,
    x: yLabels,
    y: xLabels,
    type: 'heatmap',
    colorscale: 'Portland',
    colorbar: { title: '°C', tickfont: { color: '#ccc' }, titlefont: { color: '#ccc' } }
}];
var heatLayout = {
    paper_bgcolor: 'rgba(0,0,0,0)',
    plot_bgcolor: 'rgba(0,0,0,0)',
    font: { color: '#ccc' },
    xaxis: { title: 'Y-Axis' },
    yaxis: { title: 'X-Axis' },
    margin: { l: 80, r: 30, t: 40, b: 60 },
    title: { text: 'Temperature Heatmap', font: { color: '#48dbfb' } }
};
Plotly.newPlot('heatmapChart', heatmapTrace, heatLayout, { responsive: true });

)";

    // Contour
    file << R"(var contourTrace = [{
    z: zData,
    x: yLabels,
    y: xLabels,
    type: 'contour',
    colorscale: 'Portland',
    contours: { coloring: 'heatmap', showlabels: true, labelfont: { size: 11, color: 'white' } },
    colorbar: { title: '°C', tickfont: { color: '#ccc' }, titlefont: { color: '#ccc' } },
    line: { smoothing: 0.85 }
}];
var contourLayout = {
    paper_bgcolor: 'rgba(0,0,0,0)',
    plot_bgcolor: 'rgba(0,0,0,0)',
    font: { color: '#ccc' },
    xaxis: { title: 'Y-Axis' },
    yaxis: { title: 'X-Axis' },
    margin: { l: 80, r: 30, t: 40, b: 60 },
    title: { text: 'Temperature Contour', font: { color: '#48dbfb' } }
};
Plotly.newPlot('contourChart', contourTrace, contourLayout, { responsive: true });

)";

    file << "</script>\n";
    file << html_footer();
    file.close();

    return output_path;
}

std::string ChartGenerator::generate_combined_chart(const TemperatureDataSet& data,
                                                    const std::string& output_path) {
    std::ofstream file(output_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot write to: " + output_path);
    }

    file << html_header(data.title);

    file << "    <h1>" << escape_html(data.title) << "</h1>\n";
    file << "    <p class=\"subtitle\">Combined Temperature Visualization — Curves &amp; Surface</p>\n\n";

    // Stats
    if (!data.series.empty()) {
        file << "    <div class=\"stats-grid\">\n";
        for (const auto& ts : data.series) {
            if (ts.values.empty()) continue;
            double min_v = ts.values[0], max_v = ts.values[0], sum = 0;
            for (double v : ts.values) {
                if (v < min_v) min_v = v;
                if (v > max_v) max_v = v;
                sum += v;
            }
            double avg = sum / static_cast<double>(ts.values.size());
            file << "    <div class=\"stat-card\">"
                 << "<div class=\"stat-value\">" << avg << "&deg;</div>"
                 << "<div class=\"stat-label\">" << escape_html(ts.name) << " Avg</div></div>\n";
            file << "    <div class=\"stat-card\">"
                 << "<div class=\"stat-value\">" << max_v << "&deg;</div>"
                 << "<div class=\"stat-label\">" << escape_html(ts.name) << " Max</div></div>\n";
            file << "    <div class=\"stat-card\">"
                 << "<div class=\"stat-value\">" << min_v << "&deg;</div>"
                 << "<div class=\"stat-label\">" << escape_html(ts.name) << " Min</div></div>\n";
        }
        file << "    </div>\n\n";
    }

    // Line chart
    file << "    <div class=\"chart-card\">\n";
    file << "        <h2>Temperature Curves</h2>\n";
    file << "        <div id=\"lineChart\" class=\"chart-container\"></div>\n";
    file << "    </div>\n\n";

    if (data.has_surface) {
        file << "    <div class=\"chart-card\">\n";
        file << "        <h2>3D Surface Plot</h2>\n";
        file << "        <div id=\"surfaceChart\" class=\"chart-container\" style=\"min-height:600px\"></div>\n";
        file << "    </div>\n\n";

        file << "    <div class=\"chart-card\">\n";
        file << "        <h2>Heatmap</h2>\n";
        file << "        <div id=\"heatmapChart\" class=\"chart-container\"></div>\n";
        file << "    </div>\n\n";
    }

    file << "<script>\n";

    file << "var colors = ['#ff6b6b','#48dbfb','#feca57','#ff9ff3','#54a0ff','#5f27cd',\n"
         << "              '#01a3a4','#f368e0','#ee5a24','#009432'];\n";

    file << R"(var darkLayout = {
    paper_bgcolor: 'rgba(0,0,0,0)',
    plot_bgcolor: 'rgba(0,0,0,0)',
    font: { color: '#ccc', family: 'sans-serif' },
    xaxis: { gridcolor: 'rgba(255,255,255,0.08)', title: { text: 'Time' } },
    yaxis: { gridcolor: 'rgba(255,255,255,0.08)', title: { text: 'Temperature (°C)' } },
    legend: { bgcolor: 'rgba(0,0,0,0)', font: { color: '#ccc' } },
    margin: { l: 60, r: 30, t: 40, b: 60 },
    hovermode: 'x unified'
};
var config = { responsive: true, displayModeBar: true };

)";

    // Line traces
    file << "var lineTraces = [\n";
    for (size_t i = 0; i < data.series.size(); ++i) {
        const auto& ts = data.series[i];
        file << "  {\n";
        file << "    x: " << to_json_array(ts.timestamps) << ",\n";
        file << "    y: " << to_json_array(ts.values) << ",\n";
        file << "    name: '" << ts.name << "',\n";
        file << "    type: 'scatter', mode: 'lines+markers',\n";
        file << "    line: { color: colors[" << (i % 10) << "], width: 2.5, shape: 'spline' },\n";
        file << "    marker: { size: 5 }\n";
        file << "  }" << (i + 1 < data.series.size() ? "," : "") << "\n";
    }
    file << "];\n";
    file << "Plotly.newPlot('lineChart', lineTraces, "
         << "Object.assign({}, darkLayout, {title:{text:'Temperature Curves',font:{color:'#48dbfb'}}}), config);\n\n";

    if (data.has_surface) {
        file << "var zData = " << to_json_2d_array(data.surface.z_values) << ";\n";
        file << "var xLabels = " << to_json_array(data.surface.x_labels) << ";\n";
        file << "var yLabels = " << to_json_array(data.surface.y_labels) << ";\n\n";

        file << R"(Plotly.newPlot('surfaceChart', [{
    z: zData, x: xLabels, y: yLabels,
    type: 'surface', colorscale: 'Portland',
    colorbar: { title: '°C', tickfont: { color: '#ccc' }, titlefont: { color: '#ccc' } },
    contours: { z: { show: true, usecolormap: true, highlightcolor: '#fff', project: { z: true } } },
    lighting: { ambient: 0.6, diffuse: 0.7, specular: 0.4, roughness: 0.5 }
}], {
    paper_bgcolor: 'rgba(0,0,0,0)',
    font: { color: '#ccc' },
    scene: {
        xaxis: { title: 'X', gridcolor: 'rgba(255,255,255,0.1)', color: '#ccc' },
        yaxis: { title: 'Y', gridcolor: 'rgba(255,255,255,0.1)', color: '#ccc' },
        zaxis: { title: 'Temperature (°C)', gridcolor: 'rgba(255,255,255,0.1)', color: '#ccc' },
        bgcolor: 'rgba(0,0,0,0)',
        camera: { eye: { x: 1.5, y: 1.5, z: 1.2 } }
    },
    margin: { l: 10, r: 10, t: 40, b: 10 },
    title: { text: '3D Temperature Surface', font: { color: '#48dbfb' } }
}, { responsive: true });

Plotly.newPlot('heatmapChart', [{
    z: zData, x: yLabels, y: xLabels,
    type: 'heatmap', colorscale: 'Portland',
    colorbar: { title: '°C', tickfont: { color: '#ccc' }, titlefont: { color: '#ccc' } }
}], {
    paper_bgcolor: 'rgba(0,0,0,0)',
    plot_bgcolor: 'rgba(0,0,0,0)',
    font: { color: '#ccc' },
    xaxis: { title: 'Y-Axis' }, yaxis: { title: 'X-Axis' },
    margin: { l: 80, r: 30, t: 40, b: 60 },
    title: { text: 'Temperature Heatmap', font: { color: '#48dbfb' } }
}, { responsive: true });
)";
    }

    file << "</script>\n";
    file << html_footer();
    file.close();

    return output_path;
}
