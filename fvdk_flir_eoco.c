// SPDX-License-Identifier: GPL-2.0-or-later
/***********************************************************************
 *
 *    FLIR Video Device driver.
 *    General hw dependent functions
 *
 * Copyright: FLIR Systems AB.  All rights reserved.
 *
 ***********************************************************************/

#include "flir_kernel_os.h"
#include "fpga.h"
#include "fvdkernel.h"
#include "fvdk_internal.h"
#include "asm/io.h"
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/regulator/consumer.h>

#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/regulator/of_regulator.h>
#endif

static BOOL SetupGpioAccessEOCO(PFVD_DEV_INFO pDev);
static void CleanupGpioEOCO(PFVD_DEV_INFO pDev);
static void BSPFvdPowerDownEOCO(PFVD_DEV_INFO pDev);
static BOOL GetPinDoneEOCO(PFVD_DEV_INFO pDev);
static BOOL GetPinStatusEOCO(PFVD_DEV_INFO pDev);
static BOOL GetPinReadyEOCO(PFVD_DEV_INFO pDev);
static DWORD PutInProgrammingModeEOCO(PFVD_DEV_INFO);
static void BSPFvdPowerDownFPAEOCO(PFVD_DEV_INFO pDev);
static void BSPFvdPowerUpFPAEOCO(PFVD_DEV_INFO pDev);
static void BSPFvdPowerUpEOCO(PFVD_DEV_INFO pDev, BOOL restart);
static void set_fpga_power(PFVD_DEV_INFO pDev, int enable);
static void reload_fpga(PFVD_DEV_INFO pDev);

// Local variables
static bool fpaIsEnabled;
static bool fpgaIsEnabled;

// Code
void Setup_FLIR_EOCO(PFVD_DEV_INFO pDev)
{
	pDev->pSetupGpioAccess = SetupGpioAccessEOCO;
	pDev->pCleanupGpio = CleanupGpioEOCO;
	pDev->pBSPFvdPowerDown = BSPFvdPowerDownEOCO;
	pDev->pGetPinDone = GetPinDoneEOCO;
	pDev->pGetPinStatus = GetPinStatusEOCO;
	pDev->pGetPinReady = GetPinReadyEOCO;
	pDev->pPutInProgrammingMode = PutInProgrammingModeEOCO;
	pDev->pBSPFvdPowerUp = BSPFvdPowerUpEOCO;
	pDev->pBSPFvdPowerDownFPA = BSPFvdPowerDownFPAEOCO;
	pDev->pBSPFvdPowerUpFPA = BSPFvdPowerUpFPAEOCO;
	pDev->iI2c = 2;	// Main i2c bus
	pDev->spi_flash = true;
}

