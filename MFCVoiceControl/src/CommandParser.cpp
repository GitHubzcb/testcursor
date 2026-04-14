#include "stdafx.h"
#include "CommandParser.h"

CommandParser::CommandParser()
{
    // 默认参数
    AddParameter("温度", {"温度", "温度值", "temp"});
    AddParameter("湿度", {"湿度", "湿度值", "humidity"});
    AddParameter("压力", {"压力", "压力值", "气压", "pressure"});

    // 默认模式
    AddMode("自动", {"自动", "自动模式", "auto"});
    AddMode("手动", {"手动", "手动模式", "manual"});
    AddMode("节能", {"节能", "节能模式", "省电", "eco"});

    // 中文数字映射
    m_chineseDigits["零"] = 0; m_chineseDigits["〇"] = 0;
    m_chineseDigits["一"] = 1; m_chineseDigits["壹"] = 1;
    m_chineseDigits["二"] = 2; m_chineseDigits["贰"] = 2; m_chineseDigits["两"] = 2;
    m_chineseDigits["三"] = 3; m_chineseDigits["叁"] = 3;
    m_chineseDigits["四"] = 4; m_chineseDigits["肆"] = 4;
    m_chineseDigits["五"] = 5; m_chineseDigits["伍"] = 5;
    m_chineseDigits["六"] = 6; m_chineseDigits["陆"] = 6;
    m_chineseDigits["七"] = 7; m_chineseDigits["柒"] = 7;
    m_chineseDigits["八"] = 8; m_chineseDigits["捌"] = 8;
    m_chineseDigits["九"] = 9; m_chineseDigits["玖"] = 9;
}

void CommandParser::AddParameter(const std::string& name,
                                  const std::vector<std::string>& aliases)
{
    m_parameters[name] = aliases;
}

void CommandParser::AddMode(const std::string& name,
                             const std::vector<std::string>& aliases)
{
    m_modes[name] = aliases;
}

CommandParser::Command CommandParser::Parse(const std::string& text)
{
    Command cmd;
    cmd.rawText = text;

    std::string normalized = TrimText(text);
    if (normalized.empty()) return cmd;

    // 1. 检查确认/重置命令
    if (normalized.find("确定") != std::string::npos ||
        normalized.find("确认") != std::string::npos ||
        normalized.find("OK") != std::string::npos ||
        normalized.find("ok") != std::string::npos) {
        cmd.type = Command::CMD_CONFIRM;
        cmd.isValid = true;
        cmd.responseText = "已确认";
        return cmd;
    }

    if (normalized.find("重置") != std::string::npos ||
        normalized.find("复位") != std::string::npos ||
        normalized.find("清除") != std::string::npos) {
        cmd.type = Command::CMD_RESET;
        cmd.isValid = true;
        cmd.responseText = "已重置";
        return cmd;
    }

    // 2. 检查开始/停止监听命令
    if (normalized.find("开始监听") != std::string::npos ||
        normalized.find("开始录音") != std::string::npos) {
        cmd.type = Command::CMD_START_LISTEN;
        cmd.isValid = true;
        cmd.responseText = "开始监听";
        return cmd;
    }

    if (normalized.find("停止监听") != std::string::npos ||
        normalized.find("停止录音") != std::string::npos) {
        cmd.type = Command::CMD_STOP_LISTEN;
        cmd.isValid = true;
        cmd.responseText = "停止监听";
        return cmd;
    }

    // 3. 检查模式选择命令
    if (normalized.find("选择") != std::string::npos ||
        normalized.find("切换") != std::string::npos ||
        normalized.find("模式") != std::string::npos) {
        std::string mode = MatchMode(normalized);
        if (!mode.empty()) {
            cmd.type = Command::CMD_SELECT_MODE;
            cmd.mode = mode;
            cmd.isValid = true;
            cmd.responseText = "已切换到" + mode + "模式";
            return cmd;
        }
    }

    // 4. 检查参数设置命令（核心：如 "设置温度48"）
    std::string param = MatchParameter(normalized);
    if (!param.empty()) {
        std::string afterParam = normalized;
        // 找到参数名后面的部分
        for (auto& alias : m_parameters[param]) {
            size_t pos = afterParam.find(alias);
            if (pos != std::string::npos) {
                afterParam = afterParam.substr(pos + alias.length());
                break;
            }
        }

        // 先做中文数字转换
        std::string converted = ChineseNumberToArabic(afterParam);
        std::string value = ExtractNumber(converted);

        if (!value.empty()) {
            cmd.type = Command::CMD_SET_VALUE;
            cmd.parameter = param;
            cmd.value = value;
            cmd.isValid = true;
            cmd.responseText = "已设置" + param + "为" + value;
            return cmd;
        }
    }

    // 5. 尝试不带"设置"前缀的直接参数赋值（如 "温度48"）
    if (cmd.type == Command::CMD_NONE) {
        for (auto& kv : m_parameters) {
            for (auto& alias : kv.second) {
                size_t pos = normalized.find(alias);
                if (pos != std::string::npos) {
                    std::string afterAlias = normalized.substr(pos + alias.length());
                    std::string converted = ChineseNumberToArabic(afterAlias);
                    std::string value = ExtractNumber(converted);
                    if (!value.empty()) {
                        cmd.type = Command::CMD_SET_VALUE;
                        cmd.parameter = kv.first;
                        cmd.value = value;
                        cmd.isValid = true;
                        cmd.responseText = "已设置" + kv.first + "为" + value;
                        return cmd;
                    }
                }
            }
        }
    }

    return cmd;
}

