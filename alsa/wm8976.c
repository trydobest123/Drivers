#include <linux/kernel.h>
#include <linux/module.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/control.h>
#include <asm/delay.h>
#include <asm/io.h>
#include <linux/platform_device.h>

static volatile unsigned int *gpbcon = NULL;
static volatile unsigned int *gpbdat = NULL;
static volatile unsigned int *gpbup  = NULL;

static struct platform_device *s3c_audio_codec = NULL;

#define WM8976_REG_NUMS 58

#define WM8976_RATES SNDRV_PCM_RATE_8000_48000
#define WM8976_FORMATS (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE | \
		SNDRV_PCM_FMTBIT_S18_3LE | SNDRV_PCM_FMTBIT_S20_3LE)

#define WM8976_RESET        0x0
#define WM8976_POWER1       0x1
#define WM8976_POWER2       0x2
#define WM8976_POWER3       0x3
#define WM8976_IFACE        0x4
#define WM8976_COMP         0x5
#define WM8976_CLOCK        0x6
#define WM8976_ADD          0x7
#define WM8976_GPIO         0x8
#define WM8976_JACK1        0x9
#define WM8976_DAC          0xa
#define WM8976_DACVOLL      0xb
#define WM8976_DACVOLR      0xc
#define WM8976_JACK2        0xd
#define WM8976_ADC          0xe
#define WM8976_ADCVOL       0xf
#define WM8976_EQ1          0x12
#define WM8976_EQ2          0x13
#define WM8976_EQ3          0x14
#define WM8976_EQ4          0x15
#define WM8976_EQ5          0x16
#define WM8976_DACLIM1      0x18
#define WM8976_DACLIM2      0x19
#define WM8976_NOTCH1       0x1b
#define WM8976_NOTCH2       0x1c
#define WM8976_NOTCH3       0x1d
#define WM8976_NOTCH4       0x1e
#define WM8976_ALC1         0x20
#define WM8976_ALC2         0x21
#define WM8976_ALC3         0x22
#define WM8976_NGATE        0x23
#define WM8976_PLLN         0x24
#define WM8976_PLLK1        0x25
#define WM8976_PLLK2        0x26
#define WM8976_PLLK3        0x27
#define WM8976_3D           0x29
#define WM8976_BEEP         0x2b
#define WM8976_INPUT        0x2c
#define WM8976_INPPGA       0x2d
#define WM8976_ADCBOOST     0x2f
#define WM8976_OUTPUT       0x31
#define WM8976_MIXL         0x32
#define WM8976_MIXR         0x33
#define WM8976_HPVOLL       0x34
#define WM8976_HPVOLR       0x35
#define WM8976_SPKVOLL      0x36
#define WM8976_SPKVOLR      0x37
#define WM8976_OUT3MIX      0x38
#define WM8976_MONOMIX      0x39

static unsigned short wm8976_regs[WM8976_REG_NUMS] = {
	0x0000, 0x0000, 0x0000, 0x0000,
    0x0050, 0x0000, 0x0140, 0x0000,
    0x0000, 0x0000, 0x0000, 0x00ff,
    0x00ff, 0x0000, 0x0100, 0x00ff,
    0x00ff, 0x0000, 0x012c, 0x002c,
    0x002c, 0x002c, 0x002c, 0x0000,
    0x0032, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000,
    0x0038, 0x000b, 0x0032, 0x0000,
    0x0008, 0x000c, 0x0093, 0x00e9,
    0x0000, 0x0000, 0x0000, 0x0000,
    0x0033, 0x0010, 0x0010, 0x0100,
    0x0100, 0x0002, 0x0001, 0x0001,
    0x0039, 0x0039, 0x0039, 0x0039,
    0x0001, 0x0001,
};

static void l3_set_clk(unsigned char dat)
{
	if(dat)
		*gpbdat |= (1 << 4);
	else
		*gpbdat &= ~(1 << 4);
}

static void l3_set_dat(unsigned int dat)
{
	if(dat)
		*gpbdat |= (1 << 3);
	else
		*gpbdat &= ~(1 << 3);
}

static void l3_set_cs(unsigned char dat)
{
	if(dat)
		*gpbdat |= (1 << 2);
	else
		*gpbdat &= ~(1 << 2);
}

static void l3_write_data(unsigned int dat,unsigned int addr)
{
	unsigned int i;
	unsigned int data;
	data = (addr << 9) | (dat&0x1ff);
	
	l3_set_clk(1);
	l3_set_cs(1);
	
	for(i = 0; i < 16; i++)
	{
		l3_set_clk(0); 		
		udelay(1);
		l3_set_dat(data&(1<<15)); 
		udelay(1);
		l3_set_clk(1);
		data <<= 1;
	}

	l3_set_cs(0);
	udelay(1);
	l3_set_cs(1);
	l3_set_clk(1);
}

