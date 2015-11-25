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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
#include "../arch/arm/mach-imx/mx6.h"
#else	/*  */
#include "mach/mx6.h"
#endif	/*  */
     
// Definitions
#define FPGA_CE             ((5-1)*32 + 28)	// GPIO 5.28
#define FPGA_CONF_DONE		((7-1)*32 + 13)
#define FPGA_CONFIG         ((5-1)*32 + 25)
#define FPGA_STATUS         ((4-1)*32 +  5)
#define FPGA_READY          ((3-1)*32 + 19)
#define FPGA_POWER_EN		((6-1)*32 + 25)
#define FPGA_POWER_EN_ROCO_A	((6-1)*32 + 19)
#define FPGA_IRQ_0          ((3-1)*32 + 16)
#define GPIO_SPI1_SCLK		((5-1)*32 + 22)
#define GPIO_SPI1_MOSI		((5-1)*32 + 23)
#define GPIO_SPI1_MISO		((5-1)*32 + 24)
#define GPIO_SPI1_CS		((4-1)*32 + 10)

// FPGA register 41 bits
#define FPA_CLCK_ENABLE     0x01
#define IRDM_PDWN_n         0x02
#define IRDM_RESET_n        0x04
#define IRDM_MClk           0x08
#define SpimSendReqFSEn     0x10
#define USE_SPI_CRC         0x1000
    
// Local prototypes
static BOOL SetupGpioAccessMX6Q(PFVD_DEV_INFO pDev);
static void CleanupGpioMX6Q(PFVD_DEV_INFO pDev);
static BOOL GetPinDoneMX6Q(void);
static BOOL GetPinStatusMX6Q(void);
static BOOL GetPinReadyMX6Q(void);
static DWORD PutInProgrammingModeMX6Q(PFVD_DEV_INFO);
static void BSPFvdPowerDownMX6Q(PFVD_DEV_INFO pDev);
static void BSPFvdPowerDownFPAMX6Q(PFVD_DEV_INFO pDev);
static void BSPFvdPowerUpMX6Q(PFVD_DEV_INFO pDev, BOOL restart);
static void BSPFvdPowerUpFPAMX6Q(PFVD_DEV_INFO pDev);

// Local variables
static u32 fpgaPower = FPGA_POWER_EN;
static bool fpaIsEnabled = false;

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
	pDev->iSpiBus = 32766;	// SPI no = 0
	pDev->iSpiCountDivisor = 1;	// Count is no of bytes
	pDev->iI2c = 3;		// Main i2c bus
}

BOOL SetupGpioAccessMX6Q(PFVD_DEV_INFO pDev) 
{
	int article, revision;
    struct device *dev = &pDev->pLinuxDevice->dev;

	GetMainboardVersion(pDev, &article, &revision);
	if (article == ROCO_ARTNO && revision == 1)
		fpgaPower = FPGA_POWER_EN_ROCO_A;
	if (gpio_is_valid(FPGA_CE) == 0) {
		pr_err("FpgaCE can not be used\n");
	} else {
		gpio_request(FPGA_CE, "FpgaCE");
	}
	if (gpio_is_valid(FPGA_CONF_DONE) == 0) {
		pr_err("FpgaConfDone can not be used\n");
	} else {
		gpio_request(FPGA_CONF_DONE, "FpgaConfDone");
		gpio_direction_input(FPGA_CONF_DONE);
	}
	if (gpio_is_valid(FPGA_CONFIG) == 0) {
		pr_err("FpgaConfig can not be used\n");
	} else {
		gpio_request(FPGA_CONFIG, "FpgaConfig");
	}
	if (gpio_is_valid(FPGA_STATUS) == 0) {
		pr_err("FpgaStatus can not be used\n");
	} else {
		gpio_request(FPGA_STATUS, "FpgaStatus");
		gpio_direction_input(FPGA_STATUS);
	}
	if (gpio_is_valid(FPGA_READY) == 0) {
		pr_err("FpgaReady can not be used\n");
	} else {
		gpio_request(FPGA_READY, "FpgaReady");
		gpio_direction_input(FPGA_READY);
	}
	if (gpio_is_valid(fpgaPower) == 0) {
		pr_err("FpgaPowerEn can not be used\n");
	} else {
		gpio_request(fpgaPower, "FpgaPowerEn");
	}

	//Pins already configured in bootloader
	gpio_direction_output(FPGA_CE, 0);
	gpio_direction_output(FPGA_CONFIG, 1);
	gpio_direction_output(fpgaPower, 1);	//Enable fpga power as default


    pDev->reg_3v15_fpa = devm_regulator_get(dev, "3V15_fpa");
    if(IS_ERR(pDev->reg_3v15_fpa))
        dev_err(dev,"can't get regulator 3V15_fpa\n");

    pDev->reg_4v0_fpa = devm_regulator_get(dev, "4V0_fpa");
    if(IS_ERR(pDev->reg_4v0_fpa))
        dev_err(dev,"can't get regulator 4V0_fpa");

    pDev->reg_detector = devm_regulator_get(dev, "uat1k_detector");
    if(IS_ERR(pDev->reg_detector))
        dev_err(dev,"can't get regulator uat1k_detector");

    pDev->reg_mems = devm_regulator_get(dev, "uat1k_mems");
    if(IS_ERR(pDev->reg_mems))
        dev_err(dev,"can't get regulator uat1k_mems");

	return TRUE;
}

