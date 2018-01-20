#include <linux/kernel.h>
#include <linux/module.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/soc-dai.h>
#include <sound/asound.h>
#include <sound/pcm_params.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <asm/io.h>

struct {
	unsigned char *addr_vir;
	unsigned int addr_phy;
	unsigned int size;
	unsigned int buf_size;
	unsigned int period_size;
	unsigned int dma_ofs;
	unsigned int be_running;
}playback_dma_info;

struct s3c_dma_regs{
	unsigned int disrc;  
	unsigned int disrcc;  
	unsigned int didst;   
	unsigned int didstc;  
	unsigned int dcon;    
	unsigned int dstat;   
	unsigned int dcsrc;   
	unsigned int dcdst;   
	unsigned int dmasktrig;
};

static struct platform_device *s3c2440_audio_dma = NULL;
static volatile struct s3c_dma_regs *s3c_dma_reg = NULL;

static const struct snd_pcm_hardware s3c_dma_hardware = {
	.info			= SNDRV_PCM_INFO_INTERLEAVED |
				    SNDRV_PCM_INFO_BLOCK_TRANSFER |
				    SNDRV_PCM_INFO_MMAP |
				    SNDRV_PCM_INFO_MMAP_VALID,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_U16_LE |
				    SNDRV_PCM_FMTBIT_U8 |
				    SNDRV_PCM_FMTBIT_S8,
	.channels_min		= 2,
	.channels_max		= 2,
	.buffer_bytes_max	= 128*1024,
	.period_bytes_min	= PAGE_SIZE,
	.period_bytes_max	= PAGE_SIZE*2,
	.periods_min		= 2,
	.periods_max		= 128,
	.fifo_size			= 32,
};
static void s3c_dma2_start(void)
{
	s3c_dma_reg->dmasktrig = (1<<1); 
}

static void s3c_dma2_stop(void)
{
	s3c_dma_reg->dmasktrig = (0<<1) | (1<<2); 
}

static void dma_load_period(void)
{
	s3c_dma_reg->disrcc = (0<<1) | (0<<0);
	s3c_dma_reg->disrc  = playback_dma_info.addr_phy + playback_dma_info.dma_ofs;
	s3c_dma_reg->didstc = (0<<2) | (1<<1) | (1<<0);
	s3c_dma_reg->didst  = 0x55000010;
	s3c_dma_reg->dcon   = ((1<<31) | (0<<30) | (1<<29) | (0<<28) | (0<<27) | (0<<24) |\
						  (1<<23) | (1<<22) | (1<<20) | (playback_dma_info.period_size/2));
}

static irqreturn_t s3c2440_dma2_irq(int irq, void *devid)
{
	struct snd_pcm_substream *substream = devid;

	playback_dma_info.dma_ofs += playback_dma_info.period_size;
	/*  更新状态信息*/
	if(playback_dma_info.dma_ofs >= playback_dma_info.buf_size)
		playback_dma_info.dma_ofs = 0;
	snd_pcm_period_elapsed(substream);

	if(playback_dma_info.be_running)
	{
		dma_load_period();
		s3c_dma2_start();
	}
	else
		playback_dma_info.dma_ofs = 0;	
	
	return 	IRQ_HANDLED;
}

static int s3c_dma_hw_params(struct snd_pcm_substream *substream,struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned long totbytes = params_buffer_bytes(params);
	
	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	runtime->dma_bytes = totbytes;
	playback_dma_info.buf_size    = totbytes;
	playback_dma_info.period_size =  params_period_bytes(params);
	
	return 0;
}

static int s3c_dma_prepare(struct snd_pcm_substream *substream)
{
	/* 更新信息 */
	playback_dma_info.dma_ofs = 0;
	playback_dma_info.be_running = 0;
	/* 设置dma */
	dma_load_period();
	return 0;
}

static int s3c_dma_trigger(struct snd_pcm_substream *substream, int cmd)
{
	int ret = 0;
	
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		/* 启动传输 */
		playback_dma_info.be_running = 1;
		s3c_dma2_start();
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		/* 停止传输 */
		playback_dma_info.be_running = 0;
		s3c_dma2_stop();
		break;

	default:
		ret = -EINVAL;
		break;
	}
	
	return ret;
}