std::string CommandParser::ChineseNumberToArabic(const std::string& text)
{
    std::string result;
    // UTF-8 中文字符为3字节
    size_t i = 0;
    while (i < text.size()) {
        // 检查是否是3字节 UTF-8 字符（中文）
        if (i + 2 < text.size() &&
            (static_cast<unsigned char>(text[i]) & 0xE0) == 0xE0) {
            std::string ch = text.substr(i, 3);

            auto it = m_chineseDigits.find(ch);
            if (it != m_chineseDigits.end()) {
                result += std::to_string(it->second);
                i += 3;
                continue;
            }

            // 处理 "十" = 10
            if (ch == "十" || ch == "拾") {
                if (result.empty() || !std::isdigit(result.back())) {
                    result += "1";
                }
                result += "0";
                i += 3;
                // 如果十后面还有数字，替换掉最后的0
                if (i + 2 < text.size()) {
                    std::string nextCh = text.substr(i, 3);
                    auto nextIt = m_chineseDigits.find(nextCh);
                    if (nextIt != m_chineseDigits.end()) {
                        result.back() = '0' + nextIt->second;
                        i += 3;
                    }
                }
                continue;
            }

            // 处理 "百" = 100
            if (ch == "百" || ch == "佰") {
                result += "00";
                i += 3;
                continue;
            }

            // 处理 "点" / "." 小数点
            if (ch == "点") {
                result += ".";
                i += 3;
                continue;
            }
        }

        result += text[i];
        i++;
    }

    return result;
}

std::string CommandParser::ExtractNumber(const std::string& text)
{
    // 匹配整数或小数（包括负数）
    std::regex numRegex("-?\\d+\\.?\\d*");
    std::smatch match;
    if (std::regex_search(text, match, numRegex)) {
        return match.str();
    }
    return "";
}

std::string CommandParser::MatchParameter(const std::string& text)
{
    for (auto& kv : m_parameters) {
        for (auto& alias : kv.second) {
            if (text.find(alias) != std::string::npos) {
                return kv.first;
            }
        }
    }
    return "";
}

std::string CommandParser::MatchMode(const std::string& text)
{
    for (auto& kv : m_modes) {
        for (auto& alias : kv.second) {
            if (text.find(alias) != std::string::npos) {
                return kv.first;
            }
        }
    }
    return "";
}

std::string CommandParser::TrimText(const std::string& text)
{
    std::string result = text;

    // 去除常见中文标点
    std::vector<std::string> punctuations = {
        "。", "，", "、", "；", "：", "！", "？",
        "（", "）", "【", "】", "《", "》",
        """, """, "'", "'", "…"
    };

    for (auto& p : punctuations) {
        size_t pos;
        while ((pos = result.find(p)) != std::string::npos) {
            result.erase(pos, p.length());
        }
    }

    // 去除 ASCII 标点和首尾空格
    while (!result.empty() && (result.front() == ' ' || result.front() == '\t' ||
           result.front() == '.' || result.front() == ',')) {
        result.erase(result.begin());
    }
    while (!result.empty() && (result.back() == ' ' || result.back() == '\t' ||
           result.back() == '.' || result.back() == ',')) {
        result.pop_back();
    }

    return result;
}
