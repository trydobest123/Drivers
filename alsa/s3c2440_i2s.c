#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/soc-dai.h>
#include <sound/pcm_params.h>
#include <linux/platform_device.h>

#define S3C2440_I2S_RATES \
	(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 | SNDRV_PCM_RATE_16000 | \
	SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
	SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000)

struct s3c_i2s_reg{
	unsigned int iiscon;
	unsigned int iismod;
	unsigned int iispsr;
	unsigned int iisfcon;
	unsigned int iisfifo;
};

static volatile struct s3c_i2s_reg *i2s_reg = NULL;
static volatile unsigned int *gpecon = NULL;
static struct platform_device *s3c_i2s_device = NULL;
static struct clk *i2s_clk = NULL;

static unsigned int FreqTab[7][2]={
	{8000,3072000},
	{11025,4233600},
	{16000,6144000},
	{22050,8467200},
	{32000,12288000},
	{44100,16934400},
    {48000,18432000}
};

static int s3c2440_i2s_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	unsigned int fs,tmp_fs_int,tmp_fs_float;
	unsigned int i;
	unsigned int rate,pre;
	unsigned char found_freq = 0;
		
	struct clk *pclk;
	pclk = clk_get(NULL,"pclk");

	rate = clk_get_rate(pclk);
	/* 根据params的值设置硬件 */
	/* 设置数据格式 */
	if(params_format(params) == SNDRV_PCM_FORMAT_S16_LE)
	{
		i2s_reg->iiscon = (0x1<<5)|(0x1<<4)|(0x1<<1);
		i2s_reg->iismod = (0x3<<6)|(0x1<<5)|(0x1<<3)|(0x1<<2)|(0x1<<0);
	}
	else if(params_format(params) == SNDRV_PCM_FORMAT_S8)
	{
		i2s_reg->iiscon = (0x1<<5)|(0x1<<4)|(0x1<<1);
		i2s_reg->iismod = (0x3<<6)|(0x1<<5)|(0x1<<2)|(0x1<<0);
	}
	else
		return -EINVAL;
		
	  /*设置采样率 */
		fs = params_rate(params);
		for(i = 0;i < 7; i++)
		{
			if(fs == FreqTab[i][0])
			{	
				tmp_fs_int   = rate / FreqTab[i][1];
				tmp_fs_float = rate % FreqTab[i][1];
				if(tmp_fs_float > 500000) //取四舍五入
					tmp_fs_int += 1;
				pre = tmp_fs_int - 1;
				found_freq = 1;
				i2s_reg->iispsr = (pre<<5)|(pre);
		    }
		}

		if(!found_freq)
		{
			clk_put(pclk);
			return -EINVAL;
		}

		i2s_reg->iisfcon = (1<<15) | (1<<14) | (1<<13) | (1<<12);
		
		clk_put(pclk);
		
		return 0;
} 


static void s3c_i2s_start(void)
{
	i2s_reg->iiscon |= 0x1;	
}

static void s3c_i2s_stop(void)
{
	i2s_reg->iiscon &= ~(0x1);
}

static int s3c2440_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			       struct snd_soc_dai *dai)
{
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		s3c_i2s_start();
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		s3c_i2s_stop();
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}


static int s3c2440_i2s_probe(struct snd_soc_dai *dai)
{
	/* 设置时钟 */
	i2s_clk = clk_get(NULL,"iis");
	if(!i2s_clk)
	{
		printk("can't get i2s_clk.\n");
		return -EIO;
	}
	clk_enable(i2s_clk);
	
	/* 设置引脚 */
	*gpecon &= ~((3<<0)|(3<<2)|(3<<4)|(3<<6)|(3<<8));
	*gpecon |= ((2<<0)|(2<<2)|(2<<4)|(2<<6)|(2<<8));
	return 0;
}

static struct snd_soc_dai_ops s3c2440_i2s_dai_ops = {
	.trigger   = s3c2440_i2s_trigger,
	.hw_params = s3c2440_i2s_hw_params,
};

static struct snd_soc_dai_driver s3c_i2s_dai = {
	.probe = s3c2440_i2s_probe,
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = S3C2440_I2S_RATES,
		.formats = SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE,},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = S3C2440_I2S_RATES,
		.formats = SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE,},
	.ops = &s3c2440_i2s_dai_ops,
};

static int s3c_i2s_probe(struct platform_device *pdev)
{
	snd_soc_register_dai(&pdev->dev,&s3c_i2s_dai);

	return 0;
}

static int s3c_i2s_remove(struct platform_device *pdev)
{
	snd_soc_unregister_dai(&pdev->dev);
	return 0;
}

static struct platform_driver s3c_i2s_driver = {
	.probe  = s3c_i2s_probe,
	.remove = s3c_i2s_remove,
	.driver = {
		.name = "s3c2440_i2s",
	},
};

static int __init s3c_i2s_init(void)
{	
	int ret;
	s3c_i2s_device = platform_device_alloc("s3c2440_i2s",-1);
	if(!s3c_i2s_device)
		return -ENODEV;
		
	ret = platform_device_add(s3c_i2s_device);
	if(ret)
	{
		platform_device_put(s3c_i2s_device);
		return -ENODEV;
	}

	i2s_reg = ioremap(0x55000000,sizeof(struct s3c_i2s_reg));
	gpecon  = ioremap(0x56000040,4);
	platform_driver_register(&s3c_i2s_driver);

	return 0;
}

static void __exit s3c_i2s_exit(void)
{
	platform_driver_unregister(&s3c_i2s_driver);
	platform_device_unregister(s3c_i2s_device);
	iounmap(i2s_reg);
	iounmap(gpecon);
}

module_init(s3c_i2s_init);
module_exit(s3c_i2s_exit);

MODULE_LICENSE("GPL");

