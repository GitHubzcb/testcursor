# MainBoardAfterAging.cpp 修改指南

配套的 `MainBoardAfterAging.h` 已给出完整修改版（见 `src/`，仅新增成员 `m_nEleTypeSel`）。
本文件共 9000+ 行，采用**定点替换**方式给出修改；每处修改附原代码与替换代码，
按函数定位（行号为参考值，以函数名为准）。

各修改对应的问题编号见 `docs/主板测试程序问题分析与重构建议.md`：
- 修改 1/2/3/11 → 3.4 线程生命周期（TerminateThread、悬空 CWinThread 指针）
- 修改 4/5      → 3.1/3.2 消息风暴与轮询节流
- 修改 6/7      → 3.1 工作线程操作 UI（死机首因）
- 修改 8/9      → 3.5 缓冲区越界（COM≥10 崩溃、chname 栈溢出）
- 修改 10       → 3.2 UI 线程 Sleep(3000) 假死与定时器重入
- 修改 12       → 4.6 通讯中断时旧值误判（症状③）

---

## 修改 1：构造函数（约第 100~120 行）——修复越界写 + 补线程成员初始化

原代码：

```cpp
	fEletrodeTemp[2] = { 0.0 };
	m_setEletrodeMode = electrode1_monpolar;
	fRTimes = 0.0;
	fFTimes = 0.0;
	m_getStimAcc = { 0.0 };

	//初始化电源线程0723
	m_pBlaComThread = NULL;
	m_hBlaComEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_bBlaComThreadRunning = false;

}
```

替换为：

```cpp
	// 修复：原 fEletrodeTemp[2] = { 0.0 } 是对下标 2 的越界写（数组大小为 2）
	memset(fEletrodeTemp, 0, sizeof(fEletrodeTemp));
	m_setEletrodeMode = electrode1_monpolar;
	fRTimes = 0.0;
	fFTimes = 0.0;
	m_getStimAcc = { 0.0 };

	//初始化电源线程0723
	m_pBlaComThread = NULL;
	m_hBlaComEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_bBlaComThreadRunning = false;

	// 修复：以下成员原来未初始化就被 OnInitDialog→StartDataCollection 读取
	m_pDataThread = NULL;
	m_hThreadEvent = NULL;
	m_bThreadRunning = false;
	DevCnt = 0;
	DevCntLast = 0;
	m_nEleTypeSel = 0;   // 新增：电刀型号缓存（见头文件说明）
}
```

## 修改 2：析构函数（约第 122~133 行）——修复对 auto-delete 线程的悬空访问

原代码：

```cpp
CMainBoardAfterAging::~CMainBoardAfterAging()
{
	//0723
	if (m_pBlaComThread)
	{
		SetEvent(m_hBlaComEvent); // 通知线程退出
		WaitForSingleObject(m_pBlaComThread->m_hThread, 1000);
	}
	CloseHandle(m_hBlaComEvent);
	//0724
	StopDataCollection();
}
```

替换为：

```cpp
CMainBoardAfterAging::~CMainBoardAfterAging()
{
	//0723
	// 修复：m_pBlaComThread 现在以 m_bAutoDelete=FALSE 创建（见修改 11），
	// 原版对默认 auto-delete 的 CWinThread 做 WaitForSingleObject 是悬空指针访问
	// （线程结束时 CWinThread 对象已 delete）
	if (m_pBlaComThread)
	{
		SetEvent(m_hBlaComEvent); // 通知线程退出
		WaitForSingleObject(m_pBlaComThread->m_hThread, 3000);
		delete m_pBlaComThread;
		m_pBlaComThread = NULL;
	}
	CloseHandle(m_hBlaComEvent);
	//0724
	StopDataCollection();
}
```

## 修改 3：StopDataCollection（约第 691~708 行）——禁用 TerminateThread

原代码：

```cpp
		if (WaitForSingleObject(m_pDataThread->m_hThread, 3000) == WAIT_TIMEOUT)
		{
			TerminateThread(m_pDataThread->m_hThread, 0);
		}
```