void CleanupGpioMX6Q(PFVD_DEV_INFO pDev) 
{
	gpio_free(FPGA_CE);
	gpio_free(FPGA_CONF_DONE);
	gpio_free(FPGA_CONFIG);
	gpio_free(FPGA_STATUS);
	gpio_free(FPGA_READY);
	gpio_free(fpgaPower);
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
	if (!GetPinStatusMX6Q())
	{
		pr_err("FPGA: Status not high when config released\n");
		return 0;
	}
	msleep(1);
	return 1;
}

void BSPFvdPowerUpMX6Q(PFVD_DEV_INFO pDev, BOOL restart)
{
	gpio_set_value(fpgaPower, 1);
	msleep(50);
	gpio_set_value(FPGA_CE, 0);
	gpio_set_value(FPGA_CONFIG, 1);

	if (restart) {
		int timeout = 100;

		if (gpio_request(GPIO_SPI1_SCLK, "SPI1_MOSI"))
			pr_err("SPI1_SCLK can not be requested\n");
		else
			gpio_direction_input(GPIO_SPI1_SCLK);
		if (gpio_request(GPIO_SPI1_MOSI, "SPI1_MOSI"))
			pr_err("SPI1_MOSI can not be requested\n");
		else
			gpio_direction_input(GPIO_SPI1_MOSI);
		if (gpio_request(GPIO_SPI1_MISO, "SPI1_MISO"))
			pr_err("SPI1_MISO can not be requested\n");
		else
			gpio_direction_input(GPIO_SPI1_MISO);
		if (gpio_request(GPIO_SPI1_CS, "SPI1_CS"))
			pr_err("SPI1_CS can not be requested\n");
		else
			gpio_direction_input(GPIO_SPI1_CS);

		msleep(1);
		gpio_set_value(FPGA_CONFIG, 0);
		msleep(1);
		gpio_set_value(FPGA_CONFIG, 1);

		while (timeout--) {
			msleep (10);
			if (GetPinDoneMX6Q())
				break;
		}
		pr_info("FVDK timeout MX6Q %d\n", timeout);

		gpio_direction_output(GPIO_SPI1_CS,1);
		gpio_free(GPIO_SPI1_SCLK);
		gpio_free(GPIO_SPI1_MOSI);
		gpio_free(GPIO_SPI1_MISO);
		gpio_free(GPIO_SPI1_CS);
	}
}

void BSPFvdPowerDownMX6Q(PFVD_DEV_INFO pDev) 
{
	// Disable FPGA
	gpio_set_value(FPGA_CE, 1);
	msleep(1);
	
	gpio_set_value(fpgaPower, 0);
}  

// Separate FPA power down
void BSPFvdPowerDownFPAMX6Q(PFVD_DEV_INFO pDev) 
{
	int ret;
    if(!fpaIsEnabled)
        return;
    fpaIsEnabled = false;

    ret = regulator_disable(pDev->reg_mems);
    ret |= regulator_disable(pDev->reg_detector);
    ret |= regulator_disable(pDev->reg_3v15_fpa);
    ret |= regulator_disable(pDev->reg_4v0_fpa);

    if(ret)
        dev_err(&pDev->pLinuxDevice->dev,"can't disable fpa \n");
}

void BSPFvdPowerUpFPAMX6Q(PFVD_DEV_INFO pDev) 
{
    int ret;
    if(fpaIsEnabled)
        return;
    fpaIsEnabled = true;


    ret = regulator_enable(pDev->reg_3v15_fpa);
    ret |= regulator_enable(pDev->reg_4v0_fpa);
	msleep(5);
    ret |= regulator_enable(pDev->reg_detector);
    ret |= regulator_enable(pDev->reg_mems);

    if(ret)
        dev_err(&pDev->pLinuxDevice->dev,"can't enable fpa \n");
} 

