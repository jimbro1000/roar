/** \file
 *
 * \brief CRC-16 functions.
 */

#ifndef XROAR_CRC16_H_
#define XROAR_CRC16_H_

#include "top-config.h"

#include <stdint.h>

/**
 * CRC-16-CCITT with bytes processed high bit first ("big-endian"), as used in
 * the WD279X FDC (polynomial 0x1021).  In the FDC, CRC is initialised to
 * 0xffff and NOT inverted before appending to the message.
 */

#define CRC16_CCITT_RESET (0xffff)

uint16_t crc16_ccitt_byte(uint16_t crc, uint8_t value);
uint16_t crc16_ccitt_block(uint16_t crc, const uint8_t *block, unsigned length);

#endif
