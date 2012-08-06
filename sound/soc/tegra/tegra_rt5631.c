/*
 * tegra_rt5631.c - Tegra machine ASoC driver for boards using RT5631 codec.
 *
 */

#include <asm/mach-types.h>

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#ifdef CONFIG_SWITCH
#include <linux/switch.h>
#endif

#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "tegra_pcm.h"
#include "tegra_asoc_utils.h"
#include <mach/board-cardhu-misc.h>

#include "../drivers/input/asusec/asusdec.h"
#define DRV_NAME "tegra-snd-codec"

struct tegra_rt5631 {
	struct tegra_asoc_utils_data util_data;
	struct regulator *spk_reg;
	struct regulator *dmic_reg;
	int gpio_requested;
#ifdef CONFIG_SWITCH
	int jack_status;
#endif
};
extern bool headset_alive;
extern bool lineout_alive;
extern struct snd_soc_codec *global_audio_codec;
static bool audio_dock_in = false;
static bool audio_stand_in = false;

bool isAudioStandIn(void)
{
    return audio_stand_in;
}

EXPORT_SYMBOL(audio_stand_in);

int audio_stand_route(bool status)
{
    if(global_audio_codec == NULL){
        printk("%s: global_audio_codec is NULL\n", __func__);
        return 0;
    }

    struct snd_soc_dapm_context *dapm = &global_audio_codec->dapm;

    if(snd_soc_dapm_get_pin_status(dapm, "Int Spk") || snd_soc_dapm_get_pin_status(dapm, "AUX")){
         if(status){
                printk("%s: audio stand lineout on\n", __func__);
                snd_soc_dapm_enable_pin(dapm, "AUX");
                snd_soc_dapm_disable_pin(dapm, "Int Spk");
                snd_soc_dapm_sync(dapm);
         }else{
                printk("%s: audio stand lineout off\n", __func__);
                snd_soc_dapm_disable_pin(dapm, "AUX");
                snd_soc_dapm_enable_pin(dapm, "Int Spk");
                snd_soc_dapm_sync(dapm);
         }
    }
}
EXPORT_SYMBOL(audio_stand_route);


int audio_dock_in_out(u8 status)
{
    audio_dock_in = (status == AUDIO_DOCK) ? true : false;
    audio_stand_in = (status == AUDIO_STAND) ? true : false;

    if(global_audio_codec == NULL){
        printk("%s: global_audio_codec is NULL\n", __func__);
        return 0;
    }

    struct snd_soc_dapm_context *dapm = &global_audio_codec->dapm;

    if(snd_soc_dapm_get_pin_status(dapm, "Int Spk") || snd_soc_dapm_get_pin_status(dapm, "AUX")){
	 if(status == AUDIO_DOCK ){
		printk("%s: audio_dock_in\n", __func__);
		snd_soc_dapm_enable_pin(dapm, "AUX");
		snd_soc_dapm_disable_pin(dapm, "Int Spk");
		snd_soc_dapm_sync(dapm);
	 }else{
		printk("%s: audio_stand_dock_out\n", __func__);
		snd_soc_dapm_disable_pin(dapm, "AUX");
		snd_soc_dapm_enable_pin(dapm, "Int Spk");
		snd_soc_dapm_sync(dapm);
	 }
    }
}
EXPORT_SYMBOL(audio_dock_in_out);
	

