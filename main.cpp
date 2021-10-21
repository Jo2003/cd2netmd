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

std::wstring StringToWString(const std::string &s)
{
    std::wstring ret(s.begin(), s.end());

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
					ret = tok + "/";
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
    
    printf("CDDB ID: 0x%08x\n", AudioCD.cddbId());
    printf("CDDB Request: %s\n", AudioCD.cddbRequest("http://gnudb.gnudb.org", "me@you.org").c_str());
    
    WinHttpWrapper::HttpRequest req(L"gnudb.gnudb.org", 443, true);
    WinHttpWrapper::HttpResponse resp;
    // const char* contentType = "Content-Type: text/plain";
    
    if (req.Get(StringToWString(AudioCD.cddbRequest("", "me@you.org")), L"Content-Type: text/plain", resp))
    {
    	printf("%s\n", resp.text.c_str());
    	
    	req.setup(L"gnudb.org", 443, true);
    	std::ostringstream oss;
    	oss << "/gnudb/" << parseCddbResults(resp.text);
	
		resp.Reset();
		
		std::cout << "Resquest CDDB entry: " << oss.str() << std::endl;
		
		if (req.Get(StringToWString(oss.str()), L"Content-Type: text/plain", resp))
    	{
    		std::cout << resp.text << std::endl;
    		parseCddbInfo(resp.text, tracks);
    		
    		for (const auto& t : tracks)
    		{
    			std::cout << t << std::endl;
			}
    	}
	}
	
	if (!tracks.empty())
	{
		toNetMD(NetMDCmds::ERASE_DISC);
		toNetMD(NetMDCmds::DISC_TITLE, "", tracks.at(0));
		std::ostringstream oss;
		
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
