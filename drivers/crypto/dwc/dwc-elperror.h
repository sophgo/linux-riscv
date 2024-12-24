/*
 * Copyright (c) 2015 Elliptic Technologies Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#pragma once

#include <linux/errno.h>
/*
 * Common error definitions.  Be sure to update pdu_error_code when changing
 * anything in this list.
 */

#define CRYPTO_OK                      (   0)
#define CRYPTO_FAILED                  (  -1)
#define CRYPTO_INPROGRESS              (  -2)
#define CRYPTO_INVALID_HANDLE          (  -3)
#define CRYPTO_INVALID_CONTEXT         (  -4)
#define CRYPTO_INVALID_SIZE            (  -5)
#define CRYPTO_NOT_INITIALIZED         (  -6)
#define CRYPTO_NO_MEM                  (  -7)
#define CRYPTO_INVALID_ALG             (  -8)
#define CRYPTO_INVALID_KEY_SIZE        (  -9)
#define CRYPTO_INVALID_ARGUMENT        ( -10)
#define CRYPTO_MODULE_DISABLED         ( -11)
#define CRYPTO_NOT_IMPLEMENTED         ( -12)
#define CRYPTO_INVALID_BLOCK_ALIGNMENT ( -13)
#define CRYPTO_INVALID_MODE            ( -14)
#define CRYPTO_INVALID_KEY             ( -15)
#define CRYPTO_AUTHENTICATION_FAILED   ( -16)
#define CRYPTO_INVALID_IV_SIZE         ( -17)
#define CRYPTO_MEMORY_ERROR            ( -18)
#define CRYPTO_LAST_ERROR              ( -19)
#define CRYPTO_HALTED                  ( -20)
#define CRYPTO_TIMEOUT                 ( -21)
#define CRYPTO_SRM_FAILED              ( -22)
#define CRYPTO_COMMON_ERROR_MAX        (-100)
#define CRYPTO_INVALID_ICV_KEY_SIZE    (-100)
#define CRYPTO_INVALID_PARAMETER_SIZE  (-101)
#define CRYPTO_SEQUENCE_OVERFLOW       (-102)
#define CRYPTO_DISABLED                (-103)
#define CRYPTO_INVALID_VERSION         (-104)
#define CRYPTO_FATAL                   (-105)
#define CRYPTO_INVALID_PAD             (-106)
#define CRYPTO_FIFO_FULL               (-107)
#define CRYPTO_INVALID_SEQUENCE        (-108)
#define CRYPTO_INVALID_FIRMWARE        (-109)
#define CRYPTO_NOT_FOUND               (-110)
#define CRYPTO_CMD_FIFO_INACTIVE       (-111)
#define CRYPTO_INVALID_PROTOCOL        (-112)
#define CRYPTO_REPLAY                  (-113)

/* Convert SDK error codes to corresponding kernel error codes. */
int dwc_elp_errorcode(int code);

