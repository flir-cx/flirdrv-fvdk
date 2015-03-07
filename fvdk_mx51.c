/***********************************************************************
 *                                                                     
 * $Date$
 * $Author$
 *
 * $Id$
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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
#include "../arch/arm/mach-imx/mx51.h"
#else
#include "mach/mx51.h"
#endif


// Definitions
#define FPGA_CE			((4-1)*32 + 24)		// GPIO 4.24
#define FPGA_CONF_DONE	((4-1)*32 + 25)
#define FPGA_CONFIG		((4-1)*32 + 26)
#define FPGA_STATUS		((1-1)*32 +  7)
#define FPGA_READY		((3-1)*32 + 13)
#define FPGA_POWER_EN	((3-1)*32 + 22)
#define FPA_POWER_EN	((3-1)*32 +  9)
#define FPA_I2C_EN		((3-1)*32 +  3)
#define FPGA_IRQ_0		((2-1)*32 + 23)

// Local prototypes


static BOOL SetupGpioAccessMX51(PFVD_DEV_INFO pDev);
static void CleanupGpioMX51(PFVD_DEV_INFO pDev);
static BOOL GetPinDoneMX51(void);
static BOOL GetPinStatusMX51(void);
static BOOL GetPinReadyMX51(void);
static DWORD PutInProgrammingModeMX51(PFVD_DEV_INFO);

static void BSPFvdPowerDownMX51(PFVD_DEV_INFO pDev);
static void BSPFvdPowerUpMX51(PFVD_DEV_INFO pDev);

// Local variables

static struct resource fvd_resources[] = {
	{
		.start = MX51_CS1_BASE_ADDR,
		.end = MX51_CS1_BASE_ADDR + 0x3FF,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = MX51_CS2_BASE_ADDR,
		.end = MX51_CS2_BASE_ADDR + 0x3FFF,
		.flags = IORESOURCE_MEM,
	},
};

// Code

void SetupMX51(PFVD_DEV_INFO pDev)
{
	pDev->pSetupGpioAccess = SetupGpioAccessMX51;
	pDev->pCleanupGpio = CleanupGpioMX51;
	pDev->pGetPinDone = GetPinDoneMX51;
	pDev->pGetPinStatus = GetPinStatusMX51;
	pDev->pGetPinReady = GetPinReadyMX51;
	pDev->pPutInProgrammingMode = PutInProgrammingModeMX51;
	pDev->pBSPFvdPowerUp = BSPFvdPowerUpMX51;
	pDev->pBSPFvdPowerDown = BSPFvdPowerDownMX51;

    platform_device_add_resources(pDev->pLinuxDevice, fvd_resources,
    							  ARRAY_SIZE(fvd_resources));

    pDev->iSpiBus = 1;			// SPI no = 1
    pDev->iSpiCountDivisor = 4;	// Count is no of words
    pDev->iI2c = 2;	// Main i2c bus
}


BOOL SetupGpioAccessMX51(PFVD_DEV_INFO pDev)
{
	if (gpio_is_valid(FPGA_CE) == 0)
	    pr_err("FpgaCE can not be used\n");
	if (gpio_is_valid(FPGA_CONF_DONE) == 0)
	    pr_err("FpgaConfDone can not be used\n");
	if (gpio_is_valid(FPGA_CONFIG) == 0)
	    pr_err("FpgaConfig can not be used\n");
	if (gpio_is_valid(FPGA_STATUS) == 0)
	    pr_err("FpgaStatus can not be used\n");
	if (gpio_is_valid(FPGA_READY) == 0)
	    pr_err("FpgaReady can not be used\n");
	if (gpio_is_valid(FPGA_POWER_EN) == 0)
	    pr_err("FpgaPowerEn can not be used\n");
	if (gpio_is_valid(FPA_POWER_EN) == 0)
	    pr_err("FpaPowerEn can not be used\n");
	if (gpio_is_valid(FPA_I2C_EN) == 0)
	    pr_err("FpaI2CEn can not be used\n");

	gpio_request(FPGA_CE, "FpgaCE");
	gpio_request(FPGA_CONF_DONE, "FpgaConfDone");
	gpio_request(FPGA_CONFIG, "FpgaConfig");
	gpio_request(FPGA_STATUS, "FpgaStatus");
	gpio_request(FPGA_READY, "FpgaReady");
	gpio_request(FPGA_POWER_EN, "FpgaPowerEn");
	gpio_request(FPA_POWER_EN, "FpaPowerEn");
	gpio_request(FPA_I2C_EN, "FpaI2CEn");

	gpio_direction_input(FPGA_CONF_DONE);
	gpio_direction_input(FPGA_STATUS);
	gpio_direction_input(FPGA_READY);

	gpio_direction_output(FPGA_CE, 1);
	gpio_direction_output(FPGA_CONFIG, 1);
	gpio_direction_output(FPGA_POWER_EN, 0);
	gpio_direction_output(FPA_POWER_EN, 0);
	gpio_direction_output(FPA_I2C_EN, 0);

    return TRUE;
}

void CleanupGpioMX51(PFVD_DEV_INFO pDev)
{
	gpio_free(FPGA_CE);
	gpio_free(FPGA_CONF_DONE);
	gpio_free(FPGA_CONFIG);
	gpio_free(FPGA_STATUS);
	gpio_free(FPGA_READY);
	gpio_free(FPGA_POWER_EN);
	gpio_free(FPA_POWER_EN);
	gpio_free(FPA_I2C_EN);
	free_irq(gpio_to_irq(FPGA_IRQ_0), pDev);
	gpio_free(FPGA_IRQ_0);
}

BOOL GetPinDoneMX51(void)
{
    return (gpio_get_value(FPGA_CONF_DONE) != 0);
}

BOOL GetPinStatusMX51(void)
{
    return (gpio_get_value(FPGA_STATUS) != 0);
}

BOOL GetPinReadyMX51(void)
{
    return (gpio_get_value(FPGA_READY) != 0);
}

DWORD PutInProgrammingModeMX51(PFVD_DEV_INFO pDev)
{
	// Set idle state (probably already done)
	gpio_set_value(FPGA_CONFIG, 1);
	msleep(1);

    // Activate programming (CONFIG  LOW)
	gpio_set_value(FPGA_CONFIG, 0);
	msleep(1);

    // Verify status
    if (GetPinStatusMX51())
    {
        pr_err("FPGA: Status not initially low\n");
        return 0;
    }
    if (GetPinDoneMX51())
    {
        pr_err("FPGA: Conf_Done not initially low\n");
        return 0;
    }

    // Release config
	gpio_set_value(FPGA_CONFIG, 1);
	msleep(1);

    // Verify status
    if (! GetPinStatusMX51())
    {
        pr_err("FPGA: Status not high when config released\n");
        return 0;
    }

	msleep(1);
    return 1;
}

void BSPFvdPowerUpMX51(PFVD_DEV_INFO pDev)
{
	gpio_set_value(FPGA_POWER_EN, 1);
	msleep(50);
	gpio_set_value(FPGA_CE, 0);
	gpio_set_value(FPGA_CONFIG, 1);
	msleep(2);
	gpio_set_value(FPA_POWER_EN, 1);
	msleep(5);
	gpio_set_value(FPA_I2C_EN, 1);
}

void BSPFvdPowerDownMX51(PFVD_DEV_INFO pDev)
{
    // This function should suspend power to the device.
    // It is useful only with devices that can power down under software control.
	gpio_set_value(FPA_I2C_EN, 0);
	msleep(1);
	gpio_set_value(FPA_POWER_EN, 0);

    // Disable FPGA
	gpio_set_value(FPGA_CE, 1);
	msleep(1);
	gpio_set_value(FPGA_POWER_EN, 0);
}

