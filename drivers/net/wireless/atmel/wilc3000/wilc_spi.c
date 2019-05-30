/*
 * Atmel WILC 802.11 b/g/n driver
 *
 * Copyright (c) 2015 Atmel Corportation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "wilc_wlan_if.h"
#include "wilc_wlan.h"

struct wilc_spi {
	int (*spi_tx)(uint8_t *, uint32_t);
	int (*spi_rx)(uint8_t *, uint32_t);
	int (*spi_trx)(uint8_t *, uint8_t *, uint32_t);
	int crc_off;
	int nint;
	int has_thrpt_enh;
};

static struct wilc_spi g_spi;

static int spi_read(uint32_t, uint8_t *, uint32_t);
static int spi_write(uint32_t, uint8_t *, uint32_t);
static int spi_reset(void);

/*
 * Crc7
 */
static const uint8_t crc7_syndrome_table[256] = {
	0x00, 0x09, 0x12, 0x1b, 0x24, 0x2d, 0x36, 0x3f,
	0x48, 0x41, 0x5a, 0x53, 0x6c, 0x65, 0x7e, 0x77,
	0x19, 0x10, 0x0b, 0x02, 0x3d, 0x34, 0x2f, 0x26,
	0x51, 0x58, 0x43, 0x4a, 0x75, 0x7c, 0x67, 0x6e,
	0x32, 0x3b, 0x20, 0x29, 0x16, 0x1f, 0x04, 0x0d,
	0x7a, 0x73, 0x68, 0x61, 0x5e, 0x57, 0x4c, 0x45,
	0x2b, 0x22, 0x39, 0x30, 0x0f, 0x06, 0x1d, 0x14,
	0x63, 0x6a, 0x71, 0x78, 0x47, 0x4e, 0x55, 0x5c,
	0x64, 0x6d, 0x76, 0x7f, 0x40, 0x49, 0x52, 0x5b,
	0x2c, 0x25, 0x3e, 0x37, 0x08, 0x01, 0x1a, 0x13,
	0x7d, 0x74, 0x6f, 0x66, 0x59, 0x50, 0x4b, 0x42,
	0x35, 0x3c, 0x27, 0x2e, 0x11, 0x18, 0x03, 0x0a,
	0x56, 0x5f, 0x44, 0x4d, 0x72, 0x7b, 0x60, 0x69,
	0x1e, 0x17, 0x0c, 0x05, 0x3a, 0x33, 0x28, 0x21,
	0x4f, 0x46, 0x5d, 0x54, 0x6b, 0x62, 0x79, 0x70,
	0x07, 0x0e, 0x15, 0x1c, 0x23, 0x2a, 0x31, 0x38,
	0x41, 0x48, 0x53, 0x5a, 0x65, 0x6c, 0x77, 0x7e,
	0x09, 0x00, 0x1b, 0x12, 0x2d, 0x24, 0x3f, 0x36,
	0x58, 0x51, 0x4a, 0x43, 0x7c, 0x75, 0x6e, 0x67,
	0x10, 0x19, 0x02, 0x0b, 0x34, 0x3d, 0x26, 0x2f,
	0x73, 0x7a, 0x61, 0x68, 0x57, 0x5e, 0x45, 0x4c,
	0x3b, 0x32, 0x29, 0x20, 0x1f, 0x16, 0x0d, 0x04,
	0x6a, 0x63, 0x78, 0x71, 0x4e, 0x47, 0x5c, 0x55,
	0x22, 0x2b, 0x30, 0x39, 0x06, 0x0f, 0x14, 0x1d,
	0x25, 0x2c, 0x37, 0x3e, 0x01, 0x08, 0x13, 0x1a,
	0x6d, 0x64, 0x7f, 0x76, 0x49, 0x40, 0x5b, 0x52,
	0x3c, 0x35, 0x2e, 0x27, 0x18, 0x11, 0x0a, 0x03,
	0x74, 0x7d, 0x66, 0x6f, 0x50, 0x59, 0x42, 0x4b,
	0x17, 0x1e, 0x05, 0x0c, 0x33, 0x3a, 0x21, 0x28,
	0x5f, 0x56, 0x4d, 0x44, 0x7b, 0x72, 0x69, 0x60,
	0x0e, 0x07, 0x1c, 0x15, 0x2a, 0x23, 0x38, 0x31,
	0x46, 0x4f, 0x54, 0x5d, 0x62, 0x6b, 0x70, 0x79
};

static uint8_t crc7_byte(uint8_t crc, uint8_t data)
{
	return crc7_syndrome_table[(crc << 1) ^ data];
}

static uint8_t crc7(uint8_t crc, const uint8_t *buffer, uint32_t len)
{
	while (len--)
		crc = crc7_byte(crc, *buffer++);
	return crc;
}

#define CMD_DMA_WRITE		0xc1
#define CMD_DMA_READ		0xc2
#define CMD_INTERNAL_WRITE	0xc3
#define CMD_INTERNAL_READ	0xc4
#define CMD_TERMINATE		0xc5
#define CMD_REPEAT			0xc6
#define CMD_DMA_EXT_WRITE	0xc7
#define CMD_DMA_EXT_READ	0xc8
#define CMD_SINGLE_WRITE	0xc9
#define CMD_SINGLE_READ		0xca
#define CMD_RESET			0xcf

#define N_OK				1
#define N_FAIL				0
#define N_RESET				(-1)
#define N_RETRY				(-2)

#define SPI_RESP_RETRY_COUNT (10)
#define SPI_RETRY_COUNT		(10)
#define DATA_PKT_SZ_256		256
#define DATA_PKT_SZ_512		512
#define DATA_PKT_SZ_1K		1024
#define DATA_PKT_SZ_2K		(2 * 1024)
#define DATA_PKT_SZ_4K		(4 * 1024)
#define DATA_PKT_SZ_8K		(8 * 1024)
#define DATA_PKT_SZ			DATA_PKT_SZ_8K