替换为：

```cpp
		// 修复：禁用 TerminateThread——强杀持有 CRT/堆锁的线程会损坏堆、
		// 造成之后任意 malloc/UI 调用死锁（死机根因之一）。
		// 采集线程已无长阻塞（见修改 4/6），10 秒内必然自然退出。
		WaitForSingleObject(m_pDataThread->m_hThread, 10000);
```

## 修改 4：RunDataCollection（约第 716~733 行）——轮询节流

原代码：

```cpp
UINT CMainBoardAfterAging::RunDataCollection()
{
	while (m_bThreadRunning)
	{
		// 处理测试数据
		ProcessTestData();
		// 处理COM口更新
		ProcessComboBoxUpdate();
		// 适当休眠，避免CPU占用过高
		Sleep(10);
		// 检查退出事件
		if (WaitForSingleObject(m_hThreadEvent, 0) == WAIT_OBJECT_0)
		{
			break;
		}
	}
	return 0;
}
```

替换为：

```cpp
UINT CMainBoardAfterAging::RunDataCollection()
{
	while (m_bThreadRunning)
	{
		// 先检查退出事件，收到后立即返回
		if (WaitForSingleObject(m_hThreadEvent, 0) == WAIT_OBJECT_0)
		{
			break;
		}
		// 处理测试数据
		ProcessTestData();
		// 处理COM口更新
		ProcessComboBoxUpdate();
		// 修复：原 10ms 轮询毫无必要（BLA 状态 ~400ms 才更新一次），
		// 造成仪器 IO 风暴与 WM_UPDATE_TEST_DATA 消息风暴
		Sleep(200);
	}
	return 0;
}
```

## 修改 5：ProcessTestData（约第 774~790 行）——消除消息风暴

原代码：

```cpp
	// 通知UI更新
	PostMessage(WM_UPDATE_TEST_DATA, 0, 0);
```

替换为：

```cpp
	// 修复：仅在有测试项运行时才通知 UI，空闲时不再向消息队列灌消息
	if (m_curBlatestFun != nonTest)
	{
		PostMessage(WM_UPDATE_TEST_DATA, 0, 0);
	}
```

## 修改 6：GetShow7EleSurAnaBlaValue（约第 2327~2361 行）——工作线程禁止操作 UI（死机首因）

原代码：

```cpp
	//读取电刀相关值
	int index = m_eleType.GetCurSel();
	if (index == 2)
	{
		auto result = m_kingpoAnalyzer->getResult();
		if (result.success)
		{
			try {
				if (result.data.contains("measurements")) {
					auto& measurements = result.data["measurements"];

					f7SurAnaPW = measurements["power"].get<float>();
					f7SurAnaImA = measurements["current"].get<float>();
					f7SurAnaVV = measurements["voltage_rms"].get<float>();
					UpdateData(FALSE);
				}
			}
			catch (const std::exception& e) {
				CString strError;
				strError.Format(_T("读取值失败: %s"), CString(e.what()));
				AfxMessageBox(strError);
			}
		}
	}
	else
	{
		m_eleSurgAnalyer.getContIPV(&f7SurAnaPW, &f7SurAnaImA, &f7SurAnaVV);
	}
```

替换为：

```cpp
	//读取电刀相关值
	// 修复 1：本函数运行在采集线程。m_eleType.GetCurSel() 底层是跨线程
	// SendMessage，会与 UI 线程的 Sleep 形成双向等待（界面假死主因），
	// 改读 UI 线程维护的缓存 m_nEleTypeSel（见修改 7）。
	int index = m_nEleTypeSel;
	if (index == 2)
	{
		auto result = m_kingpoAnalyzer->getResult();
		if (result.success)
		{
			try {
				if (result.data.contains("measurements")) {
					auto& measurements = result.data["measurements"];

					f7SurAnaPW = measurements["power"].get<float>();
					f7SurAnaImA = measurements["current"].get<float>();
					f7SurAnaVV = measurements["voltage_rms"].get<float>();
					// 修复 2：删除 UpdateData(FALSE)——MFC 明确禁止跨线程调用
					// （窗口句柄映射是线程私有的，轻则断言崩溃重则内存损坏）。
					// UI 刷新统一由 WM_UPDATE_TEST_DATA→UpdateCaliRFIV7UI 完成。
				}
			}
			catch (const std::exception& e) {
				// 修复 3：工作线程禁止弹窗（AfxMessageBox 同样是 UI 操作），
				// 记录到调试输出即可
				TRACE(_T("KingPo getResult parse failed: %s\n"), CString(e.what()));
			}
		}
	}
	else
	{
		m_eleSurgAnalyer.getContIPV(&f7SurAnaPW, &f7SurAnaImA, &f7SurAnaVV);
	}
```

