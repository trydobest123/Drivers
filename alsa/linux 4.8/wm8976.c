/*
 * wm8976.c  --  WM8976 ALSA Soc Audio driver
 *
 * Copyright 2007-9 Wolfson Microelectronics PLC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/soc-dai.h>

#define WM8976_RESET		0x0
#define WM8976_POWER1		0x1
#define WM8976_POWER2		0x2
#define WM8976_POWER3		0x3
#define WM8976_IFACE		0x4
#define WM8976_COMP			0x5
#define WM8976_CLOCK		0x6
#define WM8976_ADD			0x7
#define WM8976_GPIO			0x8
#define WM8976_JACK1        0x9
#define WM8976_DAC			0xa
#define WM8976_DACVOLL	    0xb
#define WM8976_DACVOLR      0xc
#define WM8976_JACK2        0xd
#define WM8976_ADC			0xe
#define WM8976_ADCVOL		0xf
#define WM8976_EQ1			0x12
#define WM8976_EQ2			0x13
#define WM8976_EQ3			0x14
#define WM8976_EQ4			0x15
#define WM8976_EQ5			0x16
#define WM8976_DACLIM1		0x18
#define WM8976_DACLIM2		0x19
#define WM8976_NOTCH1		0x1b
#define WM8976_NOTCH2		0x1c
#define WM8976_NOTCH3		0x1d
#define WM8976_NOTCH4		0x1e
#define WM8976_ALC1			0x20
#define WM8976_ALC2			0x21
#define WM8976_ALC3			0x22
#define WM8976_NGATE		0x23
#define WM8976_PLLN			0x24
#define WM8976_PLLK1		0x25
#define WM8976_PLLK2		0x26
#define WM8976_PLLK3		0x27
#define WM8976_3D           0x29
#define WM8976_BEEP         0x2b
#define WM8976_INPUT		0x2c
#define WM8976_INPPGA	  	0x2d
#define WM8976_ADCBOOST		0x2f
#define WM8976_OUTPUT		0x31
#define WM8976_MIXL	        0x32
#define WM8976_MIXR         0x33
#define WM8976_HPVOLL		0x34
#define WM8976_HPVOLR       0x35
#define WM8976_SPKVOLL      0x36
#define WM8976_SPKVOLR      0x37
#define WM8976_OUT3MIX		0x38
#define WM8976_MONOMIX      0x39

#define WM8976_CACHEREGNUM 	58

/*
 * WM8976 Clock dividers
 */
#define WM8976_MCLKDIV 		0
#define WM8976_BCLKDIV		1
#define WM8976_OPCLKDIV		2
#define WM8976_DACOSR		3
#define WM8976_ADCOSR		4
#define WM8976_MCLKSEL		5

#define WM8976_MCLK_MCLK		(0 << 8)
#define WM8976_MCLK_PLL			(1 << 8)

#define WM8976_MCLK_DIV_1		(0 << 5)
#define WM8976_MCLK_DIV_1_5		(1 << 5)
#define WM8976_MCLK_DIV_2		(2 << 5)
#define WM8976_MCLK_DIV_3		(3 << 5)
#define WM8976_MCLK_DIV_4		(4 << 5)
#define WM8976_MCLK_DIV_5_5		(5 << 5)
#define WM8976_MCLK_DIV_6		(6 << 5)

#define WM8976_BCLK_DIV_1		(0 << 2)
#define WM8976_BCLK_DIV_2		(1 << 2)
#define WM8976_BCLK_DIV_4		(2 << 2)
#define WM8976_BCLK_DIV_8		(3 << 2)
#define WM8976_BCLK_DIV_16		(4 << 2)
#define WM8976_BCLK_DIV_32		(5 << 2)

#define WM8976_DACOSR_64		(0 << 3)
#define WM8976_DACOSR_128		(1 << 3)

#define WM8976_ADCOSR_64		(0 << 3)
#define WM8976_ADCOSR_128		(1 << 3)

