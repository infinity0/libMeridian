#ifndef CLASS_QUERY
#define CLASS_QUERY

#include <stdint.h>
#include <sys/time.h>
#include <assert.h>
#include <map>
#include <set>
#include "RingSet.h"
#include "Marshal.h"

extern "C" {
	#include <qhull/qhull.h>
	#include <atlas/cblas.h>	
}

class MeridianProcess;

#define MICRO_IN_MILLI	1000
#define MINI_IN_SECOND	1000
#define MICRO_IN_SECOND	1000000
#define MAX_RTT_MS		5000

class SchedObject {
public:
	virtual int runOnce() = 0;
	virtual ~SchedObject() {}
};

class SchedGossip : public SchedObject {
private:	
	MeridianProcess* meridProcess;
public:
	SchedGossip(MeridianProcess* in_process) : meridProcess(in_process) {}
	virtual int runOnce();
	virtual ~SchedGossip() {}
};

class SchedRingManage : public SchedObject {
private:	
	MeridianProcess* meridProcess;
public:
	SchedRingManage(MeridianProcess* in_process) : meridProcess(in_process) {}
	virtual int runOnce();
	virtual ~SchedRingManage() {}
};

//	Base interface class 
class Query {
protected:
	static void computeTimeout(
			u_int periodUS, struct timeval* nextTimeOut) {
		struct timeval curTime;
		gettimeofday(&curTime, NULL);	
		struct timeval offsetTV = 
				{ periodUS / MICRO_IN_SECOND, periodUS % MICRO_IN_SECOND}; 			
		timeradd(&curTime, &offsetTV, nextTimeOut);				
	}
	
public:
	virtual uint64_t getQueryID() const = 0;	
	virtual struct timeval timeOut() const = 0;
	virtual int init() = 0;
	virtual int handleEvent(
		const NodeIdent& in_remote, const char* inPacket, int packetSize) = 0;
	// Used primarily between queries
	virtual int handleLatency(
		const vector<NodeIdentLat>& in_remoteNodes) = 0;
	virtual int handleTimeout() = 0;
	virtual bool isFinished() const = 0;
	virtual int subscribeLatency(uint64_t in_qid) 	{ 
		ERROR_LOG("Unhandled subscribeLatency call\n"); 
		return 0; 
	}
	virtual ~Query() {}	
};

class AddNodeQuery : public Query {
private:
	uint64_t			qid;
	NodeIdentRendv		remoteNode;
	bool 				finished;
	struct timeval		startTime;
	struct timeval		timeoutTV;
	MeridianProcess*	meridProcess;
	vector<uint64_t>	subscribers;	
public:
	AddNodeQuery(const NodeIdentRendv& in_remote, 
				MeridianProcess* in_process);
	virtual ~AddNodeQuery() {}	
	virtual uint64_t getQueryID() const				{ return qid;		}
	virtual struct timeval timeOut() const			{ return timeoutTV;	} 	
	virtual int handleEvent(
		const NodeIdent& in_remote, 
		const char* inPacket, int packetSize);
	virtual int handleLatency(
		const vector<NodeIdentLat>& in_remoteNodes);
	virtual int handleTimeout();
	virtual bool isFinished() const					{ return finished;	}		
	virtual int init();
	virtual int subscribeLatency(uint64_t in_qid);
};

class GossipQuery : public Query {
private:
	uint64_t			qid;	
	//NodeIdent			remoteNode;
	NodeIdentRendv		remoteNode;
	bool 				finished;
	struct timeval		startTime;
	struct timeval		timeoutTV;
	MeridianProcess*	meridProcess;	
public:
	GossipQuery(NodeIdentRendv& in_remote, MeridianProcess* in_process);
	virtual ~GossipQuery() {}	
	virtual uint64_t getQueryID() const				{ return qid;		}
	virtual struct timeval timeOut() const			{ return timeoutTV;	} 	
	virtual int handleEvent(
		const NodeIdent& in_remote, 
		const char* inPacket, int packetSize)		{ return 0;			}
	virtual int handleLatency(
		const vector<NodeIdentLat>& in_remoteNodes);
	virtual int handleTimeout();
	virtual bool isFinished() const					{ return finished;	}
	virtual int init();
	static int fillGossipPacket(GossipPacketGeneric& in_packet, 
		const NodeIdentRendv& in_target, MeridianProcess* in_merid);
};

class SearchQuery : public Query {
private:
	uint64_t qid;
public :
	SearchQuery(uint64_t id) : qid(id) {}
	virtual ~SearchQuery() {}	
	virtual uint64_t getQueryID() const				{ return qid;		}
	virtual struct timeval timeOut() const			{ assert(false);	}	
	virtual int handleEvent(
		const NodeIdent& in_remote, 
		const char* inPacket, int packetSize) 		{ assert(false);	}	
	virtual int handleLatency(
		const vector<NodeIdentLat>& in_remoteNodes)	{ assert(false); 	}
	virtual int handleTimeout()						{ assert(false); 	}
	virtual bool isFinished() const					{ assert(false);	}
	virtual int init()								{ assert(false);	}	
};


class QueryScheduler : public Query {
private:	
	uint64_t			qid;
	SchedObject*		schedObj;
	struct timeval		timeoutTV;
	u_int				initInterval_MS;
	u_int				numInitInterval;
	u_int				ssInterval_MS;
	MeridianProcess* 	meridProcess;	
	bool				finished;	
	void computeSchedTimeout();
public:
	QueryScheduler(u_int in_initInterval_MS, u_int in_numInitInterval, 
				u_int in_ssInterval_MS, MeridianProcess* in_process, 
				SchedObject* in_schedObj);
	virtual ~QueryScheduler() {}	 
	virtual uint64_t getQueryID() const				{ return qid;		}
	virtual struct timeval timeOut() const			{ return timeoutTV;	}	
	virtual int handleEvent(
		const NodeIdent& in_remote, 
		const char* inPacket, int packetSize) 		{ return 0;			}	
	virtual int handleLatency(
		const vector<NodeIdentLat>& in_remoteNodes)	{ return 0;			}
	virtual int handleTimeout();
	virtual bool isFinished() const					{ return finished;	}
	virtual int init() 								{ return 0;			}
	int removeScheduler();
};

class RingManageQuery : public Query {
private:
	uint64_t						qid;
	int								ringNum;
	set<NodeIdent, ltNodeIdent>		remoteNodes;	
	bool 							finished;
	struct timeval					startTime;
	struct timeval					timeoutTV;
	MeridianProcess*				meridProcess;
	map<NodeIdent, map<NodeIdent, u_int, ltNodeIdent>*, ltNodeIdent> RetNodeMap;
	