> 同类检查：全文搜索 `UpdateData(`、`AfxMessageBox(`、`MessageBox(`、`GetCurSel()`、
> `GetWindowText(`，凡出现在 `ProcessTestData` 可达的函数
> （`GetandShow3Value`、`GetShow7/8/9...`、`GetShow14DOS*`、`GetShow15*`、`GetShowNEM*`）中的，
> 一律按上述三条原则处理：控件读改缓存、UI 写移入 `Update*UI`、弹窗改 TRACE。
> （`GetShow13Impedance*` 内的 `SetText` 可暂保留——它们只被 `OnTimer(0)` 在 UI 线程调用，
> `ProcessTestData` 中对应分支已被注释。）

## 修改 7：维护 m_nEleTypeSel 缓存（两处，均在 UI 线程）

7a. `OnInitDialog`（约第 566~569 行）：

```cpp
	m_eleType.AddString("ESU-2400");
	m_eleType.AddString("ESXTRA");
	m_eleType.AddString("KingPo");
	m_eleType.SetCurSel(0);
```

替换为：

```cpp
	m_eleType.AddString("ESU-2400");
	m_eleType.AddString("ESXTRA");
	m_eleType.AddString("KingPo");
	m_eleType.SetCurSel(0);
	m_nEleTypeSel = 0;   // 新增：同步缓存（供工作线程读取）
```

7b. `OnCbnSelchangeComboEletype`（文件末尾，约第 9069 行）函数体**开头**加一行：

```cpp
void CMainBoardAfterAging::OnCbnSelchangeComboEletype()
{
	m_nEleTypeSel = m_eleType.GetCurSel();   // 新增：UI 线程更新缓存
	// ……以下原有代码不动……
}
```

## 修改 8：GetUsbSn 及相关全局数组（约第 530~532、1469~1548 行）——修复 COM≥10 必然溢出

8a. 全局数组定义（约第 531~532 行）：

```cpp
int DevCnt, DevCntLast;
TCHAR pComName[10][5];
```

替换为：

```cpp
int DevCnt, DevCntLast;
// 修复："COM10" 及以上需要 6 个 TCHAR（含结尾符），原 [5] 必然溢出，
// 这正是"插拔多个 USB 串口设备后崩溃"的直接原因
TCHAR pComName[10][8];
```

8b. `GetUsbSn` 开头（约第 1471~1473 行）：

```cpp
	pComName[10][5] = NULL;
	//pBlaComName[5] = NULL;
	DevCnt = 0;
```

替换为：

```cpp
	// 修复：原 pComName[10][5] = NULL 是对越界地址的写入，不是清零；
	// 改为整体 memset
	memset(pComName, 0, sizeof(pComName));
	//pBlaComName[5] = NULL;
	DevCnt = 0;
```

8c. `GetUsbSn` 中拷贝 COM 名的代码（约第 1533~1548 行）：

```cpp
				TCHAR	*pFind = NULL;
				pFind = _tcsstr(buffer, _T("COM"));
				if (pFind != NULL)
				{
					//_stprintf(pszComName, _T("COM%d"), _ttoi(pFind + 3));
					sprintf_s(pszComName, 100, _T("COM%d"), _ttoi(pFind + 3));
					int k = 0;
					while (*pszComName != NULL)
					{
						pComName[DevCnt][k++] = *pszComName;
						pszComName++;
					}
					DevCnt++;


				}
```