static int spi_cmd(uint8_t cmd, uint32_t addr,
		   uint32_t data, uint32_t sz, uint8_t clockless)
{
	uint8_t bc[9];
	int len = 5;
	int result = N_OK;

	bc[0] = cmd;
	switch (cmd) {
	case CMD_SINGLE_READ:	/* single word (4 bytes) read */
		bc[1] = (uint8_t)(addr >> 16);
		bc[2] = (uint8_t)(addr >> 8);
		bc[3] = (uint8_t)addr;
		len = 5;
		break;
	case CMD_INTERNAL_READ:	/* internal register read */
		bc[1] = (uint8_t)(addr >> 8);
		if (clockless)
			bc[1] |= (1 << 7);
		bc[2] = (uint8_t)addr;
		bc[3] = 0x00;
		len = 5;
		break;
	case CMD_TERMINATE:	/* termination */
		bc[1] = 0x00;
		bc[2] = 0x00;
		bc[3] = 0x00;
		len = 5;
		break;
	case CMD_REPEAT:	/* repeat */
		bc[1] = 0x00;
		bc[2] = 0x00;
		bc[3] = 0x00;
		len = 5;
		break;
	case CMD_RESET:		/* reset */
		bc[1] = 0xff;
		bc[2] = 0xff;
		bc[3] = 0xff;
		len = 5;
		break;
	case CMD_DMA_WRITE:	/* dma write */
	case CMD_DMA_READ:	/* dma read */
		bc[1] = (uint8_t)(addr >> 16);
		bc[2] = (uint8_t)(addr >> 8);
		bc[3] = (uint8_t)addr;
		bc[4] = (uint8_t)(sz >> 8);
		bc[5] = (uint8_t)(sz);
		len = 7;
		break;
	case CMD_DMA_EXT_WRITE:	/* dma extended write */
	case CMD_DMA_EXT_READ:	/* dma extended read */
		bc[1] = (uint8_t)(addr >> 16);
		bc[2] = (uint8_t)(addr >> 8);
		bc[3] = (uint8_t)addr;
		bc[4] = (uint8_t)(sz >> 16);
		bc[5] = (uint8_t)(sz >> 8);
		bc[6] = (uint8_t)(sz);
		len = 8;
		break;
	case CMD_INTERNAL_WRITE:/* internal register write */
		bc[1] = (uint8_t)(addr >> 8);
		if (clockless)
			bc[1] |= (1 << 7);
		bc[2] = (uint8_t)(addr);
		bc[3] = (uint8_t)(data >> 24);
		bc[4] = (uint8_t)(data >> 16);
		bc[5] = (uint8_t)(data >> 8);
		bc[6] = (uint8_t)(data);
		len = 8;
		break;
	case CMD_SINGLE_WRITE:	/* single word write */
		bc[1] = (uint8_t)(addr >> 16);
		bc[2] = (uint8_t)(addr >> 8);
		bc[3] = (uint8_t)(addr);
		bc[4] = (uint8_t)(data >> 24);
		bc[5] = (uint8_t)(data >> 16);
		bc[6] = (uint8_t)(data >> 8);
		bc[7] = (uint8_t)(data);
		len = 9;
		break;
	default:
		result = N_FAIL;
		break;
	}

	if (result) {
		if (!g_spi.crc_off)
			bc[len-1] = (crc7(0x7f, (const uint8_t *)&bc[0], len-1)) << 1;
		else
			len -= 1;

		if (!g_spi.spi_tx(bc, len)) {
			PRINT_ER("Failed cmd write, bus error...\n");
			result = N_FAIL;
		}
	}

	return result;
}

static int spi_data_rsp(uint8_t cmd)
{
	uint8_t len;
	uint8_t rsp[3];
	int result = N_OK;

	if (!g_spi.crc_off)
		len = 2;
	else
		len = 3;

	if (!g_spi.spi_rx(&rsp[0], len)) {
		PRINT_ER("Failed bus error...\n");
		result = N_FAIL;
		goto _fail_;
	}
		
	if((rsp[len-1] != 0)||(rsp[len-2] != 0xC3)){
		PRINT_ER("Failed data response read, %x %x %x\n",rsp[0],rsp[1],rsp[2]);
		result = N_FAIL;
		goto _fail_;
	}
	
_fail_:
	return result;
}

static int spi_cmd_rsp(uint8_t cmd)
{
	uint8_t rsp;
	int result = N_OK;
	int retry;

	/*
	 * Command/Control response
	 */
	if ((cmd == CMD_RESET) ||
	    (cmd == CMD_TERMINATE) ||
	    (cmd == CMD_REPEAT)) {
		if (!g_spi.spi_rx(&rsp, 1)) {
			result = N_FAIL;
			goto _fail_;
		}
	}

	retry = SPI_RESP_RETRY_COUNT;
	do {
		if (!g_spi.spi_rx(&rsp, 1)) {
			PRINT_ER("Failed cmd response read, bus error...\n");
			result = N_FAIL;
			goto _fail_;
		}
	} while((rsp != cmd) && (retry-- >0));

	if (rsp != cmd) {
		PRINT_ER("Failed cmd response, cmd (%02x), resp (%02x)\n", cmd, rsp);
		
		result = N_FAIL;
		goto _fail_;
	}

	/**
		State response
	**/
	retry = SPI_RESP_RETRY_COUNT;
	do {
		if (!g_spi.spi_rx(&rsp, 1)) {
			PRINT_ER("Failed cmd state read, bus error...\n");
			result = N_FAIL;
			goto _fail_;
		}
	} while((rsp != 0x00) && (retry-- >0));

	if (rsp != 0x00) {
		PRINT_ER("Failed cmd state response state (%02x)\n", rsp);
		result = N_FAIL;
		goto _fail_;
	}

_fail_:
	return result;
}

