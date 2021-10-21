// Written by Michel Helms (MichelHelms@Web.de).
// Parts of code were taken from Idael Cardoso (http://www.codeproject.com/csharp/csharpripper.asp)
//   and from Larry Osterman (http://blogs.msdn.com/larryosterman/archive/2005/05.aspx).
// Finished at 26th of September in 2006
// Of course you are allowed to cut this lines off ;)




#include "CAudioCD.h"
#include "AudioCD_Helpers.h"
#include <iostream>
#include <iomanip>
#include <sstream>



// Constructor / Destructor
CAudioCD::CAudioCD( char Drive )
{
	m_hCD = NULL;
	if ( Drive != '\0' )
		Open( Drive );
}


CAudioCD::~CAudioCD()
{
	Close();
}




// Open / Close access
BOOL CAudioCD::Open( char Drive )
{
	Close();

	// Open drive-handle
	char Fn[] = { '\\', '\\', '.', '\\', Drive, ':', '\0' };
	if ( INVALID_HANDLE_VALUE == ( m_hCD = CreateFile( Fn, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL ) ) )
	{
		m_hCD = NULL;
		return FALSE;
	}

	// Lock drive
	if ( ! LockCD() )
	{
		UnlockCD();
		CloseHandle( m_hCD );
		m_hCD = NULL;
		return FALSE;
	}

	// Get track-table and add it to the intern array
	ULONG BytesRead;
	// CDROM_TOC Table;
	if ( 0 == DeviceIoControl( m_hCD, IOCTL_CDROM_READ_TOC, NULL, 0, &m_TOC, sizeof(m_TOC), &BytesRead, NULL ) )
	{
		UnlockCD();
		CloseHandle( m_hCD );
		m_hCD = NULL;
		return FALSE;
	}
	for ( ULONG i=m_TOC.FirstTrack-1; i<m_TOC.LastTrack; i++ )
	{
		CDTRACK NewTrack;
		NewTrack.Address = AddressToSectors( m_TOC.TrackData[i].Address );
		NewTrack.Length = AddressToSectors( m_TOC.TrackData[i+1].Address ) - NewTrack.Address;
		m_aTracks.push_back( NewTrack );
	}

	// Return if track-count > 0
	return m_aTracks.size() > 0;
}


BOOL CAudioCD::IsOpened()
{
	return m_hCD != NULL;
}


void CAudioCD::Close()
{
	UnlockCD();
	m_aTracks.clear();
	CloseHandle( m_hCD );
	m_hCD = NULL;
}




// Read / Get track-data
ULONG CAudioCD::GetTrackCount()
{
	if ( m_hCD == NULL )
		return 0xFFFFFFFF;
	return m_aTracks.size();
}


ULONG CAudioCD::GetTrackTime( ULONG Track )
{
	if ( m_hCD == NULL )
		return 0xFFFFFFFF;
	if ( Track >= m_aTracks.size() )
		return 0xFFFFFFFF;

	CDTRACK& Tr = m_aTracks.at(Track);
	return Tr.Length / 75;
}


ULONG CAudioCD::GetTrackSize( ULONG Track )
{
	if ( m_hCD == NULL )
		return 0xFFFFFFFF;
	if ( Track >= m_aTracks.size() )
		return 0xFFFFFFFF;

	CDTRACK& Tr = m_aTracks.at(Track);
	return Tr.Length * RAW_SECTOR_SIZE;
}


