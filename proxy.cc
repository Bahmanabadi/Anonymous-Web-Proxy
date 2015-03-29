/* Proxy.cpp is very simple program to implement the HTTP proxy function.
// To make it short and clear , some error tolerant codes are omitted.
// Written by Saeed Bahmanabadi   
// bahmanabadi.s@gmail.com
*/ 2015-3-18
#include "stdafx.h"
#include "Proxy.h"
#include <winsock2.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// The one and only application object

#define HTTP  "http://"
#define FTP   "ftp://"
#define PROXYPORT    5060    //Proxy Listen Port
#define BUFSIZE   10240      //Buffer size


CWinApp theApp;

using namespace std;

UINT ProxyToServer(LPVOID pParam);
UINT UserToProxyThread(void *pParam);

struct SocketPair{
	SOCKET  user_proxy;      //socket : local machine to proxy server
	SOCKET  proxy_server;    //socket : proxy sever to remote server
	BOOL    IsUser_ProxyClosed; // status of local machine to proxy server
	BOOL    IsProxy_ServerClosed; // status of proxy server to remote server
};


struct ProxyParam{
	char Address[256];    // address of remote server
	HANDLE User_SvrOK;    // status of setup connection between proxy server and remote server
	SocketPair *pPair;    // pointer to socket pair
	int     Port;         // port which will be used to connect to remote server
	};                   //This struct is used to exchange information between threads.

SOCKET    gListen_Socket;   

int StartServer()
{
     WSADATA wsaData;
	 sockaddr_in local;
	 SOCKET listen_socket;

	if(::WSAStartup(0x202,&wsaData)!=0)
		{printf("\nError in Startup session.\n");WSACleanup();return -1;};

	local.sin_family=AF_INET;
	local.sin_addr.s_addr=INADDR_ANY;
	local.sin_port=htons(PROXYPORT);

	listen_socket=socket(AF_INET,SOCK_STREAM,0);
	if(listen_socket==INVALID_SOCKET)
		{printf("\nError in New a Socket.");WSACleanup();return -2;}

	if(::bind(listen_socket,(sockaddr *)&local,sizeof(local))!=0)
		{printf("\n Error in Binding socket.");	WSACleanup();return -3;	};

	if(::listen(listen_socket,5)!=0)
		{printf("\n Error in Listen."); WSACleanup(); return -4;}
	gListen_Socket=listen_socket; 
	AfxBeginThread(UserToProxyThread,NULL);   //Start accept function
	return 1;
}

int CloseServer()
{
	closesocket(gListen_Socket);
	WSACleanup();
	return 1;
}

//Analisys the string, to find the remote address
int GetAddressAndPort( char * str, char *address, int * port)
{
	char buf[BUFSIZE], command[512], proto[128], *p;
	int j;
	sscanf(str,"%s%s%s",command,buf,proto);
	p=strstr(buf,HTTP);
	//HTTP
	if(p)
	{
		p+=strlen(HTTP);
		for(int i=0;i<strlen(p);i++)
			if( *(p+i)=='/') break;
		*(p+i)=0;
		strcpy(address,p);
		p=strstr(str,HTTP);
		for(int j=0;j<i+strlen(HTTP);j++)
			*(p+j)=' ';  //to remove the host name: GET http://www.njust.edu.cn/ HTTP1.1  ==> GET / HTTP1.1
		*port=80;      //default http port 
	}
	else
	{//FTP, Not supported, and following code can be omitted.
	 p=strstr(buf,FTP);
	 if(!p) return 0;
	 	p+=strlen(FTP);
		for(int i=0;i<strlen(p);i++)
			if( *(p+i)=='/') break;      //Get The Remote Host
		*(p+i)=0;
		for(j=0;j<strlen(p);j++)
			if(*(p+j)==':') 
				{*port=atoi(p+j+1);    //Get The Port
			     *(p+j)=0;
				}
			 else *port=21;		 			              

		strcpy(address,p);
		p=strstr(str,FTP);
		for(j=0;j<i+strlen(FTP);j++)
			*(p+j)=' ';		
	}
	return 1; 
}

