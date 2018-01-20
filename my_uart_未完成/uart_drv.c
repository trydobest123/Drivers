/*  ʵ��tx2440 uart1������ */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/serial_core.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/console.h> 
#include <linux/serial.h>
#include <linux/delay.h>
#include <linux/circ_buf.h>
#include <linux/irq.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <mach/irqs.h>

struct s3c2440_uart1_reg{
	unsigned int ulcon; 
	unsigned int ucon;  
	unsigned int ufcon; 
	unsigned int umcon; 
	unsigned int utrstat;
	unsigned int uerstat;
	unsigned int ufstat;
	unsigned int umstat;
	unsigned char reg[7];
	unsigned int utxh;  
	unsigned int urxh;  
	unsigned int ubrdiv;
};

#define uart_break	(1<<3)
#define uart_frame	(1<<2)
#define uart_parity	(1<<1)
#define uart_overrun	(1<<0)

#define uart_uerstate_mask		(uart_break|uart_frame|uart_parity|uart_overrun)

static struct s3c2440_uart1_reg *uart_regs = NULL;
static struct uart_port *u_port = NULL;
static struct clk *uart_clk = NULL;

static int s3c2440_serial_console_set(struct console *co, char *options)
{
	struct uart_port *port = u_port;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		return -EIO;
		
	return uart_set_options(port, co, baud, parity, bits, flow);
}

static unsigned int s3c2440_txfifo_has_data(void)
{	
	return ((uart_regs->ufstat >> 8) & 0x1ff);
}

/* ��黺���Ƿ�Ϊ��*/
static unsigned int s3c2440_uart_is_empty(struct uart_port *port)
{	
	unsigned int ufcon = uart_regs->ufcon;
	unsigned int utrstat;
	
	/* FIFO Mode */
	if(ufcon & (1<<0))
	{
		return s3c2440_txfifo_has_data() ? 0:1;
	}
	else
	{
		utrstat = uart_regs->utrstat;
		return (utrstat & 1<<2)  ?  1:0;
	}
}

static void putch(unsigned char *s)
{	
	/* �ȴ������Ƿ���� */
	while(s3c2440_uart_is_empty(NULL))
		udelay(10);
	uart_regs->utxh = *s;
}

/* ������� */
static void s3c2440_serial_console_put(struct console *co, const char *s,unsigned int count)
{
	int i = 0;
	
	for (i = 0; i < count; i++, s++) {
		if (*s == '\n')
			putch('\r');
		putch(*s);
	}
}

static struct console s3c2440_serial_console = {
	.name		= "my_tty",
	.device		= uart_console_device,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.write		= s3c2440_serial_console_put,
	.setup		= s3c2440_serial_console_set,
};

static struct uart_driver my_uart_drv = {
	.owner 	      = THIS_MODULE,
	.driver_name = "s3c2440_serial1", //������
	.dev_name     = "my_tty", //�豸�ڵ���
	.minor            = 1,
	.nr		       = 1,
	.cons              = &s3c2440_serial_console,
};

/*  ���ô��ڵĸ�ʽ */
static void s3c2440_serial_set_termios(struct uart_port *port,
						struct ktermios *termios,
				      		 struct ktermios *old)
{
	struct clk *clk;
	unsigned int buad;
	unsigned int ulcon;
	unsigned int umcon;
	unsigned int clk_rate;
	
	termios->c_cflag &= ~(HUPCL | CMSPAR);
	termios->c_cflag |= CLOCAL;

	buad = uart_get_baud_rate(port, termios, old, 0, 115200*8);
	
	/* ���ݲ����ʼ���ʱ�� */
	clk = clk_get(NULL,"pclk");
	clk_rate = clk_get_rate(clk);
	printk("clk_rate = %d\n",clk_rate);
	clk_put(clk);
	/* ubrdivn=(pclk/(buad rate x 16) )�C1*/
	uart_regs->ubrdiv = (unsigned int)((clk_rate / (buad * 16)) -1);
	
	/* ȷ������λ */
	switch (termios->c_cflag & CSIZE) {
	case CS5:
		printk("config: 5bits/char\n");
		ulcon = 0x0;
		break;
	case CS6:
		printk("config: 6bits/char\n");
		ulcon = 0x1;
		break;
	case CS7:
		printk("config: 7bits/char\n");
		ulcon = 0x2;
		break;
	case CS8:
	default:
		printk("config: 8bits/char\n");
		ulcon = 0x3;
		break;
	}

	/* ȷ������ */
	umcon = (termios->c_cflag & CRTSCTS) ? (1<<4) : 0;
	
	 /* ȷ��ֹͣλ */
	 if (termios->c_cflag & CSTOPB)
		ulcon |= (1<<2); //��λֹͣλ
	 /* ȷ��У��λ */
	 if (termios->c_cflag & PARENB) 
	 {
	 	/*  ������������У��λ */
		if (termios->c_cflag & PARODD)
			ulcon |= (0x4<<3);
		else
			ulcon |= (0x5<<3);
	}
	else 
	{
		ulcon |= (0x0 << 3);
	}

