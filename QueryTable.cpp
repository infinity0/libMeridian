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

int QueryTable::addTimeout(Query* inQuery) {
	struct timeval tv = inQuery->timeOut();	
	normalizeTime(tv);				// 	Normalized to standard form	
	queryTimeoutMap[inQuery] = tv;	//	Insert into query timeout map
	//	Now insert it into timeout query map (really multimap)
	map<struct timeval, vector<Query*>*, timevalLT>::iterator findIt 
		= timeoutQueryMap.find(tv);
	if (findIt != timeoutQueryMap.end()) {
		(findIt->second)->push_back(inQuery);
	} else {
		vector<Query*>* newVector = new vector<Query*>();
		newVector->push_back(inQuery);
		timeoutQueryMap[tv] = newVector;
	}
	return 0;				
}
	
int QueryTable::removeOldTimeout(Query* inQuery) {
	// Retrieve old timemout and remove from query-timeout map		
	struct timeval tv = retrieveOldTimeout(inQuery);
	queryTimeoutMap.erase(inQuery);
	// Remove entry from timeout-query map		
	map<struct timeval, vector<Query*>*, timevalLT>::iterator findTime
		= timeoutQueryMap.find(tv);
	if (findTime == timeoutQueryMap.end()) {
		assert(false); // Not found, should not be possible		
	}
	vector<Query*>* tmpVect = findTime->second;
	bool foundQuery = false;
	for (u_int i = 0; i < tmpVect->size(); i++) {
		if ((*tmpVect)[i] == inQuery) {
			foundQuery = true;
			(*tmpVect)[i] = (*tmpVect)[tmpVect->size()-1];
			tmpVect->pop_back();
			break;					
		}
	}
	assert(foundQuery); // Logic error otherwise
	//	If the vector is now empty
	if (tmpVect->size() == 0) {
		timeoutQueryMap.erase(findTime);
		delete tmpVect;	// Vector empty, delete it
	}
	return 0;
}
	
struct timeval QueryTable::retrieveOldTimeout(Query* inQuery) {
	map<Query*, struct timeval, queryLT>::iterator tmpIt = 
		queryTimeoutMap.find(inQuery);
	if (tmpIt == queryTimeoutMap.end()) {
		assert(false); 	// Every query should have a timeout		
	}
	return tmpIt->second;			
}	
	
void QueryTable::normalizeTime(struct timeval& tv) {
	if (tv.tv_usec > MICRO_IN_SECOND) {
		u_int extraSeconds = (tv.tv_usec / MICRO_IN_SECOND);			
		tv.tv_sec += extraSeconds;
		tv.tv_usec -= (extraSeconds * MICRO_IN_SECOND);
	}	
}
	
int QueryTable::updateTimeout(Query* inQuery) {
	removeOldTimeout(inQuery); 		// 	Old timeout no longer valid		
	if (inQuery->isFinished()) {
		delete inQuery; 				// 	Done with query
	} else {			
		addTimeout(inQuery); 			//	Set the new timeout				
	}
	return 0;
}
		
int QueryTable::insertNewQuery(Query* inQuery) {
	if (queryTimeoutMap.find(inQuery) != queryTimeoutMap.end()) {
		return -1;	// Query already exists
	}
	return addTimeout(inQuery);
}

int QueryTable::notifyQPacket(uint64_t id, const NodeIdent& remoteNode, 
		const char* packet, int packetSize) {
	SearchQuery tmpQ(id);
	map<Query*, struct timeval, queryLT>::iterator findIt 
		= queryTimeoutMap.find(&tmpQ);
	if (findIt == queryTimeoutMap.end()) {
		return -1;	
	}
	Query* curQuery = findIt->first; 	// 	Actual query
	curQuery->handleEvent(remoteNode, packet, packetSize);
	return updateTimeout(curQuery);
}

int QueryTable::notifyQLatency(uint64_t id,
	const vector<NodeIdentLat>& in_remoteNodes) {
//	const map<NodeIdent, u_int, ltNodeIdent>& in_remoteNodes) {
//	const NodeIdent& remoteNode, u_int latency_us) {
	SearchQuery tmpQ(id);
	map<Query*, struct timeval, queryLT>::iterator findIt 
		= queryTimeoutMap.find(&tmpQ);
	if (findIt == queryTimeoutMap.end()) {
		return -1;	
	}
	Query* curQuery = findIt->first; 	// 	Actual query
	curQuery->handleLatency(in_remoteNodes);
	return updateTimeout(curQuery);
}

int QueryTable::handleTimeout() {
	struct timeval curTime;
	gettimeofday(&curTime, NULL);
	normalizeTime(curTime);		
	//	Aggregate all timed out queries
	vector<Query*> deleteQueries;
	map<struct timeval, vector<Query*>*, timevalLT>::iterator it 
		= timeoutQueryMap.begin();			
	for (; it != timeoutQueryMap.end(); it++) {
		struct timeval s1 = it->first;			
		if ((s1.tv_sec < curTime.tv_sec) || 
				((s1.tv_sec == curTime.tv_sec) && 
				(s1.tv_usec <= curTime.tv_usec))) {
			//	Push all the timed out queries in here
			vector<Query*>* curTimeoutVect = it->second;				
			for (u_int i = 0; i < curTimeoutVect->size(); i++) {
				deleteQueries.push_back((*curTimeoutVect)[i]);
			}
		}
	}
	//	Now iterate through all the queries that have timeout and either
	//	delete them or update to new timeout value
	for (u_int i = 0; i < deleteQueries.size(); i++) {
		Query* curQuery = deleteQueries[i];
		curQuery->handleTimeout();
		updateTimeout(curQuery);
		//if (curQuery->isFinished()) {				
		//	removeOldTimeout(curQuery);
		//	delete curQuery;
		//} else {
		//	updateTimeout(curQuery);	
		//}
	}
	return 0;		
}

