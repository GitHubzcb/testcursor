#include "pc_m2_protocol.h"
#include "float.h"

/*==============================================================================
 修改说明（对照原版）：
 1. crc32_get：原版直接 return 0，线路误码完全无法发现。
    现在由 PC_M2_ENABLE_CRC32 开关控制：开=真实 CRC32，关=兼容旧固件填 0。
 2. 新增 Pc_Com_Pack_Buf()：组包到调用方缓冲区并返回帧长（线程安全）；
    原 Pc_Com_Pack 保留并转调它，兼容旧调用点（仍写全局 dat/send_len，
    该旧接口本身非线程安全，新代码请勿再使用）。
 3. bla_pc_test_elec_select_unpack：修复 memcpy 长度误用 sizeof(pELModeStat)
    （指针大小 4/8 字节）的问题，应为 sizeof(tELModeStat)。
 4. 其余 pack/unpack 函数逻辑保持与原版一致。
==============================================================================*/

char dat[2100];
unsigned int  send_len = 0;

/*******************************************************************************
* 函数名称：crc32_get
* 功能描述：获取字节流的 CRC32 校验码
* 注意事项：PC_M2_ENABLE_CRC32=1 时必须与 MCU 固件同步启用！
******************************************************************************/
#if PC_M2_ENABLE_CRC32
/* 标准 CRC32 (poly 0xEDB88320)，查表法，首次调用时建表 */
static unsigned long s_crc32_table[256];
static int s_crc32_table_ready = 0;
static void crc32_make_table(void)
{
	unsigned long c;
	int n, k;
	for (n = 0; n < 256; n++)
	{
		c = (unsigned long)n;
		for (k = 0; k < 8; k++)
		{
			c = (c & 1) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
		}
		s_crc32_table[n] = c;
	}
	s_crc32_table_ready = 1;
}
#endif

unsigned long crc32_get(unsigned char *dat, unsigned int len)
{
#if PC_M2_ENABLE_CRC32
	unsigned long crc = 0xFFFFFFFFUL;
	unsigned int i;
	if (!s_crc32_table_ready)
	{
		crc32_make_table();
	}
	for (i = 0; i < len; i++)
	{
		crc = s_crc32_table[(crc ^ dat[i]) & 0xFF] ^ (crc >> 8);
	}
	return crc ^ 0xFFFFFFFFUL;
#else
	/* 兼容模式：与现有固件保持一致（CRC 字段填 0，接收不校验） */
	(void)dat;
	(void)len;
	return 0;
#endif
}

/*******************************************************************************
* 函数名称：Pc_Com_Pack_Buf（新增）
* 功能描述：PC 通信打包，组包到调用方缓冲区（线程安全）
* 输入参数：flag 标识码；buf/buf_lenth 指令数据；out/out_size 输出缓冲
* 返回参数：帧总长；0 = 缓冲区不足或参数错误
******************************************************************************/
unsigned int Pc_Com_Pack_Buf(PC_PROTOCOL_COMM_FLAG flag, const unsigned char *buf, unsigned int buf_lenth,
                             unsigned char *out, unsigned int out_size)
{
	unsigned int i;
	unsigned int base;
	unsigned long crc32;

	if (out == NULL || out_size < buf_lenth + 11)
	{
		return 0;
	}
	if (buf_lenth > 0 && buf == NULL)
	{
		return 0;
	}

	out[0] = 0xAA;
	out[1] = 0x55;
	out[2] = (unsigned char)flag;

	/*将命令数据拷贝到发送缓冲*/
	for (i = 0; i < buf_lenth; i++)
	{
		out[5 + i] = buf[i];
	}
	/*填充指令数据长度*/
	out[3] = (unsigned char)(buf_lenth);
	out[4] = (unsigned char)(buf_lenth >> 8);

	base = buf_lenth + 5;
	/*计算 CRC32 并填充*/
	crc32 = crc32_get(&(out[0]), base);
	out[base++] = (unsigned char)(crc32);
	out[base++] = (unsigned char)(crc32 >> 8);
	out[base++] = (unsigned char)(crc32 >> 16);
	out[base++] = (unsigned char)(crc32 >> 24);
	/*填充协议尾*/
	out[base++] = 0xCC;
	out[base++] = 0x33;
	return base;
}

