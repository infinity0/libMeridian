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

#include "QueryTable.h"
#include "LatencyCache.h"

LatencyCache::~LatencyCache() {
	//	Not the fastest way, but speed doesn't really matter
	//	Push all entries into allEntries vector
	vector<NodeIdent> allEntries;
	map<NodeIdent, pair<struct timeval, uint32_t>*, ltNodeIdent>::iterator
		it = latencyMap.begin();
	for (; it != latencyMap.end(); it++) {
		allEntries.push_back(it->first);						
	}
	// Iterate through all entries vector and call eraseEntry
	for (u_int i = 0; i < allEntries.size(); i++) {
		eraseEntry(allEntries[i]);	
	}
	assert(timeoutMap.size() == 0);
	assert(latencyMap.size() == 0);
}


int LatencyCache::getLatency(const NodeIdent& inNode, uint32_t* latencyUS) {
	map<NodeIdent, pair<struct timeval, uint32_t>*, ltNodeIdent>::iterator
		findIt = latencyMap.find(inNode);
	if (findIt != latencyMap.end()) {
		//	Get current normalized time
		struct timeval curTime;
		gettimeofday(&curTime, NULL);
		QueryTable::normalizeTime(curTime);
		//	Get timeout time of entry
		struct timeval s1 = findIt->second->first;			
		if ((s1.tv_sec < curTime.tv_sec) || 
				((s1.tv_sec == curTime.tv_sec) && 
				(s1.tv_usec <= curTime.tv_usec))) {
			//	Timed out. Don't even bother to remove
			return -1;	
		}					
		*latencyUS = findIt->second->second;
		return 0;
	}
	return -1;
}

int LatencyCache::insertMeasurement(
		const NodeIdent& inNode, uint32_t latencyUS) {
	if (maxSize == 0) {
		return 0;
	}
	//	Remove existing measurements of this node
	if (eraseEntry(inNode) == -1) {
		return -1;	// Error on removing node, just return
	}		
	if (latencyMap.size() >= maxSize) {			
		//	Erase oldest entry. There should always be at least
		//	one entry in a vector
		NodeIdent tmp = (*((timeoutMap.begin())->second))[0];
		eraseEntry(tmp);
	}		
	//	Get the next timeout
	struct timeval curTime, nextTimeOut;
	gettimeofday(&curTime, NULL);	
	struct timeval offsetTV = 
			{ periodUS / MICRO_IN_SECOND, periodUS % MICRO_IN_SECOND}; 			
	timeradd(&curTime, &offsetTV, &nextTimeOut);
	QueryTable::normalizeTime(nextTimeOut);
	//	See if the timeout is already there. If it is, add to the vector
	//	If not, create a new vector
	map<struct timeval, vector<NodeIdent>*, timevalLT>::iterator 
		findIt = timeoutMap.find(nextTimeOut);
	if (findIt != timeoutMap.end()) {
		findIt->second->push_back(inNode);
	} else {
		vector<NodeIdent>* tmpVect = new vector<NodeIdent>();
		tmpVect->push_back(inNode);
		timeoutMap[nextTimeOut] = tmpVect;	
	}
	//	Add the new pair into the latencyMap
	latencyMap[inNode] 
		= new pair<struct timeval, uint32_t>(nextTimeOut, latencyUS);
	return 0;
}
	
int LatencyCache::eraseEntry(const NodeIdent& inNode) {
	map<NodeIdent, pair<struct timeval, uint32_t>*, ltNodeIdent>::iterator
		findIt = latencyMap.find(inNode);
	if (findIt != latencyMap.end()) {
		//	Get the pair that contains the latency and timeout info
		pair<struct timeval, uint32_t>* curPair = findIt->second;
		//	We can remove the entry from latencyMap now
		latencyMap.erase(findIt);
		//	Get the corresponding vector from timeoutMap
		map<struct timeval, vector<NodeIdent>*, timevalLT>::iterator
			findTO = timeoutMap.find(curPair->first);
		if (findTO == timeoutMap.end()) {
			ERROR_LOG("Data structure in inconsistent state\n");
			assert(false);
		}						
		delete curPair;	//	We no longer need the pair
		vector<NodeIdent>* toVect = findTO->second;
		//	Iterate through the vector to remove inNode
		for (u_int i = 0; i < toVect->size(); i++) {
			if ((*toVect)[i].addr == inNode.addr &&	
					(*toVect)[i].port == inNode.port) {
				(*toVect)[i] = toVect->back();
				toVect->pop_back();
				break;
			}
		}
		//	If vector is now empty, remove the entry from timeoutMap
		//	and also delete the vector
		if (toVect->empty()) {
			timeoutMap.erase(findTO);
			delete toVect;				
		}
	}
	return 0;
}

