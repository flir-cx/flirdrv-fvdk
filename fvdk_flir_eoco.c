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

static BOOL SetupGpioAccessEOCO(struct device *dev);
static void CleanupGpioEOCO(struct device *dev);
static void BSPFvdPowerDownEOCO(struct device *dev);
static BOOL GetPinDoneEOCO(struct device *dev);
static BOOL GetPinStatusEOCO(struct device *dev);
static BOOL GetPinReadyEOCO(struct device *dev);
static DWORD PutInProgrammingModeEOCO(struct device *dev);
static void BSPFvdPowerDownFPAEOCO(struct device *dev);
static void BSPFvdPowerUpFPAEOCO(struct device *dev);
static void BSPFvdPowerUpEOCO(struct device *dev, BOOL restart);
static void set_fpga_power(struct device *dev, int enable);
static void reload_fpga(struct device *dev);

// Local variables
static bool fpaIsEnabled;
static bool fpgaIsEnabled;

// Code
void Setup_FLIR_EOCO(struct device *dev)
{
	struct fvdkdata *data = dev_get_drvdata(dev);
	PFVD_DEV_INFO pDev = &data->pDev;

	data->ops.pSetupGpioAccess = SetupGpioAccessEOCO;
	data->ops.pCleanupGpio = CleanupGpioEOCO;
	data->ops.pBSPFvdPowerDown = BSPFvdPowerDownEOCO;
	data->ops.pGetPinDone = GetPinDoneEOCO;
	data->ops.pGetPinStatus = GetPinStatusEOCO;
	data->ops.pGetPinReady = GetPinReadyEOCO;
	data->ops.pPutInProgrammingMode = PutInProgrammingModeEOCO;
	data->ops.pBSPFvdPowerUp = BSPFvdPowerUpEOCO;
	data->ops.pBSPFvdPowerDownFPA = BSPFvdPowerDownFPAEOCO;
	data->ops.pBSPFvdPowerUpFPA = BSPFvdPowerUpFPAEOCO;
	pDev->iI2c = 2;	// Main i2c bus
	pDev->spi_flash = true;
}

