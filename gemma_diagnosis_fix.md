# Gemma 红外图像诊断 — 无分析结果根因分析及修复方案

## 问题现象

输入包含完整数据（大腿/小腿/脚的温度），但模型输出只有：

```
【各区域分析】
【最终结论】
```

标题存在，但没有任何具体内容，数据也没有被原样复述。

---

## 根因分析

### 1. n_predict = 512 但实际只生成了 12 个 token

服务器日志显示：
```
eval time = 1191.10 ms / 12 tokens
```
模型在输出仅 12 个 token 后就停止了。这是**提前终止**，不是正常完成。

### 2. Prompt 结尾触发了模型的"角色切换"行为

当前 prompt 末尾是：
```
请开始输出：
【数据】
```

Gemma 的 chat template 使用 `<start_of_turn>model\n` 标记模型回复开始。
当 prompt 以 `【数据】` 结尾时，模型将其理解为**已经开始了"数据"部分的输出**，
然后直接跳到下一个格式节点（`【各区域分析】`、`【最终结论】`），输出两个标题后即停止。

本质上是 **prompt 末尾的内容泄漏进了模型的"已生成"上下文**，导致模型认为数据部分已经写完，只剩标题需要生成。

### 3. `stop` 参数被注释掉

```cpp
//j["stop"] = { "</s>", "[Stop]" };
```

Gemma 的 EOS token 是 `<eos>`，正确应使用 Gemma 的停止符。
但这不是主要问题，主要问题是 prompt 设计。

### 4. Gemma 使用 Chat Template，原始 `/completion` 接口需要手动包裹格式

Gemma-4 系列模型需要严格的 chat template 格式：
```
<start_of_turn>user
{用户输入}
<end_of_turn>
<start_of_turn>model
```
如果使用 `/completion` 接口（原始补全）而不是 `/v1/chat/completions`，
必须手动在 prompt 里加入这个结构，否则模型无法正确区分"用户说的"和"模型要输出的"。

---

## 修复方案

### 方案 A（推荐）：改用 `/v1/chat/completions` 接口 + messages 格式

这是最稳定的方式，llama-server 内部会自动应用 chat template。

```cpp
CString CallGemma(const CStringW& input)
{
    CString result;

    HINTERNET hSession = WinHttpOpen(
        L"MFC-GEMMA/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!hSession) return "WinHttpOpen failed";

    HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", 8080, 0);
    if (!hConnect) return "WinHttpConnect failed";

    // ★ 改用 /v1/chat/completions
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"POST",
        L"/v1/chat/completions",   // ← 关键修改
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0);
    if (!hRequest) return "WinHttpOpenRequest failed";

    WinHttpSetTimeouts(hRequest, 60000, 60000, 60000, 180000);

    // ★ 使用 messages 格式
    json j;
    j["model"] = "gemma";
    j["messages"] = json::array({
        {
            {"role", "user"},
            {"content", CStringToUTF8(input)}
        }
    });
    j["max_tokens"] = 1024;
    j["temperature"] = 0.7;
    j["top_p"] = 0.7;
    j["repeat_penalty"] = 1.15;
    // ★ Gemma 的停止符
    j["stop"] = json::array({"<eos>", "<end_of_turn>"});

    std::string body = j.dump();

    LPCWSTR headers = L"Content-Type: application/json\r\n";
    BOOL bResult = WinHttpSendRequest(
        hRequest, headers, -1,
        (LPVOID)body.c_str(), (DWORD)body.length(), (DWORD)body.length(), 0);
    if (!bResult) return "WinHttpSendRequest failed";

    bResult = WinHttpReceiveResponse(hRequest, NULL);
    if (!bResult) return "WinHttpReceiveResponse failed";

    std::string response;
    DWORD dwSize = 0;
    do {
        DWORD dwDownloaded = 0;
        WinHttpQueryDataAvailable(hRequest, &dwSize);
        if (dwSize == 0) break;
        std::vector<char> buffer(dwSize + 1);
        ZeroMemory(buffer.data(), dwSize + 1);
        WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded);
        response.append(buffer.data(), dwDownloaded);
    } while (dwSize > 0);

    try {
        json res = json::parse(response);
        // /v1/chat/completions 返回格式
        if (res.contains("choices") && !res["choices"].empty()) {
            auto& choice = res["choices"][0];
            if (choice.contains("message") && choice["message"].contains("content")) {
                result = UTF8ToCString(choice["message"]["content"].get<std::string>());
            } else if (choice.contains("text")) {
                result = UTF8ToCString(choice["text"].get<std::string>());
            }
        } else if (res.contains("content")) {
            result = UTF8ToCString(res["content"].get<std::string>());
        } else {
            result = UTF8ToCString(response);
        }
    } catch (...) {
        result = UTF8ToCString(response);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}
```