static int spi_cmd_complete(uint8_t cmd, uint32_t addr,
			    uint8_t *b, uint32_t sz, uint8_t clockless)
{
	uint8_t wb[32], rb[32];
	uint8_t wix, rix;
	uint32_t len2;
	uint8_t rsp;
	int len = 0;
	int result = N_OK;

	wb[0] = cmd;
	switch (cmd) {
	case CMD_SINGLE_READ:		/* single word (4 bytes) read */
		wb[1] = (uint8_t)(addr >> 16);
		wb[2] = (uint8_t)(addr >> 8);
		wb[3] = (uint8_t)addr;
		len = 5;
		break;
	case CMD_INTERNAL_READ:		/* internal register read */
		wb[1] = (uint8_t)(addr >> 8);
		if (clockless == 1)
			wb[1] |= (1 << 7);
		wb[2] = (uint8_t)addr;
		wb[3] = 0x00;
		len = 5;
		break;
	case CMD_TERMINATE:		/* termination */
		wb[1] = 0x00;
		wb[2] = 0x00;
		wb[3] = 0x00;
		len = 5;
		break;
	case CMD_REPEAT:		/* repeat */
		wb[1] = 0x00;
		wb[2] = 0x00;
		wb[3] = 0x00;
		len = 5;
		break;
	case CMD_RESET:			/* reset */
		wb[1] = 0xff;
		wb[2] = 0xff;
		wb[3] = 0xff;
		len = 5;
		break;
	case CMD_DMA_WRITE:		/* dma write */
	case CMD_DMA_READ:		/* dma read */
		wb[1] = (uint8_t)(addr >> 16);
		wb[2] = (uint8_t)(addr >> 8);
		wb[3] = (uint8_t)addr;
		wb[4] = (uint8_t)(sz >> 8);
		wb[5] = (uint8_t)(sz);
		len = 7;
		break;
	case CMD_DMA_EXT_WRITE:		/* dma extended write */
	case CMD_DMA_EXT_READ:		/* dma extended read */
		wb[1] = (uint8_t)(addr >> 16);
		wb[2] = (uint8_t)(addr >> 8);
		wb[3] = (uint8_t)addr;
		wb[4] = (uint8_t)(sz >> 16);
		wb[5] = (uint8_t)(sz >> 8);
		wb[6] = (uint8_t)(sz);
		len = 8;
		break;
	case CMD_INTERNAL_WRITE:	/* internal register write */
		wb[1] = (uint8_t)(addr >> 8);
		if (clockless == 1)
			wb[1] |= (1 << 7);
		wb[2] = (uint8_t)(addr);
		wb[3] = b[3];
		wb[4] = b[2];
		wb[5] = b[1];
		wb[6] = b[0];
		len = 8;
		break;
	case CMD_SINGLE_WRITE:		/* single word write */
		wb[1] = (uint8_t)(addr >> 16);
		wb[2] = (uint8_t)(addr >> 8);
		wb[3] = (uint8_t)(addr);
		wb[4] = b[3];
		wb[5] = b[2];
		wb[6] = b[1];
		wb[7] = b[0];
		len = 9;
		break;
	default:
		result = N_FAIL;
		break;
	}

	if (result != N_OK) {
		return result;
	}

	if (!g_spi.crc_off) {
		wb[len-1] = (crc7(0x7f, (const uint8_t *)&wb[0], len-1)) << 1;
	} else {
		len -=1;
	}

#define NUM_SKIP_BYTES		(1)
#define NUM_RSP_BYTES		(2)
#define NUM_DATA_HDR_BYTES	(1)
#define NUM_DATA_BYTES		(4)
#define NUM_CRC_BYTES		(2)
#define NUM_DUMMY_BYTES		(3)
	if ((cmd == CMD_RESET) ||
	    (cmd == CMD_TERMINATE) ||
	    (cmd == CMD_REPEAT)) {
		len2 = len + (NUM_SKIP_BYTES + NUM_RSP_BYTES + NUM_DUMMY_BYTES);
	} else if ((cmd == CMD_INTERNAL_READ) || (cmd == CMD_SINGLE_READ)) {
		if (!g_spi.crc_off) {
			len2 = len + (NUM_RSP_BYTES + NUM_DATA_HDR_BYTES
				      + NUM_DATA_BYTES + NUM_CRC_BYTES
				      + NUM_DUMMY_BYTES);
		} else {
			len2 = len + (NUM_RSP_BYTES + NUM_DATA_HDR_BYTES
				      + NUM_DATA_BYTES + NUM_DUMMY_BYTES);
		}
	} else {
		len2 = len + (NUM_RSP_BYTES + NUM_DUMMY_BYTES);
	}
#undef NUM_DUMMY_BYTES

	if (len2 > ARRAY_SIZE(wb)) {
		PRINT_ER("spi buf size too small %d,%d\n",
			len2, ARRAY_SIZE(wb));
		result = N_FAIL;
		return result;
	}

	/*
	 * zero spi write buffers
	 */
	for(wix = len; wix< len2; wix++) {
		wb[wix] = 0;
	}
	rix = len;

	if (!g_spi.spi_trx(wb, rb, len2)) {
		PRINT_ER("Failed cmd write, bus error...\n");
		result = N_FAIL;
		return result;
	}

	/*
	 * Command/Control response
	 */
	if ((cmd == CMD_RESET) ||
		(cmd == CMD_TERMINATE) ||
		(cmd == CMD_REPEAT))
			rix++; /* skip 1 byte */

	rsp = rb[rix++];

	if (rsp != cmd) {
		/*PRINT_ER("Failed cmd response, cmd (%02x)"
			", resp (%02x)\n", cmd, rsp);*/
		result = N_FAIL;
		return result;
	}

	/*
	 * State response
	 */
	rsp = rb[rix++];
	if (rsp != 0x00) {
		PRINT_ER("Failed cmd state response "
			"state (%02x)\n", rsp);
		result = N_FAIL;
		return result;
	}

	if ((cmd == CMD_INTERNAL_READ) || (cmd == CMD_SINGLE_READ) ||
	    (cmd == CMD_DMA_READ) || (cmd == CMD_DMA_EXT_READ)) {
		int retry;
		uint8_t crc[2];

		/*
		 * Data Respnose header
		 */
		retry = SPI_RESP_RETRY_COUNT;
		do {
			/*
			 * ensure there is room in buffer later
			 * to read data and crc
			 */
			if (rix < len2) {
				rsp = rb[rix++];
			} else {
				retry = 0;
				break;
			}
			if (((rsp >> 4) & 0xf) == 0xf)
				break;
		} while (retry--);

		if (retry <= 0) {
			PRINT_ER("Error, data read resp %02x\n", rsp);
			result = N_RESET;
			return result;
		}

		if ((cmd == CMD_INTERNAL_READ) || (cmd == CMD_SINGLE_READ)) {
			/*
			 * Read bytes
			 */
			if ((rix + 3) < len2) {
				b[0] = rb[rix++];
				b[1] = rb[rix++];
				b[2] = rb[rix++];
				b[3] = rb[rix++];
			} else {
				PRINT_ER("buffer overrun when reading data.\n");
				result = N_FAIL;
				return result;
			}

			if (!g_spi.crc_off) {
				/*
				 * Read Crc
				 */
				if ((rix + 1) < len2) {
					crc[0] = rb[rix++];
					crc[1] = rb[rix++];
				} else {
					PRINT_ER("buffer  overrun when reading crc.\n");
					result = N_FAIL;
					return result;
				}
			}
		} else if ((cmd == CMD_DMA_READ) || (cmd == CMD_DMA_EXT_READ)) {
			int ix;

			/*
			 * some data may be read
			 * in response to dummy bytes
			 */
			for(ix=0; (rix < len2) && (ix < sz);) {
				b[ix++] = rb[rix++];				
			}
			sz -= ix;

			if (sz > 0) {
				int nbytes;

				if (sz <= (DATA_PKT_SZ-ix)) {
					nbytes = sz;
				} else {
					nbytes = DATA_PKT_SZ-ix;
				}

				/*
				 * Read bytes
				 */
				if (!g_spi.spi_rx(&b[ix], nbytes)) {
					PRINT_ER("Failed data block read, bus error...\n");
					result = N_FAIL;
					goto _error_;
				}

				/*
				 * Read Crc
				 */
				if (!g_spi.crc_off) {
					if (!g_spi.spi_rx(crc, 2)) {
						PRINT_ER("Failed data block crc read, bus error...\n");
						result = N_FAIL;
						goto _error_;
					}
				}

				ix += nbytes;
				sz -= nbytes;
			}

			/*
			 * if any data is left unread,
			 * then read the rest using normal DMA code
			 */
			while (sz > 0) {
				int nbytes;

				if (sz <= DATA_PKT_SZ)
					nbytes = sz;
				else
					nbytes = DATA_PKT_SZ;

				/*
				 * read data response only on the next DMA
				 * cycles not the first DMA since data
				 * response header is already handled above
				 * for the first DMA.
				 */
				retry = SPI_RESP_RETRY_COUNT;
				do {
					if (!g_spi.spi_rx(&rsp, 1)) {
							PRINT_ER("Failed data response read, bus error...\n");
						result = N_FAIL;
						break;
					}
					if (((rsp >> 4) & 0xf) == 0xf)
						break;
				} while (retry--);

				if (result == N_FAIL)
					break;

				/*
				 * Read bytes
				 */
				if (!g_spi.spi_rx(&b[ix], nbytes)) {
					PRINT_ER("Failed data block read, bus error...\n");
					result = N_FAIL;
					break;
				}

				/*
				 * Read Crc
				 */
				if (!g_spi.crc_off) {
					if (!g_spi.spi_rx(crc, 2)) {
						PRINT_ER("Failed data block crc read, bus error...\n");
						result = N_FAIL;
						break;
					}
				}

				ix += nbytes;
				sz -= nbytes;
			}
		}
	}

_error_:
	return result;
}

