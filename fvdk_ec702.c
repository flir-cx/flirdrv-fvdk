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

#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/regulator/consumer.h>

#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/regulator/of_regulator.h>
#endif


static BOOL ec702_setup_gpio_access(PFVD_DEV_INFO pDev);
static void ec702_cleanup_gpio(PFVD_DEV_INFO pDev);
static BOOL ec702_get_pin_done(PFVD_DEV_INFO pDev);
static BOOL ec702_get_pin_status(PFVD_DEV_INFO pDev);
static BOOL ec702_get_pin_ready(PFVD_DEV_INFO pDev);
static DWORD ec702_enter_programming_mode(PFVD_DEV_INFO pDev);

static void ec702_bsp_fvd_power_up(PFVD_DEV_INFO pDev, BOOL restart);
static void ec702_bsp_fvd_power_down(PFVD_DEV_INFO pDev);
static void ec702_bsp_fvd_power_up_fpa(PFVD_DEV_INFO pDev);
static void ec702_bsp_fvd_power_down_fpa(PFVD_DEV_INFO pDev);

static int ec702_reload_fpga(PFVD_DEV_INFO pDev);
static int ec702_reg_enable(struct device *dev, struct regulator *reg,
	BOOL enable, const char *reg_name);
static int ec702_set_fpa_power(PFVD_DEV_INFO pDev, BOOL enable);
static int ec702_set_fpga_power(PFVD_DEV_INFO pDev, int enable);

static int ec702_fpa_powered = 0;
static int ec702_fpga_powered = 0;

void Setup_FLIR_ec702(PFVD_DEV_INFO pDev)
{
	pDev->pSetupGpioAccess = ec702_setup_gpio_access;
	pDev->pCleanupGpio = ec702_cleanup_gpio;
	pDev->pGetPinDone = ec702_get_pin_done;
	pDev->pGetPinStatus = ec702_get_pin_status;
	pDev->pGetPinReady = ec702_get_pin_ready;
	pDev->pPutInProgrammingMode = ec702_enter_programming_mode;
	pDev->pBSPFvdPowerUp = ec702_bsp_fvd_power_up;
	pDev->pBSPFvdPowerDown = ec702_bsp_fvd_power_down;
	pDev->pBSPFvdPowerUpFPA = ec702_bsp_fvd_power_up_fpa;
	pDev->pBSPFvdPowerDownFPA = ec702_bsp_fvd_power_down_fpa;
	pDev->iI2c = 2; /* Main i2c bus */
	pDev->spi_flash = true;

	/* These handle that we call regulator_enable only once */
	ec702_fpa_powered = 0;
	ec702_fpga_powered = 0;
}

#ifdef CONFIG_OF
static int get_and_request_gpio(struct device *dev,
		struct device_node *np, const char *name, unsigned long flags)
{
	int gpio = of_get_named_gpio(np, name, 0);
	if (!gpio_is_valid(gpio)) {
		dev_err(dev, "cannot find '%s' in dtb or invalid gpio\n", name);
		return -EINVAL;
	} else if (devm_gpio_request_one(dev, gpio, flags, name) != 0) {
		dev_err(dev, "unable to get gpio %d: '%s'\n", gpio, name);
		return -ENODEV;
	}

	return gpio;
}

