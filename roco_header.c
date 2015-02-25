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
#include <linux/spi/spi.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
int of_dev_node_match(struct device *dev, void *data)
{
        return dev->of_node == data;
}

/** 
 * Fetch spi device by name defined in device tree
 * 
 * @param ofname name of device in devicetreee
 * 
 * @return struct spi_device *, NULL on failure
 */
struct spi_device * get_spi_device(const char *ofname)
{
	struct spi_device *spidev = NULL;
	struct device_node *node=NULL;
	struct device * device;

	node=of_find_node_by_name(0, ofname);
	if(! node){
		pr_err("Could not find node %s\n", ofname);
		return NULL;
	}
	device = bus_find_device(&spi_bus_type, NULL, node, of_dev_node_match);
	if(! device){
		pr_err("Could not extract device from node\n");
		return NULL;
	}

	spidev = to_spi_device(device);
	if(!spidev){
		return NULL;
	}

	return spidev;
}


/** 
 * Read from spi device att position for header data from FPGA bin file, 
 * Check if 5 first bytes are FLIR\0
 * 
 * @return true if first 5 bytes are FLIR\0
 */
int check_if_header_exist(struct spi_device *spidev)
{
	//Now i got an spi_device of the node... What i would like is an sturct m25p *flash...
	//Or a struct mtd_in *mtd...
	unsigned char txbuf[5];
	unsigned char rxbuf[5];
	int ntx, nrx;
	int ret = false;
	int address = HEADER_START_ADDRESS;

	txbuf[0]=OPCODE_NORM_READ_4B;
	txbuf[1]=address >> 24 & 0xff;
	txbuf[2]=address >> 16 & 0xff;
	txbuf[3]=address >> 8  & 0xff;
	txbuf[4]=address >> 0  & 0xff;
	ntx = 5;
	nrx = 5;
	ret = spi_write_then_read(spidev, txbuf, ntx, rxbuf, nrx);

	if(strncmp(rxbuf, "FLIR", 5) == 0)
	{
		ret = true;
	} 

	return ret;
}

/** 
 * Read header from spi device 
 * 
 * rxbuf - allocated buffer the size of HEADER_LENGTH
 * @return header data on success, else NULL
 */
unsigned char * read_header(struct spi_device *spidev, unsigned char *rxbuf)
{
	//Now i got an spi_device of the node... What i would like is an sturct m25p *flash...
	//Or a struct mtd_in *mtd...
	unsigned char txbuf[5];
	int ntx, nrx;
	unsigned char *ret = NULL;
	int address = HEADER_START_ADDRESS;

	txbuf[0]=OPCODE_NORM_READ_4B;
	txbuf[1]=address >> 24 & 0xff;
	txbuf[2]=address >> 16 & 0xff;
	txbuf[3]=address >> 8  & 0xff;
	txbuf[4]=address >> 0  & 0xff;
	ntx = 5;
	nrx = HEADER_LENGTH;
	if(spi_write_then_read(spidev, txbuf, ntx, rxbuf, nrx) == 0){
		ret = rxbuf;
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

		/* pr_err("MD5SUM in header\n"); */
		/* pr_err("%s\n\n", &rxbuf[HEADER_LENGTH-37]); */

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
	int ret;
	struct spi_device *spidev;

	spidev = get_spi_device("m25p80");
	if(!spidev){
		ret = -1;
		goto END;
	}

	if(check_if_header_exist(spidev)){
		read_header(spidev, rxbuf);
		/* pr_err("Size of generic + specific header is %i\n", sizeof(GENERIC_FPGA_T) + sizeof(BXAB_FPGA_T)); */
	} else { 
		ret=-1;
		goto END;
	}
		
	ret = 0;
END:
	return ret;
}


#endif
