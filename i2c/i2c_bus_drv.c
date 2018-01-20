#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/i2c.h>
#include <uapi/linux/i2c.h>

#include <asm/io.h>
#include <mach/regs-gpio.h>
#include <mach/gpio-samsung.h>
#include <plat/gpio-cfg.h>

//#define PRINTK printk
#define PRINTK(...)

struct i2c_regs{
	unsigned int iiccon; 
	unsigned int iicstat;
	unsigned int iicadd;
	unsigned int iicds;
	unsigned int iiclc;
};

typedef enum{
	I2C_STOP,
	I2C_START,
	I2C_WRITE,
	I2C_READ,
}i2c_state;

struct i2c_transmit_data{
	int error;
	struct i2c_msg *msg_ptr;
	unsigned msg_num;
	unsigned cur_msg;
	unsigned cur_data;
	i2c_state state;
	unsigned char addr;
	struct completion  i2c_complete;
};

static int s3c_i2c_master_xfer(struct i2c_adapter *adap,struct i2c_msg *msgs, int num);
static u32 s3c_i2c_functionality(struct i2c_adapter *);
static struct i2c_transmit_data s3c_i2c_xfer_data;

static struct i2c_adapter *s3c_i2c_adapter = NULL;
static struct i2c_regs *s3c_i2c_regs = NULL;
static struct clk *s3c_clk = NULL;

static struct i2c_algorithm s3c_i2c_algo = {
	.master_xfer = s3c_i2c_master_xfer,
	.functionality= s3c_i2c_functionality,
};

/* i2c �������� */
static void i2c_start_work(void)
{	
	unsigned int addr = s3c_i2c_xfer_data.msg_ptr->addr  << 1;
	
	s3c_i2c_xfer_data.state = I2C_START;
	
	/* �� */
	if (s3c_i2c_xfer_data.msg_ptr->flags & I2C_M_RD)
	{
		s3c_i2c_regs->iicds    = addr | 0x01;
		s3c_i2c_regs->iicstat  = 0xb0;
		/* ���ж� */
		s3c_i2c_regs->iiccon   |= (1<<5);
	}
	/* д */
	else
	{	
		s3c_i2c_regs->iicds    = addr;
		s3c_i2c_regs->iicstat  = 0xf0;
		/* ���ж� */
		s3c_i2c_regs->iiccon   |= (1<<5);
	}
	
	ndelay(50);
	
}

/* i2cֹͣ����*/
static void i2c_stop_work(int ret)
{
	PRINTK("stop.\n");
	
	/* �ر��ж� */
	s3c_i2c_regs->iiccon &= ~(1<<5);
	/* ȷ��P�ź��Ѿ����� */
	s3c_i2c_regs->iicstat &= ~(1<<5);
	ndelay(50);
	
	s3c_i2c_xfer_data.state = I2C_STOP;
	s3c_i2c_xfer_data.msg_num =  0;
	s3c_i2c_xfer_data.msg_ptr	  = NULL;
	s3c_i2c_xfer_data.cur_msg = 0;
	s3c_i2c_xfer_data.cur_data = 0;
	s3c_i2c_xfer_data.error = ret;
	
	/* ���ѽ��� */
	complete(&s3c_i2c_xfer_data.i2c_complete);
}

static int s3c_i2c_master_xfer(struct i2c_adapter *adap,struct i2c_msg *msgs, int num)
{
	int timeout;
	
	s3c_i2c_xfer_data.msg_num =  num;
	s3c_i2c_xfer_data.msg_ptr	  = msgs;
	s3c_i2c_xfer_data.cur_msg = 0;
	s3c_i2c_xfer_data.cur_data = 0;
	s3c_i2c_xfer_data.error     = -ENODEV;
	
	/* ����i2c���� */
	i2c_start_work();
	timeout = wait_for_completion_timeout(&s3c_i2c_xfer_data.i2c_complete,HZ*5);
	
	if(timeout==0)
	{
		PRINTK("transmit timeout.\n");
		return  -EBUSY;
	}
	else
	{
		return s3c_i2c_xfer_data.error;
	}
	
}

static u32 s3c_i2c_functionality(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_PROTOCOL_MANGLING;
}

/* ����һ����Ϣ */
static int isLastMsg(void)
{
	return (s3c_i2c_xfer_data.cur_msg == s3c_i2c_xfer_data.msg_num - 1);
}

/* �����Ѿ������� */
static int isEndData(void)
{
	return (s3c_i2c_xfer_data.cur_data >= s3c_i2c_xfer_data.msg_ptr->len);
}

/* ��Ϣ�е����һ������ */
static int isLastData(void)
{
	return (s3c_i2c_xfer_data.msg_ptr->len == s3c_i2c_xfer_data.msg_ptr->len -1);
}