static int spi_data_read(uint8_t *b, uint32_t sz)
{
	int retry, ix, nbytes;
	int result = N_OK;
	uint8_t crc[2];
	uint8_t rsp;

int i, k = sz;	
	
	/*
	 * Data
	 */
	ix = 0;
	do {
		if (sz <= DATA_PKT_SZ)
			nbytes = sz;
		else
			nbytes = DATA_PKT_SZ;

		/*
		 * Data Respnose header
		 */
		retry = SPI_RESP_RETRY_COUNT;
		do {
			if (!g_spi.spi_rx(&rsp, 1)) {
				PRINT_ER("Failed data response read, bus error...\n");
				result = N_FAIL;
				break;
			}
			if (((rsp >> 4) & 0xf) == 0xf)
				break;
		} while (retry--);

		if (result == N_FAIL)
			break;

		if (retry <= 0) {
			PRINT_ER("Failed data response read...(%02x)\n", rsp);
			result = N_FAIL;
			break;
		}

		/*
		 * Read bytes
		 */
		if (!g_spi.spi_rx(&b[ix], nbytes)) {
			PRINT_ER("Failed data block read, bus error...\n");
			result = N_FAIL;
			break;
		}

		/*
		 * Read Crc
		 */
		if (!g_spi.crc_off) {
			if (!g_spi.spi_rx(crc, 2)) {
				PRINT_ER("Failed data block crc read, bus error...\n");
				result = N_FAIL;
				break;
			}
		}

		ix += nbytes;
		sz -= nbytes;

	} while (sz);

	printk("%s ", __func__);
	for(i=0; i < k; i++) {
	   printk("%02X ", b[i]);
	}
	printk("\n");
	
	return result;
}

