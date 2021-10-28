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

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <ostream>
#include <string>
#include <iostream>
#include <iomanip>
#include <mutex>
#include <thread>
#include <vector>
#include <condition_variable>
#include <windows.h>
#include <tchar.h>
#include <io.h>
#include "WinHttpWrapper.h"
#include "CAudioCD.h"
#include "Flags.hh"
#include "CPipeStream.hpp"

/// tool version
static constexpr const char* C2N_VERSION = "v0.2.8";

/// tool chain path
static constexpr const char* TOOLCHAIN_PATH = "toolchain/";

/// Sony WAVE format
static const uint32_t WAVE_FORMAT_SONY_SCX = 624;

/// atrac3 header size in bytes
static const uint32_t ATRAC3_HEADER_SIZE = 96;

/// do verbose output if enabled
#define VERBOSE(...) if (g_bVerbose) __VA_ARGS__

/// supported NetMD commands
enum class NetMDCmds : uint8_t {
    UNKNOWN,            ///< unknown command
    ERASE_DISC,         ///< erase MD 
    DISC_TITLE,         ///< write disc title
    WRITE_TRACK,        ///< write track in SP
    WRITE_TRACK_LP2,    ///< write tracl in lp2
    WRITE_TRACK_LP4     ///< write track in lp4
};

/// store track file name and title
struct STrackDescr 
{
    std::string mName;  ///< track title
    std::string mFile;  ///< file name
};

/// define track vector type
typedef std::vector<STrackDescr> TrackVector_t;


/// NetMD transfer thread
std::mutex trf_mtxTracks;       ///< synchronize access to track description vector
TrackVector_t trf_TracksDescr;  ///< track description vector
 
std::mutex trf_m;               ///< mutex for NetMD write thread synchronization
std::condition_variable trf_cv; ///< condition variable for NetMD write thread synchronization
bool trf_ready = false;         ///< synchronization helper
bool trf_complete = false;      ///< synchronization helper

/// external encoder thread
std::mutex xenc_mtxTracks;       ///< synchronize access to track description vector
TrackVector_t xenc_TracksDescr;  ///< track description vector
 
std::mutex xenc_m;               ///< mutex for NetMD write thread synchronization
std::condition_variable xenc_cv; ///< condition variable for NetMD write thread synchronization
bool xenc_ready = false;         ///< synchronization helper
bool xenc_complete = false;      ///< synchronization helper

/// cmd line parameters
bool        g_bVerbose;     ///< do verbose output if set
bool        g_bHelp;        ///< print help if set
bool        g_bNoMdDelete;  ///< don't delte MD before writing if set
bool        g_bNoCDDBLookup;///< don't use CDDB lookup
char        g_cDrive;       ///< drive letter of CD drive
std::string g_sEncoding;    ///< NetMD encoding
std::string g_sXEncoding;   ///< NetMD external encoding

/// stdout handle for piping of external tools' output
HANDLE g_hNetMDCli_stdout_wr = INVALID_HANDLE_VALUE;
HANDLE g_hNetMDCli_stdout_rd = INVALID_HANDLE_VALUE;
HANDLE g_hAtracEnc_stdout_wr = INVALID_HANDLE_VALUE;
HANDLE g_hAtracEnc_stdout_rd = INVALID_HANDLE_VALUE;
HANDLE g_hCDRip_stdout_wr    = INVALID_HANDLE_VALUE;
HANDLE g_hCDRip_stdout_rd    = INVALID_HANDLE_VALUE;

/// run flag
BOOL g_bRuns;

/// status line helper
int g_iNoTracks = 0;
int g_iRipTrack = 0;
int g_iEncTrack = 0;
int g_iTrfTrack = 0;

