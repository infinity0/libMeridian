#ifndef CLASS_LATENCY_CACHE
#define CLASS_LATENCY_CACHE

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <map>
#include "Marshal.h"

class LatencyCache {
private:	
	map<struct timeval, vector<NodeIdent>*, timevalLT>				timeoutMap;
	map<NodeIdent, pair<struct timeval, uint32_t>*, ltNodeIdent>	latencyMap;
	u_int 															maxSize;
	u_int															periodUS;	
public:
	LatencyCache(u_int in_maxSize, u_int in_periodUS) 
		: maxSize(in_maxSize), periodUS(in_periodUS) {}
		
	~LatencyCache();
	int getLatency(const NodeIdent& inNode, uint32_t* latencyUS);
	int insertMeasurement(const NodeIdent& inNode, uint32_t latencyUS);
	int eraseEntry(const NodeIdent& inNode);
};

#endif
