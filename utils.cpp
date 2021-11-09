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
#include "utils.h"
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

//------------------------------------------------------------------------------
//! @brief      convert string
//!
//! @param[in]  cd    conversion enum
//! @param[in]  in    string to convert
//! @param      out   converted string
//!
//! @return     converted string on success; unconverted in error case
//------------------------------------------------------------------------------
std::string cddb_str_iconv(iconv_t cd, const char *in)
{
    std::string ret = in;
    size_t inlen, outlen;
    int buflen, rc;
    int len;                    /* number of chars in buffer */
    char *buf;
    char *inbuf = strdup(in);
    char *inPtr = inbuf;

    if (inbuf != nullptr)
    {
        inlen = strlen(inbuf);
        buflen = 0;
        buf = NULL;
        do {
            outlen = inlen * 2;
            buflen += outlen;
            /* iconv() below changes the buf pointer:
             * - decrement to point at beginning of buffer before realloc
             * - re-increment to point at first free position after realloc
             */
            len = buflen - outlen;
            buf = (char*)realloc(buf - len, buflen) + len;
            if (buf == NULL) {
                /* XXX: report out of memory error */
                free(inbuf);
                return ret;
            }
            rc = iconv(cd, &inPtr, &inlen, &buf, &outlen);
            if ((rc == -1) && (errno != E2BIG)) {
                free(buf);
                free(inbuf);
                fprintf(stderr, "Error in character encoding!\n");
                fflush(stderr);
                return ret;       /* conversion failed */
            }
        } while (inlen != 0);
        len = buflen - outlen;
        buf -= len;                 /* reposition at begin of buffer */
        
        /* make a copy just big enough for the result */
        char *o = new char[len + 1]; 

        if (o != nullptr)
        {
            memcpy(o, buf, len);
            o[len] = '\0';
            ret = o;
            delete [] o;

            free(inbuf);

            return ret;
        }

        free(inbuf);
    }
    
    return ret;
}

//------------------------------------------------------------------------------
//! @brief      replace umlaut with ae, ue, oe, ss
//!
//! @param[in]  in    string to parse
//!
//! @return     parsed string
//------------------------------------------------------------------------------
std::string deUmlaut(const std::string& in)
{
    std::string ret = in;
    std::vector<std::string> search, replace;

    search  = {"Ä" , "ä" , "Ö" , "ö" , "Ü" , "ü" , "ß"  };
    replace = {"Ae", "ae", "Oe", "oe", "Ue", "ue", "ss" };

    size_t pos;

    for (size_t i = 0; i < search.size(); i++)
    {
        while ((pos = ret.find(search[i], pos + 2)) != std::string::npos)
        {
            ret.replace(pos, 2, replace[i]);
        }
    }

    return ret;
}
