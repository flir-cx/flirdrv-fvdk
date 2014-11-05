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


#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
#include "../arch/arm/mach-imx/mx6.h"
#else
#include "mach/mx6.h"
#endif


// Definitions
#define FPGA_CE			((5-1)*32 + 28)		// GPIO 5.28
#define FPGA_CONF_DONE	((5-1)*32 + 27)
#define FPGA_CONFIG		((5-1)*32 + 25)     // GPIO 5.25
#define FPGA_STATUS		((5-1)*32 + 26)
#define FPGA_READY		((3-1)*32 + 19)    // GPIO 3.19
#define FPGA_POWER_EN	((6-1)*32 + 23)
#define FPA_POWER_EN	((6-1)*32 + 29)
#define FPGA_IRQ_0		((3-1)*32 + 16)   // GPIO 3.16

#define RETAILMSG(a,b) if (a) pr_err  b

// FPGA register 41 bits
#define FPA_CLCK_ENABLE     0x01
#define IRDM_PDWN_n         0x02
#define IRDM_RESET_n        0x04
#define IRDM_MClk           0x08
#define SpimSendReqFSEn     0x10
#define USE_SPI_CRC         0x1000

// Local prototypes

static BOOL SetupGpioAccessMX6Q(void);
static void CleanupGpioMX6Q(PFVD_DEV_INFO pDev);
static BOOL GetPinDoneMX6Q(void);
static BOOL GetPinStatusMX6Q(void);
static BOOL GetPinReadyMX6Q(void);
static DWORD PutInProgrammingModeMX6Q(PFVD_DEV_INFO);

static void BSPFvdPowerDownMX6Q(PFVD_DEV_INFO pDev);
static void BSPFvdPowerDownFPAMX6Q(PFVD_DEV_INFO pDev);
static void BSPFvdPowerUpMX6Q(PFVD_DEV_INFO pDev);
static void BSPFvdPowerUpFPAMX6Q(PFVD_DEV_INFO pDev);

// Local variables

// Code

void SetupMX6Q(PFVD_DEV_INFO pDev)
{
    pDev->pSetupGpioAccess = SetupGpioAccessMX6Q;
    pDev->pCleanupGpio = CleanupGpioMX6Q;
    pDev->pGetPinDone = GetPinDoneMX6Q;
    pDev->pGetPinStatus = GetPinStatusMX6Q;
    pDev->pGetPinReady = GetPinReadyMX6Q;
    pDev->pPutInProgrammingMode = PutInProgrammingModeMX6Q;
    pDev->pBSPFvdPowerUp = BSPFvdPowerUpMX6Q;
    pDev->pBSPFvdPowerDown = BSPFvdPowerDownMX6Q;
    pDev->pBSPFvdPowerDownFPA = BSPFvdPowerDownFPAMX6Q;
    pDev->pBSPFvdPowerUpFPA = BSPFvdPowerUpFPAMX6Q;

    pDev->iSpiBus = 32766;			// SPI no = 0
    pDev->iSpiCountDivisor = 1;	// Count is no of bytes
}

BOOL SetupGpioAccessMX6Q(void)
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

	gpio_request(FPGA_CE, "FpgaCE");
	gpio_request(FPGA_CONF_DONE, "FpgaConfDone");
	gpio_request(FPGA_CONFIG, "FpgaConfig");
	gpio_request(FPGA_STATUS, "FpgaStatus");
	gpio_request(FPGA_READY, "FpgaReady");
	gpio_request(FPGA_POWER_EN, "FpgaPowerEn");
	gpio_request(FPA_POWER_EN, "FpaPowerEn");

	gpio_direction_input(FPGA_CONF_DONE);
	gpio_direction_input(FPGA_STATUS);
	gpio_direction_input(FPGA_READY);

	gpio_direction_output(FPGA_CE, 1);
	gpio_direction_output(FPGA_CONFIG, 1);
	gpio_direction_output(FPGA_POWER_EN, 0);
	gpio_direction_output(FPA_POWER_EN, 0);

    return TRUE;
}

void CleanupGpioMX6Q(PFVD_DEV_INFO pDev)
{
	gpio_free(FPGA_CE);
	gpio_free(FPGA_CONF_DONE);
	gpio_free(FPGA_CONFIG);
	gpio_free(FPGA_STATUS);
	gpio_free(FPGA_READY);
	gpio_free(FPGA_POWER_EN);
	gpio_free(FPA_POWER_EN);
}

BOOL GetPinDoneMX6Q(void)
{
    return (gpio_get_value(FPGA_CONF_DONE) != 0);
}

BOOL GetPinStatusMX6Q(void)
{
    return (gpio_get_value(FPGA_STATUS) != 0);
}

BOOL GetPinReadyMX6Q(void)
{
    return (gpio_get_value(FPGA_READY) != 0);
}

DWORD PutInProgrammingModeMX6Q(PFVD_DEV_INFO pDev)
{
	// Set idle state (probably already done)
	gpio_set_value(FPGA_CONFIG, 1);
	msleep(1);

    // Activate programming (CONFIG  LOW)
	gpio_set_value(FPGA_CONFIG, 0);
	msleep(1);

    // Verify status
    if (GetPinStatusMX6Q())
    {
        pr_err("FPGA: Status not initially low\n");
        return 0;
    }
    if (GetPinDoneMX6Q())
    {
        pr_err("FPGA: Conf_Done not initially low\n");
        return 0;
    }

    // Release config
	gpio_set_value(FPGA_CONFIG, 1);
	msleep(1);

    // Verify status
    if (! GetPinStatusMX6Q())
    {
        pr_err("FPGA: Status not high when config released\n");
        return 0;
    }

	msleep(1);
    return 1;
}

void BSPFvdPowerUpMX6Q(PFVD_DEV_INFO pDev)
{
    gpio_set_value(FPGA_POWER_EN, 0);
	msleep(50);
	gpio_set_value(FPGA_CE, 0);
	gpio_set_value(FPGA_CONFIG, 1);
}

void BSPFvdPowerDownMX6Q(PFVD_DEV_INFO pDev)
{
    // This function should suspend power to the device.
    // It is useful only with devices that can power down under software control.
    gpio_set_value(FPA_POWER_EN, 1);

    // Disable FPGA
	gpio_set_value(FPGA_CE, 1);
	msleep(1);
	gpio_set_value(FPGA_POWER_EN, 0);
}

// Separate FPA power down
void BSPFvdPowerDownFPAMX6Q(PFVD_DEV_INFO pDev)
{
    gpio_set_value(FPA_POWER_EN, 0);
}

void BSPFvdPowerUpFPAMX6Q(PFVD_DEV_INFO pDev)
{
    gpio_set_value(FPA_POWER_EN, 1);
}