	int performReplacement();
	double* createLatencyMatrix(); 		
	int removeCandidateNode(const NodeIdent& in_node);
	double getVolume(coordT* points, int dim, int numpoints);
	double calculateHV(
		const int N,					// Physical size of the latencyMatrix
		const int NPrime,				// Size of the latencyMatrix in use
		double* latencyMatrix);			// Pointer to latencyMatrix
	double reduceSetByN(
		vector<NodeIdent>& inVector,	// Vector of nodes
		vector<NodeIdent>& deletedNodes,
		int numReduction,				// How many nodes to remove
		double* latencyMatrix);			// Pointer to latencyMatrix			
public:
	RingManageQuery(int in_ringNum,	MeridianProcess* in_process);
	virtual ~RingManageQuery() {
		map<NodeIdent, map<NodeIdent, u_int, ltNodeIdent>*, ltNodeIdent>::
			iterator it = RetNodeMap.begin();
		for (; it != RetNodeMap.end(); it++) {
			if (it->second != NULL) {
				delete it->second;	// Delete all entries
			}
		}				
	}	
	virtual uint64_t getQueryID() const				{ return qid;		}
	virtual struct timeval timeOut() const			{ return timeoutTV;	} 	
	virtual int handleEvent(
		const NodeIdent& in_remote, 
		const char* inPacket, int packetSize);	
	virtual int handleLatency(
		const vector<NodeIdentLat>& in_remoteNodes)	{ return 0;			}
	virtual int handleTimeout();
	virtual bool isFinished() const					{ return finished;	}
	virtual int init();		
};


class ProbeQueryGeneric : public Query {
private:
	int					sockFD;
	uint64_t 			qid;
	NodeIdent			remoteNode;
	bool 				finished;
	struct timeval		startTime;
	struct timeval		timeoutTV;
	MeridianProcess*	meridProcess;
	vector<uint64_t>	subscribers;	
protected:
	NodeIdent getRemoteNode() const		{ return remoteNode;				}
	void setSockFD(int fd)				{ sockFD = fd;						}
	int getSockFD()	const				{ return sockFD;					}
	struct timeval getStartTime() const	{ return startTime;					}
	void setStartTime() 				{ gettimeofday(&startTime, NULL); 	}		
	MeridianProcess* getMerid() 		{ return meridProcess; 				}
	void setFinished(bool flag)			{ finished = flag;					}

	virtual void insertCache(const NodeIdent& inNode, uint32_t latencyUS) = 0;
	
public:
	ProbeQueryGeneric(const NodeIdent& in_remote, MeridianProcess* in_process);				
	virtual ~ProbeQueryGeneric() {}	
	virtual uint64_t getQueryID() const				{ return qid;		}
	virtual struct timeval timeOut() const			{ return timeoutTV;	} 	
	virtual int handleEvent(
		const NodeIdent& in_remote, 
		const char* inPacket, int packetSize);
	virtual int handleLatency(
		const vector<NodeIdentLat>& in_remoteNodes);
	virtual int handleTimeout() = 0;
	virtual bool isFinished() const					{ return finished;	}		
	virtual int init() = 0;					
	virtual int subscribeLatency(uint64_t in_qid);
};

class ProbeQueryTCP : public ProbeQueryGeneric {
protected:
	virtual void insertCache(const NodeIdent& inNode, uint32_t latencyUS);
public:
	ProbeQueryTCP(const NodeIdent& in_remote, MeridianProcess* in_process)
		: ProbeQueryGeneric(in_remote, in_process) {}				
	virtual ~ProbeQueryTCP() {}	
	virtual int handleTimeout();			
	virtual int init();					
};

class ProbeQueryDNS : public ProbeQueryGeneric {
protected:	
	virtual void insertCache(const NodeIdent& inNode, uint32_t latencyUS);
public:
	ProbeQueryDNS(const NodeIdent& in_remote, MeridianProcess* in_process)
		: ProbeQueryGeneric(in_remote, in_process) {}				
	virtual ~ProbeQueryDNS() {}	
	virtual int handleTimeout();			
	virtual int init();					
};

class ProbeQueryPing : public ProbeQueryGeneric {
protected:	
	virtual void insertCache(const NodeIdent& inNode, uint32_t latencyUS);	
public:
	ProbeQueryPing(const NodeIdent& in_remote, MeridianProcess* in_process)
		: ProbeQueryGeneric(in_remote, in_process) {}				
	virtual ~ProbeQueryPing() {}	
	virtual int handleTimeout();			
	virtual int init();					
};

#ifdef PLANET_LAB_SUPPORT
class ProbeQueryICMP : public ProbeQueryGeneric {
protected:	
	virtual void insertCache(const NodeIdent& inNode, uint32_t latencyUS);	
public:
	ProbeQueryICMP(const NodeIdent& in_remote, MeridianProcess* in_process)
		: ProbeQueryGeneric(in_remote, in_process) {}				
	virtual ~ProbeQueryICMP() {}	
	virtual int handleTimeout();			
	virtual int init();					
};
#endif


class HandleReqGeneric : public Query {
private:		
	uint64_t							qid;	
	NodeIdentRendv						srcNode;
	bool 								finished;
	//struct timeval						startTime;
	struct timeval						timeoutTV;
	MeridianProcess*					meridProcess;
	set<NodeIdent, ltNodeIdent>			remoteNodes;	
	map<NodeIdent, u_int, ltNodeIdent>	remoteLatencies;
protected:
	NodeIdentRendv getSrcNode()						{ return srcNode;		}
	set<NodeIdent, ltNodeIdent>* getRemoteNodes() 	{ return &remoteNodes; 	}
	MeridianProcess* getMerid() 					{ return meridProcess; 	}
	void setFinished(bool flag)						{ finished = flag;		}
	//void setStartTime() 				{ gettimeofday(&startTime, NULL); 	}
	
	virtual ProbeQueryGeneric* createProbeQuery(const NodeIdent& in_remote, 
		MeridianProcess* in_process) = 0;
		
	virtual int getLatency(const NodeIdent& inNode, uint32_t* latencyUS) = 0;
		
public:
	HandleReqGeneric(uint64_t id, const NodeIdentRendv& in_srcNode, 
					const vector<NodeIdent>& in_remote, 
					MeridianProcess* in_process);
	virtual ~HandleReqGeneric() {}	
	virtual uint64_t getQueryID() const				{ return qid;		}
	virtual struct timeval timeOut() const			{ return timeoutTV;	} 	
	virtual int handleEvent(
		const NodeIdent& in_remote, 
		const char* inPacket, int packetSize)		{ return 0;			}		
	virtual int handleLatency(
		const vector<NodeIdentLat>& in_remoteNodes);
	virtual int handleTimeout();
	virtual bool isFinished() const					{ return finished;	}	
	virtual int init();		
	int sendReturnPacket();
};

class HandleReqPing : public HandleReqGeneric {
protected:
	virtual ProbeQueryGeneric* createProbeQuery(const NodeIdent& in_remote, 
			MeridianProcess* in_process) {
		return (new ProbeQueryPing(in_remote, in_process));	
	}
	virtual int getLatency(const NodeIdent& inNode, uint32_t* latencyUS);
public:
	HandleReqPing(uint64_t id, const NodeIdentRendv& in_srcNode, 
					const vector<NodeIdent>& in_remote, 
					MeridianProcess* in_process) 
		: HandleReqGeneric(id, in_srcNode, in_remote, in_process) {}
	virtual ~HandleReqPing() {}		
	//virtual int init();
};

