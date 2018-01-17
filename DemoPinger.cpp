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
//	Marshal.h must be included to create the packets
#include "Marshal.h"
//	MeridianProcess.h is not necessary, but contains many
//	static methods that may be useful
#include "MeridianProcess.h"
//	Contains functions that are shared across all the demo applications
#include "MeridianDemo.h"

//	How long to wait for before timing out
#define SEARCH_TIMEOUT_S	8

int main(int argc, char* argv[]) {
	if (argc <= 3) {
		fprintf(stderr, 
			"Usage: %s [tcp|dns|icmp|ping] meridNode:port sitename:port" 
			" [sitename:port ... ]\n", argv[0]);
		return -1;
	}
	int meridSock;
	if ((meridSock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		perror("Cannot create UDP socket");
		return -1;
	}
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
	ReqGeneric* reqPacket;
	if (strcmp(argv[1], "tcp") == 0) {
		reqPacket = new ReqMeasureTCP(queryNum, 0, 0);	
	} else if (strcmp(argv[1], "dns") == 0) {
		reqPacket = new ReqMeasureDNS(queryNum, 0, 0);
	} else if (strcmp(argv[1], "ping") == 0) {
		reqPacket = new ReqMeasurePing(queryNum, 0, 0);
#ifdef PLANET_LAB_SUPPORT
	} else if (strcmp(argv[1], "icmp") == 0) {
		reqPacket = new ReqMeasureICMP(queryNum, 0, 0);
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
	char* meridNode = argv[2];
	NodeIdent remoteNode;	// Now we fill the remoteNode struct
	if (parseHostAndPort(meridNode, remoteNode) == -1) {
		close(meridSock);
		delete reqPacket;
		return -1;
	}
	// Get the address and port of all targets
	for (int i = 3; i < argc; i++) {
		//	Put the address and port (host order) into the NodeIdent
		//	struct and then add it to the request packet
		NodeIdent tmpIdent;
		if (parseHostAndPort(argv[i], tmpIdent) != -1) {
			reqPacket->addTarget(tmpIdent);
		}
	}
	const vector<NodeIdent>* tmpVectConst = reqPacket->returnTargets();
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
	delete reqPacket;
	reqPacket = NULL;
	cout << "Send query " << queryNum << endl;	
	//printf("Send query %llu\n", queryNum);
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
						// printf("Error on route received for query %llu\n", 
						//	newError->retReqID());
						delete newError;													
					}				
					break;
				} else if (queryType == RET_PING_REQ) {									
					RetPing* newRetPing = RetPing::parse(buf, numBytes);
					if (newRetPing == NULL) {
						printf("Response packet malformed\n");	
					} else {
						const vector<NodeIdentLat>* tmpVectLat = 
							newRetPing->returnNodes();
						cout << "Number of results received is "
							<< tmpVectLat->size() << endl;
						//printf("Number of results received is %d\n", 
						//	tmpVectLat->size());
						for (u_int i = 0; i < tmpVectLat->size(); i++) {
							NodeIdentLat cur = (*tmpVectLat)[i];
							u_int netAddr = htonl(cur.addr);
							printf("Latency to %s:%d is %0.2f ms\n",
								inet_ntoa(*(struct in_addr*)&(netAddr)), 
								cur.port, cur.latencyUS / 1000.0);
						}
						delete newRetPing;	// Done with RetResponse
					}					
					break;
				}
			}
		}
	}
	close(meridSock);
	return 0;
}