static int spi_data_write(uint8_t *b, uint32_t sz)
{
	int ix, nbytes;
	int result = 1;
	uint8_t cmd, order, crc[2] = {0};

	/*
	 * Data
	 */
	ix = 0;
	do {
		if (sz <= DATA_PKT_SZ)
			nbytes = sz;
		else
			nbytes = DATA_PKT_SZ;

		/*
		 * Write command
		 */
		cmd = 0xf0;
		if (ix == 0) {
			if (sz <= DATA_PKT_SZ)
				order = 0x3;
			else
				order = 0x1;
		} else {
			if (sz <= DATA_PKT_SZ)
				order = 0x3;
			else
				order = 0x2;
		}
		cmd |= order;
		if (!g_spi.spi_tx(&cmd, 1)) {
			PRINT_ER("Failed data block cmd write, bus error...\n");
			result = N_FAIL;
			break;
		}

		/*
		 * Write data
		 */
		if (!g_spi.spi_tx(&b[ix], nbytes)) {
			PRINT_ER("Failed data block write, bus error...\n");
			result = N_FAIL;
			break;
		}

		/*
		 * Write Crc
		 */
		if (!g_spi.crc_off) {
			if (!g_spi.spi_tx(crc, 2)) {
				PRINT_ER("Failed data block crc write, bus error...\n");
				result = N_FAIL;
				break;
			}
		}

		ix += nbytes;
		sz -= nbytes;
	} while (sz);

	return result;
}

/*
 * Spi Internal Read/Write Function
 */
static int spi_internal_write(uint32_t addr, uint32_t dat)
{
	int result = N_OK;
	uint8_t retry = SPI_RETRY_COUNT;

_RETRY_:

#if defined USE_OLD_SPI_SW
	/*
	 * Command
	 */
	result = spi_cmd(CMD_INTERNAL_WRITE, addr, dat, 4, 0);
	if (result != N_OK) {
		PRINT_ER("Failed internal write cmd...\n");
		goto _FAIL_;		
	}

	result = spi_cmd_rsp(CMD_INTERNAL_WRITE);
	if (result != N_OK) {
		PRINT_ER("Failed internal write cmd response...\n");		
		goto _FAIL_;
	}
#else
#ifdef BIG_ENDIAN
	dat = BYTE_SWAP(dat);
#endif
	result = spi_cmd_complete(CMD_INTERNAL_WRITE, addr,
				  (uint8_t *)&dat, 4,
				  0);
	if (result != N_OK) {
		PRINT_ER("Failed internal write cmd...\n");
		goto _FAIL_;
	}
#endif

_FAIL_:
	if(result != N_OK)
	{
		msleep(1);
		spi_reset();
		PRINT_ER("Reset and retry %d %x\n",retry, addr);
		msleep(1);
		retry--;
		if(retry) 
			goto _RETRY_;
	}
	return result;
}

static int spi_internal_read(uint32_t addr, uint32_t *data)
{
	int result = N_OK;
	uint8_t retry = SPI_RETRY_COUNT;

_RETRY_:

#if defined USE_OLD_SPI_SW
	result = spi_cmd(CMD_INTERNAL_READ, addr, 0, 4, 0);
	if (result != N_OK) {
		PRINT_ER("Failed internal read cmd...\n");
		goto _FAIL_;
	}

	result = spi_cmd_rsp(CMD_INTERNAL_READ);
	if (result != N_OK) {
		PRINT_ER("Failed internal read cmd response...\n");
		goto _FAIL_;
	}

	/*
	 * Data
	 */
	result = spi_data_read((uint8_t *)data, 4);
	if (result != N_OK) {
		PRINT_ER("Failed internal read data...\n");
		goto _FAIL_;
	}
#else
	result = spi_cmd_complete(CMD_INTERNAL_READ, addr,
				  (uint8_t *)data, 4,
				  0);
	if (result != N_OK) {
		PRINT_ER("Failed internal read cmd...\n");
		goto _FAIL_;
	}
#endif

#ifdef BIG_ENDIAN
	*data = BYTE_SWAP(*data);
#endif

_FAIL_:
	if(result != N_OK)
	{
		msleep(1);
		spi_reset();
		PRINT_ER("Reset and retry %d %x\n",retry, addr);
		msleep(1);
		retry--;
		if(retry) 
			goto _RETRY_;
	}
	return result;
}

/*
 * Spi interfaces
 */
static int spi_write_reg(uint32_t addr, uint32_t data)
{
	uint8_t retry = SPI_RETRY_COUNT;
	int result = N_OK;
	uint8_t cmd = CMD_SINGLE_WRITE;
	uint8_t clockless = 0;

_RETRY_:	
	if (addr <= 0x30)
	{
		/**
		WILC clockless registers.
		**/
		cmd = CMD_INTERNAL_WRITE;
		clockless = 1;
	}
	else
	{
		cmd = CMD_SINGLE_WRITE;
		clockless = 0;
	}
	
#if defined USE_OLD_SPI_SW
	result = spi_cmd(cmd, addr, data, 4, clockless);
	if (result != N_OK) {
		PRINT_ER("Failed cmd, write reg (%08x)...\n", addr);	
		goto _FAIL_;
	}

	result = spi_cmd_rsp(cmd);
	if (result != N_OK) {
		PRINT_ER("Failed cmd response, write reg (%08x)...\n", addr);	
		goto _FAIL_;
	}
#else
#ifdef BIG_ENDIAN
	data = BYTE_SWAP(data);
#endif
	result = spi_cmd_complete(cmd, addr, (uint8_t *)&data, 4, clockless);
	if (result != N_OK) {
		PRINT_ER("Failed cmd, write reg (%08x)...\n", addr);		
		goto _FAIL_;
	}
#endif

_FAIL_:
	if(result != N_OK)
	{
		msleep(1);
		spi_reset();
		PRINT_ER("Reset and retry %d %x %d\n",retry, addr, data);
		msleep(1);
		retry--;
		if(retry) 
			goto _RETRY_;
	}
	return result;
}