class HandleReqTCP : public HandleReqGeneric {
protected:
	virtual ProbeQueryGeneric* createProbeQuery(const NodeIdent& in_remote, 
			MeridianProcess* in_process) {
		return (new ProbeQueryTCP(in_remote, in_process));	
	}
	virtual int getLatency(const NodeIdent& inNode, uint32_t* latencyUS);
public:
	HandleReqTCP(uint64_t id, const NodeIdentRendv& in_srcNode, 
					const vector<NodeIdent>& in_remote, 
					MeridianProcess* in_process)
		: 	HandleReqGeneric(id, in_srcNode, in_remote, in_process) {}
	virtual ~HandleReqTCP() {}
	//virtual int init();		
};

class HandleReqDNS : public HandleReqGeneric {
protected:
	virtual ProbeQueryGeneric* createProbeQuery(const NodeIdent& in_remote, 
			MeridianProcess* in_process) {
		return (new ProbeQueryDNS(in_remote, in_process));	
	}
	virtual int getLatency(const NodeIdent& inNode, uint32_t* latencyUS);
public:
	HandleReqDNS(uint64_t id, const NodeIdentRendv& in_srcNode, 
					const vector<NodeIdent>& in_remote, 
					MeridianProcess* in_process)
		: 	HandleReqGeneric(id, in_srcNode, in_remote, in_process) {}
	virtual ~HandleReqDNS() {}
	//virtual int init();		
};

#ifdef PLANET_LAB_SUPPORT
class HandleReqICMP : public HandleReqGeneric {
protected:
	virtual ProbeQueryGeneric* createProbeQuery(const NodeIdent& in_remote, 
			MeridianProcess* in_process) {
		return (new ProbeQueryICMP(in_remote, in_process));	
	}
	virtual int getLatency(const NodeIdent& inNode, uint32_t* latencyUS);
public:
	HandleReqICMP(uint64_t id, const NodeIdentRendv& in_srcNode, 
					const vector<NodeIdent>& in_remote, 
					MeridianProcess* in_process) 
		: HandleReqGeneric(id, in_srcNode, in_remote, in_process) {}
	virtual ~HandleReqICMP() {}		
	//virtual int init();
};
#endif

class ReqProbeGeneric : public Query {
private:
	uint64_t					qid;
	NodeIdentRendv				srcNode;
	set<NodeIdent, ltNodeIdent>	remoteNodes;
	bool 						finished;
	struct timeval				timeoutTV;
	MeridianProcess*			meridProcess;
	vector<uint64_t>			subscribers;
protected:
	NodeIdentRendv getSrcNode()						{ return srcNode;		}
	set<NodeIdent, ltNodeIdent>* getRemoteNodes() 	{ return &remoteNodes; 	}
	MeridianProcess* getMerid() 					{ return meridProcess; 	}
	void setFinished(bool flag)						{ finished = flag;		}
	
public:
	ReqProbeGeneric(const NodeIdentRendv& in_src_node,
					const set<NodeIdent, ltNodeIdent>& in_remote, 
					MeridianProcess* in_process);
					
	ReqProbeGeneric(const NodeIdentRendv& in_src_node,
					const set<NodeIdentConst, ltNodeIdentConst>& in_remote, 
					MeridianProcess* in_process);	
					
	virtual ~ReqProbeGeneric() {}	
	virtual uint64_t getQueryID() const				{ return qid;		}
	virtual struct timeval timeOut() const			{ return timeoutTV;	} 	
	virtual int handleEvent(
		const NodeIdent& in_remote, 
		const char* inPacket, int packetSize);		
	virtual int handleLatency(
		const vector<NodeIdentLat>& in_remoteNodes)	{ return 0;			}
	virtual int handleTimeout();	
	virtual bool isFinished() const					{ return finished;	}		
	virtual int init() = 0;	
	virtual int subscribeLatency(uint64_t in_qid);
};

class ReqProbeTCP : public ReqProbeGeneric {
public:
	ReqProbeTCP(const NodeIdentRendv& in_src_node,
				const set<NodeIdent, ltNodeIdent>& in_remote, 
				MeridianProcess* in_process) 
		: ReqProbeGeneric(in_src_node, in_remote, in_process) {} 	
		
	ReqProbeTCP(const NodeIdentRendv& in_src_node,
				const set<NodeIdentConst, ltNodeIdentConst>& in_remote, 
				MeridianProcess* in_process) 
		: ReqProbeGeneric(in_src_node, in_remote, in_process) {} 	
		
	virtual ~ReqProbeTCP() {}			
	virtual int init();
};


class ReqProbeDNS : public ReqProbeGeneric {
public:
	ReqProbeDNS(const NodeIdentRendv& in_src_node,
				const set<NodeIdent, ltNodeIdent>& in_remote, 
				MeridianProcess* in_process) 
		: ReqProbeGeneric(in_src_node, in_remote, in_process) {}

	ReqProbeDNS(const NodeIdentRendv& in_src_node,
				const set<NodeIdentConst, ltNodeIdentConst>& in_remote, 
				MeridianProcess* in_process) 
		: ReqProbeGeneric(in_src_node, in_remote, in_process) {} 
 		
	virtual ~ReqProbeDNS() {}			
	virtual int init();
};

class ReqProbePing : public ReqProbeGeneric {
public:
	ReqProbePing(const NodeIdentRendv& in_src_node,
				const set<NodeIdent, ltNodeIdent>& in_remote, 
				MeridianProcess* in_process) 
		: ReqProbeGeneric(in_src_node, in_remote, in_process) {}				
				
	ReqProbePing(const NodeIdentRendv& in_src_node,
				const set<NodeIdentConst, ltNodeIdentConst>& in_remote, 
				MeridianProcess* in_process) 				
		: ReqProbeGeneric(in_src_node, in_remote, in_process) {}
 		
	virtual ~ReqProbePing() {}			
	virtual int init();
};

#ifdef PLANET_LAB_SUPPORT
class ReqProbeICMP : public ReqProbeGeneric {
public:
	ReqProbeICMP(const NodeIdentRendv& in_src_node,
				const set<NodeIdent, ltNodeIdent>& in_remote, 
				MeridianProcess* in_process) 
		: ReqProbeGeneric(in_src_node, in_remote, in_process) {}				
				
	ReqProbeICMP(const NodeIdentRendv& in_src_node,
				const set<NodeIdentConst, ltNodeIdentConst>& in_remote, 
				MeridianProcess* in_process) 				
		: ReqProbeGeneric(in_src_node, in_remote, in_process) {}
 		
	virtual ~ReqProbeICMP() {}			
	virtual int init();
};
#endif

enum HandleClosest_SM { 
	HC_INIT, 
	HC_WAIT_FOR_DIRECT_PING, 
	HC_INDIRECT_PING,
	HC_WAIT_FOR_FIN
};

