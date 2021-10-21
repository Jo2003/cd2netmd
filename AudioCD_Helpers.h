#ifndef AUDIOCD_HELPERS_INCLUDED
#define AUDIOCD_HELPERS_INCLUDED


#include <windows.h>




#define RAW_SECTOR_SIZE			2352
#define CD_SECTOR_SIZE			2048
#define MAXIMUM_NUMBER_TRACKS	100
#define SECTORS_AT_READ			20
#define CD_BLOCKS_PER_SECOND	75
#define IOCTL_CDROM_RAW_READ	0x2403E
#define IOCTL_CDROM_READ_TOC	0x24000


// These structures are defined somewhere in the windows-api, but I did
//   not have the include-file.
typedef struct _TRACK_DATA
{
	UCHAR Reserved;
	UCHAR Control : 4;
	UCHAR Adr : 4;
	UCHAR TrackNumber;
	UCHAR Reserved1;
	UCHAR Address[4];
} TRACK_DATA;

typedef struct _CDROM_TOC
{
	UCHAR Length[2];
	UCHAR FirstTrack;
	UCHAR LastTrack;
	TRACK_DATA TrackData[MAXIMUM_NUMBER_TRACKS];
} CDROM_TOC;

typedef enum _TRACK_MODE_TYPE
{
	YellowMode2,
	XAForm2,
	CDDA
} TRACK_MODE_TYPE, *PTRACK_MODE_TYPE;

typedef struct __RAW_READ_INFO
{
	LARGE_INTEGER  DiskOffset;
	ULONG  SectorCount;
	TRACK_MODE_TYPE  TrackMode;
} RAW_READ_INFO, *PRAW_READ_INFO;



// Msf: Hours, Minutes, Seconds, Frames
ULONG AddressToSectors( UCHAR Addr[4] );

int cddb_sum(int n);

//! return rip percent 
int logRipPercent(int count, int pos);

// A tiny class that helps us creating the header of a wave-file.
class CWaveFileHeader
{
	public:
		CWaveFileHeader();
		CWaveFileHeader( ULONG SamplingRate, USHORT BitsPerSample, USHORT Channels, ULONG DataSize );
		void Set( ULONG SamplingRate, USHORT BitsPerSample, USHORT Channels, ULONG DataSize );

	private:
		UCHAR  m_Riff_ID[4];		//'RIFF'
		ULONG  m_Riff_Size;			// FileSize-8
		UCHAR  m_Riff_Type[4];		// 'WAVE'

		UCHAR  m_Fmt_ID[4];			// 'FMT '
		ULONG  m_Fmt_Length;		// 16
		USHORT m_Fmt_Format;		// 1 (=MS PCM)
		USHORT m_Fmt_Channels;		// 1 = mono, 2 = stereo
		ULONG  m_Fmt_SamplingRate;	// Abtastrate pro Sekunde (z.B. 44100)
		ULONG  m_Fmt_DataRate;		// SamplingRate * BlockAlign
		USHORT m_Fmt_BlockAlign;	// Channels * BitsPerSample / 8
		USHORT m_Fmt_BitsPerSample;	// 8 or 16

		UCHAR  m_Data_ID[4];		// 'data'
		ULONG  m_Data_DataSize;		// Size of the following data
};




#endif
