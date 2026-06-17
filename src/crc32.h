#ifndef _CRC32_H
#define _CRC32_H

#include <stdint.h>

static uint32_t crc32_update(uint32_t crc, const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (int j = 0; j < 8; ++j)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;
    }
    return crc;
}


uint32_t crc32(const void *data, size_t len)	
{
    uint32_t crc = 0xFFFFFFFF;
	
    crc = crc32_update(crc, data, len);

    return crc ^ 0xFFFFFFFF;
}

#endif
