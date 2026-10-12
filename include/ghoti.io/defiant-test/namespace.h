/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Defiant Test.
 *
 * Ghoti.io Defiant Test is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License version
 * 3 as published by the Free Software Foundation.
 *
 * Ghoti.io Defiant Test is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file namespace.h
 * @brief Maps every public name into this build's version namespace.
 */

#ifndef GHOTI_IO_GDEFTEST_NAMESPACE_H
#define GHOTI_IO_GDEFTEST_NAMESPACE_H

#include <ghoti.io/defiant-test/libver.h>

/// @cond HIDDEN_SYMBOLS

#define GDEFTEST_Allocator GHOTIIO_DEFTEST(GDEFTEST_Allocator)
#define GDEFTEST_Result GHOTIIO_DEFTEST(GDEFTEST_Result)

#define gdeftest_allocator_default GHOTIIO_DEFTEST(gdeftest_allocator_default)
#define gdeftest_lint GHOTIIO_DEFTEST(gdeftest_lint)
#define gdeftest_response_has_internal_id                                      \
  GHOTIIO_DEFTEST(gdeftest_response_has_internal_id)
#define gdeftest_result_string GHOTIIO_DEFTEST(gdeftest_result_string)

/// @endcond

#endif /* GHOTI_IO_GDEFTEST_NAMESPACE_H */