static BOOL ec702_setup_gpio_access(PFVD_DEV_INFO pDev)
{
	struct device *dev = &pDev->pLinuxDevice->dev;
	struct device_node *np = dev->of_node;
	int result = TRUE;

	/* Get FPGA gpios */
	pDev->pin_fpga_ce_n =
		get_and_request_gpio(dev, np, "fpga_ce_n", GPIOF_IN);
	if (pDev->pin_fpga_ce_n < 0)
		result = FALSE;
	pDev->pin_fpga_config_n =
		get_and_request_gpio(dev, np, "fpga_config_n", GPIOF_IN);
	if (pDev->pin_fpga_config_n < 0)
		result = FALSE;
	pDev->pin_fpga_conf_done =
		get_and_request_gpio(dev, np, "fpga_conf_done", GPIOF_IN);
	if (pDev->pin_fpga_conf_done < 0)
		result = FALSE;
	pDev->pin_fpga_status_n =
		get_and_request_gpio(dev, np, "fpga_status_n", GPIOF_IN);
	if (pDev->pin_fpga_status_n < 0)
		result = FALSE;

	pDev->ready_gpio =
		get_and_request_gpio(dev, np, "fpga-ready-gpio", GPIOF_IN);
	if (pDev->ready_gpio < 0)
		result = FALSE;

	/* Get SPI bus gpios */
	pDev->spi_sclk_gpio = of_get_named_gpio(np, "spi-sclk-gpio", 0);
	pDev->spi_mosi_gpio = of_get_named_gpio(np, "spi-mosi-gpio", 0);
	pDev->spi_miso_gpio = of_get_named_gpio(np, "spi-miso-gpio", 0);
	pDev->spi_cs_gpio = of_get_named_gpio(np, "spi-cs-gpio", 0);

	/* FPA regulators */
	pDev->reg_4v0_fpa = devm_regulator_get(dev, "4V0_fpa");
	if (IS_ERR(pDev->reg_4v0_fpa)) {
		dev_err(dev, "cannot get regulator 4V0_fpa\n");
		result = FALSE;
	}

	pDev->reg_fpa_i2c = devm_regulator_get(dev, "fpa_i2c");
	if (IS_ERR(pDev->reg_fpa_i2c)) {
		dev_err(dev, "cannot get regulator fpa_i2c\n");
		result = FALSE;
	}

	/* FPGA regulators */
	pDev->reg_1v1_fpga = devm_regulator_get(dev, "DA9063_BPRO");
	if (IS_ERR(pDev->reg_1v1_fpga)) {
		dev_err(dev, "cannot get regulator DA9063_BPRO");
		result = FALSE;
	}

	pDev->reg_1v2_fpga = devm_regulator_get(dev, "DA9063_PERI_SW");
	if (IS_ERR(pDev->reg_1v2_fpga)) {
		dev_err(dev, "can't get regulator DA9063_PERI_SW");
		result = FALSE;
	}

	pDev->reg_1v8_fpga = devm_regulator_get(dev, "DA9063_CORE_SW");
	if (IS_ERR(pDev->reg_1v8_fpga)) {
		dev_err(dev, "cannot get regulator DA9063_CORE_SW");
		result = FALSE;
	}

	pDev->reg_2v5_fpga = devm_regulator_get(dev, "DA9063_BMEM");
	if (IS_ERR(pDev->reg_2v5_fpga)) {
		dev_err(dev, "cannot get regulator DA9063_BMEM");
		result = FALSE;
	}

	pDev->reg_3v15_fpga = devm_regulator_get(dev, "DA9063_LDO8");
	if (IS_ERR(pDev->reg_3v15_fpga)) {
		dev_err(dev, "cannot get regulator DA9063_LDO8");
		result = FALSE;
	}

	if (!ec702_get_pin_done(pDev)) {
		dev_err(dev, "U-boot FPGA load failed");
		/* Hold CONFIG_n low */
		(void)gpio_direction_output(pDev->pin_fpga_config_n, 0);
	}

	pDev->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(pDev->pinctrl)) {
		dev_err(dev, "cannot get pinctrl");
		result = FALSE;
	}

	pDev->pins_default = pinctrl_lookup_state(pDev->pinctrl, "spi-default");
	if (IS_ERR(pDev->pins_default)) {
		dev_err(dev, "cannot get default pins %p %p", pDev->pinctrl,
			pDev->pins_default);
		result = FALSE;
	}

	pDev->pins_idle = pinctrl_lookup_state(pDev->pinctrl, "spi-idle");
	if (IS_ERR(pDev->pins_idle)) {
		dev_err(dev, "cannot get idle pins %p %d", pDev->pinctrl,
			(int)(pDev->pins_idle));
		result = FALSE;
	}

	/* fpga power already on, but need to sync regulator_enable */
	if (ec702_set_fpga_power(pDev, 1) != 0) {
		result = FALSE;
	}
	(void)of_node_put(np);

	if (!result) {
		dev_err(dev, "setup gpio access failed\n");
	}

	return result;
}
#else
static BOOL ec702_setup_gpio_access(PFVD_DEV_INFO pDev)
{
	(void)pDev;
	return FALSE;
}
#endif

static void ec702_cleanup_gpio(PFVD_DEV_INFO pDev)
{
	/* devm does the cleanup */
	(void)ec702_set_fpga_power(pDev, 0);
}

static BOOL ec702_get_pin_done(PFVD_DEV_INFO pDev)
{
	return (gpio_get_value(pDev->pin_fpga_conf_done) != 0);
}

