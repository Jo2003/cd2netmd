/**
 * Copyright (C) 2021 Jo2003 (olenka.joerg@gmail.com)
 * This file is part of cd2netmd
 *
 * cd2netmd is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * cd2netmd is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 */
#pragma once
#include <iconv.h>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>

//------------------------------------------------------------------------------
//! @brief      convert string
//!
//! @param[in]  cd    conversion enum
//! @param[in]  in    string to convert
//! @param      out   converted string
//!
//! @return     converted string on success; unconverted in error case
//------------------------------------------------------------------------------
std::string cddb_str_iconv(iconv_t cd, const char *in);

//------------------------------------------------------------------------------
//! @brief      replace umlaut with ae, ue, oe, ss
//!
//! @param[in]  in    string to parse
//!
//! @return     parsed string
//------------------------------------------------------------------------------
std::string deUmlaut(const std::string& in);
