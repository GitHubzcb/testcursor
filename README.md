# testcursor

## Excel 文件工具函数

本项目包含 MFC (C++) 中用于检测 Excel 文件打开状态的工具函数。

---

### `GetFileNameWithoutExt`

```cpp
CString GetFileNameWithoutExt(const CString& filePath);
```

**功能**：从完整文件路径中提取不含扩展名的纯文件名。

**参数**：
| 参数 | 类型 | 说明 |
|------|------|------|
| `filePath` | `const CString&` | 文件的绝对路径或相对路径 |

**返回值**：`CString` — 不含路径前缀和扩展名的文件名。

**示例**：

| 输入 | 输出 |
|------|------|
| `C:\Users\test\demo.xlsx` | `demo` |
| `report.csv` | `report` |
| `C:\folder\file.name.txt` | `file.name` |
| `noextension` | `noextension` |

**处理逻辑**：
1. 使用 `ReverseFind('\\')` 找到最后一个反斜杠位置，截取其后的文件名（含扩展名）。
2. 使用 `ReverseFind('.')` 找到最后一个点的位置，截取其前的内容作为纯文件名。

---

### `CPayBoardTestDlg::IsExcelFileOpen`

```cpp
BOOL CPayBoardTestDlg::IsExcelFileOpen(const CString& filePath);
```

**功能**：判断指定路径的 Excel 文件是否已经在 Excel 应用程序中打开。

**参数**：
| 参数 | 类型 | 说明 |
|------|------|------|
| `filePath` | `const CString&` | Excel 文件的**绝对路径** |

**返回值**：`BOOL` — `TRUE` 表示文件已在 Excel 中打开，`FALSE` 表示未打开。

**处理逻辑**：
1. 通过 `GetActiveObject` 获取当前运行的 Excel COM 实例。如果没有 Excel 在运行，直接返回 `FALSE`。
2. 通过 `QueryInterface` 获取 `IDispatch` 接口，附加到 `CApplication` 对象。
3. 遍历所有已打开的工作簿（`CWorkbooks`），对每个工作簿：
   - 使用 `book.get_FullName()` 获取工作簿的**完整路径**
   - 与传入的 `filePath` 做**不区分大小写**的比较
4. 如果找到匹配项返回 `TRUE`，否则返回 `FALSE`。

**依赖**：
- MFC OLE Automation 支持
- Excel 类型库生成的包装类（`CApplication`、`CWorkbooks`、`CWorkbook`）

**注意事项**：
- 使用 `get_FullName()` 而非 `get_Name()` 进行比较，原因是 `Workbook.Name` 在某些情况下会被 Excel 自动追加数字后缀（如同名文件冲突时），导致匹配失败。
- `GetActiveObject` 只能获取到 ROT（Running Object Table）中注册的第一个 Excel 实例，多实例场景下可能无法检测到所有打开的文件。