class HandleClosestGeneric : public Query {
private:
	uint64_t												qid;
	u_short													betaNumer;
	u_short													betaDenom;
	u_int													averageLatUS;
	NodeIdentRendv											srcNode;
	bool 													finished;
	NodeIdent												selectedMember;
	struct timeval											timeoutTV;
	MeridianProcess*										meridProcess;
	set<NodeIdent, ltNodeIdent>								remoteNodes;
	set<NodeIdentRendv, ltNodeIdentRendv> 					ringMembers;
	map<NodeIdent, u_int, ltNodeIdent>						remoteLatencies;	
	HandleClosest_SM										stateMachine;
	map<NodeIdent, 
		map<NodeIdent, u_int, ltNodeIdent>*, ltNodeIdent> 	ringLatencies;
	
	static int getMaxAndAverage(
		const map<NodeIdent, u_int, ltNodeIdent>& inMap, 
		u_int* maxValue, u_int* avgValue);

	static int getMaxAndMinAndAverage(
		const map<NodeIdent, u_int, ltNodeIdent>& inMap, 
		u_int* maxValue, u_int* minValue, u_int* avgValue);
		
	int handleForward();	
	int sendReqProbes();
protected:
	MeridianProcess* getMerid() { return meridProcess; 	}
	
	virtual ReqClosestGeneric* createReqClosest(uint64_t id, 
		u_short in_beta_num, u_short in_beta_den, u_int in_rendv_addr, 
		u_short in_rendv_port) = 0;
		
	virtual ProbeQueryGeneric* createProbeQuery(const NodeIdent& in_remote, 
		MeridianProcess* in_process) = 0;
		
	virtual ReqProbeGeneric* createReqProbe(const NodeIdentRendv& in_src_node,
		const set<NodeIdent, ltNodeIdent>& in_remote, 
		MeridianProcess* in_process) = 0;
		
	virtual int getLatency(const NodeIdent& inNode, uint32_t* latencyUS) = 0;		
	virtual char getQueryType() = 0;

public:
	HandleClosestGeneric(uint64_t id,
			u_short in_betaNumer, u_short in_betaDenom,
			const NodeIdentRendv& in_srcNode, 
			const vector<NodeIdent>& in_remote, 
			MeridianProcess* in_process);			
	virtual ~HandleClosestGeneric();
	virtual uint64_t getQueryID() const				{ return qid;		}
	virtual struct timeval timeOut() const			{ return timeoutTV;	} 	
	virtual int handleEvent(
		const NodeIdent& in_remote, 
		const char* inPacket, int packetSize);
	virtual int handleLatency(
		const vector<NodeIdentLat>& in_remoteNodes);
	virtual int handleTimeout();
	virtual bool isFinished() const					{ return finished;	}		
	virtual int init();	
};

class HandleClosestTCP : public HandleClosestGeneric {
protected:
	virtual ReqClosestGeneric* createReqClosest(uint64_t id, 
			u_short in_beta_num, u_short in_beta_den, u_int in_rendv_addr, 
			u_short in_rendv_port) {
		return (new ReqClosestTCP(id, in_beta_num, 
			in_beta_den, in_rendv_addr, in_rendv_port));
	};
		
	virtual ProbeQueryGeneric* createProbeQuery(const NodeIdent& in_remote, 
			MeridianProcess* in_process) {
		return (new ProbeQueryTCP(in_remote, in_process));
	}
		
	virtual ReqProbeGeneric* createReqProbe(const NodeIdentRendv& in_src_node,
				const set<NodeIdent, ltNodeIdent>& in_remote, 
				MeridianProcess* in_process) {
		return (new ReqProbeTCP(in_src_node, in_remote, in_process));					
	}
	
	virtual int getLatency(const NodeIdent& inNode, uint32_t* latencyUS);
	
	virtual char getQueryType() {
		return REQ_CLOSEST_N_TCP;
	}
	
public:				
	HandleClosestTCP(uint64_t id, u_short in_betaNumer, u_short in_betaDenom,
		const NodeIdentRendv& in_srcNode, const vector<NodeIdent>& in_remote, 
		MeridianProcess* in_process) 
			:	HandleClosestGeneric(id, in_betaNumer, in_betaDenom, in_srcNode, 
				in_remote, in_process) {}
				
	virtual ~HandleClosestTCP() {}
};

class HandleClosestDNS : public HandleClosestGeneric {
protected:
	virtual ReqClosestGeneric* createReqClosest(uint64_t id, 
			u_short in_beta_num, u_short in_beta_den, u_int in_rendv_addr, 
			u_short in_rendv_port) {
		return (new ReqClosestDNS(id, in_beta_num, 
			in_beta_den, in_rendv_addr, in_rendv_port));
	};
		
	virtual ProbeQueryGeneric* createProbeQuery(const NodeIdent& in_remote, 
			MeridianProcess* in_process) {
		return (new ProbeQueryDNS(in_remote, in_process));
	}
		
	virtual ReqProbeGeneric* createReqProbe(const NodeIdentRendv& in_src_node,
				const set<NodeIdent, ltNodeIdent>& in_remote, 
				MeridianProcess* in_process) {
		return (new ReqProbeDNS(in_src_node, in_remote, in_process));					
	}
	
	virtual int getLatency(const NodeIdent& inNode, uint32_t* latencyUS);
	
	virtual char getQueryType() {
		return REQ_CLOSEST_N_DNS;
	}	
	
public:				
	HandleClosestDNS(uint64_t id, u_short in_betaNumer, u_short in_betaDenom,
		const NodeIdentRendv& in_srcNode, const vector<NodeIdent>& in_remote, 
		MeridianProcess* in_process) 
			:	HandleClosestGeneric(id, in_betaNumer, in_betaDenom, in_srcNode, 
				in_remote, in_process) {}
				
	virtual ~HandleClosestDNS() {}
};


class HandleClosestPing : public HandleClosestGeneric {
protected:
	virtual ReqClosestGeneric* createReqClosest(uint64_t id, 
			u_short in_beta_num, u_short in_beta_den, u_int in_rendv_addr, 
			u_short in_rendv_port) {
		return (new ReqClosestMeridPing(id, in_beta_num, 
			in_beta_den, in_rendv_addr, in_rendv_port));
	};
		
	virtual ProbeQueryGeneric* createProbeQuery(const NodeIdent& in_remote, 
			MeridianProcess* in_process) {
		return (new ProbeQueryPing(in_remote, in_process));
	}
		
	virtual ReqProbeGeneric* createReqProbe(const NodeIdentRendv& in_src_node,
				const set<NodeIdent, ltNodeIdent>& in_remote, 
				MeridianProcess* in_process) {
		return (new ReqProbePing(in_src_node, in_remote, in_process));					
	}
	
	virtual int getLatency(const NodeIdent& inNode, uint32_t* latencyUS);
	
