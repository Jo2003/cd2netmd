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
#include "WinHttpWrapper.h"
#include "CAudioCD.h"
#include <iconv.h>

#define YOUR_CDROM_DRIVE 'F'

enum class NetMDCmds : uint8_t {
	UNKNOWN,
	ERASE_DISC,
	DISC_TITLE,
	WRITE_TRACK,
	WRITE_TRACK_LP2,
	WRITE_TRACK_LP4
};

struct STrackDescr 
{
	std::string mName;
	std::string mFile;
};

typedef std::vector<STrackDescr> TrackVector_t;

std::mutex mtxTracks;
TrackVector_t TracksDescr;
 
std::mutex m;
std::condition_variable cv;
std::string data;
bool ready = false;
bool complete = false;

std::string utf8ToLatin1(const std::string& t)
{
	size_t  inSz   = t.size() + 1;
	size_t  outSz  = inSz;
	char*   utf8   = new [inSz];
	char*   latin1 = new [outSz];
	iconv_t ic     = iconv_open("ISO-8859-1","UTF-8");

	strcpy(utf8, t.c_str());
	iconv(ic, &utf8, &inSz, &latin1, &outSz);

	std::string ret = latin1;

	delete [] utf8;
	delete [] latin1;

	return ret;
}

std::wstring StringToWString(const std::string &s)
{
    std::wstring ret(s.begin(), s.end());

    return ret;
}

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
		int         no = 0;

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
			std::cout << "Multiple entries found:" << std::endl;
			std::cout << "=======================" << std::endl;
			for(const auto& d : choices)
			{
				std::cout << no << ") (" << d.mQuery << ") " << utf8ToLatin1(d.mDescr) << std::endl;
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

std::string parseCddbResults(const std::string& input)
{
	std::string line, tok, ret;
	ssize_t pos = std::string::npos;
	char* endPos;
	int   code = 0; 
	int   startPos;
	
	std::istringstream iss(input);
	while(std::getline(iss, line))
	{
		code = std::strtol(line.c_str(), &endPos, 0);
		
		if (endPos != line.c_str())
		{
			if (code == 200)
			{
				startPos = 1;
			}
			else if (code == 211)
			{
				startPos = 0;
				continue;
			}
			else
			{
				startPos = -1;
			}
		}
		
		if (startPos > -1)
		{
			int count = 0;
			std::stringstream ss(line);
			
			while(std::getline(ss, tok, ' '))
			{
				if (count == startPos)
				{
					ret = tok + "+";
				}
				else if(count == (startPos + 1))
				{
					ret += tok;
				}
				else if (count > (startPos + 1))
				{
					break;
				}
				count ++;
			}
			break;
		}
	}
	
	return ret;
}

int parseCddbInfo(const std::string& input, std::vector<std::string>& info)
{
	std::string line, tok;
	ssize_t pos;
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

int toNetMD(NetMDCmds cmd, const std::string& file = "", const std::string& title = "")
{
	int err = 0;
	std::ostringstream cmdLine;
	cmdLine << "netmd-cli.exe -y ";
	
	switch(cmd)
	{
	case NetMDCmds::ERASE_DISC:
		cmdLine << "erase_disc";
		break;
	case NetMDCmds::DISC_TITLE:
		cmdLine << "title \"" << utf8ToLatin1(title) << "\"";
		break;
	case NetMDCmds::WRITE_TRACK:
		cmdLine << "send \"" << file << "\" \"" << utf8ToLatin1(title) << "\"";
		break;
	case NetMDCmds::WRITE_TRACK_LP2:
		cmdLine << "-d lp2 send \"" << file << "\" \"" << utf8ToLatin1(title) << "\"";
		break;
	case NetMDCmds::WRITE_TRACK_LP4:
		cmdLine << "-d lp4 send \"" << file << "\" \"" << utf8ToLatin1(title) << "\"";
		break;
	default:
		err = -1;
		break;
	}
	
	if (err == 0)
	{
		std::cout << "Running command: " << cmdLine.str() << std::endl;
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
	bool go = true;
	
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
			toNetMD(NetMDCmds::WRITE_TRACK, currJob.mFile, currJob.mName);
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

int main(int argc, const char* argv[])
{
	(void)argc;
	(void)argv;
	std::ostringstream oss;

	// Set console code page to UTF-8 so console known how to interpret string data
    // SetConsoleOutputCP(CP_UTF8);

    // Enable buffering to prevent VS from chopping up UTF-8 byte sequences
    // setvbuf(stdout, nullptr, _IOFBF, 1000);
	
	TCHAR tmpPath[MAX_PATH];
	GetTempPathA(MAX_PATH, tmpPath);
	
	std::vector<std::string> tracks;
	
    CAudioCD AudioCD;
    if ( ! AudioCD.Open( YOUR_CDROM_DRIVE ) )
    {
        printf( "Cannot open cd-drive!\n" );
        return 0;
    }

    ULONG TrackCount = AudioCD.GetTrackCount();
    printf( "Track-Count: %i\n", TrackCount );

    for ( ULONG i=0; i<TrackCount; i++ )
    {
        ULONG Time = AudioCD.GetTrackTime( i );
        printf( "Track %i: %i:%.2i;  %i bytes of size\n", i+1, Time/60, Time%60, AudioCD.GetTrackSize(i) );
    }
    
    AudioCD.printTOC();
    
    oss << "/~cddb/cddb.cgi?cmd=cddb+query+" << AudioCD.cddbQueryPart() << "&hello=me@you.org+localhost+MyRipper+0.0.1&proto=6";
    printf("CDDB ID: 0x%08x\n", AudioCD.cddbId());
    printf("CDDB Request: http://gnudb.gnudb.org%s\n",oss.str().c_str());
    
    WinHttpWrapper::HttpRequest req(L"gnudb.gnudb.org", 443, true);
    WinHttpWrapper::HttpResponse resp;
    
    if (req.Get(StringToWString(oss.str()), L"Content-Type: text/plain; charset=utf-8", resp))
    {
    	oss.clear();
    	oss.str("");
    	oss << "/~cddb/cddb.cgi?cmd=cddb+read+" << parseCddbResultsEx(resp.text) << "&hello=me@you.org+localhost+MyRipper+0.0.1&proto=6";
    	printf("CDDB Data: http://gnudb.gnudb.org%s\n",oss.str().c_str());
	
		resp.Reset();
		
	
		if (req.Get(StringToWString(oss.str()), L"Content-Type: text/plain; charset=utf-8", resp))
    	{
    		parseCddbInfo(resp.text, tracks);
    		
    		for (const auto& t : tracks)
    		{
    			std::cout << utf8ToLatin1(t) << std::endl;
			}
    	}
	}
	
	if (!tracks.empty())
	{
		toNetMD(NetMDCmds::ERASE_DISC);
		toNetMD(NetMDCmds::DISC_TITLE, "", tracks.at(0));
		
		char fname[MAX_PATH];
		
		std::thread NetMd(tfunc_mdwrite);
		
		for (UINT i = 0; i < TrackCount; i++)
		{
			GetTempFileNameA(tmpPath, "c2n", 0, fname);

			std::cout << "Extracting Audio track " << i+1 << " to " << fname << std::endl;
			AudioCD.ExtractTrack(i, fname);
			
			mtxTracks.lock();
			TracksDescr.push_back({tracks.at(i+1), fname});
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
