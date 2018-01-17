/******************************************************************************
Meridian prototype distribution
Copyright (C) 2005 Bernard Wong

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

The copyright owner can be contacted by e-mail at bwong@cs.cornell.edu
*******************************************************************************/

using namespace std;

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <getopt.h>
//	Marshal.h must be included to create the packets
#include "Marshal.h"
//	MeridianProcess.h is not necessary, but contains many
//	static methods that may be useful
#include "MeridianProcess.h"
//	Contains functions that are shared across all the demo applications
#include "MeridianDemo.h"

//	How long to wait for before timing out
#define SEARCH_TIMEOUT_S	8

void usage() {
	fprintf(stderr, 
		"Usage: demoMultiConst [-r ratio] packet_type meridian_node:port " 
		"sitename:port:ms [sitename:port:ms ... ]\n"
		"Packet_type can be tcp, dns, icmp, or ping\n\n"
		"e.g. demoMultiConst -b 0.5 tcp planetlab1.cs.cornell.edu:3964 "
		"www.slashdot.org:80:10 www.espn.org:80:5\n");
}

int main(int argc, char* argv[]) {
	double ratio = 0.5;		// Default Beta ratio of query
	int option_index = 0;
	static struct option long_options[] = {
		{"ratio", 1, NULL, 1},
		{"help", 0, NULL, 2}, 
		{0, 0, 0, 0}
	};
	// 	Start parsing parameters 
	while (true) {
		int c = getopt_long_only(argc, argv, "", long_options, &option_index);
		if (c == -1) {
			break;	// Done
		}
		switch (c) {
		case 1:
			ratio = atof(optarg);
			if (ratio <= 0.0 || ratio >= 1.0) {
				fprintf(stderr, "Ratio must be between 0 and 1\n");
				return -1;			
			}
			break;
		case 2:
			usage();
			return -1;
		case '?':
			usage();
			return -1;
		default:
			fprintf(stderr, "Unrecognized character %d returned \n", c);
			break;
		}
	}
	//	There are 3 required fields
	if (argc < (optind + 3)) {
		usage();
		return -1;
	}	
	int meridSock;
	if ((meridSock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		perror("Cannot create UDP socket");
		return -1;
	}
	//	Bind the socket to any port. The early binding is to 
	//	determine ahead of time what port is used. The port
	//	number is used in generating query ids.	
	struct sockaddr_in myAddr;
	myAddr.sin_family 		= AF_INET;
	myAddr.sin_port 		= 0;
	myAddr.sin_addr.s_addr 	= INADDR_ANY;
	memset(&(myAddr.sin_zero), '\0', 8);
	if (bind(meridSock, (struct sockaddr*)&myAddr, 
			sizeof(struct sockaddr)) == -1) {
		perror("Cannot bind UDP socket to desired port");
		close(meridSock);
		return -1;
	}	
	srand(time(NULL));
	uint64_t queryNum = getNewQueryID(meridSock);
	//	Set up the beta for the query 
	ushort betaNum = (ushort)(ratio * 1000.0);
	ushort betaDen = 1000;	
	ReqConstraintGeneric* reqPacket;
	char* packetType = argv[optind++];
	if (strcmp(packetType, "tcp") == 0) {
		reqPacket = new ReqConstraintTCP(queryNum, betaNum, betaDen, 0, 0);	
	} else if (strcmp(packetType, "dns") == 0) {
		reqPacket = new ReqConstraintDNS(queryNum, betaNum, betaDen, 0, 0);
	} else if (strcmp(packetType, "ping") == 0) {
		reqPacket = new ReqConstraintPing(queryNum, betaNum, betaDen, 0, 0);
#ifdef PLANET_LAB_SUPPORT
	} else if (strcmp(packetType, "icmp") == 0) {
		reqPacket = new ReqConstraintICMP(queryNum, betaNum, betaDen, 0, 0);
#endif
	} else {
		fprintf(stderr, "Unknown query type specified\n");
		close(meridSock);
		return -1;
	}
	if (reqPacket == NULL) {
		fprintf(stderr, "Error creating reqPacket\n");
		close(meridSock);
		return -1;
	}
	char* meridNode = argv[optind++];
	NodeIdent remoteNode;	// Now we fill the remoteNode struct
	if (parseHostAndPort(meridNode, remoteNode) == -1) {
		close(meridSock);
		delete reqPacket;
		return -1;
	}
	//	Parsing the params triplets host:port:constraint
	for (; optind < argc; optind++) {
		char* tmpStrPtr = strchr(argv[optind], ':');
		if (tmpStrPtr == NULL) {
			fprintf(stderr, "Invalid parameter, should be hostname:port:ms\n");
			close(meridSock);
			delete reqPacket;
			return -1;
		}
		*tmpStrPtr = '\0';
		char* portPtr = strchr(tmpStrPtr+1, ':');
		if (portPtr == NULL) {
			fprintf(stderr, "Invalid parameter, should be hostname:port:ms\n");
			close(meridSock);
			delete reqPacket;
			return -1;			
		}
		*portPtr = '\0';		
		u_short tmpPort = (u_short) atoi(tmpStrPtr + 1);
		u_int tmpMS = (u_int) atoi(portPtr + 1);		
		//	Get the add of remote host
		struct hostent * tmp = gethostbyname(argv[optind]);
		if (tmp == NULL) {
			fprintf(stderr, "Can not resolve hostname %s\n", argv[optind]);			
		} else {
			if (tmp->h_addr_list != NULL) {
				NodeIdentConst tmpIdent = 
				 { 	ntohl(((struct in_addr *)(tmp->h_addr))->s_addr), 
				 	tmpPort, tmpMS };
				reqPacket->addTarget(tmpIdent);
			}
		}
	}
	const vector<NodeIdentConst>* tmpVectConst = reqPacket->returnTargets();
	if (tmpVectConst == NULL || tmpVectConst->size() == 0) {
		fprintf(stderr, "Site destinations invalid\n");
		close(meridSock);
		delete reqPacket;
		return -1;
	}	
	RealPacket outPacket(remoteNode);
	if (reqPacket->createRealPacket(outPacket) == -1) {
		fprintf(stderr, "Cannot create out packet\n");
		close(meridSock);
		delete reqPacket;
		return -1;
	}
	delete reqPacket;	// Done with reqPacket
	reqPacket = NULL;
	cout << "Send query " << queryNum << endl;
	//printf("Send query %llu\n", queryNum);
	struct timeval startTime;
	gettimeofday(&startTime, NULL);
	if (MeridianProcess::performSend(meridSock, &outPacket) == -1) {
		fprintf(stderr, "Cannot send out packet\n");
		close(meridSock);
		return -1;
	}
	char buf[MAX_UDP_PACKET_SIZE];
	struct sockaddr_in theirAddr;
	int addrLen = sizeof(struct sockaddr);
	fd_set readSet, curReadSet;
	FD_ZERO(&readSet);
	FD_SET(meridSock, &readSet);
	int maxFD = meridSock;
	while (true) {
		struct timeval timeOutTV = {SEARCH_TIMEOUT_S, 0};
		memcpy(&curReadSet, &readSet, sizeof(fd_set));
		
		int selectRet = select(maxFD+1, &curReadSet, NULL, NULL, &timeOutTV);
		
		if (selectRet == -1) {
			if (errno == EINTR) {					
				continue; // Interrupted by signal, retry
			}
			printf("Select returned an unrecoverable error\n");
			close(meridSock);
			return -1;	// Return with error
		} else if (selectRet == 0) {		
			printf("Query timed out, "
				"please retry with another meridian node\n");
			close(meridSock);				
			return -1;
		}
		
		if (FD_ISSET(meridSock, &curReadSet)) {		
			int numBytes = recvfrom(meridSock, buf, MAX_UDP_PACKET_SIZE, 0,
				(struct sockaddr*)&theirAddr, (socklen_t*)&addrLen);
			if (numBytes == -1) {
				fprintf(stderr, "Error on receive\n");
				close(meridSock);
				return -1;
			}
			NodeIdent remoteNode = {ntohl(theirAddr.sin_addr.s_addr), 
									ntohs(theirAddr.sin_port) };	
			BufferWrapper rb(buf, numBytes);		
			char queryType;	uint64_t queryID;
			if (Packet::parseHeader(rb, &queryType, &queryID) != -1) {
				if (queryType == RET_INFO) {	
					RetInfo* newInfo 
						= RetInfo::parse(remoteNode, buf, numBytes);
					if (newInfo == NULL) {
						printf("Info packet malformed\n");	
					} else {
						NodeIdent newIdent = newInfo->getInfoNode();
						u_int netAddr = htonl(newIdent.addr);				
						char* remoteString 
							= inet_ntoa(*(struct in_addr*)&(netAddr));	
						printf("Traversing through intermediate node %s:%d\n",
							remoteString, newIdent.port);
						delete newInfo;													
					}				
				} else if (queryType == RET_ERROR) {
					RetError* newError = RetError::parse(buf, numBytes);
					if (newError == NULL) {
						printf("Error packet malformed\n");	
					} else {
						cout << "Error on route received for query " 
							<< newError->retReqID() << endl;
						//printf("Error on route received for query %llu\n", 
						//	newError->retReqID());
						delete newError;													
					}				
					break;
				} else if (queryType == RET_RESPONSE) {
					RetResponse* newResp 
						= RetResponse::parse(remoteNode, buf, numBytes);
					if (newResp == NULL) {
						printf("Response packet malformed\n");	
					} else {				
						NodeIdent newIdent = newResp->getResponse();
						u_int netAddr = htonl(newIdent.addr);				
						char* remoteString 
							= inet_ntoa(*(struct in_addr*)&(netAddr));	
						printf("Closest node is %s:%d and is\n",
							remoteString, newIdent.port);
						const vector<NodeIdentLat>* tmpVect 
							= newResp->getTargets();
						assert(tmpVect != NULL);
						for (u_int i = 0; i < tmpVect->size(); i++) {
							NodeIdentLat newIdentLat = (*tmpVect)[i];
							netAddr = htonl(newIdentLat.addr);				
							char* remoteString 
								= inet_ntoa(*(struct in_addr*)&(netAddr));							
							printf("--> %0.2f ms from %s:%d\n", 
								newIdentLat.latencyUS / 1000.0, remoteString,
								newIdentLat.port);
						}
						delete newResp;	// Done with RetResponse
					}					
					break;
				}
			}
		}
	}
	struct timeval endTime;
	gettimeofday(&endTime, NULL);
	printf("Time to complete query: %0.2f ms\n",
		((endTime.tv_sec - startTime.tv_sec) * 1000.0) +
		((endTime.tv_usec - startTime.tv_usec) / 1000.0));
	close(meridSock);
	return 0;
}