static irqreturn_t s3c_i2c_irq_handle(int irq, void *dev_id)
{
	unsigned char iicstat = s3c_i2c_regs->iicstat;
	
	if(iicstat & (1<<3))
	{
		PRINTK("bus arbitration failed.\n");
		i2c_stop_work(-EBUSY);
	}
	
	if(s3c_i2c_xfer_data.state == I2C_START)
	{	
		PRINTK("start.\n");
		/*�����豸�Ƿ��շ���Ӧ���ź�*/
		if(iicstat & 0x1)
		{
			PRINTK("no device ack\n");
			i2c_stop_work(-ENODEV);
			goto clean_flag;
		}

		/* ���ö�ٹ��̵Ĵ��� */
		if(isLastMsg() && isEndData())
		{
			i2c_stop_work(0);
			goto clean_flag;
		}

		/* �� */
		if(s3c_i2c_xfer_data.msg_ptr->flags & I2C_M_RD)
		{
			s3c_i2c_xfer_data.state = I2C_READ;
			/* ֱ���˳� */
			goto clean_flag;
		}
		/* д */
		else
		{
			s3c_i2c_xfer_data.state = I2C_WRITE;
		}
	}

	if(s3c_i2c_xfer_data.state == I2C_READ)
	{
		PRINTK("read.\n");
		
		s3c_i2c_xfer_data.msg_ptr->buf[s3c_i2c_xfer_data.cur_data++] = s3c_i2c_regs->iicds;
		ndelay(50);
		
		/* �����ǰmessagea����������Ҫ��ȡ */
		if(!isEndData())
		{	
			/* ����ACK �ź� */
			if(!isLastData())
			{
				
				s3c_i2c_regs->iiccon  |= (0x1 << 7); 
				ndelay(50); 
			}
			/* ����ACK �ź� */
			else
			{
				s3c_i2c_regs->iiccon  &= ~(0x1 << 7); 
				ndelay(50); 
			}
		}
		else if(!isLastMsg())
		{
			s3c_i2c_xfer_data.cur_data = 0;
			s3c_i2c_xfer_data.cur_msg++;
			s3c_i2c_xfer_data.msg_ptr++;
			s3c_i2c_xfer_data.state = I2C_START;
			
			i2c_start_work();
		}
		else
		{
			i2c_stop_work(0);
		}
	}
	else if(s3c_i2c_xfer_data.state == I2C_WRITE)
	{	
		PRINTK("write.\n");
		/*�����豸�Ƿ��շ���Ӧ���ź�*/
		if(iicstat & 0x1)
		{
			PRINTK("no device ack\n");
			i2c_stop_work(-ENODEV);
			goto clean_flag;
		}

		/* ����һ��д���� */
		/* ��ǰ message ���滹��������Ҫ����*/
		if(!isEndData())
		{
			s3c_i2c_regs->iicds = s3c_i2c_xfer_data.msg_ptr->buf[s3c_i2c_xfer_data.cur_data++];
			ndelay(50);
		}
		else if(!isLastMsg())
		{
			s3c_i2c_xfer_data.cur_data = 0;
			s3c_i2c_xfer_data.cur_msg++;
			s3c_i2c_xfer_data.msg_ptr++;
			s3c_i2c_xfer_data.state = I2C_START;
			/* ������һ�εĴ���*/
			i2c_start_work();
		}
		else
		{
			i2c_stop_work(0);
		}		
	}
	/*��ǰ��״̬��stop  */
	else
	{
		i2c_stop_work(0);
	}

clean_flag:
	/* ���жϱ�־λ */
	s3c_i2c_regs->iiccon &= ~(1<<4);
	return IRQ_HANDLED;
}

static int s3c_i2c_bus_init(void)
{
	s3c_i2c_regs  = ioremap(0x54000000,sizeof(struct i2c_regs));
	
	/* ����һ��i2c_adapter */
	s3c_i2c_adapter = kzalloc(sizeof(struct i2c_adapter),GFP_KERNEL);
	if(!s3c_i2c_adapter)
	{
		PRINTK("alloc i2c adapter failed.\n");
		return -ENOMEM;
	}

	strcpy(s3c_i2c_adapter->name ,"s3c2440_i2c");
	s3c_i2c_adapter->owner = THIS_MODULE;
	s3c_i2c_adapter->algo    = &s3c_i2c_algo;
	s3c_i2c_adapter->class  = I2C_CLASS_DEPRECATED;

	/* �������� */
	s3c_gpio_cfgpin(S3C2410_GPE(14), S3C2410_GPE14_IICSCL);
	s3c_gpio_cfgpin(S3C2410_GPE(15), S3C2410_GPE15_IICSDA);
	
	/* ��ʱ��*/
	s3c_clk = clk_get(NULL,"i2c");
	clk_prepare_enable(s3c_clk);
	
	/* i2c controler ���� */
	/* IICCLK = 3.125MHZ */
	/* Tx Clock =  100KHZ*/
	s3c_i2c_regs->iiccon = (1<< 7) | (1<<5) | (0xf);
	s3c_i2c_regs->iicadd = 0x10;
	
	/* �����ж� */
	if(request_irq(IRQ_IIC,s3c_i2c_irq_handle,0,"s3c_i2c",NULL))
	{
		PRINTK("request irq failed.\n");
		return -ENODEV;
	}
	
	init_completion(&s3c_i2c_xfer_data.i2c_complete);
	i2c_add_adapter(s3c_i2c_adapter);
	
	return 0; 
}


static void s3c_i2c_bus_exit(void)
{	
	iounmap(s3c_i2c_regs);
	i2c_del_adapter(s3c_i2c_adapter);
	free_irq(IRQ_IIC,NULL);
	kfree(s3c_i2c_adapter);
}

module_init(s3c_i2c_bus_init);
module_exit(s3c_i2c_bus_exit);
MODULE_AUTHOR("Kristoffer Nyborg Gregertsen <kngregertsen@norway.atmel.com>");
MODULE_LICENSE("GPL");

