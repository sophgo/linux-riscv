#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>

#if 1
#define LOGV(args...) printf(args)
#else
#define LOGV(args...)
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef int32_t s32;

static int i2c_file; // I2C device fd
static unsigned char i2c_addr; // I2C slave address

int i2c_smbus_xfer(uint8_t read_write, uint8_t command, uint32_t size, union i2c_smbus_data *data)
{
	int ret = 0;
	struct i2c_smbus_ioctl_data i2c_smbus_data = {
		.read_write = read_write,
		.command = command,
		.size = size,
		.data = data,
	};

	ret = ioctl(i2c_file, I2C_SLAVE, i2c_addr);
	if (ret < 0) {
		printf("ioctl set slave addr fail %d\n", ret);
		return ret;
	}

	ret = ioctl(i2c_file, I2C_SMBUS, (unsigned long)&i2c_smbus_data);
	if (ret < 0) {
		printf("I2C_SMBUS failed %d %d\n", ret, errno);
		return ret;
	}

	return 0;
}


s32 i2c_smbus_read_byte(void)
{
	union i2c_smbus_data data;
	int status;

	status = i2c_smbus_xfer(I2C_SMBUS_READ, 0, I2C_SMBUS_BYTE, &data);
	return (status < 0) ? status : data.byte;
}

s32 i2c_smbus_write_byte(u8 value)
{
	union i2c_smbus_data data;

	data.byte = value;
	return i2c_smbus_xfer(I2C_SMBUS_WRITE, 0, I2C_SMBUS_BYTE, &data);
}

s32 i2c_smbus_read_byte_data(u8 command)
{
	union i2c_smbus_data data;
	int status;

	status = i2c_smbus_xfer(I2C_SMBUS_READ, command, I2C_SMBUS_BYTE_DATA, &data);
	return (status < 0) ? status : data.byte;
}

s32 i2c_smbus_write_byte_data(u8 command, u8 value)
{
	union i2c_smbus_data data;

	data.byte = value;
	return i2c_smbus_xfer(I2C_SMBUS_WRITE, command, I2C_SMBUS_BYTE_DATA, &data);
}

s32 i2c_smbus_read_word_data(u8 command)
{
	union i2c_smbus_data data;
	int status;

	status = i2c_smbus_xfer(I2C_SMBUS_READ, command, I2C_SMBUS_WORD_DATA, &data);
	return (status < 0) ? status : data.word;
}

s32 i2c_smbus_write_word_data(u8 command, u16 value)
{
	union i2c_smbus_data data;

	data.word = value;
	return i2c_smbus_xfer(I2C_SMBUS_WRITE, command, I2C_SMBUS_WORD_DATA, &data);
}

s32 i2c_smbus_read_block_data(u8 command, u8 *values)
{
	union i2c_smbus_data data;
	int status;

	status = i2c_smbus_xfer(I2C_SMBUS_READ, command, I2C_SMBUS_BLOCK_DATA, &data);
	if (status)
		return status;

printf("raw %x %x %x %x %x\n", data.block[0], data.block[1], data.block[2], data.block[3], data.block[4]);
	memcpy(values, &data.block[1], data.block[0]);
	return data.block[0];
}