static int spi_write(uint32_t addr, uint8_t *buf, uint32_t size)
{
	int result;
	uint8_t retry = SPI_RETRY_COUNT;
	uint8_t cmd = CMD_DMA_EXT_WRITE;

	/*
	 * has to be greated than 4
	 */
	if (size <= 4)
		return 0;

_RETRY_:

#if defined USE_OLD_SPI_SW
	/*
	 * Command
	 */
	result = spi_cmd(cmd, addr, 0, size, 0);
	if (result != N_OK) {
		PRINT_ER("Failed cmd, write block (%08x)...\n", addr);		
		goto _FAIL_;
	}

	result = spi_cmd_rsp(cmd);
	if (result != N_OK) {
		PRINT_ER("Failed cmd response, write block (%08x)...\n", addr);
		goto _FAIL_;
	}
#else
	result = spi_cmd_complete(cmd, addr, NULL, size, 0);
	if (result != N_OK) {
		PRINT_ER("Failed cmd, write block (%08x)...\n", addr);		
		goto _FAIL_;
	}
#endif

	/*
	 * Data
	 */
	result = spi_data_write(buf, size);
	if (result != N_OK) {
		PRINT_ER("Failed block data write...\n");
		goto _FAIL_;
	}

	/**
		Data RESP
	**/
	result = spi_data_rsp(cmd);
	if (result != N_OK) {
		PRINT_ER("Failed block data write...\n");
		goto _FAIL_;
	}
	
_FAIL_:
	if(result != N_OK)
	{
		msleep(1);
		spi_reset();
		PRINT_ER("Reset and retry %d %x %d\n",retry, addr, size);
		msleep(1);
		retry--;
		if(retry) 
			goto _RETRY_;
	}
	return result;
}

static int spi_read_reg(uint32_t addr, uint32_t *data)
{
	uint8_t retry = SPI_RETRY_COUNT;
	int result = N_OK;
	uint8_t cmd = CMD_SINGLE_READ;
	uint8_t clockless = 0;

_RETRY_:

	if (addr <= 0x30)
	{
		/**
		WILC clockless registers.
		**/
		cmd = CMD_INTERNAL_READ;
		clockless = 1;
	}
	else
	{
		cmd = CMD_SINGLE_READ;
		clockless = 0;
	}

#if defined USE_OLD_SPI_SW
	result = spi_cmd(cmd, addr, 0, 4, clockless);
	if (result != N_OK) {
		PRINT_ER("Failed cmd, read reg (%08x)...\n", addr);
		goto _FAIL_;
	}
	result = spi_cmd_rsp(cmd);
	if (result != N_OK) {
		PRINT_ER("Failed cmd response, read reg (%08x)...\n", addr);
		goto _FAIL_;
	}

	result = spi_data_read((uint8_t *)data, 4);
	if (result != N_OK) {
		PRINT_ER("Failed data read...\n");
		goto _FAIL_;
	}
#else

	result = spi_cmd_complete(cmd, addr, (uint8_t *)data, 4, clockless);
	if (result != N_OK) {
		PRINT_ER("Failed cmd, read reg (%08x)...\n", addr);
		goto _FAIL_;
	}
#endif

#ifdef BIG_ENDIAN
	*data = BYTE_SWAP(*data);
#endif

_FAIL_:
	if(result != N_OK)
	{
		msleep(1);
		spi_reset();
		PRINT_ER("Reset and retry %d %x\n",retry, addr);
		msleep(1);
		retry--;
		if(retry) 
			goto _RETRY_;
	}	
	return result;
}

static int spi_read(uint32_t addr, uint8_t *buf, uint32_t size)
{
	uint8_t cmd = CMD_DMA_EXT_READ;
	int result;
	uint8_t retry = SPI_RETRY_COUNT;

	if (size <= 4)
		return 0;

_RETRY_:
	
#if defined USE_OLD_SPI_SW
	/*
	 * Command
	 */
	result = spi_cmd(cmd, addr, 0, size, 0);
	if (result != N_OK) {
		PRINT_ER("Failed cmd, read block (%08x)...\n", addr);
		goto _FAIL_;
	}  
 
	result = spi_cmd_rsp(cmd);
	if (result != N_OK) {
		PRINT_ER("Failed cmd response, read block (%08x)...\n", addr);
		goto _FAIL_;
	}

	/*
	 * Data
	 */
	result = spi_data_read(buf, size);
	if (result != N_OK) {
		PRINT_ER("Failed block data read...\n");
		goto _FAIL_;
	}
#else
	result = spi_cmd_complete(cmd, addr, buf, size, 0);
	if (result != N_OK) {
		PRINT_ER("Failed cmd, read block (%08x)...\n", addr);
		goto _FAIL_;
	}
#endif

_FAIL_:
	if(result != N_OK)
	{
		msleep(1);
		spi_reset();
		PRINT_ER("Reset and retry %d %x %d\n",retry, addr, size);
		msleep(1);
		retry--;
		if(retry) 
			goto _RETRY_;
	}
	return result;
}

/*
 * Bus Interfaces
 */
static void spi_init_pkt_sz(uint32_t* reg)
{
	switch(DATA_PKT_SZ)	
	{
		case DATA_PKT_SZ_256:  
			*reg |= (0 << 4); 
			break;
		case DATA_PKT_SZ_512:  
			*reg |= (1 << 4); 
			break;
		case DATA_PKT_SZ_1K: 
			*reg |= (2 << 4); 
			break;
		case DATA_PKT_SZ_2K: 
			*reg |= (3 << 4); 
			break;
		case DATA_PKT_SZ_4K: 
			*reg |= (4 << 4); 
			break;
		case DATA_PKT_SZ_8K: 
			*reg |= (5 << 4); 
			break;
	}
}

