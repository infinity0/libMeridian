#ifndef CLASS_RING_SET
#define CLASS_RING_SET

#include <math.h>
#include <vector>
#include <deque>
#include <map>
#include <set>
#include "Marshal.h"

#define MAX_NUM_RINGS	10

class RingSet {
private:
	vector<NodeIdent>		primaryRing[MAX_NUM_RINGS];	
	deque<NodeIdent>		secondaryRing[MAX_NUM_RINGS];
	bool 					ringFrozen[MAX_NUM_RINGS];
	u_int					primarySize;
	u_int					secondarySize;
	u_int					exponentBase;
	
	map<NodeIdent, u_int, ltNodeIdent> 			nodeLatencyUS;
	map<NodeIdent, NodeIdent, ltNodeIdent>		rendvMapping;
		
public:
	RingSet(u_int prim_ring_size, u_int second_ring_size, u_int base) 
		: 	primarySize(prim_ring_size), secondarySize(second_ring_size),
			exponentBase(base) {
		for (u_int i = 0; i < MAX_NUM_RINGS; i++) {
			ringFrozen[i] = false;			
		}
	}
	u_int nodesInPrimaryRing() {
		return primarySize;	
	}
	
	u_int nodesInSecondaryRing() {
		return secondarySize;	
	}	
			
	int freezeRing(int ringNum) {
		if (ringNum < 0 || ringNum >= getNumberOfRings()) {
			return -1;		
		}
		ringFrozen[ringNum] = true;
		return 0;
	}
	
	int unfreezeRing(int ringNum) { 	
		if (ringNum < 0 || ringNum >= getNumberOfRings()) {
			return -1;		
		}
		ringFrozen[ringNum] = false;
		return 0;	
	}
	
	int getNumberOfRings() const 	{ return MAX_NUM_RINGS; }
	
	const vector<NodeIdent>* returnPrimaryRing(int ringNum) const {
		if (ringNum >= MAX_NUM_RINGS) {
			return NULL;
		}
		return &(primaryRing[ringNum]);	
	}
	
	bool isPrimRingFull(int ringNum) {
		assert(ringNum < MAX_NUM_RINGS);
		if (primaryRing[ringNum].size() == primarySize) {
			return true;	
		}
		return false;		
	}
	
	bool isSecondRingEmpty(int ringNum) {
		assert(ringNum < MAX_NUM_RINGS);
		if (secondaryRing[ringNum].empty()) {
			return true;	
		}
		return false;				
	}
	
	const deque<NodeIdent>* returnSecondaryRing(int ringNum) const {
		if (ringNum >= MAX_NUM_RINGS) {
			return NULL;
		}
		return &(secondaryRing[ringNum]);	
	}	
	
	int getNodeLatency(const NodeIdent& inNode, u_int* latencyUS) const {
		map<NodeIdent, u_int, ltNodeIdent>::const_iterator it
			= nodeLatencyUS.find(inNode);
		if (it == nodeLatencyUS.end()) {
			return -1;		
		}
		*latencyUS = it->second;	
		return 0;
	}
	
	int	rendvLookup(const NodeIdent& remoteNode, NodeIdent& rendvNode) {
		map<NodeIdent, NodeIdent, ltNodeIdent>::iterator it
			= rendvMapping.find(remoteNode);
		if (it == rendvMapping.end()) {
			return -1;			
		}
		rendvNode = it->second;
		return 0;
	}
	
