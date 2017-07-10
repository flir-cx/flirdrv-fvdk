/**
 * @file   roco_header.c
 * @author Svangård <bo.svangard@flir.com>
 * @date   Fri Feb 20 15:27:01 2015
 * 
 * @brief  Functions for reading header data from SPI flash on ROCO
 * 
 * 
 */

#include "flir_kernel_os.h"
#include "fpga.h"
#include "roco_header.h"
#include <linux/mtd/mtd.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0)
int mtd_read(struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen,
	     u_char *buf)
{
	*retlen=0;
	return -1;
};
#endif

/** 
 * Read header from spi device 
 * 
 * rxbuf - allocated buffer the size of HEADER_LENGTH
 * @return header data on success, else NULL
 */
int read_header(struct mtd_info *mtd, unsigned char *rxbuf)
{
	int address = (mtd->size - HEADER_LENGTH);
	int retlen;
	int ret = mtd_read(mtd, address, HEADER_LENGTH, &retlen, rxbuf);

	if(ret !=0 || retlen != HEADER_LENGTH)
	{
		pr_err("Failed reading spi flash %d %d\n",ret,retlen);
		return -ENODEV;
	}

	if(strncmp(rxbuf, "FLIR", 4))
	{
		pr_err("Missing FLIR header in spi flash %d\n",ret);
		return -EINVAL;
	}

	return ret;
}

/** 
 * Print generic header with pr_err
 * 
 * @param pGen 
 */
void prerr_generic_header(GENERIC_FPGA_T *pGen)
{
/* Print generic header information */
	pr_err("\n");
	pr_err("Data in generic header\n");
	pr_err("*****************\n");
	pr_err("Identity: %s\n", pGen->identity);
	pr_err("headerrev %i\n", pGen->headerrev);
	if (pGen->LSBfirst) pr_err("LSbit first (ALTERA)\n");
	else pr_err("MSbit first (XILINX)\n");
	pr_err("FPGA Name %s\n", pGen->name);
	pr_err("Creation date %s\n", pGen->date);
	pr_err("FPGA Major version %ld\n", pGen->major);
	pr_err("FPGA Minor version %ld\n", pGen->minor);
	pr_err("FPGA edit number %ld\n", pGen->edit);
	pr_err("FPGA reserved 0 %ld\n", pGen->reserved[0]);
	pr_err("FPGA reserved 1 %ld\n", pGen->reserved[1]);
	pr_err("FPGA reserved 2 %ld\n", pGen->reserved[2]);
	pr_err("FPGA reserved 3 %ld\n", pGen->reserved[3]);
	pr_err("Byte offset to load data 0x%lX\n", pGen->offset);
	pr_err("Size of FPGA data spec_size 0x%lX\n", pGen->spec_size);
	pr_err("*****************\n");
	pr_err("\n");
}

/** 
 * Print specific header with pr_err
 * 
 * @param pSpec 
 */
void prerr_specific_header(BXAB_FPGA_T *pSpec)
{
/* Print specific header information */
	pr_err("\n");
	pr_err("Data in specific header\n");
	pr_err("*****************\n");
	pr_err("identity %s\n", pSpec->identity);
	pr_err("Headerrev %i\n", pSpec->headerrev);
	pr_err("FPGAType %i\n", pSpec->FPGAtype);
	pr_err("hwType %i\n", pSpec->hwType);
	pr_err("hwMajor %i\n", pSpec->hwMajor);
	pr_err("frbWidth %i\n", pSpec->frbWidth);
	pr_err("frbHeight %i\n", pSpec->frbHeight);
	pr_err("frbPixelSize %i\n", pSpec->frbPixelSize);
	pr_err("spare %i\n", pSpec->spare);
	pr_err("cftVers %i\n", pSpec->cftVers);
	pr_err("cftSize %i\n", pSpec->cftSize);
	pr_err("palVers %i\n", pSpec->palVers);
	pr_err("palSize %i\n", pSpec->palSize);
	pr_err("expVers %i\n", pSpec->expVers);
	pr_err("expSize %i\n", pSpec->expSize);
	pr_err("hstSize %i\n", pSpec->hstSize);
	pr_err("noOfBuffers %i\n", pSpec->noOfBuffers);
	pr_err("Reserved 0 %ld\n", pSpec->reserved[0]);
	pr_err("Reserved 1 %ld\n", pSpec->reserved[1]);
	pr_err("Reserved 2 %ld\n", pSpec->reserved[2]);
	pr_err("Reserved 3 %ld\n", pSpec->reserved[3]);
	pr_err("*****************\n");
	pr_err("\n");
}

/** 
 * extract generic header and specific header to their structs
 * 
 * @param rxbuf buffer containing the header raw data
 * @param pGen generic header struct
 * @param pSpec  specific header struct
 * 
 * @return 0 on success
 */
int extract_headers(unsigned char *rxbuf, GENERIC_FPGA_T *pGen, BXAB_FPGA_T *pSpec)
{
	memcpy(pGen, rxbuf, sizeof(GENERIC_FPGA_T));

	if(pGen->spec_size){
		memcpy(pSpec, &rxbuf[sizeof(GENERIC_FPGA_T)], sizeof(BXAB_FPGA_T));

	} else {
		pSpec = NULL;
	}
	return 0;
}

/** 
* Read data from SPI Device, 
* 
* @param rxbuf 
* 
* @return 0 on success, <0 on error
*/
int read_spi_header(unsigned char *rxbuf)
{
	struct mtd_info *mtd = get_mtd_device(NULL, MTD_DEVICE);

	if(!mtd){
		pr_err("Failed to get mtd device \n");
		return -ENODEV;
	}

	return read_header(mtd,rxbuf);
}


