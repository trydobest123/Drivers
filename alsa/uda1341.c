#include <linux/kernel.h>
#include <linux/module.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/control.h>
#include <asm/delay.h>
#include <asm/io.h>
#include <linux/platform_device.h>

#define UDA134X_RATES SNDRV_PCM_RATE_8000_48000
#define UDA134X_FORMATS (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE | \
		SNDRV_PCM_FMTBIT_S18_3LE | SNDRV_PCM_FMTBIT_S20_3LE)
		
#define UDA1341_EXTADDR_PREFIX	0xC0
#define UDA1341_EXTDATA_PREFIX	0xE0

/* status control */
#define STAT0 (0x00)
#define STAT0_RST (1 << 6)
#define STAT0_SC_MASK (3 << 4)
#define STAT0_SC_512FS (0 << 4)
#define STAT0_SC_384FS (1 << 4)
#define STAT0_SC_256FS (2 << 4)
#define STAT0_IF_MASK (7 << 1)
#define STAT0_IF_I2S (0 << 1)
#define STAT0_IF_LSB16 (1 << 1)
#define STAT0_IF_LSB18 (2 << 1)
#define STAT0_IF_LSB20 (3 << 1)
#define STAT0_IF_MSB (4 << 1)
#define STAT0_IF_LSB16MSB (5 << 1)
#define STAT0_IF_LSB18MSB (6 << 1)
#define STAT0_IF_LSB20MSB (7 << 1)
#define STAT0_DC_FILTER (1 << 0)
#define STAT0_DC_NO_FILTER (0 << 0)
#define STAT1 (0x80)
#define STAT1_DAC_GAIN (1 << 6) /* gain of DAC */
#define STAT1_ADC_GAIN (1 << 5) /* gain of ADC */
#define STAT1_ADC_POL (1 << 4) /* polarity of ADC */
#define STAT1_DAC_POL (1 << 3) /* polarity of DAC */
#define STAT1_DBL_SPD (1 << 2) /* double speed playback */
#define STAT1_ADC_ON (1 << 1) /* ADC powered */
#define STAT1_DAC_ON (1 << 0) /* DAC powered */

#define UDA1341_REGS_NUM 12

#define UDA1341_L3ADDR	5
#define UDA1341_DATA0_ADDR	((UDA1341_L3ADDR << 2) | 0)
#define UDA1341_DATA1_ADDR	((UDA1341_L3ADDR << 2) | 1)
#define UDA1341_STATUS_ADDR	((UDA1341_L3ADDR << 2) | 2)

/* UDA1341 registers */
#define UDA1341_DATA00 0
#define UDA1341_DATA01 1
#define UDA1341_DATA10 2
#define UDA1341_EA000  3
#define UDA1341_EA001  4
#define UDA1341_EA010  5
#define UDA1341_EA100  6
#define UDA1341_EA101  7
#define UDA1341_EA110  8
#define UDA1341_DATA1  9
#define UDA1341_STATUS0 10
#define UDA1341_STATUS1 11

static char uda1341_reg[UDA1341_REGS_NUM] = {
	 /* DATA0 */    
	 0x00, 0x40, 0x80,
	 /* Extended address registers */	
	 0x04, 0x04, 0x04, 0x00, 0x00, 0x00,
	 /* data1 */    
	 0x00,
	 /* status regs */
	 0x00, 0x83,
};

static char uda1341_data_bit[UDA1341_REGS_NUM] = {
	/* data0 */
	0, (1<<6), (1<<7),  
	0, 0, 0, 0, 0, 0,
	/*data1*/
	0,    
	/* status */
	0, (1<<7),
};

static volatile unsigned int *gpbcon = NULL;
static volatile unsigned int *gpbdat = NULL;
static volatile unsigned int *gpbup  = NULL;

static struct platform_device *s3c_audio_codec = NULL;

static void l3_clk(unsigned char dat)
{
	if(dat)
		*gpbdat |= (1 << 4);
	else
		*gpbdat &= ~(1 << 4);
}

static void l3_dat(unsigned char dat)
{
	if(dat)
		*gpbdat |= (1 << 3);
	else
		*gpbdat &= ~(1 << 3);
}

/* 高电平设置为数据传输模式 */
/* 低电平设置为地址传输模式 */
static void l3_mode(unsigned char dat)
{
	if(dat)
		*gpbdat |= (1 << 2);
	else
		*gpbdat &= ~(1 << 2);
}

static void l3_write_byte(unsigned char dat)
{
	unsigned int i;

	for(i = 0; i < 8; i++)
	{
		l3_clk(0); 		
		udelay(1);
		l3_dat(dat&0x1); 
		udelay(1);
		l3_clk(1);
		l3_dat(0);
		dat >>= 1;
	}

	l3_clk(1);
}

static void l3_write_address(unsigned char address)
{
	l3_mode(0);
	udelay(1);
	l3_write_byte(address);
	udelay(1);
	l3_mode(1);
}

static void l3_write_dat(unsigned char dat)
{
	l3_mode(1);
	udelay(1);
	l3_write_byte(dat);
	udelay(1);
	l3_mode(1);
}

static void l3_write(unsigned int addr,unsigned int dat)
{
	l3_write_address(addr);
	l3_write_dat(dat);
}

static unsigned int uda1341_read_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg)
{
	u8 *cache = codec->reg_cache;

	if (reg >= UDA1341_REGS_NUM)
		return -1;
	return cache[reg];
} 

static int uda1341_write_cache(struct snd_soc_codec *codec,unsigned int reg,unsigned char val)
{
	
	if (reg >= UDA1341_REGS_NUM)
		return -1;
		
	uda1341_reg[reg] = val;
	return 0;
} 