### 方案 B：保留 `/completion` 接口，但修正 Prompt 格式（手动包裹 Chat Template）

如果必须使用 `/completion`，需要在 prompt 构造时手动加入 Gemma 的 chat template 格式，
**并且 prompt 末尾必须是 `<start_of_turn>model\n`，而不是任何数据内容**：

```cpp
// OnBnClickedButtonAitrain 中的 prompt 构造部分
CStringW input;

// ★ 手动包裹 Gemma Chat Template
input += L"<start_of_turn>user\n";

// 角色设定
input += L"你是一名临床医学红外热成像分析专家。\n";
input += L"必须用中文输出完整医学分析报告。\n";
input += L"禁止输出代码、JSON或编程内容。\n";

// 数据
input += L"\n【数据】\n";
for (int i = 0; i < (int)allImages.size(); i++)
{
    CStringW line;
    line.Format(L"图像%d：\n", i + 1);
    input += line;
    for (auto& kv : allImages[i])
    {
        CStringW tmp;
        tmp.Format(L"  %ls：%.1f℃\n", kv.first.GetString(), kv.second);
        input += tmp;
    }
    input += L"\n";
}

// 输出要求
input +=
    L"\n【输出要求】\n"
    L"1. 必须完整输出以下三个部分：\n"
    L"【数据】\n"
    L"【各区域分析】\n"
    L"【最终结论】\n"
    L"\n"
    L"2. 【数据】必须原样复述\n"
    L"\n"
    L"3. 【各区域分析】格式如下：\n"
    L"- 大腿：温度35.4℃，正常\n"
    L"\n"
    L"4. 如果无法判断异常：只输出温度，不写判断\n"
    L"\n"
    L"5. 禁止输出JSON、代码或解释说明\n";

// ★ 关键：结束用户轮次，开始模型轮次
// 不要在这里写任何数据或标题，让模型自己从【数据】开始输出
input += L"<end_of_turn>\n";
input += L"<start_of_turn>model\n";
// ★ 不要追加 "【数据】" 或其他任何内容

CString output = CallGemma(input);
AfxMessageBox(output);
```

同时在 `CallGemma` 中修正停止符：
```cpp
// 将被注释的 stop 改为 Gemma 正确的停止符
j["stop"] = json::array({"<eos>", "<end_of_turn>"});
// n_predict 适当增大
j["n_predict"] = 1024;
```

---

## 各原因权重总结

| 原因 | 严重程度 | 说明 |
|------|----------|------|
| Prompt 末尾写了 `【数据】`，被模型当成已生成内容 | ★★★★★ 最关键 | 模型跳过数据直接输出后续标题 |
| 使用 `/completion` 而非 `/v1/chat/completions` | ★★★★ 重要 | 未应用 chat template，模型无法区分 user/model 轮次 |
| `stop` 被注释，缺少 Gemma 的 EOS token | ★★★ 次要 | 可能导致输出在错误位置停止 |
| `n_predict = 512` 偏小 | ★★ 轻微 | 对于本场景内容量足够，但建议改为 1024 |

---

## 快速验证方法

在 llama-server 的 WebUI（通常是 `http://127.0.0.1:8080`）中，
直接用以下 prompt 测试（使用 Chat 模式），如果输出正常，说明是 API 调用方式的问题：

```
你是一名临床医学红外热成像分析专家。
必须用中文输出完整医学分析报告。

【数据】
图像1：
  大腿：35.4℃
  小腿：35.8℃
  脚：35.3℃

【输出要求】
必须输出【数据】【各区域分析】【最终结论】三部分。
```