//------------------------------------------------------------------------------
//! @brief      Starts an external tool.
//!
//! @param[in]  cmdLine   The command line
//! @param[in]  hdStdOut  The standard out handle (optional)
//!
//! @return     0 -> ok; else -> error
//------------------------------------------------------------------------------
int startExternalTool(const std::string cmdLine, HANDLE hdStdOut = INVALID_HANDLE_VALUE)
{
    int iRet = 0;
    VERBOSE(std::cout << "Running command: " << cmdLine << std::endl);
    char* pCmd = new char[cmdLine.size() + 1];

    strcpy(pCmd, cmdLine.c_str());
    
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory( &si, sizeof(si) );
    si.cb = sizeof(si);
    ZeroMemory( &pi, sizeof(pi) );

    if (hdStdOut != INVALID_HANDLE_VALUE)
    {
        si.hStdError  = hdStdOut;
        si.hStdOutput = hdStdOut;
        si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
        si.dwFlags   |= STARTF_USESTDHANDLES;
    }

    // Start the child process. 
    if( !CreateProcess( NULL,   // No module name (use command line)
        pCmd,           // Command line
        NULL,           // Process handle not inheritable
        NULL,           // Thread handle not inheritable
        TRUE,           // Set handle inheritance to TRUE
        0,              // No creation flags
        NULL,           // Use parent's environment block
        NULL,           // Use parent's starting directory 
        &si,            // Pointer to STARTUPINFO structure
        &pi )           // Pointer to PROCESS_INFORMATION structure
    ) 
    {
        std::cerr << "CreateProcess failed (" << GetLastError() << ")" << std::endl;
        iRet = -1;
    }

    // Wait until child process exits.
    WaitForSingleObject( pi.hProcess, INFINITE );

    if (iRet == 0)
    {
        // get return code
        DWORD retCode = 0;
        if (GetExitCodeProcess(pi.hProcess, &retCode))
        {
            iRet = static_cast<int>(retCode);
        }

        if ((iRet == 0) && (hdStdOut != INVALID_HANDLE_VALUE))
        {
            // fake 100%
            WriteFile(hdStdOut, " 100% \n", 7, nullptr, nullptr);
        }
    }

    // Close process and thread handles. 
    CloseHandle( pi.hProcess );
    CloseHandle( pi.hThread );
    
    delete [] pCmd;

    return iRet;
}

//------------------------------------------------------------------------------
//! @brief      convert std::string to std::wstring
//!
//! @param[in]  s     string to convert
//!
//! @return     converted string
//------------------------------------------------------------------------------
std::wstring StringToWString(const std::string &s)
{
    std::wstring ret(s.begin(), s.end());

    return ret;
}

//------------------------------------------------------------------------------
//! @brief      parse CDDB query result line
//!
//! @param[in]  line   The line
//! @param      descr  The description
//!
//! @return     genre+cddbid as string
//------------------------------------------------------------------------------
std::string parseResultLine(const std::string& line, std::string& descr)
{
    std::ostringstream oss;
    size_t last = 0; 
    size_t next = 0; 
    int    no   = 0;

    while ((next = line.find_first_of(" \t", last)) != std::string::npos)
    {
        if (no == 0)
        {
            oss << line.substr(last, next - last) << "+";
        }
        else if (no == 1)
        {
            oss << line.substr(last, next - last);
        }

        last = next + 1;

        if (no == 1)
        {
            descr = line.substr(last);

            // remove \r
            if ((next = descr.rfind("\r")) != descr.npos)
            {
                descr.erase(next, 1);
            }
            break;
        }
        no++;
    }
    return oss.str();
}

//------------------------------------------------------------------------------
//! @brief      parse complete CDDB query result
//!
//! @param[in]  input  CDDB query response
//!
//! @return     genre+cddbid as string
//------------------------------------------------------------------------------
std::string parseCddbResultsEx(const std::string& input)
{
    char* endPos;
    int   code = std::strtol(input.c_str(), &endPos, 0);
    std::string ret, descr;

    if (endPos != input.c_str())
    {
        struct SDisc 
        {
            std::string mQuery;
            std::string mDescr;
        };
        std::vector<SDisc> choices;
        std::istringstream iss(input);
        std::string line;
        size_t      no = 0;

        while(std::getline(iss, line))
        {
            switch(code)
            {
            // single exact match
            case 200:
                ret = parseResultLine(line.substr(line.find_first_of(" \t") + 1), descr);
                break;
            // multiple exact matches
            case 210:
            // [fallthrough] multiple inexact matches
            case 211:
                if (line[0] == '.')
                {
                    break;
                }

                if (no++ > 0)
                {
                    ret = parseResultLine(line, descr);
                    choices.push_back({ret, descr});
                }
                break;
            // anything we wont handle
            default:
                break;
            }
        }

        if (choices.size())
        {
            no = 1;
            std::cout << std::endl 
                      << "=======================" << std::endl;
            std::cout << "Multiple entries found:" << std::endl;
            std::cout << "=======================" << std::endl;
            for(const auto& d : choices)
            {
                std::cout << std::setw(2) << std::right << no << std::left << ") (" << d.mQuery << ") " << d.mDescr << std::endl;
                no++;
            }
            std::cout << "Please choose the entry number to use: ";
            std::cin >> no;

            if ((no > 0) && (no <= choices.size()))
            {
                ret = choices.at(no - 1).mQuery;
            }
            std::cout << std::endl;
        }
    }

    return ret;
}