	uart_regs->umcon = umcon;
	uart_regs->ulcon 	  = ulcon;
	
	udelay(5);
	/* ���³�ʱʱ�� */
	uart_update_timeout(port, termios->c_cflag, buad);
}

static void s3c2440_serial_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_S3C2440;
}

static int  s3c2440_serial_verify_port(struct uart_port *port, struct serial_struct *serial)
{
	if (serial->type != PORT_UNKNOWN && serial->type != PORT_S3C2440)
		return -EINVAL;
		
	return 0;
}

static unsigned int s3c2440_serial_tx_empty(struct uart_port *port)
{
	unsigned int ufcon = uart_regs->ufcon;
	unsigned int ufstate = uart_regs->ufstat;
	unsigned int utrstate = uart_regs->utrstat; 
	unsigned int empty = 0;
	
	if(ufcon & (1<<0))
	{
		if(ufstate & (1<<14))
			empty = 0;
		else
			empty = 1;
	}
	else
	{
		if(utrstate & (1<<2))
			empty = 1;
		else
			empty = 0;
	}
	
	return empty;
}

static unsigned int	s3c2440_serial_get_mctrl(struct uart_port *port)
{
	unsigned int umcon = uart_regs ->umcon;

	if(umcon & (1<<4))
		return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
		
	return TIOCM_CAR | TIOCM_DSR;
}

static char *s3c2440_serial_type(struct uart_port *port)
{
	switch (port->type) {
	case PORT_S3C2410:
		return "S3C2410";
	case PORT_S3C2440:
		return "S3C2440";
	case PORT_S3C2412:
		return "S3C2412";
	case PORT_S3C6400:
		return "S3C6400/10";
	default:
		return NULL;
	}
}

static void s3c2440_serial_start_tx(struct uart_port *port)
{
	printk("%s\n", __FUNCTION__);
	/* ���ж� */
	enable_irq(IRQ_S3CUART_TX1);
}

static void s3c2440_serial_stop_tx(struct uart_port *port)
{
	printk("%s\n", __FUNCTION__);
	/* �����������û�з��ͳ�ȥ����еȴ� */
	while(!s3c2440_uart_is_empty(port))
		udelay(10);
	/* �ر��ж� */
	disable_irq(IRQ_S3CUART_TX1);
}

static void s3c2440_serial_stop_rx(struct uart_port *port)
{
	printk("%s\n", __FUNCTION__);
	/* �ر��ж� */
	disable_irq(IRQ_S3CUART_TX1);
}

static irqreturn_t s3c2440_uart1_txirq(int irq, void *dev_id)
{
	printk("%s\n", __FUNCTION__);
	struct uart_port *port = (struct uart_port *)dev_id;
	struct circ_buf *xmit = &port->state->xmit;  //����ַ��Ļ�����
	int count = 256;

	if (port->x_char) {
		uart_regs->utxh = port->x_char;
		port->icount.tx++;
		port->x_char = 0;
		goto out;
	}

	/* ���û������Ҫ���ͣ���ֹͣ���� */
	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		s3c2440_serial_stop_tx(port);
		goto out;
	}

	/* �����ݼ������� */
	while((count-- > 0)&&(!uart_circ_empty(xmit)))
	{	
		uart_regs->utxh = port->x_char;
		xmit->tail = (xmit->tail + 1)  % UART_XMIT_SIZE;
		port->icount.tx++;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	/* ���Ҫ�ж���û�����ݣ���ֹ�´ν����ж� */
	if (uart_circ_empty(xmit))
		s3c2440_serial_stop_tx(port);
out:
	return IRQ_HANDLED;
}

static unsigned char s3c2440_serial_fifocnt(void)
{
	return (uart_regs->ufstat & 0x3ff);
}

static irqreturn_t s3c2440_uart1_rxirq(int irq, void *dev_id)
{
	printk("%s\n", __FUNCTION__);
	struct uart_port *port = (struct uart_port *)dev_id;
	struct tty_struct *tty = port->state->port.tty;
	unsigned int max_count = 64;
	unsigned int uerstat;
	unsigned char flag;
	unsigned char ch;
	
	while ((max_count-- > 0)&&s3c2440_serial_fifocnt())
	{
		/* ��ȡ״̬ */
		uerstat = uart_regs->uerstat;
		/* ��ý��յ����� */
		ch = uart_regs->urxh;

		/* insert the character into the buffer */
		flag = TTY_NORMAL;
		port->icount.rx++;

		if (uerstat & uart_uerstate_mask) 
		{
			/* check for break */
			if (uerstat & uart_break) 
			{
				port->icount.brk++;
				if (uart_handle_break(port))
				    continue;
			}

			if (uerstat & uart_frame)
				port->icount.frame++;
			if (uerstat & uart_overrun)
				port->icount.overrun++;

			uerstat &= port->read_status_mask;
			switch(uerstat)
			{
				case uart_break:
					flag = TTY_BREAK;
				break;
				case uart_frame:
					flag = TTY_FRAME;
				break;
				case uart_parity:
					flag = TTY_PARITY;
				break;
				case uart_overrun:
					flag = TTY_OVERRUN; 
				break;
			}
		}
		
		uart_insert_char(port, uerstat, uart_overrun,ch, flag);
	}

	tty_flip_buffer_push(tty);
	
	return IRQ_HANDLED;
}