BOOL SetupGpioAccessEOCO(PFVD_DEV_INFO pDev)
{
	struct device *dev = &pDev->pLinuxDevice->dev;
	struct device_node *np = dev->of_node;
	int ret;
	int article, revision;

	ret = GetMainboardVersion(pDev, &article, &revision);

	pDev->pin_fpga_ce_n = of_get_named_gpio(np, "fpga_ce_n", 0);
	if (gpio_is_valid(pDev->pin_fpga_ce_n)) {
		ret = devm_gpio_request_one(dev, pDev->pin_fpga_ce_n,
					    GPIOF_IN, "FPGA_CE_N");
		if (ret)
			dev_err(dev, "unable to get FPGA CE_N gpio\n");
	} else {
		dev_err(dev, "Can not find fpga_ce_n in device tree\n");
	}

	pDev->pin_fpga_conf_done = of_get_named_gpio(np, "fpga_conf_done", 0);
	if (gpio_is_valid(pDev->pin_fpga_conf_done)) {
		ret = devm_gpio_request_one(dev, pDev->pin_fpga_conf_done,
					    GPIOF_IN, "FPGA conf done");
		if (ret)
			dev_err(dev, "unable to get FPGA conf done gpio\n");
	} else {
		dev_err(dev, "Can not find fpga_conf_done in device tree\n");
	}

	pDev->pin_fpga_config_n = of_get_named_gpio(np, "fpga_config_n", 0);
	if (gpio_is_valid(pDev->pin_fpga_config_n)) {
		ret = devm_gpio_request_one(dev, pDev->pin_fpga_config_n,
					    GPIOF_IN, "FPGA config_n");
		if (ret)
			dev_err(dev, "unable to get FPGA config_n gpio\n");
	} else {
		dev_err(dev, "Can not find fpga_config_n in device tree\n");
	}

	pDev->pin_fpga_status_n = of_get_named_gpio(np, "fpga_status_n", 0);
	if (gpio_is_valid(pDev->pin_fpga_status_n)) {
		ret = devm_gpio_request_one(dev, pDev->pin_fpga_status_n,
					    GPIOF_IN, "FPGA status_n");
		if (ret)
			dev_err(dev, "unable to get FPGA status_n gpio\n");
	} else {
		dev_err(dev, "Can not find fpga_config_n in device tree\n");
	}

	/*SPI bus shared with fpga */
	pDev->spi_sclk_gpio = of_get_named_gpio(np, "spi-sclk-gpio", 0);
	pDev->spi_mosi_gpio = of_get_named_gpio(np, "spi-mosi-gpio", 0);
	pDev->spi_miso_gpio = of_get_named_gpio(np, "spi-miso-gpio", 0);
	pDev->spi_cs_gpio = of_get_named_gpio(np, "spi-cs-gpio", 0);

/* FPA regulators */
	pDev->reg_4v0_fpa = devm_regulator_get(dev, "4V0_fpa");
	if (IS_ERR(pDev->reg_4v0_fpa))
		dev_err(dev, "can't get regulator 4V0_fpa");

	pDev->reg_fpa_i2c = devm_regulator_get(dev, "fpa_i2c");
	if (IS_ERR(pDev->reg_fpa_i2c))
		dev_err(dev, "can't get regulator fpa_i2c\n");

/* FPGA regulators */
	pDev->reg_1v0_fpga = devm_regulator_get(dev, "DA9063_BPRO");
	if (IS_ERR(pDev->reg_1v0_fpga))
		dev_err(dev, "can't get regulator DA9063_BPRO");

	pDev->reg_1v2_fpga = devm_regulator_get(dev, "DA9063_CORE_SW");
	if (IS_ERR(pDev->reg_1v2_fpga))
		dev_err(dev, "can't get regulator DA9063_CORE_SW");

	pDev->reg_1v8_fpga = devm_regulator_get(dev, "DA9063_PERI_SW");
	if (IS_ERR(pDev->reg_1v8_fpga))
		dev_err(dev, "can't get regulator DA9063_PERI_SW");

	if (article == EC101_ARTNO && revision == 3) {	//revC
		pDev->reg_2v5_fpga = devm_regulator_get(dev, "DA9063_BMEM");
		if (IS_ERR(pDev->reg_2v5_fpga))
			dev_err(dev, "can't get regulator DA9063_BMEM");

		pDev->reg_3v15_fpga = devm_regulator_get(dev, "DA9063_LDO10");
		if (IS_ERR(pDev->reg_3v15_fpga))
			dev_err(dev, "can't get regulator DA9063_LDO10");
	} else {
		pDev->reg_2v5_fpga = devm_regulator_get(dev, "DA9063_LDO10");
		if (IS_ERR(pDev->reg_2v5_fpga))
			dev_err(dev, "can't get regulator DA9063_LDO10");

		pDev->reg_3v15_fpga = devm_regulator_get(dev, "DA9063_LDO8");
		if (IS_ERR(pDev->reg_3v15_fpga))
			dev_err(dev, "can't get regulator DA9063_LDO8");
	}

	if (!GetPinDoneEOCO(pDev)) {
		dev_err(dev, "U-boot FPGA load failed");
		//COMMENTED
		/* gpio_direction_output(pDev->program_gpio, 0); */
		/* gpio_direction_output(pDev->init_gpio, 0); */
	}

	pDev->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(pDev->pinctrl))
		dev_err(dev, "can't get pinctrl");

	pDev->pins_default = pinctrl_lookup_state(pDev->pinctrl, "spi-default");
	if (IS_ERR(pDev->pins_default))
		dev_err(dev, "can't get default pins %p %d", pDev->pinctrl,
			(int)(pDev->pins_default));

	pDev->pins_idle = pinctrl_lookup_state(pDev->pinctrl, "spi-idle");
	if (IS_ERR(pDev->pins_idle))
		dev_err(dev, "can't get idle pins %p %d", pDev->pinctrl,
			(int)(pDev->pins_idle));

	//fpga power already on, but need to sync regulator_enable
	set_fpga_power(pDev, 1);
	of_node_put(np);

	return TRUE;
}

