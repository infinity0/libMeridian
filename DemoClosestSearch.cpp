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
		"Usage: demoClosest [-r ratio] packet_type meridian_node:port " 
		"sitename:port [sitename:port ... ]\n"
		"Packet_type can be tcp, dns, icmp, or ping\n\n"
		"e.g. demoClosest -r 0.5 tcp planetlab1.cs.cornell.edu:3964 "
		"www.slashdot.org:80\n");
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
	//	Depending on the command line parameter, we generate
	//	different closest node request queries
	ReqClosestGeneric* reqPacket;
	char* packetType = argv[optind++];
	if (strcmp(packetType, "tcp") == 0) {
		//	Params are a unique query id, numerator of beta,
		//	denominator of beta, rendavous host address,
		//	and rendavous host port. Description of beta
		//	can be found in the paper. The rendavous address
		//	and port is normally for Meridian nodes only,
		//	and can be left as 0, 0
		reqPacket = new ReqClosestTCP(queryNum, betaNum, betaDen, 0, 0);	
	} else if (strcmp(packetType, "dns") == 0) {
		reqPacket = new ReqClosestDNS(queryNum, betaNum, betaDen, 0, 0);
	} else if (strcmp(packetType, "ping") == 0) {
		//	Specifing ping does not actually mean ICMP ping, but is
		//	actually a Meridian "ping". The host must be able to 
		//	understand the Meridian packet format and send back a "pong"
		reqPacket = new ReqClosestMeridPing(queryNum, betaNum, betaDen, 0, 0);	
#ifdef PLANET_LAB_SUPPORT
	} else if (strcmp(packetType, "icmp") == 0) {
		reqPacket = new ReqClosestICMP(queryNum, betaNum, betaDen, 0, 0);	
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
	NodeIdent remoteNode;	// Now we fill the remoteNode struct, which holds
							// the ip and port of the Meridian node that 
							// we will contact
	if (parseHostAndPort(meridNode, remoteNode) == -1) {
		close(meridSock);
		delete reqPacket;
		return -1;
	}
	// Get the address and port of all targets
	for (; optind < argc; optind++) {
		//	Put the address and port (host order) into the NodeIdent
		//	struct and then add it to the request packet
		NodeIdent tmpIdent;
		if (parseHostAndPort(argv[optind], tmpIdent) != -1) {
			reqPacket->addTarget(tmpIdent);
		}
	}
	//	Just to check that we have at least one target
	const vector<NodeIdent>* tmpVectConst = reqPacket->returnTargets();
	if (tmpVectConst == NULL || tmpVectConst->size() == 0) {
		fprintf(stderr, "Site destinations invalid\n");
		close(meridSock);
		delete reqPacket;
		return -1;
	}
	//	RealPacket is our abstraction of a packet. It basically
	//	contains a buffer and a destination	
	RealPacket outPacket(remoteNode);
	//	This tells reqPacket to serialize itself to outPacket
	if (reqPacket->createRealPacket(outPacket) == -1) {
		fprintf(stderr, "Cannot create out packet\n");
		close(meridSock);
		delete reqPacket;
		return -1;
	}
	delete reqPacket;	// reqPacket no longer needed
	reqPacket = NULL;
	cout << "Sending query " << queryNum << endl;
	//printf("Sending query %llu\n", queryNum);
	//	Set start time to determine the amount of time require to
	//	satisfy the query
	struct timeval startTime;
	gettimeofday(&startTime, NULL);
	if (MeridianProcess::performSend(meridSock, &outPacket) == -1) {
		fprintf(stderr, "Cannot send out packet\n");
		close(meridSock);
		return -1;
	}
	// 	Holds incoming packets
	char buf[MAX_UDP_PACKET_SIZE];
	// 	Used by recv
	struct sockaddr_in theirAddr;
	int addrLen = sizeof(struct sockaddr);
	//	Set meridSock to be readable and wait for reply
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
			// BufferWrapper is just a convenience class I use to
			// put a light wrapper around a buffer for traversal
			BufferWrapper rb(buf, numBytes);		
			char queryType;	uint64_t queryID;
			// Look at header to see what type of query it is,
			// and what the queryID is 
			if (Packet::parseHeader(rb, &queryType, &queryID) != -1) {
				// RET_INFO simply tells us what Meridian node the 
				// query is traversing through right now and is optional
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
				//	RET_ERROR means	a meridian node encountered an error
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
				//	RET_RESPONSE is when the query is complete
				} else if (queryType == RET_RESPONSE) {				
					RetResponse* newResp 
						= RetResponse::parse(remoteNode, buf, numBytes);
					if (newResp == NULL) {
						printf("Response packet malformed\n");	
					} else {
						//	getReponse gives the closest node				
						NodeIdent newIdent = newResp->getResponse();
						u_int netAddr = htonl(newIdent.addr);				
						char* remoteString 
							= inet_ntoa(*(struct in_addr*)&(netAddr));	
						printf("Closest node is %s:%d and is\n",
							remoteString, newIdent.port);
						//	The vector holds the actual latency info
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