static int tegra_rt5631_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_card *card = codec->card;
	struct tegra_rt5631 *machine = snd_soc_card_get_drvdata(card);
	int srate, mclk, i2s_daifmt;
	int err;

	srate = params_rate(params);
	switch (srate) {
	case 64000:
	case 88200:
	case 96000:
		mclk = 128 * srate;
		break;
	default:
		mclk = 384 * srate;
		break;
	}
	/* FIXME: Codec only requires >= 3MHz if OSR==0 */
	while (mclk < 6000000)
		mclk *= 2;

	err = tegra_asoc_utils_set_rate(&machine->util_data, srate, mclk);
	if (err < 0) {
		dev_err(card->dev, "Can't configure clocks\n");
		return err;
	}

	i2s_daifmt = SND_SOC_DAIFMT_NB_NF |
		     SND_SOC_DAIFMT_CBS_CFS;

	/* Use DSP mode for mono on Tegra20 */
	if ((params_channels(params) != 2) &&
	    (machine_is_ventana() || machine_is_harmony() ||
	    machine_is_kaen() || machine_is_aebl()))
		i2s_daifmt |= SND_SOC_DAIFMT_DSP_A;
	else
		i2s_daifmt |= SND_SOC_DAIFMT_I2S;

	err = snd_soc_dai_set_fmt(codec_dai, i2s_daifmt);
	if (err < 0) {
		dev_err(card->dev, "codec_dai fmt not set\n");
		return err;
	}

	err = snd_soc_dai_set_fmt(cpu_dai, i2s_daifmt);
	if (err < 0) {
		dev_err(card->dev, "cpu_dai fmt not set\n");
		return err;
	}

	err = snd_soc_dai_set_sysclk(codec_dai, 0, mclk,
					SND_SOC_CLOCK_IN);
	if (err < 0) {
		dev_err(card->dev, "codec_dai clock not set\n");
		return err;
	}

	return 0;
}

static struct snd_soc_ops tegra_rt5631_ops = {
	.hw_params = tegra_rt5631_hw_params,
};

static struct snd_soc_ops tegra_spdif_ops;


static const struct snd_soc_dapm_widget cardhu_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Int Spk", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_SPK("AUX", NULL),

};

static const struct snd_soc_dapm_widget tegra_rt5631_default_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Int Spk", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
};

static const struct snd_soc_dapm_route cardhu_audio_map[] = {
	{"Headphone Jack", NULL, "HPOL"},
	{"Headphone Jack", NULL, "HPOR"},
	{"Int Spk", NULL, "SPOL"},
	{"Int Spk", NULL, "SPOR"},
	{"MIC1", NULL, "Mic Bias1"},
	{"Mic Bias1", NULL, "Mic Jack"},
	{"DMIC", NULL, "Int Mic"},
	{"AUX", NULL, "AUXO2"},
};

static const struct snd_kcontrol_new cardhu_controls[] = {
	SOC_DAPM_PIN_SWITCH("Int Spk"),
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Mic Jack"),
	SOC_DAPM_PIN_SWITCH("Int Mic"),
	SOC_DAPM_PIN_SWITCH("AUX"),
};

static const struct snd_kcontrol_new tegra_rt5631_default_controls[] = {
	SOC_DAPM_PIN_SWITCH("Int Spk"),
};

static int tegra_rt5631_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	struct snd_soc_card *card = codec->card;
	struct tegra_rt5631 *machine = snd_soc_card_get_drvdata(card);

	int ret;
	printk("%s+\n", __func__);
	if (machine_is_cardhu() || machine_is_ventana()) {
		ret = snd_soc_add_controls(codec, cardhu_controls,
				ARRAY_SIZE(cardhu_controls));
		if (ret < 0)
			return ret;

		snd_soc_dapm_new_controls(dapm, cardhu_dapm_widgets,
				ARRAY_SIZE(cardhu_dapm_widgets));
	}
	else {
		ret = snd_soc_add_controls(codec,
				tegra_rt5631_default_controls,
				ARRAY_SIZE(tegra_rt5631_default_controls));
		if (ret < 0)
			return ret;

		snd_soc_dapm_new_controls(dapm,
				tegra_rt5631_default_dapm_widgets,
				ARRAY_SIZE(tegra_rt5631_default_dapm_widgets));
	}

	snd_soc_dapm_add_routes(dapm, cardhu_audio_map,
					ARRAY_SIZE(cardhu_audio_map));

	snd_soc_dapm_nc_pin(dapm, "MIC2");
	snd_soc_dapm_nc_pin(dapm, "AXIL");
	snd_soc_dapm_nc_pin(dapm, "AXIR");
	snd_soc_dapm_nc_pin(dapm, "MONOIN_RXN");
	snd_soc_dapm_nc_pin(dapm, "MONOIN_RXP");
	snd_soc_dapm_nc_pin(dapm, "MONO");
	snd_soc_dapm_disable_pin(dapm, "Int Mic");
	snd_soc_dapm_disable_pin(dapm, "Mic Jack");
	snd_soc_dapm_disable_pin(dapm, "Headphone Jack");
	snd_soc_dapm_disable_pin(dapm, "Int Spk");
	snd_soc_dapm_disable_pin(dapm, "AUX");
	snd_soc_dapm_sync(dapm);

	return 0;
}

