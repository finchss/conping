#define ENABLE_ZENTIMER
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

#ifdef __unix
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif


#include <stdint.h>

#include <sys/time.h>


#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <signal.h>





#ifndef IP_PROTO_TCP
#define IP_PROTO_TCP     6
#endif // IP_PROTO_TCP
#include "zentimer.h"


int got_signal=0;

#ifdef _WIN32
void GetNetwork()
{

    WORD wVersionRequested;
    WSADATA wsaData;
    int err;

/* Use the MAKEWORD(lowbyte, highbyte) macro declared in Windef.h */
    wVersionRequested = MAKEWORD(2, 2);

    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) {
        /* Tell the user that we could not find a usable */
        /* Winsock DLL.                                  */
        printf("WSAStartup failed with error: %d\n", err);
        exit(0);

    }

/* Confirm that the WinSock DLL supports 2.2.*/
/* Note that if the DLL supports versions greater    */
/* than 2.2 in addition to 2.2, it will still return */
/* 2.2 in wVersion since that is the version we      */
/* requested.                                        */

    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
        /* Tell the user that we could not find a usable */
        /* WinSock DLL.                                  */
        printf("Could not find a usable version of Winsock.dll\n");
        WSACleanup();
        exit(0);
    }



/* The Winsock DLL is acceptable. Proceed to use it. */

/* Add network programming using Winsock here */

/* then call WSACleanup when done using the Winsock dll */


}


BOOL WINAPI ConsoleHandler(DWORD dwType)
{
    switch(dwType) {
    case CTRL_C_EVENT:
        printf("ctrl-c\n");
        got_signal=1;
        break;
    case CTRL_BREAK_EVENT:
        printf("break\n");
        got_signal=1;
        break;
    default:
        printf("Some other event\n");
    }
    return TRUE;
}


#endif // _Win32

void ErrorExit(int code, char *format, ...) {

	va_list args;
	va_start(args, format);
	vfprintf(stderr,format,args);
	exit(code);

}


int ping(char *host,char *port, int timeout,int seq) {

	int s;
	struct sockaddr_in addr;
	struct timeval tv;
	fd_set set;
	int err;
	ztimer_t zt;
	uint64_t elapsed;
	int ret=0;



	s=socket(PF_INET,SOCK_STREAM,IP_PROTO_TCP);
	if(s<0) {
		perror("socket");
		ErrorExit(1,"Cannot create TCP socket\n");
	}
	memset(&addr,0,sizeof(struct sockaddr_in));
	addr.sin_addr.s_addr=inet_addr(host);
	addr.sin_port=htons(atoi(port));
	addr.sin_family=AF_INET;
	#ifdef _WIN32
	 unsigned long mode=1;
	 ioctlsocket(s, FIONBIO, &mode);
	#else // _WIN32
	fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
	#endif // _WIN32

	ZenTimerStart(&zt);
	if (connect(s,(struct sockaddr *)&addr,sizeof(struct sockaddr_in))==0)
		goto connected;

	tv.tv_usec=0;
	tv.tv_sec=timeout;

	FD_ZERO(&set);
	FD_SET(s,&set);

	err=select(s+1,NULL,&set,NULL,&tv);
	if(err==1) {
		err=connect(s,(struct sockaddr *)&addr,sizeof(struct sockaddr_in));
		#ifdef __WIN32
		if(WSAGetLastError()==WSAEISCONN)
		#else
		if((err==0) || (errno==56))
		#endif
			goto connected;
		else
			goto refused;
	} else if(errno==EINTR) {
		ret = - 1 ;
		goto cleanup;
	} else
		goto timeoutl;

timeoutl:
	printf("Request timed out.\n");
	goto cleanup;

refused:
	ret=1;
	ZenTimerStop(&zt);
	ZenTimerElapsed(&zt,&elapsed);
	printf("R from %s:%s: tcp_seq=%d time=%u.%.3ums\n",host,port,seq,(unsigned int)elapsed/1000,(unsigned int)elapsed%1000);
	goto cleanup;

connected:
	ret=1;
	ZenTimerStop(&zt);
	ZenTimerElapsed(&zt,&elapsed);
	printf("SA from %s:%s: tcp_seq=%d time=%u.%.3ums\n",host,port,seq,(unsigned int)elapsed/1000,(unsigned int)elapsed%1000);
	goto cleanup;

cleanup:
	#ifdef _WIN32
	closesocket(s);
	#else  // _WIN32
	shutdown(s,SHUT_RDWR);
	close(s);
	#endif
	return (ret);


}