BOOL SetupGpioAccessEOCO(struct device *dev)
{
	struct fvdkdata *data = dev_get_drvdata(dev);
	PFVD_DEV_INFO pDev = &data->pDev;
	struct device_node *np = dev->of_node;
	int ret;
	int article, revision;

	ret = GetMainboardVersion(dev, &article, &revision);

	data->fpga_pins.pin_fpga_ce_n = of_get_named_gpio(np, "fpga_ce_n", 0);
	if (gpio_is_valid(data->fpga_pins.pin_fpga_ce_n)) {
		ret = devm_gpio_request_one(dev, data->fpga_pins.pin_fpga_ce_n,
					    GPIOF_IN, "FPGA_CE_N");
		if (ret)
			dev_err(dev, "unable to get FPGA CE_N gpio\n");
	} else {
		dev_err(dev, "Can not find fpga_ce_n in device tree\n");
	}

	data->fpga_pins.pin_fpga_conf_done = of_get_named_gpio(np, "fpga_conf_done", 0);
	if (gpio_is_valid(data->fpga_pins.pin_fpga_conf_done)) {
		ret = devm_gpio_request_one(dev, data->fpga_pins.pin_fpga_conf_done,
					    GPIOF_IN, "FPGA conf done");
		if (ret)
			dev_err(dev, "unable to get FPGA conf done gpio\n");
	} else {
		dev_err(dev, "Can not find fpga_conf_done in device tree\n");
	}

	data->fpga_pins.pin_fpga_config_n = of_get_named_gpio(np, "fpga_config_n", 0);
	if (gpio_is_valid(data->fpga_pins.pin_fpga_config_n)) {
		ret = devm_gpio_request_one(dev, data->fpga_pins.pin_fpga_config_n,
					    GPIOF_IN, "FPGA config_n");
		if (ret)
			dev_err(dev, "unable to get FPGA config_n gpio\n");
	} else {
		dev_err(dev, "Can not find fpga_config_n in device tree\n");
	}

	data->fpga_pins.pin_fpga_status_n = of_get_named_gpio(np, "fpga_status_n", 0);
	if (gpio_is_valid(data->fpga_pins.pin_fpga_status_n)) {
		ret = devm_gpio_request_one(dev, data->fpga_pins.pin_fpga_status_n,
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
	data->reg_4v0_fpa = devm_regulator_get(dev, "4V0_fpa");
	if (IS_ERR(data->reg_4v0_fpa))
		dev_err(dev, "can't get regulator 4V0_fpa");

	data->reg_fpa_i2c = devm_regulator_get(dev, "fpa_i2c");
	if (IS_ERR(data->reg_fpa_i2c))
		dev_err(dev, "can't get regulator fpa_i2c\n");

/* FPGA regulators */
	data->reg_1v0_fpga = devm_regulator_get(dev, "DA9063_BPRO");
	if (IS_ERR(data->reg_1v0_fpga))
		dev_err(dev, "can't get regulator DA9063_BPRO");

	data->reg_1v2_fpga = devm_regulator_get(dev, "DA9063_CORE_SW");
	if (IS_ERR(data->reg_1v2_fpga))
		dev_err(dev, "can't get regulator DA9063_CORE_SW");

	data->reg_1v8_fpga = devm_regulator_get(dev, "DA9063_PERI_SW");
	if (IS_ERR(data->reg_1v8_fpga))
		dev_err(dev, "can't get regulator DA9063_PERI_SW");

	if (article == EC101_ARTNO && revision == 3) {	//revC
		data->reg_2v5_fpga = devm_regulator_get(dev, "DA9063_BMEM");
		if (IS_ERR(data->reg_2v5_fpga))
			dev_err(dev, "can't get regulator DA9063_BMEM");

		data->reg_3v15_fpga = devm_regulator_get(dev, "DA9063_LDO10");
		if (IS_ERR(data->reg_3v15_fpga))
			dev_err(dev, "can't get regulator DA9063_LDO10");
	} else {
		data->reg_2v5_fpga = devm_regulator_get(dev, "DA9063_LDO10");
		if (IS_ERR(data->reg_2v5_fpga))
			dev_err(dev, "can't get regulator DA9063_LDO10");

		data->reg_3v15_fpga = devm_regulator_get(dev, "DA9063_LDO8");
		if (IS_ERR(data->reg_3v15_fpga))
			dev_err(dev, "can't get regulator DA9063_LDO8");
	}

	if (!GetPinDoneEOCO(dev))
		dev_err(dev, "U-boot FPGA load failed");

	data->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(data->pinctrl))
		dev_err(dev, "can't get pinctrl");

	data->pins_default = pinctrl_lookup_state(data->pinctrl, "spi-default");
	if (IS_ERR(data->pins_default))
		dev_err(dev, "can't get default pins %p %d", data->pinctrl,
			(int)(data->pins_default));

	data->pins_idle = pinctrl_lookup_state(data->pinctrl, "spi-idle");
	if (IS_ERR(data->pins_idle))
		dev_err(dev, "can't get idle pins %p %d", data->pinctrl,
			(int)(data->pins_idle));

	//fpga power already on, but need to sync regulator_enable
	set_fpga_power(dev, 1);
	of_node_put(np);

	return TRUE;
}

void CleanupGpioEOCO(struct device *dev)
{
	//devm does the cleanup
	set_fpga_power(dev, 0);
}

BOOL GetPinDoneEOCO(struct device *dev)
{
	struct fvdkdata *data = dev_get_drvdata(dev);

	return (gpio_get_value(data->fpga_pins.conf_done_gpio) != 0);
}

//no status pin on ec101
BOOL GetPinStatusEOCO(struct device *dev)
{
	return 1;
}

BOOL GetPinReadyEOCO(struct device *dev)
{
	struct fvdkdata *data = dev_get_drvdata(dev);

	return (gpio_get_value(data->fpga_pins.ready_gpio) != 0);
}

DWORD PutInProgrammingModeEOCO(struct device *dev)
{
	return 1;
}

void BSPFvdPowerUpEOCO(struct device *dev, BOOL restart)
{
	set_fpga_power(dev, 1);

        // BC-236, FVD_Open (fvdc_main) sometimes fails (ec101).
	// When failure occurs, the "read_spi_header()" indicate failure
	// probably the pBSPFvdPowerUp (this routine) needs some settle
	// time before we start to use HW depending on this voltage
	// The problem has not been observed on eoco, but you never know...
	msleep(300);

	if (restart)
		reload_fpga(dev);

}

static void reload_fpga(struct device *dev)
{
	dev_dbg(dev, "%s: Reload FPGA disabled\n", __func__);
}

static void set_fpga_power(struct device *dev, int enable)
{
	struct fvdkdata *data = dev_get_drvdata(dev);
	int ret;

	if (IS_ERR(data->reg_1v0_fpga) || IS_ERR(data->reg_1v2_fpga) ||
	    IS_ERR(data->reg_1v8_fpga) || IS_ERR(data->reg_2v5_fpga)
	    || IS_ERR(data->reg_3v15_fpga))
		return;


	// FPGA PowerDown is disabled
	// FPGA PowerOn remains active, power is enabled once, due to "fpgaIsEnabled" boolean
	// Used to count up the use-count of the regulators!
	if (enable) {
		if (fpgaIsEnabled)
			return;
		fpgaIsEnabled = true;
		dev_dbg(dev, "Fpga power enable\n");

		ret = regulator_enable(data->reg_1v0_fpga);
		if (ret)
			dev_err(dev, "can't enable reg_1v0_fpga\n");
		ret = regulator_enable(data->reg_1v8_fpga);
		if (ret)
			dev_err(dev, "can't enable reg_1v8_fpga\n");
		ret = regulator_enable(data->reg_1v2_fpga);
		if (ret)
			dev_err(dev, "can't enable reg_1v2_fpga\n");
		ret = regulator_enable(data->reg_2v5_fpga);
		if (ret)
			dev_err(dev, "can't enable reg_2v5_fpga\n");
		ret = regulator_enable(data->reg_3v15_fpga);
		if (ret)
			dev_err(dev, "can't enable reg_3v15_fpga\n");
	} else {
		dev_dbg(dev, "%s: FPGA PowerDown disabled\n", __func__);
	}
}


static void set_fpa_power(struct device *dev, int enable)
{
	struct fvdkdata *data = dev_get_drvdata(dev);
	int ret;

	if (IS_ERR(data->reg_fpa_i2c) || IS_ERR(data->reg_4v0_fpa))
		return;

	if (enable) {
		if (fpaIsEnabled)
			return;
		fpaIsEnabled = true;
		dev_dbg(dev, "FPA power enable\n");

		ret = regulator_enable(data->reg_4v0_fpa);
		ret |= regulator_enable(data->reg_fpa_i2c);

		if (ret)
			dev_err(dev, "can't enable fpa\n");

	} else {
		if (!fpaIsEnabled)
			return;
		fpaIsEnabled = false;
		dev_dbg(dev, "FPA power disable\n");

		ret = regulator_disable(data->reg_fpa_i2c);
		ret |= regulator_disable(data->reg_4v0_fpa);

		if (ret)
			dev_err(dev, "can't disable fpa\n");
	}
}

void BSPFvdPowerDownEOCO(struct device *dev)
{
	set_fpga_power(dev, 0);
	/* Powerdown is disabled in Eoco, not reconfiguring FPGA configuration pins!! */
}



void BSPFvdPowerDownFPAEOCO(struct device *dev)
{
	set_fpa_power(dev, 0);
}

void BSPFvdPowerUpFPAEOCO(struct device *dev)
{
	set_fpa_power(dev, 1);
}