static int s3c_dma_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	snd_soc_set_runtime_hwparams(substream, &s3c_dma_hardware);
	
	if(request_irq(IRQ_DMA2,s3c2440_dma2_irq,0,"s3c_dma2",substream))
	{
		printk("request irq error.\n");
		return -EIO;
	}
	
	return 0;
}

static int s3c_dma_close(struct snd_pcm_substream *substream)
{
	free_irq(IRQ_DMA2,substream);
	return 0;
}

static u64 dma_mask = DMA_BIT_MASK(32);

static int s3c_dma_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;
	struct snd_pcm_substream *substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;

	size_t size = s3c_dma_hardware.buffer_bytes_max;
	
	if (!card->dev->dma_mask)
		card->dev->dma_mask = &dma_mask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = DMA_BIT_MASK(32);
	
	/* 分配一个dma buffer */
	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) 
	{
		playback_dma_info.addr_vir = dma_alloc_writecombine(pcm->card->dev, size,
						   &playback_dma_info.addr_phy, GFP_KERNEL);
		if (!playback_dma_info.addr_vir)
		{
			printk("alloc dma buffer error.\n");
			return -ENOMEM;
		}
		
		buf->dev.type = SNDRV_DMA_TYPE_DEV;
		buf->dev.dev = pcm->card->dev;
		buf->private_data = NULL;
		buf->area = playback_dma_info.addr_vir;
		buf->bytes = size;
		playback_dma_info.size = size;
	}

	return 0;
}

static snd_pcm_uframes_t s3c_dma_pointer(struct snd_pcm_substream *substream)
{
	if (playback_dma_info.dma_ofs >= snd_pcm_lib_buffer_bytes(substream)) {
		if (playback_dma_info.dma_ofs == snd_pcm_lib_buffer_bytes(substream))
			playback_dma_info.dma_ofs = 0;
	}

	return bytes_to_frames(substream->runtime, playback_dma_info.dma_ofs);
}

static void s3c_dma_free(struct snd_pcm *pcm)
{	
	struct snd_pcm_substream *substream;
	substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;

	dma_free_writecombine(pcm->card->dev, playback_dma_info.size,
						playback_dma_info.addr_vir, playback_dma_info.addr_phy);
}

static struct snd_pcm_ops s3c_dma_ops = {
	.open		= s3c_dma_open,
	.close      = s3c_dma_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= s3c_dma_hw_params,
	.prepare    = s3c_dma_prepare,
	.trigger    = s3c_dma_trigger,
	.pointer    = s3c_dma_pointer,
};

static struct snd_soc_platform_driver s3c_audio_dma = {
	.ops		= &s3c_dma_ops,
	.pcm_new	= s3c_dma_new,
	.pcm_free   = s3c_dma_free,
};

static int s3c2440_audio_dma_probe(struct platform_device *pdev)
{
	snd_soc_register_platform(&pdev->dev,&s3c_audio_dma);
	return 0;
}

static int s3c2440_audio_dma_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}
	
static struct platform_driver s3c2440_audio_dma_driver = {
	.probe  = s3c2440_audio_dma_probe,
	.remove = s3c2440_audio_dma_remove,
	.driver ={
		.name = "s3c_audio_dma",
	},
};

static int __init s3c_dma_init(void)
{
	int ret;
	
	s3c2440_audio_dma = platform_device_alloc("s3c_audio_dma",-1);
	if(!s3c2440_audio_dma)
		return -ENOMEM;
	ret = platform_device_add(s3c2440_audio_dma);
	if(ret)
	{
		platform_device_put(s3c2440_audio_dma);
		return -ENODEV;
	}

	s3c_dma_reg = ioremap(0x4B000080,sizeof(struct s3c_dma_regs));
	platform_driver_register(&s3c2440_audio_dma_driver);
	return ret;
}

static void __exit s3c_dma_exit(void)
{
	iounmap(s3c_dma_reg);
	platform_device_unregister(s3c2440_audio_dma);
	platform_driver_unregister(&s3c2440_audio_dma_driver);
}

module_init(s3c_dma_init);
module_exit(s3c_dma_exit);

MODULE_LICENSE("GPL");

