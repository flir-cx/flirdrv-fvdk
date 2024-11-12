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
#include <linux/spi/spi.h>
#include <linux/mtd/spi-nor.h>
#include <linux/types.h>

#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/regulator/of_regulator.h>

/* Micron-st specific */
#define SPINOR_OP_MT_WR_ANY_REG	0x81	/* Write volatile register */

static BOOL ec702_setup_gpio_access(struct device *dev);
static void ec702_cleanup_gpio(struct device *dev);
static BOOL ec702_get_pin_done(struct device *dev);
static BOOL ec702_get_pin_status(struct device *dev);
static BOOL ec702_get_pin_ready(struct device *dev);
static DWORD ec702_enter_programming_mode(struct device *dev);

static void ec702_bsp_fvd_power_up(struct device *dev, BOOL restart);
static void ec702_bsp_fvd_power_down(struct device *dev);
static void ec702_bsp_fvd_power_up_fpa(struct device *dev);
static void ec702_bsp_fvd_power_down_fpa(struct device *dev);

static int ec702_reload_fpga(struct device *dev);
static int ec702_reg_enable(struct device *dev, struct regulator *reg,
	BOOL enable, const char *reg_name);
static int ec702_set_fpa_power(struct device *dev, BOOL enable);
static int ec702_set_fpga_power(struct device *dev, int enable);
static int set_spi_bus_active(struct device *dev, BOOL enable);

static int ec702_fpa_powered = 0;
static int ec702_fpga_powered = 0;

static struct spi_device *spi_dev;

/* Get SPI device, remember to put it after use */
static struct spi_device *get_spi_device_from_node_prop(struct device *dev)
{
	/* Find SPI device via phandle */
	static const char spi_flash_prop_name[] = "spi_fpga_flash";
	struct device_node *np = dev->of_node;
	struct device_node *spi_np = of_parse_phandle(np, spi_flash_prop_name, 0);

	if (spi_np) {
		struct spi_device *spi_dev;

		spi_dev = of_find_spi_device_by_node(spi_np);
		of_node_put(spi_np);

		if (!spi_dev)
			dev_err(dev, "cannot find ref to SPI flash driver\n");

		return spi_dev;
	}

	dev_err(dev, "property '%s' not found\n", spi_flash_prop_name);
	return NULL;
}

void Setup_FLIR_ec702(struct device *dev)
{
	struct fvdkdata *data = dev_get_drvdata(dev);
	PFVD_DEV_INFO pDev = &data->pDev;

	data->ops.pSetupGpioAccess = ec702_setup_gpio_access;
	data->ops.pCleanupGpio = ec702_cleanup_gpio;
	data->ops.pGetPinDone = ec702_get_pin_done;
	data->ops.pGetPinStatus = ec702_get_pin_status;
	data->ops.pGetPinReady = ec702_get_pin_ready;
	data->ops.pPutInProgrammingMode = ec702_enter_programming_mode;
	data->ops.pBSPFvdPowerUp = ec702_bsp_fvd_power_up;
	data->ops.pBSPFvdPowerDown = ec702_bsp_fvd_power_down;
	data->ops.pBSPFvdPowerUpFPA = ec702_bsp_fvd_power_up_fpa;
	data->ops.pBSPFvdPowerDownFPA = ec702_bsp_fvd_power_down_fpa;

	pDev->iI2c = 2; /* Main i2c bus */
	pDev->spi_flash = true;

	/* These handle that we call regulator_enable only once */
	ec702_fpa_powered = 0;
	ec702_fpga_powered = 0;

	spi_dev = get_spi_device_from_node_prop(dev);
}

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