/*******************************************************************************
* 函数名称：Pc_Com_Pack（兼容保留）
* 功能描述：组包到全局 dat/send_len。
* 注意事项：该接口非线程安全（全局缓冲会被并发调用互相覆盖），
*           仅为兼容旧调用点保留，新代码请使用 Pc_Com_Pack_Buf。
******************************************************************************/
void   Pc_Com_Pack(PC_PROTOCOL_COMM_FLAG flag, unsigned char *buf, unsigned int buf_lenth)
{
	send_len = Pc_Com_Pack_Buf(flag, buf, buf_lenth, (unsigned char *)dat, sizeof(dat));
}

void bla_pc_test_get_status_pack(u8 *buf, p_pc_test_status pc_test_status)
{
	memcpy(buf, pc_test_status, sizeof(t_pc_test_status));
}

void bla_pc_test_get_status_unpack(u8 *buf, p_pc_test_status pc_test_status)
{
	memcpy(pc_test_status, buf, sizeof(t_pc_test_status));
}

void bla_pc_test_page_display_pack(u8 *buf, u8 page_display)
{
	buf[0] = page_display;
}

void bla_pc_test_page_display_unpack(u8 *buf, u8* page_display)
{
	memcpy(page_display, buf, 1);
}

void bla_pc_test_elec_select_pack(u8 *buf, pELModeStat elect_select_mode)
{
	memcpy(buf, elect_select_mode, sizeof(tELModeStat));
}

void bla_pc_test_elec_select_unpack(u8 *buf, pELModeStat elect_select_mode)
{
	tELModeStat elect_select_mode_buf;
	/* 修复：原版为 sizeof(pELModeStat)，即指针大小（4/8 字节），
	   导致结构体绝大部分内容没有被拷贝 */
	memcpy(&elect_select_mode_buf, buf, sizeof(tELModeStat));
	if (elect_select_mode_buf.mode != 0xFF)
	{
		elect_select_mode->mode = elect_select_mode_buf.mode;
		printf("elect_select_mode->mode = %d\r\n", elect_select_mode->mode);
	}
	if (elect_select_mode_buf.set_port[0] != 0xFF)
	{
		elect_select_mode->set_port[0] = elect_select_mode_buf.set_port[0];
		printf("elect_select_mode->set_port[0] = %d\r\n", elect_select_mode->set_port[0]);
	}
	if (elect_select_mode_buf.set_port[1] != 0xFF)
	{
		elect_select_mode->set_port[1] = elect_select_mode_buf.set_port[1];
		printf("elect_select_mode->set_port[1] = %d\r\n", elect_select_mode->set_port[1]);
	}
}

void bla_pc_test_stim_param_pack(u8 *buf, pStimParam stim_param)
{
	memcpy(buf, stim_param, sizeof(tStimParam));
}