static void s3c2440_uart_boot_init(void)
{
	/* ѡ��ΪPOLLģʽ��NO-FIFO */
	uart_regs->ucon = (0 << 10) | (0 << 7 ) |(1 << 6) | (0<<5) | (0<<4) | (1<<2) ;
	uart_regs->ulcon = (0x3<<0); //Ĭ����8bit����λ
	uart_regs->ufcon = (0<<0);
}

static void s3c2440_uart_console_init(void)
{
	/*ѡ��ʱ��ԴΪPCLK,�շ�Ϊ�ж�ģʽ*/
	uart_regs->ucon = (0 << 10) |(1<<9) |(1<<8)  | (1 << 7 ) |(1 << 6) | (0<<5) | (0<<4) | (1<<2) |(1<<0);
	uart_regs->ulcon = (0x3<<0);
	uart_regs->ufcon = (1<<2) | (1<<1); //��λfifo
	udelay(5);
	uart_regs->ufcon = (1<<0) | (1<<4) | (1<<6);
}

static int s3c2440_serial_startup(struct uart_port *port)
{
	int ret = 0;

	/* ����Interruptģʽ,ʹ��FIFO */
	s3c2440_uart_console_init();
	
	ret = request_irq(IRQ_S3CUART_TX0,s3c2440_uart1_txirq,0,"s3c2440_uart1_tx",(void*)port);
	ret = request_irq(IRQ_S3CUART_RX0,s3c2440_uart1_rxirq,0,"s3c2440_uart1_rx",(void*)port);

	if(ret)
		goto error;

	
error:
	free_irq(IRQ_S3CUART_TX0,NULL);
	free_irq(IRQ_S3CUART_RX0,NULL);
	
	return -EINVAL;
}

static void s3c2440_serial_shutdown(struct uart_port *port)
{
	free_irq(IRQ_S3CUART_TX0,NULL);
	free_irq(IRQ_S3CUART_RX0,NULL);
}

static void s3c2440_serial_release_port(struct uart_port *port)
{
	return;
}

static int	s3c2440_serial_request_port(struct uart_port *port)
{
	
	return 0;
}

static struct uart_ops s3c2440_serial1_ops = {
	.tx_empty	= s3c2440_serial_tx_empty,
	.get_mctrl	= s3c2440_serial_get_mctrl,
	.stop_tx		= s3c2440_serial_stop_tx,
	.start_tx		= s3c2440_serial_start_tx,
	.stop_rx		= s3c2440_serial_stop_rx,
	.startup		= s3c2440_serial_startup,
	.shutdown	= s3c2440_serial_shutdown,
	.set_termios	= s3c2440_serial_set_termios,
	.type		= s3c2440_serial_type,
	.release_port	= s3c2440_serial_release_port,
	.request_port	= s3c2440_serial_request_port,
	.config_port	= s3c2440_serial_config_port,
	.verify_port	= s3c2440_serial_verify_port,
};

static struct uart_port my_uart_port  = {
	.lock		= __SPIN_LOCK_UNLOCKED(my_uart_spinlock),
	.iotype	= UPIO_MEM,
	.uartclk	= 0,
	.fifosize	= 16,
	.ops		= &s3c2440_serial1_ops,
	.flags	= UPF_BOOT_AUTOCONF,
	.line		= 0,
};

static int my_uart_init(void)
{
	int ret = 0;

	printk("%s\n",__FUNCTION__);
	uart_regs = ioremap(0x50004000,sizeof(struct s3c2440_uart1_reg));
	if(!uart_regs)
		return -ENOMEM;

	uart_clk = clk_get(NULL,"uart");
	if(!uart_clk)
	{
		printk("don't find uart clock.\n");
		return -ENODEV;
	}
	clk_enable(uart_clk);

	u_port = &my_uart_port;
	s3c2440_uart_boot_init();	
	ret = uart_register_driver(&my_uart_drv);
	udelay(10); 
	ret |= uart_add_one_port(&my_uart_drv,&my_uart_port);
	if(ret)
		return -EBUSY;
	return 0;
}

static void my_uart_exit(void)
{
	uart_unregister_driver(&my_uart_drv);
	uart_remove_one_port(&my_uart_drv,&my_uart_port);
	free_irq(IRQ_S3CUART_TX0,NULL);
	free_irq(IRQ_S3CUART_RX0,NULL);
	iounmap(uart_regs);
	clk_disable(uart_clk);
	
}

module_init(my_uart_init);
module_exit(my_uart_exit);
MODULE_LICENSE("GPL");