#define WM8976_OPCLK_DIV_1		(0 << 4)
#define WM8976_OPCLK_DIV_2		(1 << 4)
#define WM8976_OPCLK_DIV_3		(2 << 4)
#define WM8976_OPCLK_DIV_4		(3 << 4)

static struct platform_device *wm8976_platform = NULL;
static volatile unsigned int *gpbcon = NULL;
static volatile unsigned int *gpbdat = NULL;
static volatile unsigned int *gpbup  = NULL;

/*
 * wm8976 register cache
 * We can't read the WM8976 register space when we are
 * using 2 wire for device control, so we cache them instead.
 */
static const u16 wm8976_reg[WM8976_CACHEREGNUM] = {
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
	data = (addr << 9)|(dat&0x1ff);
	
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

/*
 * read wm8976 register cache
 */
static inline unsigned int wm8976_read_reg_cache(struct snd_soc_codec  *codec,
	unsigned int reg)
{
	u16 *cache = codec->reg_cache;
	if (reg == WM8976_RESET)
		return 0;
	if (reg >= WM8976_CACHEREGNUM)
		return -1;
	return cache[reg];
}

/*
 * write wm8976 register cache
 */
static inline void wm8976_write_reg_reg_cache(struct snd_soc_codec  *codec,
	u16 reg, unsigned int value)
{
	u16 *cache = codec->reg_cache;
	if (reg >= WM8976_CACHEREGNUM)
		return;
	cache[reg] = value;
}

/*
 * write to the WM8976 register space
 */
static int wm8976_write_reg(struct snd_soc_codec  *codec, unsigned int reg,
	unsigned int value)
{
	u8 data[2];

	/* data is
	 *   D15..D9 WM8976 register offset
	 *   D8...D0 register data
	 */
	data[0] = (reg << 1) | ((value >> 8) & 0x0001);
	data[1] = value & 0x00ff;

	wm8976_write_reg_reg_cache (codec, reg, value);

	if(reg == WM8976_HPVOLL || reg == WM8976_HPVOLR)
		value |= (1<<8);
		
	l3_write_data(value,reg);
	
	return 0;
}

#define wm8976_reset(c)	wm8976_write_reg(c, WM8976_RESET, 0)

static const char *wm8976_companding[] = {"Off", "NC", "u-law", "A-law" };
static const char *wm8976_deemp[] = {"None", "32kHz", "44.1kHz", "48kHz" };
static const char *wm8976_eqmode[] = {"Capture", "Playback" };
static const char *wm8976_bw[] = {"Narrow", "Wide" };
static const char *wm8976_eq1[] = {"80Hz", "105Hz", "135Hz", "175Hz" };
static const char *wm8976_eq2[] = {"230Hz", "300Hz", "385Hz", "500Hz" };
static const char *wm8976_eq3[] = {"650Hz", "850Hz", "1.1kHz", "1.4kHz" };
static const char *wm8976_eq4[] = {"1.8kHz", "2.4kHz", "3.2kHz", "4.1kHz" };
static const char *wm8976_eq5[] = {"5.3kHz", "6.9kHz", "9kHz", "11.7kHz" };
static const char *wm8976_alc[] =
    {"ALC both on", "ALC left only", "ALC right only", "Limiter" };

static const struct soc_enum wm8976_enum[] = {
	SOC_ENUM_SINGLE(WM8976_COMP, 1, 4, wm8976_companding), /* adc */
	SOC_ENUM_SINGLE(WM8976_COMP, 3, 4, wm8976_companding), /* dac */
	SOC_ENUM_SINGLE(WM8976_DAC,  4, 4, wm8976_deemp),
	SOC_ENUM_SINGLE(WM8976_EQ1,  8, 2, wm8976_eqmode),

	SOC_ENUM_SINGLE(WM8976_EQ1,  5, 4, wm8976_eq1),
	SOC_ENUM_SINGLE(WM8976_EQ2,  8, 2, wm8976_bw),
	SOC_ENUM_SINGLE(WM8976_EQ2,  5, 4, wm8976_eq2),
	SOC_ENUM_SINGLE(WM8976_EQ3,  8, 2, wm8976_bw),

	SOC_ENUM_SINGLE(WM8976_EQ3,  5, 4, wm8976_eq3),
	SOC_ENUM_SINGLE(WM8976_EQ4,  8, 2, wm8976_bw),
	SOC_ENUM_SINGLE(WM8976_EQ4,  5, 4, wm8976_eq4),
	SOC_ENUM_SINGLE(WM8976_EQ5,  8, 2, wm8976_bw),

	SOC_ENUM_SINGLE(WM8976_EQ5,  5, 4, wm8976_eq5),
	SOC_ENUM_SINGLE(WM8976_ALC3,  8, 2, wm8976_alc),
};

static const struct snd_kcontrol_new wm8976_snd_controls[] = {
SOC_SINGLE("Digital Loopback Switch", WM8976_COMP, 0, 1, 0),

SOC_ENUM("ADC Companding", wm8976_enum[0]),
SOC_ENUM("DAC Companding", wm8976_enum[1]),

SOC_SINGLE("Jack Detection Enable", WM8976_JACK1, 6, 1, 0),

SOC_DOUBLE("DAC Inversion Switch", WM8976_DAC, 0, 1, 1, 0),

SOC_SINGLE("High Pass Filter Switch", WM8976_ADC, 8, 1, 0),
SOC_SINGLE("High Pass Cut Off", WM8976_ADC, 4, 7, 0),

SOC_DOUBLE("ADC Inversion Switch", WM8976_ADC, 0, 1, 1, 0),

SOC_SINGLE("Capture Volume", WM8976_ADCVOL,  0, 127, 0),

SOC_ENUM("Equaliser Function", wm8976_enum[3]),
SOC_ENUM("EQ1 Cut Off", wm8976_enum[4]),
SOC_SINGLE("EQ1 Volume", WM8976_EQ1,  0, 31, 1),

SOC_ENUM("Equaliser EQ2 Bandwith", wm8976_enum[5]),
SOC_ENUM("EQ2 Cut Off", wm8976_enum[6]),
SOC_SINGLE("EQ2 Volume", WM8976_EQ2,  0, 31, 1),

SOC_ENUM("Equaliser EQ3 Bandwith", wm8976_enum[7]),
SOC_ENUM("EQ3 Cut Off", wm8976_enum[8]),
SOC_SINGLE("EQ3 Volume", WM8976_EQ3,  0, 31, 1),

SOC_ENUM("Equaliser EQ4 Bandwith", wm8976_enum[9]),
SOC_ENUM("EQ4 Cut Off", wm8976_enum[10]),
SOC_SINGLE("EQ4 Volume", WM8976_EQ4,  0, 31, 1),

SOC_ENUM("Equaliser EQ5 Bandwith", wm8976_enum[11]),
SOC_ENUM("EQ5 Cut Off", wm8976_enum[12]),
SOC_SINGLE("EQ5 Volume", WM8976_EQ5,  0, 31, 1),

SOC_SINGLE("DAC Playback Limiter Switch", WM8976_DACLIM1,  8, 1, 0),
SOC_SINGLE("DAC Playback Limiter Decay", WM8976_DACLIM1,  4, 15, 0),
SOC_SINGLE("DAC Playback Limiter Attack", WM8976_DACLIM1,  0, 15, 0),

SOC_SINGLE("DAC Playback Limiter Threshold", WM8976_DACLIM2,  4, 7, 0),
SOC_SINGLE("DAC Playback Limiter Boost", WM8976_DACLIM2,  0, 15, 0),

SOC_SINGLE("ALC Enable Switch", WM8976_ALC1,  8, 1, 0),
SOC_SINGLE("ALC Capture Max Gain", WM8976_ALC1,  3, 7, 0),
SOC_SINGLE("ALC Capture Min Gain", WM8976_ALC1,  0, 7, 0),

SOC_SINGLE("ALC Capture ZC Switch", WM8976_ALC2,  8, 1, 0),
SOC_SINGLE("ALC Capture Hold", WM8976_ALC2,  4, 7, 0),
SOC_SINGLE("ALC Capture Target", WM8976_ALC2,  0, 15, 0),

SOC_ENUM("ALC Capture Mode", wm8976_enum[13]),
SOC_SINGLE("ALC Capture Decay", WM8976_ALC3,  4, 15, 0),
SOC_SINGLE("ALC Capture Attack", WM8976_ALC3,  0, 15, 0),

SOC_SINGLE("ALC Capture Noise Gate Switch", WM8976_NGATE,  3, 1, 0),
SOC_SINGLE("ALC Capture Noise Gate Threshold", WM8976_NGATE,  0, 7, 0),

SOC_SINGLE("Capture PGA ZC Switch", WM8976_INPPGA,  7, 1, 0),
SOC_SINGLE("Capture PGA Volume", WM8976_INPPGA,  0, 63, 0),

SOC_DOUBLE_R("Headphone Playback ZC Switch", WM8976_HPVOLL,  WM8976_HPVOLR, 7, 1, 0),
SOC_DOUBLE_R("Headphone Playback Switch", WM8976_HPVOLL,  WM8976_HPVOLR, 6, 1, 1),
SOC_DOUBLE_R("Headphone Playback Volume", WM8976_HPVOLL,  WM8976_HPVOLR, 0, 63, 0),

SOC_DOUBLE_R("Speaker Playback ZC Switch", WM8976_SPKVOLL,  WM8976_SPKVOLR, 7, 1, 0),
SOC_DOUBLE_R("Speaker Playback Switch", WM8976_SPKVOLL,  WM8976_SPKVOLR, 6, 1, 1),
SOC_DOUBLE_R("Speaker Playback Volume", WM8976_SPKVOLL,  WM8976_SPKVOLR, 0, 63, 0),

SOC_SINGLE("Capture Boost(+20dB)", WM8976_ADCBOOST, 8, 1, 0),
};

/* Left Output Mixer */
static const struct snd_kcontrol_new wm8976_left_mixer_controls[] = {
SOC_DAPM_SINGLE("Right PCM Playback Switch", WM8976_OUTPUT, 6, 1, 1),
SOC_DAPM_SINGLE("Left PCM Playback Switch", WM8976_MIXL, 0, 1, 1),
SOC_DAPM_SINGLE("Line Bypass Switch", WM8976_MIXL, 1, 1, 0),
SOC_DAPM_SINGLE("Aux Playback Switch", WM8976_MIXL, 5, 1, 0),
};

/* Right Output Mixer */
static const struct snd_kcontrol_new wm8976_right_mixer_controls[] = {
SOC_DAPM_SINGLE("Left PCM Playback Switch", WM8976_OUTPUT, 5, 1, 1),
SOC_DAPM_SINGLE("Right PCM Playback Switch", WM8976_MIXR, 0, 1, 1),
SOC_DAPM_SINGLE("Line Bypass Switch", WM8976_MIXR, 1, 1, 0),
SOC_DAPM_SINGLE("Aux Playback Switch", WM8976_MIXR, 5, 1, 0),
};

/* Left AUX Input boost vol */
static const struct snd_kcontrol_new wm8976_laux_boost_controls =
SOC_DAPM_SINGLE("Aux Volume", WM8976_ADCBOOST, 0, 3, 0);

/* Left Input boost vol */
static const struct snd_kcontrol_new wm8976_lmic_boost_controls =
SOC_DAPM_SINGLE("Input Volume", WM8976_ADCBOOST, 4, 3, 0);

/* Left Aux In to PGA */
static const struct snd_kcontrol_new wm8976_laux_capture_boost_controls =
SOC_DAPM_SINGLE("Capture Switch", WM8976_ADCBOOST,  8, 1, 0);

/* Left Input P In to PGA */
static const struct snd_kcontrol_new wm8976_lmicp_capture_boost_controls =
SOC_DAPM_SINGLE("Input P Capture Boost Switch", WM8976_INPUT,  0, 1, 0);

/* Left Input N In to PGA */
static const struct snd_kcontrol_new wm8976_lmicn_capture_boost_controls =
SOC_DAPM_SINGLE("Input N Capture Boost Switch", WM8976_INPUT,  1, 1, 0);

struct _pll_div {
	unsigned int pre:4; /* prescale - 1 */
	unsigned int n:4;
	unsigned int k;
};

static struct _pll_div pll_div;

/* The size in bits of the pll divide multiplied by 10
 * to allow rounding later */
#define FIXED_PLL_SIZE ((1 << 24) * 10)

static void pll_factors(unsigned int target, unsigned int source)
{
	unsigned long long Kpart;
	unsigned int K, Ndiv, Nmod;

	Ndiv = target / source;
	if (Ndiv < 6) {
		source >>= 1;
		pll_div.pre = 1;
		Ndiv = target / source;
	} else
		pll_div.pre = 0;

	if ((Ndiv < 6) || (Ndiv > 12))
		printk(KERN_WARNING
			"WM8976 N value outwith recommended range! N = %d\n",Ndiv);

	pll_div.n = Ndiv;
	Nmod = target % source;
	Kpart = FIXED_PLL_SIZE * (long long)Nmod;

	do_div(Kpart, source);

	K = Kpart & 0xFFFFFFFF;

	/* Check if we need to round */
	if ((K % 10) >= 5)
		K += 5;

	/* Move down to proper range now rounding is done */
	K /= 10;

	pll_div.k = K;
}

static int wm8976_set_dai_pll(struct snd_soc_dai *codec_dai,
		int pll_id, int source,unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 reg;

	if(freq_in == 0 || freq_out == 0) {
		reg = wm8976_read_reg_cache(codec, WM8976_POWER1);
		wm8976_write_reg(codec, WM8976_POWER1, reg & 0x1df);
		return 0;
	}

	pll_factors(freq_out * 8, freq_in);

	wm8976_write_reg(codec, WM8976_PLLN, (pll_div.pre << 4) | pll_div.n);
	wm8976_write_reg(codec, WM8976_PLLK1, pll_div.k >> 18);
	wm8976_write_reg(codec, WM8976_PLLK1, (pll_div.k >> 9) && 0x1ff);
	wm8976_write_reg(codec, WM8976_PLLK1, pll_div.k && 0x1ff);
	reg = wm8976_read_reg_cache(codec, WM8976_POWER1);
	wm8976_write_reg(codec, WM8976_POWER1, reg | 0x020);
	
	
	return 0;
}

static int wm8976_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 iface = wm8976_read_reg_cache(codec, WM8976_IFACE) & 0x3;
	u16 clk = wm8976_read_reg_cache(codec, WM8976_CLOCK) & 0xfffe;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		clk |= 0x0001;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= 0x0010;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= 0x0008;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface |= 0x00018;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface |= 0x0180;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= 0x0100;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		iface |= 0x0080;
		break;
	default:
		return -EINVAL;
	}

	wm8976_write_reg(codec, WM8976_IFACE, iface);
	wm8976_write_reg(codec, WM8976_CLOCK, clk);

	return 0;
}

