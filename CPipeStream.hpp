#pragma once
#include <fileapi.h>
#include <iostream>
#include <sstream>
#include <windows.h>

//------------------------------------------------------------------------------
//! @brief      This class describes a pipe stream buffer.
//------------------------------------------------------------------------------
class CPipeStreamBuf : public std::stringbuf
{
	HANDLE mhWrite;
public:
    CPipeStreamBuf(HANDLE h) : mhWrite(h)
    {}

    virtual ~CPipeStreamBuf() 
    {
        if (pbase() != pptr()) 
        {
            putOutput();
        }
    }

    // When we sync the stream with the output. 
    // 1) Output the buffer
    // 2) Reset the buffer
    // 3) flush the actual output stream we are using.
    int sync() override 
    {
        putOutput();
        return 0;
    }
    
    void putOutput() 
    {
        // Called by destructor.
        // destructor can not call virtual methods.
        DWORD written = 0;
        WriteFile(mhWrite, str().c_str(), str().size(), &written, nullptr);
        str("");
    }
};

//------------------------------------------------------------------------------
//! @brief      This class describes a pipe stream.
//------------------------------------------------------------------------------
class CPipeStream : public std::ostream
{
    CPipeStreamBuf buffer;

public:
	CPipeStream() = delete;

    CPipeStream(HANDLE h) :std::ostream(&buffer), buffer(h)
    {
    }
};
