#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/io.h>
#include <linux/clk.h>

static void ts_tasklet_function(unsigned int data);
/*定义一个tasklet_struct, 以实现延迟操作*/
static DECLARE_TASKLET(ts_task,ts_tasklet_function,0);

struct adc_regs{
	unsigned long adccon;
	unsigned long adctsc;
	unsigned long adcdly;
	unsigned long adcdat0;
	unsigned long adcdat1;
	unsigned long adcupdn;
};

static volatile struct adc_regs *adc_reg = NULL;
static struct input_dev *ts_input = NULL;
static struct clk *adc_clock = NULL;
static struct timer_list adc_timer;

static void ADCStart(void)
{
	adc_reg->adccon |= (1 <<0);
}

/* 等待触摸屏按下 */
static void TsWaitTouch(void)
{
	adc_reg->adctsc = (0 <<8) | (1 << 7) |(1<<6) | (1<< 4) | (0 << 3) | 0x3;
}

/*  等待触摸屏松开*/
static void TsWaitRelaease(void)
{
	adc_reg->adctsc = (1<< 8) | (1 << 7) | (1<<6) | (1<< 4)  | (0 << 3) | 0x3;
}

static void EnterXYPosition(void)
{
	adc_reg->adctsc = (1<<3) | (1 << 2)  ;
}

/* 检测是否按下或者松开 
  *1:松开	0:按下*/
static char CheckPenState(void) 
{
	char dat0 = 0;
	char dat1 = 0;

	dat0 = adc_reg->adcdat0 >>15;
	dat1 = adc_reg->adcdat1 >>15;

	return (dat0 & dat1);
}

/* 进行触摸笔的连续滑动操作 */
static void adc_timer_fun(unsigned long data)
{
	if(CheckPenState())
	{
		/*  上报输入事件*/
		input_event(ts_input,EV_KEY,BTN_TOUCH,0);
		input_event(ts_input,EV_ABS,ABS_PRESSURE,0);
		input_sync(ts_input);
	}
	else
	{
		/* 进行连续的转换 */
		EnterXYPosition();
		ADCStart();
	}
}

/* 触摸屏的中断处理函数 */
static irqreturn_t PenDownUpIRQ(int irq, void *devid)
{	
	if(CheckPenState())
	{
		/*  上报输入事件*/
		input_event(ts_input,EV_KEY,BTN_TOUCH,0);
		input_event(ts_input,EV_ABS,ABS_PRESSURE,0);
		input_sync(ts_input);
		
		TsWaitTouch();
	}
	else
	{	
		mod_timer(&adc_timer,jiffies+msecs_to_jiffies(10));
	}
	
	return IRQ_HANDLED;
}

static irqreturn_t ADC_ConverIRQ(int irq, void *devid)
{
	tasklet_schedule(&ts_task);
	
	return IRQ_HANDLED;
}

/* 推迟的中断处理函数 */
static void ts_tasklet_function(unsigned int data)
{
	static int x_data[4] = {0};
	static int y_data[4] = {0};
	static int cnt = 0;
	int avr_x = 0;
	int avr_y =0;

	/* 判断本次数据是否有效 */
	if(!CheckPenState())
	{
		/* 多次测量求平均值 */
		if(cnt != 4)
		{	
			x_data[cnt] = adc_reg->adcdat0 & 0x3ff;
			y_data[cnt] = adc_reg->adcdat1 & 0x3ff;

			cnt++;
		}
		else
		{	
			avr_x = (x_data[0] + x_data[1] + x_data[2] + x_data[3]) / 4;
			avr_y = (y_data[0] + y_data[1] + y_data[2] + y_data[3]) / 4;
			
			/* 上报事件 */
			input_event(ts_input,EV_ABS,ABS_X,avr_x);
			input_event(ts_input,EV_ABS,ABS_Y,avr_y);
			input_event(ts_input,EV_ABS,ABS_PRESSURE,1);
			input_event(ts_input,EV_KEY,BTN_TOUCH,1);
			input_sync(ts_input);

			cnt = 0;
		}

		mod_timer(&adc_timer,jiffies+msecs_to_jiffies(2));
		TsWaitRelaease();
	}
	else
	{
		if(cnt != 4)
		{
			cnt = 0;
		}

		/* 上报事件 */
		input_event(ts_input,EV_KEY,BTN_TOUCH,0);
		input_event(ts_input,EV_ABS,ABS_PRESSURE,0);
		input_sync(ts_input);
	}
}

static int touchscreen_init(void) 
{
	int ret = 0;
	
	/* 进行地址的映射 */
	adc_reg = ioremap(0x58000000,sizeof(struct adc_regs));
	if(!adc_reg)
	{
		printk("can't remmap adcreg.\n");
		return -ENOMEM;
	}

	/* 分配一个input_dev */
	ts_input = input_allocate_device();
	if(!ts_input)
	{
		printk("alloc input_dev fail.\n");
		return -ENOMEM;		
	}

	ts_input->name = "s3c_ts";

	set_bit(EV_KEY,ts_input->evbit);
	set_bit(EV_ABS,ts_input->evbit);
	
	set_bit(BTN_TOUCH,ts_input->keybit);

	input_set_abs_params(ts_input, ABS_Y, 0, 0x3FF, 0, 0);
	input_set_abs_params(ts_input, ABS_X, 0, 0x3FF, 0, 0);
	input_set_abs_params(ts_input, ABS_PRESSURE, 0, 1, 0, 0);
	
	/*  注册输入设备*/
	input_register_device(ts_input);

	/* 使能控制器时钟 */
	adc_clock = clk_get(NULL,"adc");
	clk_prepare_enable(adc_clock);
	
	adc_reg->adccon = (1 << 14) | (49 << 6) |(0<<2); 
	adc_reg->adcdly  = 0xffff;
	
	/* 定时器相关的操作 */
	setup_timer(&adc_timer,adc_timer_fun,0);

	/* 申请中断 */
	ret  = request_irq(IRQ_TC,PenDownUpIRQ,0,"PenIRQ",NULL);
	ret  |=request_irq(IRQ_ADC,ADC_ConverIRQ,0,"ADCIRQ",NULL);

	if(ret)
		goto exit;
	
	/*将触摸屏设置成 等待触摸状态*/
	TsWaitTouch();

	return 0;
	
exit:
	free_irq(IRQ_ADC,NULL);
	free_irq(IRQ_TC,NULL);
	
	return -EIO;
}

static void touchscreen_exit(void)
{
	free_irq(IRQ_ADC,NULL);
	free_irq(IRQ_TC,NULL);
	input_unregister_device(ts_input);
	input_free_device(ts_input);
	iounmap(adc_reg);
	clk_disable(adc_clock);
	del_timer_sync(&adc_timer);
	tasklet_kill(&ts_task);
}

module_init(touchscreen_init);
module_exit(touchscreen_exit);
MODULE_LICENSE("GPL");

