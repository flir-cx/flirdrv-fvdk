/* SPDX-License-Identifier: GPL-2.0-or-later */
/***********************************************************************
 *
 * $Date$
 * $Author$
 *
 *
 * Description of file:
 *    FLIR Video Device driver.
 *    General hw dependent functions
 *
 * Last check-in changelist:
 * $Change$
 *
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

#if KERNEL_VERSION(3, 10, 0) <= LINUX_VERSION_CODE
#include "../arch/arm/mach-imx/mx6.h"
#else /*  */
#include "mach/mx6.h"
#define devm_regulator_get regulator_get
static inline int pinctrl_select_state(struct pinctrl *p,
				       struct pinctrl_state *s)
{
	return 0;
}
#endif /*  */

// Local prototypes
static BOOL SetupGpioAccessMX6S(struct device *dev);
static void CleanupGpioMX6S(struct device *dev);
static BOOL GetPinDoneMX6S(struct device *dev);
static BOOL GetPinStatusMX6S(struct device *dev);
static BOOL GetPinReadyMX6S(struct device *dev);
static DWORD PutInProgrammingModeMX6S(struct device *dev);
static void BSPFvdPowerDownMX6S(struct device *dev);
static void BSPFvdPowerDownFPAMX6S(struct device *dev);
static void BSPFvdPowerUpMX6S(struct device *dev, BOOL restart);
static void BSPFvdPowerUpFPAMX6S(struct device *dev);
static void enable_fpga_power(struct device *dev);
static void reload_fpga(struct device *dev);

// Local variables
static bool fpaIsEnabled;
static bool fpgaIsEnabled;

// Code
void SetupMX6S_ec101(struct device *dev)
{
	struct fvdkdata *data = dev_get_drvdata(dev);
	PFVD_DEV_INFO pDev = &data->pDev;

	data->ops.pSetupGpioAccess = SetupGpioAccessMX6S;
	data->ops.pCleanupGpio = CleanupGpioMX6S;
	data->ops.pGetPinDone = GetPinDoneMX6S;
	data->ops.pGetPinStatus = GetPinStatusMX6S;
	data->ops.pGetPinReady = GetPinReadyMX6S;
	data->ops.pPutInProgrammingMode = PutInProgrammingModeMX6S;
	data->ops.pBSPFvdPowerUp = BSPFvdPowerUpMX6S;
	data->ops.pBSPFvdPowerDown = BSPFvdPowerDownMX6S;
	data->ops.pBSPFvdPowerDownFPA = BSPFvdPowerDownFPAMX6S;
	data->ops.pBSPFvdPowerUpFPA = BSPFvdPowerUpFPAMX6S;
	pDev->iI2c = 2;		// Main i2c bus
	pDev->spi_flash = true;
}