void CleanupGpioEOCO(PFVD_DEV_INFO pDev)
{
	//devm does the cleanup
	set_fpga_power(pDev, 0);
}

BOOL GetPinDoneEOCO(PFVD_DEV_INFO pDev)
{
	return (gpio_get_value(pDev->conf_done_gpio) != 0);
}

//no status pin on ec101
BOOL GetPinStatusEOCO(PFVD_DEV_INFO pDev)
{
	return 1;
}

BOOL GetPinReadyEOCO(PFVD_DEV_INFO pDev)
{
	return (gpio_get_value(pDev->ready_gpio) != 0);
}

DWORD PutInProgrammingModeEOCO(PFVD_DEV_INFO pDev)
{
	return 1;
}

void BSPFvdPowerUpEOCO(PFVD_DEV_INFO pDev, BOOL restart)
{
	set_fpga_power(pDev, 1);

	if (restart)
		reload_fpga(pDev);

}

static void reload_fpga(PFVD_DEV_INFO pDev)
{
	dev_dbg(&pDev->pLinuxDevice->dev, "%s: Reload FPGA disabled\n", __func__);

	/* int timeout = 100; */
	/* struct device *dev = &pDev->pLinuxDevice->dev; */

	/* pinctrl_select_state(pDev->pinctrl, pDev->pins_idle); */
	/* //gpio requested in spi-imx */
	/* gpio_direction_input(pDev->spi_cs_gpio); */

	/* if (gpio_request(pDev->spi_sclk_gpio, "SPI1_SCLK")) */
	/*	dev_err(dev, "SPI1_SCLK can not be requested\n"); */
	/* else */
	/*	gpio_direction_input(pDev->spi_sclk_gpio); */
	/* if (gpio_request(pDev->spi_mosi_gpio, "SPI1_MOSI")) */
	/*	dev_err(dev, "SPI1_MOSI can not be requested\n"); */
	/* else */
	/*	gpio_direction_input(pDev->spi_mosi_gpio); */
	/* if (gpio_request(pDev->spi_miso_gpio, "SPI1_MISO")) */
	/*	dev_err(dev, "SPI1_MISO can not be requested\n"); */
	/* else */
	/*	gpio_direction_input(pDev->spi_miso_gpio); */

	/* msleep(1); */
	/* gpio_direction_input(pDev->program_gpio); */
	/* gpio_direction_input(pDev->init_gpio); */

	/* while (timeout--) { */
	/*	msleep(5); */
	/*	if (GetPinDoneMX6S(pDev)) */
	/*		break; */
	/* } */
	/* msleep(5); */

	/* if (!GetPinDoneMX6S(pDev)) { */
	/*	dev_err(dev, "FPGA load failed"); */
	/*	gpio_direction_output(pDev->program_gpio, 0); */
	/*	gpio_direction_output(pDev->init_gpio, 0); */
	/* } else */
	/*	dev_info(dev, "FPGA loaded in %d ms\n", (100 - timeout) * 5); */

	/* gpio_direction_output(pDev->spi_cs_gpio, 1); */
	/* // Set SPI as SPI */
	/* pinctrl_select_state(pDev->pinctrl, pDev->pins_default); */
	/* gpio_free(pDev->spi_sclk_gpio); */
	/* gpio_free(pDev->spi_mosi_gpio); */
	/* gpio_free(pDev->spi_miso_gpio); */
}