static unsigned int wm8976_read_reg(struct snd_soc_codec *codec, unsigned int reg)
{
	if(reg >= WM8976_REG_NUMS)
		return -EIO;

	return wm8976_regs[reg];
}

static int wm8976_write_reg(struct snd_soc_codec *codec, unsigned int reg, unsigned int value)
{
	if(reg >= WM8976_REG_NUMS)
		return -EIO;

	wm8976_regs[reg] = value;
	l3_write_data(value,reg);
	return 0;
}

static void wm8976_init_regs(void)
{
	wm8976_write_reg(NULL,0, 0);
	wm8976_write_reg(NULL,0x3, 0x6f);
	wm8976_write_reg(NULL,0x1, 0x1f);//biasen,BUFIOEN.VMIDSEL=11b
	wm8976_write_reg(NULL,0x2, 0x185);//ROUT1EN LOUT1EN, inpu PGA enable ,ADC enable
	wm8976_write_reg(NULL,0x6, 0x0);//SYSCLK=MCLK  	
	wm8976_write_reg(NULL,0x4, 0x10);//16bit 			
	wm8976_write_reg(NULL,0x2B,0x10);//BTL OUTPUT  	
	wm8976_write_reg(NULL,0x9, 0x50);//Jack detect enable
	wm8976_write_reg(NULL,0xD, 0x21);//Jack detect
	wm8976_write_reg(NULL,0x7, 0x01);//Jack detect 
}

static int wm8976_startup(struct snd_pcm_substream *substream,struct snd_soc_dai *dai)
{
	/*硬件初始化*/
	wm8976_init_regs();
	return 0;
}

static int wm8976_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params,
	struct snd_soc_dai *dai)
{
	/* 根据params传进来的参数设置硬件 */
	
	return 0;
}

static struct snd_soc_dai_ops wm8976_dai_ops = {
	.startup  = wm8976_startup,
	.hw_params = wm8976_hw_params,
};

static struct snd_soc_dai_driver wm8976_dai = {
	.name = "wm8976-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8976_RATES,
		.formats = WM8976_FORMATS,
	},
	.capture = {
		.stream_name  = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates   = WM8976_RATES,
		.formats = WM8976_FORMATS,
	},
	.ops = &wm8976_dai_ops,
};

static int wm8976_get_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{	
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 63;
	
	return 0;
}

static int wm8976_get_volume(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = wm8976_read_reg(NULL,WM8976_HPVOLL) & ~(1<<8);
	ucontrol->value.integer.value[1] = wm8976_read_reg(NULL,WM8976_HPVOLR) & ~(1<<8);

	return 0;
}

static int wm8976_put_volume(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	unsigned int val;

	val = ucontrol->value.integer.value[0];
	wm8976_write_reg(NULL,52,(1<<8)|val);
	
	val = ucontrol->value.integer.value[1];
	wm8976_write_reg(NULL,53,(1<<8)|val);
	
	return 0;
}

static struct snd_kcontrol_new wm8976_kcontrol = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name  = "Headphone Playback Volume",
	.info  = wm8976_get_info,
	.get   = wm8976_get_volume,
	.put   = wm8976_put_volume,
};

static int wm8976_codec_probe(struct snd_soc_codec *codec)
{
	int ret = 0;
	/* 配置引脚*/
	/* L3MODE GPB2*/
	/* L3DATA GPB3*/
	/* L3CLOCK GPB4 */

	*gpbcon &= ~((3 << 4) | (3 << 6) | (3 << 8));
	*gpbcon |= ((1 << 4) | (1 << 6) | (1 << 8)) ;

	snd_soc_add_codec_controls(codec,&wm8976_kcontrol,1);
	return ret;
}

struct snd_soc_codec_driver wm8976_codec = {
	.probe = wm8976_codec_probe,
	.reg_cache_size = sizeof(wm8976_regs),
	.reg_word_size = sizeof(u8),
	.reg_cache_default = wm8976_regs,
	.reg_cache_step = 1,
	.read = wm8976_read_reg,
	.write = wm8976_write_reg,
};

static int s3c_audio_codec_probe(struct platform_device *pdev)
{
	snd_soc_register_codec(&pdev->dev,&wm8976_codec,&wm8976_dai,1);
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
		.name = "wm8976-codec",
	},
};

static int __init wm8976_init(void)
{
	int ret; 
	s3c_audio_codec = platform_device_alloc("wm8976-codec",-1);
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

static void __exit wm8976_exit(void)
{

	iounmap(gpbcon);
	iounmap(gpbdat);
	iounmap(gpbup);
	platform_driver_unregister(&s3c_audio_codec_drv);
	platform_device_unregister(s3c_audio_codec);
	
}

module_init(wm8976_init);
module_exit(wm8976_exit);

MODULE_LICENSE("GPL");

