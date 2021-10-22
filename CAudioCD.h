// Written by Michel Helms (MichelHelms@Web.de).
// Parts of code were taken from Idael Cardoso (http://www.codeproject.com/csharp/csharpripper.asp)
//   and from Larry Osterman (http://blogs.msdn.com/larryosterman/archive/2005/05.aspx).
// Finished at 26th of September in 2006
// Of course you are allowed to cut this lines off ;)



#pragma once
// #ifndef CAUDIOCD_INCLUDED
// #define CAUDIOCD_INCLUDED


#include <windows.h>
#include <vector>
#include <string>
#include "CBuf.h"
#include "AudioCD_Helpers.h"




// Structure to hold the basic information for a cd-track
struct CDTRACK
{
    ULONG Address;
    ULONG Length;
};




// This class helps you to read out audio-tracks from cd.
// It holds only basic functions, e.g. no progress-information
//   is integrated.

// Example for saving a track as a wav-file to your hdd:
// CAudioCD AudioCD;
// AudioCD.Open( 'F' );
// AudioCD.ExtractTrack( 7, "C:\\MyTrack.wav" );
class CAudioCD
{
    public:
        // CONSTRUCTOR / DESTRUCTOR

        // Initializes the class. If you specify a letter for
        //   'Drive', it acts like a call to "Open".
        CAudioCD( char Drive='\0' );

        // Destructs the class, acts like a call to "Close".
        ~CAudioCD();




        // OPEN / CLOSE ACCESS TO CD-DRIVE

        // Opens a handle to the drive, locks it and gets track-information
        BOOL Open( char Drive );

        // Returns whether the CD-drive has been opened
        BOOL IsOpened();

        // Unlocks the CD-Drive and shuts down the access.
        // Basically resets the class.
        void Close();




        // READ / GET TRACK-DATA

        // Returns the number of tracks avaiable.
        // 0xFFFFFFFF on failure (e.g. no "Open" called)
        ULONG GetTrackCount();

        // Returns the length in seconds of an audio-track.
        // 0xFFFFFFFF on failure (e.g. no "Open" called)
        ULONG GetTrackTime( ULONG Track );

        // Returns the size (in bytes) of an audio-track
        // 0xFFFFFFFF on failure (e.g. no "Open" called)
        ULONG GetTrackSize( ULONG Track );

        // Stores the audio-track-data into the specified buffer.
        // The buffer is allocated by the function.
        // Be careful: An audio-track of 4 minutes is about 40 MB of size!
        BOOL ReadTrack( ULONG Track, CBuf<char>* pBuf );

        // Saves down the given track to a file (Path).
        // That file will be in valid wav-format with the default
        //   cd-audio-attributes: 44100Hz, 16Bit, Stereo
        BOOL ExtractTrack( ULONG Track, LPCTSTR Path );




        // LOCK / UNLOCK CD-DRIVE

        // Locks the CD-drive, so the user should(!) not be able to be
        //   open the drive.
        BOOL LockCD();

        // Unlocks the CD-drive, so it can be ejected.
        BOOL UnlockCD();




        // GENERAL OPERATIONS

        // Gets the cd into you computer.
        BOOL InjectCD();

        // Returns whether the CD is inserted and ready to be read.
        // If you specify no drive-letter, the CD must have been opened by
        //   a call to "Open".
        BOOL IsCDReady( char Drive='\0' );

        // Ejects the cd (so the user can change the cd or whatever...)
        BOOL EjectCD();
        
        /**
           print table of contents
        */
        void printTOC();
        
        /**
            return cddb id for title search
        */
        UINT cddbId();

        /**
            create request to be used to obtain disc information
        */
        std::string cddbQueryPart();
        
    protected:
        HANDLE                  m_hCD;
        std::vector<CDTRACK>    m_aTracks;
        CDROM_TOC               m_TOC;
};




// #endif