static BOOL ec702_setup_gpio_access(struct device *dev)
{
	struct fvdkdata *data = dev_get_drvdata(dev);
	PFVD_DEV_INFO pDev = &data->pDev;
	struct device_node *np = dev->of_node;
	int result = TRUE;

	/* Get FPGA gpios */
	data->fpga_pins.pin_fpga_ce_n =
		get_and_request_gpio(dev, np, "fpga_ce_n", GPIOF_OUT_INIT_LOW);
	if (data->fpga_pins.pin_fpga_ce_n < 0)
		result = FALSE;
	data->fpga_pins.pin_fpga_config_n =
		get_and_request_gpio(dev, np, "fpga_config_n", GPIOF_IN);
	if (data->fpga_pins.pin_fpga_config_n < 0)
		result = FALSE;
	data->fpga_pins.pin_fpga_conf_done =
		get_and_request_gpio(dev, np, "fpga_conf_done", GPIOF_IN);
	if (data->fpga_pins.pin_fpga_conf_done < 0)
		result = FALSE;
	data->fpga_pins.pin_fpga_status_n =
		get_and_request_gpio(dev, np, "fpga_status_n", GPIOF_IN);
	if (data->fpga_pins.pin_fpga_status_n < 0)
		result = FALSE;

	data->fpga_pins.ready_gpio =
		get_and_request_gpio(dev, np, "fpga-ready-gpio", GPIOF_IN);
	if (data->fpga_pins.ready_gpio < 0)
		result = FALSE;

	/* Get SPI bus gpios */
	pDev->spi_sclk_gpio = of_get_named_gpio(np, "spi-sclk-gpio", 0);
	pDev->spi_mosi_gpio = of_get_named_gpio(np, "spi-mosi-gpio", 0);
	pDev->spi_miso_gpio = of_get_named_gpio(np, "spi-miso-gpio", 0);
	pDev->spi_cs_gpio = of_get_named_gpio(np, "spi-cs-gpio", 0);

	/* FPA regulators */
	data->reg_4v0_fpa = devm_regulator_get(dev, "fpa");
	if (IS_ERR(data->reg_4v0_fpa)) {
		dev_err(dev, "cannot get regulator supply fpa\n");
		result = FALSE;
	}


	/* FPGA regulators */
	data->reg_1v1_fpga = devm_regulator_get(dev, "1v1_fpga");
	if (IS_ERR(data->reg_1v1_fpga)) {
		dev_err(dev, "cannot get regulator supply 1v1_fpga");
		result = FALSE;
	}

	data->reg_1v2_fpga = devm_regulator_get(dev, "1v2_fpga");
	if (IS_ERR(data->reg_1v2_fpga)) {
		dev_err(dev, "can't get regulator supply 1v2_fpga");
		result = FALSE;
	}

	data->reg_1v8_fpga = devm_regulator_get(dev, "1v8_fpga");
	if (IS_ERR(data->reg_1v8_fpga)) {
		dev_err(dev, "cannot get regulator supply 1v8_fpga");
		result = FALSE;
	}

	data->reg_2v5_fpga = devm_regulator_get(dev, "2v5_fpga");
	if (IS_ERR(data->reg_2v5_fpga)) {
		dev_err(dev, "cannot get regulator supply 2v5_fpga");
		result = FALSE;
	}

	data->reg_3v15_fpga = devm_regulator_get(dev, "3v15_fpga");
	if (IS_ERR(data->reg_3v15_fpga)) {
		dev_err(dev, "cannot get regulator supply 3v15_fpga");
		result = FALSE;
	}

	if (!ec702_get_pin_done(dev)) {
		/* Expected state. U-Boot does not load FPGA */
		dev_info(dev, "FPGA not loaded by u-boot");
		/* Disable FPGA (CE_n=1) and hold CONFIG_n low */
		gpio_direction_output(data->fpga_pins.pin_fpga_ce_n, 1);
		gpio_direction_output(data->fpga_pins.pin_fpga_config_n, 0);
	} else {
		dev_warn(dev, "FPGA loaded by u-boot");
	}

	data->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(data->pinctrl)) {
		dev_err(dev, "cannot get pinctrl");
		result = FALSE;
	}

	data->pins_default = pinctrl_lookup_state(data->pinctrl, "spi-default");
	if (IS_ERR(data->pins_default)) {
		dev_err(dev, "cannot get default pins %p %p", data->pinctrl,
			data->pins_default);
		result = FALSE;
	}

	data->pins_idle = pinctrl_lookup_state(data->pinctrl, "spi-idle");
	if (IS_ERR(data->pins_idle)) {
		dev_err(dev, "cannot get idle pins %p %d", data->pinctrl,
			(int)(data->pins_idle));
		result = FALSE;
	}

	/* fpga power already on, but need to sync regulator_enable */
	if (ec702_set_fpga_power(dev, 1) != 0)
		result = FALSE;

	if (!result)
		dev_err(dev, "setup gpio access failed\n");

	/* Set SPI pins as SPI */
	if (set_spi_bus_active(dev, TRUE))
		dev_err(dev, "failed to set SPI pins active");

	return result;
}

