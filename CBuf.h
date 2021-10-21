#pragma once

#include <windows.h>
#include <malloc.h>


template<class Cl> class CBuf
{
	public:
		CBuf()
		{
			m_pData = NULL;
			m_iSize = 0;
		}
		CBuf( ULONG Count )
		{
			m_pData = (Cl*)malloc( (m_iSize=Count)*sizeof(Cl) );
		}
		~CBuf()
		{
			Free();
		}

		Cl* Alloc( ULONG Count )
		{
			return m_pData = (Cl*) realloc( m_pData, (m_iSize=Count)*sizeof(Cl) );
		}

		void Free()
		{
			free( m_pData );;
			m_pData = NULL;
			m_iSize = 0;
		}

		ULONG Count()
		{
			return m_iSize;
		}

		ULONG Size()
		{
			return m_iSize*sizeof(Cl);
		}

		Cl* Ptr()
		{
			return m_pData;
		}

		operator Cl* ()
		{
			return m_pData;
		}

	private:
		Cl*		m_pData;
		ULONG	m_iSize;
};