static int spi_reset(void)
{
	int result = N_OK;
	
	result = spi_cmd_complete(CMD_RESET, 0, 0 ,0, 0);
	if (result != N_OK) {
		PRINT_ER("Failed cmd reset \n");	
		return 0;
	}
	
	return 1;
}

static int spi_clear_int(void)
{
	uint32_t reg;
	if (!spi_read_reg(WILC_HOST_RX_CTRL_0, &reg)) {
		PRINT_ER("Failed read reg (%08x)...\n", WILC_HOST_RX_CTRL_0);
		return 0;
	}
	reg &= ~0x1;
	spi_write_reg(WILC_HOST_RX_CTRL_0, reg);
	return 1;
}

static int spi_deinit(void *pv)
{
	return 1;
}

static int spi_sync(void)
{
	uint32_t reg;
	int ret;

	/*
	 * interrupt pin mux select
	 */
	ret = spi_read_reg(WILC_PIN_MUX_0, &reg);
	if (!ret) {
		PRINT_ER("Failed read reg (%08x)...\n", WILC_PIN_MUX_0);
		return 0;
	}
	reg |= (1 << 8);
	ret = spi_write_reg(WILC_PIN_MUX_0, reg);
	if (!ret) {
		PRINT_ER("Failed write reg (%08x)...\n", WILC_PIN_MUX_0);
		return 0;
	}

	/*
	 * interrupt enable
	 */
	ret = spi_read_reg(WILC_INTR_ENABLE, &reg);
	if (!ret) {
		PRINT_ER("Failed read reg (%08x)...\n", WILC_INTR_ENABLE);
		return 0;
	}
	reg |= (1 << 16);
	ret = spi_write_reg(WILC_INTR_ENABLE, reg);
	if (!ret) {
		PRINT_ER("Failed write reg (%08x)...\n", WILC_INTR_ENABLE);
		return 0;
	}

	return 1;
}

static int spi_init(struct wilc_wlan_inp *inp)
{
	uint32_t reg;
	uint8_t dat[32];
	uint32_t chipid;
	uint8_t result;

	static int isinit = 0;

	if(isinit) {
		if (!spi_read_reg(0x3b0000, &chipid)) {
			PRINT_ER("Fail cmd read chip id...\n");
			return 0;
		}
		return 1;
	}

	memset(&g_spi, 0, sizeof(struct wilc_spi));

	if (inp->io_func.io_init) {
		if (!inp->io_func.io_init(&(inp->os_context))) {
			PRINT_ER("Failed io init bus...\n");
			return 0;
		}
	} else {
		return 0;
	}
	g_spi.spi_tx = inp->io_func.u.spi.spi_tx;
	g_spi.spi_rx = inp->io_func.u.spi.spi_rx;
	g_spi.spi_trx = inp->io_func.u.spi.spi_trx;

	/**
		configure protocol 
	**/
	g_spi.crc_off = 0;


	/* Simpler test probe */
	result = spi_cmd_complete(CMD_INTERNAL_READ, WILC_SPI_PROTOCOL_OFFSET,
			  (uint8_t *)dat, 4,
			  0);
	if (result != N_OK) {
		printk(KERN_INFO "wilc3000: Not detected.\n");
		return 0;
	}
	printk(KERN_INFO "wilc3000: Detected.\n");

	// TODO: We can remove the CRC trials if there is a definite way to reset 
	// the SPI to it's initial value.
	if (!spi_internal_read(WILC_SPI_PROTOCOL_OFFSET, &reg)) {
		/* Read failed. Try with CRC off. This might happen when module 
		is removed but chip isn't reset*/
		g_spi.crc_off = 1;
		PRINT_ER("Failed internal read protocol with CRC on, retyring with CRC off...\n");
		if (!spi_internal_read(WILC_SPI_PROTOCOL_OFFSET, &reg)){
			// Reaad failed with both CRC on and off, something went bad
			PRINT_ER("Failed internal read protocol...\n");
			return 0;
		}
	}
	if(g_spi.crc_off == 0)
	{
		reg &= ~0xc;			/* disable crc checking */
		reg &= ~0x70;		/*Reset pkt size bits*/
		spi_init_pkt_sz(&reg);
		if (!spi_internal_write(WILC_SPI_PROTOCOL_OFFSET, reg)) {
			PRINT_ER("Failed internal write protocol reg...\n");
			return 0;
		}
		g_spi.crc_off = 1;
	}

	/**
		make sure can read back chip id correctly
	**/
	if (!spi_read_reg(0x3b0000, &chipid)) {
		PRINT_ER("Fail cmd read chip id...\n");
		return 0;
	}
	
	g_spi.has_thrpt_enh = 1;
		
	isinit = 1;

	return 1;
}

static int spi_read_size(uint32_t *size)
{
	int ret;

	if (g_spi.has_thrpt_enh) {
		ret = spi_internal_read(0xe840 - WILC_SPI_REG_BASE, size);
		*size = *size  & IRQ_DMA_WD_CNT_MASK;
	} else {
		uint32_t tmp;
		uint32_t byte_cnt;

		ret = spi_read_reg(WILC_VMM_TO_HOST_SIZE, &byte_cnt);

		if (!ret) {
			PRINT_ER("Failed read WILC_VMM_TO_HOST_SIZE\n");
			goto _fail_;
		}
		tmp = (byte_cnt >> 2) & IRQ_DMA_WD_CNT_MASK;
		*size = tmp;
	}
_fail_:
	return ret;
}