static void ec702_cleanup_gpio(struct device *dev)
{
	ec702_set_fpga_power(dev, 0);

	if (spi_dev) {
		spi_dev_put(spi_dev);
		spi_dev = NULL;
	}
}

static BOOL ec702_get_pin_done(struct device *dev)
{
	struct fvdkdata *data = dev_get_drvdata(dev);

	return (gpio_get_value(data->fpga_pins.pin_fpga_conf_done) != 0);
}

static BOOL ec702_get_pin_status(struct device *dev)
{
	struct fvdkdata *data = dev_get_drvdata(dev);

	/* Return 1 when not driven low */
	return (gpio_get_value(data->fpga_pins.pin_fpga_status_n) != 0);
}

static BOOL ec702_get_pin_ready(struct device *dev)
{
	struct fvdkdata *data = dev_get_drvdata(dev);

	return (gpio_get_value(data->fpga_pins.ready_gpio) != 0);
}

static DWORD ec702_enter_programming_mode(struct device *dev)
{
	struct fvdkdata *data = dev_get_drvdata(dev);
	PFVD_DEV_INFO pDev = &data->pDev;

	(void)pDev;
	return 1;
}

/* Enable or disable SPI bus */
static int set_spi_bus_active(struct device *dev, BOOL enable)
{
	struct fvdkdata *data = dev_get_drvdata(dev);
	PFVD_DEV_INFO pDev = &data->pDev;
	int ret;

	if (enable) {
		ret = pinctrl_select_state(data->pinctrl, data->pins_default);
		if (ret != 0) {
			dev_err(dev, "cannot select SPI pin state, ret=%d\n", ret);
			goto out_err;
		}
		ret = gpio_direction_output(pDev->spi_cs_gpio, 1);
		if (ret != 0) {
			dev_err(dev, "cannot change SPI CS to output, ret=%d\n", ret);
			goto out_err;
		}
	} else {
		ret = gpio_direction_input(pDev->spi_cs_gpio);
		if (ret != 0) {
			dev_err(dev, "cannot change SPI CS to input, ret=%d\n", ret);
			goto out_err;
		}
		ret = pinctrl_select_state(data->pinctrl, data->pins_idle);
		if (ret != 0) {
			dev_err(dev, "cannot select SPI pin state, ret=%d\n", ret);
			goto out_err;
		}

		if (gpio_request(pDev->spi_sclk_gpio, "SPI1_SCLK") != 0) {
			ret = -ENODEV;
		} else {
			ret = gpio_direction_input(pDev->spi_sclk_gpio);
			gpio_free(pDev->spi_sclk_gpio);
		}
		if (ret != 0) {
			dev_err(dev, "SPI1_SCLK cannot be changed to input\n");
			goto out_err;
		}

		if (gpio_request(pDev->spi_mosi_gpio, "SPI1_MOSI") != 0) {
			ret = -ENODEV;
		} else {
			ret = gpio_direction_input(pDev->spi_mosi_gpio);
			gpio_free(pDev->spi_mosi_gpio);
		}
		if (ret != 0) {
			dev_err(dev, "SPI1_MOSI cannot be changed to input\n");
			goto out_err;
		}

		if (gpio_request(pDev->spi_miso_gpio, "SPI1_MISO") != 0) {
			ret = -ENODEV;
		} else {
			ret = gpio_direction_input(pDev->spi_miso_gpio);
			gpio_free(pDev->spi_miso_gpio);
		}
		if (ret != 0) {
			dev_err(dev, "SPI1_MISO cannot be changed to input\n");
			goto out_err;
		}
	}

out_err:
	return ret;
}

