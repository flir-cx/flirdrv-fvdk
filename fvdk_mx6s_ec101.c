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
#include <linux/regulator/consumer.h>

#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/regulator/of_regulator.h>
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
    #include "../arch/arm/mach-imx/mx6.h"
#else	/*  */
    #include "mach/mx6.h"
    #define devm_regulator_get regulator_get
#endif	/*  */
     

    
// Local prototypes
static BOOL SetupGpioAccessMX6S(PFVD_DEV_INFO pDev);
static void CleanupGpioMX6S(PFVD_DEV_INFO pDev);
static BOOL GetPinDoneMX6S(PFVD_DEV_INFO pDev);
static BOOL GetPinStatusMX6S(PFVD_DEV_INFO pDev);
static BOOL GetPinReadyMX6S(PFVD_DEV_INFO pDev);
static DWORD PutInProgrammingModeMX6S(PFVD_DEV_INFO);
static void BSPFvdPowerDownMX6S(PFVD_DEV_INFO pDev);
static void BSPFvdPowerDownFPAMX6S(PFVD_DEV_INFO pDev);
static void BSPFvdPowerUpMX6S(PFVD_DEV_INFO pDev, BOOL restart);
static void BSPFvdPowerUpFPAMX6S(PFVD_DEV_INFO pDev);

// Local variables
static bool fpaIsEnabled = false;

// Code
void SetupMX6S_ec101(PFVD_DEV_INFO pDev)
{
	pDev->pSetupGpioAccess = SetupGpioAccessMX6S;
	pDev->pCleanupGpio = CleanupGpioMX6S;
	pDev->pGetPinDone = GetPinDoneMX6S;
	pDev->pGetPinStatus = GetPinStatusMX6S;
	pDev->pGetPinReady = GetPinReadyMX6S;
	pDev->pPutInProgrammingMode = PutInProgrammingModeMX6S;
	pDev->pBSPFvdPowerUp = BSPFvdPowerUpMX6S;
	pDev->pBSPFvdPowerDown = BSPFvdPowerDownMX6S;
	pDev->pBSPFvdPowerDownFPA = BSPFvdPowerDownFPAMX6S;
	pDev->pBSPFvdPowerUpFPA = BSPFvdPowerUpFPAMX6S;
	pDev->iI2c = 3;		// Main i2c bus
	pDev->spi_flash = true;
}


BOOL SetupGpioAccessMX6S(PFVD_DEV_INFO pDev)
{
   #ifdef CONFIG_OF
    struct device *dev = &pDev->pLinuxDevice->dev;
    struct device_node *np = pDev->np;
    int ret;

    pDev->program_gpio = of_get_named_gpio(np, "fpga-program-gpio", 0);
	if (gpio_is_valid(pDev->program_gpio)) {
		ret = devm_gpio_request_one(dev, pDev->program_gpio,
					    GPIOF_OUT_INIT_HIGH, "FPGA program");
		if (ret) {
			dev_err(dev, "unable to get FPGA program gpio\n");
		}
	}

    pDev->init_gpio = of_get_named_gpio(np, "fpga-init-gpio", 0);
	if (gpio_is_valid(pDev->init_gpio)) {
		ret = devm_gpio_request_one(dev, pDev->init_gpio,
					    GPIOF_OUT_INIT_HIGH, "FPGA init");
		if (ret) {
			dev_err(dev, "unable to get FPGA init gpio\n");
		}
	}

    pDev->conf_done_gpio = of_get_named_gpio(np, "fpga-conf-done-gpio", 0);
	if (gpio_is_valid(pDev->conf_done_gpio)) {
		ret = devm_gpio_request_one(dev, pDev->conf_done_gpio,
					    GPIOF_IN, "FPGA conf done");
		if (ret) {
			dev_err(dev, "unable to get FPGA conf done gpio\n");
		}
	}

    pDev->ready_gpio = of_get_named_gpio(np, "fpga-ready-gpio", 0);
	if (gpio_is_valid(pDev->ready_gpio)) {
		ret = devm_gpio_request_one(dev, pDev->ready_gpio,
					    GPIOF_IN, "FPGA ready");
		if (ret) {
			dev_err(dev, "unable to get FPGA ready gpio\n");
		}
	}

	pDev->reg_4v0_fpa = devm_regulator_get(dev, "4V0_fpa");
    if(IS_ERR(pDev->reg_4v0_fpa))
        dev_err(dev,"can't get regulator 4V0_fpa");

	pDev->reg_fpa_i2c = devm_regulator_get(dev, "fpa_i2c");
    if(IS_ERR(pDev->reg_fpa_i2c))
        dev_err(dev,"can't get regulator fpa_i2c\n");

    of_node_put(np);
#endif

	return TRUE;
}

void CleanupGpioMX6S(PFVD_DEV_INFO pDev)
{

}

BOOL GetPinDoneMX6S(PFVD_DEV_INFO pDev)
{
	return (gpio_get_value(pDev->conf_done_gpio) != 0);
}

//no status pin on ec101
BOOL GetPinStatusMX6S(PFVD_DEV_INFO pDev)
{
	return 1;
}

BOOL GetPinReadyMX6S(PFVD_DEV_INFO pDev)
{
	return (gpio_get_value(pDev->ready_gpio) != 0);
}

DWORD PutInProgrammingModeMX6S(PFVD_DEV_INFO pDev)
{
	return 1;
}

void BSPFvdPowerUpMX6S(PFVD_DEV_INFO pDev, BOOL restart)
{

}

void BSPFvdPowerDownMX6S(PFVD_DEV_INFO pDev)
{
	// Disable FPGA

}  

void BSPFvdPowerDownFPAMX6S(PFVD_DEV_INFO pDev)
{
	int ret;
	if( IS_ERR(pDev->reg_fpa_i2c)   || IS_ERR(pDev->reg_4v0_fpa))
		return;

    if(!fpaIsEnabled)
        return;
    fpaIsEnabled = false;

    ret = regulator_disable(pDev->reg_fpa_i2c);
    ret |= regulator_disable(pDev->reg_4v0_fpa);

    if(ret)
        dev_err(&pDev->pLinuxDevice->dev,"can't disable fpa \n");
}

void BSPFvdPowerUpFPAMX6S(PFVD_DEV_INFO pDev)
{
	int ret;
	if( IS_ERR(pDev->reg_fpa_i2c)   || IS_ERR(pDev->reg_4v0_fpa))
		return;

    if(fpaIsEnabled)
        return;
    fpaIsEnabled = true;

    ret = regulator_enable(pDev->reg_4v0_fpa);
    ret |= regulator_enable(pDev->reg_fpa_i2c);

    if(ret)
        dev_err(&pDev->pLinuxDevice->dev,"can't enable fpa \n");
}

