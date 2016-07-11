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
static void enable_fpga_power(PFVD_DEV_INFO pDev);
static void reload_fpga(PFVD_DEV_INFO pDev);

// Local variables
static bool fpaIsEnabled = false;
static bool fpgaIsEnabled = false;

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
	pDev->iI2c = 2;		// Main i2c bus
	pDev->spi_flash = true;
}


BOOL SetupGpioAccessMX6S(PFVD_DEV_INFO pDev)
{
	#ifdef CONFIG_OF
    struct device *dev = &pDev->pLinuxDevice->dev;
    struct device_node *np = pDev->np;
    int ret;
	int article, revision;
	GetMainboardVersion(pDev, &article, &revision);

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

/*SPI bus shared with fpga*/
	pDev->spi_sclk_gpio = of_get_named_gpio(np, "spi-sclk-gpio", 0);
	pDev->spi_mosi_gpio = of_get_named_gpio(np, "spi-mosi-gpio", 0);
	pDev->spi_miso_gpio = of_get_named_gpio(np, "spi-miso-gpio", 0);
	pDev->spi_cs_gpio = of_get_named_gpio(np, "spi-cs-gpio", 0);

/* FPA regulators */
	pDev->reg_4v0_fpa = devm_regulator_get(dev, "4V0_fpa");
    if(IS_ERR(pDev->reg_4v0_fpa))
        dev_err(dev,"can't get regulator 4V0_fpa");

	pDev->reg_fpa_i2c = devm_regulator_get(dev, "fpa_i2c");
    if(IS_ERR(pDev->reg_fpa_i2c))
        dev_err(dev,"can't get regulator fpa_i2c\n");

/* FPGA regulators */
	pDev->reg_1v0_fpga = devm_regulator_get(dev, "DA9063_BPRO");
    if(IS_ERR(pDev->reg_1v0_fpga))
        dev_err(dev,"can't get regulator DA9063_BPRO");

	pDev->reg_1v2_fpga = devm_regulator_get(dev, "DA9063_CORE_SW");
    if(IS_ERR(pDev->reg_1v2_fpga))
        dev_err(dev,"can't get regulator DA9063_CORE_SW");

	pDev->reg_1v8_fpga = devm_regulator_get(dev, "DA9063_PERI_SW");
    if(IS_ERR(pDev->reg_1v8_fpga))
        dev_err(dev,"can't get regulator DA9063_PERI_SW");

	if(article == EC101_ARTNO && revision == 3) //revC
	{
		pDev->reg_2v5_fpga = devm_regulator_get(dev, "DA9063_BMEM");
		if(IS_ERR(pDev->reg_2v5_fpga))
			dev_err(dev,"can't get regulator DA9063_BMEM");

		pDev->reg_3v15_fpga = devm_regulator_get(dev, "DA9063_LDO10");
		if(IS_ERR(pDev->reg_3v15_fpga))
			dev_err(dev,"can't get regulator DA9063_LDO10");
	}
	else
	{
		pDev->reg_2v5_fpga = devm_regulator_get(dev, "DA9063_LDO10");
		if(IS_ERR(pDev->reg_2v5_fpga))
			dev_err(dev,"can't get regulator DA9063_LDO10");

		pDev->reg_3v15_fpga = devm_regulator_get(dev, "DA9063_LDO8");
		if(IS_ERR(pDev->reg_3v15_fpga))
			dev_err(dev,"can't get regulator DA9063_LDO8");
	}

	if (!GetPinDoneMX6S(pDev))
	{
		dev_err(dev,"U-boot FPGA load failed");
		gpio_set_value(pDev->program_gpio, 0);
		gpio_set_value(pDev->init_gpio, 0);
	}

    of_node_put(np);
#endif

	return TRUE;
}

void CleanupGpioMX6S(PFVD_DEV_INFO pDev)
{
	//devm does the cleanup
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
	enable_fpga_power(pDev);

	if(restart)
		reload_fpga(pDev);

}

