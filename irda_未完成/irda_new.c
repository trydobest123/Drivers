#include <linux/consolemap.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/slab.h>

#include <linux/kbd_kern.h>
#include <linux/kbd_diacr.h>
#include <linux/vt_kern.h>
#include <linux/input.h>
#include <linux/reboot.h>
#include <linux/notifier.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>

#include <asm/irq_regs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>

#include <linux/gpio.h>
#include <mach/regs-gpio.h>
#include <mach/gpio-samsung.h>
#include <mach/hardware.h>
#include <plat/gpio-cfg.h>

/*
标准的 NEC 码规范：
首次发送的是9ms的高电平脉冲，其后是4.5ms的低电平，接下来就是8bit的地址码（从低有效位开始发），
而后是8bit的地址码的反码（主要是用于校验是否出错）。然后是8bit 的命令码（也是从低有效位开始发），
而后也是8bit 的命令码的反码。其“0”为载波发射0.56ms,不发射0.565ms,其“1”为载波发射0.56ms,
不发射1.69ms。
*/

#define IR gpio_get_value(S3C2410_GPF(2))

static irqreturn_t irm_irq(int irq, void *dev_id)
{
    int k,i;
    unsigned char irm_code[4] = {0, 0, 0, 0};
    
    /* 操作原理 当有中断发生中 配置引脚功能为 GPIO 输入型 直接读IRM信号电平高低 */
    s3c_gpio_cfgpin(S3C2410_GPF(2), S3C2410_GPIO_INPUT);
    mdelay(5); //9ms 内必须是低电平否则就不是头信息
    if(0 == IR)
     {
         while(! IR);//等待4.5ms的高电平
         
         //检测是否是 2.5ms 重码 
         mdelay(3);
         if(1 == IR)
         {
             //k 4位编码
             for(k=0; k<4; k++)
             {
                 //i 每一个编码的 8bit
                 for(i=0;i<8;i++)
                 {
                     while(IR);     //等待变为低电平时
                     while(! IR);   //等待变为高电平后
                     udelay(700);  //休眠700us 后读值
                     irm_code[k] |= (IR & 1)<<i;//先存低位
                 }
             }
             
             //计算反码 code码是否正确
             if((irm_code[2]&0x3f) == (~(irm_code[3]) & 0x3f))
             {
                 printk("FID:0x%x 0x%x CID: 0x%x 0x%x\n", irm_code[0], irm_code[1], irm_code[2], irm_code[3]);
             }
         }
     }
    s3c_gpio_cfgpin(S3C2410_GPF(2), S3C2410_GPIO_IRQ);
    return IRQ_HANDLED;
}

static int irm_init(void)
{
    int ret;
    //注册中断 上升沿触发
    ret = request_irq(IRQ_EINT2, irm_irq, IRQF_TRIGGER_RISING, "irm_irq", NULL);
    return 0;
}

static void irm_exit(void)
{
    free_irq(IRQ_EINT2, NULL);
}

module_init(irm_init);
module_exit(irm_exit);
MODULE_LICENSE("GPL");