void hdl (int sig) {
	sig++;
	got_signal = 1;
}

void encode_ip(unsigned char *dest, char *string) {
	struct hostent *dnsres;
	if ((dnsres = gethostbyname(string)) == NULL || dnsres->h_addr_list == NULL || dnsres->h_addr_list[0] == NULL) {
		fprintf(stderr,"Unable to resolve %s.\n",string);
		exit(1);
	}
	memcpy(dest,dnsres->h_addr_list[0],4);
	return;
}

int main(int argc, char **argv) {
	int err;
	int timeout=2;
	char hoststr[256]= {0};
	char hostip[256]= {0};
	#ifdef __unix
	sigset_t mask;
	sigset_t orig_mask;
	struct sigaction act;
	#endif // _unix
	int count=0;
	int PacketsSent=0;
	int PacketsReceived=0;
	char portstr[10]= {0};
	struct in_addr in;
	int i;

	#ifdef _WIN32
	if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)ConsoleHandler,TRUE)) {
        fprintf(stderr, "Unable to install handler!\n");
        return EXIT_FAILURE;
    }
	GetNetwork();
	#endif // _Win32
	strcpy(portstr,"80");
	#ifdef __unix
	memset (&act, 0, sizeof(act));
	act.sa_handler = hdl;
	if (sigaction(SIGINT, &act, 0)) {
		perror ("sigaction");
		return 1;
	}


	sigemptyset (&mask);
	sigaddset (&mask, SIGINT);
	#endif // __unix

	for(i=1; i<argc; i++) {
		if(strcmp(argv[i],"-c")==0) {
			count=atoi(argv[i+1]);
			i++;
			continue;
		}
		if(strcmp(argv[i],"-p")==0) {
			strcpy(portstr,argv[i+1]);
			i++;
			continue;
		}
		if(strcmp(argv[i],"-t")==0) {
			timeout=atoi(argv[i+1]);
			i++;
			continue;
		}
		strcpy(hoststr,argv[i]);
		if(inet_addr(hoststr)!=INADDR_NONE)
			strcpy(hostip,argv[i]);
		else {
			encode_ip((unsigned char *)&in,hoststr);
			strcpy(hostip,inet_ntoa(in));
		}


	}

	if(strlen(hoststr)==0)
		ErrorExit(1,"Please enter a hostname/ip\n");

	while(1) {

		if(got_signal)
			break;
		/*

		*/
		err=ping(hostip,portstr,timeout,PacketsSent);
		#ifdef __unix
		if (sigprocmask(SIG_BLOCK, &mask, &orig_mask) < 0) {
			perror ("sigprocmask");
			return 1;
		}
		#endif // __unxi

		if(err>0)
			PacketsReceived++;
		if(err>=0)
			PacketsSent++;

		#ifdef __unix
		if (sigprocmask(SIG_SETMASK, &orig_mask, NULL) < 0) {
			perror ("sigprocmask");
			return 1;
		}
		#endif // __unix

		if(count>0)
			if(PacketsSent>=count)
				break;

		if(got_signal)
			break;
		sleep(1);
	}
	if(got_signal)
		printf("\b\b\n");

	printf("--- %s tcp ping statistics ---\n",hoststr);
	printf("%d packets transmitted, %d packets received, %d%% packet loss\n",PacketsSent,PacketsReceived,100-PacketsReceived*100/PacketsSent);

}