// Setup chanel and read data from local , send data to remote
UINT UserToProxyThread(void *pParam)
{
	 char Buffer[BUFSIZE];
	 int  Len;
	 sockaddr_in from;
	 SOCKET msg_socket;
	 int fromlen,retval;
	 SocketPair SPair;
	 ProxyParam ProxyP;
	 CWinThread *pChildThread;
     fromlen=sizeof(from);
	 msg_socket=accept(gListen_Socket,(struct sockaddr*)&from,&fromlen);
 	 AfxBeginThread(UserToProxyThread,pParam); //Start another thread to listen.
	 if( msg_socket==INVALID_SOCKET)
		{ printf( "\nError  in accept "); return -5;}
	 
	//recieve the first line of client

	 SPair.IsUser_ProxyClosed=FALSE;
	 SPair.IsProxy_ServerClosed=TRUE;
	 SPair.user_proxy=msg_socket;

	 retval=recv(SPair.user_proxy,Buffer,sizeof(Buffer),0);
	 
	 if(retval==SOCKET_ERROR)
			{ printf("\nError Recv"); 
		      if(SPair.IsUser_ProxyClosed==FALSE)
				{closesocket(SPair.user_proxy);
				 SPair.IsUser_ProxyClosed=TRUE;
				}
			}
	 if(retval==0)
			{printf("Client Close connection\n");
		     if(SPair.IsUser_ProxyClosed==FALSE)
				{closesocket(SPair.user_proxy);
				 SPair.IsUser_ProxyClosed=TRUE;
				}
			}
	 Len=retval;
#ifdef _DEBUG

	 Buffer[Len]=0;
	 printf("\n Received %d bytes,data[%s]from client\n",retval,Buffer);
#endif
	 //
	 SPair.IsUser_ProxyClosed=FALSE;
	 SPair.IsProxy_ServerClosed=TRUE;
	 SPair.user_proxy=msg_socket;

	 ProxyP.pPair=&SPair;
	 ProxyP.User_SvrOK=CreateEvent(NULL,TRUE,FALSE,NULL);
	 
	 GetAddressAndPort( Buffer,ProxyP.Address,&ProxyP.Port);

	 pChildThread=AfxBeginThread(ProxyToServer,(LPVOID)&ProxyP);
	 ::WaitForSingleObject(ProxyP.User_SvrOK,60000);  //Wait for connection between proxy and remote server
	 ::CloseHandle(ProxyP.User_SvrOK);

  while(SPair.IsProxy_ServerClosed ==FALSE && SPair.IsUser_ProxyClosed==FALSE)
	 {	
	   retval=send(SPair.proxy_server,Buffer,Len,0);
	   if(retval==SOCKET_ERROR)
		{ printf("\n send() failed:error%d\n",WSAGetLastError());
	     if(SPair.IsProxy_ServerClosed==FALSE)
			{
				closesocket(SPair.proxy_server);
				SPair.IsProxy_ServerClosed=TRUE;
			}
		  continue;
		}
	    retval=recv(SPair.user_proxy,Buffer,sizeof(Buffer),0);
	 
		if(retval==SOCKET_ERROR)
			{ printf("\nError Recv"); 
		      if(SPair.IsUser_ProxyClosed==FALSE)
				{closesocket(SPair.user_proxy);
				 SPair.IsUser_ProxyClosed=TRUE;
				}
		      continue;
			}
		if(retval==0)
			{printf("Client Close connection\n");
		     if(SPair.IsUser_ProxyClosed==FALSE)
				{closesocket(SPair.user_proxy);
				 SPair.IsUser_ProxyClosed=TRUE;
				}
			 break;
			}
		Len=retval;
#ifdef _DEBUG
		Buffer[Len]=0;
	 printf("\n Received %d bytes,data[%s]from client\n",retval,Buffer);
#endif
	
  } //End While

     if(SPair.IsProxy_ServerClosed==FALSE)
			{
				closesocket(SPair.proxy_server);
				SPair.IsProxy_ServerClosed=TRUE;
			}
	 if(SPair.IsUser_ProxyClosed==FALSE)
				{closesocket(SPair.user_proxy);
				 SPair.IsUser_ProxyClosed=TRUE;
				}
	::WaitForSingleObject(pChildThread->m_hThread,20000);  //Should check the reture value
	return 0;
}