static void wm8976_init_regs(struct snd_soc_codec *codec)
{
	wm8976_write_reg(codec,0, 0);
	wm8976_write_reg(codec,0x3, 0x6f);
	wm8976_write_reg(codec,0x1, 0x1f);//biasen,BUFIOEN.VMIDSEL=11b
	wm8976_write_reg(codec,0x2, 0x185);//ROUT1EN LOUT1EN, inpu PGA enable ,ADC enable
	wm8976_write_reg(codec,0x6, 0x0);//SYSCLK=MCLK  	
	wm8976_write_reg(codec,0x4, 0x10);//16bit 			
	wm8976_write_reg(codec,0x2B,0x10);//BTL OUTPUT  	
	wm8976_write_reg(codec,0x9, 0x50);//Jack detect enable
	wm8976_write_reg(codec,0xD, 0x21);//Jack detect
	wm8976_write_reg(codec,0x7, 0x01);//Jack detect 
}

static int wm8976_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	u16 iface = wm8976_read_reg_cache(codec, WM8976_IFACE) & 0xff9f;
	u16 adn = wm8976_read_reg_cache(codec, WM8976_ADD) & 0x1f1;

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		iface |= 0x0020;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		iface |= 0x0040;
		break;
	}

	/* filter coefficient */
	switch (params_rate(params)) {
	case SNDRV_PCM_RATE_8000:
		adn |= 0x5 << 1;
		break;
	case SNDRV_PCM_RATE_11025:
		adn |= 0x4 << 1;
		break;
	case SNDRV_PCM_RATE_16000:
		adn |= 0x3 << 1;
		break;
	case SNDRV_PCM_RATE_22050:
		adn |= 0x2 << 1;
		break;
	case SNDRV_PCM_RATE_32000:
		adn |= 0x1 << 1;
		break;
	}

	/* set iface */
	wm8976_write_reg(codec, WM8976_IFACE, iface);
	wm8976_write_reg(codec, WM8976_ADD, adn);
	wm8976_init_regs(codec);
	return 0;
}

