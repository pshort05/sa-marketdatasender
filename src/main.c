/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * main.c
 * Copyright (C) Paul Short 2010 <paul@short-associates.com>
 * 
 */

#include <stdio.h>      
#include <sys/socket.h> // for socket(), connect(), sendto(), and recvfrom()
#include <sys/poll.h>   // for poll() to get socket status
#include <arpa/inet.h>  // for sockaddr_in and inet_addr()
#include <stdlib.h>     
#include <string.h> 
#include <ctype.h>    
#include <unistd.h>     // for close() 
#include <pthread.h>    // POSIX multithreading libary
#include <sched.h>		// Needed to change scheduling priority on threads
#include <stdarg.h>
#include <sys/time.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h> 
#include <assert.h>

#define MAX_LEN  1024    /* maximum string size to send */
#define MIN_PORT 1024    /* minimum port allowed */
#define MAX_PORT 65535   /* maximum port allowed */

#define TRUE                1
#define FALSE               0
#define LINE_BUFFER_SIZE    4096
#define BUFFER_SIZE			4096

// number of Microseconds between each send
#define MARKET_DATA_WAIT_TIME   50


#define SOCKET_BUFFER_SIZE		128
#define LISTENQ        			(1024) 

#define MULTICAST_IP_ADDRESS	"192.168.1.24"
#define MULTICAST_PORT			5052

#define SOCKET_SERVER_IP		"192.168.1.211"
#define SOCKET_SERVER_PORT		5025

#define DUMMY_TRADE_DATA		"TU AAAA 40415 250.060 @ Q 100 P A 4977567 T---"
#define DUMMY_QUOTE_DATA		"IU ZZZZ S S 86.81 2000 87.65 900"

// ----------------------------------------------------------------------------
//****************************  fnWriteSocket  **************************
//      Function to initialize the socket layer within the program
//		This will be called in it's own thread
// ----------------------------------------------------------------------------

// Read a line from a socket
ssize_t Readline(int sockd, void *vptr, size_t maxlen) 
{
    ssize_t n, rc;
    char    c, *buffer;

    buffer = vptr;

    for ( n = 1; n < maxlen; n++ ) 
	{
	
		if ( (rc = read(sockd, &c, 1)) == 1 ) 
		{
	    	*buffer++ = c;
	    	if ( c == '\n' )
				break;
		}
		else if ( rc == 0 ) 
		{
	    	if ( n == 1 )
				return 0;
	    	else
				break;
		}
		else 
		{
	    	if ( errno == EINTR )
				continue;
	    	return -1;
		}
    }

    *buffer = 0;
    return n;
}


//  Write a line to a socket
ssize_t Writeline(int sockd, const void *vptr, size_t n) 
{
    size_t      nleft;
    ssize_t     nwritten;
    const char *buffer;

    buffer = vptr;
    nleft  = n;

    while ( nleft > 0 ) 
	{
		if ( (nwritten = write(sockd, buffer, 1)) <= 0 ) 
		{
	    	if ( errno == EINTR )
				nwritten = 0;
	    	else
				return -1;
		}
		nleft  -= nwritten;
		buffer += nwritten;
    }

    return n;
}

//****************************  fnWriteSocket  **************************
inline int fnWriteSocket(int tSocket, char *szBuffer, size_t soBuffer) 
{
	return send(tSocket, szBuffer, soBuffer, 0);
}

//****************************  fnReadSocket  **************************
inline int fnReadSocket(int tSocket, char *szBuffer, size_t soBuffer) 
{
    ssize_t n, rc;
    char    c, *buffer;

    buffer = szBuffer;

    for ( n = 1; n < soBuffer; n++ ) 
	{
	
		if ( (rc = read(tSocket, &c, 1)) == 1 ) 
		{
	    	*buffer++ = c;
	    	if ( c == '\n' )
				break;
		}
		else if ( rc == 0 ) 
		{
	    	if ( n == 1 )
				return 0;
	    	else
				break;
		}
		else 
		{
	    	if ( errno == EINTR )
				continue;
	    	return -1;
		}
    }
	return 0;
}

void fnHandleError( char * szFunction, char * szError )
{
	printf( "Error in function %s: %s\n", szFunction, szError );
}

void fnDebug( char * szMsg )
{
	printf( "%s\n", szMsg );
}