static struct snd_soc_dai_link tegra_rt5631_dai[] = {
	{
		.name = "RT5631",
		.stream_name = "RT5631 PCM",
		.codec_name = "rt5631.4-001a",
		.platform_name = "tegra-pcm-audio",
		.cpu_dai_name = "tegra30-i2s.1",
		.codec_dai_name = "rt5631-hifi",
		.init = tegra_rt5631_init,
		.ops = &tegra_rt5631_ops,
	},
	{
		.name = "SPDIF",
		.stream_name = "SPDIF PCM",
		.codec_name = "spdif-dit.0",
		.platform_name = "tegra-pcm-audio",
		.cpu_dai_name = "tegra30-spdif",
		.codec_dai_name = "dit-hifi",
		.ops = &tegra_spdif_ops,
	}
};

static struct snd_soc_card snd_soc_tegra_rt5631 = {
	.name = "tegra-codec",
	.dai_link = tegra_rt5631_dai,
	.num_links = ARRAY_SIZE(tegra_rt5631_dai),
};

static __devinit int tegra_rt5631_driver_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_tegra_rt5631;
	struct tegra_rt5631 *machine;

	int ret;
	printk("%s+\n", __func__);

	machine = kzalloc(sizeof(struct tegra_rt5631), GFP_KERNEL);
	if (!machine) {
		dev_err(&pdev->dev, "Can't allocate tegra_rt5631 struct\n");
		return -ENOMEM;
	}

	ret = tegra_asoc_utils_init(&machine->util_data, &pdev->dev);
	if (ret)
		goto err_free_machine;

	card->dev = &pdev->dev;
	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, machine);

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n",
			ret);
		goto err_fini_utils;
	}
	printk("%s-\n", __func__);
	return 0;

err_fini_utils:
	tegra_asoc_utils_fini(&machine->util_data);
err_free_machine:
	kfree(machine);
	return ret;
}

static int __devexit tegra_rt5631_driver_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct tegra_rt5631 *machine = snd_soc_card_get_drvdata(card);

	snd_soc_unregister_card(card);

	tegra_asoc_utils_fini(&machine->util_data);

	kfree(machine);

	return 0;
}

static struct platform_driver tegra_rt5631_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
	},
	.probe = tegra_rt5631_driver_probe,
	.remove = __devexit_p(tegra_rt5631_driver_remove),
};

static int __init tegra_rt5631_modinit(void)
{
	printk(KERN_INFO "%s+ #####\n", __func__);
	int ret = 0;
	 u32 project_info = tegra3_get_project_id();

	if(project_info == TEGRA3_PROJECT_TF201 || project_info == TEGRA3_PROJECT_TF300TG ||
                project_info == TEGRA3_PROJECT_TF700T || project_info == TEGRA3_PROJECT_TF300TL)
	{
		printk("%s(): support codec rt5631\n", __func__);
	}else{
		printk("%s(): not support codec rt5631\n", __func__);
		return 0;
	}

	ret = platform_driver_register(&tegra_rt5631_driver);

	printk(KERN_INFO "%s- #####\n", __func__);
	return ret;
}
module_init(tegra_rt5631_modinit);

static void __exit tegra_rt5631_modexit(void)
{
	platform_driver_unregister(&tegra_rt5631_driver);
}
module_exit(tegra_rt5631_modexit);

MODULE_AUTHOR("Stephen Warren <swarren@nvidia.com>");
MODULE_DESCRIPTION("Tegra+RT5631 machine ASoC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
