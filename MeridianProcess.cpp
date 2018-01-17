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
#include <netinet/tcp.h>
#include <limits.h>
#include <resolv.h>
#include <signal.h>
#include "Marshal.h"
#include "MeridianProcess.h" 

int MeridianProcess::createRendavousTunnel(const NodeIdent& rendvNode) {
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1) {
		ERROR_LOG("Cannot create sock\n");
		return -1;
	}
	int opt = 1;
	if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
				  (char *) &opt, sizeof(opt)) < 0) {
		ERROR_LOG("setsockopt reuseaddr failed\n");
		close(sock);
		return -1;
	}
    // Set socket options that are necessary for rate limiting
	opt = 1;
	if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(int)) == -1) {
		//perror("setsockopt");
		ERROR_LOG("Cannot set TCP_NODELAY\n");
		close(sock);
		return -1;
	}	
	struct sockaddr_in addr;
	addr.sin_family 		= AF_INET;         
	addr.sin_port 			= htons(g_meridPort);     
	addr.sin_addr.s_addr 	= INADDR_ANY;
	memset(&(addr.sin_zero), '\0', 8);
	if (bind(sock, (struct sockaddr *)&addr, 
			sizeof(struct sockaddr)) 		== -1 ||
		setNonBlock(sock) 					== -1) {
		ERROR_LOG("Cannot bind or set non block\n");
		close(sock);
		return -1;
	}	
	if (performConnect(sock, rendvNode.addr, rendvNode.port) == -1) {
		ERROR_LOG("Perform connect returns -1\n");
		close(sock);
		return -1;		
	}
	//	Set the socket to be writeable
	fd_set curWriteSet, writeSet;
	FD_ZERO(&writeSet);
	FD_SET(sock, &writeSet);		
	//	Set up the current time and the future timeout time
	struct timeval curTime, maxTime, timeout;
	gettimeofday(&maxTime, NULL);
	maxTime.tv_sec += DEFAULT_TIME_OUT_S;	
	while (true) {
		memcpy(&curWriteSet, &writeSet, sizeof(fd_set));		
		gettimeofday(&curTime, NULL);		
		if (timeoutLength(&curTime, &maxTime, &timeout) == -1) {
			WARN_LOG("Timeout out in connecting to rendavous\n");
			break;
		}							
		int selectRet = select(sock+1, NULL, &curWriteSet, NULL, &timeout);		
		if (selectRet == -1) {
			if (errno == EINTR) {
				continue;	// Interrupted by signal, retry	
			}
			WARN_LOG("Select returned error in rendavous\n");
			break;
		} else if (selectRet == 0) {
			continue;	//	Let the timeoutLength part check the time
		} else if (FD_ISSET(sock, &curWriteSet)) {
			struct sockaddr	peerAddr;
			socklen_t peerLen = sizeof(struct sockaddr);			
			if (getpeername(sock, &peerAddr, &peerLen) != -1){				
				return sock; //	Connection completed successfully
			}
			WARN_LOG("Call to getpeername fails\n");
		}
		break;	// Would have called continue if we wanted to loop
	}
	//	If we had to break out of the loop, then the connect failed,
	//	or timed out. Close and return -1
	close(sock);
	return -1;
}

int MeridianProcess::setNonBlock(int fd) {
	int sockflags;
	if ((sockflags = fcntl(fd, F_GETFL, 0)) != -1){
		sockflags |= O_NONBLOCK;
		return fcntl(fd, F_SETFL, sockflags);
	}
	return -1;
}
		
//	Creates a TCP listener socket	
int MeridianProcess::createTCPListener(u_short port) {
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	int opt = 1;
	if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
				  (char *) &opt, sizeof(opt)) < 0) {
		perror("setsockopt reuseaddr failed\n");
		close(sock);
		return -1;
	}
    // Set socket options that are necessary for rate limiting
	opt = 1;
	if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(int)) == -1) {
		perror("setsockopt");
		close(sock);
		return -1;
	}		
	struct sockaddr_in addr;
	addr.sin_family 		= AF_INET;         
	addr.sin_port 			= htons(port);     
	addr.sin_addr.s_addr 	= INADDR_ANY;
	memset(&(addr.sin_zero), '\0', 8);
	if (bind(sock, (struct sockaddr *)&addr, 
			sizeof(struct sockaddr)) 		== -1 ||
		listen(sock, TCP_LISTEN_BACKLOG) 	== -1 ||
		setNonBlock(sock) 					== -1) {
		close(sock);
		return -1;
	}
	return sock;
}

int MeridianProcess::performConnect(
		int sendSock, uint32_t addr, uint16_t port) {
	struct sockaddr_in hostAddr;	
	hostAddr.sin_family 		= AF_INET;
	hostAddr.sin_port			= htons(port);
	hostAddr.sin_addr.s_addr	= htonl(addr);
	memset(&(hostAddr.sin_zero), '\0', 8);
	if (connect(sendSock, (struct sockaddr*)&hostAddr,
		sizeof(struct sockaddr)) == -1 && errno != EINPROGRESS) {
		return -1;
	}
	return 0;
}

int MeridianProcess::timeoutLength(struct timeval* curTime, 
		struct timeval* nextEventTime, struct timeval* timeOutTV) {
	QueryTable::normalizeTime(*curTime);
	QueryTable::normalizeTime(*nextEventTime);
	if (curTime->tv_sec > nextEventTime->tv_sec ||
			(curTime->tv_sec == nextEventTime->tv_sec && 
			curTime->tv_usec >= nextEventTime->tv_usec)) {
		return -1;	// Expired
	}
	timersub(nextEventTime, curTime, timeOutTV);
	return 0;
}

int MeridianProcess::eraseTCPConnection(
		const map<int, pair<uint64_t, NodeIdent>*>::iterator& conIt) {
	int tmpSock = conIt->first;	
	pair<uint64_t, NodeIdent>* curPair = conIt->second;		
	g_tcpProbeConnections.erase(conIt);			
	FD_CLR(tmpSock, &g_writeSet);
	close(tmpSock);
	delete curPair;
	return 0;
}

int MeridianProcess::eraseDNSConnection(
		const map<int, pair<uint64_t, NodeIdent>*>::iterator& conIt) {
	int tmpSock = conIt->first;	
	pair<uint64_t, NodeIdent>* curPair = conIt->second;		
	g_dnsProbeConnections.erase(conIt);			
	FD_CLR(tmpSock, &g_readSet);	// Note that we are clearing readSet
	close(tmpSock);
	delete curPair;
	return 0;
}	

int MeridianProcess::eraseTCPConnection(int in_sock) {
	WARN_LOG("Entering eraseTCPConnection\n");
	map<int, pair<uint64_t, NodeIdent>*>::iterator it 
		= g_tcpProbeConnections.find(in_sock);
	if (it != g_tcpProbeConnections.end()) {
		WARN_LOG("Found something to erase\n");
		return eraseTCPConnection(it);
	}
	// It might have been deleted already (tcp connect failed), so if 
	// it is not in the map, don't do anything		
	return 0;
}

int MeridianProcess::eraseDNSConnection(int in_sock) {
	WARN_LOG("Entering eraseDNSConnection\n");
	map<int, pair<uint64_t, NodeIdent>*>::iterator it 
		= g_dnsProbeConnections.find(in_sock);
	if (it != g_dnsProbeConnections.end()) {
		WARN_LOG("Found something to erase\n");
		return eraseDNSConnection(it);
	}
	return 0;
}

MeridianProcess::MeridianProcess(u_short meridian_port, u_short info_port, 
				u_int prim_size, u_int second_size, int ring_base, int stopFD) 
		: 	g_meridPort(meridian_port), g_infoPort(info_port), 
			g_meridSock(-1), g_infoSock(-1), g_rendvFD(-1), g_rendvListener(-1), 
			g_maxFD(0), g_stopFD(stopFD)
#ifdef MERIDIAN_DSL
			, g_dummySock(-1), g_max_ttl(DEFAULT_MAX_TTL)
#endif
#ifdef PLANET_LAB_SUPPORT
			, g_icmpSock(-1)