int main(int argc, char *argv[]) 
{
    int       list_s;                /*  listening socket          */
    int       conn_s;                /*  connection socket         */
    short int port;                  /*  port number               */
    struct    sockaddr_in servaddr;  /*  socket address structure  */
    //char      szBuffer[BUFFER_SIZE];      /*  character buffer          */
	//char	  szSocketBuffer[BUFFER_SIZE];
	char	  szServerIP[BUFFER_SIZE];
	struct pollfd	sPoll;
	//double 	fPrice;
	//int		iPass=0;
	//int		iFound;
	int i;
    FILE *fpMDSPackets;
	unsigned int send_len;      
	char szLineBuffer[LINE_BUFFER_SIZE];
	char *szMarketData;


	port = SOCKET_SERVER_PORT;
	strcpy ( szServerIP, SOCKET_SERVER_IP );


	// Create the listening socket
	if ( (list_s = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) 
	{
		fnHandleError ( "fnSocketEngine", "Error creating listening socket");
		//pthread_exit(NULL);
	}

		
	// Set all bytes in socket address structure to zero  
	// fill in all the relevant data members
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family      = AF_INET;
	servaddr.sin_addr.s_addr = inet_addr(szServerIP);
	servaddr.sin_port        = htons(port);


	// Bind our socket addresss to the listening socket, and call listen()
	if ( bind(list_s, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0 ) 
	{
		fnHandleError( "fnSocketEngine", "Error calling bind()");
	}

	if ( listen(list_s, LISTENQ) < 0 ) 
	{
		fnHandleError( "fnSocketEngine", "Error calling listen()");
	}

    
    // Enter an infinite loop to respond to client requests and stream MDS Data
    while ( TRUE ) 
	{
		fnDebug( "Socket Server Initialized - waiting for connection" );

		// Wait for a connection, then accept() it
		if ( (conn_s = accept(list_s, NULL, NULL) ) < 0 ) 
		{
	    	fnHandleError ( "fnSocketEngine", "Error calling accept()");
	    	continue;
		}
		
		
		// The client has attached to the server - start streaming data
		fnDebug ( "Client Connected, streaming data" );
		sPoll.fd = conn_s;
		sPoll.events = POLLOUT;
		
	    // Open the MDSPackets file and start sending MDS Data
		fpMDSPackets = fopen( argv[1] ,"r");
		assert(fpMDSPackets);

		
		// Start reading the trade updates from the Queue
        while ( fgets(szLineBuffer, LINE_BUFFER_SIZE-2, fpMDSPackets) != NULL )
        {
			// Check for EOF before working with any data
			if ( feof( fpMDSPackets ) != 0 )
				break;


			// Poll the socket to see if the client has disconnected
			poll( &sPoll, 1, 1 );
			if (sPoll.revents&POLLHUP)
				goto SocketClosed;

			
			// Filter any lines that don't have the "MDS" tag
			if( szLineBuffer[20] == 'M' )
			{	

				szMarketData = szLineBuffer+26;  
				send_len = strlen( szMarketData );

				if ( fnWriteSocket ( conn_s, szMarketData, send_len) < send_len ) 
				{
					fnHandleError( "fnSocketEngine", "Error writing to socket" );
					goto SocketClosed;
				}
				fnDebug( szMarketData );

				usleep(MARKET_DATA_WAIT_TIME);
			}
			memset(szLineBuffer, 0, sizeof(szLineBuffer));
			

		}

		SocketClosed:
			
		close(conn_s);  
		fnDebug( "Client Disconnected" );
		
	}
	exit(1);
}

/*
int main(int argc, char *argv[]) 
{
    FILE *fpMDSPackets;
	int sock;                  
	//char send_str[MAX_LEN];     
	struct sockaddr_in mc_addr; 
	unsigned int send_len;      
	char mc_addr_str[1024];     
	unsigned short mc_port;     
	unsigned char mc_ttl=1;     
	struct in_addr interface_addr;
	char szLineBuffer[LINE_BUFFER_SIZE];
	char *szMarketData;

	
    // quick check for the command line arguments
    //if ( argc != 1 )
    //{
    //   printf( "USAGE: ./sa-marketdatasender <datafile>\n" );
    //   exit(FALSE);
    //}
	
	// Hardcode the IP address and port for this application
	strcpy( mc_addr_str, MULTICAST_IP_ADDRESS );	 
	mc_port     = MULTICAST_PORT;								 

	
	if ((mc_port < MIN_PORT) || (mc_port > MAX_PORT)) 
	{
		fprintf(stderr, "Invalid port number argument %d.\n", mc_port);
		fprintf(stderr, "Valid range is between %d and %d.\n", MIN_PORT, MAX_PORT);
		exit(1);
	}

	
	if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) 
	{
		perror("socket() failed");
		exit(1);
	}

	// Set the Inteface to use for Multicast
	//interface_addr.s_addr = inet_addr("192.168.224.2");
	//if ( setsockopt (sock, IPPROTO_IP, IP_MULTICAST_IF, &interface_addr, sizeof(interface_addr)) < 0 );
	//{
	//	perror("setsockopt(inteface) failed");
	//	//exit(1);
	//} 
	
	// set the TTL (time to live/hop count) for the send
	if ((setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (void*) &mc_ttl, sizeof(mc_ttl))) < 0) 
	{
		perror("setsockopt(ttl) failed");
		exit(1);
	} 
	  
	
	memset(&mc_addr, 0, sizeof(mc_addr));
	mc_addr.sin_family      = AF_INET;
	mc_addr.sin_addr.s_addr = inet_addr(mc_addr_str);
	mc_addr.sin_port        = htons(mc_port);

	
	memset(szLineBuffer, 0, sizeof(szLineBuffer));

    // Open the MDSPackets file and start sending MDS Data
    fpMDSPackets = fopen( argv[1] ,"r");
    assert(fpMDSPackets);

	printf( "Reading MDS Packets File\n" );
    if (fpMDSPackets != NULL)
    {
        while ( fgets(szLineBuffer, LINE_BUFFER_SIZE-2, fpMDSPackets) != NULL )
        {
			
              // Check for EOF before working with any data
              if ( feof( fpMDSPackets ) != 0 )
                  break;

			  // Filter any lines that don't have the "MDS" tag
			  if( szLineBuffer[20] == 'M' )
			  {	
					szMarketData = szLineBuffer+26;  
					send_len = strlen( szMarketData );
					
					if ((sendto(sock, szMarketData, send_len, 0, (struct sockaddr *) &mc_addr, sizeof(mc_addr))) != send_len) 
					{
						perror("sendto() sent incorrect number of bytes");
						exit(1);
					}				  
			  }
			  memset(szLineBuffer, 0, sizeof(szLineBuffer));
        }
    }
    fclose(fpMDSPackets);


	
	close(sock);  

	exit(0);
}
*/
