#include <cctype>
#include <cstdio>
#include <string>
#include <iostream>
#include <sstream>
#include <mutex>
#include <thread>
#include <vector>
#include <condition_variable>
#include <windows.h>
#include <tchar.h>
#include <sys/types.h>
#include <algorithm>
#include "WinHttpWrapper.h"
#include "CAudioCD.h"
#include "Flags.hh"

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


/// global variables
std::mutex mtxTracks;       ///< synchronize access to track description vector
TrackVector_t TracksDescr;  ///< track description vector
 
std::mutex m;               ///< mutex for NetMD write thread synchronization
std::condition_variable cv; ///< condition variable for NetMD write thread synchronization
bool ready = false;         ///< synchronization helper
bool complete = false;      ///< synchronization helper

/// cmd line parameters
bool        g_bVerbose;     ///< do verbose output if set
bool        g_bHelp;        ///< print help if set
bool        g_bNoMdDelete;  ///< don't delte MD before writing if set
bool        g_bNoCDDBLookup;///< don't use CDDB lookup
char        g_cDrive;       ///< drive letter of CD drive
std::string g_sEncoding;    ///< NetMD encoding

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

    while ((next = line.find_first_of(" \n\r\t", last)) != std::string::npos)
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
                std::cout << no << ") (" << d.mQuery << ") " << d.mDescr << std::endl;
                no++;
            }
            std::cout << "Please choose the entry number to use: ";
            std::cin >> no;

            if ((no > 0) && (no <= choices.size()))
            {
                ret = choices.at(no - 1).mQuery;
            }
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
    cmdLine << "netmd-cli.exe -y ";

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
        VERBOSE(std::cout << "Running command: " << cmdLine.str() << std::endl);
        char* pCmd = new char[cmdLine.str().size() + 1];
        strcpy(pCmd, cmdLine.str().c_str());
        
        STARTUPINFO si;
        PROCESS_INFORMATION pi;
    
        ZeroMemory( &si, sizeof(si) );
        si.cb = sizeof(si);
        ZeroMemory( &pi, sizeof(pi) );

        // Start the child process. 
        if( !CreateProcess( NULL,   // No module name (use command line)
            pCmd,           // Command line
            NULL,           // Process handle not inheritable
            NULL,           // Thread handle not inheritable
            FALSE,          // Set handle inheritance to FALSE
            0,              // No creation flags
            NULL,           // Use parent's environment block
            NULL,           // Use parent's starting directory 
            &si,            // Pointer to STARTUPINFO structure
            &pi )           // Pointer to PROCESS_INFORMATION structure
        ) 
        {
            printf( "CreateProcess failed (%d).\n", GetLastError() );
            err = -2;
        }
    
        // Wait until child process exits.
        WaitForSingleObject( pi.hProcess, INFINITE );
    
        // Close process and thread handles. 
        CloseHandle( pi.hProcess );
        CloseHandle( pi.hThread );
        
        delete [] pCmd;
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
    std::string enc    = g_sEncoding;

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
    
    do
    {
        mtxTracks.lock();
        if (TracksDescr.size() > 0)
        {
            currJob = TracksDescr[0];
            TracksDescr.erase(TracksDescr.begin());
        }
        else
        {
            currJob = {"", ""};
            if (complete)
            {
                go = false;
            }
        }
        mtxTracks.unlock();
        
        if (!currJob.mFile.empty())
        {
            toNetMD(wrtCmd, currJob.mFile, currJob.mName);
            _unlink(currJob.mFile.c_str());
        }
        else if (go)
        {
            std::unique_lock<std::mutex> lk(m);
            cv.wait(lk, []{return ready;});
            ready = false;
        }
    }
    while(go);
    
    return 0;
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
    Flags parser;
    parser.Bool(g_bVerbose     , 'v', "verbose"  , "Do verbose output.");
    parser.Bool(g_bHelp        , 'h', "help"     , "Print help screen and exits program.");
    parser.Bool(g_bNoMdDelete  , 'n', "no-delete", "Do not erase MD before writing. In that case also disc title isn't changed.");
    parser.Bool(g_bNoCDDBLookup, 'c', "no-cddb"  , "Ignore CDDB lookup errors.");
    parser.Var (g_cDrive       , 'd', "drive"    , '-'              , "Drive letter of CD drive to use (w/o colon). If not given first drive found will be used.");
    parser.Var (g_sEncoding    , 'e', "encode"   , std::string{"sp"}, "Encoding for NetMD transfer. Default is 'sp'. MDLP modi (lp2, lp4) are only supported on SHARP IM-DR4x0, Sony MDS-JB980, Sony MDS-JB780");
    
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

    // Set console code page to UTF-8 so console known how to interpret string data
    SetConsoleOutputCP(CP_UTF8);

    // Enable buffering to prevent VS from chopping up UTF-8 byte sequences
    setvbuf(stdout, nullptr, _IOFBF, 1000);
    
    TCHAR tmpPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpPath);
    
    std::vector<std::string> tracks;
    
    CAudioCD AudioCD;
    if ( ! AudioCD.Open( g_cDrive ) )
    {
        printf( "Cannot open cd-drive!\n" );
        return 0;
    }

    ULONG TrackCount = AudioCD.GetTrackCount();
    printf( "Track-Count: %i\n", TrackCount );

    for ( ULONG i=0; i<TrackCount; i++ )
    {
        ULONG Time = AudioCD.GetTrackTime( i );
        printf( "Track %i: %i:%.2i;  %i bytes\n", i+1, Time/60, Time%60, AudioCD.GetTrackSize(i) );
    }
    
    oss << "/~cddb/cddb.cgi?cmd=cddb+query+" << AudioCD.cddbQueryPart() << "&hello=me@you.org+localhost+MyRipper+0.0.1&proto=6";
    printf("\nCDDB ID: 0x%08x\n", AudioCD.cddbId());
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
        if (!g_bNoMdDelete)
        {
            toNetMD(NetMDCmds::ERASE_DISC);
            toNetMD(NetMDCmds::DISC_TITLE, "", tracks.empty() ? "" : tracks.at(0));
        }
        
        char fname[MAX_PATH];
        
        std::thread NetMd(tfunc_mdwrite);
        
        for (UINT i = 0; i < TrackCount; i++)
        {
            GetTempFileNameA(tmpPath, "c2n", 0, fname);

            std::cout << "Extracting Audio track " << i+1 << " to " << fname << std::endl;
            AudioCD.ExtractTrack(i, fname);
            
            mtxTracks.lock();
            TracksDescr.push_back({(tracks.empty() ? "" : tracks.at(i+1)), fname});
            mtxTracks.unlock();
            
            // notify md write thread
            {
                std::lock_guard<std::mutex> lk(m);
                ready = true;
            }
            cv.notify_one();
        }
        
        AudioCD.EjectCD();
        
        complete = true;
        {
            std::lock_guard<std::mutex> lk(m);
            ready = true;
        }
        cv.notify_one();
        
        // wait for md writing ends
        NetMd.join();
    }

    return 0;
}