#endif
			{
	//	Packet used as a temp buffer to hold rendavous client traffic
	NodeIdent dummy = {0, 0};
	g_rendvRecvPacket = new RealPacket(dummy);
	setRendavousNode(0, 0);
	FD_ZERO(&g_readSet);
	FD_ZERO(&g_writeSet);
	setGossipInterval(10, 10, 60);
	setReplaceInterval(300);
	g_rings = new RingSet(prim_size, second_size, ring_base);
	g_tcpCache = new LatencyCache(PROBE_CACHE_SIZE, PROBE_CACHE_TIMEOUT_US);
	g_dnsCache = new LatencyCache(PROBE_CACHE_SIZE, PROBE_CACHE_TIMEOUT_US);
	g_pingCache = new LatencyCache(PROBE_CACHE_SIZE, PROBE_CACHE_TIMEOUT_US);
#ifdef PLANET_LAB_SUPPORT
	g_icmpCache = new LatencyCache(PROBE_CACHE_SIZE, PROBE_CACHE_TIMEOUT_US);
#endif
}
		
MeridianProcess::~MeridianProcess() {
	//	Delete caches
	if (g_tcpCache) {
		delete g_tcpCache;	
	}
	if (g_dnsCache) {
		delete g_dnsCache;	
	}
	if (g_pingCache) {
		delete g_pingCache;	
	}	
	// Delete ring set
	if (g_rings) {
		delete g_rings;	
	}
	//	Cleanup sockets
	if (g_meridSock != -1) {
		close(g_meridSock);	
	}
	if (g_infoSock != -1) {
		close(g_infoSock);	
	}
	if (g_rendvFD != -1) {
		close(g_rendvFD);	
	}	
	if (g_rendvListener != -1) {
		close(g_rendvListener);	
	}
	if (g_stopFD != -1) {
		close(g_stopFD);	
	}
	if (g_rendvRecvPacket) {
		delete g_rendvRecvPacket;
	}	
	list<RealPacket*>::iterator packetIt = g_outPacketList.begin();
	for (; packetIt != g_outPacketList.end(); packetIt++) {
		if (*packetIt) {
			delete *packetIt;
		}
	}		
	list<pair<int, RealPacket*>*>::iterator infoIt 
		= g_infoConnects.begin();
	for (; infoIt != g_infoConnects.end(); infoIt++) {
		pair<int, RealPacket*>* curPair = *infoIt;
		if (curPair) {
			if (curPair->second) {
				delete curPair->second;	
			}
			delete curPair;
		}
	}
	//	Close all TCP connections and free structures
	map<int, pair<uint64_t, NodeIdent>*>::iterator mapIt 
		= g_tcpProbeConnections.begin();
	for (; mapIt != g_tcpProbeConnections.begin(); mapIt++) {
		if (mapIt->first != -1) {
			close(mapIt->first);
		}
		if (mapIt->second != NULL) {
			delete mapIt->second;
		}
	}
	//	Close all DNS connections and free structures
	mapIt = g_dnsProbeConnections.begin();
	for (; mapIt != g_dnsProbeConnections.begin(); mapIt++) {
		if (mapIt->first != -1) {
			close(mapIt->first);
		}
		if (mapIt->second != NULL) {
			delete mapIt->second;
		}
	}
	//	Clean up all rendavous connections
	map<int, list<RealPacket*>*>::iterator rendvQIt = g_rendvQueue.begin();
	for (; rendvQIt != g_rendvQueue.end(); rendvQIt++) {
		if (rendvQIt->second != NULL) {
			list<RealPacket*>::iterator listQIt = rendvQIt->second->begin();
			for (; listQIt != rendvQIt->second->end(); listQIt++) {
				if (*listQIt) {
					delete *listQIt;
				}
			}
			delete rendvQIt->second;				
		}
		if (rendvQIt->first != -1) {
			close(rendvQIt->first);
		}
	}

#ifdef MERIDIAN_DSL
	if (g_dummySock != -1) {
		close(g_dummySock);	
	}
#endif

#ifdef PLANET_LAB_SUPPORT
	if (g_icmpSock != -1) {
		close(g_icmpSock);	
	}	
#endif
	
}

int MeridianProcess::evaluateTimeout() {
	WARN_LOG("TIMEOUT Encountered\n");
	return g_queryTable.handleTimeout();
}

uint64_t MeridianProcess::getNewQueryID() {
	uint64_t retVal;		
	for (u_int i = 0; i < USHRT_MAX; i++) {		
		u_short randVal = rand() % USHRT_MAX;
		//	Concat with port
		u_int secondParam = g_meridPort;
		secondParam = secondParam << 16;
		secondParam |= randVal;
		//	Concat with address		
		retVal = Packet::to64(g_localAddr, secondParam);
		//	Check to see if it is used
		if (!(g_queryTable.isQueryInTable(retVal))) {
			return retVal;
		}
	}
	//	The system seem to be processing near capcity, let's create a 
	//	random 64 value for a query id instead, which is unluckly to collide
	retVal = Packet::to64(rand(), rand());
	return retVal;
}
	
int MeridianProcess::addNodeToRing(const NodeIdentRendv& in_remote) {
	//	To avoid rapid pinging of a node, we skip ping nodes that
	//	have been recently pinged
	uint32_t curLatencyUS;
	NodeIdent curIdent = {in_remote.addr, in_remote.port};	
	if (pingCacheGetLatency(curIdent, &curLatencyUS) != -1) {		
		WARN_LOG("Skip pinging of recently pinged node\n");
		return 0;
	}
	//	Create query to encapsulate the state
	AddNodeQuery* newQuery = new AddNodeQuery(in_remote, this);
	if (g_queryTable.insertNewQuery(newQuery) == -1) {			
		delete newQuery;
		return -1;
	}
	newQuery->init();	
	return 0;
}

int MeridianProcess::performGossip() {
	vector<NodeIdentRendv> randNodes;
	g_rings->getRandomNodes(randNodes);
	for (u_int i = 0; i < randNodes.size(); i++) {		
		//NodeIdent remoteNode = {randNodes[i].addr, randNodes[i].port}; 
		//GossipQuery* newQuery = new GossipQuery(remoteNode, this);
		GossipQuery* newQuery = new GossipQuery(randNodes[i], this);
		if (g_queryTable.insertNewQuery(newQuery) == -1) {			
			delete newQuery;
			return -1;
		}
		newQuery->init();		
	}
	return 0;
}

int MeridianProcess::performRingManagement() {
	//	Find all full rings
	int numRings = g_rings->getNumberOfRings();
	vector<int> eligibleRings;
	for (int i = 0; i < numRings; i++) {
		//	Test if the ring is eligible for ring management
		if (g_rings->eligibleForReplacement(i)) {
			eligibleRings.push_back(i);
		}
	}
	if (eligibleRings.empty()) {
		return 0;	
	}
	// 	Pick a random eligible ring
	int selectedRing = eligibleRings[rand() % eligibleRings.size()];
	RingManageQuery* newQuery = new RingManageQuery(selectedRing, this);
	if (g_queryTable.insertNewQuery(newQuery) == -1) {			
		delete newQuery;
		return -1;
	}
	newQuery->init();	
	return 0;	
}

int MeridianProcess::addOutPacket(RealPacket* in_packet) {
	g_outPacketList.push_back(in_packet);
	FD_SET(g_meridSock, &g_writeSet);
	g_maxFD = MAX(g_meridSock, g_maxFD); 
	return 0;
}

void MeridianProcess::writePending() {
	while (true) {
		assert(!(g_outPacketList.empty()));
		RealPacket* firstPacket = g_outPacketList.front();
		if (performSend(g_meridSock, firstPacket) == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {									
				break; // Retry again later when ready to send
			} else {
				//	Let's just continute still, but remove this packet
				ERROR_LOG("Error calling send\n");	
			}
		}		
		g_outPacketList.pop_front();
		delete firstPacket;	// Done with packet
		if (g_outPacketList.empty()) {
			FD_CLR(g_meridSock, &g_writeSet);
			break;	// No more to send
		}
	}
}