void bla_pc_test_stim_param_unpack(u8 *buf, pStimParam stim_param)
{
	tStimParam stim_param_buf;
	memcpy(&stim_param_buf, buf, sizeof(tStimParam));
	if (stim_param_buf.work_state != 0xFF)     /* 工作状态开始或停止 */
	{
		stim_param->work_state = stim_param_buf.work_state;
		printf("stim_param->work_state = %d\r\n", stim_param->work_state);
	}
	if (stim_param_buf.work_space != 0xFF) /* 界面显示状态 */
	{
		stim_param->work_space = stim_param_buf.work_space;
		printf("stim_param->work_space = %d\r\n", stim_param->work_space);
	}
	if (stim_param_buf.set_port[0] != 0xFF)    /* 端口连接状态 */
	{
		stim_param->set_port[0] = stim_param_buf.set_port[0];
		printf("stim_param->set_port[0] = %d\r\n", stim_param->set_port[0]);
	}
	if (stim_param_buf.set_port[1] != 0xFF)    /* 端口连接状态 */
	{
		stim_param->set_port[1] = stim_param_buf.set_port[1];
		printf("stim_param->set_port[1] = %d\r\n", stim_param->set_port[1]);
	}

	if (stim_param_buf.stim_workmode != 0xFF) /* 刺激模式  运动刺激或感觉刺激*/
	{
		stim_param->stim_workmode = stim_param_buf.stim_workmode;
		printf("stim_param->stim_workmode = %d\r\n", stim_param->stim_workmode);
	}
	if (stim_param_buf.bip_port12 != 0xFF) /* 12电极双极选择 */
	{
		stim_param->bip_port12 = stim_param_buf.bip_port12;
		printf("stim_param->bip_port12 = %d\r\n", stim_param->bip_port12);
	}

	if (stim_param_buf.stim_group[0] != 0xFF)              /* 刺激参数组 */
	{
		stim_param->stim_group[0] = stim_param_buf.stim_group[0];
		printf("stim_param->stim_group[0] = %d\r\n", stim_param->stim_group[0]);
	}
	if (stim_param_buf.stim_group[1] != 0xFF)              /* 刺激参数组 */
	{
		stim_param->stim_group[1] = stim_param_buf.stim_group[1];
		printf("stim_param->stim_group[1] = %d\r\n", stim_param->stim_group[1]);
	}

	if (stim_param_buf.stim_type != 0xFF)     /* 刺激类型 电压刺激或电流刺激 */
	{
		stim_param->stim_type = stim_param_buf.stim_type;
		printf("stim_param->stim_type = %d\r\n", stim_param->stim_type);
	}
	if (!_isnan(stim_param_buf.voltage))                /* 刺激电压 v */
	{
		stim_param->voltage = stim_param_buf.voltage;
		printf("stim_param->voltage = %f\r\n", stim_param->voltage);
	}
	if (!_isnan(stim_param_buf.current))          /* 刺激电流 mA*/
	{
		stim_param->current = stim_param_buf.current;
		printf("stim_param->current = %f\r\n", stim_param->current);
	}
	if (stim_param_buf.motor_freq != 0xFF)              /* 运动频率 hz */
	{
		stim_param->motor_freq = stim_param_buf.motor_freq;
		printf("stim_param->motor_freq = %d\r\n", stim_param->motor_freq);
	}
	if (stim_param_buf.sens_freq != 0xFF)               /* 感觉频率 hz */
	{
		stim_param->sens_freq = stim_param_buf.sens_freq;
		printf("stim_param->sens_freq = %d\r\n", stim_param->sens_freq);
	}
	if (!_isnan(stim_param_buf.pulse_width))          /* 刺激脉冲宽度 ms */
	{
		stim_param->pulse_width = stim_param_buf.pulse_width;
		printf("stim_param->pulse_width = %f\r\n", stim_param->pulse_width);
	}
	if (!_isnan(stim_param_buf.motor_volt))           /* 运动电压电流输出 */
	{
		stim_param->motor_volt = stim_param_buf.motor_volt;
		printf("stim_param->motor_volt = %f\r\n", stim_param->motor_volt);
	}
	if (!_isnan(stim_param_buf.motor_curr))
	{
		stim_param->motor_curr = stim_param_buf.motor_curr;
		printf("stim_param->motor_curr = %f\r\n", stim_param->motor_curr);
	}
	if (!_isnan(stim_param_buf.sense_volt))           /* 感觉电压电流输出 */
	{
		stim_param->sense_volt = stim_param_buf.sense_volt;
		printf("stim_param->sense_volt = %f\r\n", stim_param->sense_volt);
	}
	if (!_isnan(stim_param_buf.sense_curr))
	{
		stim_param->sense_curr = stim_param_buf.sense_curr;
		printf("stim_param->sense_curr = %f\r\n", stim_param->sense_curr);
	}
	if (stim_param_buf.clr_range != 0xFF)        /* 清除量程 */
	{
		stim_param->clr_range = stim_param_buf.clr_range;
		printf("stim_param->clr_range = %d\r\n", stim_param->clr_range);
	}
	if (stim_param_buf.ramp_out != 0xFF)         /* 缓慢输出 */
	{
		stim_param->ramp_out = stim_param_buf.ramp_out;
		printf("stim_param->ramp_out = %d\r\n", stim_param->ramp_out);
	}
	if (stim_param_buf.VoltageGears != 0xFF)     /*电压幅度挡位*/
	{
		stim_param->VoltageGears = stim_param_buf.VoltageGears;
		printf("stim_param->VoltageGears = %d\r\n", stim_param->VoltageGears);
	}
	if (stim_param_buf.CurrentGears != 0xFF)     /*电流幅度挡位*/
	{
		stim_param->CurrentGears = stim_param_buf.CurrentGears;
		printf("stim_param->CurrentGears = %d\r\n", stim_param->CurrentGears);
	}
	if (stim_param_buf.SimOutPolar != 0xFF)           /*刺激输出极性*/
	{
		stim_param->SimOutPolar = stim_param_buf.SimOutPolar;
		printf("stim_param->SimOutPolar = %d\r\n", stim_param->SimOutPolar);
	}
	if (stim_param_buf.stim_setp_value != 0xFF)    /*刺激步进值*/
	{
		stim_param->stim_setp_value = stim_param_buf.stim_setp_value;
		printf("stim_param->stim_setp_value = %d\r\n", stim_param->stim_setp_value);
	}
}