static void ec702_bsp_fvd_power_up(struct device *dev, BOOL restart)
{
	int ret;
	struct fvdkdata *data = dev_get_drvdata(dev);

	if (!restart) {
		dev_info(dev, "ignoring fvd power up without FPGA restart\n");
		return;
	}
	dev_info(dev, "fvd power up\n");

	ret = ec702_set_fpga_power(dev, TRUE);
	if (ret != 0)
		goto out_err;

	/* BC-236, FVD_Open (fvdc_main) sometimes fails.
	 * When failure occurs, the "read_spi_header()" indicate failure
	 * probably the pBSPFvdPowerUp (this routine) needs some settle
	 * time before we start to use HW depending on this voltage
	 */
	msleep(300); /* Max POR delay, until FPGA is ready for programming */

	/* Set SPI pins as SPI */
	ret = set_spi_bus_active(dev, TRUE);
	if (ret != 0)
		goto out_err;

	if (restart)
		ret = ec702_reload_fpga(dev);

out_err:
	if (ret != 0)
		dev_err(dev, "fvd power up failed\n");
}

static void ec702_bsp_fvd_power_down(struct device *dev)
{
	struct fvdkdata *data = dev_get_drvdata(dev);
	int ret;

	dev_info(dev, "fvd power down\n");

	/* Disable FPGA */
	ret = ec702_set_fpga_power(dev, FALSE);
	if (ret != 0)
		goto out_err;

	ret = gpio_direction_output(data->fpga_pins.pin_fpga_config_n, 0);
	if (ret != 0) {
		dev_err(dev, "failed to set FPGA_CONFIG_n low, ret=%d\n", ret);
		goto out_err;
	}
	ret = gpio_direction_input(data->fpga_pins.pin_fpga_ce_n);
	if (ret != 0) {
		dev_err(dev, "failed to set FPGA_CE_n to input, ret=%d\n", ret);
		goto out_err;
	}

	/* Disable SPI bus */
	ret = set_spi_bus_active(dev, FALSE);
	if (ret != 0) {
		dev_err(dev, "failed to disable SPI bus, ret=%d\n", ret);
		goto out_err;
	}

out_err:
	if (ret != 0)
		dev_err(dev, "fvd power down failed\n");
}

static void ec702_bsp_fvd_power_up_fpa(struct device *dev)
{
	ec702_set_fpa_power(dev, TRUE);
}

static void ec702_bsp_fvd_power_down_fpa(struct device *dev)
{
	ec702_set_fpa_power(dev, FALSE);
}

/**
 * Write a command to the spi flash, with optional data to be written.
 *
 * dout the data to be written, null if no parameters to cmd
 * len length of dout
 *
 * Return negative on error
 */
static int spi_flash_cmd(struct spi_device *spi_dev,
		u8 cmd, const u8 *dout, size_t len)
{
	u8 buf[4];

	if (len > 0 && !dout)
		return -EINVAL;
	else if (1 + len > sizeof(buf))
		return -EINVAL;

	buf[0] = cmd;
	if (len > 0)
		memcpy(&buf[1], dout, len);

	return spi_write_then_read(spi_dev, buf, 1 + len, NULL, 0);
}

/**
 * Set up spi flash according to altera spec
 * Return negative on error
 */
static int init_spi_flash_for_fpga_reload(struct device *dev,
		struct spi_device *spi_dev)
{
	u8 hold_disable_mask = 0xef;
	u8 twelve_dummy_bits_mask = 0xcb;
	int ret;

	if (!spi_dev)
		return -ENOENT;

