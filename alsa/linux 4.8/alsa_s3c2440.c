#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <linux/platform_device.h>
#include <sound/s3c24xx_uda134x.h>

#define S3C2440_IISMOD_MPLL		(1 << 9)
#define S3C2410_IISMOD_SLAVE		(1 << 8)
#define S3C2410_IISMOD_NOXFER		(0 << 6)
#define S3C2410_IISMOD_RXMODE		(1 << 6)
#define S3C2410_IISMOD_TXMODE		(2 << 6)
#define S3C2410_IISMOD_TXRXMODE		(3 << 6)
#define S3C2410_IISMOD_LR_LLOW		(0 << 5)
#define S3C2410_IISMOD_LR_RLOW		(1 << 5)
#define S3C2410_IISMOD_IIS		(0 << 4)
#define S3C2410_IISMOD_MSB		(1 << 4)
#define S3C2410_IISMOD_8BIT		(0 << 3)
#define S3C2410_IISMOD_16BIT		(1 << 3)
#define S3C2410_IISMOD_BITMASK		(1 << 3)
#define S3C2410_IISMOD_256FS		(0 << 2)
#define S3C2410_IISMOD_384FS		(1 << 2)
#define S3C2410_IISMOD_16FS		(0 << 0)
#define S3C2410_IISMOD_32FS		(1 << 0)
#define S3C2410_IISMOD_48FS		(2 << 0)
#define S3C2410_IISMOD_FS_MASK		(3 << 0)

#define S3C24XX_CLKSRC_PCLK 0
#define S3C24XX_CLKSRC_MPLL 1
#define S3C24XX_DIV_MCLK	0
#define S3C24XX_DIV_BCLK	1
#define S3C24XX_DIV_PRESCALER	2

/* prescaler */
#define S3C24XX_PRESCALE(a,b) \
	(((a - 1) << S3C2410_IISPSR_INTSHIFT) | ((b - 1) << S3C2410_IISPSR_EXTSHFIT))
	
#define S3C2410_IISPSR_INTMASK		(31 << 5)
#define S3C2410_IISPSR_INTSHIFT		(5)
#define S3C2410_IISPSR_EXTMASK		(31 << 0)
#define S3C2410_IISPSR_EXTSHFIT		(0)

static int clk_users;
static DEFINE_MUTEX(clk_lock);
static struct clk *xtal;
static struct clk *pclk;
static unsigned int rates[33 * 2];

static struct platform_device *s3c_alsa = NULL;
static int s3c24xx_uda1341_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
#ifdef ENFORCE_RATES
	struct snd_pcm_runtime *runtime = substream->runtime;
#endif
	int ret = 0;

	mutex_lock(&clk_lock);
	pclk = clk_get(cpu_dai->dev, "iis");
	if (IS_ERR(pclk)) {
		printk(KERN_ERR "%s cannot get pclk\n",
		       __func__);
		clk_put(xtal);
		ret = PTR_ERR(pclk);
	}
	if (!ret) {
		int i, j;

		for (i = 0; i < 2; i++) {
			int fs = i ? 256 : 384;

			rates[i*33] = clk_get_rate(xtal) / fs;
			for (j = 1; j < 33; j++)
				rates[i*33 + j] = clk_get_rate(pclk) /
					(j * fs);
		}
	}
	clk_users += 1;
	mutex_unlock(&clk_lock);
	
	return ret;
}

static int s3c24xx_uda1341_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int clk = 0;
	int ret = 0;
	int clk_source, fs_mode;
	unsigned long rate = params_rate(params);
	long err, cerr;
	unsigned int div;
	int i, bi;

	err = 999999;
	bi = 0;
	for (i = 0; i < 2*33; i++) {
		cerr = rates[i] - rate;
		if (cerr < 0)
			cerr = -cerr;
		if (cerr < err) {
			err = cerr;
			bi = i;
		}
	}
	if (bi / 33 == 1)
		fs_mode = S3C2410_IISMOD_256FS;
	else
		fs_mode = S3C2410_IISMOD_384FS;
	if (bi % 33 == 0) {
		clk_source = S3C24XX_CLKSRC_MPLL;
		div = 1;
	} else {
		clk_source = S3C24XX_CLKSRC_PCLK;
		div = bi % 33;
	}

	clk = (fs_mode == S3C2410_IISMOD_384FS ? 384 : 256) * rate;

	ret = snd_soc_dai_set_clkdiv(cpu_dai, S3C24XX_DIV_MCLK, fs_mode);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_clkdiv(cpu_dai, S3C24XX_DIV_BCLK,
			S3C2410_IISMOD_32FS);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_clkdiv(cpu_dai, S3C24XX_DIV_PRESCALER,
			S3C24XX_PRESCALE(div, div));
	if (ret < 0)
		return ret;
		
	return 0;
}


static struct snd_soc_ops tq2440_snd_ops = {
	.startup   = s3c24xx_uda1341_startup,
	.hw_params = s3c24xx_uda1341_hw_params,
};

static struct snd_soc_dai_link tq2440_snd_dai_link = {
	.name = "UDA134X",
	.stream_name = "UDA134X",
	.codec_name = "uda1341-codec",
	.codec_dai_name = "uda1341-hifi",
	.cpu_dai_name = "s3c2440_i2s",
	.ops = &tq2440_snd_ops,
	.platform_name	= "s3c_audio_dma",
};

static struct snd_soc_card tq2440_snd_dev = {
	.name = "S3C24XX_UDA1341",
	.owner = THIS_MODULE,
	.dai_link = &tq2440_snd_dai_link,
	.num_links = 1,
};

static struct snd_soc_dai_link s3c24xx_wm8976_dai_link = {
	.name = "WM8976",
	.stream_name = "WM8976",
	.codec_name = "wm8976-codec",
	.codec_dai_name = "wm8976-hifi",
	.cpu_dai_name = "s3c24xx-iis",
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		   SND_SOC_DAIFMT_CBS_CFS,
	.ops = &tq2440_snd_ops,
	.platform_name	= "s3c24xx-iis",
};

static struct snd_soc_card jz2440_snd_dev = {
	.name = "S3C24XX_WM8976",
	.owner = THIS_MODULE,
	.dai_link = &s3c24xx_wm8976_dai_link,
	.num_links = 1,
};

static int __init s3c_alsa_init(void)
{
	int ret;
	
	s3c_alsa = platform_device_alloc("soc-audio", -1);
	if (!s3c_alsa) {
		printk(KERN_ERR "S3C24XX_UDA134X SoC Audio: "
		       "Unable to register\n");
		return -ENOMEM;
	}

	platform_set_drvdata(s3c_alsa,&jz2440_snd_dev);
	ret = platform_device_add(s3c_alsa);
	if (ret) {
		printk(KERN_ERR "S3C24XX_UDA134X SoC Audio: Unable to add\n");
		platform_device_put(s3c_alsa);
	}

	return ret;
}

module_init(s3c_alsa_init);

MODULE_LICENSE("GPL");