int MeridianProcess::performSend(int sock, RealPacket* in_packet) {
#ifdef DEBUG	
	u_int netAddr = htonl(in_packet->getAddr());
	char* ringNodeStr = inet_ntoa(*(struct in_addr*)&(netAddr));	
	WARN_LOG_3("Sending query to port number %s:%d of size %d\n", 
		ringNodeStr, in_packet->getPort(), in_packet->getPayLoadSize());
#endif		

	// Handle firewall host by wrapping it around a PUSH packet
	if (in_packet->getRendvAddr() != 0 || in_packet->getRendvPort() != 0) {
#ifdef DEBUG		
		u_int netAddr = htonl(in_packet->getRendvAddr());		
		char* ringNodeStr = inet_ntoa(*(struct in_addr*)&(netAddr));		
		WARN_LOG_2("Redirecting to rendavous node, %s:%d\n", ringNodeStr, 
			in_packet->getRendvPort());
#endif			
		//	QID for push packet should never be used, just set it to 0
		PushPacket pushPacket(0, in_packet->getAddr(), in_packet->getPort());
		NodeIdent rendvNode 
			= {in_packet->getRendvAddr(), in_packet->getRendvPort()};
		//	This packet MUST not have a rendavous host
		RealPacket tmpPacket(rendvNode);
		if (pushPacket.createRealPacket(tmpPacket) == -1) {
			ERROR_LOG("Cannot create PUSH packet\n");
			return -1;
		}
		tmpPacket.append_packet(*in_packet);
		if (!(tmpPacket.completeOkay())) {			
			ERROR_LOG("Cannot create PUSH packet\n");
			return -1;
		}
		return performSend(sock, &tmpPacket);
	}				
	struct sockaddr_in hostAddr;
	//memset(&(hostAddr), '\0', sizeof(struct sockaddr_in));
	hostAddr.sin_family         = AF_INET;
	hostAddr.sin_port           = htons(in_packet->getPort());
	hostAddr.sin_addr.s_addr    = htonl(in_packet->getAddr());
	memset(&(hostAddr.sin_zero), '\0', 8);
	int sendRet = sendto(sock, in_packet->getPayLoad(),
		in_packet->getPayLoadSize(), 0,
		(struct sockaddr*)&hostAddr, sizeof(struct sockaddr));
	return sendRet;
}

int MeridianProcess::readPacket() {
	char buf[MAX_UDP_PACKET_SIZE];
	struct sockaddr_in theirAddr;
	int addrLen = sizeof(struct sockaddr);
	//	Perform actual recv on socket
	int numBytes = recvfrom(g_meridSock, buf, MAX_UDP_PACKET_SIZE, 0,
		(struct sockaddr*)&theirAddr, (socklen_t*)&addrLen);		
	if (numBytes == -1) {
		perror("Error on recvfrom");
		return -1;		
	}
	NodeIdent remoteNode = {ntohl(theirAddr.sin_addr.s_addr), 
							ntohs(theirAddr.sin_port) };
							
	return handleNewPacket(buf, numBytes, remoteNode);
}

int MeridianProcess::handleNewPacket(
		char* buf, int numBytes, const NodeIdent& remoteNode) {
	BufferWrapper rb(buf, numBytes);		
	char queryType;	uint64_t queryID;
	if (Packet::parseHeader(rb, &queryType, &queryID) != -1) {
		switch (queryType) {
			case PUSH: {
					RealPacket* inPacket 
						= PushPacket::parse(remoteNode, buf, numBytes);						
					NodeIdent destIdent 
						= {inPacket->getAddr(), inPacket->getPort()};
#ifdef DEBUG						
					u_int netAddr = htonl(destIdent.addr);
					char* ringNodeStr = inet_ntoa(*(struct in_addr*)&(netAddr));
					WARN_LOG_2("PUSH dest is %s:%d\n", 
						ringNodeStr, destIdent.port);
#endif										
					map<NodeIdent, int, ltNodeIdent>::iterator rendvIt 
						= g_rendvConnections.find(destIdent);
					if (rendvIt == g_rendvConnections.end()) {
						ERROR_LOG("Node not rendavous for target\n");
						delete inPacket;												
					} else {
						map<int, list<RealPacket*>*>::iterator rendvQIt 
							= g_rendvQueue.find(rendvIt->second);
						if (rendvQIt == g_rendvQueue.end()) {
							ERROR_LOG("Inconsistent rendavous state\n");
							delete inPacket;
							//	Let's try to fix it anyway
 							g_rendvConnections.erase(rendvIt);
						} else {
							//	Push it into queue and then on fd in writeSet
							rendvQIt->second->push_back(inPacket);
							FD_SET(rendvQIt->first, &g_writeSet);
							g_maxFD = MAX(rendvQIt->first, g_maxFD);
						}
					}					
				} break;
			case PING: {
					PongPacket pongPacket(queryID);
					RealPacket* inPacket = new RealPacket(remoteNode);
					if (pongPacket.createRealPacket(*inPacket) == -1) {
						delete inPacket;						
					} else {
						addOutPacket(inPacket);
					}
				} break;
			case GOSSIP: 
			case GOSSIP_PULL: {
#ifdef DEBUG				
					if (queryType == GOSSIP) {
						WARN_LOG("Received a GOSSIP packet\n");
					} else {
						WARN_LOG("Received a GOSSIP_PULL packet\n");
					}
#endif
					GossipPacketGeneric* tmp = NULL;
					if (queryType == GOSSIP) {					
						tmp = GossipPacketGeneric::
							parse<GossipPacketPush>(buf, numBytes);
					} else {
						tmp = GossipPacketGeneric::
							parse<GossipPacketPull>(buf, numBytes);
					}
					if (tmp != NULL) {
						//	Add remote node to ring
						NodeIdentRendv remoteNodeRendv = { 
							remoteNode.addr, remoteNode.port, 
							tmp->getRendvAddr(), tmp->getRendvPort() };
#ifdef DEBUG							
						u_int netAddr = htonl(tmp->getRendvAddr());
						char* ringNodeStr = 
							inet_ntoa(*(struct in_addr*)&(netAddr));
						WARN_LOG_2("Rendv in GOSSIP packet is %s:%d\n", 
							ringNodeStr, tmp->getRendvPort());
#endif							
						addNodeToRing(remoteNodeRendv);
						// 	Add nodes in gossip packet to ring						
						const vector<NodeIdentRendv>* tmpVect 
							= tmp->returnTargets();
						for (u_int i = 0; i < tmpVect->size(); i++) {
							//NodeIdentRendv tmpRendv = (*tmpVect)[i];
							addNodeToRing((*tmpVect)[i]);
						}
#ifdef GOSSIP_PUSHPULL
						if (queryType == GOSSIP) {
							GossipPacketPull gPacket(getNewQueryID(), 
									g_rendvNode.addr, g_rendvNode.port);
							if (GossipQuery::fillGossipPacket(gPacket, 
									remoteNodeRendv, this) == 0) {
								WARN_LOG("Creating GOSSIP_PULL ###########\n");
								RealPacket* inPacket = 
									new RealPacket(remoteNodeRendv);
								if (gPacket.createRealPacket(*inPacket) == -1) {
									delete inPacket;	
								} else {
									addOutPacket(inPacket);
								}
							}
						}
#endif						
						delete tmp;	// Finished with gossip packet
					}					
				} break;
#ifdef PLANET_LAB_SUPPORT				
			case REQ_CONSTRAINT_N_ICMP: {	
					handleMCReq<ReqConstraintICMP, HandleMCICMP>(
						queryID, remoteNode, buf, numBytes);
				} break;
#endif				
			case REQ_CONSTRAINT_N_PING: {	
					handleMCReq<ReqConstraintPing, HandleMCPing>(
						queryID, remoteNode, buf, numBytes);
				} break;				
			case REQ_CONSTRAINT_N_DNS: {	
					handleMCReq<ReqConstraintDNS, HandleMCDNS>(
						queryID, remoteNode, buf, numBytes);
				} break;
			case REQ_CONSTRAINT_N_TCP: {	
					handleMCReq<ReqConstraintTCP, HandleMCTCP>(
						queryID, remoteNode, buf, numBytes);			
				} break;
#ifdef PLANET_LAB_SUPPORT				
			case REQ_CLOSEST_N_ICMP: {	
					handleClosestReq<ReqClosestICMP, HandleClosestICMP>(
						queryID, remoteNode, buf, numBytes);
				} break;
#endif				
			case REQ_CLOSEST_N_MERID_PING: {	
					handleClosestReq<ReqClosestMeridPing, HandleClosestPing>(
						queryID, remoteNode, buf, numBytes);
				} break;				
			case REQ_CLOSEST_N_DNS: {	
					handleClosestReq<ReqClosestDNS, HandleClosestDNS>(
						queryID, remoteNode, buf, numBytes);
				} break;
			case REQ_CLOSEST_N_TCP: {	
					handleClosestReq<ReqClosestTCP, HandleClosestTCP>(
						queryID, remoteNode, buf, numBytes);			
				} break;
			case RET_RESPONSE: {
					WARN_LOG("Received a RET_RESPONSE packet\n");
					g_queryTable.notifyQPacket(
						queryID, remoteNode, buf, numBytes);
				} break;				
			case RET_INFO: {
					WARN_LOG("Received a RET_INFO packet\n");
					g_queryTable.notifyQPacket(
						queryID, remoteNode, buf, numBytes);
				} break;				
			case RET_ERROR: {
					WARN_LOG("Received a RET_ERROR packet\n");
					g_queryTable.notifyQPacket(
						queryID, remoteNode, buf, numBytes);
				} break;				
			case PONG: {
					WARN_LOG("Received PONG packet\n");					
					g_queryTable.notifyQPacket(
						queryID, remoteNode, buf, numBytes);
				} break;			
			case REQ_MEASURE_N_MERID_PING: {
					WARN_LOG("Received ReqMeasurePing packet\n");
					handleMeasureReq<ReqMeasurePing, HandleReqPing>(
						remoteNode, buf, numBytes);
				} break;
			case REQ_MEASURE_N_TCP: {
					WARN_LOG("Received ReqMeasureTCP packet\n");
					handleMeasureReq<ReqMeasureTCP, HandleReqTCP>(
						remoteNode, buf, numBytes);
				} break;
			case REQ_MEASURE_N_DNS: {
					WARN_LOG("Received ReqMeasureDNS packet\n");
					handleMeasureReq<ReqMeasureDNS, HandleReqDNS>(
						remoteNode, buf, numBytes);
				} break;
#ifdef PLANET_LAB_SUPPORT
			case REQ_MEASURE_N_ICMP: {
					WARN_LOG("Received ReqMeasureICMP packet\n");
					handleMeasureReq<ReqMeasureICMP, HandleReqICMP>(
						remoteNode, buf, numBytes);
				} break;
#endif
			case RET_PING_REQ: {
					WARN_LOG("Received a RET_PING\n");
					g_queryTable.notifyQPacket(
						queryID, remoteNode, buf, numBytes);					
				} break;
#ifdef MERIDIAN_DSL
			case DSL_REPLY: {
					g_queryTable.notifyQPacket(
						queryID, remoteNode, buf, numBytes);
				} break;
			case DSL_REQUEST: {
					ParserState* new_state = new ParserState();
					if (new_state == NULL) {
						break;	// Cannot create new parser state	
					}
					DSLRequestPacket* inPacket 
						= DSLRequestPacket::parse(new_state, buf, numBytes);
					if (inPacket == NULL) {
						delete new_state;
						break;	// Cannot parse packet						
					}
					//	Get necessary data from packet
					NodeIdentRendv remoteNodeRendv = { 
						remoteNode.addr, remoteNode.port, 
						inPacket->getRendvAddr(), inPacket->getRendvPort() };
					uint64_t prevID = inPacket->retReqID();
					uint16_t q_timeout = inPacket->timeout_ms();
					uint16_t q_ttl = inPacket->getTTL();
					//printf("Timeout of %d ms\n", q_timeout);
					delete inPacket;	// No longer needed
					//	Drop packets that have too large TTLs
					if (q_ttl > g_max_ttl) {
						delete new_state;
						break;						
					}					
					g_parser_line = 1;	// Reset line count for parser
					if (yyparse((void*)new_state) == -1) {
						delete new_state;
						break;	// Parse error
					}
					if (new_state->save_context() == -1) {
						delete new_state;
						break;	// Error saving context
					}					
					makecontext(new_state->get_context(), 
						(void (*)())(&jmp_eval), 1, new_state);
					DSLRecvQuery* newQ = new DSLRecvQuery(new_state, 
						this, remoteNodeRendv, prevID, q_timeout, q_ttl);
					if (newQ == NULL) {
						delete new_state;
						break; // Error creating new Query						
					}
					//	Associate parser state with query id
					new_state->setQuery(newQ);
					//	Associate parser with this meridian process
					new_state->setMeridProcess(this);
					if (g_queryTable.insertNewQuery(newQ) == -1) {			
						delete newQ;	// State is deleted with newQ
					} else {
						newQ->init();
						g_psList.push_back(newQ->getQueryID());
						FD_SET(g_dummySock, &g_writeSet);
						g_maxFD = MAX(g_dummySock, g_maxFD);
					}			
				} break;
#endif
			default:
				break;
		}
	}
	return 0;
}

