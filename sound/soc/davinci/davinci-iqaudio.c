/*
 * ASoC driver for IQAudioIO DAC
 *
 * Author: Howard Mitchell <hm@hmdedded.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of_platform.h>

#include <sound/soc.h>

static const struct of_device_id davinci_iqaudio_dt_ids[] = {
	{ .compatible = "iqaudio,am335x-iqaudio", },
	{ }
};
MODULE_DEVICE_TABLE(of, davinci_iqaudio_dt_ids);

static struct snd_soc_dai_link iqaudio_dai = {
	.name			= "IQaudIO DAC",
	.stream_name	= "IQaudIO DAC HiFi",
	.codec_dai_name	= "pcm512x-hifi",
	.dai_fmt		= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBM_CFM | SND_SOC_DAIFMT_NB_IF,
};

static struct snd_soc_card iqaudio_soc_card = {
	.owner = THIS_MODULE,
	.dai_link = &iqaudio_dai,
	.num_links = 1,
};

static int davinci_iqaudio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *refclk;
	int ret = 0;

	iqaudio_dai.codec_of_node = of_parse_phandle(np, "iqaudio,audio-codec", 0);
	if (!iqaudio_dai.codec_of_node)
		return -EINVAL;

	iqaudio_dai.cpu_of_node = of_parse_phandle(np,
						"iqaudio,mcasp-controller", 0);
	if (!iqaudio_dai.cpu_of_node)
		return -EINVAL;

	iqaudio_dai.platform_of_node = iqaudio_dai.cpu_of_node;

	iqaudio_soc_card.dev = &pdev->dev;
	ret = snd_soc_of_parse_card_name(&iqaudio_soc_card, "iqaudio,model");
	if (ret)
		return ret;

	ret = snd_soc_register_card(&iqaudio_soc_card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);
	}
	return ret;
}

static int davinci_iqaudio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

static struct platform_driver davinci_iqaudio_driver = {
	.probe		= davinci_iqaudio_probe,
	.remove		= davinci_iqaudio_remove,
	.driver		= {
		.name	= "davinci_iqaudio",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(davinci_iqaudio_dt_ids),
	},
};

static int __init iqaudio_init(void)
{
	/*
	 * When using device tree the devices will be created dynamically.
	 * Only register platfrom driver structure.
	 */
	return platform_driver_register(&davinci_iqaudio_driver);
}

static void __exit iqaudio_exit(void)
{
	platform_driver_unregister(&davinci_iqaudio_driver);
	return;
}

module_init(iqaudio_init);
module_exit(iqaudio_exit);

MODULE_AUTHOR("Howard Mitchell");
MODULE_DESCRIPTION("IQaudIO ASoC driver");
MODULE_LICENSE("GPL");
