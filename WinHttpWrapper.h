// The MIT License (MIT)
// WinHTTP Wrapper 1.0
// Copyright (C) 2020, by Wong Shao Voon (shaovoon@yahoo.com)
//
// http://opensource.org/licenses/MIT


#pragma once

#include <string>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace WinHttpWrapper
{
    struct HttpResponse
    {
        HttpResponse() : statusCode(0) {}
        void Reset()
        {
            text = "";
            header = L"";
            statusCode = 0;
            error = L"";
        }

        std::string text;
        std::wstring header;
        DWORD statusCode;
        std::wstring error;
    };

    class HttpRequest
    {
    public:
        HttpRequest(
            const std::wstring& domain,
            int port,
            bool secure,
            const std::wstring& user_agent = L"WinHttpClient",
            const std::wstring& proxy_username = L"",
            const std::wstring& proxy_password = L"",
            const std::wstring& server_username = L"",
            const std::wstring& server_password = L"")
            : m_Domain(domain)
            , m_Port(port)
            , m_Secure(secure)
            , m_UserAgent(user_agent)
            , m_ProxyUsername(proxy_username)
            , m_ProxyPassword(proxy_password)
            , m_ServerUsername(server_username)
            , m_ServerPassword(server_password)
        {}
        
        void setup(const std::wstring& domain,
            int port,
            bool secure,
            const std::wstring& user_agent = L"WinHttpClient",
            const std::wstring& proxy_username = L"",
            const std::wstring& proxy_password = L"",
            const std::wstring& server_username = L"",
            const std::wstring& server_password = L"");
        
        bool Get(const std::wstring& rest_of_path,
            const std::wstring& requestHeader,
            HttpResponse& response);
        bool Post(const std::wstring& rest_of_path,
            const std::wstring& requestHeader,
            const std::string& body,
            HttpResponse& response);
        bool Put(const std::wstring& rest_of_path,
            const std::wstring& requestHeader,
            const std::string& body,
            HttpResponse& response);
        bool Delete(const std::wstring& rest_of_path,
            const std::wstring& requestHeader,
            const std::string& body,
            HttpResponse& response);

    private:
        // Request is wrapper around http()
        bool Request(
            const std::wstring& verb,
            const std::wstring& rest_of_path,
            const std::wstring& requestHeader,
            const std::string& body,
            HttpResponse& response);
        static bool http(
            const std::wstring& verb, const std::wstring& user_agent, const std::wstring& domain,
            const std::wstring& rest_of_path, int port, bool secure,
            const std::wstring& requestHeader, const std::string& body,
            std::string& text, std::wstring& responseHeader,
            DWORD& statusCode, std::wstring& error,
            const std::wstring& szProxyUsername, const std::wstring& szProxyPassword,
            const std::wstring& szServerUsername, const std::wstring& szServerPassword);

        static DWORD ChooseAuthScheme(DWORD dwSupportedSchemes);

        std::wstring m_Domain;
        int m_Port;
        bool m_Secure;
        std::wstring m_UserAgent;
        std::wstring m_ProxyUsername;
        std::wstring m_ProxyPassword;
        std::wstring m_ServerUsername;
        std::wstring m_ServerPassword;
    };

}
