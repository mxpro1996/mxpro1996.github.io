---
title: "蓝桥杯嵌入式第十四届国赛实现"
date: 2024-05-29T11:07:25+08:00
draft: false
---

# sample.c
```c
// 定义双变量为采样值
float volt_r37,volt_r37_last;
float temp,temp_last;

u16 adc_val;

// 四个界面的参数，带默认值
u8 interface;
//interface 2
u16 FH=2000,FH_set=2000;
float AH=3.0,AH_set=3.0;
u8 TH=30,TH_set=30;
//interface 3
u8 FN,AN,TN;
//interface 4
u8 FP=1,FP_set=1;
float VP=0.9,VP_set=0.9;
u8 TT=6,TT_set=6;

//1.ADC
void ADC_Process()
{
	HAL_ADC_Start(&hadc2);
  adc_val = HAL_ADC_GetValue(&hadc2);
	volt_r37_last = volt_r37;
	volt_r37 = adc_val/4095.0f*3.3f;

	// 单一采集ADC，超限指Flag
	/* 当上一次小于，此处大于，记作超限*/
	// 防止无限递增
	if(volt_r37>AH_set&&volt_r37_last<AH_set)
		AN++;
}

//2.PWM IC
u8 state_tim2;
u16 cnt1_tim2,cnt2_tim2;

u16 freq_PA1_r,freq_PA1,freq_PA1_last;
u8 duty_PA1_R,duty_PA1;
u32 freq;
u8 i;
u16 duty;
u8 j;

// 对PA1的回调采集实现，一般使用PWMI主从通道
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
	// 此处采用单通道，以状态机修改捕获边沿
	if(state_tim2==0)
	{
		__HAL_TIM_SetCounter(&htim2,0);
		TIM2->CCER |= 0x02;
		state_tim2 =1;
	}
	else if(state_tim2==1)
	{
		cnt1_tim2 = __HAL_TIM_GetCounter(&htim2);
		TIM2->CCER &= ~0x02;
		state_tim2 =2;
	} // 第二次上升沿
	else if(state_tim2==2)
	{
		cnt2_tim2 = __HAL_TIM_GetCounter(&htim2);
		state_tim2 =0;
		freq_PA1_last = freq_PA1;
		freq_PA1_r = 1e6/cnt2_tim2;
		duty_PA1_R = cnt1_tim2*100.0f/cnt2_tim2;
		// 结算占空比和频率数值
		
		if(++i<60)
			freq+=freq_PA1_r;
		else if(i==60){
			i=0;
			freq_PA1 = freq/(60-1);
			freq=0;
		}
		if(++j<100)
			duty+=duty_PA1_R;
		else if(j==100){
			j=0;
			duty_PA1 = duty/(100-1);
			duty=0;
		}
		// 双平均滤波
	
		// 超限统计，不包含占空比	
		if(freq_PA1>FH_set&&freq_PA1_last<=FH_set)
	  	FN++;
	}
	HAL_TIM_IC_Start_IT(&htim2,TIM_CHANNEL_2);
}

//3.Temp
__IO u16 temp_tick;
void Temp_Process()
{
	temp_last =temp;
	temp = ds18b20_read();
	// 总计入两个数据，判定超限且递增
	if(temp>TH_set&&temp_last<TH_set)
		TN++;
}
```

# Systick
```c
//4.LCD	基本界面绘制

//record
u16 freq_r[101];
u8 duty_r[101];
u8 duty_Adc[101];
// 基本点采样回放
/*
 * 频率和占空比
 * 以ADC转占空比
*/

// 回放用索引Bool
_Bool flag_Record;
u16 cnt_Record;
u8 cnt_index;

// 按键Time，采样Flag
u16 cnt_KB34;
_Bool flag_K34;
_Bool flag_pwm;
_Bool flag_adc;
_Bool flag_jilu;

u8 led;
u8 led_cnt;
// 记录和回放实现
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */

  /* USER CODE END SysTick_IRQn 0 */
  HAL_IncTick();
  /* USER CODE BEGIN SysTick_IRQn 1 */
	if(flag_K34){
		// 长按2s以上，默认参数
		if(++cnt_KB34>2000){
			cnt_KB34=0;
			key_lock=1;
			flag_K34=0;
			
			interface=0;
			FH_set=FH=2000;
			AH_set=AH=3.0f;
			TH_set=TH=30;
			FP_set=FP=1;
			VP_set=VP=0.9f;
			TT_set=TT=6;
			FN=AN=TN=0;
		}
	}
	// Flag内部累积时间

	if(++temp_tick>750)
	{
		temp_tick = 0;
		Temp_Process();
	}
	// 每750ms刷新一次DS18B20的值
	
	if(flag_Record)
	{
		// 总体记录时间结束
		if(++cnt_Record>1000*TT_set)
		{
			cnt_Record=0;
			flag_Record=0;
			cnt_index=0;
			flag_jilu=1;
		}
		// 每100ms采样Duty,Freq
		if(cnt_Record%100==0)//0.1s
		{
			freq_r[cnt_index]=freq_PA1;
			duty_r[cnt_index]=duty_PA1;
			if(volt_r37>=VP_set)
				duty_Adc[cnt_index]=90.0f/(3.3f-VP_set)*(volt_r37-3.3f)+100.0f;
			else
				duty_Adc[cnt_index]=10;
			cnt_index++;
			// 除10关系
		}
		if(++led_cnt==100)
		{
			led^=0x01;
			led_cnt=0;
		}
	}
	else led&=~0x01;
	// LD1 指示记录是否进行
	
	if(flag_pwm)
	{
		if(++cnt_Record>1000*TT_set)
		{
			cnt_Record=0;
			flag_pwm=0;
			cnt_index=0;
		}
		if(cnt_Record%100==0)//0.1s
		{
			TIM17->ARR=1e6/freq_r[cnt_index]*FP_set-1;
			TIM17->CCR1=duty_r[cnt_index]*(1e6/freq_r[cnt_index]*FP_set)/100.0f;
			cnt_index++;
		}
		if(cnt_Record==0)
		{
			TIM17->CCR1=0;
		}
		if(++led_cnt==100)
		{
			led_cnt=0;
			led^=0x02;
		}
	}
	else led&=~0x02;
	// 回放标志
	
	// ADC回放
	if(flag_adc)
	{
		if(++cnt_Record>1000*TT_set)
		{
			cnt_Record=0;
			flag_adc=0;
			cnt_index=0;
		}
		if(cnt_Record%100==0)//0.1s
		{
			TIM17->ARR=999;
			TIM17->CCR1=duty_Adc[cnt_index]*10.0f;
			cnt_index++;
		}
		if(cnt_Record==0)
		{
			TIM17->CCR1=0;
		}
		if(++led_cnt==100)
		{
			led_cnt=0;
			led^=0x04;
		}
	}
	else
		led&=~0x04;
  /* USER CODE END SysTick_IRQn 1 */
}
//5.LED
void LED_Process()
{
	LED_Contr(led);
	if(freq_PA1>FH_set)
		led|=0x08;
	else
		led&=~0x08;
	
	if(volt_r37>AH_set)
		led|=0x10;
	else
		led&=~0x10;
	
	if(temp>TH_set)
		led|=0x20;
	else 
		led&=~0x20;
	// 超限灯
}

int main(void)
{
    LCD_Init();
    HAL_TIM_IC_Start_IT(&htim2,TIM_CHANNEL_2);
    ds18b20_init_x();
    HAL_TIM_PWM_Start(&htim17,TIM_CHANNEL_1);
    TIM17->CCR1 = 0;//high 0
    // PA1,PA7

    LCD_Clear(Black);
    LCD_SetBackColor(Black);
    LCD_SetTextColor(White);
    while((u8)ds18b20_read()==85);
   // 等待18B20初始化    
    temp = ds18b20_read();

    while (1){
	    ADC_Process();
	    LCD_Process();
	    Key_Process();
	    LED_Process();
    }
  }
```

