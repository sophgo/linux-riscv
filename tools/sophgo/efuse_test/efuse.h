#ifndef _EFUSE_H_
#define _EFUSE_H_

typedef unsigned int            uint32_t;

uint32_t efuse_embedded_read(uint32_t address);
void efuse_embedded_write(uint32_t address, uint32_t val);

#endif
