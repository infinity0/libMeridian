#ifndef CLASS_MERIDIAN_PROCESS
#define CLASS_MERIDIAN_PROCESS

#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <vector>
#include <list>
#include <sys/socket.h>
#include "QueryTable.h"
#include "RingSet.h"
#include "Marshal.h"
#include "LatencyCache.h"

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX			1024
#endif
#define TCP_LISTEN_BACKLOG		32
#define MAX_INFO_PACKET_SIZE	65536
#define MAX_INFO_CONNECTIONS	16
#define DRAIN_BUFFER_SIZE		65536
#define	PROBE_CACHE_SIZE		1024
#define PROBE_CACHE_TIMEOUT_US	(5*1000*1000)

//	Contains the majority of the non-membership state of the node
class MeridianProcess {
private:	
	u_short 	g_meridPort;		// Main UDP port that meridian operates on
	u_short 	g_infoPort;			// TCP port that Meridian distributes 
									// ring information
	int			g_meridSock;		// Main UDP socket
	int			g_infoSock;			// Socket for info port
	int			g_rendvFD;
	int			g_rendvListener;  	// TODO: Listener port for accepting 
									// rendavous connections
									
	u_int		g_initGossipInterval_s;		// Initial gossip period
	u_int		g_numInitIntervalRemain;	// Number of initial gossip periods
	u_int		g_ssGossipInterval_s;		// Steady state gossip period
	u_int		g_replaceInterval_s;		// Ring replacement period
	
	fd_set		g_readSet;			// Read set for main select loop
	fd_set 		g_writeSet;			// Write set for main select loop
	int			g_maxFD;			// Keeps track of the largest FD in select	
	NodeIdent	g_rendvNode;		// Rendavous node for this node. {0,0} means
									// this node does not need one
									
	RingSet*	g_rings;			// Rings for this node		
	uint32_t	g_localAddr;		// IP address of this node
	int			g_stopFD;			// File descriptor used to stop the process
	QueryTable	g_queryTable;		// Table that keeps track of all active 
									// queries
									
	LatencyCache* g_tcpCache;		// Latency cache for past tcp probes				
	LatencyCache* g_dnsCache;		// Latency cache for past dns probes
	LatencyCache* g_pingCache;		// Latency cache for past merid ping probes
#ifdef PLANET_LAB_SUPPORT	
	LatencyCache* g_icmpCache;
#endif	
						
	char g_webDrainBuf[DRAIN_BUFFER_SIZE];	// A temp buffer												
	char g_hostname[HOST_NAME_MAX];			// Host name of this node
	
	//	Holds all out packets that are ready to be sent
	list<RealPacket*>				g_outPacketList;
	
	//	Contains the <fd, packet> pair of a info connection
	list<pair<int, RealPacket*>*> 	g_infoConnects;
	
	//	Contain all the initial seed nodes that node knows of. TODO: May not
	//	need to keep this for the entire lifetime
	vector<NodeIdentRendv>			g_seedNodes;
	
	//	Contains all the information for handling a tcp/dns probe request. The
	//	key is the fd, and the value is the queryid and the probe target
	map<int, pair<uint64_t, NodeIdent>*> g_tcpProbeConnections;
	map<int, pair<uint64_t, NodeIdent>*> g_dnsProbeConnections;

	//	Key is the destination, value is the socket
	map<NodeIdent, int, ltNodeIdent> 	g_rendvConnections;
	map<int, list<RealPacket*>*> 		g_rendvQueue;
	RealPacket*							g_rendvRecvPacket; 
	
#ifdef MERIDIAN_DSL	
	int									g_dummySock;
	list<uint64_t> 						g_psList;
	uint16_t							g_max_ttl;
#endif

#ifdef PLANET_LAB_SUPPORT
	list<RealPacket*> 					g_icmpOutPacketList;
	int 								g_icmpSock;
	uint16_t							g_icmpPort;
	uint16_t							g_icmpSeq;
#endif
	
private:
	//	Create a TCP listener and return the FD
	static int createTCPListener(u_short port);
	