static int wm8976_set_dai_clkdiv(struct snd_soc_dai *codec_dai,
		int div_id, int div)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 reg;

	switch (div_id) {
	case WM8976_MCLKDIV:
		reg = wm8976_read_reg_cache(codec, WM8976_CLOCK) & 0x11f;
		wm8976_write_reg(codec, WM8976_CLOCK, reg | div);
		break;
	case WM8976_BCLKDIV:
		reg = wm8976_read_reg_cache(codec, WM8976_CLOCK) & 0x1c7;
		wm8976_write_reg(codec, WM8976_CLOCK, reg | div);
		break;
	case WM8976_OPCLKDIV:
		reg = wm8976_read_reg_cache(codec, WM8976_GPIO) & 0x1cf;
		wm8976_write_reg(codec, WM8976_GPIO, reg | div);
		break;
	case WM8976_DACOSR:
		reg = wm8976_read_reg_cache(codec, WM8976_DAC) & 0x1f7;
		wm8976_write_reg(codec, WM8976_DAC, reg | div);
		break;
	case WM8976_ADCOSR:
		reg = wm8976_read_reg_cache(codec, WM8976_ADC) & 0x1f7;
		wm8976_write_reg(codec, WM8976_ADC, reg | div);
		break;
	case WM8976_MCLKSEL:
		reg = wm8976_read_reg_cache(codec, WM8976_CLOCK) & 0x0ff;
		wm8976_write_reg(codec, WM8976_CLOCK, reg | div);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int wm8976_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 mute_reg = wm8976_read_reg_cache(codec, WM8976_DAC) & 0xffbf;

	if(mute)
		wm8976_write_reg(codec, WM8976_DAC, mute_reg | 0x40);
	else
		wm8976_write_reg(codec, WM8976_DAC, mute_reg);

	return 0;
}