	ret = spi_flash_cmd(spi_dev, SPINOR_OP_WREN, NULL, 0);
	if (ret != 0)
		goto out_err;
	ret = spi_flash_cmd(spi_dev, SPINOR_OP_WD_EVCR, &hold_disable_mask, 1);
	if (ret != 0)
		goto out_err;
	ret = spi_flash_cmd(spi_dev, SPINOR_OP_WREN, NULL, 0);
	if (ret != 0)
		goto out_err;
	ret = spi_flash_cmd(spi_dev, SPINOR_OP_MT_WR_ANY_REG, &twelve_dummy_bits_mask, 1);
	if (ret != 0)
		goto out_err;
	ret = spi_flash_cmd(spi_dev, SPINOR_OP_WREN, NULL, 0);
	if (ret != 0)
		goto out_err;
	ret = spi_flash_cmd(spi_dev, SPINOR_OP_EN4B, NULL, 0);

out_err:
	if (ret != 0)
		dev_err(dev, "spi_flash_cmd() failed with ret=%d\n", ret);
	return ret;
}

/**
 * Revert (some) flash chip settings
 * Use 16 dummy bits after FAST READ
 * Disable 4B mode
 * Return negative on error
 */
static int uninit_spi_flash_for_fpga_reload(struct device *dev,
		struct spi_device *spi_dev)
{
	u8 sixteen_dummy_bits_mask = 0xfb;
	int ret;

	if (!spi_dev)
		return -ENOENT;

	ret = spi_flash_cmd(spi_dev, SPINOR_OP_WREN, NULL, 0);
	if (ret != 0)
		goto out_err;
	ret = spi_flash_cmd(spi_dev, SPINOR_OP_MT_WR_ANY_REG, &sixteen_dummy_bits_mask, 1);
	if (ret != 0)
		goto out_err;
	ret = spi_flash_cmd(spi_dev, SPINOR_OP_WREN, NULL, 0);
	if (ret != 0)
		goto out_err;
	ret = spi_flash_cmd(spi_dev, SPINOR_OP_EX4B, NULL, 0);

out_err:
	if (ret != 0)
		dev_err(dev, "spi_flash_cmd() failed with ret=%d\n", ret);
	return ret;
}


static int ec702_reload_fpga(struct device *dev)
{
	struct fvdkdata *data = dev_get_drvdata(dev);
	int ret;
	int elapsed;
	BOOL done = FALSE;

	/* FPGA_CE_n must be disabled while we prepare SPI flash */
	ret = gpio_direction_output(data->fpga_pins.pin_fpga_ce_n, 1);
	if (ret != 0) {
		dev_err(dev, "failed to set FPGA_CE_n high, ret=%d\n", ret);
		goto out_err;
	}

	ret = init_spi_flash_for_fpga_reload(dev, spi_dev);
	if (ret != 0) {
		dev_err(dev, "failed to configure the SPI flash for reload\n");
		goto restore_spi_bus;
	}

	dev_dbg(dev, "configured SPI flash for fpga reload\n");

	/* Disable SPI bus during FPGA programming */
	ret = set_spi_bus_active(dev, FALSE);
	if (ret != 0)
		goto restore_spi_bus;

	/* CE_n and CONFIG_n should initially be low. If we fail, don't enable fpga */
	/* since that would make the fpga hog the spi bus which is bad for our spi driver*/
	ret = gpio_direction_output(data->fpga_pins.pin_fpga_config_n, 0);
	if (!ret)
		ret = gpio_direction_output(data->fpga_pins.pin_fpga_ce_n, 0);

	msleep(20);

	/* Release CONFIG_n to start config (has pull up resistor) */
	if (!ret)
		ret = gpio_direction_input(data->fpga_pins.pin_fpga_config_n);
	if (ret != 0)
		dev_err(dev, "failed to initiate FPGA load\n");

	elapsed = 0;
	do {
		const int delay = 10;
		msleep(delay);
		elapsed += delay;
		done = ec702_get_pin_done(dev);
		dev_dbg(dev, "FPGA pin done=%d, status=%d\n", done, ec702_get_pin_status(dev));
	} while (!done && elapsed < 500); /* ms */

	if (!done) {
		dev_err(dev, "FPGA load failed");
		gpio_direction_output(data->fpga_pins.pin_fpga_config_n, 0);
		gpio_direction_output(data->fpga_pins.pin_fpga_ce_n, 1);
	} else {
		dev_info(dev, "FPGA loaded in %d ms\n", elapsed);
	}

restore_spi_bus:
	{
		/* Enable SPI bus again */
		int ret2 = set_spi_bus_active(dev, TRUE);

		if (!ret2)
			ret2 = uninit_spi_flash_for_fpga_reload(dev, spi_dev);

		if (ret2 != 0)
			dev_err(dev, "failed to de-configure the SPI flash\n");
		else
			dev_dbg(dev, "deconfigured SPI flash for fpga reload\n");

		/* Set return value if not failed before */
		if (!ret)
			ret = ret2;
	}

	/* Check FPGA load result */
	if (!ret && !done)
		ret = -EFAULT;

out_err:
	if (ret != 0)
		dev_err(dev, "reload_fpga failed\n");

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

	if (enable)
		ret = regulator_enable(reg);
	else
		ret = regulator_disable(reg);

	if (ret != 0)
		dev_err(dev, "cannot set regulator '%s' to %s, ret=%d\n",
			reg_name, enable ? "enabled" : "disabled", ret);

	return ret;
}