替换为：

```cpp
				TCHAR	*pFind = NULL;
				pFind = _tcsstr(buffer, _T("COM"));
				if (pFind != NULL)
				{
					// 修复 1：调用方实参 ch 只有 10 个 TCHAR，原来谎报 100
					// 使 sprintf_s 的溢出保护完全失效
					sprintf_s(pszComName, 10, _T("COM%d"), _ttoi(pFind + 3));
					// 修复 2：原来直接推进 pszComName 指针且不回退，
					// 第二个设备起 sprintf_s 写入的是越界地址；改用局部指针拷贝。
					// 修复 3：DevCnt 加上限，防止越过 pComName[10]
					if (DevCnt < 10)
					{
						const TCHAR *p = pszComName;
						int k = 0;
						while (*p != _T('\0') && k < 7)
						{
							pComName[DevCnt][k++] = *p++;
						}
						pComName[DevCnt][k] = _T('\0');
						DevCnt++;
					}
				}
```

## 修改 9：chname 栈缓冲（3 处）——防 combo 文本超长溢出

以下 3 个函数内的相同模式一并修改：
- `OnUpdateComboBox`（约第 1376 行）
- `upDateComCombox`（约第 7933 行）
- `OnTimer` 的 `case 1`（约第 8001 行）

原代码（模式）：

```cpp
	TCHAR chname[6] = { 0 };
	...
	int m = XXX.GetLength();
	for (int i = 0; i < m; i++)
	{
		chname[i] = XXX.GetAt(i);
	}
```

替换为（模式）：

```cpp
	// 修复：原 [6] 在文本超过 5 字符时栈溢出（如 "COM10" 恰好 5 字符已到极限）
	TCHAR chname[16] = { 0 };
	...
	int m = XXX.GetLength();
	for (int i = 0; i < m && i < 15; i++)   // 修复：加边界
	{
		chname[i] = XXX.GetAt(i);
	}
```

注意 `OnUpdateComboBox` 和 `OnTimer case 1` 中该模式各出现**两次**
（EleSurAna 一次、BlaPortsName 一次），共 5 处拷贝循环都要加边界。

## 修改 10：OnTimer case 2 的 Sleep(3000)（约第 8071~8081 行）——消除 UI 假死与定时器重入

原代码：

```cpp
		case 1:
		{
			CString str = "";
			GetDlgItem(IDC_BUTTON_8GETPOWERSTAELESURANA)->GetWindowText(str);
			if (str == "……停止……")     // 按钮文字保持原文
			{
				OnBnClickedButton8getpowerstaelesurana();
			}
			Sleep(3000); //L4 H5 W2
			OnBnClickedButton9getpowerstaelesurana();
		}
			break;
```

替换为：

```cpp
		case 1:
		{
			CString str = "";
			GetDlgItem(IDC_BUTTON_8GETPOWERSTAELESURANA)->GetWindowText(str);
			if (str == "……停止……")     // 按钮文字保持原文
			{
				OnBnClickedButton8getpowerstaelesurana();
			}
			// 修复：删除 Sleep(3000)——UI 线程冻结 3 秒（假死），且期间定时器
			// 消息排队，醒来后 OnTimer 立即重入把按钮当成被重复点击。
			// 改为一次性 3 秒定时器过渡到新增阶段 10。
			RFTestFlag = 10;
			KillTimer(2);
			SetTimer(2, 3000, NULL);
		}
			break;
		case 10:   // 新增阶段：3 秒延时到，启动高档测试并恢复原定时周期
		{
			KillTimer(2);
			SetTimer(2, TIMER2_PERIOD_MS, NULL);
			OnBnClickedButton9getpowerstaelesurana();
			RFTestFlag = 2;   // 与原流程一致，下一拍进入 case 2
		}
			break;
```