BOOL CAudioCD::ReadTrack( ULONG TrackNr, CBuf<char>* pBuf )
{
	if ( m_hCD == NULL )
		return FALSE;

	if ( TrackNr >= m_aTracks.size() )
		return FALSE;
	CDTRACK& Track = m_aTracks.at(TrackNr);

	pBuf->Alloc( Track.Length*RAW_SECTOR_SIZE );

	RAW_READ_INFO Info;
	Info.TrackMode = CDDA;
	Info.SectorCount = SECTORS_AT_READ;

	ULONG i=0;
	
	for ( i=0; i<Track.Length/SECTORS_AT_READ; i++ )
	{
		Info.DiskOffset.QuadPart = (Track.Address + i*SECTORS_AT_READ) * CD_SECTOR_SIZE;
		ULONG Dummy;
		if ( 0 == DeviceIoControl( m_hCD, IOCTL_CDROM_RAW_READ, &Info, sizeof(Info), pBuf->Ptr()+i*SECTORS_AT_READ*RAW_SECTOR_SIZE, SECTORS_AT_READ*RAW_SECTOR_SIZE, &Dummy, NULL ) )
		{
			pBuf->Free();
			return FALSE;
		}
	}

	Info.SectorCount = Track.Length % SECTORS_AT_READ;
	Info.DiskOffset.QuadPart = (Track.Address + i*SECTORS_AT_READ) * CD_SECTOR_SIZE;
	ULONG Dummy;
	if ( 0 == DeviceIoControl( m_hCD, IOCTL_CDROM_RAW_READ, &Info, sizeof(Info), pBuf->Ptr()+i*SECTORS_AT_READ*RAW_SECTOR_SIZE, SECTORS_AT_READ*RAW_SECTOR_SIZE, &Dummy, NULL ) )
	{
		pBuf->Free();
		return FALSE;
	}

	return TRUE;
}


BOOL CAudioCD::ExtractTrack( ULONG TrackNr, LPCTSTR Path )
{
	BOOL ret = TRUE;
	if ( m_hCD == NULL )
		return FALSE;

	ULONG Dummy;

	if ( TrackNr >= m_aTracks.size() )
		return FALSE;
	CDTRACK& Track = m_aTracks.at(TrackNr);

	// HANDLE hFile = CreateFile( Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
	
	HANDLE hFile = CreateFileA( Path, (GENERIC_READ | GENERIC_WRITE), FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL );
	if ( hFile == INVALID_HANDLE_VALUE )
		return FALSE;

	CWaveFileHeader WaveFileHeader( 44100, 16, 2, Track.Length*RAW_SECTOR_SIZE );
	WriteFile( hFile, &WaveFileHeader, sizeof(WaveFileHeader), &Dummy, NULL );

	CBuf<char> Buf( SECTORS_AT_READ * RAW_SECTOR_SIZE );

	RAW_READ_INFO Info;
	Info.TrackMode = CDDA;
	Info.SectorCount = SECTORS_AT_READ;
	
	ULONG i=0;
	int percent = 0, perlast = -1;
	
	for ( i=0; i<Track.Length/SECTORS_AT_READ; i++ )
	{
		percent = logRipPercent(Track.Length/SECTORS_AT_READ, i);
		
		if (percent != perlast)
		{
			perlast = percent;
			std::cout << percent << "% of track #" << TrackNr + 1 << " ripped!" << std::endl;
		}
		
		Info.DiskOffset.QuadPart = (Track.Address + i*SECTORS_AT_READ) * CD_SECTOR_SIZE;
		if ( DeviceIoControl( m_hCD, IOCTL_CDROM_RAW_READ, &Info, sizeof(Info), Buf, SECTORS_AT_READ*RAW_SECTOR_SIZE, &Dummy, NULL ) )
		{
			WriteFile( hFile, Buf, Buf.Size(), &Dummy, NULL );
		}
		else
		{
			std::cerr << "Error while reading CD Audio: " << GetLastError() << std::endl;
			ret = FALSE;
			break;
		}
	}

	if (ret)
	{
		Info.SectorCount = Track.Length % SECTORS_AT_READ;
		
		// not yet all read?
		if (Info.SectorCount)
		{
			Info.DiskOffset.QuadPart = (Track.Address + i*SECTORS_AT_READ) * CD_SECTOR_SIZE;
			if (DeviceIoControl( m_hCD, IOCTL_CDROM_RAW_READ, &Info, sizeof(Info), Buf, Info.SectorCount*RAW_SECTOR_SIZE, &Dummy, NULL ) )
			{
				std::cout << 100 << "% of track #" << TrackNr + 1 << " ripped!" << std::endl;
				WriteFile( hFile, Buf, Info.SectorCount*RAW_SECTOR_SIZE, &Dummy, NULL );
			}
			else
			{
				std::cerr << "Error while reading CD Audio: " << GetLastError() << std::endl;
				ret = FALSE;
			}
		}
	}
	
	FlushFileBuffers(hFile);

	return CloseHandle( hFile );
}




