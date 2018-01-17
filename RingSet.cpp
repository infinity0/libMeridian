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

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "RingSet.h"

int RingSet::getRingNumber(u_int latencyUS) {
	double latencyMS = (latencyUS / 1000.0);	
	int ringNumber = (int)ceil((log(latencyMS) / log((double)exponentBase)));
	//	If node is really far away, put it in the maximum ring
	if (ringNumber >= MAX_NUM_RINGS) {
		ringNumber = MAX_NUM_RINGS - 1;				
	} else if (ringNumber < 0) {
		ringNumber = 0;	
	}
	return ringNumber;
}

int RingSet::eraseNode(const NodeIdent& inNode) {
	map<NodeIdent, u_int, ltNodeIdent>::iterator findIt 
		= nodeLatencyUS.find(inNode);			
	if (findIt == nodeLatencyUS.end()) {
		return -1;	// Node does not exist
	}
	
	int ringNumber = getRingNumber(findIt->second);
	if (eraseNode(inNode, ringNumber) == -1) {
		assert(false);	// Logic error	
	}
	return 0;		
}

int RingSet::eraseNode(const NodeIdent& inNode, int ring) {
	if (ringFrozen[ring]) {
		WARN_LOG("Cannot erase node from frozen ring\n");
		return 0;	
	}
	bool foundNode = false;
	for (u_int i = 0; i < primaryRing[ring].size(); i++) {
		NodeIdent tmp = primaryRing[ring][i];
		if ((tmp.addr == inNode.addr) && (tmp.port == inNode.port)) {
			foundNode = true;
			primaryRing[ring][i] = primaryRing[ring].back();
			primaryRing[ring].pop_back();
			// Pick a random node from secondary ring to insert
			if (secondaryRing[ring].size() > 0) {
				primaryRing[ring].push_back(secondaryRing[ring].front());
				secondaryRing[ring].pop_front();
			}
			break;	// Found it, exit loop
		}					
	}
	//	If not in primary, must be in secondary
	if (!foundNode) {
		deque<NodeIdent>::iterator dIt
			= secondaryRing[ring].begin();
		for (; dIt != secondaryRing[ring].end(); dIt++) {
			if ((dIt->addr == inNode.addr) && (dIt->port == inNode.port)) {
				//	Found it, erase and end
				secondaryRing[ring].erase(dIt);
				foundNode = true;
				break;
			}
		}
	}		
	if (!foundNode) {
		return -1; //	If deleting a node that doesn't exist, return -1
	}
	nodeLatencyUS.erase(inNode);	// Erase from latency map
	rendvMapping.erase(inNode);		// Erase from rendavous map
	return 0;
}

int RingSet::insertNode(
	const NodeIdent& inNode, u_int latencyUS) {
	NodeIdent rend = {0, 0};	
	map<NodeIdent,	NodeIdent, ltNodeIdent>:: iterator findRend =
		rendvMapping.find(inNode);
	if (findRend != rendvMapping.end()) {
		rend = findRend->second;
	}
	return insertNode(inNode, latencyUS, rend);
}

int RingSet::insertNode(
		const NodeIdent& inNode, u_int latencyUS, const NodeIdent& rend) {
	//	Store/update rendavous information
	//	Okay to update even if ring frozen
	if ((rend.addr) != 0 && (rend.port) != 0) {
		rendvMapping[inNode] = rend; 
	}				
	int ringNum = getRingNumber(latencyUS);	// New ring number
	if (ringFrozen[ringNum]) {
		WARN_LOG("Cannot update frozen ring\n");
		return 0;	
	}
	map<NodeIdent, u_int, ltNodeIdent>::iterator findIt 
		= nodeLatencyUS.find(inNode);			
	if (findIt != nodeLatencyUS.end()) {
		int prevRingNum = getRingNumber(findIt->second);
		if (prevRingNum == ringNum) {
			// If old and new ring is the same, just need to update latency 
			nodeLatencyUS[inNode] = latencyUS;		
			return 0;
		} else {
			// If old ring is frozen, just return
			if (ringFrozen[prevRingNum]) {
				WARN_LOG("Cannot update frozen ring\n");
				return 0;	
			}
			// If node has changed rings, remove node from old ring			
			if (eraseNode(inNode, prevRingNum) == -1) {
				assert(false);	// Logic error	
			}
		}
	}
/*	
	u_int netAddr = htonl(inNode.addr);
	printf("Adding node %s:%d of latency %0.2f ms to ring %d\n",
		 	inet_ntoa(*(struct in_addr*)&netAddr), inNode.port, 
			latencyUS / 1000.0, ringNum);
*/			
	//	Push new node into rings
	if (primaryRing[ringNum].size() < primarySize) {
		primaryRing[ringNum].push_back(inNode);
	} else {
		if (secondaryRing[ringNum].size() >= secondarySize) {
			// Remove oldest member of secondary ring
			if (eraseNode(secondaryRing[ringNum].front(), ringNum) == -1) {
				assert(false);	// Logic error	
			}				
		}
		secondaryRing[ringNum].push_back(inNode);
	}
	//	Store latency of new node
	nodeLatencyUS[inNode] = latencyUS;
	return 0;
}	

