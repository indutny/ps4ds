#ifndef SRC_CRC32_H_
#define SRC_CRC32_H_

#include <stdint.h>

uint32_t crc32(uint32_t crc, const void *buf, int size);

#endif  // SRC_CRC32_H_