int MeridianProcess::getInfoPacket(RealPacket& inPacket) {
	int pos = inPacket.getPayLoadSize();
	char* buf = inPacket.getPayLoad();
	int packetSize = inPacket.getPacketSize();
	// This is taken from old version of code
	struct timeval tvStart, tvEnd;
	gettimeofday(&tvStart, NULL);	
	pos += snprintf(buf + pos, packetSize - pos, "HTTP/1.1 200 OK\r\n");
	pos += snprintf(buf + pos, packetSize - pos, 
		"Content-Type: text/html; charset=iso-8859-1\r\n\r\n");
	pos += snprintf(buf + pos, packetSize - pos, "<HTML>\n"
		"<title>Meridian</title>\n<body>\n");
	pos += snprintf(buf + pos, packetSize - pos, 
		"<H2>Meridian node: %s</H2>\n", g_hostname);
	pos += snprintf(buf + pos, packetSize - pos, "<HR SIZE=\"0\">\n");
	pos += snprintf(buf + pos, packetSize - pos,
		"<STRONG>Ring members of %s</STRONG>\n", g_hostname);
	pos += snprintf(buf + pos, packetSize - pos, 
		"<TABLE cellspacing=\"3\" cellPadding=\"3\" width=\"100%s\" "
		"border=\"1\">\n<TBODY>\n", "%");		
	int numRings = g_rings->getNumberOfRings();					
	for (int i = 0; i < numRings; i++) {
		const vector<NodeIdent>* primRing = g_rings->returnPrimaryRing(i);
		if (primRing != NULL && primRing->size() > 0) {
			pos += snprintf(buf + pos, packetSize - pos,
				"<TR><TD><STRONG>Nodes in ring %d</STRONG></TD>"
				"<TD><STRONG>Latency</STRONG></TD></TR>\n", i);						
			for (u_int j = 0; j < primRing->size(); j++) {
				u_int latencyUS = 0;
				if (g_rings->getNodeLatency((*primRing)[j], &latencyUS) == -1) {
					assert(false);					
				}				
				u_int netAddr = htonl((*primRing)[j].addr);
				char* ringNodeStr = inet_ntoa(*(struct in_addr*)&(netAddr));
				pos += snprintf(buf + pos, packetSize - pos,
					"<TR><TD>%s</TD><TD>%0.3f ms</TD></TR>\n", 
					ringNodeStr, latencyUS / 1000.0);
			}
		}
	}	
	pos += snprintf(buf + pos, packetSize - pos, "</TBODY>\n</TABLE>\n");
	gettimeofday(&tvEnd, NULL);
	pos += snprintf(buf + pos, packetSize - pos,
		"<BR>Time to create this page is %0.2f ms\n",
		((tvEnd.tv_sec - tvStart.tv_sec) * 1000000 +  
		(tvEnd.tv_usec - tvStart.tv_usec)) / 1000.0);
	pos += snprintf(buf + pos, packetSize - pos, "\n</body>\n</HTML>");		
	inPacket.setPayLoadSize(pos);	// Reset payload size
	if (!(inPacket.completeOkay())) {
		assert(false);
		return -1;	// This should never happen
	}
	return 0;
}