	//	Performs a TCP connect on the socket to the addr:port
	static int performConnect(int sendSock, uint32_t addr, uint16_t port);
	
	//	Sends a ping packet to the remote node and adds it to the ring set
	int addNodeToRing(const NodeIdentRendv& in_remote);	
	
	//	Handle packets read from Meridian port
	int readPacket();	
	int handleNewPacket(char* buf, int numBytes, const NodeIdent& remoteNode);	
	
	//	Handle all pending writes to the Meridian port
	void writePending();
	
	//	Called when a timeout has occured. Determines what actions need to be
	//	performed due to the timeout
	int evaluateTimeout();
	
	//	Handle all active info/TCP/DNS connections
	int handleInfoConnections(fd_set* curReadSet, fd_set* curWriteSet);
	int handleTCPConnections(fd_set* curReadSet, fd_set* curWriteSet);
	int handleDNSConnections(fd_set* curReadSet, fd_set* curWriteSet);
	int handleRendavous(fd_set* curReadSet, fd_set* curWriteSet);	
	
	//	Creates an information packet	
	int getInfoPacket(RealPacket& inPacket);
	
	//	Handles all types of measurement request packets
	//	NOTE: Has to be in the header file unfortunately, due to template	
	template <class U, class T> int handleMeasureReq(
			const NodeIdent& remoteNode, const char* buf, int numBytes) {
		WARN_LOG("Received a REQ_PING/REQ_PING_TCP packet\n");
		ReqGeneric* tmp = ReqGeneric::parse<U>(buf, numBytes);
		if (tmp != NULL) {
			const vector<NodeIdent>* tmpVect =
				tmp->returnTargets();
			if (tmpVect->size() == 0) {
				// If num targets is 0, just return empty RET_PING
				RetPing retPacket(tmp->retReqID());
				NodeIdentRendv rNodeRendv = { 
					remoteNode.addr, remoteNode.port, 
					tmp->getRendvAddr(), tmp->getRendvPort() };														
				RealPacket* inPacket = 
					new RealPacket(rNodeRendv);
				if (retPacket.createRealPacket(*inPacket) == -1) {
					delete inPacket;						
				} else {
					addOutPacket(inPacket);
				}									
			} else {
				WARN_LOG("Parsed correctly by ReqMeasureGeneric\n");
				NodeIdentRendv rNodeRendv = { 
					remoteNode.addr, remoteNode.port, 
					tmp->getRendvAddr(), tmp->getRendvPort() };
				Query* reqQ = 
					new T(tmp->retReqID(), rNodeRendv, *tmpVect, this);
				if (g_queryTable.insertNewQuery(reqQ) == -1) {			
					delete reqQ;
				} else {
					reqQ->init();
				}
			}
			delete tmp;	// Finished with the REQ_PING packet
		}
		return 0;				
	}
	
	//	Handles all types of closest node request packets
	//	NOTE: Has to be in the header file unfortunately, due to template		
	template <class U, class T> int handleClosestReq(uint64_t queryID, 
			const NodeIdent& remoteNode, const char* buf, int numBytes) {		
		if (g_queryTable.isQueryInTable(queryID)) {
			//	This is only to check for routing loops
			g_queryTable.notifyQPacket(
				queryID, remoteNode, buf, numBytes);						
		} else {									
			ReqClosestGeneric* tmp = ReqClosestGeneric::
				parse<U>(buf, numBytes);						
			if (tmp != NULL) {						
				//	Add remote node to ring
				NodeIdentRendv remoteNodeRendv = { 
					remoteNode.addr, remoteNode.port, 
					tmp->getRendvAddr(), tmp->getRendvPort() };
				T* newQuery	= new T(queryID, tmp->getBetaNumerator(), 
						tmp->getBetaDenominator(), remoteNodeRendv,
						*(tmp->returnTargets()), this);						
				if (newQuery != NULL) {
					if (g_queryTable.insertNewQuery(newQuery) == -1) {			
						delete newQuery;								
					} else {
						newQuery->init();
					}
				}
				delete tmp;	// Finished with gossip packet						
			}
		}
		return 0;		
	}