static BOOL ec702_get_pin_status(PFVD_DEV_INFO pDev)
{
	/* Return 1 when not driven low */
	return (gpio_get_value(pDev->pin_fpga_status_n) != 0);
}

static BOOL ec702_get_pin_ready(PFVD_DEV_INFO pDev)
{
	return (gpio_get_value(pDev->ready_gpio) != 0);
}

static DWORD ec702_enter_programming_mode(PFVD_DEV_INFO pDev)
{
	(void)pDev;
	return 1;
}

static void ec702_bsp_fvd_power_up(PFVD_DEV_INFO pDev, BOOL restart)
{
	int ret = 0;
	ret |= ec702_set_fpga_power(pDev, TRUE);

	/* BC-236, FVD_Open (fvdc_main) sometimes fails.
	 * When failure occurs, the "read_spi_header()" indicate failure
	 * probably the pBSPFvdPowerUp (this routine) needs some settle
	 * time before we start to use HW depending on this voltage
	 */
	msleep(300);

	if (restart) {
		ret |= ec702_reload_fpga(pDev);
	}

	if (ret != 0) {
		dev_err(&pDev->pLinuxDevice->dev, "fvd power up failed\n");
	} else {
		dev_dbg(&pDev->pLinuxDevice->dev, "fvd power up done\n");
	}
}

static void ec702_bsp_fvd_power_down(PFVD_DEV_INFO pDev)
{
	int ret = 0;

	/* Disable FPGA */
	ret |= gpio_direction_output(pDev->pin_fpga_ce_n, 1);
	ret |= ec702_set_fpga_power(pDev, FALSE);
	ret |= gpio_direction_input(pDev->pin_fpga_config_n);
	ret |= gpio_direction_input(pDev->spi_cs_gpio);
	ret |= pinctrl_select_state(pDev->pinctrl, pDev->pins_idle);

	if (ret != 0) {
		dev_err(&pDev->pLinuxDevice->dev, "fvd power down failed\n");
	} else {
		dev_dbg(&pDev->pLinuxDevice->dev, "fvd power down done\n");
	}
}

static void ec702_bsp_fvd_power_up_fpa(PFVD_DEV_INFO pDev)
{
	(void)ec702_set_fpa_power(pDev, TRUE);
}

static void ec702_bsp_fvd_power_down_fpa(PFVD_DEV_INFO pDev)
{
	(void)ec702_set_fpa_power(pDev, FALSE);
}

static int ec702_reload_fpga(PFVD_DEV_INFO pDev)
{
	struct device *dev = &pDev->pLinuxDevice->dev;
	int ret = 0;
	int elapsed;
	BOOL done;

	/* Disable SPI bus */
	ret |= pinctrl_select_state(pDev->pinctrl, pDev->pins_idle);
	/* gpio requested in spi-imx */
	ret |= gpio_direction_input(pDev->spi_cs_gpio);

	if (gpio_request(pDev->spi_sclk_gpio, "SPI1_SCLK") != 0) {
		dev_err(dev, "SPI1_SCLK cannot be requested\n");
		ret = -ENODEV;
	} else {
		ret |= gpio_direction_input(pDev->spi_sclk_gpio);
	}

	if (gpio_request(pDev->spi_mosi_gpio, "SPI1_MOSI") != 0) {
		dev_err(dev, "SPI1_MOSI cannot be requested\n");
		ret = -ENODEV;
	} else {
		ret |= gpio_direction_input(pDev->spi_mosi_gpio);
	}

	if (gpio_request(pDev->spi_miso_gpio, "SPI1_MISO") != 0) {
		dev_err(dev, "SPI1_MISO cannot be requested\n");
		ret = -ENODEV;
	} else {
		ret |= gpio_direction_input(pDev->spi_miso_gpio);
	}

	/* CE_n and CONFIG_n initially low */
	ret |= gpio_direction_output(pDev->pin_fpga_config_n, 0);

	msleep(5);

	/* Release CONFIG_n to start config (has pull up resistor) */
	ret |= gpio_direction_input(pDev->pin_fpga_config_n);

	elapsed = 0;
	do {
		const int delay = 5;
		msleep(delay);
		elapsed += delay;
		done = ec702_get_pin_done(pDev);
	} while (!done && elapsed < 500); /* ms */

	if (!done) {
		dev_err(dev, "FPGA load failed");
		(void)gpio_direction_output(pDev->pin_fpga_config_n, 0);
		ret = -EFAULT;
	} else {
		dev_info(dev, "FPGA loaded in %d ms\n", elapsed);
	}

	/* Set SPI pins as SPI */
	ret |= gpio_direction_output(pDev->spi_cs_gpio, 1);
	ret |= pinctrl_select_state(pDev->pinctrl, pDev->pins_default);
	gpio_free(pDev->spi_sclk_gpio);
	gpio_free(pDev->spi_mosi_gpio);
	gpio_free(pDev->spi_miso_gpio);

	if (ret != 0) {
		dev_err(dev, "reload_fpga failed\n");
	}

	return ret;
}