int MeridianProcess::handleInfoConnections(
		fd_set* curReadSet, fd_set* curWriteSet) {			
	if (g_infoSock == -1) {
		return 0;	// Info service not started, just exit function
	}
	u_int numConnections = 0;	// Keep a counter of the number of live
								// connections in order to put a upper bound
								
	// Handle existing requests
	vector<list<pair<int, RealPacket*>*>::iterator> deleteVector;	
	list<pair<int, RealPacket*>*>::iterator conIt = g_infoConnects.begin();
	for (; conIt != g_infoConnects.end(); conIt++) {
		numConnections++;
		if (FD_ISSET((*conIt)->first, curReadSet)) {
			int recvRet = 
				recv((*conIt)->first, g_webDrainBuf, DRAIN_BUFFER_SIZE, 0);				
			if (recvRet == -1 || recvRet == 0) {				
				deleteVector.push_back(conIt);	// Error reading				
			} else {
				//	HACK: If the received first character is M, then return
				//	a binary packet with info instead of a formatted string					
				if (g_webDrainBuf[0] == 'M') {
					// Fill using info packet
					InfoPacket tmpInfo(0, getRings());
					if (tmpInfo.createRealPacket(*((*conIt)->second)) == -1) {
						deleteVector.push_back(conIt);
						continue;
					}
				} else {
					//	Fill a formatted output
					if (getInfoPacket(*((*conIt)->second)) == -1) {
						deleteVector.push_back(conIt);
						continue;
					}	
				}
				FD_CLR((*conIt)->first, &g_readSet);
				FD_SET((*conIt)->first, &g_writeSet);
			}
		} else if (FD_ISSET((*conIt)->first, curWriteSet)) {
			//	Write out the info to the browser
			RealPacket* curPacket = (*conIt)->second; 
			int sendRet = send((*conIt)->first, 
				curPacket->getPayLoad() + curPacket->getPos(), 
				curPacket->getPayLoadSize() - curPacket->getPos(), 0);
			if (sendRet == -1 || 
				sendRet == curPacket->getPayLoadSize() - curPacket->getPos()) {	
				deleteVector.push_back(conIt);
			} else {
				curPacket->incrPos(sendRet);
			}
		}
	}
	//	Check for new requests
	if (FD_ISSET(g_infoSock, curReadSet)) {	
		struct sockaddr_in tmpAddr;
		int sinSize = sizeof(struct sockaddr_in);
		int infoSock = accept(g_infoSock,
			(struct sockaddr*)&tmpAddr, (socklen_t*)&sinSize);
		if (infoSock != -1) {			
			NodeIdent remoteNode = {ntohl(tmpAddr.sin_addr.s_addr), 
									ntohs(tmpAddr.sin_port) };									
			RealPacket* inPacket = 
				new RealPacket(remoteNode, MAX_INFO_PACKET_SIZE);
			if (inPacket != NULL) {
				g_infoConnects.push_back(
					new pair<int, RealPacket*>(infoSock, inPacket));
				numConnections++;					
				//	Only allow MAX_INFO_CONNECTIONS simultaneous connections
				if (numConnections > 
						(MAX_INFO_CONNECTIONS + deleteVector.size())) {
					deleteVector.push_back(g_infoConnects.begin());				
				}
				FD_SET(infoSock, &g_readSet);
				g_maxFD = MAX(g_maxFD, infoSock);
			} else {
				ERROR_LOG("Cannot create RealPacket\n");
				close(infoSock);	
			}
		}
	}	
	//	Cleanup all finished connections
	for (u_int i = 0; i < deleteVector.size(); i++) {
		pair<int, RealPacket*>* curPair = *(deleteVector[i]);
		g_infoConnects.erase(deleteVector[i]);
		FD_CLR(curPair->first, &g_readSet);				
		FD_CLR(curPair->first, &g_writeSet);
		close(curPair->first);		
		delete curPair->second;
		delete curPair;
	}
	return 0;
}

int MeridianProcess::increaseSockBuf(int sock) {
	for (int i = 16; i >= 0; i--) {
		int sockBufMax = 1 << i;	// 32K
		int retRCV = setsockopt(sock, SOL_SOCKET, 
			SO_RCVBUF, &sockBufMax, sizeof(sockBufMax));
		int retSND = setsockopt(sock, SOL_SOCKET, 
			SO_SNDBUF, &sockBufMax, sizeof(sockBufMax));
		if (retRCV == 0 && retSND == 0) {
			WARN_LOG_1("Socket buffer size is %d\n", sockBufMax);
			break;	
		}
	}
	return 0;
}
	