	//	Handles all types of multi-constraint request packets
	//	NOTE: Has to be in the header file unfortunately, due to template		
	template <class U, class T> int handleMCReq(uint64_t queryID, 
			const NodeIdent& remoteNode, const char* buf, int numBytes) {		
		if (g_queryTable.isQueryInTable(queryID)) {
			//	This is only to check for routing loops
			g_queryTable.notifyQPacket(
				queryID, remoteNode, buf, numBytes);						
		} else {									
			ReqConstraintGeneric* tmp = ReqConstraintGeneric::
				parse<U>(buf, numBytes);						
			if (tmp != NULL) {						
				//	Add remote node to ring
				NodeIdentRendv remoteNodeRendv = { 
					remoteNode.addr, remoteNode.port, 
					tmp->getRendvAddr(), tmp->getRendvPort() };
				T* newQuery	= new T(queryID, tmp->getBetaNumerator(), 
						tmp->getBetaDenominator(), remoteNodeRendv,
						*(tmp->returnTargets()), this);						
				if (newQuery != NULL) {
					if (g_queryTable.insertNewQuery(newQuery) == -1) {			
						delete newQuery;								
					} else {
						newQuery->init();
					}
				}
				delete tmp;	// Finished with gossip packet						
			}
		}
		return 0;		
	}	
	
	//	Calculates the duration of the timeout given the current time
	//	and the next timeout time
	static int timeoutLength(struct timeval* curTime, 
		struct timeval* nextEventTime, struct timeval* timeOutTV);	
		
	//	Erase a TCP/DNS connection and clean all resources given an iterator
	//	to the entry in the tcp/dns connection map
	int eraseTCPConnection(
		const map<int, pair<uint64_t, NodeIdent>*>::iterator& conIt);	
	int eraseDNSConnection(
		const map<int, pair<uint64_t, NodeIdent>*>::iterator& conIt);
		
	int createRendavousTunnel(const NodeIdent& rendvNode);
	
	int removeRendavousConnection(	
		map<NodeIdent, int, ltNodeIdent>::iterator& in_it);
		
#ifdef PLANET_LAB_SUPPORT
	static u_short in_cksum(
		const u_short *addr, register int len, u_short csum);	
	int createICMPSocket();	
	int addICMPOutPacket(RealPacket* in_packet);	
	void icmpWritePending();	
	int readICMPPacket();
#endif
	
public:
	
	// Only constructor for a meridian process. Must specify meridian port,
	// ring size, exponetial base, and file descriptor that causes the process
	// to end. NOTE: An info_port value of 0 means that the information 
	// service is not started.
	MeridianProcess(u_short meridian_port, u_short info_port, u_int prim_size,
					u_int second_size, int ring_base, int stopFD);
	
	// Default destructor, cleans up all resources and closes all sockets
	~MeridianProcess();
	
	//	Insert a probe entry into the tcp/dns/ping cache
	int tcpCacheInsert(const NodeIdent& inNode, uint32_t latencyUS) {
		return (g_tcpCache->insertMeasurement(inNode, latencyUS));	
	}	
	int dnsCacheInsert(const NodeIdent& inNode, uint32_t latencyUS) {
		return (g_dnsCache->insertMeasurement(inNode, latencyUS));	
	}
	int pingCacheInsert(const NodeIdent& inNode, uint32_t latencyUS) {
		return (g_pingCache->insertMeasurement(inNode, latencyUS));	
	}
#ifdef PLANET_LAB_SUPPORT
	int icmpCacheInsert(const NodeIdent& inNode, uint32_t latencyUS) {
		// No real port information, just set port to 0
		NodeIdent tmpNode = {inNode.addr, 0};
		return (g_icmpCache->insertMeasurement(tmpNode, latencyUS));	
	}
#endif