// Read data from remote and send data to local
UINT ProxyToServer(LPVOID pParam)
{
    ProxyParam * pPar=(ProxyParam*)pParam;
	char Buffer[BUFSIZE];
	char *server_name= "localhost";
	unsigned short port ;
	int retval,Len;
	unsigned int addr;
	int socket_type ;
	struct sockaddr_in server;
	struct hostent *hp;
	SOCKET  conn_socket;

	socket_type = SOCK_STREAM;
	server_name = pPar->Address;
	port = pPar->Port;

	if (isalpha(server_name[0])) {   /* server address is a name */
		hp = gethostbyname(server_name);
	}
	else  { /* Convert nnn.nnn address to a usable one */
		addr = inet_addr(server_name);
		hp = gethostbyaddr((char *)&addr,4,AF_INET);
	}
	if (hp == NULL ) {
		fprintf(stderr,"Client: Cannot resolve address [%s]: Error %d\n",
			server_name,WSAGetLastError());
		::SetEvent(pPar->User_SvrOK);	
		return 0;
	}

	//
	// Copy the resolved information into the sockaddr_in structure
	//
	memset(&server,0,sizeof(server));
	memcpy(&(server.sin_addr),hp->h_addr,hp->h_length);
	server.sin_family = hp->h_addrtype;
	server.sin_port = htons(port);

	conn_socket = socket(AF_INET,socket_type,0); /* Open a socket */
	if (conn_socket <0 ) {
		fprintf(stderr,"Client: Error Opening socket: Error %d\n",
			WSAGetLastError());
		pPar->pPair->IsProxy_ServerClosed=TRUE;
		::SetEvent(pPar->User_SvrOK);	
		return -1;
	}


#ifdef _DEBUG
	printf("Client connecting to: %s\n",hp->h_name);
#endif
	if (connect(conn_socket,(struct sockaddr*)&server,sizeof(server))
		== SOCKET_ERROR) {
		fprintf(stderr,"connect() failed: %d\n",WSAGetLastError());
		pPar->pPair->IsProxy_ServerClosed=TRUE;	
		::SetEvent(pPar->User_SvrOK);	
		return -1;
	}
	pPar->pPair->proxy_server=conn_socket;
	pPar->pPair->IsProxy_ServerClosed=FALSE;
    ::SetEvent(pPar->User_SvrOK);
	// cook up a string to send
	while(!pPar->pPair->IsProxy_ServerClosed &&!pPar->pPair->IsUser_ProxyClosed)
	{
		retval = recv(conn_socket,Buffer,sizeof (Buffer),0 );
		if (retval == SOCKET_ERROR ) {
			fprintf(stderr,"recv() failed: error %d\n",WSAGetLastError());
			closesocket(conn_socket);
			pPar->pPair->IsProxy_ServerClosed=TRUE;
			break;
		}
		Len=retval;
		if (retval == 0) {
			printf("Server closed connection\n");
			closesocket(conn_socket);
			pPar->pPair->IsProxy_ServerClosed=TRUE;
			break;
			}

		retval = send(pPar->pPair->user_proxy,Buffer,Len,0);
		if (retval == SOCKET_ERROR) {
			fprintf(stderr,"send() failed: error %d\n",WSAGetLastError());
			closesocket(pPar->pPair->user_proxy);
			pPar->pPair->IsUser_ProxyClosed=TRUE;
			break;						
		}
#ifdef _DEBUG	
		Buffer[Len]=0;		
		printf("Received %d bytes, data [%s] from server\n",retval,Buffer);
#endif
	}
	if(pPar->pPair->IsProxy_ServerClosed==FALSE)
			{
				closesocket(pPar->pPair->proxy_server);
				pPar->pPair->IsProxy_ServerClosed=TRUE;
			}
	 if(pPar->pPair->IsUser_ProxyClosed==FALSE)
				{closesocket(pPar->pPair->user_proxy);
				 pPar->pPair->IsUser_ProxyClosed=TRUE;
				}
	 return 1;
}



int _tmain(int argc, TCHAR* argv[], TCHAR* envp[])
{
	int nRetCode = 0;

	// initialize MFC and print and error on failure
	if (!AfxWinInit(::GetModuleHandle(NULL), NULL, ::GetCommandLine(), 0))
	{
		// TODO: change error code to suit your needs
		cerr << _T("Fatal Error: MFC initialization failed") << endl;
		nRetCode = 1;
	}
	else
	{
		// TODO: code your application's behavior here.
		StartServer();	
		while(1)	
			if(getchar()=='q') break;
		CloseServer();
	}

	return nRetCode;
}