static void set_fpga_power(PFVD_DEV_INFO pDev, int enable)
{
	int ret;

	if (IS_ERR(pDev->reg_1v0_fpga) || IS_ERR(pDev->reg_1v2_fpga) ||
	    IS_ERR(pDev->reg_1v8_fpga) || IS_ERR(pDev->reg_2v5_fpga)
	    || IS_ERR(pDev->reg_3v15_fpga))
		return;


	// FPGA PowerDown is disabled
	// FPGA PowerOn remains active, power is enabled once, due to "fpgaIsEnabled" boolean
	// Used to count up the use-count of the regulators!
	if (enable) {
		if (fpgaIsEnabled)
			return;
		fpgaIsEnabled = true;
		dev_dbg(&pDev->pLinuxDevice->dev, "Fpga power enable\n");

		ret = regulator_enable(pDev->reg_1v0_fpga);
		if (ret)
			dev_err(&pDev->pLinuxDevice->dev, "can't enable reg_1v0_fpga\n");
		ret = regulator_enable(pDev->reg_1v8_fpga);
		if (ret)
			dev_err(&pDev->pLinuxDevice->dev, "can't enable reg_1v8_fpga\n");
		ret = regulator_enable(pDev->reg_1v2_fpga);
		if (ret)
			dev_err(&pDev->pLinuxDevice->dev, "can't enable reg_1v2_fpga\n");
		ret = regulator_enable(pDev->reg_2v5_fpga);
		if (ret)
			dev_err(&pDev->pLinuxDevice->dev, "can't enable reg_2v5_fpga\n");
		ret = regulator_enable(pDev->reg_3v15_fpga);
		if (ret)
			dev_err(&pDev->pLinuxDevice->dev, "can't enable reg_3v15_fpga\n");
	} else {
		dev_dbg(&pDev->pLinuxDevice->dev, "%s: FPGA PowerDown disabled\n", __func__);
		/* if (! fpgaIsEnabled) */
		/*	return; */
		/* fpgaIsEnabled = false; */
		/* dev_dbg(&pDev->pLinuxDevice->dev, "Fpga power disable\n"); */

		/* ret = regulator_disable(pDev->reg_1v0_fpga); */
		/* if (ret) */
		/*	dev_err(&pDev->pLinuxDevice->dev, "can't disable reg_1v0_fpga\n"); */
		/* ret = regulator_disable(pDev->reg_1v8_fpga); */
		/* if (ret) */
		/*	dev_err(&pDev->pLinuxDevice->dev, "can't disable reg_1v8_fpga\n"); */
		/* ret = regulator_disable(pDev->reg_1v2_fpga); */
		/* if (ret) */
		/*	dev_err(&pDev->pLinuxDevice->dev, "can't disable reg_1v2_fpga\n"); */
		/* ret = regulator_disable(pDev->reg_2v5_fpga); */
		/* if (ret) */
		/*	dev_err(&pDev->pLinuxDevice->dev, "can't disable reg_2v5_fpga\n"); */
		/* ret = regulator_disable(pDev->reg_3v15_fpga); */
		/* if (ret) */
		/*	dev_err(&pDev->pLinuxDevice->dev, "can't disable reg_3v15_fpga\n"); */
	}
}


static void set_fpa_power(PFVD_DEV_INFO pDev, int enable)
{
	int ret;

	if (IS_ERR(pDev->reg_fpa_i2c) || IS_ERR(pDev->reg_4v0_fpa))
		return;

	if (enable) {
		if (fpaIsEnabled)
			return;
		fpaIsEnabled = true;
		dev_dbg(&pDev->pLinuxDevice->dev, "FPA power enable\n");

		ret = regulator_enable(pDev->reg_4v0_fpa);
		ret |= regulator_enable(pDev->reg_fpa_i2c);

		if (ret)
			dev_err(&pDev->pLinuxDevice->dev, "can't enable fpa\n");

	} else {
		if (!fpaIsEnabled)
			return;
		fpaIsEnabled = false;
		dev_dbg(&pDev->pLinuxDevice->dev, "FPA power disable\n");

		ret = regulator_disable(pDev->reg_fpa_i2c);
		ret |= regulator_disable(pDev->reg_4v0_fpa);

		if (ret)
			dev_err(&pDev->pLinuxDevice->dev, "can't disable fpa\n");
	}
}

void BSPFvdPowerDownEOCO(PFVD_DEV_INFO pDev)
{
	set_fpga_power(pDev, 0);
	/* Powerdown is disabled in Eoco, not reconfiguring FPGA configuration pins!! */
	/* gpio_direction_input(pDev->program_gpio); */
	/* gpio_direction_output(pDev->init_gpio, 0); */
	/* gpio_direction_input(pDev->spi_cs_gpio); */
	/* pinctrl_select_state(pDev->pinctrl, pDev->pins_idle); */
}



void BSPFvdPowerDownFPAEOCO(PFVD_DEV_INFO pDev)
{
	set_fpa_power(pDev, 0);
}

void BSPFvdPowerUpFPAEOCO(PFVD_DEV_INFO pDev)
{
	set_fpa_power(pDev, 1);
}
