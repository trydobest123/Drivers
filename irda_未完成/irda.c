#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irqflags.h>
#include <linux/irq.h>
#include <linux/delay.h>

static unsigned int *gpfcon = NULL;
static unsigned int *gpfdat = NULL;

#define IRDA  (*gpfdat & (1<<2))

static irqreturn_t irda_handler(int irq, void *devid)
{
		unsigned char code_buf[4] = {0};
		unsigned int i,j;
		
		mdelay(8);
		/* ˵������ȷ�������� */
		if(IRDA)
		{
			mdelay(4);
			if(IRDA == 0)
			{
				/*������*/
				for( i = 0; i < 8; i++)
				{
						for( j = 0; j < 8; j++)
						{
							 while(IRDA);     //�ȴ���Ϊ�͵�ƽʱ
                         while(! IRDA);   //�ȴ���Ϊ�ߵ�ƽ��
                         udelay(10);  //����700us ���ֵ
                         code_buf[i] |= (IRDA & 1)  << j;//�ȴ��λ
						}
						
				}

				for( i = 0; i < 8; i++)
					printk("0x%x  ",code_buf[i]);0
				printk("\n");
			}
			else
			{
				printk("%d invalid code.\n",__LINE__);
			}
		}
		else
		{
			 printk("%d invalid code.\n",__LINE__);
		}

		*gpfcon |= 0x2 << 4;
		return IRQ_HANDLED;
}

static int irda_init(void)
{
		/* ӳ��ܽ� */
		gpfcon = ioremap(0x56000050,4);
		gpfdat = gpfcon + 1;

		/* ����gpf2Ϊ�������� */
		*gpfcon = 0x2 << 4;
		
		/* �����ж� */
		request_irq(IRQ_EINT2,irda_handler,IRQF_TRIGGER_RISING,"ira_dev",NULL);
		
		return 0;
}


static void irda_exit(void)
{
		iounmap(gpfdat);
		iounmap(gpfcon);
		
		/* �ͷ��ж� */
		free_irq(IRQ_EINT2,NULL);
}

module_init(irda_init);
module_exit(irda_exit);

MODULE_LICENSE("GPL");
