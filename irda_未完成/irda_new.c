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
��׼�� NEC ��淶��
�״η��͵���9ms�ĸߵ�ƽ���壬�����4.5ms�ĵ͵�ƽ������������8bit�ĵ�ַ�루�ӵ���Чλ��ʼ������
������8bit�ĵ�ַ��ķ��루��Ҫ������У���Ƿ������Ȼ����8bit �������루Ҳ�Ǵӵ���Чλ��ʼ������
����Ҳ��8bit ��������ķ��롣�䡰0��Ϊ�ز�����0.56ms,������0.565ms,�䡰1��Ϊ�ز�����0.56ms,
������1.69ms��
*/

#define IR gpio_get_value(S3C2410_GPF(2))

static irqreturn_t irm_irq(int irq, void *dev_id)
{
    int k,i;
    unsigned char irm_code[4] = {0, 0, 0, 0};
    
    /* ����ԭ�� �����жϷ����� �������Ź���Ϊ GPIO ������ ֱ�Ӷ�IRM�źŵ�ƽ�ߵ� */
    s3c_gpio_cfgpin(S3C2410_GPF(2), S3C2410_GPIO_INPUT);
    mdelay(5); //9ms �ڱ����ǵ͵�ƽ����Ͳ���ͷ��Ϣ
    if(0 == IR)
     {
         while(! IR);//�ȴ�4.5ms�ĸߵ�ƽ
         
         //����Ƿ��� 2.5ms ���� 
         mdelay(3);
         if(1 == IR)
         {
             //k 4λ����
             for(k=0; k<4; k++)
             {
                 //i ÿһ������� 8bit
                 for(i=0;i<8;i++)
                 {
                     while(IR);     //�ȴ���Ϊ�͵�ƽʱ
                     while(! IR);   //�ȴ���Ϊ�ߵ�ƽ��
                     udelay(700);  //����700us ���ֵ
                     irm_code[k] |= (IR & 1)<<i;//�ȴ��λ
                 }
             }
             
             //���㷴�� code���Ƿ���ȷ
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
    //ע���ж� �����ش���
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