static int spi_read_int(uint32_t *int_status)
{
	int ret;
	int happended, unknown_mask, j;

	if (g_spi.has_thrpt_enh) {
		ret = spi_internal_read(0xe840 - WILC_SPI_REG_BASE, int_status);
	} else {
		uint32_t tmp;
		uint32_t byte_cnt;

		ret = spi_read_reg(WILC_VMM_TO_HOST_SIZE, &byte_cnt);

		if (!ret) {
			PRINT_ER("Failed read WILC_VMM_TO_HOST_SIZE\n");
			goto _fail_;
		}

		tmp = (byte_cnt >> 2) & IRQ_DMA_WD_CNT_MASK;

		j = 0;
		do {
			uint32_t irq_flags;

			happended = 0;
			spi_read_reg(0x1a90, &irq_flags);
			tmp |= ((irq_flags >> 27) << IRG_FLAGS_OFFSET);

			if (g_spi.nint > 5) {
				spi_read_reg(0x1a94, &irq_flags);
				tmp |= (((irq_flags >> 0) & 0x7) << (IRG_FLAGS_OFFSET + 5));
			}

			unknown_mask = ~((1ul << g_spi.nint) - 1);
			if ((tmp >> IRG_FLAGS_OFFSET) & unknown_mask) {
				PRINT_ER("Unexpected int:j=%d, tmp=%x, mask=%x\n"
				       , j, tmp, unknown_mask);
				happended = 1;
			}
			j++;
		} while (happended);

		*int_status = tmp;
	}
_fail_:
	return ret;
}

static int spi_clear_int_ext(uint32_t val)
{
	int ret;
	uint32_t tbl_ctl;

	if (g_spi.has_thrpt_enh) {
		ret = spi_internal_write(0xe844 - WILC_SPI_REG_BASE, val);
	} else {
		uint32_t flags;

		flags = val & ((1 << MAX_NUM_INT) - 1);
		if (flags) {
			int i;

			ret = 1;
			for (i = 0; i < g_spi.nint; i++) {
				/*
				 * No matter what you write 1 or 0,
				 * it will clear interrupt.
				 */
				if (flags & 1)
					ret = spi_write_reg(0x10c8 + i * 4, 1);
				if (!ret)
					break;
				flags >>= 1;
			}
			if (!ret) {
				PRINT_ER("Failed spi_write_reg, set reg %x ...\n", 0x10c8+i*4);
				goto _fail_;
			}
			for (i = g_spi.nint; i < MAX_NUM_INT; i++) {
				if (flags & 1)
					PRINT_ER("Unexpected interrupt cleared %d...\n", i);
				flags >>= 1;
			}
		}

		tbl_ctl = 0;
		/* select VMM table 0 */
		if ((val & SEL_VMM_TBL0) == SEL_VMM_TBL0)
			tbl_ctl |= (1 << 0);
		/* select VMM table 1 */
		if ((val & SEL_VMM_TBL1) == SEL_VMM_TBL1)
			tbl_ctl |= (1 << 1);

		ret = spi_write_reg(WILC_VMM_TBL_CTL, tbl_ctl);
		if (!ret) {
			PRINT_ER("fail write reg vmm_tbl_ctl\n");
			goto _fail_;
		}
		if ((val & EN_VMM) == EN_VMM) {
			/* enable vmm transfer */
			ret = spi_write_reg(WILC_VMM_CORE_CTL, 1);
			if (!ret) {
				PRINT_ER("fail write reg vmm_core_ctl\n");
				goto _fail_;
			}
		}
	}
_fail_:
	return ret;
}

static int spi_sync_ext(int nint)
{
	uint32_t reg;
	int ret, i;

	if (nint > MAX_NUM_INT) {
		PRINT_ER("Too many interupts %d\n", nint);
		return 0;
	}

	g_spi.nint = nint;

	/*
	 * interrupt pin mux select
	 */
	ret = spi_read_reg(WILC_PIN_MUX_0, &reg);
	if (!ret) {
		PRINT_ER("Failed read reg %08x\n", WILC_PIN_MUX_0);
		return 0;
	}
	reg |= (1 << 8);
	ret = spi_write_reg(WILC_PIN_MUX_0, reg);
	if (!ret) {
		PRINT_ER("Failed write reg %08x\n", WILC_PIN_MUX_0);
		return 0;
	}

	/*
	 * interrupt enable
	 */
	ret = spi_read_reg(WILC_INTR_ENABLE, &reg);
	if (!ret) {
		PRINT_ER("Failed read reg %08x\n", WILC_INTR_ENABLE);
		return 0;
	}

	for (i = 0; (i < 5) && (nint > 0); i++, nint--)
		reg |= (1 << (27 + i));

	ret = spi_write_reg(WILC_INTR_ENABLE, reg);
	if (!ret) {
		PRINT_ER("Failed write reg %08x\n", WILC_INTR_ENABLE);
		return 0;
	}
	if (nint) {
		ret = spi_read_reg(WILC_INTR2_ENABLE, &reg);
		if (!ret) {
			PRINT_ER("Failed read reg %08x\n", WILC_INTR2_ENABLE);
			return 0;
		}

		for (i = 0; (i < 3) && (nint > 0); i++, nint--)
			reg |= (1 << i);

		ret = spi_read_reg(WILC_INTR2_ENABLE, &reg);
		if (!ret) {
			PRINT_ER("Failed write reg %08x\n", WILC_INTR2_ENABLE);
			return 0;
		}
	}

	return 1;
}

/*
 * Global spi HIF function table
 */
struct wilc_hif_func hif_spi = {
	spi_init,
	spi_deinit,
	spi_read_reg,
	spi_write_reg,
	spi_read,
	spi_write,
	spi_sync,
	spi_clear_int,
	spi_read_int,
	spi_clear_int_ext,
	spi_read_size,
	spi_write,
	spi_read,
	spi_sync_ext,
	spi_reset,
};
EXPORT_SYMBOL(hif_spi);