/* Control regulator enabled/disabled */
static int ec702_reg_enable(struct device *dev, struct regulator *reg,
	BOOL enable, const char *reg_name)
{
	int ret;

	if (IS_ERR(reg)) {
		dev_err(dev, "regulator '%s' not ok\n", reg_name);
		return -EFAULT;
	}

	if (enable) {
		ret = regulator_enable(reg);
	} else {
		ret = regulator_disable(reg);
	}

	if (ret != 0) {
		dev_err(dev, "cannot set regulator '%s' to %s, ret=%d\n",
			reg_name, enable ? "enabled" : "disabled", ret);
	}

	return ret;
}

static int ec702_set_fpa_power(PFVD_DEV_INFO pDev, BOOL enable)
{
	struct device *dev = &pDev->pLinuxDevice->dev;
	int ret = 0;

	if (enable && !ec702_fpa_powered) {
		dev_dbg(dev, "fpa power enable\n");
		ec702_fpa_powered = TRUE;
		ret |= ec702_reg_enable(dev, pDev->reg_4v0_fpa, TRUE, "4V0_fpa");
		ret |= ec702_reg_enable(dev, pDev->reg_fpa_i2c, TRUE, "fpa_i2c");
	} else if (!enable && ec702_fpa_powered) {
		dev_dbg(dev, "fpa power disable\n");
		ec702_fpa_powered = FALSE;
		ret |= ec702_reg_enable(dev, pDev->reg_fpa_i2c, FALSE, "fpa_i2c");
		ret |= ec702_reg_enable(dev, pDev->reg_4v0_fpa, FALSE, "4V0_fpa");
	}

	if (ret != 0) {
		dev_err(dev, "change fpa power failed\n");
	}

	return ret;
}

static int ec702_set_fpga_power(PFVD_DEV_INFO pDev, BOOL enable)
{
	struct device *dev = &pDev->pLinuxDevice->dev;
	int ret = 0;

	if (enable && !ec702_fpga_powered) {
		dev_dbg(dev, "fpga power enable\n");
		ec702_fpga_powered = TRUE;
		ret |= ec702_reg_enable(dev, pDev->reg_1v1_fpga, TRUE, "1V1D_FPGA");
		ret |= ec702_reg_enable(dev, pDev->reg_1v2_fpga, TRUE, "1V2D_FPGA");
		ret |= ec702_reg_enable(dev, pDev->reg_1v8_fpga, TRUE, "1V8D_FPGA");
		ret |= ec702_reg_enable(dev, pDev->reg_2v5_fpga, TRUE, "2V5D_FPGA");
		ret |= ec702_reg_enable(dev, pDev->reg_3v15_fpga, TRUE, "3V15D_FPGA");
	} else if (!enable && ec702_fpga_powered) {
		dev_dbg(dev, "fpga power disable\n");
		ec702_fpga_powered = FALSE;
		ret |= ec702_reg_enable(dev, pDev->reg_3v15_fpga, FALSE, "3V15D_FPGA");
		ret |= ec702_reg_enable(dev, pDev->reg_2v5_fpga, FALSE, "2V5D_FPGA");
		ret |= ec702_reg_enable(dev, pDev->reg_1v8_fpga, FALSE, "1V8D_FPGA");
		ret |= ec702_reg_enable(dev, pDev->reg_1v2_fpga, FALSE, "1V2D_FPGA");
		ret |= ec702_reg_enable(dev, pDev->reg_1v1_fpga, FALSE, "1V1D_FPGA");
	}

	if (ret != 0) {
		dev_err(dev, "change fpga power failed\n");
	}

	return ret;
}
