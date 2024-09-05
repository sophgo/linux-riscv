#include <string.h>
#include "efuse.h"
#include "devmem.h"
#include "debug.h"
#include "argvtoint.h"
#include <linux/fs.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>


/*
 * define the debug level of this file,
 * please see 'debug.h' for detail info
 */
//DEBUG_SET_LEVEL(DEBUG_LEVEL_DEBUG);
int debug = DEBUG_LEVEL_ERR;

/*
 * Organization of EFUSE_ADR register:
 *   [  i:   0]: i <= 6, address of 32-bit cell
 *   [i+6: i+1]: 5-bit bit index within the cell
 */

#define NUM_ADDRESS_BITS 8

#define EFUSE_BASE 0x7040000000


struct efuse_ioctl_data {
	uint32_t addr;
	uint32_t val;
};

#define EFUSE_IOCTL_READ	_IOWR('y', 0x20, struct efuse_ioctl_data)
#define EFUSE_IOCTL_WRITE	_IOWR('y', 0x21, struct efuse_ioctl_data)


static const uint64_t EFUSE_MODE = EFUSE_BASE;
static const uint64_t EFUSE_ADR = EFUSE_BASE + 0x4;
static const uint64_t EFUSE_RD_DATA = EFUSE_BASE + 0xc;

uint32_t efuse_num_cells(void)
{
	return (uint32_t) 1 << NUM_ADDRESS_BITS;
}

static int cell_is_empty(uint32_t c)
{
	return c == 0;
}

static uint32_t num_empty_cells(void)
{
	uint32_t cells = 0;
	uint32_t n = efuse_num_cells();

	for (uint32_t i = 0; i < n; i++) {
		uint32_t v = efuse_embedded_read(i);

		if (cell_is_empty(v))
			cells++;
	}
	return cells;
}

static uint32_t find_empty_cell(uint32_t start)
{
	uint32_t n = efuse_num_cells();

	for (uint32_t i = start; i < n; i++) {
		uint32_t v = efuse_embedded_read(i);

		if (cell_is_empty(v))
			return i;
	}
	return n;
}

static uint32_t efuse_readflg(uint32_t wdata)
{
	uint32_t n = 128;
	char str[10];
	char flag[10];

	IntToHexStr(wdata, flag);
	for (uint32_t i = 0; i < n; i++) {
		uint32_t v = efuse_embedded_read(i);

		sprintf(str, "%08x", v);
		if (strncmp(flag, str, 2) == 0)
			ERR("efuse_readflg :cell[%d] =  0x%x\n", i, v);
	}
	return n;
}

static void efuse_mode_md_write(uint32_t val)
{
	uint32_t mode = devmem_readl(EFUSE_MODE);
	uint32_t new = (mode & 0xfffffffc) | (val & 0x3); //0b11

	devmem_writel(EFUSE_MODE, new);
}

static void efuse_mode_wait_ready(void)
{
	while (devmem_readl(EFUSE_MODE) != 0x80)
		;
}

static void efuse_mode_reset(void)
{
	devmem_writel(EFUSE_MODE, 0);
	efuse_mode_wait_ready();
}

static uint32_t make_adr_val(uint32_t address, uint32_t bit_i)
{
	const uint32_t address_mask = (1 << NUM_ADDRESS_BITS) - 1;

	return (address & address_mask) | ((bit_i & 0x1f) << NUM_ADDRESS_BITS);
}

static void efuse_set_bit(uint32_t address, uint32_t bit_i)
{
	efuse_mode_reset();
	uint32_t adr_val = make_adr_val(address, bit_i);
	//ERR("%s, 0x%08x\n", __func__, adr_val);
	devmem_writel(EFUSE_ADR, adr_val);
	efuse_mode_md_write(0x3); //0b11
	efuse_mode_wait_ready();
}