/* TODO: liam need to make this lower power with dapm */
static int wm8976_set_bias_level(struct snd_soc_codec *codec,
	enum snd_soc_bias_level level)
{

	switch (level) {
	case SND_SOC_BIAS_ON:
		wm8976_write_reg(codec, WM8976_POWER1, 0x1ff);
		wm8976_write_reg(codec, WM8976_POWER2, 0x1ff);
		wm8976_write_reg(codec, WM8976_POWER3, 0x1ff);
		break;
	case SND_SOC_BIAS_STANDBY:
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_OFF:
		wm8976_write_reg(codec, WM8976_POWER1, 0x0);
		wm8976_write_reg(codec, WM8976_POWER2, 0x0);
		wm8976_write_reg(codec, WM8976_POWER3, 0x0);
		break;
	}
	
	return 0;
}

static int wm8976_codec_probe(struct snd_soc_codec *codec)
{	
	int ret = 0;
	ret = snd_soc_add_codec_controls(codec, wm8976_snd_controls,
			     ARRAY_SIZE(wm8976_snd_controls));
	
	return ret;
}

/* power down chip */
static int wm8976_codec_remove(struct snd_soc_codec *codec)
{
        struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
        
        
        wm8976_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
        wm8976_set_bias_level(codec, SND_SOC_BIAS_OFF);
        
        snd_soc_dapm_free(dapm);
        
        return 0;
}

