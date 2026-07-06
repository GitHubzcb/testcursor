
#ifndef _PC_M2_PROTOCOL_H_
#define _PC_M2_PROTOCOL_H_
#ifdef __cplusplus
extern "C" {
#endif
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "define.h"

/*==============================================================================
 修改说明（对照原版）：
 1. 新增 PC_M2_ENABLE_CRC32 开关：
    置 1 时 crc32_get 计算真实 CRC32 (poly 0xEDB88320)，收发双方均校验；
    置 0（默认）时保持与现有固件一致的行为（CRC 字段填 0、接收不校验）。
    ★ 必须与 MCU 固件同步启用，否则对方会拒收本方帧！
 2. 新增 Pc_Com_Pack_Buf()：组包到调用方提供的缓冲区并返回帧长，
    线程安全（不再依赖全局 dat/send_len）。
    原 Pc_Com_Pack 保留以兼容旧调用点，但其本身仍非线程安全，
    新代码一律使用 Pc_Com_Pack_Buf。
 3. 其余枚举/结构体定义与原版完全一致（协议布局不变，勿改动）。
==============================================================================*/
#ifndef PC_M2_ENABLE_CRC32
#define PC_M2_ENABLE_CRC32   0
#endif

#define PC_TEST_ELEC_SELECT_PAGE  0           /*电极选择界面*/
#define PC_TEST_STIM_PAGE         1           /*电刺激页面*/
#define PC_TEST_RF_PAGE           2           /*连续射频界面*/
#define PC_TEST_TEST_PAGE         3           /*后台射频测试界面*/
#define PC_TEST_DEVICE_INFO_PAGE          4   /* 设备信息页面 */
#define PC_TEST_IMPD_CALIBRATION_PAGE     5   /* 阻抗矫正页面 */
#define PC_TEST_STIMULATOR_PAGE           6   /* 刺激标定页面 */
#define PC_TEST_RELAY_SWITCH_PAGE         7   /* 继电器切换页面 */
#define PC_TEST_PURF_PAGE                 8   /* 脉冲射频页面 */
#define PC_TEST_TEST_PLUG_PAGE            9   /* 测试狗页面 */

#define PC_TEST_IMPD_CALIBRATION_MONO        0x0616   /*单入单出*/
#define PC_TEST_IMPD_CALIBRATION_SINGLE_BIP  0x0618   /*单入双出*/
#define PC_TEST_IMPD_CALIBRATION_BIP         0x0617   /*双入双出*/
#define PC_TEST_IMPD_CALIBRATION_AUTO        0x0615   /*自动写入*/

#define PC_TEST_STIMULATOR_KEY_0V_1K    0x0251
#define PC_TEST_STIMULATOR_KEY_10V_1K   0x0252
#define PC_TEST_STIMULATOR_KEY_5V_500   0x0253
#define PC_TEST_STIMULATOR_KEY_8MA_500  0x0254
#define PC_TEST_STIMULATOR_KEY_1V_100   0x0255
#define PC_TEST_STIMULATOR_KEY_2MA_100  0x0256

#define PC_TEST_RELAY_SWITCH_KEY_RELAY          0x0261   /*继电器切换*/
#define PC_TEST_RELAY_SWITCH_KEY_NEM_CALIBRATE  0x0262   /*NEM标定*/
#define PC_TEST_RELAY_SWITCH_KEY_NEM_OPEN       0x0263   /*NEM开链*/
#define PC_TEST_RELAY_SWITCH_KEY_NEM_SHORT      0x0264   /*NEM短接*/
#define PC_TEST_RELAY_SWITCH_KEY_CURR_ACC_30    0x0265   /*电流挡位30%*/
#define PC_TEST_RELAY_SWITCH_KEY_CURR_ACC_15    0x0266   /*电流挡位15%*/

/*************************************************************************
	通信协议帧格式描述:

	起始位+标识码+指令数据长度+指令数据+CRC32+结束帧
	所有多字节数据：低字节在前，高字节在后!(STM32的小端模式)
	起始位(2Bytes):0XAA,0X55
	标识码(1Bytes):用于指示帧的意义

	指令数据长度(2Bytes):用于指示指令数据的长度
	指令数据:一般为主机发送给从机待处理数据，如果
			     主机只查询从机的响应数据，该区域可为空
			     即指令数据长度可以=0,=0时不发送该指令数据

	CRC32(4Bytes): 起始位+标识码+指令数据长度+指令数据部分的CRC32值
	结束帧(2Bytes):不参与CRC32计算范围,0XCC,0X33

	举例:
	如果M2发送控制命令给M0，则M0返回相同标识码指令，长度为0，
	应答帧以表示收到该指令；
	如果M2发送查询状态指令给M0,则M0返回相同标识码及自身状态信息
	给M2以表示上报数据。
****************************************************************************/


/*通信标识码枚举定义，长度均!=0*/
typedef enum
{
    pc_com_stimparam_flag= 0x01,				/*刺激标识码*/
    pc_com_rfparam_flag= 	0x02,				/*射频标识码*/
    pc_com_status_rot_flag=0x03,				/*主机周期训问查询状态标识码*/
    pc_com_app_ack_flag=0x04,                   /*APP应答MCU给PC的应答指令，长度为1*/

    pc_com_m0_set=0x05,                         /*M0设置*/
    pc_com_device_info=0x06,                    /*设备信息*/


    pc_com_elec_choose_sync_flag=0x10,          /*电极选择同步*/
    pc_com_stim_sync_flag=0x11,                 /*stim同步*/
    pc_com_ctrf_sync_flag=0x12,                 /*连续射频同步*/
    pc_com_prf_sync_flag=0x13,                  /*脉冲射频同步*/
    pc_com_sysinfo_sync_flag=0x14,              /*设备信息同步*/
    pc_com_testplug_sync_flag=0x15,             /*测试狗同步*/
    pc_com_patinfo_sync_flag=0x16,              /*PAT信息同步*/
    pc_com_music_sync_flag=0x17,                /*音乐同步指令*/
    pc_com_exit_connect=0x18,                   /*退出信息返回系统设备进入电极选择界面*/

    /* 0x20 ~ 0x2F 为上位机测试工装保留指令 */
    pc_com_test_get_status = 0x20,              /* 状态查询 */
    pc_com_test_set_page = 0x21,                /* 切换页面 */
    pc_com_test_set_stim_param = 0x22,          /* 刺激参数设置 */
    pc_com_test_set_rf_param = 0x23,            /* 射频参数设置 */
    pc_com_test_start_stop = 0x24,              /* 启动停止刺激或射频 */
    pc_com_test_elec_select = 0x25,             /* 电极选择 */
    pc_com_test_set_sys_info = 0x26,            /* 系统信息设置(机器型号)*/
    pc_com_test_set_rf_test_param = 0x27,       /* 后台测试页面参数设置 */

	pc_com_test_set_multimeter_relay = 0x28,    /* 测试工装设置万用表继电器 */
	pc_com_test_set_board_power = 0x29,         /* 主板与端口板是否上电 */
	pc_com_test_set_temp_relay = 0x2A,          /* 设置温度挡位继电器 端口板 */
	pc_com_test_get_port_board_info = 0x2B,     /* 读取端口板电极温度（电极信息） */

	pc_com_test_set_key_value = 0x2C,           /* 模拟主机按键键码 */

    pc_com_handshake_flag=0XFF,			        /*握手指令，BOOT/APP模式通用,指令数据长度为0*/
	pc_com_test_set_pu_rf_param = 0x2E,         /*脉冲射频参数设置*/

}PC_PROTOCOL_COMM_FLAG,P_PC_PROTOCOL_COMM_FLAG;

/* 用于设置万用表继电器测量 测试板 */
typedef enum
{
	relay_main_p_3v3 = 0,
	relay_main_p_5v,
	relay_main_m_5v,
	relay_main_h_5v,
	relay_main_u_5v,
	relay_main_d_12v,
	relay_main_d_3v3,
	//relay_main_power_24v,
	relay_main_24v,
	//relay_main_rgnd_power,
	relay_main_fanout,
	relay_main_p_12v,
	relay_main_p_12v_n,

	relay_port_p_3v3_0,
	relay_port_5v,
	relay_port_p_3v3_1,
	relay_multimeter_max,
	relay_main_port_non
}e_relay_multimeter_def, *pe_relay_multimeter_def;
/* BLA测试工装设置端口板是否上电 */
#define MAIN_BOARD_SELECT 0
#define PORT_BOARD_SELECT 1
#define BOARD_POWER_ON    0
#define BOARD_POWER_OFF   1
typedef struct
{
	u8     board_select;         /* 选择主板还是端口板 */
	u8     power_on_or_off;      /* 是否上电 */
}tTestBoardPower, *pTestBoardPower;


#define M2_SOFT_VERSION_LENTH    (14 + 1)     //M2 软件版本号t_m2_version的长度
//typedef enum
//{
//	OFF = 0,
//	ON = !OFF,
//} tWorkStatus, *pWorkStatus;/* 启动停止工作参数的枚举 */
typedef struct
{
	unsigned char verson[M2_SOFT_VERSION_LENTH]; //软件版本
	u8 sn[10];     //序列号
	u8 ctrl[3];    //
	//u8 rf[3];
	u8 heat[3];
	u8 port[3];
	u8 input[3];
	//u8 cqm[3];
	u8 init;
}t_m2_version, * p_m2_version;

typedef struct
{
	u16 work_state;     /* tWorkStatus 工作状态开始或停止 */
    u16 pageID;
    tRfvi_Adc viadc;    /*M2监测ADC*/
    tRfvi vidata;       /*M2监测数据*/
    float temp[2];      /* M2监测温度, 0:进风温度，1:出风温度 */
	t_m2_version version;
	char picture_verson[30];       /*图片版本 E1.10*/
	u8 device_model;               /*型号*/
    tCom_M0Back com_m0_back;
}t_pc_test_status,*p_pc_test_status;

/*M2发送M3电极设置信息*/
typedef struct
{
	u8                  work_mode;                      /* 射频为0xff，刺激为0 */
	u8        sim_work_mode;                  /* tSimWorkMode ,运动刺激还是感觉刺激 */
	u8      ui_work_status;                 /*界面运行状态*/
	tCom_SeegElecParam  ui_seeg_ele_para;               /*界面电极参数--品牌、电率、脉宽、阻抗*/
	u8                  ui_seeg_contactor_select[4];       /**/
	u8    ui_seeg_contactor_group_polar[4];     /*电极编组状态对应的极性*/
															  // tSeegEleInfor   SeegElSetInfor[2][18];
															  //u8              SeegTreatNum;
} tCom_M2ToM3EleSetInfor, *pCom_M2ToM3EleSetInfor;

/*SEEG刺激、射频信息结构体*/
typedef struct {
	u8            _2CSeegBoxState;            /*是否是SEEG工作模式*/
	u8                 SeegELMode;                 /*单双极应用*/
	u8             set_port[2];                /*端口连接状态*/
	uint8_t                 EleContactNum;              /*电极信息触点选择个数*/
	uint8_t                 SeegElSelectInfor[9][4];    /*成功选择的电极触点*/
	u8        seeg_contactor_group_polar[9][4];     /*电极编组状态对应的极性*/
	tCom_M2ToM3EleSetInfor  M2ToM3EleSetInfor;          /*发送电极设置信息*/
}tSeegInfor, *pSeegInfor;

/* 刺激设定参数命令 */
typedef struct
{
	u8 work_state;     /* 工作状态开始或停止 */
	u8 work_space; /* 界面显示状态 */
	u8 set_port[2];    /* 端口连接状态 */
	u8 stim_workmode; /* 刺激模式  运动刺激或感觉刺激*/
	u8 bip_port12; /* 12电极双极选择 */

	u8 stim_group[2];              /* 刺激参数组 */
	u8 stim_type;     /* 刺激类型 电压刺激或电流刺激 */
	float voltage;              	/* 刺激电压 v */
	float current;			/* 刺激电流 mA*/
	u8 motor_freq;              /* 运动频率 hz */
	u8 sens_freq;               /* 感觉频率 hz */
	float pulse_width;          /* 刺激脉冲宽度 ms */

	float motor_volt;           /* 运动电压电流输出 */
	float motor_curr;
	float sense_volt;           /* 感觉电压电流输出 */
	float sense_curr;
	u8 clr_range;        /* 清除量程 */
	u8  ramp_out;         /* 缓慢输出 */

	u8 VoltageGears;     /*电压幅度挡位*/
	u8 CurrentGears;     /*电流幅度挡位*/
	u8 SimOutPolar;           /*刺激输出极性*/
	u8  stim_setp_value;    /*刺激步进值*/

	tSeegInfor  SeegInfor;  /*2c发送Seeg信息*/

}tStimParam, *pStimParam;

typedef struct
{
	u16 seeg_current;        /*SEEG限制电流*/
	u16 seeg_imped;          /* seeg 阻抗，默认95欧 */
}tSeegBrandParam;
/* 连续射频控温类型 */
typedef enum {
	Standard = 1,
	Step,
	PowerCtNorm,    /*除去单入双出下的功率模式*/
	PowerCtBipOne,  /*单入双出下的功率模式*/
	Voltage,
}tCtRfTempType, *pCtRfTempType;
/* 连续射频参数设置 */
typedef struct
{
	u8 work_state;     /* 工作状态开始或停止 */
	u8 port_state[2];  /* 两个电极工作开始或停止 */
	u8 work_space;    /* 界面显示状态 */
	u8 set_port[2];    /* 端口连接状态 */
	u8 bip_port12; /* 12电极双极选择 */

	u8 para_group[5];           /* 参数组 */
	u16 work_time;              /* 设定工作时间 单位：sec*/
	u8 auto_add;       /* 射频自动升温 打开关闭 */
	u8 StartTimeMode;  /* 射频计时状态 开始时计或射频温度到计时 */

									 /* ------连续射频-------- */
	u8 temper;                  /* 连续射频控制温度 */
	u8 ct_mode;      /* 连续射频控温类型  stand / step*/
	u8 max_power_temp;          /* 控温设备最大功率 */
	u16 temp_work_time;         /*  温度模式工作时间 */

								/* 阶跃模式 */
	u8 step_ctr_temp;           /* 阶跃模式下的温度控制 */
	u8 start_temp;              /* STEP设置项 开始温度 */
	u8 step_temp;               /* 温度步进 */
	u16 step_time;              /* STEP 时间 sec */
	u8 final_temp;              /* 最终温度 */
	u8 delay_start;             /* 延迟启动时间 */
	u8 max_power_step;          /* 最大功率 */
	u16 step_work_time;         /*  阶跃模式工作时间 */

								/* 单入双出下的功率模式 */
	float  setpower;                    /* 设定功率 */
	u8 up_time;                         /* 上升时间 */
	u8 max_temp;                        /* 最大温度 */
	u16 power_work_time;                /* 功率模式工作时间 */
	u8 PowerImpedRange;                 /* 单入双出下功率模式下阻抗曲线的阻抗范围*/
	u8 ImpedMarkModeBip;      /* 阻抗监测 */
	u16 limit_imped;                    /* 阻抗限定--ON之后的阻抗变化 */
	u8 seeg_auto_add;       /* 射频自动升温 打开关闭 */

									/* 除去单入双出下的功率模式 */
	float  SetPowerNorm;            /* 设定功率 */
	u8 Up_TimeNorm;                 /* 上升时间 */
	u8 Max_TempNorm;                /* 最大温度 */
	u16 PowerNorm_work_time;        /* 功率模式工作时间 */




									/* 阻抗模式 */
	u8   SetVoltage;                   /* 设定值，百分比 */
	u8   up_time_Voltage;
	u16  work_time_Voltage;
	u8   max_power_Voltage;
	u8   VoltageImpedRange;  /*单入双出下功率模式下阻抗曲线的阻抗范围*/
	u8 ImpedMarkModeImped;    /*阻抗监测*/
	u16  imped_before;

	tSeegInfor  SeegInfor;  /*2c发送Seeg信息*/
	u16 seeg_brand;             /*SEEG品牌*/
	tSeegBrandParam seegBrandParam[3]; /*SEEG品牌对应电流限制，功率*/

}tCtRfParam, *pCtRfParam;

/*脉冲射频控制*/
typedef enum {
	VoltageType = 1,
	PulseWidthType,
	Temperature,
}tControl, * pControl;

typedef enum { DISABLE = 0, ENABLE = !DISABLE } FunctionalState;

typedef struct
{
	u8 volt[2];            /* 电压 */
	float width;           /* 脉宽 */
	u8 temper[2];          /* 温度 */
	u8 freq;               /* 频率 */
	u16 work_time;         /* 工作时间 */
}tPuWorkPara, * pPuWorkPara;

/* 脉冲射频参数设置 */
typedef struct
{
	u8 work_state;     /* 工作状态开始或停止 */
	u8 port_state[2];  /* 两个电极工作开始或停止 */
	u8 work_space;     /* 界面显示状态 */
	u8 set_port[2];    /* 端口连接状态 */
	u8 bip_port12;     /* 12电极双极选择 */

	u8 para_group[3];           /* 参数组 */
	u16 work_time;              /* 设定工作时间 单位：sec*/
	u8 auto_add;                /* 射频自动升温 打开关闭 */

	/* ------脉冲射频-------- */
	u8 volt[2];                    /* 脉冲射频设定电压 */
	u8 ctrl_mode;                  /* 输出控制模式 电压、脉宽还是温度 */
	u8 pu_freq;                    /* 脉冲频率 hz */
	float pu_width;                /* 脉冲脉宽 ms */
	u8 temper[2];                  /* 脉冲控制温度 */

	tPuWorkPara volt_par;          /* 电压模式参数 */
	tPuWorkPara puls_par;          /* 脉宽模式参数 */
	tPuWorkPara temp_par;          /* 温度模式参数 */
	u8 delay_start;                /* 延迟启动时间 */
	u8 StartTimeMode;              /* 射频自动升温 打开关闭 */
	u8  pu_par_private;            /* 参数是否为私有参数显示 */

	u8 param_for_2_ch;             /* 双路参数 */
	u8 ch_selected;                /* 双路参数模式选中的通道 */
	u8 temper_float_on;            /* 温度小数位 */
}tPuRfParam, * pPuRfParam;


/* 电极连接状态 */
typedef enum
{
	Unlink = 0x00,
	Link = 0x01,
	LinkActive = 0x02,
}tPortStatus, *pPortStatus;


/*线笔颜色枚举*/
typedef enum {
	EleColour_No = 0,
	EleColour_Org,   //橙色
	EleColour_Blu,   //蓝色
	EleColour_Pur,   //紫色
	EleColour_Gre,   //绿色
}tEleLineClour, *EleLineClour;


/*SEEG电极选择信息结构体*/
typedef struct {
	u8          _2CElectrode;               /*2C主机电极模式选择--SEEG模式、普通模式*/
	/*第一个端口的连接信息*/
	u8         _2CNormode;                  /*2C主机第一端口电极模式*/
	/*SEEG工作模式下连接信息*/
	u8         status;                               /*是否可以启动射频或刺激*/
	u8         seeg_contactor_num;                   /*触点数量*/
	u8         seeg_ele_mode;                        /* 电极模式A、AB */
	u8         seeg_contactor_mode;                  /*电极触点模式*/
	u8         SeegELMode;                           /*单双极应用*/
	u8         set_port[2];                          /*端口连接状态*/
	u8         seeg_ele_out_mode[2][4];              /*电极输出模式A还是N*/
	u8         seeg_contactor_state[2][4];           /*电极选择状态*/
	u8         seeg_contactor_group[9][4];           /*电极编组状态*/
	u8         seeg_contactor_group_polar[9][4];     /*电极编组状态对应的极性*/
	u8         seeg_contactor_group_delete_group;    /*删除编组*/
	tCom_SeegElecParam    SeegElecParam[2];
	u8         seeg_ele_contactor_all;               /*Seeg电极全选状态*/
}tSeegELModeStat, * pSeegELModeStat;



/* 电极选择相关 */
typedef struct {
	u8         mode;
	u8     set_port[2];
	u8   EleLineColour[2];
	tSeegELModeStat _2CEleInfor;     /*Seeg相关信息*/
}tELModeStat, *pELModeStat;

/*电压幅度挡位*/
typedef enum {
	LowLeveV = 0,
	HighLeveV,
}tStimVoltageGears, *StimVoltageGears;
/*电流幅度挡位*/
typedef enum {
	LowLeveC = 0,
	HighLeveC,
}tStimCurrentGears, *StimCurrentGears;
/* 参数类型枚举 */
typedef enum
{
	DEVICE_VERSON = 0,
	DEVICE_CQM = 1,
	DEVICE_HD_VERSON = 2,
	DEVICE_SEEG = 3,
	DEVICE_ITALY = 4,
	DEVICE_RISHEN = 5,
	DEVICE_FACTORY = 6,

	DEVICE_SAMPLE = 0xFE,
	DEVICE_BOOT_HD_VERSON = 0XFF,
	DEVICE_PASSWORD = 0xFD,     /*工厂密码设置*/
	DEVICE_PASSWORD_ITALY_PRIVATE = 0xFC,     /*意大利密码私有参数设置*/
}SYSTEM_INFO_TYPE;

/* Rf挡位 */
typedef enum {
	LowPower = 1,
	HighPower,
	AutoPower,
}tPowerType, *pPowerType;

/* -------------------------后台RF测试相关内容-------------------- */


/* 电流挡位 */
typedef enum {
	CurrAuto = 0,
	CurrLow,
	Currmiddle,
	Currhigh,
}tCurrPos, *pCurrPos;

/* 射频测试相关设置内容 */
typedef struct
{
	u8     work_state;         /* 工作状态开始或停止 */
	u8     port_state[2];      /* 两个电极工作开始或停止 */
	u8     set_port[2];        /* 端口连接状态 */
	u8     bip_port12;         /* 12电极双极选择 */
	u8     rf_position;        /* rf挡位 */
	u16    pwm_data;           /* pwm数据 */
	u8     high_res;           /* 高电阻 */
	u8     curr_pos;           /* 电流挡位 */
	u8     fan_lev;            /*风扇工作挡位*/
	u8     rf_max_out_limit;   /*最大输出限制*/
	u8     rf_type;            /* 射频测试类型 单极、双极 */
}tRfTestParam, *pRfTestParam;

#define SYS_INFO_DEVICE_MODEL   0
#define SYS_INFO_VERSION        1

#define M2_SOFT_VERSION_LENTH    (14 + 1)     //M2 软件版本号t_m2_version的长度
/*设备型号*/
typedef enum
{
	/*BLA机器型号*/
	DEVICE_VERSON_CN_M1 = 0XFF,
	DEVICE_VERSON_CN_M2 = 0XFE,
	DEVICE_VERSON_CN_D1 = 0XFD,
	DEVICE_VERSON_CN_D2 = 0XFC,
	DEVICE_VERSON_CN_A1 = 0XFB,
	DEVICE_VERSON_CN_A2 = 0XFA,

	DEVICE_VERSON_EN_2A = 0XF9,
	DEVICE_VERSON_EN_2B = 0XF8,
	DEVICE_VERSON_EN_2C = 0XF6,
	DEVICE_VERSON_EN_LG2 = 0XF7,

	/*BLU机器型号*/
	DEVICE_VERSON_CN_2_A = 0XF5,
	DEVICE_VERSON_CN_2_B = 0XF4,
	DEVICE_VERSON_CN_2_C = 0XF3,

	DEVICE_VERSON_EN_LG1 = 0XF2,
	DEVICE_VERSON_EN_LG2_SEEG = 0XF1,
	DEVICE_VERSON_EN_2_C = 0XF0,

}Device_Verson;


typedef struct {
	u8 sys_info_type;
	union {
		u8 device_model;
		t_m2_version version;
	}sys_info_payload;
}t_sys_info, *p_sys_info;

extern char dat[2100];
extern unsigned int  send_len;

unsigned int Bla_Status_Pack(unsigned char *buf,tCom_M0Back status);

void Bla_Status_Unpack(unsigned char *buf,tCom_M0Back *status);
void   Pc_Com_Pack(PC_PROTOCOL_COMM_FLAG flag, unsigned char *buf,unsigned int buf_lenth);
/* 新增：线程安全组包，组包到 out（容量 out_size），返回帧总长，失败返回 0 */
unsigned int Pc_Com_Pack_Buf(PC_PROTOCOL_COMM_FLAG flag, const unsigned char *buf, unsigned int buf_lenth,
                             unsigned char *out, unsigned int out_size);
unsigned long crc32_get(unsigned char *dat, unsigned int len);
void Pc_Com_Sends();
void bla_pc_test_stim_param_pack(u8 *buf,pStimParam stim_param);
void bla_pc_test_rf_param_pack(u8 *buf,pCtRfParam ctrf_param);
void bla_pc_test_page_display_pack(u8 *buf, u8 page_display);
void bla_pc_test_page_display_unpack(u8 *buf, u8* page_display);
void bla_pc_test_stim_param_unpack(u8 *buf, pStimParam stim_param);
void bla_pc_test_rf_param_unpack(u8 *buf,pCtRfParam ctrf_param);
void bla_pc_test_get_status_pack(u8 *buf,p_pc_test_status pc_test_status);
void bla_pc_test_get_status_unpack(u8 *buf, p_pc_test_status pc_test_status);
void bla_pc_test_elec_select_pack(u8 *buf, pELModeStat elect_select_mode);
void bla_pc_test_elec_select_unpack(u8 *buf, pELModeStat elect_select_mode);
void bla_pc_test_set_sys_info_pack(u8 *buf, p_sys_info sys_info);
void bla_pc_test_set_sys_info_unpack(u8 *buf);
void bla_pc_test_rf_test_param_pack(u8 *buf, pRfTestParam pc_test_rf_test_param);
void bla_pc_test_rf_test_param_unpack(u8 *buf, pRfTestParam pc_test_rf_test_param);
void bla_pc_test_frock_setRelay_pack(u8 *buf, u8 frock_relay);
void bla_pc_test_frock_setRelay_unpack(u8 *buf, u8* frock_relay);


void pc_com_device_info_unpack(u8* buf, tCom_Device_Info* device_info);

unsigned int Bla_M0_Set_Pack(char *buf,tCom_M0ParamSetting m0_set);
void Bla_M0_Set_Unpack(char *buf,tCom_M0ParamSetting *m0_set);

unsigned int Bla_Info_Pack(char *buf,tCom_Device_Info device);
void Bla_Info_Unpack(char *buf,tCom_Device_Info *device);

unsigned int Bla_Elec_Choose_Sync_Pack(char *buf,tCom_Elec_Choose_Sync sync);
void Bla_Elec_Choose_Sync_Unpack(char *buf,tCom_Elec_Choose_Sync *sync);

unsigned int Bla_Elec_Stim_Sync_Pack(char *buf,tCom_Stim_Sync stim);
void Bla_Elec_Stim_Sync_Unpack(char *buf,tCom_Stim_Sync *stim);

unsigned int Bla_Elec_Ctrf_Sync_Pack(char *buf,tCom_Ctrf_Sync ctrf);
void Bla_Elec_Ctrf_Sync_Unpack(char *buf,tCom_Ctrf_Sync *ctrf);

unsigned int Bla_Elec_Prf_Sync_Pack(char *buf,tCom_Prf_Sync prf);
void Bla_Elec_Prf_Sync_Unpack(char *buf,tCom_Prf_Sync *prf);

unsigned int Bla_Elec_Patinfo_Sync_Pack(char *buf,tCom_PatInfo_Sync pat);
void Bla_Elec_Patinfo_Sync_Unpack(char *buf,tCom_PatInfo_Sync *pat);

unsigned int Bla_Elec_Testdog_Sync_Pack(char *buf,tCom_Testdog_Sync testdog);
void Bla_Elec_Testdog_Sync_Unpack(char *buf,tCom_Testdog_Sync *testdog);

unsigned int Bla_Elec_Music_Sync_Pack(char *buf,u8 num);
void Bla_Elec_Music_Sync_Unpack(char *buf,u8* num);

void bla_pc_test_set_key_value_pack(u8 *buf, u16 key_value);//测试bla按键
u16 bla_pc_test_set_key_value_unpack(u8 *buf);

void bla_pc_test_pu_rf_param_pack(u8* buf, pPuRfParam purf_param);

#ifdef __cplusplus
}
#endif
#endif