BOOL SetupGpioAccessMX6S(struct device *dev)
{
#ifdef CONFIG_OF
	struct fvdkdata *data = dev_get_drvdata(dev);
	PFVD_DEV_INFO pDev = &data->pDev;
	struct device_node *np = dev->of_node;
	int ret;
	int article, revision;

	GetMainboardVersion(dev, &article, &revision);

	data->fpga_pins.program_gpio = of_get_named_gpio(np, "fpga-program-gpio", 0);
	if (gpio_is_valid(data->fpga_pins.program_gpio)) {
		ret = devm_gpio_request_one(dev, data->fpga_pins.program_gpio,
					    GPIOF_IN, "FPGA program");
		if (ret)
			dev_err(dev, "unable to get FPGA program gpio\n");
	}

	data->fpga_pins.init_gpio = of_get_named_gpio(np, "fpga-init-gpio", 0);
	if (gpio_is_valid(data->fpga_pins.init_gpio)) {
		ret = devm_gpio_request_one(dev, data->fpga_pins.init_gpio,
					    GPIOF_IN, "FPGA init");
		if (ret)
			dev_err(dev, "unable to get FPGA init gpio\n");

	}

	data->fpga_pins.conf_done_gpio = of_get_named_gpio(np, "fpga-conf-done-gpio", 0);
	if (gpio_is_valid(data->fpga_pins.conf_done_gpio)) {
		ret = devm_gpio_request_one(dev, data->fpga_pins.conf_done_gpio,
					    GPIOF_IN, "FPGA conf done");
		if (ret)
			dev_err(dev, "unable to get FPGA conf done gpio\n");

	}

	data->fpga_pins.ready_gpio = of_get_named_gpio(np, "fpga-ready-gpio", 0);
	if (gpio_is_valid(data->fpga_pins.ready_gpio)) {
		ret = devm_gpio_request_one(dev, data->fpga_pins.ready_gpio,
					    GPIOF_IN, "FPGA ready");
		if (ret)
			dev_err(dev, "unable to get FPGA ready gpio\n");

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

	if (!GetPinDoneMX6S(dev)) {
		dev_err(dev, "U-boot FPGA load failed");
		gpio_direction_output(data->fpga_pins.program_gpio, 0);
		gpio_direction_output(data->fpga_pins.init_gpio, 0);
	}

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
	enable_fpga_power(dev);

	of_node_put(np);
#endif

	return TRUE;
}

void CleanupGpioMX6S(struct device *dev)
{
	//devm does the cleanup
}

BOOL GetPinDoneMX6S(struct device *dev)
{
	struct fvdkdata *data = dev_get_drvdata(dev);

	return (gpio_get_value(data->fpga_pins.conf_done_gpio) != 0);
}

//no status pin on ec101
BOOL GetPinStatusMX6S(struct device *dev)
{
	return 1;
}

BOOL GetPinReadyMX6S(struct device *dev)
{
	struct fvdkdata *data = dev_get_drvdata(dev);

	return (gpio_get_value(data->fpga_pins.ready_gpio) != 0);
}

DWORD PutInProgrammingModeMX6S(struct device *dev)
{
	return 1;
}

void BSPFvdPowerUpMX6S(struct device *dev, BOOL restart)
{
	enable_fpga_power(dev);

        // BC-236, FVD_Open (fvdc_main) sometimes fails.
	// When failure occurs, the "read_spi_header()" indicate failure
	// probably the pBSPFvdPowerUp (this routine) needs some settle
	// time before we start to use HW depending on this voltage
	msleep(300);

	if (restart)
		reload_fpga(dev);

}

static void reload_fpga(struct device *dev)
{
	struct fvdkdata *data = dev_get_drvdata(dev);
	PFVD_DEV_INFO pDev = &data->pDev;
	int timeout = 100;

	pinctrl_select_state(data->pinctrl, data->pins_idle);
	//gpio requested in spi-imx
	gpio_direction_input(pDev->spi_cs_gpio);

	if (gpio_request(pDev->spi_sclk_gpio, "SPI1_SCLK"))
		dev_err(dev, "SPI1_SCLK can not be requested\n");
	else
		gpio_direction_input(pDev->spi_sclk_gpio);
	if (gpio_request(pDev->spi_mosi_gpio, "SPI1_MOSI"))
		dev_err(dev, "SPI1_MOSI can not be requested\n");
	else
		gpio_direction_input(pDev->spi_mosi_gpio);
	if (gpio_request(pDev->spi_miso_gpio, "SPI1_MISO"))
		dev_err(dev, "SPI1_MISO can not be requested\n");
	else
		gpio_direction_input(pDev->spi_miso_gpio);

	msleep(1);
	gpio_direction_input(data->fpga_pins.program_gpio);
	gpio_direction_input(data->fpga_pins.init_gpio);

	while (timeout--) {
		msleep(5);
		if (GetPinDoneMX6S(dev))
			break;
	}
	msleep(5);

	if (!GetPinDoneMX6S(dev)) {
		dev_err(dev, "FPGA load failed");
		gpio_direction_output(data->fpga_pins.program_gpio, 0);
		gpio_direction_output(data->fpga_pins.init_gpio, 0);
	} else
		dev_info(dev, "FPGA loaded in %d ms\n", (100 - timeout) * 5);

	gpio_direction_output(pDev->spi_cs_gpio, 1);
	// Set SPI as SPI
	pinctrl_select_state(data->pinctrl, data->pins_default);
	gpio_free(pDev->spi_sclk_gpio);
	gpio_free(pDev->spi_mosi_gpio);
	gpio_free(pDev->spi_miso_gpio);
}

static void enable_fpga_power(struct device *dev)
{
	struct fvdkdata *data = dev_get_drvdata(dev);
	int ret;

	if (IS_ERR(data->reg_1v0_fpga) || IS_ERR(data->reg_1v2_fpga) ||
	    IS_ERR(data->reg_1v8_fpga) || IS_ERR(data->reg_2v5_fpga)
	    || IS_ERR(data->reg_3v15_fpga))
		return;

	if (fpgaIsEnabled)
		return;
	fpgaIsEnabled = true;
	dev_dbg(dev, "Fpga power enable\n");

	ret = regulator_enable(data->reg_1v0_fpga);
	ret |= regulator_enable(data->reg_1v8_fpga);
	ret |= regulator_enable(data->reg_1v2_fpga);
	ret |= regulator_enable(data->reg_2v5_fpga);
	ret |= regulator_enable(data->reg_3v15_fpga);

	if (ret)
		dev_err(dev, "can't enable fpga\n");
}

void BSPFvdPowerDownMX6S(struct device *dev)
{
	struct fvdkdata *data = dev_get_drvdata(dev);
	PFVD_DEV_INFO pDev = &data->pDev;
	int ret;

	// Disable FPGA
	if (IS_ERR(data->reg_1v0_fpga) || IS_ERR(data->reg_1v2_fpga) ||
	    IS_ERR(data->reg_1v8_fpga) || IS_ERR(data->reg_2v5_fpga)
	    || IS_ERR(data->reg_3v15_fpga))
		return;

	if (!fpgaIsEnabled)
		return;
	fpgaIsEnabled = false;
	dev_dbg(dev, "Fpga power disable\n");

	ret = regulator_disable(data->reg_3v15_fpga);
	ret |= regulator_disable(data->reg_2v5_fpga);
	ret |= regulator_disable(data->reg_1v2_fpga);
	ret |= regulator_disable(data->reg_1v8_fpga);
	ret |= regulator_disable(data->reg_1v0_fpga);

	gpio_direction_input(data->fpga_pins.program_gpio);
	gpio_direction_output(data->fpga_pins.init_gpio, 0);
	gpio_direction_input(pDev->spi_cs_gpio);

	pinctrl_select_state(data->pinctrl, data->pins_idle);

	if (ret)
		dev_err(dev, "can't disable fpga\n");
}

void BSPFvdPowerDownFPAMX6S(struct device *dev)
{
	struct fvdkdata *data = dev_get_drvdata(dev);
	int ret;

	if (IS_ERR(data->reg_fpa_i2c) || IS_ERR(data->reg_4v0_fpa))
		return;

	if (!fpaIsEnabled)
		return;
	fpaIsEnabled = false;
	dev_dbg(dev, "FPA power disable\n");

	ret = regulator_disable(data->reg_fpa_i2c);
	ret |= regulator_disable(data->reg_4v0_fpa);

	if (ret)
		dev_err(dev, "can't disable fpa\n");
}

void BSPFvdPowerUpFPAMX6S(struct device *dev)
{
	struct fvdkdata *data = dev_get_drvdata(dev);
	int ret;

	if (IS_ERR(data->reg_fpa_i2c) || IS_ERR(data->reg_4v0_fpa))
		return;

	if (fpaIsEnabled)
		return;
	fpaIsEnabled = true;
	dev_dbg(dev, "FPA power enable\n");

	ret = regulator_enable(data->reg_4v0_fpa);
	ret |= regulator_enable(data->reg_fpa_i2c);

	if (ret)
		dev_err(dev, "can't enable fpa\n");
}