//------------------------------------------------------------------------------
//! @brief      parse CDDB data response
//!
//! @param[in]  input  CDDB data response
//! @param[out] info   disc title vector
//!
//! @return     0 -> ok; -1 -> error
//------------------------------------------------------------------------------
int parseCddbInfo(const std::string& input, std::vector<std::string>& info)
{
    std::string line, tok;
    size_t pos;
    std::istringstream iss(input);
    while(std::getline(iss, line))
    {
        if ((line.find("DTITLE") == 0) || (line.find("TTITLE") == 0))
        {
            tok.clear();

            if ((pos = line.find("=")) != line.npos)
            {
                tok = line.substr(pos + 1);
            }
            
            if ((pos = tok.find("/")) != tok.npos)
            {
                tok.replace(pos, 1, "-");
            }
            
            // remove \r
            if ((pos = tok.rfind("\r")) != tok.npos)
            {
                tok.erase(pos, 1);
            }
            
            if (tok.size() > 0)
            {
                info.push_back(tok);
            }
        }
    }
    
    return info.empty() ? -1 : 0;
}

//------------------------------------------------------------------------------
//! @brief      make Atrac3 wave header to transfer pre-encoded data
//!
//! @param      fWave   The opened wave file
//! @param[in]  mode    The mode
//! @param[in]  dataSz  The data size
//!
//! @return     0 -> ok; -1 -> error
//------------------------------------------------------------------------------
int atrac3WaveHeader(FILE* fWave, NetMDCmds mode, uint32_t dataSz)
{
    int ret = -1;

    if ((fWave != nullptr) 
        && ((mode == NetMDCmds::WRITE_TRACK_LP2) || (mode == NetMDCmds::WRITE_TRACK_LP4))
        && (dataSz > 92)) // 1x lp4 frame size
    {
        // heavily inspired by atrac3tool and completed through
        // reverse engineering of ffmpeg output ...
        char dstFormatBuf[0x20];
        WAVEFORMATEX *pDstFormat   = (WAVEFORMATEX *)dstFormatBuf;
        pDstFormat->wFormatTag     = WAVE_FORMAT_SONY_SCX;
        pDstFormat->nChannels      = 2;
        pDstFormat->nSamplesPerSec = 44100;
        if (mode == NetMDCmds::WRITE_TRACK_LP2)
        {
            pDstFormat->nAvgBytesPerSec = 16537;
            pDstFormat->nBlockAlign     = 0x180;
            memcpy(&dstFormatBuf[0x12], "\x01\x00\x44\xAC\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00", 0xE);
        }
        else if (mode == NetMDCmds::WRITE_TRACK_LP4)
        {
            pDstFormat->nAvgBytesPerSec = 8268;
            pDstFormat->nBlockAlign     = 0xc0;
            memcpy(&dstFormatBuf[0x12], "\x01\x00\x44\xAC\x00\x00\x01\x00\x01\x00\x01\x00\x00\x00", 0xE);
        }
        
        pDstFormat->wBitsPerSample = 0;
        pDstFormat->cbSize         = 0xE;

        uint32_t i = 0xC + 8 + 0x20 + 8 + dataSz - 8;

        fwrite("RIFF", 1, 4, fWave);
        fwrite(&i, 4, 1, fWave);
        fwrite("WAVE", 1, 4, fWave);

        fwrite("fmt ", 1, 4, fWave);
        i = 0x20;
        fwrite(&i, 4, 1, fWave);
        fwrite(dstFormatBuf, 1, 0x20, fWave);

        fwrite("data", 1, 4, fWave);
        i = dataSz;
        fwrite(&i, 4, 1, fWave);

        ret = 0;
    }

    return ret;
}