s32 i2c_smbus_write_block_data(u8 command, u8 length, const u8 *values)
{
	union i2c_smbus_data data;

	if (length > I2C_SMBUS_BLOCK_MAX)
		length = I2C_SMBUS_BLOCK_MAX;
	data.block[0] = length;
	memcpy(&data.block[1], values, length);
	return i2c_smbus_xfer(I2C_SMBUS_WRITE, command, I2C_SMBUS_BLOCK_DATA, &data);
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

#define TYPE_BYTE	1
#define TYPE_WORD	2
#define TYPE_ISL68127	3
#define TYPE_ISL68224	4
#define TYPE_RAA228236	5
#define TYPE_MP2973 6
#define FLAG_WRITE	(1 << 8)


int dump_I(void)
{
	short val;
	int ret;
	ret = i2c_smbus_read_byte_data(0x98);
	printf("pmbus revision 0x%x\n", ret);
	if (ret < 0)
		return -1;

	ret = i2c_smbus_write_byte_data(0x0, 0x0);
	printf("switch to page 0, ret=%d\n", ret);

	ret = i2c_smbus_read_word_data(0x8B);
	val = ret & 0xFFFF;
	printf("output voltage: %humV\n", val);

	ret = i2c_smbus_read_word_data(0x8C);
	val = ret & 0xFFFF;
	printf("output current: %hdmA\n", val * 100);

	ret = i2c_smbus_read_word_data(0x8D);
	val = ret & 0xFFFF;
	printf("temperature 1: %hd`C\n", val);

	ret = i2c_smbus_read_word_data(0x96);
	val = ret & 0xFFFF;
	printf("output power: %hdW\n", val);

	ret = i2c_smbus_write_byte_data(0x0, 0x1);
	printf("switch to page 1, ret=%d\n", ret);

	ret = i2c_smbus_read_word_data(0x8B);
	val = ret & 0xFFFF;
	printf("output voltage: %humV\n", val);

	ret = i2c_smbus_read_word_data(0x8C);
	val = ret & 0xFFFF;
	printf("output current: %hdmA\n", val * 100);

	ret = i2c_smbus_read_word_data(0x8D);
	val = ret & 0xFFFF;
	printf("temperature 1: %hd`C\n", val);

	ret = i2c_smbus_read_word_data(0x96);
	val = ret & 0xFFFF;
	printf("output power: %hdW\n", val);
	return 0;
}

double power(double x, int y)
{
	int sign = 0;
	double ret = 1;
	if (y < 0)
	{
		y = -y;
		sign = -1;
	}
	for (int i = 0; i < y; i++)
	{
		ret *= x;
	}
	return sign ? 1 / ret : ret;
}

int dump_M(void)
{
	short val;
	int ret;
	unsigned int reso_i = 0, reso_v = 0, vout_mode = 0;
	signed char exp;
	ret = i2c_smbus_read_byte_data(0x98);
	printf("pmbus revision 0x%x\n", ret);
	if (ret < 0)
		return -1;

	ret = i2c_smbus_write_byte_data(0x0, 0x2);
	printf("switch to page 2, ret=%d\n", ret);

	ret = i2c_smbus_read_byte_data(0x0d);
	val = ret & 0xFFFF;
	printf("config register 0x0d on page 2: %04x\n", val);
	if ((val >> 4) & 0x0001)
		printf("vid step resolution: 5mv\n");
	else
		printf("vid step resolution: 10mv\n");

	ret = i2c_smbus_write_byte_data(0x0, 0x0);
	printf("switch to page 0, ret=%d\n", ret);

	ret = i2c_smbus_read_byte_data(0xc7);
	val = ret & 0xFFFF;
	printf("config register 0xc7 on page 0: %04x\n", val);
	reso_i = (val >> 4) & 0x0003;
	vout_mode = (val >> 6) & 0x0003;
	printf("iout resolution: %sA/LSB\n", reso_i == 0 ? "2" : (reso_i == 1 ? "1" : "0.5"));
	printf("vout mode: %s\n", vout_mode == 0 ? "VID" : (vout_mode == 1 ? "Linear" : "Direct"));

	ret = i2c_smbus_read_word_data(0x8B);
	val = ret & 0x0FFF;
	printf("output voltage: %humV\n", val);

	ret = i2c_smbus_read_word_data(0x8C);
	val = ret & 0x07FF;
	ret = ret >> 11;
	exp = (ret & 0x10) ? (ret | 0xe0) : ret;
	printf("output current: %lfA\n", val * power(10, exp) * (reso_i == 0 ? 2 : (reso_i == 1 ? 1 : 0.5)));

	ret = i2c_smbus_read_word_data(0x8D);
	val = ret & 0x00FF;
	printf("temperature: %hd'C\n", val);

	ret = i2c_smbus_read_word_data(0x96);
	val = ret & 0x07FF;
	printf("output power: %hdW\n", val);

	ret = i2c_smbus_write_byte_data(0x0, 0x1);
	printf("switch to page 1, ret=%d\n", ret);

	ret = i2c_smbus_read_byte_data(0xc7);
	val = ret & 0xFFFF;
	printf("config register 0xc7 on page 1: %04x\n", val);
	reso_i = (val >> 2) & 0x0001;
	vout_mode = (val >> 3) & 0x0003;
	printf("iout resolution: %sA/LSB\n", reso_i == 0 ? "0.5" : "0.25");
	printf("vout mode: %s\n", vout_mode == 0 ? "VID" : (vout_mode == 1 ? "Linear" : "Direct"));

	ret = i2c_smbus_read_word_data(0x8B);
	val = ret & 0x0FFF;
	printf("output voltage: %humV\n", val);

	ret = i2c_smbus_read_word_data(0x8C);
	val = ret & 0x07FF;
	ret = ret >> 11;
	exp = (ret & 0x10) ? (ret | 0xe0) : ret;
	printf("output current: %lfA\n", val * power(10, exp) * (reso_i == 0 ? 0.5 : 0.25));

	ret = i2c_smbus_read_word_data(0x8D);
	val = ret & 0x00FF;
	printf("temperature: %hd'C\n", val);

	ret = i2c_smbus_read_word_data(0x96);
	val = ret & 0x07FF;
	printf("output power: %hdW\n", val);
	
	ret = i2c_smbus_write_byte_data(0x0, 0x0);
	printf("go back to page 0, ret=%d\n", ret);
	return 0;
}

int main(int argc, char * const argv[])
{
	int ret = 0, option, i2c_port = -1, type = 0;
	char i2c_file_name[20];
	unsigned char block_data[I2C_SMBUS_BLOCK_MAX];
	unsigned char i2c_reg = 0;
	unsigned int i2c_val = 0;

	if (argc == 1) {
		printf("usage:\n\t"
			"to read or write register manually:\n\t"
			"./pmbus -d <port> -s <addr> -b/w <reg> [-v] [value]\n\t"
			"\t -d: iic port\n\t"
			"\t -s: slave address\n\t"
			"\t -b/w: data width, -b = 8bit, -w = 16bit\n\t"
			"\t -v: if specified, write data to the register(read from if not specified)\n\t"
			"for example, to read 1 byte from iic port 0, slave address 0x60, register 0x21, use:\n\t"
			"./pmbus -d 0 -s 0x60 -b 0x21\n\t"
			"for example, to write 1 byte 0x0F to the register above, use:\n\t"
			"./pmbus -d 0 -s 0x60 -b 0x21 -v 0x0f\n\t"
			"to dump ISL68127, ISL68224 or RAA238236 info: ./pmbus -d 0 -s 0x5c -i\n\t"
			"to dump MP2973 info: ./pmbus -d 1 -s 0x60 -m\n");
		return -1;
	}

	while ((option = getopt(argc, argv, "d:s:b:w:v:im")) != -1) {
		switch (option) {
		case 'd':
			i2c_port = xstrtol(optarg);
			break;
		case 's':
			i2c_addr = xstrtol(optarg);
			break;
		case 'b':
			type = TYPE_BYTE;
			i2c_reg = xstrtol(optarg);
			break;
		case 'w':
			type = TYPE_WORD;
			i2c_reg = xstrtol(optarg);
			break;
		case 'v':
			type |= FLAG_WRITE;
			i2c_val = xstrtol(optarg);
			printf("%s %d: write 0x%x to iic\n", __func__, __LINE__, i2c_val);
			break;
		case 'i':
			type = TYPE_ISL68127;
			break;
		case 'm':
			type = TYPE_MP2973;
			break;
		}
	}

	if (i2c_port < 0 || !i2c_addr || !type) {
		printf("please specify I2C port and device\n");
		return -EFAULT;
	}
	printf("I2C port %d, addr 0x%x, type 0x%x, reg 0x%x, value 0x%x\n",
		i2c_port, i2c_addr, type, i2c_reg, i2c_val);

	snprintf(i2c_file_name, sizeof(i2c_file_name), "/dev/i2c-%d", i2c_port);
	i2c_file = open(i2c_file_name, O_RDWR);
	if (i2c_file < 0) {
		printf("open I2C device node failed: %d\n", errno);
		return -ENODEV;
	}

	if ((type & 0xFF) == TYPE_BYTE) {
		if (type & FLAG_WRITE)
			ret = i2c_smbus_write_byte_data(i2c_reg, i2c_val);
		else
			ret = i2c_smbus_read_byte_data(i2c_reg);
		printf("result: %d\n", ret);
	}

	if ((type & 0xFF) == TYPE_WORD) {
		if (type & FLAG_WRITE)
			ret = i2c_smbus_write_word_data(i2c_reg, i2c_val);
		else {
			char res[4];
			char temp = 0;
			ret = i2c_smbus_read_word_data(i2c_reg);
			printf("hex value: %04x\n", ret);
			sprintf(res, "%04x", ret);
			temp = res[0];
			res[0] = res[2];
			res[2] = temp;
			temp = res[1];
			res[1] = res[3];
			res[3] = temp;
			printf("big endian: %s\n", res);
		}
		printf("decimal: %d\n", ret);
	}

	if (type == TYPE_ISL68127) {
		if (dump_I())
			goto end;
	}

	if (type == TYPE_MP2973) {
		if (dump_M())
			goto end;
	}
	
end:
	close(i2c_file);
	return 0;
}