static int uda1341_write_reg(struct snd_soc_codec *codec, unsigned int reg,unsigned int value)
{
	unsigned  int addr,dat;
	dat = value;
	
	if(uda1341_write_cache(codec,reg,value))
		return -1;
	
	switch (reg) {
	case UDA1341_STATUS0:
	case UDA1341_STATUS1:
		addr = UDA1341_STATUS_ADDR;
		break;
	case UDA1341_DATA00:
	case UDA1341_DATA01:
	case UDA1341_DATA10:
		addr = UDA1341_DATA0_ADDR;
		break;
	case UDA1341_DATA1:
		addr = UDA1341_DATA1_ADDR;
		break;
	default:
		/* It's an extended address register */
		addr =  (reg | UDA1341_EXTADDR_PREFIX);

		l3_write(UDA1341_DATA0_ADDR, addr);

		addr = UDA1341_DATA0_ADDR;
		dat = (value | UDA1341_EXTDATA_PREFIX);
		break;
	}

	dat = uda1341_data_bit[reg] | dat;
	l3_write(addr,dat);

	return 0;
}

#define DATA0_VOLUME(x) (x)
#define DATA1_BASS(x)	(x<<4)
#define DATA1_TREBLE(x)	(x & 0x3)


static struct snd_kcontrol_new uda1341_kcontrol[] = {
	SOC_SINGLE("Master Playback Volume", UDA1341_DATA00, 0, 0x3F, 1),
	SOC_SINGLE("Tone Control - Bass", UDA1341_DATA01, 2, 0xF, 0),
	SOC_SINGLE("Tone Control - Treble", UDA1341_DATA01, 0, 3, 0),
};

static void uda1341_init_regs(struct snd_soc_codec *codec)
{
	
	uda1341_write_reg(codec, UDA1341_STATUS0, 0x40 | STAT0_SC_384FS | STAT0_DC_FILTER); // reset uda1341    
	uda1341_write_reg(codec, UDA1341_STATUS1, STAT1_ADC_ON | STAT1_DAC_ON);
	uda1341_write_reg(codec, UDA1341_DATA00, DATA0_VOLUME(0x0)); // maximum volume
	uda1341_write_reg(codec, UDA1341_DATA01, DATA1_BASS(0)| DATA1_TREBLE(0));
	uda1341_write_reg(codec, UDA1341_DATA10, 0);  // not mute
}

static int uda1341_startup(struct snd_pcm_substream *substream,struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec =rtd->codec;
	/* 直接初始化UDA1341的寄存器 */
	uda1341_init_regs(codec);
	
	return 0;
}

static int uda1341_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params,
	struct snd_soc_dai *dai)
{

	/* 根据params传进来的参数设置硬件 */
	
	return 0;
}

static struct snd_soc_dai_ops uda1341_dai_ops = {
	.startup  = uda1341_startup,
	.hw_params = uda1341_hw_params,
};

static struct snd_soc_dai_driver uda1341_dai = {
	.name = "uda1341-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = UDA134X_RATES,
		.formats = UDA134X_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = UDA134X_RATES,
		.formats = UDA134X_FORMATS,
	},
	.ops = &uda1341_dai_ops,
};

static int uda1341_codec_probe(struct snd_soc_codec *codec)
{
	int ret = 0;
	/* 配置引脚*/
	/* L3MODE GPB2*/
	/* L3DATA GPB3*/
	/* L3CLOCK GPB4 */

	*gpbcon &= ~((3 << 4) | (3 << 6) | (3 << 8));
	*gpbcon |= ((1 << 4) | (1 << 6) | (1 << 8)) ;
	
	ret = snd_soc_add_codec_controls(codec,uda1341_kcontrol,ARRAY_SIZE(uda1341_kcontrol));//添加音量控制插件
	if(ret)
		return -EIO;
		
	return 0;
}

struct snd_soc_codec_driver uda1341_codec = {
	.probe = uda1341_codec_probe,
	.reg_cache_size = sizeof(uda1341_reg),
	.reg_word_size = sizeof(u8),
	.reg_cache_default = uda1341_reg,
	.reg_cache_step = 1,
	.read = uda1341_read_reg_cache,
	.write = uda1341_write_reg,
};

static int s3c_audio_codec_probe(struct platform_device *pdev)
{
	snd_soc_register_codec(&pdev->dev,&uda1341_codec,&uda1341_dai,1);
	return 0;
}

static int s3c_audio_codec_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver s3c_audio_codec_drv = {
	.probe  = s3c_audio_codec_probe,
	.remove = s3c_audio_codec_remove,
	.driver = {
		.name = "uda1341-codec",
	},
};

static int __init uda1341_init(void)
{
	int ret; 
	s3c_audio_codec = platform_device_alloc("uda1341-codec",-1);
	if(!s3c_audio_codec)
		return -ENOMEM;
	ret = platform_device_add(s3c_audio_codec);
	if(ret)
	{
		platform_device_put(s3c_audio_codec);
		return -ENODEV;
	}

	gpbcon = ioremap(0x56000010,12);
	gpbdat = gpbcon + 1;
	gpbup  = gpbdat + 1;
	
	platform_driver_register(&s3c_audio_codec_drv);
	return 0;
}

static void __exit uda1341_exit(void)
{

	iounmap(gpbcon);
	iounmap(gpbdat);
	iounmap(gpbup);
	platform_driver_unregister(&s3c_audio_codec_drv);
	platform_device_unregister(s3c_audio_codec);
	
}

module_init(uda1341_init);
module_exit(uda1341_exit);

MODULE_LICENSE("GPL");