static int wm8976_codec_suspend(struct snd_soc_codec *codec)
{
        wm8976_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	    wm8976_set_bias_level(codec, SND_SOC_BIAS_OFF);
	    
        return 0;
}

static int wm8976_codec_resume(struct snd_soc_codec *codec)
{

       wm8976_set_bias_level(codec, SND_SOC_BIAS_PREPARE);
	   wm8976_set_bias_level(codec, SND_SOC_BIAS_ON);
        return 0;
}


#define WM8976_RATES SNDRV_PCM_RATE_8000_48000 //\
	//(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 | SNDRV_PCM_RATE_16000 | \
	//SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
	//SNDRV_PCM_RATE_48000)

#define WM8976_FORMATS (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE | \
		SNDRV_PCM_FMTBIT_S18_3LE | SNDRV_PCM_FMTBIT_S20_3LE)
	//\
	//(SNDRV_PCM_FORMAT_S16_LE | SNDRV_PCM_FORMAT_S20_3LE | \
	//SNDRV_PCM_FORMAT_S24_3LE | SNDRV_PCM_FORMAT_S24_LE)

static struct snd_soc_dai_ops wm8976_dai_ops = {
	.hw_params = wm8976_hw_params,
	.digital_mute = wm8976_mute,
	.set_fmt = wm8976_set_dai_fmt,
	.set_clkdiv = wm8976_set_dai_clkdiv,
	.set_pll = wm8976_set_dai_pll,
};