	bool eligibleForReplacement(int ringNum) {	
		if (ringFrozen[ringNum]) {
			WARN_LOG("Cannot update frozen ring\n");
			return false;	
		}		
		if (ringNum >= MAX_NUM_RINGS) {
			return false;
		}
		if (isPrimRingFull(ringNum) && !isSecondRingEmpty(ringNum)) {			
			NodeIdent dummy;
			u_int numEligible = 0;		
			for (u_int i = 0; i < primaryRing[ringNum].size(); i++) {
				if (rendvLookup((primaryRing[ringNum][i]), dummy) == -1) {
					numEligible++; // No rendavous
				}
			}
			for (u_int i = 0; i < secondaryRing[ringNum].size(); i++) {	
				if (rendvLookup((secondaryRing[ringNum][i]), dummy) == -1) {
					numEligible++;	// No rendavous
				}			
			}			
			if (numEligible > primarySize) {
				WARN_LOG("********** Eligible for replacement *********\n"); 
				return true;	
			}
			WARN_LOG("********** NOT Eligible for replacement *********\n");			
			//	Equality case. We want to move all the non firewalled host to 
			//	the primary ring, as firewalled hosts are really secondary
			//	citizens. TODO: This needs to be tested
			if (numEligible == primarySize) {
				WARN_LOG("********** Performing swap *********\n");
				//	Swap ineligble nodes from primary with eligible ones
				//	in the secondary ring
				u_int j = 0;				
				for (u_int i = 0; i < primaryRing[ringNum].size(); i++) {
					if (rendvLookup((primaryRing[ringNum][i]), dummy) != -1) {
						//	Requires rendavous, find secondary that doesn't						
						for (; j < secondaryRing[ringNum].size(); j++) {
							if (rendvLookup(
								(secondaryRing[ringNum][j]), dummy) != -1) {
								continue;	// Requires rendavous, skip
							}
							//	Swap primary and secondary
							NodeIdent tmpIdent = secondaryRing[ringNum][j];
							secondaryRing[ringNum][j] = primaryRing[ringNum][i];
							primaryRing[ringNum][i] = tmpIdent;
							j++; // Can increment one more in the loop 
							break;
						} 						
					}
				}									
			} 			
		}
		return false;
	}
		
	int getRingNumber(u_int latencyUS);	
	int eraseNode(const NodeIdent& inNode);	
	int eraseNode(const NodeIdent& inNode, int ring);	
	int insertNode(
		const NodeIdent& inNode, u_int latencyUS, const NodeIdent& rend);
	// Preserves existing rendavous mapping
	int insertNode(const NodeIdent& inNode, u_int latencyUS);
	
	int getRandomNodes(vector<NodeIdentRendv>& randNodes) {
		for (int i = 0; i < MAX_NUM_RINGS; i++) {
			if (primaryRing[i].size() > 0) {				
				NodeIdent cur = primaryRing[i][rand() % primaryRing[i].size()];
				NodeIdentRendv nodeToInsert = {cur.addr, cur.port, 0, 0};
				map<NodeIdent, NodeIdent, ltNodeIdent>::iterator findRend 
					= rendvMapping.find(cur);
				if (findRend != rendvMapping.end()) {
					nodeToInsert.addrRendv = (findRend->second).addr;
					nodeToInsert.portRendv = (findRend->second).port;
				}					
				randNodes.push_back(nodeToInsert);	
			}
		}
		return 0;		
	}
	
	int membersDump(int ringNum, set<NodeIdent, ltNodeIdent>& ringMembers) {
		if (ringNum >= MAX_NUM_RINGS) {
			return -1;
		}
		for (u_int i = 0; i < primaryRing[ringNum].size(); i++) {
			ringMembers.insert(primaryRing[ringNum][i]);	
		}
		for (u_int i = 0; i < secondaryRing[ringNum].size(); i++) {
			ringMembers.insert(secondaryRing[ringNum][i]);	
		}
		return 0;
	}

/*
	int fillVector(u_int avgLatencyUS, double betaRatio,
			set<NodeIdent, ltNodeIdent>& ringMembers) {
		if (betaRatio <= 0.0 || betaRatio >= 1.0) {
			ERROR_LOG("Illegal Beta Ratio\n");
			betaRatio = 0.5;	// Set it to default beta
		}
		//u_int upperBound = (u_int) ceil(1.5 * avgLatencyUS);
		//u_int lowerBound = (u_int) floor(0.5 * avgLatencyUS);
		u_int upperBound = (u_int) ceil((1.0 + betaRatio) * avgLatencyUS);
		u_int lowerBound = (u_int) floor((1.0 - betaRatio) * avgLatencyUS);
		if (lowerBound > upperBound) {	// underflow or overflow error?
			//	This shouldn't ever happen since the timeouts is much
			//	smaller than the overflow values
			upperBound = UINT_MAX;
			lowerBound = 0;
		}
		int upperRing = getRingNumber(upperBound);
		int lowerRing = getRingNumber(lowerBound);				
		for (int i = lowerRing; i <= upperRing; i++) { 
			for (u_int j = 0; j < primaryRing[i].size(); j++) {
				u_int curLatencyUS;
				if (getNodeLatency(primaryRing[i][j], &curLatencyUS) == -1) {
					assert(false);	// This shouldn't happen
					continue;	
				}
				if ((curLatencyUS <= upperBound) && 
						(curLatencyUS >= lowerBound)) {
					ringMembers.insert(primaryRing[i][j]);					
				}
			}			
		}
		return 0;		
	}
*/		
	
