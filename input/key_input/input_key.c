#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/hid.h>
#include <linux/gpio.h>
#include <asm/irq.h>
#include <mach/regs-gpio.h>
#include <mach/gpio-samsung.h>
#include <plat/gpio-cfg.h>

struct KeyDesc {
	int KeyIRQ;
	int keyVal;
	int KeyPin;
	char *Name;
};

static struct KeyDesc KeyDevDesc[4] = {
	{IRQ_EINT4,KEY_L,S3C2410_GPF(4),"key1"},
	{IRQ_EINT5,KEY_S,S3C2410_GPF(5),"key2"},
	{IRQ_EINT6,KEY_ENTER,S3C2410_GPF(6),"key3"},
	{IRQ_EINT7,KEY_DELETE,S3C2410_GPF(7),"key4"},
};

static struct timer_list key_timer;
static struct input_dev *key_input = NULL;
static struct KeyDesc *KeyValDesc;

static irqreturn_t KeyInputIRQ(int irq, void *devid)
{
	KeyValDesc = (struct KeyDesc *)devid;
	
	mod_timer(&key_timer,jiffies+msecs_to_jiffies(10));
	
	return IRQ_RETVAL(IRQ_HANDLED);
}

void key_timer_handle(unsigned long data)
{	
	int keystatus; 

	keystatus = gpio_get_value(KeyValDesc->KeyPin);

	//printk("keystatus = %d\n",keystatus);
	
	/* 上报事件 */
	if(keystatus)
	{	/* 如果为1则表明已松开 */
		input_report_key(key_input, KeyValDesc->keyVal,1);
		input_sync(key_input);
	}
	else
	{	
		/* 如果为0则表明已按下 */
		input_report_key(key_input, KeyValDesc->keyVal,0);
		input_sync(key_input);	
	}
	
}

static int KeyInputinit(void)
{	
	unsigned int i = 0;
	unsigned int ret;
	
	/* 分配一个input_dev 结构体 */
	key_input = input_allocate_device();	
	if(!key_input)
	{
		printk("alloc input device fail.\n");

		return -ENOMEM;
	}
	
	/* 进行一些设置 */
	key_input->name = "key_input";

	/* 设置可以支持什么类型的输入事件 */
	set_bit(EV_KEY, key_input->evbit);
	
	/* 设置输输入事件中的哪种 */
	set_bit(KEY_ENTER,key_input->keybit);
	set_bit(KEY_DELETE, key_input->keybit);
	set_bit(KEY_L,key_input->keybit);
	set_bit(KEY_S,key_input->keybit);
	
	 input_register_device(key_input);

	init_timer(&key_timer);
	key_timer.function    = key_timer_handle;
	key_timer.data = 0;
	
	/* 注册中断 */
	for( i = 0; i < 4; i++)
	{
		s3c_gpio_cfgpin(KeyDevDesc[i].KeyPin,S3C_GPIO_INPUT);
		
		ret = request_irq(KeyDevDesc[i].KeyIRQ,KeyInputIRQ,IRQF_TRIGGER_FALLING |IRQF_TRIGGER_RISING, \
			KeyDevDesc[i].Name,&KeyDevDesc[i]);
	}	

	return 0;
	
out:
	while((i--) >=0)
		free_irq(KeyDevDesc[i].KeyIRQ,NULL);
	
	return -EIO;
}

static void KeyInputExit(void)
{
	int i;
	
	input_unregister_device(key_input);
	input_free_device(key_input);
	for(i = 0; i < 4; i++)
	{
		free_irq(KeyDevDesc[i].KeyIRQ,&KeyDevDesc[i]);
	}
}

module_init(KeyInputinit);
module_exit(KeyInputExit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("A Simple Key Input device");

