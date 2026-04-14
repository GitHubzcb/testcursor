#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <regex>

// 命令解析器 - 将语音识别文本转换为对话框操作指令
class CommandParser
{
public:
    // 解析后的命令结构
    struct Command {
        enum Type {
            CMD_NONE = 0,
            CMD_SET_VALUE,       // 设置参数值，如 "设置温度48"
            CMD_CONFIRM,         // 确认操作，如 "确定"
            CMD_RESET,           // 重置操作，如 "重置"
            CMD_SELECT_MODE,     // 选择模式，如 "选择自动模式"
            CMD_START_LISTEN,    // 开始监听
            CMD_STOP_LISTEN,     // 停止监听
        };

        Type        type = CMD_NONE;
        std::string parameter;      // 参数名称: "温度", "湿度", "压力"
        std::string value;          // 参数值: "48", "60", "101.3"
        std::string mode;           // 模式名称: "自动", "手动", "节能"
        std::string rawText;        // 原始识别文本
        std::string responseText;   // 语音回复文本
        bool        isValid = false;
    };

    CommandParser();
    ~CommandParser() = default;

    // 解析语音文本为命令
    Command Parse(const std::string& text);

    // 添加自定义参数名称映射
    // key: 参数名称（如 "温度"）
    // aliases: 别名列表（如 {"温度", "temp", "温度值"}）
    void AddParameter(const std::string& name,
                      const std::vector<std::string>& aliases);

    // 添加自定义模式名称
    void AddMode(const std::string& name,
                 const std::vector<std::string>& aliases);

private:
    // 中文数字转阿拉伯数字
    std::string ChineseNumberToArabic(const std::string& text);

    // 提取数值
    std::string ExtractNumber(const std::string& text);

    // 匹配参数名称
    std::string MatchParameter(const std::string& text);

    // 匹配模式名称
    std::string MatchMode(const std::string& text);

    // 去除文本前后空白和标点
    std::string TrimText(const std::string& text);

    // 参数名称 -> 别名列表
    std::map<std::string, std::vector<std::string>> m_parameters;
    // 模式名称 -> 别名列表
    std::map<std::string, std::vector<std::string>> m_modes;
    // 中文数字映射
    std::map<std::string, int> m_chineseDigits;
};