	//	Get latency information from cache to avoid probing
	int tcpCacheGetLatency(const NodeIdent& inNode, uint32_t* latencyUS) {
		return (g_tcpCache->getLatency(inNode, latencyUS));
	}	
	int dnsCacheGetLatency(const NodeIdent& inNode, uint32_t* latencyUS) {
		return (g_dnsCache->getLatency(inNode, latencyUS));
	}
	int pingCacheGetLatency(const NodeIdent& inNode, uint32_t* latencyUS) {
		return (g_pingCache->getLatency(inNode, latencyUS));
	}
#ifdef PLANET_LAB_SUPPORT
	int icmpCacheGetLatency(const NodeIdent& inNode, uint32_t* latencyUS) {
		// No real port information, just set port to 0
		NodeIdent tmpNode = {inNode.addr, 0};
		return (g_icmpCache->getLatency(tmpNode, latencyUS));
	}
#endif	
	
	//	Sets a socket to be non blocking
	static int setNonBlock(int fd);
	
	//	Increase the socket buffer to 64K (if that fails, keeps trying at 32K,
	//	then 16K, etc. until it succeeds)	
	static int increaseSockBuf(int sock);
	
	//	Sends a RealPacket using the provided socket
	static int performSend(int sock, RealPacket* in_packet);
#ifdef PLANET_LAB_SUPPORT	
	static int performSendICMP(int sock, RealPacket* in_packet);
#endif
	
	//	Allows queries that get access to the query table and ring set if they
	//	have access to the meridian process.
	//	TODO: These break abstractions, need to re-factor later 					
	QueryTable* getQueryTable() 	{ return &g_queryTable;	}	
	RingSet* getRings() 			{ return g_rings; 		}
	
	//	Add a new TCP/DNS connection that is keyed on the qid to the 
	//	provided remoteNode
	int addTCPConnection(uint64_t in_qid, const NodeIdent& in_remoteNode);	
	int addDNSConnection(uint64_t in_qid, const NodeIdent& in_remoteNode);
	
	//	Remove TCP/DNS connection of the given socket
	int eraseTCPConnection(int in_sock);	
	int eraseDNSConnection(int in_sock);
	
	//	Pushs a packet to the send queue	
	int addOutPacket(RealPacket* in_packet);
	
	//	Get a querid id that is not currently in use
	uint64_t getNewQueryID();
	
	//	Sets the gossip interval
	void setGossipInterval(u_int initial_s,	
			u_int initial_length, u_int steady_state_s) {
		g_initGossipInterval_s = initial_s;
		g_numInitIntervalRemain = initial_length;
		g_ssGossipInterval_s = steady_state_s;
	}
	
	//	Sets the ring replacement interval
	void setReplaceInterval(u_int seconds) 	{ 
		g_replaceInterval_s = seconds; 
	}
	
	//	Sets the rendavous node for this node. {0,0} means no rendavous node
	void setRendavousNode(uint32_t addr, uint16_t port) {
		g_rendvNode.addr = addr;
		g_rendvNode.port = port;
	}
	
	//	Add seed nodes 
	void addSeedNode(uint32_t addr, uint16_t port) {
		NodeIdentRendv tmp = {addr, port, 0, 0};
		g_seedNodes.push_back(tmp);
	}
	
	//	Return the rendavous node for this node	
	NodeIdent returnRendv() {
		return g_rendvNode;	
	}	
	
	//	Start a gossip session
	int performGossip();
	
	//	Start a ring management session
	int performRingManagement();		
	
	//	Starts the meridian process. Process blocks until the stopFD is written
	//	NOTE: Calls to addSeedNode, setReplaceInterval, and setGossipInterval 
	//	are ignored after a call to start (this may change in the future)
	int start();
#ifdef MERIDIAN_DSL
	void addPS(uint64_t in_id) {
		g_psList.push_back(in_id);
		FD_SET(g_dummySock, &g_writeSet);
		g_maxFD = MAX(g_dummySock, g_maxFD);		
	}
#define DEFAULT_MAX_TTL		500	
	void setMaxTTL(uint16_t in_ttl) {
		g_max_ttl = in_ttl;	
	}
#endif

#ifdef PLANET_LAB_SUPPORT
	int sendICMPProbe(uint64_t in_qid, uint32_t in_remoteNode);
#endif

};

#endif