	int fillVector(u_int minAvgUS, u_int maxAvgUS, double betaRatio, 
			set<NodeIdentRendv, ltNodeIdentRendv>& ringMembers) {
		if (betaRatio <= 0.0 || betaRatio >= 1.0) {
			ERROR_LOG("Illegal Beta Ratio\n");
			betaRatio = 0.5;	// Set it to default beta
		}
		//u_int upperBound = (u_int) ceil(1.5 * avgLatencyUS);
		//u_int lowerBound = (u_int) floor(0.5 * avgLatencyUS);
		u_int upperBound = (u_int) ceil((1.0 + betaRatio) * maxAvgUS);
		u_int lowerBound = (u_int) floor((1.0 - betaRatio) * minAvgUS);
		if (lowerBound > upperBound) {	// underflow or overflow error?
			//	This shouldn't ever happen since the timeouts is much
			//	smaller than the overflow values
			upperBound = UINT_MAX;
			lowerBound = 0;
		}
		int upperRing = getRingNumber(upperBound);
		int lowerRing = getRingNumber(lowerBound);				
		for (int i = lowerRing; i <= upperRing; i++) { 
			for (u_int j = 0; j < primaryRing[i].size(); j++) {
				u_int curLatencyUS;
				if (getNodeLatency(primaryRing[i][j], &curLatencyUS) == -1) {
					assert(false);	// This shouldn't happen
					continue;	
				}
				if ((curLatencyUS <= upperBound) && 
						(curLatencyUS >= lowerBound)) {
					NodeIdent rendvIdent;
					NodeIdentRendv tmpRendv = 
						{primaryRing[i][j].addr, primaryRing[i][j].port, 0, 0};
					if (rendvLookup(primaryRing[i][j], rendvIdent) != -1) {
						tmpRendv.portRendv = rendvIdent.port;
						tmpRendv.addrRendv = rendvIdent.addr;
					}																
					//ringMembers.insert(primaryRing[i][j]);
					ringMembers.insert(tmpRendv);					
				}
			}			
		}
		return 0;		
	}	
	
	
	int setRingMembers(int ringNum, const vector<NodeIdent>& primRing,
			const vector<NodeIdent>& secondRing) {
		//	Make sure ringNum is valid
		if (ringNum < 0 || ringNum >= getNumberOfRings()) {
			return -1;
		}
		//	Make sure ring size is not validated
		if ((primRing.size() > nodesInPrimaryRing()) || 
				(secondRing.size() > nodesInSecondaryRing())) {
			return -1;
		}
		//	Add all existing nodes to tmpSet
		set<NodeIdent, ltNodeIdent> tmpSet;	
		for (u_int i = 0; i < primaryRing[ringNum].size(); i++) {
			tmpSet.insert(primaryRing[ringNum][i]);
		}
		deque<NodeIdent>::iterator it = secondaryRing[ringNum].begin();
		for (; it != secondaryRing[ringNum].end(); it++) {
			tmpSet.insert(*it);	
		}
		//	Make sure the old set and new set are the same size
		if ((primRing.size() + secondRing.size()) != tmpSet.size()) {
			return -1;	
		}		
		//	Now make sure every node in primRing and secondRing are in tmpSet
		for (u_int i = 0; i < primRing.size(); i++) {
			if (tmpSet.find(primRing[i]) == tmpSet.end()) {
				return -1;	
			}
		}
		for (u_int i = 0; i < secondRing.size(); i++) {
			if (tmpSet.find(secondRing[i]) == tmpSet.end()) {
				return -1;	
			}
		}
		//	Clear old ring members and relocate them to new position
		primaryRing[ringNum].clear();
		secondaryRing[ringNum].clear();
		for (u_int i = 0; i < primRing.size(); i++) {
			primaryRing[ringNum].push_back(primRing[i]);	
		}		
		for (u_int i = 0; i < secondRing.size(); i++) {
			secondaryRing[ringNum].push_back(secondRing[i]);	
		}
		return 0;
	}
									
	~RingSet() {}
};

#endif