static void reload_fpga(PFVD_DEV_INFO pDev)
{

	int timeout = 100;
	struct device *dev = &pDev->pLinuxDevice->dev;

	if (gpio_request(pDev->spi_sclk_gpio, "SPI1_SCLK"))
		dev_err(dev,"SPI1_SCLK can not be requested\n");
	else
		gpio_direction_input(pDev->spi_sclk_gpio);
	if (gpio_request(pDev->spi_mosi_gpio, "SPI1_MOSI"))
		dev_err(dev,"SPI1_MOSI can not be requested\n");
	else
		gpio_direction_input(pDev->spi_mosi_gpio);
	if (gpio_request(pDev->spi_miso_gpio, "SPI1_MISO"))
		dev_err(dev,"SPI1_MISO can not be requested\n");
	else
		gpio_direction_input(pDev->spi_miso_gpio);

	msleep(1);
	gpio_set_value(pDev->program_gpio, 0);
	gpio_set_value(pDev->init_gpio, 0);
	msleep(1);
	gpio_set_value(pDev->program_gpio, 1);
	gpio_set_value(pDev->init_gpio, 1);

	while (timeout--) {
		msleep (5);
		if (GetPinDoneMX6S(pDev))
			break;
	}
	msleep (5);
	dev_info(dev,"FPGA loaded in %d ms\n", (100 - timeout) * 5);

	gpio_direction_output(pDev->spi_cs_gpio,1);
	gpio_free(pDev->spi_sclk_gpio);
	gpio_free(pDev->spi_mosi_gpio);
	gpio_free(pDev->spi_miso_gpio);
	gpio_free(pDev->spi_cs_gpio);
}



static void enable_fpga_power(PFVD_DEV_INFO pDev)
{
	int ret;
	if( IS_ERR(pDev->reg_1v0_fpga)   || IS_ERR(pDev->reg_1v2_fpga) ||
		IS_ERR(pDev->reg_1v8_fpga)   || IS_ERR(pDev->reg_2v5_fpga) || IS_ERR(pDev->reg_3v15_fpga)  )
		return;

	if(fpgaIsEnabled)
		return;
	fpgaIsEnabled = true;

	ret = regulator_enable(pDev->reg_1v0_fpga);
	ret |= regulator_enable(pDev->reg_1v8_fpga);
	ret |= regulator_enable(pDev->reg_1v2_fpga);
	ret |= regulator_enable(pDev->reg_2v5_fpga);
	ret |= regulator_enable(pDev->reg_3v15_fpga);

	if(ret)
		dev_err(&pDev->pLinuxDevice->dev,"can't enable fpga \n");
}

void BSPFvdPowerDownMX6S(PFVD_DEV_INFO pDev)
{
	int ret;
	// Disable FPGA
	if( IS_ERR(pDev->reg_1v0_fpga)   || IS_ERR(pDev->reg_1v2_fpga) ||
		IS_ERR(pDev->reg_1v8_fpga)   || IS_ERR(pDev->reg_2v5_fpga) || IS_ERR(pDev->reg_3v15_fpga)  )
		return;

	if(!fpgaIsEnabled)
		return;
	fpgaIsEnabled = false;

	ret = regulator_disable(pDev->reg_3v15_fpga);
	ret |= regulator_disable(pDev->reg_2v5_fpga);
	ret |= regulator_disable(pDev->reg_1v2_fpga);
	ret |= regulator_disable(pDev->reg_1v8_fpga);
	ret |= regulator_disable(pDev->reg_1v0_fpga);

	gpio_set_value(pDev->program_gpio, 0);
	gpio_set_value(pDev->init_gpio, 0);

	if (gpio_request(pDev->spi_cs_gpio, "SPI1_CS"))
		dev_err(&pDev->pLinuxDevice->dev,"SPI1_CS can not be requested\n");
	else
		gpio_direction_input(pDev->spi_cs_gpio);

	if(ret)
		dev_err(&pDev->pLinuxDevice->dev,"can't disable fpga \n");
}  

void BSPFvdPowerDownFPAMX6S(PFVD_DEV_INFO pDev)
{
	int ret;

	if( IS_ERR(pDev->reg_fpa_i2c) || IS_ERR(pDev->reg_4v0_fpa))
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