	virtual char getQueryType() {
		return REQ_CLOSEST_N_MERID_PING;
	}
	
public:				
	HandleClosestPing(uint64_t id, u_short in_betaNumer, u_short in_betaDenom,
		const NodeIdentRendv& in_srcNode, const vector<NodeIdent>& in_remote, 
		MeridianProcess* in_process) 
			:	HandleClosestGeneric(id, in_betaNumer, in_betaDenom, in_srcNode, 
				in_remote, in_process) {}
				
	virtual ~HandleClosestPing() {}
};


#ifdef PLANET_LAB_SUPPORT
class HandleClosestICMP : public HandleClosestGeneric {
protected:
	virtual ReqClosestGeneric* createReqClosest(uint64_t id, 
			u_short in_beta_num, u_short in_beta_den, u_int in_rendv_addr, 
			u_short in_rendv_port) {
		return (new ReqClosestICMP(id, in_beta_num, 
			in_beta_den, in_rendv_addr, in_rendv_port));
	};
		
	virtual ProbeQueryGeneric* createProbeQuery(const NodeIdent& in_remote, 
			MeridianProcess* in_process) {
		return (new ProbeQueryICMP(in_remote, in_process));
	}
		
	virtual ReqProbeGeneric* createReqProbe(const NodeIdentRendv& in_src_node,
				const set<NodeIdent, ltNodeIdent>& in_remote, 
				MeridianProcess* in_process) {
		return (new ReqProbeICMP(in_src_node, in_remote, in_process));					
	}
	
	virtual int getLatency(const NodeIdent& inNode, uint32_t* latencyUS);
	
	virtual char getQueryType() {
		return REQ_CLOSEST_N_ICMP;
	}
	
public:				
	HandleClosestICMP(uint64_t id, u_short in_betaNumer, u_short in_betaDenom,
		const NodeIdentRendv& in_srcNode, const vector<NodeIdent>& in_remote, 
		MeridianProcess* in_process) 
			:	HandleClosestGeneric(id, in_betaNumer, in_betaDenom, in_srcNode, 
				in_remote, in_process) {}
				
	virtual ~HandleClosestICMP() {}
};
#endif

enum HandleMultiConstraint_SM { 
	HMC_INIT, 
	HMC_WAIT_FOR_DIRECT_PING, 
	HMC_INDIRECT_PING,
	HMC_WAIT_FOR_FIN
};

class HandleMCGeneric : public Query {
private:
	uint64_t												qid;
	u_short													betaNumer;
	u_short													betaDenom;
	u_int													averageLatUS;
	NodeIdentRendv											srcNode;
	bool 													finished;
	NodeIdent												selectedMember;
	struct timeval											timeoutTV;
	MeridianProcess*										meridProcess;
	set<NodeIdentConst, ltNodeIdentConst>					remoteNodes;
	set<NodeIdentRendv, ltNodeIdentRendv> 					ringMembers;
	map<NodeIdent, u_int, ltNodeIdent>						remoteLatencies;	
	HandleMultiConstraint_SM								stateMachine;
	map<NodeIdent, 
		map<NodeIdent, u_int, ltNodeIdent>*, ltNodeIdent> 	ringLatencies;
	
	// Returns the maximum feasible solution that can be within the
	// solution space
	int getMaxSolution(const map<NodeIdent, u_int, ltNodeIdent>& inMap,
			u_int* maxSolution, bool subtract) {
		long long maxSol_LL = 0;
		set<NodeIdentConst, ltNodeIdentConst>::iterator it 
			= remoteNodes.begin();
		for (; it != remoteNodes.end(); it++) {
			NodeIdent tmp = {it->addr, it->port};
			map<NodeIdent, u_int, ltNodeIdent>::const_iterator findIt
				= inMap.find(tmp);
			if (findIt == inMap.end()) {
				ERROR_LOG("Only partial information available");
				return -1;		
			}						
			long long tmpLL = ((long long)findIt->second);
			if (subtract) {
				tmpLL = MAX(0, tmpLL - 
					(((long long)(it->latencyConstMS)) * 1000));
			} else {
				tmpLL += (((long long)(it->latencyConstMS)) * 1000);
			}
			if (tmpLL > maxSol_LL) {
				maxSol_LL = tmpLL;				
			}			
		}
		assert(maxSol_LL >= 0);
		if (maxSol_LL > UINT_MAX) {
			*maxSolution = UINT_MAX;
		} else {
			*maxSolution = (u_int) maxSol_LL; 	
		}
		return 0;
	}	
	
	int getAvgSolution(const map<NodeIdent, u_int, ltNodeIdent>& inMap,
			u_int* latSolution) {
		double totalSolution = 0.0;
		set<NodeIdentConst, ltNodeIdentConst>::iterator it 
			= remoteNodes.begin();
		for (; it != remoteNodes.end(); it++) {
			NodeIdent tmp = {it->addr, it->port};
			map<NodeIdent, u_int, ltNodeIdent>::const_iterator findIt
				= inMap.find(tmp);
			if (findIt == inMap.end()) {
				ERROR_LOG("Only partial information available");
				return -1;		
			}
			long long tmpLL = MAX(0, ((long long)findIt->second) -
				(((long long)(it->latencyConstMS)) * 1000));
			totalSolution += (double)(tmpLL * tmpLL);			
		}
		totalSolution = sqrt(totalSolution) / (remoteNodes.size());
		if (totalSolution > UINT_MAX) {
			*latSolution = UINT_MAX;			
		} else if (totalSolution < 0.0) {
			WARN_LOG("Solution less than 0, shouldn't be possible\n");
			*latSolution = 0;	//	This shouldn't actually happen				
		} else {
			*latSolution = (u_int) totalSolution;	
		}
		return 0;
	}	
	int sendReqProbes();		
	int handleForward();
protected:
	MeridianProcess* getMerid()  { return meridProcess; }

	virtual ReqConstraintGeneric* createReqConstraint(uint64_t id, 
		u_short in_beta_num, u_short in_beta_den, u_int in_rendv_addr, 
		u_short in_rendv_port) = 0;
		
	virtual ProbeQueryGeneric* createProbeQuery(const NodeIdent& in_remote, 
		MeridianProcess* in_process) = 0;
		
	virtual ReqProbeGeneric* createReqProbe(const NodeIdentRendv& in_src_node,
		const set<NodeIdentConst, ltNodeIdentConst>& in_remote, 
		MeridianProcess* in_process) = 0;
			
	virtual int getLatency(const NodeIdent& inNode, uint32_t* latencyUS) = 0;		
	virtual char getQueryType() = 0;
	
public:
	HandleMCGeneric(uint64_t id,
			u_short in_betaNumer, u_short in_betaDenom,
			const NodeIdentRendv& in_srcNode, 
			const vector<NodeIdentConst>& in_remote, 
			MeridianProcess* in_process);			
	virtual ~HandleMCGeneric();
	virtual uint64_t getQueryID() const				{ return qid;		}
	virtual struct timeval timeOut() const			{ return timeoutTV;	}
	virtual int handleEvent(
		const NodeIdent& in_remote, 
		const char* inPacket, int packetSize);
	virtual int handleLatency(
		const vector<NodeIdentLat>& in_remoteNodes);
	virtual int handleTimeout();
	virtual bool isFinished() const					{ return finished;	}
	virtual int init();	
};