void bla_pc_test_rf_param_pack(u8 *buf, pCtRfParam ctrf_param)
{
	memcpy(buf, ctrf_param, sizeof(tCtRfParam));
}

void bla_pc_test_rf_param_unpack(u8 *buf, pCtRfParam ctrf_param)
{
	tCtRfParam ctrf_param_buf;
	memcpy(&ctrf_param_buf, buf, sizeof(tCtRfParam));
	if (ctrf_param_buf.work_state != 0xFF)     /* 工作状态开始或停止 */
	{
		ctrf_param->work_state = ctrf_param_buf.work_state;
		printf("ctrf_param->work_state = %d\r\n", ctrf_param->work_state);
	}
	if (ctrf_param_buf.port_state[0] != 0xFF)  /* 两个电极工作开始或停止 */
	{
		ctrf_param->port_state[0] = ctrf_param_buf.port_state[0];
		printf("ctrf_param->port_state[0] = %d\r\n", ctrf_param->port_state[0]);
	}

	if (ctrf_param_buf.port_state[1] != 0xFF)  /* 两个电极工作开始或停止 */
	{
		ctrf_param->port_state[1] = ctrf_param_buf.port_state[1];
		printf("ctrf_param->port_state[1] = %d\r\n", ctrf_param->port_state[1]);
	}
	if (ctrf_param_buf.work_space != 0xFF)    /* 界面显示状态 */
	{
		ctrf_param->work_space = ctrf_param_buf.work_space;
		printf("ctrf_param->work_space = %d\r\n", ctrf_param->work_space);
	}
	if (ctrf_param_buf.set_port[0] != 0xFF)    /* 端口连接状态 */
	{
		ctrf_param->set_port[0] = ctrf_param_buf.set_port[0];
		printf("ctrf_param->set_port[0] = %d\r\n", ctrf_param->set_port[0]);
	}
	if (ctrf_param_buf.set_port[1] != 0xFF)    /* 端口连接状态 */
	{
		ctrf_param->set_port[1] = ctrf_param_buf.set_port[1];
		printf("ctrf_param->set_port[1] = %d\r\n", ctrf_param->set_port[1]);
	}
	if (ctrf_param_buf.bip_port12 != 0xFF) /* 12电极双极选择 */
	{
		ctrf_param->bip_port12 = ctrf_param_buf.bip_port12;
		printf("ctrf_param->bip_port12 = %d\r\n", ctrf_param->bip_port12);
	}

	for (int i = 0; i < 5; i++)
	{
		if (ctrf_param_buf.para_group[i] != 0xFF)           /* 参数组 */
		{
			ctrf_param->para_group[i] = ctrf_param_buf.para_group[i];
			printf("ctrf_param->para_group[%d] = %d\r\n", i, ctrf_param->para_group[i]);
		}
	}
	if (ctrf_param_buf.work_time != 0xFFFF)              /* 设定工作时间 单位：sec*/
	{
		ctrf_param->work_time = ctrf_param_buf.work_time;
		printf("ctrf_param->work_time = %d\r\n", ctrf_param->work_time);
	}
	if (ctrf_param_buf.auto_add != 0xFF)       /* 射频自动升温 打开关闭 */
	{
		ctrf_param->auto_add = ctrf_param_buf.auto_add;
		printf("ctrf_param->auto_add = %d\r\n", ctrf_param->auto_add);
	}
	if (ctrf_param_buf.StartTimeMode != 0xFF)  /* 射频计时状态 开始时计或射频温度到计时 */
	{
		ctrf_param->StartTimeMode = ctrf_param_buf.StartTimeMode;
		printf("ctrf_param->StartTimeMode = %d\r\n", ctrf_param->StartTimeMode);
	}
	/* ------连续射频-------- */
	if (ctrf_param_buf.temper != 0xFF)                  /* 连续射频控制温度 */
	{
		ctrf_param->temper = ctrf_param_buf.temper;
		printf("ctrf_param->temper = %d\r\n", ctrf_param->temper);
	}
	if (ctrf_param_buf.ct_mode != 0xFF)      /* 连续射频控温类型  stand / step*/
	{
		ctrf_param->ct_mode = ctrf_param_buf.ct_mode;
		printf("ctrf_param->ct_mode = %d\r\n", ctrf_param->ct_mode);
	}
	if (ctrf_param_buf.max_power_temp != 0xFF)          /* 控温设备最大功率 */
	{
		ctrf_param->max_power_temp = ctrf_param_buf.max_power_temp;
		printf("ctrf_param->max_power_temp = %d\r\n", ctrf_param->max_power_temp);
	}
	if (ctrf_param_buf.temp_work_time != 0xFFFF)         /*  温度模式工作时间 */
	{
		ctrf_param->temp_work_time = ctrf_param_buf.temp_work_time;
		printf("ctrf_param->temp_work_time = %d\r\n", ctrf_param->temp_work_time);
	}
	/* 阶跃模式 */
	if (ctrf_param_buf.step_ctr_temp != 0xFF)             /*阶跃模式下的温度控制*/
	{
		ctrf_param->step_ctr_temp = ctrf_param_buf.step_ctr_temp;
		printf("ctrf_param->step_ctr_temp = %d\r\n", ctrf_param->step_ctr_temp);
	}
	if (ctrf_param_buf.start_temp != 0xFF)              /* STEP设置项 开始温度 */
	{
		ctrf_param->start_temp = ctrf_param_buf.start_temp;
		printf("ctrf_param->start_temp = %d\r\n", ctrf_param->start_temp);
	}
	if (ctrf_param_buf.step_temp != 0xFF)               /* 温度步进 */
	{
		ctrf_param->step_temp = ctrf_param_buf.step_temp;
		printf("ctrf_param->step_temp = %d\r\n", ctrf_param->step_temp);
	}
	if (ctrf_param_buf.step_time != 0xFFFF)              /* STEP 时间 sec */
	{
		ctrf_param->step_time = ctrf_param_buf.step_time;
		printf("ctrf_param->step_time = %d\r\n", ctrf_param->step_time);
	}
	if (ctrf_param_buf.final_temp != 0xFF)              /* 最终温度 */
	{
		ctrf_param->final_temp = ctrf_param_buf.final_temp;
		printf("ctrf_param->final_temp = %d\r\n", ctrf_param->final_temp);
	}
	if (ctrf_param_buf.delay_start != 0xFF)             /* 延迟启动时间 */
	{
		ctrf_param->delay_start = ctrf_param_buf.delay_start;
		printf("ctrf_param->delay_start = %d\r\n", ctrf_param->delay_start);
	}
	if (ctrf_param_buf.max_power_step != 0xFF)          /* 最大功率 */
	{
		ctrf_param->max_power_step = ctrf_param_buf.max_power_step;
		printf("ctrf_param->max_power_step = %d\r\n", ctrf_param->max_power_step);
	}
	if (ctrf_param_buf.step_work_time != 0xFFFF)         /*  阶跃模式工作时间 */
	{
		ctrf_param->step_work_time = ctrf_param_buf.step_work_time;
		printf("ctrf_param->step_work_time = %d\r\n", ctrf_param->step_work_time);
	}
	/* 单入双出下的功率模式 */
	if (!_isnan(ctrf_param_buf.setpower))                    /* 设定功率 */
	{
		ctrf_param->setpower = ctrf_param_buf.setpower;
		printf("ctrf_param->setpower = %f\r\n", ctrf_param->setpower);
	}
	if (ctrf_param_buf.up_time != 0xFF)                         /* 上升时间 */
	{
		ctrf_param->up_time = ctrf_param_buf.up_time;
		printf("ctrf_param->up_time = %d\r\n", ctrf_param->up_time);
	}
	if (ctrf_param_buf.max_temp != 0xFF)                        /* 最大温度 */
	{
		ctrf_param->max_temp = ctrf_param_buf.max_temp;
		printf("ctrf_param->max_temp = %d\r\n", ctrf_param->max_temp);
	}
	if (ctrf_param_buf.power_work_time != 0xFFFF)                /*  功率模式工作时间 */
	{
		ctrf_param->power_work_time = ctrf_param_buf.power_work_time;
		printf("ctrf_param->power_work_time = %d\r\n", ctrf_param->power_work_time);
	}
	if (ctrf_param_buf.PowerImpedRange != 0xFF)                 /*单入双出下功率模式下阻抗曲线的阻抗范围*/
	{
		ctrf_param->PowerImpedRange = ctrf_param_buf.PowerImpedRange;
		printf("ctrf_param->PowerImpedRange = %d\r\n", ctrf_param->PowerImpedRange);
	}
	if (ctrf_param_buf.ImpedMarkModeBip != 0xFF)      /*阻抗监测*/
	{
		ctrf_param->ImpedMarkModeBip = ctrf_param_buf.ImpedMarkModeBip;
		printf("ctrf_param->ImpedMarkModeBip = %d\r\n", ctrf_param->ImpedMarkModeBip);
	}
	if (ctrf_param_buf.limit_imped != 0xFFFF)                    /*阻抗限定--ON之后的阻抗变化*/
	{
		ctrf_param->limit_imped = ctrf_param_buf.limit_imped;
		printf("ctrf_param->limit_imped = %d\r\n", ctrf_param->limit_imped);
	}
	if (ctrf_param_buf.seeg_auto_add != 0xFF)       /* 射频自动升温 打开关闭 */
	{
		ctrf_param->seeg_auto_add = ctrf_param_buf.seeg_auto_add;
		printf("ctrf_param->seeg_auto_add = %d\r\n", ctrf_param->seeg_auto_add);
	}

	/* 除去单入双出下的功率模式 */
	if (!_isnan(ctrf_param_buf.SetPowerNorm))             /* 设定功率 */
	{
		ctrf_param->SetPowerNorm = ctrf_param_buf.SetPowerNorm;
		printf("ctrf_param->SetPowerNorm = %f\r\n", ctrf_param->SetPowerNorm);
	}
	if (ctrf_param_buf.Up_TimeNorm != 0xFF)                 /* 上升时间 */
	{
		ctrf_param->Up_TimeNorm = ctrf_param_buf.Up_TimeNorm;
		printf("ctrf_param->Up_TimeNorm = %d\r\n", ctrf_param->Up_TimeNorm);
	}
	if (ctrf_param_buf.Max_TempNorm != 0xFF)                /* 最大温度 */
	{
		ctrf_param->Max_TempNorm = ctrf_param_buf.Max_TempNorm;
		printf("ctrf_param->Max_TempNorm = %d\r\n", ctrf_param->Max_TempNorm);
	}
	if (ctrf_param_buf.PowerNorm_work_time != 0xFFFF)        /*  功率模式工作时间 */
	{
		ctrf_param->PowerNorm_work_time = ctrf_param_buf.PowerNorm_work_time;
		printf("ctrf_param->PowerNorm_work_time = %d\r\n", ctrf_param->PowerNorm_work_time);
	}
	/* 阻抗模式 */
	if (ctrf_param_buf.SetVoltage != 0xFF)                   /* 设定值，百分比 */
	{
		ctrf_param->SetVoltage = ctrf_param_buf.SetVoltage;
		printf("ctrf_param->SetVoltage = %d\r\n", ctrf_param->SetVoltage);
	}
	if (ctrf_param_buf.up_time_Voltage != 0xFF)
	{
		ctrf_param->up_time_Voltage = ctrf_param_buf.up_time_Voltage;
		printf("ctrf_param->up_time_Voltage = %d\r\n", ctrf_param->up_time_Voltage);
	}
	if (ctrf_param_buf.work_time_Voltage != 0xFFFF)
	{
		ctrf_param->work_time_Voltage = ctrf_param_buf.work_time_Voltage;
		printf("ctrf_param->work_time_Voltage = %d\r\n", ctrf_param->work_time_Voltage);
	}
	if (ctrf_param_buf.max_power_Voltage != 0xFF)
	{
		ctrf_param->max_power_Voltage = ctrf_param_buf.max_power_Voltage;
		printf("ctrf_param->max_power_Voltage = %d\r\n", ctrf_param->max_power_Voltage);
	}
	if (ctrf_param_buf.VoltageImpedRange != 0xFF)  /*单入双出下功率模式下阻抗曲线的阻抗范围*/
	{
		ctrf_param->VoltageImpedRange = ctrf_param_buf.VoltageImpedRange;
		printf("ctrf_param->VoltageImpedRange = %d\r\n", ctrf_param->VoltageImpedRange);
	}
	if (ctrf_param_buf.ImpedMarkModeImped != 0xFF)    /*阻抗监测*/
	{
		ctrf_param->ImpedMarkModeImped = ctrf_param_buf.ImpedMarkModeImped;
		printf("ctrf_param->ImpedMarkModeImped = %d\r\n", ctrf_param->ImpedMarkModeImped);
	}
	if (ctrf_param_buf.imped_before != 0xFFFF)
	{
		ctrf_param->imped_before = ctrf_param_buf.imped_before;
		printf("ctrf_param->imped_before = %d\r\n", ctrf_param->imped_before);
	}
}

