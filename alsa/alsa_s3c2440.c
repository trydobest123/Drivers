#include <linux/kernel.h>
#include <linux/module.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <linux/platform_device.h>

static struct platform_device *s3c_alsa = NULL;

static int tq2440_alsa_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params)
{
	
	return 0;
}

static struct snd_soc_ops tq2440_snd_ops = {
	.hw_params = tq2440_alsa_hw_params,
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

static struct snd_soc_dai_link jz2440_snd_dai_link = {
	.name = "WM8976",
	.stream_name = "WM8976",
	.codec_name = "wm8976-codec",
	.codec_dai_name = "wm8976-hifi",
	.cpu_dai_name = "s3c2440_i2s",
	.ops = &tq2440_snd_ops,
	.platform_name	= "s3c_audio_dma",
};

static struct snd_soc_card jz2440_snd_dev = {
	.name = "S3C24XX_WM8976",
	.owner = THIS_MODULE,
	.dai_link = &jz2440_snd_dai_link,
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