class HandleMCTCP : public HandleMCGeneric {
protected:
	virtual ReqConstraintGeneric* createReqConstraint(uint64_t id, 
			u_short in_beta_num, u_short in_beta_den, u_int in_rendv_addr, 
			u_short in_rendv_port)  {
		return (new ReqConstraintTCP(id, in_beta_num, 
			in_beta_den, in_rendv_addr, in_rendv_port));			
	}
		
	virtual ProbeQueryGeneric* createProbeQuery(const NodeIdent& in_remote, 
			MeridianProcess* in_process) {
		return (new ProbeQueryTCP(in_remote, in_process));	
	}
		
	virtual ReqProbeGeneric* createReqProbe(const NodeIdentRendv& in_src_node,
			const set<NodeIdentConst, ltNodeIdentConst>& in_remote, 
			MeridianProcess* in_process) {
		return (new ReqProbeTCP(in_src_node, in_remote, in_process));		
	}
	
	virtual int getLatency(const NodeIdent& inNode, uint32_t* latencyUS);

	virtual char getQueryType() {
		return REQ_CONSTRAINT_N_TCP;
	}
	
public:				
	HandleMCTCP(uint64_t id, u_short in_betaNumer, u_short in_betaDenom,
		const NodeIdentRendv& in_srcNode, 
		const vector<NodeIdentConst>& in_remote, MeridianProcess* in_process) 
			:	HandleMCGeneric(id, in_betaNumer, in_betaDenom, in_srcNode, 
				in_remote, in_process) {}
				
	virtual ~HandleMCTCP() {}
};

class HandleMCPing : public HandleMCGeneric {
protected:
	virtual ReqConstraintGeneric* createReqConstraint(uint64_t id, 
			u_short in_beta_num, u_short in_beta_den, u_int in_rendv_addr, 
			u_short in_rendv_port)  {
		return (new ReqConstraintPing(id, in_beta_num, 
			in_beta_den, in_rendv_addr, in_rendv_port));			
	}
		
	virtual ProbeQueryGeneric* createProbeQuery(const NodeIdent& in_remote, 
			MeridianProcess* in_process) {
		return (new ProbeQueryPing(in_remote, in_process));	
	}
		
	virtual ReqProbeGeneric* createReqProbe(const NodeIdentRendv& in_src_node,
			const set<NodeIdentConst, ltNodeIdentConst>& in_remote, 
			MeridianProcess* in_process) {
		return (new ReqProbePing(in_src_node, in_remote, in_process));		
	}

	virtual int getLatency(const NodeIdent& inNode, uint32_t* latencyUS);
	
	virtual char getQueryType() {
		return REQ_CONSTRAINT_N_PING;
	}
	
public:				
	HandleMCPing(uint64_t id, u_short in_betaNumer, u_short in_betaDenom,
		const NodeIdentRendv& in_srcNode, 
		const vector<NodeIdentConst>& in_remote, MeridianProcess* in_process) 
			:	HandleMCGeneric(id, in_betaNumer, in_betaDenom, in_srcNode, 
				in_remote, in_process) {}
				
	virtual ~HandleMCPing() {}
};

class HandleMCDNS : public HandleMCGeneric {
protected:
	virtual ReqConstraintGeneric* createReqConstraint(uint64_t id, 
			u_short in_beta_num, u_short in_beta_den, u_int in_rendv_addr, 
			u_short in_rendv_port)  {
		return (new ReqConstraintDNS(id, in_beta_num, 
			in_beta_den, in_rendv_addr, in_rendv_port));			
	}
		
	virtual ProbeQueryGeneric* createProbeQuery(const NodeIdent& in_remote, 
			MeridianProcess* in_process) {
		return (new ProbeQueryDNS(in_remote, in_process));	
	}
		
	virtual ReqProbeGeneric* createReqProbe(const NodeIdentRendv& in_src_node,
			const set<NodeIdentConst, ltNodeIdentConst>& in_remote, 
			MeridianProcess* in_process) {
		return (new ReqProbeDNS(in_src_node, in_remote, in_process));		
	}

	virtual int getLatency(const NodeIdent& inNode, uint32_t* latencyUS);
	
	virtual char getQueryType() {
		return REQ_CONSTRAINT_N_DNS;
	}
	
public:				
	HandleMCDNS(uint64_t id, u_short in_betaNumer, u_short in_betaDenom,
		const NodeIdentRendv& in_srcNode, 
		const vector<NodeIdentConst>& in_remote, MeridianProcess* in_process) 
			:	HandleMCGeneric(id, in_betaNumer, in_betaDenom, in_srcNode, 
				in_remote, in_process) {}
				
	virtual ~HandleMCDNS() {}
};

#ifdef PLANET_LAB_SUPPORT
class HandleMCICMP : public HandleMCGeneric {
protected:
	virtual ReqConstraintGeneric* createReqConstraint(uint64_t id, 
			u_short in_beta_num, u_short in_beta_den, u_int in_rendv_addr, 
			u_short in_rendv_port)  {
		return (new ReqConstraintICMP(id, in_beta_num, 
			in_beta_den, in_rendv_addr, in_rendv_port));			
	}
		
	virtual ProbeQueryGeneric* createProbeQuery(const NodeIdent& in_remote, 
			MeridianProcess* in_process) {
		return (new ProbeQueryICMP(in_remote, in_process));	
	}
		
	virtual ReqProbeGeneric* createReqProbe(const NodeIdentRendv& in_src_node,
			const set<NodeIdentConst, ltNodeIdentConst>& in_remote, 
			MeridianProcess* in_process) {
		return (new ReqProbeICMP(in_src_node, in_remote, in_process));		
	}

	virtual int getLatency(const NodeIdent& inNode, uint32_t* latencyUS);
	
	virtual char getQueryType() {
		return REQ_CONSTRAINT_N_ICMP;
	}
	
public:				
	HandleMCICMP(uint64_t id, u_short in_betaNumer, u_short in_betaDenom,
		const NodeIdentRendv& in_srcNode, 
		const vector<NodeIdentConst>& in_remote, MeridianProcess* in_process) 
			:	HandleMCGeneric(id, in_betaNumer, in_betaDenom, in_srcNode, 
				in_remote, in_process) {}
				
	virtual ~HandleMCICMP() {}
};
#endif

#ifdef MERIDIAN_DSL
#include "MQLState.h"
// This query redirects packets to DSLRecvQuery
class DSLReqQuery : public Query {
private:
	uint64_t					qid;	
	bool 						finished;
	struct timeval				timeoutTV;
	MeridianProcess*			meridProcess;
	uint64_t					recvQueryID;
	NodeIdentRendv				destNode;
	uint16_t					ttl;
protected:
	MeridianProcess* getMerid() 					{ return meridProcess; 	}
	void setFinished(bool flag)						{ finished = flag;		}	
	
public:
	DSLReqQuery(MeridianProcess* in_process, const struct timeval& in_timeout,
		uint16_t in_ttl, const NodeIdentRendv& in_dest, uint64_t redirectQID);
		