void bla_pc_test_set_sys_info_pack(u8 *buf, p_sys_info sys_info)
{
	memcpy(buf, sys_info, sizeof(t_sys_info));
}

void bla_pc_test_set_sys_info_unpack(u8 *buf)
{
    //u8 device_version = 0;
    //memcpy(&device_version, buf, 1);
    //if(device_version>=0xF0)
    //{
    //    printf("device_version = %x\r\n",device_version);
    //    set_system_info(DEVICE_VERSON,&device_version);
    //}
}

void bla_pc_test_rf_test_param_pack(u8 *buf, pRfTestParam pc_test_rf_test_param)
{
    memcpy(buf, pc_test_rf_test_param, sizeof(tRfTestParam));
}

void bla_pc_test_rf_test_param_unpack(u8 *buf, pRfTestParam pc_test_rf_test_param)
{
    tRfTestParam pc_test_rf_test_param_buf;
    memcpy(&pc_test_rf_test_param_buf , buf, sizeof(tRfTestParam));
    if(pc_test_rf_test_param_buf.rf_position != 0xFF)     /* 功率挡位 */
    {
        pc_test_rf_test_param->rf_position = pc_test_rf_test_param_buf.rf_position;
        printf("pc_test_rf_test_param->rf_position = %d\r\n",pc_test_rf_test_param->rf_position);
    }
    if(pc_test_rf_test_param_buf.pwm_data != 0xFF)     /* PWM挡位数 */
    {
        pc_test_rf_test_param->pwm_data= pc_test_rf_test_param_buf.pwm_data;
        printf("pc_test_rf_test_param->pwm_data = %d\r\n",pc_test_rf_test_param->pwm_data);
    }
    if(pc_test_rf_test_param_buf.curr_pos != 0xFF)     /* 电流挡位 */
    {
        pc_test_rf_test_param->curr_pos = pc_test_rf_test_param_buf.curr_pos;
        printf("pc_test_rf_test_param->curr_pos = %d\r\n",pc_test_rf_test_param->curr_pos);
    }
    if(pc_test_rf_test_param_buf.rf_type != 0xFF)     /* 单极双极 */
    {
        pc_test_rf_test_param->rf_type = pc_test_rf_test_param_buf.rf_type;
        printf("pc_test_rf_test_param->rf_type = %d\r\n",pc_test_rf_test_param->rf_type);
    }
    if(pc_test_rf_test_param_buf.fan_lev != 0xFF)     /* 风扇控制 */
    {
        pc_test_rf_test_param->fan_lev = pc_test_rf_test_param_buf.fan_lev;
        printf("pc_test_rf_test_param->fan_lev = %d\r\n",pc_test_rf_test_param->fan_lev);
    }
    if(pc_test_rf_test_param_buf.rf_max_out_limit != 0xFF)     /* 最大输出限制 */
    {
        pc_test_rf_test_param->rf_max_out_limit = pc_test_rf_test_param_buf.rf_max_out_limit;
        printf("pc_test_rf_test_param->rf_max_out_limit = %d\r\n",pc_test_rf_test_param->rf_max_out_limit);
    }
}