int MeridianProcess::start() {
	signal(SIGPIPE, SIG_IGN);	// Ignore sigpipe		
	srand(time(NULL));	// Set random seed
	gethostname(g_hostname, HOST_NAME_MAX);	//	Get the host name of this node
	g_hostname[HOST_NAME_MAX - 1] = '\0';
	struct hostent* he = gethostbyname(g_hostname);
	if (he == NULL) {
		perror("Cannot resolve localhost\n");
		return -1;
	}
	g_localAddr = ntohl(((struct in_addr *)(he->h_addr))->s_addr);
	//	Main meridian UDP socket
	if ((g_meridSock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		perror("Cannot create UDP socket");			
		return -1;
	}
	increaseSockBuf(g_meridSock);	
	//	Set up to listen to meridian port
	struct sockaddr_in myAddr;
	myAddr.sin_family 		= AF_INET;
	myAddr.sin_port 		= htons(g_meridPort);
	myAddr.sin_addr.s_addr 	= INADDR_ANY;
	memset(&(myAddr.sin_zero), '\0', 8);
	if (bind(g_meridSock, (struct sockaddr*)&myAddr, 
			sizeof(struct sockaddr)) == -1) {
		ERROR_LOG("Cannot bind UDP socket to desired port\n");
		return -1;
	}
	//	Make the socket non-blocking
	if (setNonBlock(g_meridSock) == -1) {
		ERROR_LOG("Cannot set socket to be non-blocking\n");		
		return -1;
	}
#ifdef MERIDIAN_DSL	
	// Used only to allow scheduling of processes
	if ((g_dummySock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		perror("Cannot create UDP socket");			
		return -1;
	}
	//FD_SET(g_dummySock, &g_writeSet);
	//g_maxFD = MAX(g_dummySock, g_maxFD); 
#endif	
	//	Adding socket to read set 
	FD_SET(g_meridSock, &g_readSet);
	g_maxFD = MAX(g_meridSock, g_maxFD);
	//	Adding stop fd to read set
	FD_SET(g_stopFD, &g_readSet);
	g_maxFD = MAX(g_stopFD, g_maxFD);
	//	An info port of 0 means that no info service should be started
	if (g_infoPort > 0) { 
		//	Create listener for info requests
		if ((g_infoSock = createTCPListener(g_infoPort)) == -1) {
			ERROR_LOG("Cannot create TCP listener socket (info port)");
			return -1;			
		}
		//	Adding socket to read set 
		FD_SET(g_infoSock, &g_readSet);
		g_maxFD = MAX(g_infoSock, g_maxFD);		
	}
	//	If this node is not behind a firewall, it can potentially be a
	//	rendavous point for another node
	if (g_rendvNode.addr == 0 && g_rendvNode.port == 0) {
		if ((g_rendvListener = createTCPListener(g_meridPort)) == -1) {
			perror("Cannot create TCP listener socket (rendavous port)");					
		} else {
			FD_SET(g_rendvListener, &g_readSet);
			g_maxFD = MAX(g_rendvListener, g_maxFD);
		}
	} else {
		g_rendvFD = createRendavousTunnel(g_rendvNode);
		if (g_rendvFD != -1) {
			FD_SET(g_rendvFD, &g_readSet);
			g_maxFD = MAX(g_rendvFD, g_maxFD);
		} else {
			ERROR_LOG("FATAL: Cannot create rendavous tunnel\n");
			// TODO: Might be a better way to handle this
			return -1;
		}
	}	
#ifdef PLANET_LAB_SUPPORT
	if (createICMPSocket() == -1) {
		ERROR_LOG("Cannot create ICMP socket\n");
		return -1;
	}
	// TODO: call setuid to not be root anymore	
	//	Adding socket to read set 
	FD_SET(g_icmpSock, &g_readSet);
	g_maxFD = MAX(g_icmpSock, g_maxFD);	
#endif	
	// Add all seed nodes as ring members (performs probing)
	for (u_int i = 0; i < g_seedNodes.size(); i++) {
		NodeIdentRendv tmpNIR = g_seedNodes[i];
		if ((tmpNIR.addr != g_localAddr) || (tmpNIR.port != g_meridPort)) {
			addNodeToRing(tmpNIR);	
		} else {
			WARN_LOG("Cannot add itself as a seed node\n");	
		}
	}
	g_seedNodes.clear();	// Don't need them anymore	
	//	Perform gossip at next gossipInterval			
	SchedGossip gossipCallBack(this);
	QueryScheduler* gossipScheduler = new QueryScheduler(
		g_initGossipInterval_s * 1000, 
		g_numInitIntervalRemain, g_ssGossipInterval_s * 1000, this, 
		&gossipCallBack);
	if (g_queryTable.insertNewQuery(gossipScheduler) == -1) {		
		ERROR_LOG("Cannot add gossip scheduler\n");
		delete gossipScheduler;
	} else {
		gossipScheduler->init();	
	}
	//	Perform ring replacement at next ring replacement interval			
	SchedRingManage ringCallBack(this);
	QueryScheduler* ringScheduler = new QueryScheduler(0, 0, 
		g_replaceInterval_s * 1000, this, &ringCallBack);
	if (g_queryTable.insertNewQuery(ringScheduler) == -1) {		
		ERROR_LOG("Cannot add ring management scheduler\n");
		delete ringScheduler;
	} else {
		ringScheduler->init();	
	}
	//	Declaring structures that will be reused over and over
	fd_set currentReadSet, currentWriteSet;
	struct timeval curTime;
	struct timeval nextEventTime;
	struct timeval timeOutTV;
	//	Main event driven select loop
	while (true) {	
		//	Set timeout			
		gettimeofday(&curTime, NULL);			
		g_queryTable.nextTimeout(&nextEventTime);		
		//	Set time out length
		if (timeoutLength(&curTime, &nextEventTime, &timeOutTV) == -1) {			
			evaluateTimeout();	//	Already expired
			continue;	// Loop again
		}
		//	Reset fd_set values
		memcpy(&currentReadSet, &g_readSet, sizeof(fd_set));
		memcpy(&currentWriteSet, &g_writeSet, sizeof(fd_set));
		
		int selectRet = select(g_maxFD+1, 
			&currentReadSet, &currentWriteSet, NULL, &timeOutTV);
			
		if (selectRet == -1) {
			if (errno == EINTR) {					
				continue; // Interrupted by signal, retry
			}
			ERROR_LOG("Select returned an error\n");
			return -1;	// Return with error
		} else if (selectRet == 0) {		
			evaluateTimeout();	
			continue;
		}
		if (FD_ISSET(g_stopFD, &currentReadSet)) {
			ERROR_LOG("Received stop request\n");
			//	Don't even bother reading it
			break;			
		}
		if (FD_ISSET(g_meridSock, &currentReadSet)) {
			readPacket();	
		}
		if (FD_ISSET(g_meridSock, &currentWriteSet)) {
			writePending();
		}
#ifdef PLANET_LAB_SUPPORT
		if (FD_ISSET(g_icmpSock, &currentReadSet)) {
			WARN_LOG("ICMP Read pending!!!!\n");
			readICMPPacket();	
		}
		if (FD_ISSET(g_icmpSock, &currentWriteSet)) {
			icmpWritePending();					
		}
#endif		
#ifdef MERIDIAN_DSL		
		if (FD_ISSET(g_dummySock, &currentWriteSet)) {
			//vector<list<uint64_t>::iterator> delete_vect;
			vector<list<uint64_t>::iterator> clear_vect;
			list<uint64_t>::iterator psIt = g_psList.begin();
			vector<NodeIdentLat> dummyVect; 
#define MAX_THREADS_PER_ITERATION 	5
			for (int itCount = 0; psIt != g_psList.end() && 
					itCount < MAX_THREADS_PER_ITERATION; psIt++, itCount++) {
				uint64_t curQueryID = *psIt;
				clear_vect.push_back(psIt);
				if (getQueryTable()->isQueryInTable(curQueryID)) {
					const DSLRecvQuery* thisQ 
						= getQueryTable()->getDSLRecvQ(curQueryID);
					// If thisQ is NULL (error due to cast?), remove from list
					if (thisQ == NULL) {
						fprintf(stderr,	
							"g_psList contains a non-DSLRecvQuery query\n");
						continue;
					}
					// If thread is blocked, remove from g_psList
					if (thisQ->parserState() == PS_BLOCKED) {
						//printf("Thread is blocked, skip\n");
						continue;	
					}
					getQueryTable()->notifyQLatency(curQueryID, dummyVect);
					// If thread no longer active, remove from g_psList
					if (getQueryTable()->isQueryInTable(curQueryID)) {
						g_psList.push_back(curQueryID);												
					}															
				} 											
			}
			//	Just remove it from the list, as we moved 
			//	it to another position
			for (u_int i = 0; i < clear_vect.size(); i++) {
				g_psList.erase(clear_vect[i]);
			}			
			//	Turn off trigger if no process need to be executed
			if (g_psList.empty()) {
				FD_CLR(g_dummySock, &g_writeSet);	
			}
		}
#endif		
		if (handleRendavous(&currentReadSet, &currentWriteSet) == -1) { 
			break;	// Connection to rendavous node broken
		}
		handleInfoConnections(&currentReadSet, &currentWriteSet);
		handleTCPConnections(&currentReadSet, &currentWriteSet);
		handleDNSConnections(&currentReadSet, &currentWriteSet);
	}
	return 0;
}

int MeridianProcess::handleTCPConnections(
		fd_set* curReadSet, fd_set* curWriteSet) {			
	// Handle existing requests
	vector<map<int, pair<uint64_t, NodeIdent>*>::iterator> deleteVector;	
	map<int, pair<uint64_t, NodeIdent>*>::iterator conIt 
		= g_tcpProbeConnections.begin();
	for (; conIt != g_tcpProbeConnections.end(); conIt++) {
		if (FD_ISSET(conIt->first, curWriteSet)) {			
			struct sockaddr	peerAddr;
			socklen_t peerLen = sizeof(struct sockaddr);			
			if (getpeername(conIt->first, &peerAddr, &peerLen) != -1){			
				pair<uint64_t, NodeIdent>* thisPair = conIt->second;
				//	Pass back latency of 0, as the timing is done 
				//	within the query, not in the TCP connection
				NodeIdentLat outNIL = 
					{(thisPair->second).addr, (thisPair->second).port, 0};
				vector<NodeIdentLat> subVect;
				subVect.push_back(outNIL);				
				g_queryTable.notifyQLatency(thisPair->first, subVect);
			}
			// TODO: Add notifyError for quicker notification of error
			deleteVector.push_back(conIt);	// We can delete this now
		}
	}	
	//	Cleanup all finished connections
	for (u_int i = 0; i < deleteVector.size(); i++) {
		eraseTCPConnection(deleteVector[i]);
	}
	return 0;
}


int MeridianProcess::handleDNSConnections(
		fd_set* curReadSet, fd_set* curWriteSet) {			
	// Handle existing requests
	vector<map<int, pair<uint64_t, NodeIdent>*>::iterator> deleteVector;	
	map<int, pair<uint64_t, NodeIdent>*>::iterator conIt 
		= g_dnsProbeConnections.begin();
	for (; conIt != g_dnsProbeConnections.end(); conIt++) {		
		if (FD_ISSET(conIt->first, curReadSet)) {
			WARN_LOG("Response from DNS server\n");
			pair<uint64_t, NodeIdent>* thisPair = conIt->second;
			//	Pass back latency of 0, as the timing is done 
			//	within the query, not in the DNS connection
			NodeIdentLat outNIL = 
				{(thisPair->second).addr, (thisPair->second).port, 0};
			vector<NodeIdentLat> subVect;
			subVect.push_back(outNIL);				
			g_queryTable.notifyQLatency(thisPair->first, subVect);
			deleteVector.push_back(conIt);	// We can delete this now
		}
	}	
	//	Cleanup all finished connections
	for (u_int i = 0; i < deleteVector.size(); i++) {
		eraseDNSConnection(deleteVector[i]);
	}
	return 0;
}


int MeridianProcess::removeRendavousConnection(
		map<NodeIdent, int, ltNodeIdent>::iterator& in_it) {
	int oldSock = in_it->second;
	g_rendvConnections.erase(in_it);
	map<int, list<RealPacket*>*>::iterator queueIt =
		g_rendvQueue.find(oldSock);
	assert(queueIt != g_rendvQueue.end());
	list<RealPacket*>* packetList = queueIt->second;
	//	Remove from rendvQueue
	g_rendvQueue.erase(queueIt);
	//	Delete all RealPackets in queue		
	list<RealPacket*>::iterator listIt = packetList->begin();
	for (; listIt != packetList->end(); listIt++) {
		delete *listIt;
	}		
	delete packetList;	//	Delete the queue itself
	//	Make sure we FD_CLR and close the socket
	FD_CLR(oldSock, &g_writeSet);
	close(oldSock);	
	return 0;
}

int MeridianProcess::handleRendavous(
		fd_set* curReadSet, fd_set* curWriteSet) {
	// 	If we are behind a firewall, check to see if any new data has been
	//	pushed via the rendavous tunnel
	if (g_rendvFD != -1) {
		if (FD_ISSET(g_rendvFD, curReadSet)) {			
			int recvRet = recv(g_rendvFD, g_rendvRecvPacket->getPayLoad() + 
				g_rendvRecvPacket->getPayLoadSize(),
				g_rendvRecvPacket->getPacketSize() - 
				g_rendvRecvPacket->getPayLoadSize(), 0);				
			if (recvRet == -1 || recvRet == 0) {
				// Error reading
				ERROR_LOG("Rendavous host has disconnected\n");
				// TODO: Need to find another host
				FD_CLR(g_rendvFD, &g_readSet);
				close(g_rendvFD);
				g_rendvFD = -1;
				return -1;
			} else {
				//	Update payload size;
				g_rendvRecvPacket->setPayLoadSize(
					g_rendvRecvPacket->getPayLoadSize() + recvRet);
				//	See if we have complete PULL packet. If we do, read
				//	it and extract the inner packet. The rendv packet must also
				// 	be updated (portion of next packet must be moved to front
				//	of buffer
				NodeIdent srcNode;				
				RealPacket* newPacket 
					= PullPacket::parse(*g_rendvRecvPacket, srcNode);
				if (newPacket) {					
					handleNewPacket(newPacket->getPayLoad(), 
						newPacket->getPayLoadSize(), srcNode);
					delete newPacket;						
				}
			}						
		}
		return 0;
	}
	//	If we are a host to rendavous nodes
	//	Check for new requests. This must happen before checking existing
	//	connections, as we may modify the g_rendvConnections data-structure
	if (FD_ISSET(g_rendvListener, curReadSet)) {	
		struct sockaddr_in tmpAddr;
		int sinSize = sizeof(struct sockaddr_in);
		int newRendvSock = accept(g_rendvListener,
			(struct sockaddr*)&tmpAddr, (socklen_t*)&sinSize);
		if (newRendvSock != -1) {			
			NodeIdent remoteNode = {ntohl(tmpAddr.sin_addr.s_addr), 
									ntohs(tmpAddr.sin_port) };		
			map<NodeIdent, int, ltNodeIdent>::iterator findRendvIt = 
				g_rendvConnections.find(remoteNode);
			if (findRendvIt != g_rendvConnections.end()) {
				ERROR_LOG("Rendavous connection already exists\n");				
				ERROR_LOG("Closing existing connection\n");
				removeRendavousConnection(findRendvIt);
			}		
#ifdef DEBUG			
			u_int netAddr = htonl(remoteNode.addr);
			char* ringNodeStr = inet_ntoa(*(struct in_addr*)&(netAddr));
			WARN_LOG_2("Adding connection for %s:%d\n", 
				ringNodeStr, remoteNode.port);
#endif				
			g_rendvConnections[remoteNode] = newRendvSock;
			g_rendvQueue[newRendvSock] = new list<RealPacket*>();
		}
	}	
	//	Handle existing connections
	vector<map<NodeIdent, int, ltNodeIdent>::iterator> deleteVector;
	map<NodeIdent, int, ltNodeIdent>::iterator it =	g_rendvConnections.begin();
	for (; it != g_rendvConnections.end(); it++) {
		if (FD_ISSET(it->second, curWriteSet)) {
			//WARN_LOG("Ready to write through tunnel\n");
			map<int, list<RealPacket*>*>::iterator queueIt =
				g_rendvQueue.find(it->second);
			assert(queueIt != g_rendvQueue.end()); 				
			list<RealPacket*>* packetList = queueIt->second;			
			RealPacket* curPacket = packetList->front();
			assert(curPacket != NULL);
			int sendRet = send(it->second, 
				curPacket->getPayLoad() + curPacket->getPos(), 
				curPacket->getPayLoadSize() - curPacket->getPos(), 0);
			if (sendRet == -1 || sendRet == 0) {
				ERROR_LOG("Connection to rendvaous client closed\n");
				// Connection closed
				deleteVector.push_back(it);						
			} else if (sendRet 
					== (curPacket->getPayLoadSize() - curPacket->getPos())) {
				packetList->pop_front();
				if (packetList->empty()) {					
					FD_CLR(it->second, &g_writeSet); //	Turn off writeable	
				}
				delete curPacket;
			} else {
				curPacket->incrPos(sendRet);
			}						
		}		
	}	

	//	All dead connections are cleaned up here
	for (u_int i = 0; i < deleteVector.size(); i++) {		
		removeRendavousConnection(deleteVector[i]);	
	}
	return 0;
}

int MeridianProcess::addTCPConnection(
		uint64_t in_qid, const NodeIdent& in_remoteNode) {
	int newSock = socket(AF_INET, SOCK_STREAM, 0);
	if (newSock == -1) {
		return -1;	
	}
	if (setNonBlock(newSock) == -1) {			
		close(newSock);			
		return -1;
	}
	if (performConnect(newSock, 
			in_remoteNode.addr, in_remoteNode.port)  == -1) {				 
		close(newSock);
		return -1;
	}
	pair<uint64_t, NodeIdent>* tmp 
		= new pair<uint64_t, NodeIdent>(in_qid, in_remoteNode);
	if (tmp == NULL) {
		close(newSock);
		return -1;
	}
	map<int, pair<uint64_t, NodeIdent>*>::iterator it 
		= g_tcpProbeConnections.find(newSock);
	if (it != g_tcpProbeConnections.end()) {
		assert(false);			
	}
	g_tcpProbeConnections[newSock] = tmp;
	FD_SET(newSock, &g_writeSet);
	g_maxFD = MAX(newSock, g_maxFD); 				
	return newSock;
}


int MeridianProcess::addDNSConnection(
		uint64_t in_qid, const NodeIdent& in_remoteNode) {			
	int newSock = socket(AF_INET, SOCK_DGRAM, 0);
	if (newSock == -1) {
		return -1;	
	}	
	if (setNonBlock(newSock) == -1) {			
		close(newSock);			
		return -1;
	}
	//	Send DNS query for localhost
	RealPacket* newPacket = new RealPacket(in_remoteNode);
	if (newPacket == NULL) {
		close(newSock);
		return -1;
	}	
	int packetSize = res_mkquery(QUERY, "localhost", C_IN, T_A, NULL, 0, 0, 
		(u_char*)(newPacket->getPayLoad()), newPacket->getPacketSize());
	WARN_LOG_1("DNS packet size is %d\n", packetSize);
	if (packetSize == -1) {
		close(newSock);
		delete newPacket;
		return -1;		
	}
	newPacket->setPayLoadSize(packetSize);	
	pair<uint64_t, NodeIdent>* tmp 
		= new pair<uint64_t, NodeIdent>(in_qid, in_remoteNode);
	if (tmp == NULL) {
		close(newSock);
		delete newPacket;
		return -1;
	}
	//	Perform actual send of the packet
	if (performSend(newSock, newPacket) == -1) {
		close(newSock);
		delete newPacket;
		return -1;			
	}
	delete newPacket;	// No longer needed		
	map<int, pair<uint64_t, NodeIdent>*>::iterator it 
		= g_dnsProbeConnections.find(newSock);
	if (it != g_dnsProbeConnections.end()) {
		assert(false);			
	}
	//	Add connection to DNS map
	g_dnsProbeConnections[newSock] = tmp;
	//	Set socket to select loop, wait for response
	FD_SET(newSock, &g_readSet);
	g_maxFD = MAX(newSock, g_maxFD); 				
	return newSock;
}


#ifdef PLANET_LAB_SUPPORT

#include <netinet/ip_icmp.h>

// Taken from "Safe Planetlab Raw Sockets"
u_short MeridianProcess::in_cksum(
		const u_short *addr, register int len, u_short csum) {
	register int nleft = len;
	const u_short *w = addr;
	register u_short answer;
	register int sum = csum;
	/** Our algorithm is simple, using a 32 bit accumulator (sum),
	  * we add sequential 16 bit words to it, and at the end, fold
	  * back all the carry bits from the top 16 bits into the lower
	  * 16 bits.*/
	while (nleft > 1) {
		sum += *w++;
		nleft -= 2;
	}
	/* mop up an odd byte, if necessary */
	if (nleft == 1)
		sum += htons(*(u_char *)w << 8);
	/** add back carry outs from top 16 bits to low 16 bits*/
	sum = (sum >> 16) + (sum & 0xffff); /* add hi 16 to low 16 */
	sum += (sum >> 16); /* add carry */
	answer = ~sum; /* truncate to 16 bits */
	return (answer);
}

int MeridianProcess::createICMPSocket() {
    // Does the local port have to be coupled to the ICMP id?    
    g_icmpSock = socket(PF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (g_icmpSock == -1) {
		ERROR_LOG("Error creating ICMP socket\n");
        return -1;    
    }
	if (setuid(getuid()) == -1) {
		ERROR_LOG("Cannot lower privilege, exiting\n");
		close(g_icmpSock);
		return -1;
	}
    if (setNonBlock(g_icmpSock) == -1) {
		ERROR_LOG("Error setting the ICMP socket to be non-blocking\n");
        close(g_icmpSock);            
        return -1;
    }		
	// Use the same port number for ICMP as the Meridian port
	g_icmpPort = g_meridPort;
	//g_icmpPort = (rand() % ((1 << 16) - 1024)) + 1024;	
	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_port = htons(g_icmpPort);
	if (bind(g_icmpSock, (struct sockaddr*)&sin, sizeof(sin)) == -1) {
		ERROR_LOG("Cannot bind to ICMP socket\n");
		close(g_icmpSock);
		return -1;
	}	
    int tmpOpt = 1; // Flag set to true
    if (setsockopt(g_icmpSock, 0, IP_HDRINCL, &tmpOpt, sizeof(tmpOpt)) == -1) {
		ERROR_LOG("Cannot setsockopt on ICMP socket\n");
		close(g_icmpSock);
		return -1;		
	}
	return 0;
}

int MeridianProcess::sendICMPProbe(
		uint64_t in_qid, uint32_t in_remoteNode) {	
	// Set the remote port to be g_icmpPort for ICMP packets
	NodeIdent tmpRemote = {in_remoteNode, g_icmpPort};    
	// Payload is the query id
	uint16_t icmpPacketSize = sizeof(struct iphdr) +
		sizeof(struct icmphdr) + sizeof(uint64_t);    
	// Send ICMP query for localhost
	RealPacket* newPacket = new RealPacket(tmpRemote, icmpPacketSize);
	if (newPacket == NULL) {
		ERROR_LOG("Cannot create new packet in sendICMPProbe\n");
		return -1;
	}
	// Payload size the same size as packet size
	newPacket->setPayLoadSize(icmpPacketSize);
	// Clear packet first (may not be necessary actually)
	memset(newPacket->getPayLoad(), '\0', newPacket->getPacketSize());
	// Set IP fields, lots of magic numbers (taken from Bickson docs)
	struct iphdr* ip_header = (struct iphdr*) newPacket->getPayLoad();
	ip_header->ihl = 5;
	ip_header->version = 4;
	ip_header->tos = 0;
	ip_header->tot_len = htons(icmpPacketSize);
	ip_header->id = rand();
	ip_header->ttl = 64;
	ip_header->frag_off = 0x40;
	ip_header->protocol = IPPROTO_ICMP;
	ip_header->check = 0;     // Set by the kernel
	ip_header->daddr = htonl(in_remoteNode);
	ip_header->saddr = 0;    // Source addr blank, set by kernel
	// Set ICMP fields
	struct icmphdr* icmp_header =
		(struct icmphdr*)(newPacket->getPayLoad() + sizeof(struct iphdr));
	icmp_header->type = ICMP_ECHO;
	icmp_header->code = 0;
	icmp_header->un.echo.id = htons(g_icmpPort);
	icmp_header->un.echo.sequence = htons(g_icmpSeq++);
	// Set qid into network order before adding to packet
	uint32_t qid_1, qid_2;
	Packet::to32(in_qid, &qid_1, &qid_2);	
	qid_1 = htonl(qid_1);	// Write the top 32bits
	memcpy(newPacket->getPayLoad() + sizeof(struct iphdr) +
		sizeof(struct icmphdr), &qid_1, sizeof(uint32_t));		
	qid_2 = htonl(qid_2);	// Write the bottom 32bits
	memcpy(newPacket->getPayLoad() + sizeof(struct iphdr) +
		sizeof(struct icmphdr) + sizeof(uint32_t), &qid_2, sizeof(uint32_t));	
	// Calculate and write ICMP checksum
	icmp_header->checksum = in_cksum((const uint16_t*)icmp_header,
		sizeof(struct icmphdr) + sizeof(uint64_t), 0);
	// Push to ICMP waiting queue
	addICMPOutPacket(newPacket);
	return 0;    
}

int MeridianProcess::addICMPOutPacket(RealPacket* in_packet) {
	g_icmpOutPacketList.push_back(in_packet);
	FD_SET(g_icmpSock, &g_writeSet);
	g_maxFD = MAX(g_icmpSock, g_maxFD); 
	return 0;
}

void MeridianProcess::icmpWritePending() {
	while (true) {
		assert(!(g_icmpOutPacketList.empty()));
		RealPacket* firstPacket = g_icmpOutPacketList.front();
		if (performSendICMP(g_icmpSock, firstPacket) == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {									
				break; // Retry again later when ready to send
			} else {
				//	Let's just continute still, but remove this packet
				ERROR_LOG("Error calling send\n");	
			}
		}		
		g_icmpOutPacketList.pop_front();
		delete firstPacket;	// Done with packet
		if (g_icmpOutPacketList.empty()) {
			FD_CLR(g_icmpSock, &g_writeSet);
			break;	// No more to send
		}
	}
}

#define MAX_ICMP_PACKET_SIZE 	1400 

int MeridianProcess::readICMPPacket() {
	char buf[MAX_ICMP_PACKET_SIZE];
	struct sockaddr_in theirAddr;
	int addrLen = sizeof(struct sockaddr);
	//	Perform actual recv on socket
	int numBytes = recvfrom(g_icmpSock, buf, MAX_ICMP_PACKET_SIZE, 0,
		(struct sockaddr*)&theirAddr, (socklen_t*)&addrLen);		
	if (numBytes == -1) {
		perror("Error on recvfrom");
		return -1;		
	}
	NodeIdent remoteNode = {ntohl(theirAddr.sin_addr.s_addr), 0};							
	// Get query id from ICMP ECHO reply
    uint16_t icmpPacketSize = sizeof(struct iphdr) +
        sizeof(struct icmphdr) + sizeof(uint64_t);		
	if (numBytes != icmpPacketSize) {
		//ERROR_LOG("Received ICMP packet size incorrect\n");
		WARN_LOG("Received unexpected ICMP packet\n");
		return -1;
	}
	// Check that it is an ICMP packet
	struct iphdr* ip_header = (struct iphdr*)buf;
	if (ip_header->protocol != IPPROTO_ICMP) {
		WARN_LOG("Received non-ICMP packet from ICMP socket\n");
		return -1;
	}
	struct icmphdr* icmp_header = 
		(struct icmphdr*)(buf + sizeof(struct iphdr));
	// Check to see it is a ECHO reply		
	if (icmp_header->type != ICMP_ECHOREPLY) {
		WARN_LOG("Received non-ICMP reply packet\n");
		return -1;
	}
	// Check to see that it is expected
	if (icmp_header->un.echo.id != htons(g_icmpPort)) {
		ERROR_LOG("Received unexpected ICMP reply\n");
		return -1;		
	}
	// NOTE: Should probably check checksum in the future
    // Get the qid of the ICMP packet
	uint32_t qid_1, qid_2;
    memcpy(&qid_1, buf + sizeof(struct iphdr) + 
		sizeof(struct icmphdr), sizeof(uint32_t));
    memcpy(&qid_2, buf + sizeof(struct iphdr) + 
		sizeof(struct icmphdr) + sizeof(uint32_t), sizeof(uint32_t));		
	uint64_t qid_no = Packet::to64(ntohl(qid_1), ntohl(qid_2));	
	WARN_LOG_1("Received qid of value %llu\n", qid_no);
	// Notify the query with this qid of the ICMP packet
	g_queryTable.notifyQPacket(qid_no, remoteNode, buf, numBytes);
	return 0;
}

int MeridianProcess::performSendICMP(int sock, RealPacket* in_packet) {
#ifdef DEBUG	
	u_int netAddr = htonl(in_packet->getAddr());
	char* ringNodeStr = inet_ntoa(*(struct in_addr*)&(netAddr));	
	WARN_LOG_3("Sending query to port number %s:%d of size %d\n", 
		ringNodeStr, in_packet->getPort(), in_packet->getPayLoadSize());
#endif		
	struct sockaddr_in hostAddr;
	memset(&(hostAddr), '\0', sizeof(struct sockaddr_in));
	hostAddr.sin_family         = PF_INET;
	hostAddr.sin_port           = htons(in_packet->getPort());
	hostAddr.sin_addr.s_addr    = htonl(in_packet->getAddr());
	//memset(&(hostAddr.sin_zero), '\0', 8);
	int sendRet = sendto(sock, in_packet->getPayLoad(),
		in_packet->getPayLoadSize(), 0,
		(struct sockaddr*)&hostAddr, sizeof(struct sockaddr));
	return sendRet;
}

#endif

