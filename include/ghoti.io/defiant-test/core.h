/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Defiant Test.
 *
 * Ghoti.io Defiant Test is free software: you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public License
 * version 3 as published by the Free Software Foundation.
 *
 * Ghoti.io Defiant Test is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file core.h
 * @brief Result codes for the harness.
 * @stability free
 *
 * The vocabulary matches the port's, name for name. Numeric values are not
 * a contract: a caller maps with a switch, it does not cast.
 */

#ifndef GHOTI_IO_GDEFTEST_CORE_H
#define GHOTI_IO_GDEFTEST_CORE_H

#include <ghoti.io/defiant-test/macros.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Result of a memory-backend call.
 *
 * Same names as the port, including ::GDEFTEST_ERR_BUSY, so a backend and
 * the port can be talked about with one vocabulary.
 */
typedef enum GDEFTEST_Result {
  GDEFTEST_OK = 0,          ///< Succeeded.
  GDEFTEST_ERR_IO,          ///< A stream or sink reported an error.
  GDEFTEST_ERR_FORMAT,      ///< Well formed input that is not this format.
  GDEFTEST_ERR_UNSUPPORTED, ///< Unknown format name, or a newer version.
  GDEFTEST_ERR_LIMIT,       ///< A limit was exceeded. Nothing is truncated.
  GDEFTEST_ERR_CORRUPT,     ///< This format, but the bytes are wrong.
  GDEFTEST_ERR_OOM,         ///< The allocator returned NULL.
  GDEFTEST_ERR_INVALID,     ///< A caller argument is wrong, or a missing row.
  GDEFTEST_ERR_INTERNAL,    ///< A library invariant failed.
  GDEFTEST_ERR_BUSY,        ///< The tenant's writer is busy and this is not a fiber.
  GDEFTEST_RESULT_COUNT     ///< Not a result. Closes the string table.
} GDEFTEST_Result;

/**
 * @brief A static description of a result. Never NULL.
 *
 * @param result The code, including out-of-range values.
 * @return A string the caller does not free.
 */
GDEFTEST_API const char * gdeftest_result_string(GDEFTEST_Result result);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GDEFTEST_CORE_H */
