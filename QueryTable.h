#ifndef CLASS_QUERY_TABLE
#define CLASS_QUERY_TABLE

#include <stdint.h>
#include <sys/time.h>
#include <assert.h>
#include <vector>
#include <map>
#include "Common.h"
#include "Query.h"

#define DEFAULT_TIME_OUT_S	5

struct queryLT {
	bool operator()(Query* s1, Query* s2) const {
		if (s1->getQueryID() < s2->getQueryID()) {
			return true;
		}
		return false;
	}
};

struct timevalLT {
	bool operator()(struct timeval s1, struct timeval s2) const {
		// Assume time is already normalized
		if ((s1.tv_sec < s2.tv_sec) || 
			((s1.tv_sec == s2.tv_sec) && (s1.tv_usec < s2.tv_usec))) {
			return true;
		}
		return false;		
	}
};

class QueryTable {
private:
	map<Query*, struct timeval, queryLT>				queryTimeoutMap; 
	map<struct timeval, vector<Query*>*, timevalLT>		timeoutQueryMap;

	int addTimeout(Query* inQuery);	
	int removeOldTimeout(Query* inQuery);	
	struct timeval retrieveOldTimeout(Query* inQuery);	
public:
	QueryTable() {}
	~QueryTable() {
		// Since timeoutQueryMap and queryTimeoutMap both contains every
		// query in the system, only need to delete queries from
		// timeoutQueryMap
		map<struct timeval, vector<Query*>*, timevalLT>	::iterator it
			= timeoutQueryMap.begin();		
		for (; it != timeoutQueryMap.end(); it++) {
			if (it->second != NULL) {
				vector<Query*>* tmpVect = it->second;
				//	Delete all contents of the vector
				for (u_int i = 0; i < tmpVect->size(); i++) {
					if ((*tmpVect)[i] != NULL) {
						delete (*tmpVect)[i];	
					}
				}
				delete tmpVect;	// delete the vector itself
			}
		}
	}
	
	static void normalizeTime(struct timeval& tv);	
	int updateTimeout(Query* inQuery);		
	int insertNewQuery(Query* inQuery);
	int handleTimeout();	
	int notifyQPacket(uint64_t id, const NodeIdent& remoteNode, 
		const char* packet, int packetSize);
	int notifyQLatency(uint64_t id,
		const vector<NodeIdentLat>& in_remoteNodes);
	//const NodeIdent& remoteNode, u_int latency_us);	
	bool isQueryInTable(uint64_t id) {
		SearchQuery tmp(id);
		return (queryTimeoutMap.find(&tmp) != queryTimeoutMap.end());			
	}
		
	void nextTimeout(struct timeval* nextEventTime) {
		if (timeoutQueryMap.empty()) {
			gettimeofday(nextEventTime, NULL);
			nextEventTime->tv_sec += DEFAULT_TIME_OUT_S;	
		} else {
			nextEventTime->tv_sec = (timeoutQueryMap.begin())->first.tv_sec;
			nextEventTime->tv_usec = (timeoutQueryMap.begin())->first.tv_usec;
		}
	}
#ifdef MERIDIAN_DSL
	DSLRecvQuery* getDSLRecvQ(uint64_t id) {
		SearchQuery tmpQ(id);
		map<Query*, struct timeval, queryLT>::iterator findIt 
			= queryTimeoutMap.find(&tmpQ);
		if (findIt == queryTimeoutMap.end()) {
			return NULL;	
		}
		return dynamic_cast<DSLRecvQuery*>(findIt->first); 			
	}
#endif
};

#endif
