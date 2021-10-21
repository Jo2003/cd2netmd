#include "AudioCD_Helpers.h"

ULONG AddressToSectors( UCHAR Addr[4] )
{
	ULONG Sectors = ( Addr[1] * (CD_BLOCKS_PER_SECOND*60) ) + ( Addr[2]*CD_BLOCKS_PER_SECOND) + Addr[3];
	return Sectors - 150;
}

int cddb_sum(int n)
{
	/* a number like 2344 becomes 2+3+4+4 (13) */
	int ret = 0;

	while (n > 0) {
		ret = ret + (n % 10);
		n = n / 10;
	}

	return ret;
}

//! return rip percent 
int logRipPercent(int count, int pos)
{
	int ret = 0;
	if (count > 0)
	{
		ret =  ((pos * 100) / count);
	}
	return ret;
}

CWaveFileHeader::CWaveFileHeader()
{
	ZeroMemory( this, sizeof(*this) );
}


CWaveFileHeader::CWaveFileHeader( ULONG SamplingRate, USHORT BitsPerSample, USHORT Channels, ULONG DataSize )
{
	Set( SamplingRate, BitsPerSample, Channels, DataSize );
}


void CWaveFileHeader::Set( ULONG SamplingRate, USHORT BitsPerSample, USHORT Channels, ULONG DataSize )
{
	// Set Riff-Chunk
	CopyMemory( m_Riff_ID, "RIFF", 4 );
	m_Riff_Size = DataSize + 44;
	CopyMemory( m_Riff_Type, "WAVE", 4 );

	// Set Fmt-Chunk
	CopyMemory( m_Fmt_ID, "fmt ", 4 );
	m_Fmt_Length = 16;
	m_Fmt_Format = WAVE_FORMAT_PCM;
	m_Fmt_Channels = Channels;
	m_Fmt_SamplingRate = SamplingRate;
	m_Fmt_BlockAlign = Channels*BitsPerSample/8;
	m_Fmt_DataRate = Channels*BitsPerSample*SamplingRate/8;
	m_Fmt_BitsPerSample = BitsPerSample;

	// Set Data-Chunk
	CopyMemory( m_Data_ID, "data", 4 );
	m_Data_DataSize = DataSize;
}