//------------------------------------------------------------------------------
//! @brief      do extern atrac3 encode using atracdenc
//!
//! @param[in]  file  The file name to encode
//!
//! @return     0 -> ok; else -> error
//------------------------------------------------------------------------------
int externAtrac3Encode(const std::string& file)
{
    int err = 0;
    std::string atracFile = file + ".aea";
    std::ostringstream cmdLine;
    cmdLine << TOOLCHAIN_PATH << "atracdenc.exe ";

    std::string enc = g_sXEncoding;

    // encoding to lower case
    std::transform(enc.begin(), enc.end(), enc.begin(),
        [](unsigned char c){ return std::tolower(c); });

    NetMDCmds mode = NetMDCmds::UNKNOWN;

    if (enc == "lp2")
    {
        cmdLine << "-e atrac3 --bitrate=128 ";
        mode = NetMDCmds::WRITE_TRACK_LP2;
    }
    else if (enc == "lp4")
    {
        cmdLine << "-e atrac3 --bitrate=64 ";
        mode = NetMDCmds::WRITE_TRACK_LP4;
    }
    else
    {
        err = -1;
    }

    if (err == 0)
    {
        cmdLine << " -i \"" << file << "\" -o \"" << atracFile << "\"";
        if ((err = startExternalTool(cmdLine.str(), g_hAtracEnc_stdout_wr)) == 0)
        {
            // open atrac file for size check
            HANDLE hAtrac = CreateFileA(atracFile.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

            if (hAtrac != INVALID_HANDLE_VALUE)
            {
                // get file size
                DWORD sz = GetFileSize(hAtrac, nullptr) - ATRAC3_HEADER_SIZE;

                // overwrite original wave file
                FILE *fWave = fopen(file.c_str(), "wb");
        
                if (fWave != nullptr)
                {
                    // create wave header
                    atrac3WaveHeader(fWave, mode, sz);
                    
                    unsigned char buff[16'384];
                    DWORD written = 0, read = 0, copied = 0;

                    // drop atrac 3 header
                    ReadFile(hAtrac, buff, ATRAC3_HEADER_SIZE, &read, NULL);

                    // copy atrac file to wave file
                    do
                    {
                        // read 16k at once
                        ReadFile(hAtrac, buff, 16'384, &read, NULL);

                        written = 0;

                        while(written < read)
                        {
                            written += fwrite(&buff[written], 1, read - written, fWave);
                        }
                        copied += written;
                    }
                    while(copied < sz);

                    VERBOSE(std::cout << "Wrap Atrac3 file ... done!" << std::endl);
                    fclose(fWave);
                }

                CloseHandle(hAtrac);
                if (!g_bVerbose) _unlink(atracFile.c_str());
            }
        }
        else
        {
            std::cerr << "Error running '" << cmdLine.str() << "'!" << std::endl;
        }
    }
    return err;
}

//------------------------------------------------------------------------------
//! @brief      run NetMD command through external program (go-netmd-cli)
//!
//! @param[in]  cmd    The command
//! @param[in]  file   The file
//! @param[in]  title  The title
//!
//! @return     0 -> ok; -1 -> error
//------------------------------------------------------------------------------
int toNetMD(NetMDCmds cmd, const std::string& file = "", const std::string& title = "")
{
    int err = 0;
    std::ostringstream cmdLine;
    cmdLine << TOOLCHAIN_PATH << "netmd-cli.exe -y ";

    if (g_bVerbose)
    {
        cmdLine << "-v ";
    }
    
    switch(cmd)
    {
    case NetMDCmds::ERASE_DISC:
        cmdLine << "erase_disc";
        break;
    case NetMDCmds::DISC_TITLE:
        cmdLine << "title \"" << title << "\"";
        break;
    case NetMDCmds::WRITE_TRACK:
        cmdLine << "send \"" << file << "\" \"" << title << "\"";
        break;
    case NetMDCmds::WRITE_TRACK_LP2:
        cmdLine << "-d lp2 send \"" << file << "\" \"" << title << "\"";
        break;
    case NetMDCmds::WRITE_TRACK_LP4:
        cmdLine << "-d lp4 send \"" << file << "\" \"" << title << "\"";
        break;
    default:
        err = -1;
        break;
    }
    
    if (err == 0)
    {
        if ((err = startExternalTool(cmdLine.str(), g_hNetMDCli_stdout_wr)) != 0)
        {
            std::cerr << "Error running '" << cmdLine.str() << "'!" << std::endl;
        }
    }
    
    return err;
}

//------------------------------------------------------------------------------
//! @brief      thread function for netmd transfer
//!
//! @return     0
//------------------------------------------------------------------------------
int tfunc_mdwrite()
{
    STrackDescr currJob;
    bool        go     = true;
    NetMDCmds   wrtCmd = NetMDCmds::WRITE_TRACK;

    if (g_sXEncoding == "no")
    {
        std::string enc = g_sEncoding;

        // encoding to lower case
        std::transform(enc.begin(), enc.end(), enc.begin(),
            [](unsigned char c){ return std::tolower(c); });

        if (enc == "lp2")
        {
            wrtCmd = NetMDCmds::WRITE_TRACK_LP2;
        }
        else if (enc == "lp4")
        {
            wrtCmd = NetMDCmds::WRITE_TRACK_LP4;
        }
    }
    
    do
    {
        trf_mtxTracks.lock();
        if (trf_TracksDescr.size() > 0)
        {
            currJob = trf_TracksDescr[0];
            trf_TracksDescr.erase(trf_TracksDescr.begin());
        }
        else
        {
            currJob = {"", ""};
            if (trf_complete)
            {
                go = false;
            }
        }
        trf_mtxTracks.unlock();
        
        if (!currJob.mFile.empty())
        {
            g_iTrfTrack ++;
            toNetMD(wrtCmd, currJob.mFile, currJob.mName);
            if (!g_bVerbose) _unlink(currJob.mFile.c_str());
        }
        else if (go)
        {
            std::unique_lock<std::mutex> lk(trf_m);
            trf_cv.wait(lk, []{return trf_ready;});
            trf_ready = false;
        }
    }
    while(go);
    
    return 0;
}

//------------------------------------------------------------------------------
//! @brief      thread function for external encoder
//!
//! @return     0
//------------------------------------------------------------------------------
int tfunc_xencode()
{
    STrackDescr currJob;
    bool        go = true;

    do
    {
        xenc_mtxTracks.lock();
        if (xenc_TracksDescr.size() > 0)
        {
            currJob = xenc_TracksDescr[0];
            xenc_TracksDescr.erase(xenc_TracksDescr.begin());
        }
        else
        {
            currJob = {"", ""};
            if (xenc_complete)
            {
                go = false;
            }
        }
        xenc_mtxTracks.unlock();
        
        if (!currJob.mFile.empty())
        {
            if (g_sXEncoding != "no")
            {
                g_iEncTrack ++;
                externAtrac3Encode(currJob.mFile);
            }
            
            trf_mtxTracks.lock();
            trf_TracksDescr.push_back(currJob);
            trf_mtxTracks.unlock();

            // notify md write thread
            {
                std::lock_guard<std::mutex> lk(trf_m);
                trf_ready = true;
            }
            trf_cv.notify_one();
        }
        else if (go)
        {
            std::unique_lock<std::mutex> lk(xenc_m);
            xenc_cv.wait(lk, []{return xenc_ready;});
            xenc_ready = false;
        }
    }
    while(go);

    trf_complete = true;
    {
        std::lock_guard<std::mutex> lk(trf_m);
        trf_ready = true;
    }
    trf_cv.notify_one();
    
    return 0;
}

//------------------------------------------------------------------------------
//! @brief      extract percent value from token
//!
//! @param[in]  tok   token to parse
//!
//! @return     -1 -> no value found; > -1 -> percent value
//------------------------------------------------------------------------------
int extractPercent(const std::string& tok)
{
    std::size_t posPercent, posNoNumber;
    int percent = -1;

    if ((posPercent = tok.rfind('%')) != std::string::npos)
    {
        if ((posNoNumber = tok.find_last_not_of("0123456789", posPercent - 1)) != std::string::npos)
        {
            percent = std::stoi(tok.substr(posNoNumber + 1));
        }
        else
        {
            percent = std::stoi(tok.substr(0));
        }
    }

    return percent;
}

//------------------------------------------------------------------------------
//! @brief      Makes a status bar (stream formatting sucks!).
//!
//! @param[in]  rip   The rip %
//! @param[in]  enc   The encode %
//! @param[in]  trf   The transfer %
//------------------------------------------------------------------------------
void makeStatusBar(int rip, int enc, int trf)
{
    static uint32_t cycle = 0;
    const char spinner[] = {'|', '/', '-', '\\'};
    std::string srip, senc, strf;
    std::ostringstream oss;

    oss << std::setw(3) << g_iRipTrack << " / " << std::setw(3) 
        << std::left << g_iNoTracks << std::right << std::setw(4) 
        << rip << "%";
    srip = oss.str();

    oss.clear();
    oss.str("");

    oss << std::setw(3) << g_iEncTrack << " / " << std::setw(3) 
        << std::left << g_iNoTracks << std::right << std::setw(4) 
        << enc << "%";
    senc = oss.str();

    oss.clear();
    oss.str("");

    oss << std::setw(3) << g_iTrfTrack << " / " << std::setw(3) 
        << std::left << g_iNoTracks << std::right << std::setw(4) 
        << trf << "%";
    strf = oss.str();

    std::cout << "\r[ CD-Rip: " << srip << " ] ";

    if (g_sXEncoding != "no")
    {
        std::cout << "[ X-Encode: " << senc << " ] ";
    }

    std::cout << "[ MD Transfer: " << strf << " ] " << spinner[cycle] << std::flush;
    cycle = (++cycle > 3) ? 0 : cycle;
}

//------------------------------------------------------------------------------
//! @brief      parse processes stdout pipes for status update
//!
//! @return     0
//------------------------------------------------------------------------------
int tfunc_readPipes()
{
    char buffNetMD[4096];
    char buffAtracEnc[4096];
    char buffCDRip[4096];
    DWORD readNetMD    = 0;
    DWORD readAtracEnc = 0;
    DWORD readCDRip    = 0;
    std::string tok;
    int percent;
    
    int rip  = 0, enc  = 0, trf  = 0;
    int rip_ = 0, enc_ = 0, trf_ = 0;
    
    while(g_bRuns)
    {
        // nemdcli stdout
        if (ReadFile(g_hNetMDCli_stdout_rd, buffNetMD, 4095, &readNetMD, 0))
        {
            if (readNetMD > 0)
            {
                buffNetMD[readNetMD] = '\0';
                tok                  = buffNetMD;

                if ((percent = extractPercent(tok)) != -1)
                {
                    trf = percent;
                }
            }
        }

        // atracdenc stdout
        if (ReadFile(g_hAtracEnc_stdout_rd, buffAtracEnc, 4095, &readAtracEnc, 0))
        {
            if (readAtracEnc > 0)
            {
                buffAtracEnc[readAtracEnc] = '\0';
                tok                        = buffAtracEnc;
                if ((percent = extractPercent(tok)) != -1)
                {
                    enc = percent;
                }
            }
        }

        // piped rip progress (think of stdout)
        if (ReadFile(g_hCDRip_stdout_rd, buffCDRip, 4095, &readCDRip, 0))
        {
            if (readCDRip > 0)
            {
                buffCDRip[readCDRip] = '\0';
                tok                  = buffCDRip;
                if ((percent = extractPercent(tok)) != -1)
                {
                    rip = percent;
                }
            }
        }

        if ((rip != rip_) || (enc != enc_) || (trf != trf_))
        {
            makeStatusBar(rip, enc, trf);
            rip_ = rip;
            enc_ = enc;
            trf_ = trf;
        }

        Sleep(1);
    }
    return 0;
}

//------------------------------------------------------------------------------
//! @brief      Opens pipes.
//!
//! @return     0 -> ok; -1 -> error
//------------------------------------------------------------------------------
int openPipes()
{
    int ret = 0;
    SECURITY_ATTRIBUTES saAttr; 

    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
    saAttr.bInheritHandle = TRUE; 
    saAttr.lpSecurityDescriptor = NULL; 

    // Create a pipe for the child process's STDOUT. 
    DWORD dwWait = PIPE_NOWAIT; ///< non blocking on read side
    CreatePipe(&g_hNetMDCli_stdout_rd, &g_hNetMDCli_stdout_wr, &saAttr, 4096);
    SetHandleInformation(g_hNetMDCli_stdout_rd, HANDLE_FLAG_INHERIT, 0);
    SetNamedPipeHandleState(g_hNetMDCli_stdout_rd, &dwWait, nullptr, nullptr);

    CreatePipe(&g_hAtracEnc_stdout_rd, &g_hAtracEnc_stdout_wr, &saAttr, 4096);
    SetHandleInformation(g_hAtracEnc_stdout_rd, HANDLE_FLAG_INHERIT, 0);
    SetNamedPipeHandleState(g_hAtracEnc_stdout_rd, &dwWait, nullptr, nullptr);

    CreatePipe(&g_hCDRip_stdout_rd, &g_hCDRip_stdout_wr, &saAttr, 4096);
    SetHandleInformation(g_hCDRip_stdout_rd, HANDLE_FLAG_INHERIT, 0);
    SetNamedPipeHandleState(g_hCDRip_stdout_rd, &dwWait, nullptr, nullptr);

    if ((g_hNetMDCli_stdout_wr == INVALID_HANDLE_VALUE)
        || (g_hNetMDCli_stdout_rd == INVALID_HANDLE_VALUE)
        || (g_hAtracEnc_stdout_wr == INVALID_HANDLE_VALUE)
        || (g_hAtracEnc_stdout_rd == INVALID_HANDLE_VALUE)
        || (g_hCDRip_stdout_wr == INVALID_HANDLE_VALUE)
        || (g_hCDRip_stdout_rd == INVALID_HANDLE_VALUE))
    {
        ret = -1;
    } 

    return ret;
}

//------------------------------------------------------------------------------
//! @brief      Closes pipes.
//------------------------------------------------------------------------------
void closePipes()
{
    if (g_hNetMDCli_stdout_wr != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_hNetMDCli_stdout_wr);
    } 
    if (g_hNetMDCli_stdout_rd != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_hNetMDCli_stdout_rd);
    } 
    if (g_hAtracEnc_stdout_wr != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_hAtracEnc_stdout_wr);
    } 
    if (g_hAtracEnc_stdout_rd != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_hAtracEnc_stdout_rd);
    } 
    if (g_hCDRip_stdout_wr != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_hCDRip_stdout_wr);
    } 
    if (g_hCDRip_stdout_rd != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_hCDRip_stdout_rd);
    } 
}