struct snd_soc_dai_driver wm8976_dai = {
	.name = "wm8976-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8976_RATES,
		.formats = WM8976_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8976_RATES,
		.formats = WM8976_FORMATS,},
	.ops = &wm8976_dai_ops,
};

static struct snd_soc_codec_driver soc_codec_dev_wm8976 = {
	.probe   = wm8976_codec_probe,
	.remove  = wm8976_codec_remove,
	.suspend = wm8976_codec_suspend,
	.resume  = wm8976_codec_resume,
	.set_bias_level = wm8976_set_bias_level,
	.suspend_bias_off = true,
	
	.reg_cache_size = sizeof(wm8976_reg),
	.reg_word_size = sizeof(u8),
	.reg_cache_default = wm8976_reg,
	.reg_cache_step = 1,
	
	.read   = wm8976_read_reg_cache,
	.write  = wm8976_write_reg,
};

static int wm8976_probe(struct platform_device *pdev)
{
	/* ≈‰÷√“˝Ω≈*/
	/* L3MODE GPB2*/
	/* L3DATA GPB3*/
	/* L3CLOCK GPB4 */
	*gpbcon &= ~((3 << 4) | (3 << 6) | (3 << 8));
	*gpbcon |= ((1 << 4) | (1 << 6) | (1 << 8)) ;
	
	return snd_soc_register_codec(&pdev->dev, &soc_codec_dev_wm8976, &wm8976_dai,1);
}

static int wm8976_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver wm8976_platform_drv = {
	.probe  = wm8976_probe,
	.remove = wm8976_remove,
	.driver = {
		.name = "wm8976-codec",
	},
};

static int __init wm8976_init(void)
{
	int ret; 

	wm8976_platform = platform_device_alloc("wm8976-codec",-1);
	if(!wm8976_platform)
		return -ENOMEM;
	ret = platform_device_add(wm8976_platform);
	if(ret)
	{
		platform_device_put(wm8976_platform);
		return -ENODEV;
	}

	gpbcon = ioremap(0x56000010,12);
	gpbdat = gpbcon + 1;
	gpbup  = gpbdat + 1;

	platform_driver_register(&wm8976_platform_drv);
	return ret;
}

static void __exit wm8976_exit(void)
{

	iounmap(gpbcon);
	iounmap(gpbdat);
	iounmap(gpbup);
	
	platform_driver_unregister(&wm8976_platform_drv);
	platform_device_unregister(wm8976_platform);
	
}


module_init(wm8976_init);
module_exit(wm8976_exit);
MODULE_AUTHOR("Graeme Gregory");
MODULE_LICENSE("GPL");