	virtual ~DSLReqQuery() {}			
	virtual uint64_t getQueryID() const				{ return qid;		}
	virtual struct timeval timeOut() const			{ return timeoutTV;	} 	
	virtual int handleEvent(
		const NodeIdent& in_remote, 
		const char* inPacket, int packetSize); 
	virtual int handleLatency(
		const vector<NodeIdentLat>& in_remoteNodes)	{ return 0;			}		
	virtual int handleTimeout()	{ 
		setFinished(true);
		return 0; 
	}
	virtual bool isFinished() const					{ return finished;	}
	virtual int init()								{ return 0;			}
	// Special for this query
	int init(ParserState* ps, const string* func_name, const ASTNode* param); 
};


class DSLRecvQuery : public Query {
private:
	uint64_t					qid;
	uint64_t					ret_qid;	
	bool 						finished;
	struct timeval				timeoutTV;
	MeridianProcess*			meridProcess;
	ParserState*				ps;
	NodeIdentRendv				srcNode;
	uint16_t					ttl;
protected:
	NodeIdentRendv getSrcNode()						{ return srcNode;		}
	MeridianProcess* getMerid() 					{ return meridProcess; 	}
	void setFinished(bool flag)						{ finished = flag;		}	
	
public:
	DSLRecvQuery(ParserState* in_state, MeridianProcess* in_process,
			const NodeIdentRendv& in_src, uint64_t in_ret_qid, 
			uint16_t timeout_ms, uint16_t in_ttl);
	
	virtual ~DSLRecvQuery() {		
		if (ps) delete ps; // Query gains ownership of ParserState
	}	
	virtual uint64_t getQueryID() const				{ return qid;		}
	virtual struct timeval timeOut() const			{ return timeoutTV;	} 	
	virtual int handleEvent(
		const NodeIdent& in_remote, 
		const char* inPacket, int packetSize);		
	virtual int handleLatency(
		const vector<NodeIdentLat>& in_remoteNodes);
	virtual int handleTimeout()	{ 
		setFinished(true);
		return 0; 
	}
	virtual bool isFinished() const					{ return finished;	}
	virtual int init()								{ return 0; 		}
	ParserState* getPS()							{ return ps;		}
	PSState parserState() const	{
		return ps->parser_state();	
	}
	uint16_t getTTL() {
		return ttl;	
	}	
};


class ReqProbeSelfGeneric : public Query {
private:
	uint64_t							qid;
	set<NodeIdent, ltNodeIdent>			remoteNodes;
	map<NodeIdent, u_int, ltNodeIdent>	remoteLatencies;
	bool 								finished;
	struct timeval						timeoutTV;
	MeridianProcess*					meridProcess;
	vector<uint64_t>					subscribers;
protected:
	set<NodeIdent, ltNodeIdent>* getRemoteNodes() 	{ return &remoteNodes; 	}
	MeridianProcess* getMerid() 					{ return meridProcess; 	}
	void setFinished(bool flag)						{ finished = flag;		}
	
	virtual ProbeQueryGeneric* createProbeQuery(const NodeIdent& in_remote, 
		MeridianProcess* in_process) = 0;	
	
public:
	ReqProbeSelfGeneric(const set<NodeIdent, ltNodeIdent>& in_remote, 
				MeridianProcess* in_process);
					
	ReqProbeSelfGeneric(const set<NodeIdentConst, ltNodeIdentConst>& in_remote, 
				MeridianProcess* in_process);	
					
	virtual ~ReqProbeSelfGeneric() {}	
	virtual uint64_t getQueryID() const				{ return qid;		}
	virtual struct timeval timeOut() const			{ return timeoutTV;	} 	
	virtual int handleEvent(
		const NodeIdent& in_remote, 
		const char* inPacket, int packetSize)		{ return 0;			}
	virtual int handleLatency(
		const vector<NodeIdentLat>& in_remoteNodes);
	virtual int handleTimeout() {
		finished = true;
		return 0;
	}
	virtual bool isFinished() const					{ return finished;	}		
	virtual int init();
	virtual int subscribeLatency(uint64_t in_qid);
	int returnResults();
};

class ReqProbeSelfTCP : public ReqProbeSelfGeneric {
protected:
	virtual ProbeQueryGeneric* createProbeQuery(const NodeIdent& in_remote, 
			MeridianProcess* in_process) {
		return (new ProbeQueryTCP(in_remote, in_process));	
	}
public:
	ReqProbeSelfTCP(const set<NodeIdent, ltNodeIdent>& in_remote, 
				MeridianProcess* in_process) 
		: ReqProbeSelfGeneric(in_remote, in_process) {} 	
		
	ReqProbeSelfTCP(const set<NodeIdentConst, ltNodeIdentConst>& in_remote, 
				MeridianProcess* in_process) 
		: ReqProbeSelfGeneric(in_remote, in_process) {}		
		
	virtual ~ReqProbeSelfTCP() {}
};


class ReqProbeSelfDNS : public ReqProbeSelfGeneric {
protected:
	virtual ProbeQueryGeneric* createProbeQuery(const NodeIdent& in_remote, 
			MeridianProcess* in_process) {
		return (new ProbeQueryDNS(in_remote, in_process));	
	}
public:
	ReqProbeSelfDNS(const set<NodeIdent, ltNodeIdent>& in_remote, 
				MeridianProcess* in_process) 
		: ReqProbeSelfGeneric(in_remote, in_process) {}

	ReqProbeSelfDNS(const set<NodeIdentConst, ltNodeIdentConst>& in_remote, 
				MeridianProcess* in_process) 
		: ReqProbeSelfGeneric(in_remote, in_process) {} 
 		
	virtual ~ReqProbeSelfDNS() {}			
};

class ReqProbeSelfPing : public ReqProbeSelfGeneric {
protected:
	virtual ProbeQueryGeneric* createProbeQuery(const NodeIdent& in_remote, 
			MeridianProcess* in_process) {
		return (new ProbeQueryPing(in_remote, in_process));	
	}
public:
	ReqProbeSelfPing(const set<NodeIdent, ltNodeIdent>& in_remote, 
				MeridianProcess* in_process) 
		: ReqProbeSelfGeneric(in_remote, in_process) {}				
				
	ReqProbeSelfPing(const set<NodeIdentConst, ltNodeIdentConst>& in_remote, 
				MeridianProcess* in_process) 				
		: ReqProbeSelfGeneric(in_remote, in_process) {}
 		
	virtual ~ReqProbeSelfPing() {}			
};

#ifdef PLANET_LAB_SUPPORT
class ReqProbeSelfICMP : public ReqProbeSelfGeneric {
protected:
	virtual ProbeQueryGeneric* createProbeQuery(const NodeIdent& in_remote, 
			MeridianProcess* in_process) {
		return (new ProbeQueryICMP(in_remote, in_process));	
	}
public:
	ReqProbeSelfICMP(const set<NodeIdent, ltNodeIdent>& in_remote, 
				MeridianProcess* in_process) 
		: ReqProbeSelfGeneric(in_remote, in_process) {}				
				