uint32_t efuse_embedded_read(uint32_t address)
{
#if 1
	efuse_mode_reset();
	uint32_t adr_val = make_adr_val(address, 0);
	//ERR("%s, 0x%08x\n", __func__, adr_val);
	devmem_writel(EFUSE_ADR, adr_val);
	efuse_mode_md_write(0x2); //0b10
	efuse_mode_wait_ready();
	return devmem_readl(EFUSE_RD_DATA);
#else
	int file = open("/dev/bm_efuse", O_RDWR);
	struct efuse_ioctl_data data;
	int ret = 0;

	if (file < 0) {
		ERR("open efuse device failed errno=0x%x\n", errno);
		return -ENODEV;
	}
	data.addr = address;
	ret = ioctl(file, EFUSE_IOCTL_READ, &data);
	if (ret < 0) {
		ERR("EFUSE_IOCTL_READ fail,errno=0x%x", errno);
		close(file);
		return ret;
	}
	close(file);
	return data.val;
#endif
}

void efuse_embedded_write(uint32_t address, uint32_t val)
{
#if 1
	for (int i = 0; i < 32; i++)
		if ((val >> i) & 1)
			efuse_set_bit(address, i);
#else

	int file = open("/dev/bm_efuse", O_RDWR);
	struct efuse_ioctl_data data;
	int ret = 0;

	if (file < 0) {
		ERR("open efuse device failed %d\n", errno);
		return;
	}
	data.addr = address;
	data.val = val;
	ret = ioctl(file, EFUSE_IOCTL_WRITE, &data);
	if (ret < 0)
		ERR("EFUSE_IOCTL_WRITE fail ,errno=0x%x\n", errno);

	close(file);
#endif
}

long int xstrtol(const char *s)
{
	long int tmp;

	errno = 0;
	tmp = strtol(s, NULL, 0);
	if (!errno)
		return tmp;

	if (errno == ERANGE)
		fprintf(stderr, "Bad integer format: %s\n",  s);
	else
		fprintf(stderr, "Error while parsing %s: %s\n", s,
				strerror(errno));

	exit(EXIT_FAILURE);
}


int main(int argc, char *argv[])
{
	int index = 0, i;
	uint32_t rdata = 0;
	uint32_t wdata = 0x55aa1234;

	if (argc < 3) {
		ERR("Usage: efuse [write|read} data\n");
		ERR("Usage: efuse -w addr -v val,  efuse -w 0x10 -v 0x1234\n");
		ERR("Usage: efuse -r addr -l len,  efuse -r 0x10 -l 0x4\n");
		return 0;
	}
	wdata = parser_str(argv[2]);

	if (strncmp("write", argv[1], 5) == 0) {
		index = 4;
		/* find the first empty cell begin from index */
		index = find_empty_cell(index);
		ERR("index = %d\n", index);
		ERR("wdata = 0x%x\n", wdata);

		efuse_embedded_write(index, wdata);

		rdata = efuse_embedded_read(index);
		if (rdata != wdata) {
			ERR("read write data error!\n");
			return 1;
		}
		ERR("rdata = 0x%x\n", rdata);
	} else if (strncmp("read", argv[1], 4) == 0) {
		efuse_readflg(wdata);
	} else {
		int index, read_len, is_read = 0, option;

		while ((option = getopt(argc, argv, "w:v:r:l:")) != -1) {
			switch (option) {
			case 'r':
			case 'w':
				index = xstrtol(optarg);
				if (index < 0 || index > 255) {
					ERR("wrong efuse address!\n");
					return 0;
				}
				is_read =  option == 'r' ? 1 : 0;
				break;
			case 'v':
				wdata = xstrtol(optarg);
				break;
			case 'l':
				read_len = xstrtol(optarg);
				break;
			}
		}

		if (is_read) {
			for (i = 0; i < read_len; i++) {
				rdata = efuse_embedded_read(index + i);
				ERR("efuse_read :cell[%d] =  0x%x\n",
					index + i, rdata);
			}
		} else {
			if (efuse_embedded_read(index)) {
				ERR("dest addr not empty!\n");
#if 0
				return -1;
#endif
			}
			efuse_embedded_write(index, wdata);
			rdata = efuse_embedded_read(index);
			if (rdata != wdata) {
				ERR("write efuse failed or cell wrote before, write 0x%x, get 0x%x\n", wdata, rdata);
				return -1;
			}
		}

	}
	return 0;
}