其中 `TIMER2_PERIOD_MS` 用启动定时器 2 时的原周期：全文搜索 `SetTimer(2,`
找到启动该定时器的按钮处理函数（射频准确性自动流程入口），把它使用的周期值
定义为常量并在此引用，保证行为与原流程节拍一致。

> 同理排查其余按钮处理函数中 `Sleep(OPERATEQUIPSTEPTIME * 5)`（=1 秒）以上的长
> Sleep；短于 600ms 的可暂保留（P1 阶段再统一移入工作线程）。

## 修改 11：OnBnClickedButtonBlacomlink（约第 7893~7894 行）——线程对象生命周期

原代码：

```cpp
	// 启动线程
	m_pBlaComThread = AfxBeginThread(BlaComThreadProc, param);
```

替换为：

```cpp
	// 修复 1：上一个线程对象尚存时先回收（原版直接覆盖指针，泄漏 CWinThread）
	if (m_pBlaComThread)
	{
		WaitForSingleObject(m_pBlaComThread->m_hThread, 3000);
		delete m_pBlaComThread;
		m_pBlaComThread = NULL;
	}
	// 修复 2：以 m_bAutoDelete=FALSE 创建（挂起→设标志→恢复），
	// 使析构函数可以安全地等待并 delete（配合修改 2）
	m_pBlaComThread = AfxBeginThread(BlaComThreadProc, param,
		THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
	if (m_pBlaComThread)
	{
		m_pBlaComThread->m_bAutoDelete = FALSE;
		m_pBlaComThread->ResumeThread();
	}
	else
	{
		delete param;   // 线程未能创建时回收参数，避免泄漏
		GetDlgItem(IDC_BUTTON_BLACOMLINK)->EnableWindow(TRUE);
		return;
	}
```

## 修改 12（增强，建议做）：通讯中断显示——消除"液晶屏合格、软件显示不合格"

修改后的 `CBLACommunicate` 在状态帧超过 1 秒未更新时，所有 `Get*` 返回 FALSE
（不再送出旧数据）。在各 `GetShowXX` 函数开头利用这一点显式提示通讯中断，
以 `GetShow7EleSurAnaBlaValue` 为例，在 `if (m_curBlatestFun == nonTest)` 判断之后加：

```cpp
	// 新增：BLA 状态数据超过 1 秒未更新 → 通讯中断。
	// 旧版此时会继续用残留旧值判定并写 Excel，造成
	// "主板液晶屏数据合格、软件显示不合格"的误判。
	if (!m_blaCommunication.IsStatusFresh(1000))
	{
		m_str7BlaPMWatt = _T("通讯中断");
		m_str7BlaIMmA  = _T("通讯中断");
		m_str7BlaVMV   = _T("通讯中断");
		m_bMainBoardAferAgingQulityFlag.b7rCalREVolandI[3] = FALSE;
		m_bMainBoardAferAgingQulityFlag.b7rCalREVolandI[4] = FALSE;
		m_bMainBoardAferAgingQulityFlag.b7rCalREVolandI[5] = FALSE;
		return FALSE;
	}
```

其余 `GetShow8/9`、`GetShow13Impedance*`、`GetShow14DOSBla*`、`GetShow15*`、
`GetShowNEM*` 按同一模式：中断时把对应显示串置为"通讯中断"、合格标志置 FALSE、
提前 return，**绝不用上一次的值继续判定**。
（该修改保证：通讯恢复后下一拍数据即刷新为真实值；通讯没恢复时操作者看到的是
明确的"通讯中断"而不是一个看似真实的不合格数值。）

## 附：无需修改说明

- `GetandShow3Value`：只访问电源台与自身成员，配合修改后的 `CPowerStation`
  （持久会话 + 互斥）已线程安全，不需要改动。
- `OnTimer case 0`（阻抗轮询）与 `GetShow13Impedance*` 的 `SetText`：运行在
  UI 线程，允许操作控件。
- `CSpreadSheet.cpp/.h`、`define.h`：本轮不修改（Excel 方案更换属 P2 重构项；
  `define.h` 的 `#pragma pack(1)` 正确，协议布局勿动）。