	ReqProbeSelfICMP(const set<NodeIdentConst, ltNodeIdentConst>& in_remote, 
				MeridianProcess* in_process) 				
		: ReqProbeSelfGeneric(in_remote, in_process) {}
 		
	virtual ~ReqProbeSelfICMP() {}			
};
#endif

// Destination nodes cannot be behind firewall. If they are, move them
// to the source side
class HandleGetDistanceGeneric : public Query {
private:		
	uint64_t										qid;
	bool 											finished;
	//struct timeval									startTime;
	struct timeval									timeoutTV;
	MeridianProcess*								meridProcess;	
	vector<NodeIdentRendv> 							srcNodes;
	set<int>										receivedRow;	
	//	Need this vector to keep the ordering and rendv 
	vector<NodeIdent>								destNodesVect;	
	set<NodeIdent, ltNodeIdent>						destNodes;
	uint32_t* 										latencyMatrixUS;
	uint64_t										prevQID;	
protected:
	MeridianProcess* getMerid() 					{ return meridProcess; 	}
	void setFinished(bool flag)						{ finished = flag;		}
	
	virtual ReqProbeGeneric* createReqProbe(const NodeIdentRendv& in_src_node,
		const set<NodeIdent, ltNodeIdent>& in_remote, 
		MeridianProcess* in_process) = 0;
		
	virtual ReqProbeSelfGeneric* createReqSelfProbe(
		const set<NodeIdent, ltNodeIdent>& in_remote, 
		MeridianProcess* in_process) = 0;		
		
public:
	HandleGetDistanceGeneric(const vector<NodeIdentRendv>& in_src,	
				const vector<NodeIdentRendv>& in_remote, 
				MeridianProcess* in_process, 
				u_int in_timeout_ms, uint64_t in_prevQID);
							
	virtual ~HandleGetDistanceGeneric();
	virtual uint64_t getQueryID() const				{ return qid;		}
	virtual struct timeval timeOut() const			{ return timeoutTV;	} 	
	virtual int handleEvent(
		const NodeIdent& in_remote, 
		const char* inPacket, int packetSize)		{ return 0;			}		
	virtual int handleLatency(
			const vector<NodeIdentLat>& in_remoteNodes);
	virtual int handleTimeout() 					{ return returnResults(); }		
	virtual bool isFinished() const					{ return finished;	}	
	virtual int init();
	int returnResults();
};

class HandleGetDistanceTCP : public HandleGetDistanceGeneric {
protected:	
	virtual ReqProbeGeneric* createReqProbe(const NodeIdentRendv& in_src_node,
			const set<NodeIdent, ltNodeIdent>& in_remote, 
			MeridianProcess* in_process) {
		return (new ReqProbeTCP(in_src_node, in_remote, in_process));			
	}
		
	virtual ReqProbeSelfGeneric* createReqSelfProbe(
			const set<NodeIdent, ltNodeIdent>& in_remote, 
			MeridianProcess* in_process) {
		return (new ReqProbeSelfTCP(in_remote, in_process));		
	}
public:
	HandleGetDistanceTCP(const vector<NodeIdentRendv>& in_src,	
				const vector<NodeIdentRendv>& in_remote, 
				MeridianProcess* in_process, 
				u_int in_timeout_ms, uint64_t in_prevQID) 
		: HandleGetDistanceGeneric(in_src, in_remote, 
			in_process, in_timeout_ms, in_prevQID) {}
			
	virtual ~HandleGetDistanceTCP() {}
};



class HandleGetDistanceDNS : public HandleGetDistanceGeneric {
protected:	
	virtual ReqProbeGeneric* createReqProbe(const NodeIdentRendv& in_src_node,
			const set<NodeIdent, ltNodeIdent>& in_remote, 
			MeridianProcess* in_process) {
		return (new ReqProbeDNS(in_src_node, in_remote, in_process));			
	}
		
	virtual ReqProbeSelfGeneric* createReqSelfProbe(
			const set<NodeIdent, ltNodeIdent>& in_remote, 
			MeridianProcess* in_process) {
		return (new ReqProbeSelfDNS(in_remote, in_process));		
	}
public:
	HandleGetDistanceDNS(const vector<NodeIdentRendv>& in_src,	
				const vector<NodeIdentRendv>& in_remote, 
				MeridianProcess* in_process, 
				u_int in_timeout_ms, uint64_t in_prevQID) 
		: HandleGetDistanceGeneric(in_src, in_remote, 
			in_process, in_timeout_ms, in_prevQID) {}
			
	virtual ~HandleGetDistanceDNS() {}
};


class HandleGetDistancePing : public HandleGetDistanceGeneric {
protected:	
	virtual ReqProbeGeneric* createReqProbe(const NodeIdentRendv& in_src_node,
			const set<NodeIdent, ltNodeIdent>& in_remote, 
			MeridianProcess* in_process) {
		return (new ReqProbePing(in_src_node, in_remote, in_process));			
	}
		
	virtual ReqProbeSelfGeneric* createReqSelfProbe(
			const set<NodeIdent, ltNodeIdent>& in_remote, 
			MeridianProcess* in_process) {
		return (new ReqProbeSelfPing(in_remote, in_process));		
	}
public:
	HandleGetDistancePing(const vector<NodeIdentRendv>& in_src,	
				const vector<NodeIdentRendv>& in_remote, 
				MeridianProcess* in_process, 
				u_int in_timeout_ms, uint64_t in_prevQID) 
		: HandleGetDistanceGeneric(in_src, in_remote, 
			in_process, in_timeout_ms, in_prevQID) {}
			
	virtual ~HandleGetDistancePing() {}
};

#ifdef PLANET_LAB_SUPPORT
class HandleGetDistanceICMP : public HandleGetDistanceGeneric {
protected:	
	virtual ReqProbeGeneric* createReqProbe(const NodeIdentRendv& in_src_node,
			const set<NodeIdent, ltNodeIdent>& in_remote, 
			MeridianProcess* in_process) {
		return (new ReqProbeICMP(in_src_node, in_remote, in_process));			
	}
		
	virtual ReqProbeSelfGeneric* createReqSelfProbe(
			const set<NodeIdent, ltNodeIdent>& in_remote, 
			MeridianProcess* in_process) {
		return (new ReqProbeSelfICMP(in_remote, in_process));		
	}
public:
	HandleGetDistanceICMP(const vector<NodeIdentRendv>& in_src,	
				const vector<NodeIdentRendv>& in_remote, 
				MeridianProcess* in_process, 
				u_int in_timeout_ms, uint64_t in_prevQID) 
		: HandleGetDistanceGeneric(in_src, in_remote, 
			in_process, in_timeout_ms, in_prevQID) {}
			
	virtual ~HandleGetDistanceICMP() {}
};
#endif
#endif
#endif