// Lock / Unlock CD-Rom Drive
BOOL CAudioCD::LockCD()
{
	if ( m_hCD == NULL )
		return FALSE;
	ULONG Dummy;
	PREVENT_MEDIA_REMOVAL pmr = { TRUE };
	return 0 != DeviceIoControl( m_hCD, IOCTL_STORAGE_MEDIA_REMOVAL, &pmr, sizeof(pmr), NULL, 0, &Dummy, NULL );
}


BOOL CAudioCD::UnlockCD()
{
	if ( m_hCD == NULL )
		return FALSE;
	ULONG Dummy;
	PREVENT_MEDIA_REMOVAL pmr = { FALSE };
	return 0 != DeviceIoControl( m_hCD, IOCTL_STORAGE_MEDIA_REMOVAL, &pmr, sizeof(pmr), NULL, 0, &Dummy, NULL );
}




// General operations
BOOL CAudioCD::InjectCD()
{
	if ( m_hCD == NULL )
		return FALSE;
	ULONG Dummy;
	return 0 != DeviceIoControl( m_hCD, IOCTL_STORAGE_LOAD_MEDIA, NULL, 0, NULL, 0, &Dummy, NULL );
}


BOOL CAudioCD::IsCDReady( char Drive )
{
	HANDLE hDrive;
	if ( Drive != '\0' )
	{
		// Open drive-handle if a drive is specified
		char Fn[] = { '\\', '\\', '.', '\\', Drive, ':', '\0' };
		if ( INVALID_HANDLE_VALUE == ( hDrive = CreateFile( Fn, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL ) ) )
			return FALSE;
	}
	else
	{
		// Otherwise, take our open handle
		if ( m_hCD == NULL )
			return FALSE;
		hDrive = m_hCD;
	}

	ULONG Dummy;
	BOOL Success = DeviceIoControl( hDrive, IOCTL_STORAGE_CHECK_VERIFY2, NULL, 0, NULL, 0, &Dummy, NULL );

	if ( m_hCD != hDrive )
		CloseHandle( hDrive );

	return Success;
}


BOOL CAudioCD::EjectCD()
{
	if ( m_hCD == NULL )
		return FALSE;
	ULONG Dummy;
	return 0 != DeviceIoControl( m_hCD, IOCTL_STORAGE_EJECT_MEDIA, NULL, 0, NULL, 0, &Dummy, NULL );
}

void CAudioCD::printTOC()
{
	int count = 0;
	for (const auto& a : m_aTracks)
	{
		std::cout << ++count << ". address: " << a.Address << std::endl;
	}
}

UINT CAudioCD::cddbId()
{
	ULONG checksum = 0;
	UINT  ttime    = 0;
	
	std::size_t count = m_aTracks.size();
	
	for (const auto& a : m_aTracks)
	{
		checksum += cddb_sum((a.Address + 150) / CD_BLOCKS_PER_SECOND);
	}
	
	ttime = ((m_aTracks.at(count - 1).Address + m_aTracks.at(count - 1).Length + 150) / CD_BLOCKS_PER_SECOND)
		- ((m_aTracks.at(0).Address + 150) / CD_BLOCKS_PER_SECOND);
	
	return (checksum & 0xff) << 24 | ttime << 8 | count;
}

/**
	create request to be used to obtain disc information
*/
std::string CAudioCD::cddbQueryPart()
{
	uint32_t cddbid = cddbId();
	std::ostringstream oss;
	oss << std::hex << cddbid << std::dec << "+" << (cddbid & 0xff);
	
	for (const auto& a : m_aTracks)
	{
		oss << "+" << (a.Address + 150);
	}
	
	oss << "+" << ((cddbid >> 8) & 0xFFFF);
	
	return oss.str();
}