static int ec702_set_fpa_power(struct device *dev, BOOL enable)
{
	struct fvdkdata *data = dev_get_drvdata(dev);
	int ret = 0;

	if (enable && !ec702_fpa_powered) {
		dev_dbg(dev, "fpa power enable\n");
		ret = regulator_enable(data->reg_4v0_fpa);
		if (ret != 0)
			goto out_err;
		ec702_fpa_powered = TRUE;
	} else if (!enable && ec702_fpa_powered) {
		dev_dbg(dev, "fpa power disable\n");
		ret = regulator_disable(data->reg_4v0_fpa);
		if (ret != 0)
			goto out_err;
		ec702_fpa_powered = FALSE;
	}

out_err:
	if (ret != 0)
		dev_err(dev, "change fpa power failed\n");

	return ret;
}

static int ec702_set_fpga_power(struct device *dev, BOOL enable)
{
	struct fvdkdata *data = dev_get_drvdata(dev);
	int ret = 0;

	if (enable && !ec702_fpga_powered) {
		dev_dbg(dev, "fpga power enable\n");
		ret = ec702_reg_enable(dev, data->reg_1v1_fpga, TRUE, "1V1D_FPGA");
		if (ret != 0)
			goto out_err;
		ret = ec702_reg_enable(dev, data->reg_1v2_fpga, TRUE, "1V2D_FPGA");
		if (ret != 0)
			goto out_err;
		ret = ec702_reg_enable(dev, data->reg_1v8_fpga, TRUE, "1V8D_FPGA");
		if (ret != 0)
			goto out_err;
		ret = ec702_reg_enable(dev, data->reg_2v5_fpga, TRUE, "2V5D_FPGA");
		if (ret != 0)
			goto out_err;
		ret = ec702_reg_enable(dev, data->reg_3v15_fpga, TRUE, "3V15D_FPGA");
		if (ret != 0)
			goto out_err;
		ec702_fpga_powered = TRUE;
	} else if (!enable && ec702_fpga_powered) {
		dev_dbg(dev, "fpga power disable\n");
		ret = ec702_reg_enable(dev, data->reg_3v15_fpga, FALSE, "3V15D_FPGA");
		if (ret != 0)
			goto out_err;
		ret = ec702_reg_enable(dev, data->reg_2v5_fpga, FALSE, "2V5D_FPGA");
		if (ret != 0)
			goto out_err;
		ret = ec702_reg_enable(dev, data->reg_1v8_fpga, FALSE, "1V8D_FPGA");
		if (ret != 0)
			goto out_err;
		ret = ec702_reg_enable(dev, data->reg_1v2_fpga, FALSE, "1V2D_FPGA");
		if (ret != 0)
			goto out_err;
		ret = ec702_reg_enable(dev, data->reg_1v1_fpga, FALSE, "1V1D_FPGA");
		if (ret != 0)
			goto out_err;
		ec702_fpga_powered = FALSE;
	}

out_err:
	if (ret != 0)
		dev_err(dev, "change fpga power failed\n");

	return ret;
}