//------------------------------------------------------------------------------
//! @brief      Prints an information.
//------------------------------------------------------------------------------
void printInfo()
{
    std::cout << std::endl
              << " cd2netmd Version " << C2N_VERSION << ", built at " << __DATE__ << std::endl
              << " Project site: https://github.com/Jo2003/cd2netmd" << std::endl
              // << " Support me through PayPal: coujo@coujo.de" << std::endl
              << " ------------------------------------------------" << std::endl << std::endl;
}

//------------------------------------------------------------------------------
//! @brief      program entry point
//!
//! @param[in]  argc  The count of arguments
//! @param      argv  The arguments array
//!
//! @return     0 -> ok; else -> error
//------------------------------------------------------------------------------
int main(int argc, char** argv)
{
    std::ostringstream oss;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    size_t columns = 80;

    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
    {
        columns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    }

    Flags parser(columns);
    parser.Bool(g_bVerbose     , 'v', "verbose"      , "Does verbose output.");
    parser.Bool(g_bHelp        , 'h', "help"         , "Prints help screen and exits program.");
    parser.Bool(g_bNoMdDelete  , 'a', "append"       , "Don't erase MD before writing, but append tracks instead. "
                                                       "MDs discs title will not be changed.");
    parser.Bool(g_bNoCDDBLookup, 'i', "ignore-cddb"  , "Ignore CDDB lookup errors. If no match in CDDB is found your "
                                                       "tracks on MD will be untitled.");
    parser.Var (g_cDrive       , 'd', "drive-letter" , '-'              , "Drive letter of CD drive to use (w/o colon). "
                                                                          "If not given first CD drive found will be used.");

    parser.Var (g_sEncoding    , 'e', "encode"       , std::string{"sp"}, "On-the-fly encoding mode on NetMD device while transfer. "
                                                                          "Default is 'sp'. Note: MDLP modi (lp2, lp4) are supported "
                                                                          "only on SHARP IM-DR4x0, Sony MDS-JB980, Sony MDS-JB780.");
    
    parser.Var (g_sXEncoding   , 'x', "ext-encode"   , std::string{"no"}, "External encoding before NetMD transfer. "
                                                                          "Default is 'no'. MDLP modi (lp2, lp4) are supported. "
                                                                          "Note: lp4 sounds horrible. Use it - if any - only for "
                                                                          "audio books! In case your NetMD device supports On-the-fly "
                                                                          "encoding, better use -e option instead!");

    printInfo();

    if (!parser.Parse(argc, argv)) 
    {
        parser.PrintHelp(argv[0]);
        return 1;
    } 
    else if (g_bHelp) 
    {
        parser.PrintHelp(argv[0]);
        return 0;
    }

    // recognize first cd drive if not given
    if (g_cDrive == '-')
    {
        uint32_t drives = GetLogicalDrives();
        char d = 'a';
        for (int i = 0; i < 32; i++)
        {
            if (drives & (1 << i))
            {
                // get type of drive
                oss.clear();
                oss.str("");
                oss << static_cast<char>(d + i) << ":\\";
                if (GetDriveTypeA(oss.str().c_str()) == DRIVE_CDROM)
                {
                    g_cDrive = static_cast<char>(d + i);
                    break;
                }
            }
        }

        if (g_cDrive == '-')
        {
            std::cerr << "Can't find a CD drive!" << std::endl;
            return -1;
        }
        oss.clear();
        oss.str("");
    }

    if (openPipes() != 0)
    {
        closePipes();
        std::cerr << "Can't open pipes!" << std::endl;
        return -1;
    }

    // ostream device for Audio CD cout piping ...
    CPipeStream ps(g_hCDRip_stdout_wr);

    // Set console code page to UTF-8 so console known how to interpret string data
    SetConsoleOutputCP(CP_UTF8);

    // Enable buffering to prevent VS from chopping up UTF-8 byte sequences
    setvbuf(stdout, nullptr, _IOFBF, 1000);
    
    TCHAR tmpPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpPath);
    
    std::vector<std::string> tracks;
    
    CAudioCD AudioCD('\0', ps);
    if ( ! AudioCD.Open( g_cDrive ) )
    {
        printf( "Cannot open cd-drive!\n" );
        return 0;
    }

    uint32_t TrackCount = AudioCD.GetTrackCount();
    std::cout << "Track-Count: " << TrackCount << std::endl;
    g_iNoTracks = TrackCount;

    for ( uint32_t i=0; i<TrackCount; i++ )
    {
        uint32_t Time = AudioCD.GetTrackTime( i );
        printf( "Track %u: %u:%.2u;  %u bytes\n", i+1, Time/60, Time%60, static_cast<uint32_t>(AudioCD.GetTrackSize(i)) );
    }
    
    oss << "/~cddb/cddb.cgi?cmd=cddb+query+" << AudioCD.cddbQueryPart() << "&hello=me@you.org+localhost+MyRipper+0.0.1&proto=6";
    printf("\nCDDB ID: 0x%08x\n\n", AudioCD.cddbId());
    VERBOSE(printf("CDDB Request: http://gnudb.gnudb.org%s\n",oss.str().c_str()));
    
    WinHttpWrapper::HttpRequest req(L"gnudb.gnudb.org", 443, true);
    WinHttpWrapper::HttpResponse resp;
    
    if (req.Get(StringToWString(oss.str()), L"Content-Type: text/plain; charset=utf-8", resp))
    {
        oss.clear();
        oss.str("");
        oss << "/~cddb/cddb.cgi?cmd=cddb+read+" << parseCddbResultsEx(resp.text) << "&hello=me@you.org+localhost+MyRipper+0.0.1&proto=6";
        VERBOSE(printf("CDDB Data: http://gnudb.gnudb.org%s\n",oss.str().c_str()));
    
        resp.Reset();
    
        if (req.Get(StringToWString(oss.str()), L"Content-Type: text/plain; charset=utf-8", resp))
        {
            parseCddbInfo(resp.text, tracks);
        }
    }
    
    if (!tracks.empty() || g_bNoCDDBLookup)
    {
        // file name buffer
        char fname[MAX_PATH];

        // thread loop should run
        g_bRuns = TRUE;

        // stdout parse thread
        std::thread PipeWatch(tfunc_readPipes);
        
        // external encoder thread
        std::thread XEnc(tfunc_xencode);
        
        // netmd transfer thread
        std::thread NetMd(tfunc_mdwrite);

        if (!g_bNoMdDelete)
        {
            toNetMD(NetMDCmds::ERASE_DISC);
            toNetMD(NetMDCmds::DISC_TITLE, "", tracks.empty() ? "" : tracks.at(0));
            WriteFile(g_hNetMDCli_stdout_wr, " 0% \n", 5, nullptr, nullptr);
        }
        
        for (UINT i = 0; i < TrackCount; i++)
        {
            g_iRipTrack = i + 1;
            GetTempFileNameA(tmpPath, "c2n", 0, fname);

            VERBOSE(std::cout << "Extracting Audio track " << i+1 << " to " << fname << std::endl);

            AudioCD.ExtractTrack(i, fname);
            
            xenc_mtxTracks.lock();
            xenc_TracksDescr.push_back({(tracks.empty() ? "" : tracks.at(i+1)), fname});
            xenc_mtxTracks.unlock();
            
            // notify external encoder thread
            {
                std::lock_guard<std::mutex> lk(xenc_m);
                xenc_ready = true;
            }
            xenc_cv.notify_one();
        }
        
        AudioCD.UnlockCD();
        AudioCD.EjectCD();
        
        xenc_complete = true;
        {
            std::lock_guard<std::mutex> lk(xenc_m);
            xenc_ready = true;
        }
        xenc_cv.notify_one();

        // wait for encoder thread
        XEnc.join();
        
        // wait for md writing ends
        NetMd.join();

        // stop thread loop
        g_bRuns = FALSE;

        // wait for stdout parser
        PipeWatch.join();
    }
    else
    {
        std::cerr << "Error: No CDDB entry found for this CD. "
                  << "To force copy this CD, use command line option '-i'!" << std::endl;
    }

    closePipes();

    return 0;
}
