
#include "coreio.h"

#include <time.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <sstream>
#include <string>
#include <iomanip>
#include <cctype>

#define TRUE 1
#define FALSE 0


#include <string>
#include <sstream>

#if FEAT_HAS_COREIO_HTTP
	#if HOST_OS == OS_LINUX || HOST_OS == OS_DARWIN || HOST_OS == OS_PS4_BSD
		#include <sys/socket.h>
		#include <netinet/in.h>
#if HOST_OS == OS_PS4_BSD
		//#include <libnet/header/ip.h>
#else
		#include <netinet/ip.h>
#endif
		#include <netinet/tcp.h>
		#include <netdb.h>
		#include <unistd.h>
	#else
		#pragma comment (lib, "wsock32.lib")
	#endif
#endif

#if FEAT_HAS_COREIO_HTTP
wstring url_encode(const wstring &value) {
	wostringstream escaped;
	escaped.fill('0');
	escaped << hex;

	for (wstring::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
		wstring::value_type c = (*i);

		// Keep alphanumeric and other accepted characters intact
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c  == '/' || c =='%' ) {
			escaped << c;
			continue;
		}

		// Any other characters are percent-encoded
		escaped << '%' << setw(2) << int((unsigned char)c);
	}

	return escaped.str();
}

size_t HTTP_GET(wstring host, int port,wstring path, size_t offs, size_t len, void* pdata){
    wstring request;
    wstring response;

    struct sockaddr_in serveraddr;
    int sock;

    std::wstringstream request2;

	if (len) {
		request2 << L"GET " << path << L" HTTP/1.1"<<endl;
		request2 << L"User-Agent: reicastdc" << endl;
		//request2 << "" << endl;
		request2 << L"Host: " << host << endl;
		request2 << L"Accept: */*" << endl;
		request2 << L"Range: bytes=" << offs << L"-" << (offs + len-1) << endl;
		request2 << endl;
	}
	else {
		request2 << L"HEAD " << path << L" HTTP/1.1"<<endl;
		request2 << L"User-Agent: reicastdc" << endl;
		//request2 << "" << endl;
		request2 << L"Host: " << host << endl;
		request2 << endl;
	}

	request = request2.str();
    //init winsock

	static bool init = false;
	if (!init) {
#if HOST_OS == OS_WINDOWS
		static WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0)
			die(L"WSAStartup fail");
#endif
		init = true;
	}

    //open socket
    if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        return -1;

	
    //connect
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family      = AF_INET;
	serveraddr.sin_addr.s_addr = *(int*)gethostbyname( toString(host).c_str() )->h_addr_list[0];
    serveraddr.sin_port        = htons((unsigned short) port);
    if (connect(sock, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0)
        return -1;

#if HOST_OS == OS_WINDOWS
	BOOL v = TRUE;
#else
	int v = 1;
#endif

	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&v, sizeof(v));
    //send request
    if (send(sock, toString(request).c_str(), request.length(), 0) != request.length())
        return -1;

	/*
    //get response
    response = "";
    resp_leng= BUFFERSIZE;
    
	*/

	/*
		parse headers ..
	*/
	size_t content_length = 0;
	
	size_t rv = 0;

	for (;;) {
		wstringstream ss;
		for (;;) {
			char t;
			if (recv(sock, &t, 1, 0) <= 0)
				goto _cleanup;

			if (t != '\n'){
				ss << t;
				continue;
			}

			wstring ln = ss.str();

			if (ln.size() == 1)
				goto _data;
			wstring CL = L"Content-Length:";

			if (ln.substr(0, CL.size()) == CL) {
				swscanf(ln.substr(CL.size(), ln.npos).c_str(),L"%d", &content_length);
			}

			break;
		}
	}

_data:

	if (len == 0) {
		rv = content_length;
	}
	else {
		verify(len == content_length); //crash is a bit too harsh here perhaps?
		u8* ptr = (u8*)pdata;
		do
		{
			int rcv = recv(sock, (char*)ptr, len, 0);
			verify(rcv > 0 && len>= rcv);
			len -= rcv;
			ptr += rcv;
		}
		while (len >0);

		rv = content_length;
	}

	_cleanup:
    //disconnect
#if HOST_OS == OS_WINDOWS
    closesocket(sock);
#else
	close(sock);
#endif

	/*
    //cleanup
    WSACleanup();
	*/

    return  rv;
}
#endif

struct CORE_FILE {
	FILE* f;
	wstring path;
	size_t seek_ptr;

	wstring host;
	int port;
};

core_file* core_fopen(const wchar_t* filename)
{
	wstring p = filename;

	CORE_FILE* rv = new CORE_FILE();
	rv->f = 0;
	rv->path = p;
#if FEAT_HAS_COREIO_HTTP
	if (p.substr(0,7)==L"http://") {
		rv->host = p.substr(7,p.npos);
		rv->host = rv->host.substr(0, rv->host.find_first_of(L"/"));

		rv->path = url_encode(p.substr(p.find(L"/", 7), p.npos));
		
		rv->port = 80;
		size_t pos = rv->host.find_first_of(L":");
		if (pos != rv->host.npos) {
			wstring port = rv->host.substr(pos, rv->host.npos );
			rv->host = rv->host.substr(0, rv->host.find_first_of(L":"));
			swscanf(port.c_str(),L"%d",&rv->port);
		}
	} else
#endif	
  {
		rv->f = fopen(filename, "rb");

		if (!rv->f) {
			delete rv;
			return 0;
		}
	}

	core_fseek((core_file*)rv, 0, SEEK_SET);
	return (core_file*)rv;
}

size_t core_fseek(core_file* fc, size_t offs, size_t origin) {
	CORE_FILE* f = (CORE_FILE*)fc;
	
	if (origin == SEEK_SET)
		f->seek_ptr = offs;
	else if (origin == SEEK_CUR)
		f->seek_ptr += offs;
	else
		die(L"Invalid code path");

	if (f->f)
		fseek(f->f, f->seek_ptr, SEEK_SET);

	return 0;
}

size_t core_ftell(core_file* fc) {
	CORE_FILE* f = (CORE_FILE*)fc;
	return f->seek_ptr;
}
int core_fread(core_file* fc, void* buff, size_t len)
{
	CORE_FILE* f = (CORE_FILE*)fc;

	if (f->f) {
		fread(buff,1,len,f->f);
	} else {
	#if FEAT_HAS_COREIO_HTTP
		HTTP_GET(f->host, f->port, f->path, f->seek_ptr, len, buff);
	#endif
	}

	f->seek_ptr += len;

	return len;
}

int core_fclose(core_file* fc)
{
	CORE_FILE* f = (CORE_FILE*)fc;

	if (f->f) {
		fclose(f->f);
	}

	delete f;

	return 0;
}

size_t core_fsize(core_file* fc)
{
	CORE_FILE* f = (CORE_FILE*)fc;

	if (f->f) {
		size_t p=ftell(f->f);
		fseek(f->f,0,SEEK_END);
		size_t rv=ftell(f->f);
		fseek(f->f,p,SEEK_SET);
		return rv;
	}
	else {
	#if FEAT_HAS_COREIO_HTTP
		return HTTP_GET(f->host, f->port, f->path, 0, 0,0);
	#endif
	}
    return 0;
}