void bla_pc_test_frock_setRelay_pack(u8 *buf, u8 frock_relay)
{
	buf[0] = frock_relay;
}
void bla_pc_test_frock_setRelay_unpack(u8 *buf, u8* frock_relay)
{
	memcpy(frock_relay, buf, 1);
}

void bla_pc_test_set_key_value_pack(u8 *buf, u16 key_value)
{
	buf[0] = key_value & ~0xff00;
	buf[1] = key_value >> 8;
}

u16 bla_pc_test_set_key_value_unpack(u8 *buf)
{
	u16 key_value = 0;
	key_value = buf[0] + (buf[1] << 8);
	return key_value;
}

void bla_pc_test_pu_rf_param_pack(u8* buf, pPuRfParam purf_param)
{
	memcpy(buf, purf_param, sizeof(tPuRfParam));
}


//0819
void pc_com_device_info_unpack(u8* buf, tCom_Device_Info* device_info)
{
	memcpy(device_info->device_name, buf, 30);
	memcpy(device_info->device_sn, buf + 30, 30);
	memcpy(device_info->soft_verson, buf + 60, 30);
	memcpy(device_info->hardware_verson, buf + 90, 30);
	memcpy(device_info->picture_verson, buf + 120, 30);

	// 确保字符串终止
	device_info->device_name[29] = '\0';
	device_info->device_sn[29] = '\0';
	device_info->soft_verson[29] = '\0';
	device_info->hardware_verson[29] = '\0';
	device_info->picture_verson[29] = '\0';
}