# key.c
```c
#include "key.h"
#define Key_Port (KB1)|(KB2<<1)|(KB3<<2)|(KB4<<3)|0xf0

u8 Trg;
u8 Cnt;
_Bool key_lock;

// 异或加锁按键
void Scan_Key(void)
{
	u8 Read_Dat = (Key_Port)^0xff;
	if(key_lock==0)
		Trg = Cnt&(Read_Dat^Cnt);
	Cnt = Read_Dat;
}

u8 flag_interface1,flag_interface3;
__IO u32 key_tick;

void Key_Process()
{
	if(uwTick - key_tick<10)	return;
		key_tick = uwTick;
	Scan_Key();
	if(flag_Record) return;
	if(!Cnt)
	{
		key_lock =0;
		cnt_KB34=0;
		flag_K34=0;
	}
	if(Cnt&0x0c)
	{
		flag_K34=1;
	}
	
	if(Trg&0x01)
	{
		if(++interface==4)
			interface=0;
		if(interface==2)
		{
			FH_set = FH;
			AH_set = AH;
			TH_set = TH;
		}
		else if(interface==0)
		{
			FP_set = FP;
			VP_set = VP;
			TT_set = TT;
		}
		else if(interface==1)
		{
			flag_interface1=0;
		}
		else if(interface==3)
		{
			flag_interface3=0;
		}
	}
	else if(Trg&0x02)
	{
		if(interface==0)//Record
		{
			flag_Record=1;
			led_cnt=0;
		}
		else if(interface==1)
		{
			if(++flag_interface1==3)
				flag_interface1=0;
		}
		else if(interface==2)
		{
			FN=AN=TN=0;
		}
		else if(interface==3)
		{
			if(++flag_interface3==3)
				flag_interface3=0;
		}
	}
	else if(Trg&0x04)
	{
		if(interface==1)
		{
			if(flag_interface1==0)
			{
				if(FH<10000)
					FH+=1000;
			}
			else if(flag_interface1==1)
			{
				if(AH<3.2f)
					AH+=0.3f;
			}
			else if(flag_interface1==2)
			{
				if(TH<80)
					TH+=1;
			}
		}
		else if(interface==3)
		{
			if(flag_interface3==0)
			{
				if(FP<10)
					FP+=1;
			}
			else if(flag_interface3==1)
			{
				if(VP<3.2f)
					VP+=0.3f;
			}
			else if(flag_interface3==2)
			{
				if(TT<10)
					TT+=2;
			}
		}
		else if(interface==0)
		{
			if(!flag_pwm&&flag_jilu)
			{
				flag_adc=1;
				led_cnt=0;
			}
		}
	}
	else if(Trg&0x08)
	{
		if(interface==1)
		{
			if(flag_interface1==0)
			{
				if(FH>1000)
					FH-=1000;
			}
			else if(flag_interface1==1)
			{
				if(AH>0.2f)
					AH-=0.3f;
			}
			else if(flag_interface1==2)
			{
				if(TH>0)
					TH-=1;
			}
		}
		else if(interface==3)
		{
			if(flag_interface3==0)
			{
				if(FP>1)
					FP-=1;
			}
			else if(flag_interface3==1)
			{
				if(VP>0.5f)
					VP-=0.3f;
				else 
					VP=0;
			}
			else if(flag_interface3==2)
			{
				if(TT>2)
					TT-=2;
			}
		}
		else if(interface==0)
		{
			if(!flag_adc&&flag_jilu)
			{
				flag_pwm=1;
				led_cnt=0;
			}
		}
	}
}


```
