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
#include <math.h>
#include <ucontext.h>
#include "Marshal.h"
#include "Query.h"
#include "RingSet.h"
#include "MeridianProcess.h"
#include "GramSchmidtOpt.h"

AddNodeQuery::AddNodeQuery(const NodeIdentRendv& in_remote, 
							MeridianProcess* in_process) 
		: remoteNode(in_remote), finished(false), meridProcess(in_process) {	
	qid = meridProcess->getNewQueryID();
	computeTimeout(MAX_RTT_MS * MICRO_IN_MILLI, &timeoutTV);		
}

int AddNodeQuery::handleLatency(
		const vector<NodeIdentLat>& in_remoteNodes) {
	//	There must be the srcNode as the first entry, and the target node
	//	(itself) as the second entry
	if (in_remoteNodes.size() != 2) {
		ERROR_LOG("Received RET_PING, but not correct size\n");
		return -1;		
	}
	if (in_remoteNodes[0].addr != remoteNode.addr ||
		in_remoteNodes[0].port != remoteNode.port) {
		ERROR_LOG("Received packet from unexpected node\n");
		return -1;
	}	
	u_int latencyUS = in_remoteNodes[1].latencyUS;
	NodeIdent mainRemoteNode = {remoteNode.addr, remoteNode.port};
	NodeIdent rendvRemoteNode = {remoteNode.addrRendv, remoteNode.portRendv};	
	meridProcess->getRings()->insertNode(
		mainRemoteNode, latencyUS, rendvRemoteNode);	
	NodeIdentLat outNIL = {remoteNode.addr, remoteNode.port, latencyUS};
	vector<NodeIdentLat> subVect;
	subVect.push_back(outNIL);
	for (u_int i = 0; i < subscribers.size(); i++) {
		meridProcess->getQueryTable()->notifyQLatency(subscribers[i], subVect);	
	}	
	WARN_LOG("Add node query handled successfully\n");	
	//	Adding to cache
	NodeIdent curIdent = {remoteNode.addr, remoteNode.port};
	meridProcess->pingCacheInsert(curIdent, latencyUS);		
	finished = true;
	return 0;
}

int AddNodeQuery::handleEvent(
		const NodeIdent& in_remote, const char* inPacket, int packetSize) {
	if (inPacket[0] != PONG) {
		ERROR_LOG("Expecting PONG packet, received something else\n");
		return -1;	// Not pong packet
	}
	if (in_remote.addr != remoteNode.addr || 
		in_remote.port != remoteNode.port) {
		ERROR_LOG("Received packet from unexpected node\n");
		return -1;
	}
	struct timeval curTime;		
	gettimeofday(&curTime, NULL);
	u_int latencyUS = (curTime.tv_sec - startTime.tv_sec) * MICRO_IN_SECOND 
		+ curTime.tv_usec - startTime.tv_usec;
	NodeIdent mainRemoteNode = {remoteNode.addr, remoteNode.port};
	NodeIdent rendvRemoteNode = {remoteNode.addrRendv, remoteNode.portRendv};	
	meridProcess->getRings()->insertNode(
		mainRemoteNode, latencyUS, rendvRemoteNode);			
	//meridProcess->getRings()->insertNode(remoteNode, latencyUS);	
	NodeIdentLat outNIL = {remoteNode.addr, remoteNode.port, latencyUS};
	vector<NodeIdentLat> subVect;
	subVect.push_back(outNIL);
	for (u_int i = 0; i < subscribers.size(); i++) {
		meridProcess->getQueryTable()->notifyQLatency(subscribers[i], subVect);	
	}
	//	Adding to cache
	NodeIdent curIdent = {remoteNode.addr, remoteNode.port};
	meridProcess->pingCacheInsert(curIdent, latencyUS);		
	finished = true;
	return 0;
}

int AddNodeQuery::handleTimeout() {
	WARN_LOG("######################### QUERY TIMEOUT ###################\n");
	NodeIdent tmpIdent = {remoteNode.addr, remoteNode.port};	
	meridProcess->getRings()->eraseNode(tmpIdent);		
	finished = true;
	return 0;
}

int AddNodeQuery::init() {
	gettimeofday(&startTime, NULL);	
	//	Firewall support for AddNodeQuery is special. If target is behind
	//	a firewall, instead of sending a pushed packet, we perform a req-ping
	NodeIdent rendvInfo = meridProcess->returnRendv();	
	//	If the target is not behind a firewall	
	if (remoteNode.addrRendv == 0 && remoteNode.portRendv == 0) {
		PingPacket pingPacket(qid);
		RealPacket* inPacket = new RealPacket(remoteNode);
		if (pingPacket.createRealPacket(*inPacket) == -1) {
			delete inPacket;			
			return -1;
		}
		meridProcess->addOutPacket(inPacket);
	} else {
		// Target is behind a firewall
		if (rendvInfo.addr == 0 && rendvInfo.port == 0) {
			// But we're not, so perform a ReqProbe
			NodeIdent emptyIdent = {0, 0};
			set<NodeIdent, ltNodeIdent> tmpSet;			
			tmpSet.insert(emptyIdent);
			ReqProbePing* newQuery =
				new ReqProbePing(remoteNode, tmpSet, meridProcess); 
			if (meridProcess->getQueryTable()->insertNewQuery(newQuery) == -1) {			
				delete newQuery;
			} else {
				newQuery->subscribeLatency(qid);
				newQuery->init();
			}
		} 
		// else we're both behind firewall, we give up and wait for timeout
	}
	return 0;
}

int AddNodeQuery::subscribeLatency(uint64_t in_qid) {
	subscribers.push_back(in_qid);
	return 0;		
}

//GossipQuery::GossipQuery(NodeIdent& in_remote,
GossipQuery::GossipQuery(NodeIdentRendv& in_remote,
						MeridianProcess* in_process) 
		: 	remoteNode(in_remote), finished(false),
			meridProcess(in_process) {
	qid = meridProcess->getNewQueryID();
	computeTimeout(MAX_RTT_MS * MICRO_IN_MILLI, &timeoutTV);
}

int GossipQuery::init() {
	WARN_LOG("Starting gossip query\n");
	gettimeofday(&startTime, NULL);
#ifdef DEBUG	
	u_int netAddr = htonl(remoteNode.addr);
	WARN_LOG_2("Sending gossip to node %s:%d\n",
		 	inet_ntoa(*(struct in_addr*)&netAddr), remoteNode.port);
#endif
	AddNodeQuery* newQuery = new AddNodeQuery(remoteNode, meridProcess);
	if (meridProcess->getQueryTable()->insertNewQuery(newQuery) == -1) {			
		delete newQuery;
		return -1;
	}
	newQuery->subscribeLatency(qid);
	newQuery->init();
	return 0;
}

int GossipQuery::handleTimeout() {
	finished = true;
	return 0;
}

int GossipQuery::fillGossipPacket(GossipPacketGeneric& in_packet, 
		const NodeIdentRendv& in_target, MeridianProcess* in_merid) {
	vector<NodeIdentRendv> randomNodes;
	in_merid->getRings()->getRandomNodes(randomNodes);
	// Return okay even if the gossip packet itself is empty
	for (u_int i = 0; i < randomNodes.size(); i++) {
		NodeIdentRendv curR = randomNodes[i];
		// Don't send remote node itself
		if (curR.addr != in_target.addr || curR.port != in_target.port) {
			in_packet.addNode(
				curR.addr, curR.port, curR.addrRendv, curR.portRendv);
		}		
	}
	return 0;
}

int GossipQuery::handleLatency(
		const vector<NodeIdentLat>& in_remoteNodes) {
//		const map<NodeIdent, u_int, ltNodeIdent>& in_remoteNodes) {
//		const NodeIdent& in_remote, u_int latency_us) {
	if (in_remoteNodes.size() != 1) {
		return -1;		
	}	
	NodeIdent in_remote = {in_remoteNodes[0].addr, in_remoteNodes[0].port};
	// u_int latency_us = tmpMapIt->second; 
	//	Check to see whether it is the expected addr
	if (in_remote.addr != remoteNode.addr || 
		in_remote.port != remoteNode.port) {
		ERROR_LOG("Received packet from unexpected node\n");
		return -1;
	}			
	//	Now send a gossip packet
	NodeIdent tmpRendvNode = meridProcess->returnRendv();
	GossipPacketPush gPacket(qid, tmpRendvNode.addr, tmpRendvNode.port);
/*	
	vector<NodeIdentRendv> randomNodes;
	meridProcess->getRings()->getRandomNodes(randomNodes);	
	if (randomNodes.size() > 0 ) {
		for (u_int i = 0; i < randomNodes.size(); i++) {
			NodeIdentRendv curR = randomNodes[i];
			// Don't send remote node itself
			if (curR.addr != remoteNode.addr || curR.port != remoteNode.port) {
				gPacket.addNode(
					curR.addr, curR.port, curR.addrRendv, curR.portRendv);
			}		
		}
*/
	if (fillGossipPacket(gPacket, remoteNode, meridProcess) == 0) {
		WARN_LOG("Creating gossip packet ###############\n");
		RealPacket* inPacket = new RealPacket(remoteNode);
		if (gPacket.createRealPacket(*inPacket) == -1) {
			delete inPacket;	
		} else {
			meridProcess->addOutPacket(inPacket);
		}
	}
	finished = true;
	return 0;	
}

void QueryScheduler::computeSchedTimeout() {
	if (numInitInterval > 0) {
		computeTimeout(initInterval_MS * MICRO_IN_MILLI, &timeoutTV);
		numInitInterval--;
	} else {
		computeTimeout(ssInterval_MS * MICRO_IN_MILLI, &timeoutTV);
	}		
}	

QueryScheduler::QueryScheduler(u_int in_initInterval_MS, 
			u_int in_numInitInterval, u_int in_ssInterval_MS, 
			MeridianProcess* in_process, SchedObject* in_schedObj)
		:	schedObj(in_schedObj), initInterval_MS(in_initInterval_MS), 
			numInitInterval(in_numInitInterval), 
			ssInterval_MS(in_ssInterval_MS), meridProcess(in_process),
			finished(false) {
	qid = meridProcess->getNewQueryID();
	computeSchedTimeout();
}

int QueryScheduler::handleTimeout() {
	WARN_LOG("QueryScheduler activated\n");
	schedObj->runOnce();
	computeSchedTimeout();
	return 0;	
}

int QueryScheduler::removeScheduler() 	{ 
	finished = true;
	meridProcess->getQueryTable()->updateTimeout(this);
	return 0;
}

int SchedGossip::runOnce() {
	meridProcess->performGossip();
	return 0;	
}

int SchedRingManage::runOnce() {
	meridProcess->performRingManagement();
	return 0;	
}

RingManageQuery::RingManageQuery(int in_ringNum, MeridianProcess* in_process) 
		: 	ringNum(in_ringNum), finished(false), meridProcess(in_process) {
	qid = meridProcess->getNewQueryID();	
	computeTimeout(2 * MAX_RTT_MS * MICRO_IN_MILLI, &timeoutTV);	
}

int RingManageQuery::init() {
	NodeIdent dummy;
	gettimeofday(&startTime, NULL);
	if (ringNum < 0 || 
		(ringNum >= meridProcess->getRings()->getNumberOfRings())) {
		return -1;		
	}
	meridProcess->getRings()->membersDump(ringNum, remoteNodes);	
	meridProcess->getRings()->freezeRing(ringNum);	
	//	Create Req packets to send to each one
	//	Only send packets to nodes that are not behind firewalls
	set<NodeIdent, ltNodeIdent>::iterator outerIt = remoteNodes.begin();
	for (; outerIt != remoteNodes.end(); outerIt++) {				
		if (meridProcess->getRings()->rendvLookup(*outerIt, dummy) != -1) {			
			continue; // Requires rendavous, don't add
		}
		NodeIdent tmpRendvNode = meridProcess->returnRendv();		
		ReqMeasurePing req(qid, tmpRendvNode.addr, tmpRendvNode.port);
		set<NodeIdent, ltNodeIdent>::iterator innerIt = remoteNodes.begin();
		for (; innerIt != remoteNodes.end(); innerIt++) {
			if (((outerIt->addr == innerIt->addr) &&
				(outerIt->port == innerIt->port)) || 
				(meridProcess->getRings()->rendvLookup(*innerIt, dummy) != -1)){
				continue;
			}
			req.addTarget(*innerIt);
		}
		RealPacket* inPacket = new RealPacket(*outerIt);
		if (req.createRealPacket(*inPacket) == -1) {
			delete inPacket;			
			continue;
		}
		meridProcess->addOutPacket(inPacket);						
	}
	return 0;
}

int RingManageQuery::handleEvent(
		const NodeIdent& in_remote, const char* inPacket, int packetSize) {
	if (inPacket[0] != RET_PING_REQ) {
		ERROR_LOG("Expecting RET_PING_REQ, received somthing else\n");
		return -1;	// Not RET_PING_REQ packet
	}
	if (remoteNodes.find(in_remote) == remoteNodes.end()) {
		ERROR_LOG("Received packet from unexpected node\n");
		return -1;
	} else {
		if (RetNodeMap.find(in_remote) != RetNodeMap.end()) {
			ERROR_LOG("Node already reported sent a RET_PING_REQ\n");
			return -1;					
		}
	}
	RetPing* ret = RetPing::parse(inPacket, packetSize);
	if (ret == NULL) {
		ERROR_LOG("RET_PING_REQ Ill-formed\n");
		return -1;
	}
	map<NodeIdent, u_int, ltNodeIdent>* newMap 
		= new map<NodeIdent, u_int, ltNodeIdent>();	
	const vector<NodeIdentLat>* retNodes = ret->returnNodes();
	for (u_int i = 0; i < retNodes->size(); i++) {
		NodeIdent tmp = {(*retNodes)[i].addr, (*retNodes)[i].port};
		if (remoteNodes.find(tmp) != remoteNodes.end()) {
			(*newMap)[tmp] = (*retNodes)[i].latencyUS;		
		}				
	}
	RetNodeMap[in_remote] = newMap;
	WARN_LOG_2("remoteNodes has %d entries, RetNodeMap has %d entries\n",
		remoteNodes.size(), RetNodeMap.size()); 
	if (RetNodeMap.size() == remoteNodes.size()) {
		WARN_LOG("^^^^^^^^^^^^Receive all RET_PING^^^^^^^^^^^^^^^^\n");
		// Done, perform ring management
		meridProcess->getRings()->unfreezeRing(ringNum);		
		performReplacement();
		finished = true;
	}
	delete ret;	// Done with packet
	return 0;
}

int RingManageQuery::removeCandidateNode(const NodeIdent& in_node) {
	//	Remove in_node from both RetNodeMap and remoteNodes
	map<NodeIdent, map<NodeIdent, u_int, ltNodeIdent>*, ltNodeIdent>::
		iterator retNodeIt = RetNodeMap.find(in_node);
	if (retNodeIt != RetNodeMap.end()) {
		delete retNodeIt->second;
		RetNodeMap.erase(retNodeIt);
	}	
	remoteNodes.erase(in_node);		
	//	Iterate through all entries and remove in_node from it
	retNodeIt = RetNodeMap.begin();
	for (; retNodeIt != RetNodeMap.end(); retNodeIt++) {
		//	Erase all bade nodes from map
		(retNodeIt->second)->erase(in_node);			
	}
	return 0;
}

int RingManageQuery::handleTimeout() {
	meridProcess->getRings()->unfreezeRing(ringNum);	
	set<NodeIdent, ltNodeIdent>::iterator it = remoteNodes.begin();
	vector<NodeIdent> badNodes;
	for (; it != remoteNodes.end(); it++) {
		if (RetNodeMap.find(*it) == RetNodeMap.end()) {
			// Did not receive response back from node, delete it
			WARN_LOG("############## REQ_PING TIMEOUT ###############\n");
			meridProcess->getRings()->eraseNode(*it);
			badNodes.push_back(*it);
		}
	}
	for (u_int i = 0; i < badNodes.size(); i++) {
		removeCandidateNode(badNodes[i]);
	}	
	performReplacement();		
	finished = true;	
	return 0;
}


double RingManageQuery::getVolume(coordT* points, int dim, int numpoints){
	boolT ismalloc= False;	/* True if qhull should free points in qh_freeqhull() or reallocation */
	char flags[250];		/* option flags for qhull, see qh_opt.htm */
	FILE *outfile= NULL;	/* output from qh_produce_output() use NULL to skip qh_produce_output() */
	FILE *errfile= NULL;	/* error messages from qhull code */
	int curlong, totlong;	/* memory remaining after qh_memfreeshort */

	/*	Write these to NULL, we don't want to keep them
	*/
	outfile = fopen("/dev/null", "r+");
	errfile = fopen("/dev/null", "r+");  

	/* Run 1: convex hull
	*/
	sprintf(flags, "qhull FA");
	int exitcode= qh_new_qhull (dim, numpoints, points, ismalloc,
		flags, outfile, errfile); 

	double returnVolume = 0.0;

	if (exitcode == 0) {
		returnVolume = qh totvol;
	} else {
		ERROR_LOG("Bad exit code for qhull hypervolume computation\n");
	}
	fclose(outfile);
	fclose(errfile);

	qh_freeqhull(!qh_ALL);                   /* free long memory  */
	qh_memfreeshort (&curlong, &totlong);    /* free short memory and memory allocator */
	if (curlong || totlong) { 
		ERROR_LOG_2("qhull internal warning (user_eg, #1): "
			"did not free %d bytes of long memory "
			"(%d pieces)\n", totlong, curlong);
	}
	return returnVolume;
}



/*	Calculates the hypervolume from the latency matrix
*/
double RingManageQuery::calculateHV(
	const int N,			// Physical size of the latencyMatrix
	const int NPrime,		// Size of the latencyMatrix in use
	double* latencyMatrix)	// Pointer to latencyMatrix
{
	/*	Time to perform Gram Schmidt to reduce the dimension by 1
	*/
	GramSchmidtOpt gs(NPrime);

	/*	tmpModMatrix is the latencyMatrix where every row subtracts
		the last row in the matrix (and the last row is all 0)
	*/
	double* tmpModMatrix = 
		(double*)malloc(sizeof(double) * NPrime * NPrime);

	for (int i = 0; i < NPrime - 1; i++) {
		// Copy from latencyMatrix to tmpModMatrix
		cblas_dcopy(NPrime, 
			&latencyMatrix[i * N], 1, &tmpModMatrix[i * NPrime], 1);

		// Perform the subtraction
		cblas_daxpy(NPrime, -1.0, &latencyMatrix[(NPrime-1) * N], 
			1, &tmpModMatrix[i * NPrime] , 1);

		gs.addVector(&tmpModMatrix[i * NPrime]);
	}

	/*	Zero out last row explictly
	*/
	for (int i = 0; i < NPrime; i++) {
		tmpModMatrix[(NPrime-1) * NPrime + i] = 0.0;
	}
			
	/*	Retrieve the orthogonal matrix that has been computed
	*/
	int orthSize;
	double* orthMatrix = gs.returnOrth(&orthSize);

	/*	Now re-create the latency matrix based on the dot product
	*/
	coordT* latencyMatrixMod = 
		(double*) malloc(sizeof(double) * orthSize * NPrime);

	cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
		NPrime, orthSize, NPrime, 1.0, tmpModMatrix, NPrime, 
		orthMatrix, NPrime, 0.0, latencyMatrixMod, orthSize);

	free(tmpModMatrix); // No no longer useful, can delete

	/*	Let's get the hypervolume
	*/
	double hyperVolume = 
		getVolume(latencyMatrixMod, orthSize, NPrime);

	free(latencyMatrixMod);	// Done, free memory of intermediate structure
	return hyperVolume;
}


/*	Used in diverse set formation, it reduces at set of nodes
	by N nodes, where the remaining nodes have the approximately highest 
	hypervolume
*/
double RingManageQuery::reduceSetByN(
					vector<NodeIdent>& inVector,	// Vector of nodes
					vector<NodeIdent>& deletedNodes,
					int numReduction,				// How many nodes to remove
					double* latencyMatrix)			// Pointer to latencyMatrix
{
	int N = inVector.size();	// Dimension of matrix
	int colSize = N;
	int rowSize = N;	
	double maxHyperVolume = 0.0;
	//	Perform reductions iteratively
	for (int rCount = 0; rCount < numReduction; rCount++) {
		bool maxHVNodeFound	= false;
		NodeIdent maxHVNode = {0, 0};
		double maxHV	= 0.0;
		maxHyperVolume	= 0.0; // Reset		

		/*	Iterate through the nodes
		*/		
		for (u_int k = 0; k < inVector.size(); k++) {
			//if (anchorNodes != NULL && 
			//		anchorNodes->find(*eIt) != anchorNodes->end()) {
			//	continue; // We want to skip this anchor, as we can't remove it
			//}
			//	Swap out the current working column
			for (int i = 0; i < rowSize; i++) {
				double tmpValue = latencyMatrix[i * N + k];
				latencyMatrix[i * N + k] = latencyMatrix[i * N + colSize - 1];
				latencyMatrix[i * N + colSize - 1] = tmpValue;
			}
			colSize--;			
			//	And the corresponding row information
			cblas_dswap(colSize, 
				&latencyMatrix[k * N], 1, &latencyMatrix[(rowSize-1) * N], 1);
			rowSize--;
			assert(rowSize == colSize);
			//	Calcuate the hypervolume without this node
			double hyperVolume = calculateHV(N, rowSize, latencyMatrix);
			/*	See if it is the minimum so far
				Rationale:	By removing this node, we still have the maxHV
							comparing to removing any other node. Therefore,
							we want to remove this node to keep a big HV
			*/
			if (hyperVolume >= maxHV) {
				maxHVNodeFound 	= true;
				maxHVNode 		= inVector[k];
				maxHV 			= hyperVolume;
			}
			//	The max hypervolume at this reduction level
			if (hyperVolume > maxHyperVolume) {
				maxHyperVolume = hyperVolume;
			}
			//	Undo row and column swap
			rowSize++;			 
			cblas_dswap(colSize, 
				&latencyMatrix[k * N], 1, &latencyMatrix[(rowSize-1) * N], 1);
			colSize++;
			for (int i = 0; i < rowSize; i++) {
				double tmpValue = latencyMatrix[i * N + k];
				latencyMatrix[i * N + k] = latencyMatrix[i * N + colSize - 1];
				latencyMatrix[i * N + colSize - 1] = tmpValue;
			}
		}
		if (maxHVNodeFound == false) {
			//	Could not reduce any further, all anchors
			assert(false); // This shouldn't really happen for any valid case 
			return 0.0;
		}
		//	For the node that we have removed, remove it from the latency
		//  matrix as well as from the vector of nodes
		for (u_int k = 0; k < inVector.size(); k++) {
			if ((inVector[k].addr == maxHVNode.addr) && 
					(inVector[k].port == maxHVNode.port)) {
				for (int i = 0; i < rowSize; i++) {
					double tmpValue = latencyMatrix[i * N + k];
					latencyMatrix[i * N + k] = latencyMatrix[i * N + colSize - 1];
					latencyMatrix[i * N + colSize - 1] = tmpValue;
				}
				colSize--;								
				cblas_dswap(colSize, 
					&latencyMatrix[k * N], 1, &latencyMatrix[(rowSize-1) * N], 1);
				rowSize--;
				deletedNodes.push_back(inVector[k]);
				inVector[k] = inVector.back();
				inVector.pop_back();
			}
		}
	}
	return maxHyperVolume;
}

double* RingManageQuery::createLatencyMatrix() { 		
	int N = remoteNodes.size();	// Dimension of matrix
	//	Allocate the matrix here
	double* latencyMatrix = (double*) malloc(sizeof(double) * N * N);		
	if (latencyMatrix == NULL) {
		return NULL;
	}	
	set<NodeIdent, ltNodeIdent>::iterator outerIt = remoteNodes.begin();		
	//	Create coordnates for each of these nodes
	for (u_int i = 0; outerIt != remoteNodes.end(); outerIt++, i++) {
		set<NodeIdent, ltNodeIdent>::iterator innerIt = remoteNodes.begin();
		for (u_int j = 0; innerIt != remoteNodes.end(); innerIt++, j++) {
			if (i == j) {
				latencyMatrix[i * N + j] = 0.0;				
			} else {
				map<NodeIdent, map<NodeIdent, u_int, ltNodeIdent>*, 
						ltNodeIdent>::iterator thisNodeMapIt 
					= RetNodeMap.find(*outerIt);				
				if (thisNodeMapIt == RetNodeMap.end()) {
					ERROR_LOG("Outer member not found: createLatMatrix\n");
					free(latencyMatrix);
					return NULL;
				} else {
					map<NodeIdent, u_int, ltNodeIdent>* thisNodeMap = 
						thisNodeMapIt->second;				
					map<NodeIdent, u_int, ltNodeIdent>::iterator findCur =
						thisNodeMap->find(*innerIt);
					if (findCur == thisNodeMap->end()) {
						ERROR_LOG("Inner member not found: createLatMatrix\n");
						free(latencyMatrix);
						return NULL;
					} else {
						// Stored in milliseconds
						latencyMatrix[i * N + j] = findCur->second / 1000.0;
					}
				}
			}
			WARN_LOG_1("%0.2f ", latencyMatrix[i * N + j]);
		}
		WARN_LOG("\n");
	}
	return latencyMatrix;
}

int RingManageQuery::performReplacement() {	
	if (remoteNodes.size() != RetNodeMap.size()) {
		ERROR_LOG("Inconsistent data structure for ring replacement\n");
		return -1;	
	}
	const u_int fullPrimRingSize 
		= meridProcess->getRings()->nodesInPrimaryRing();				
	vector<NodeIdent> removedNodes;	
	while (true) { 			
		if (remoteNodes.size() <= fullPrimRingSize) {
			return 0;	// Not enough node to perform replacement		
		}
		NodeIdent worstNode = {0, 0};
		u_int worstNodeCount = UINT_MAX;		
		// Let's ensure we have a full matrix
		map<NodeIdent, map<NodeIdent, u_int, ltNodeIdent>*, ltNodeIdent>::
			iterator retNodeIt = RetNodeMap.begin();						
		for (; retNodeIt != RetNodeMap.end(); retNodeIt++) {			
			if (((retNodeIt->second)->size() + 1) < remoteNodes.size()) {
				if ((retNodeIt->second)->size() < worstNodeCount) {
					worstNode = retNodeIt->first;
					worstNodeCount = (retNodeIt->second)->size(); 
				}											
			}
		}
		if ((worstNode.addr == 0) && (worstNode.port == 0)) {
			break;	// Done, all nodes satisfy
		} else {
			removedNodes.push_back(worstNode);
			u_int numPrev = remoteNodes.size();
			removeCandidateNode(worstNode);
			//	Remote node should decrease in size by one on every loop
			if ((remoteNodes.size() + 1) != numPrev) {
				ERROR_LOG("Inconsistent data structure for ring replacement\n");
				return -1;	
			}
		}
	}
	double* matrix = createLatencyMatrix();	
	if (matrix != NULL) {
		vector<NodeIdent> newPrimNodes;
		//vector<NodeIdent> removedNodes;
		set<NodeIdent, ltNodeIdent>::iterator it = remoteNodes.begin();
		for (; it != remoteNodes.end(); it++) {
			newPrimNodes.push_back(*it);	
		}
#ifdef DEBUG			
		double hv = reduceSetByN(newPrimNodes, removedNodes, 
			newPrimNodes.size() - fullPrimRingSize, matrix);			
		WARN_LOG_1("@@@@@@@@@@@ Max hypervolume is %0.2f @@@@@@@@@@@@\n", hv);
#else		
		reduceSetByN(newPrimNodes, removedNodes,		
			newPrimNodes.size() - fullPrimRingSize, matrix);
#endif
		free(matrix);	// Matrix must be released
		if (meridProcess->getRings()->setRingMembers(
				ringNum, newPrimNodes, removedNodes) == -1) {
			WARN_LOG("!!!!!!!!!!!! RING REPLACEMENT UNSUCCESSFUL !!!!!!!!\n");
		} else {
			WARN_LOG("************ RING REPLACEMENT SUCCESSFUL **********\n");
		}
	}
	return 0;
}

HandleReqGeneric::HandleReqGeneric(uint64_t id, 
							const NodeIdentRendv& in_srcNode, 
							const vector<NodeIdent>& in_remote, 
							MeridianProcess* in_process)
		: 	qid(id), srcNode(in_srcNode), finished(false), 
			meridProcess(in_process) {
	computeTimeout(MAX_RTT_MS * MICRO_IN_MILLI, &timeoutTV);
	//	Copy all targets over
	for (u_int i = 0; i < in_remote.size(); i++) {
		if (in_remote[i].addr == 0 && in_remote[i].port == 0) {
			//	Requesting pinging of src node. The src node really
			//	should NOT be behind a firewall
			NodeIdent tmpIdent = {srcNode.addr, srcNode.port};
			remoteNodes.insert(tmpIdent);
		} else {				
			remoteNodes.insert(in_remote[i]);
		}			
	}	
}

int HandleReqGeneric::handleLatency(
		const vector<NodeIdentLat>& in_remoteNodes) {
	//	Done (all retrieved from cache). Send return packet
	if (remoteLatencies.size() == remoteNodes.size()) {
		finished = true; // Received results from everybody
		return sendReturnPacket();
	}
	//	Not done, therefore, the vector must have at least one entry
	if (in_remoteNodes.size() != 1) {
		return -1;		
	} 
	NodeIdent in_remote = {in_remoteNodes[0].addr, in_remoteNodes[0].port};
	u_int latency_us = in_remoteNodes[0].latencyUS;	
	if (remoteNodes.find(in_remote) == remoteNodes.end()) {
		ERROR_LOG("Received result from unexpected node\n");
		return -1;
	}
	if (remoteLatencies.find(in_remote) != remoteLatencies.end()) {
		ERROR_LOG("Received duplicate results\n");
		return -1;
	}
	remoteLatencies[in_remote] = latency_us;
	if (remoteLatencies.size() != remoteNodes.size()) {
		return 0;	// More results needed
	}
	finished = true; // Received results from everybody
	return sendReturnPacket();
}

int HandleReqGeneric::handleTimeout() {
	finished = true;
	return sendReturnPacket();
}

int HandleReqGeneric::sendReturnPacket() {
	RetPing retPacket(qid);
	map<NodeIdent, u_int, ltNodeIdent>::iterator it = 
		remoteLatencies.begin();
	for (; it != remoteLatencies.end(); it++) {
		retPacket.addNode(it->first, it->second);
	}
	RealPacket* inPacket = new RealPacket(srcNode);
	if (retPacket.createRealPacket(*inPacket) == -1) {
		delete inPacket;
		return -1;		
	}	
	meridProcess->addOutPacket(inPacket);			
	return 0;
}			



int HandleReqGeneric::init() {
	//setStartTime();
	set<NodeIdent, ltNodeIdent>* curRemoteNodes = getRemoteNodes();
	set<NodeIdent, ltNodeIdent>::iterator it = curRemoteNodes->begin();
	for (; it != curRemoteNodes->end(); it++) {
		NodeIdent curIdent = *it;
/*		
		if (curIdent.addr == 0 && curIdent.port == 0) {
			//	Requesting pinging of src node. The src node really
			//	should NOT be behind a firewall			
			curIdent.addr = srcNode.addr;
			curIdent.port = srcNode.port;
		}
*/		
		uint32_t curLatencyUS;
		if (getLatency(curIdent, &curLatencyUS) == -1) {		
			ProbeQueryGeneric* newQuery = 
				createProbeQuery(curIdent, getMerid());			
			if (getMerid()->getQueryTable()->insertNewQuery(newQuery) == -1) {			
				delete newQuery;
				continue;
			}
			newQuery->subscribeLatency(getQueryID());
			newQuery->init();
		} else {
			remoteLatencies[curIdent] = curLatencyUS;
		}
	}
	if (remoteLatencies.size() == remoteNodes.size()) {
		//	It's done, tell the query it is
		vector<NodeIdentLat> dummy;
		getMerid()->getQueryTable()->notifyQLatency(getQueryID(), dummy);
	}	
	return 0;	
}


int HandleReqTCP::getLatency(const NodeIdent& inNode, uint32_t* latencyUS) {
	return getMerid()->tcpCacheGetLatency(inNode, latencyUS);
}

int HandleReqDNS::getLatency(const NodeIdent& inNode, uint32_t* latencyUS) {
	return getMerid()->dnsCacheGetLatency(inNode, latencyUS);
}

int HandleReqPing::getLatency(const NodeIdent& inNode, uint32_t* latencyUS) {
	return getMerid()->pingCacheGetLatency(inNode, latencyUS);
}

#ifdef PLANET_LAB_SUPPORT
int HandleReqICMP::getLatency(const NodeIdent& inNode, uint32_t* latencyUS) {
	return getMerid()->icmpCacheGetLatency(inNode, latencyUS);
}
#endif

ProbeQueryGeneric::ProbeQueryGeneric(const NodeIdent& in_remote, 
							MeridianProcess* in_process) 
		: 	sockFD(-1), remoteNode(in_remote), finished(false), 
			meridProcess(in_process) {			
	qid = meridProcess->getNewQueryID();
	computeTimeout(MAX_RTT_MS * MICRO_IN_MILLI, &timeoutTV);		
}

void ProbeQueryTCP::insertCache(const NodeIdent& inNode, uint32_t latencyUS) {
	getMerid()->tcpCacheInsert(inNode, latencyUS);
}

int ProbeQueryTCP::init() {
	setStartTime();	
	WARN_LOG("ProbeQueryTCP: Adding new TCP connection\n");
	setSockFD(getMerid()->addTCPConnection(getQueryID(), getRemoteNode()));
	return 0;
}

int ProbeQueryTCP::handleTimeout() {
	WARN_LOG("##################### TCP QUERY TIMEOUT ###################\n");
	int tmpFD = getSockFD(); 
	if (tmpFD != -1) {
		getMerid()->eraseTCPConnection(tmpFD);	
	}
	setFinished(true);
	return 0;
}

void ProbeQueryDNS::insertCache(const NodeIdent& inNode, uint32_t latencyUS) {
	getMerid()->dnsCacheInsert(inNode, latencyUS);
}	

int ProbeQueryDNS::init() {
	setStartTime();
	WARN_LOG("ProbeQueryDNS: Adding new DNS connection\n");
	setSockFD(getMerid()->addDNSConnection(getQueryID(), getRemoteNode()));
	return 0;
}

int ProbeQueryDNS::handleTimeout() {	 
	WARN_LOG("##################### DNS QUERY TIMEOUT ###################\n");
	WARN_LOG("Erasing old DNS connections\n");
	int tmpFD = getSockFD(); 
	if (tmpFD != -1) {
		getMerid()->eraseDNSConnection(tmpFD);	
	}
	setFinished(true);
	return 0;
}

void ProbeQueryPing::insertCache(const NodeIdent& inNode, uint32_t latencyUS) {
	getMerid()->pingCacheInsert(inNode, latencyUS);
}	

int ProbeQueryPing::init() {
	setStartTime();
	PingPacket pingPacket(getQueryID());	
	RealPacket* inPacket = new RealPacket(getRemoteNode());
	if (pingPacket.createRealPacket(*inPacket) == -1) {
		delete inPacket;			
		return -1;	
	}
	WARN_LOG("ProbeQueryPing: Sending Ping packet\n");	
	getMerid()->addOutPacket(inPacket);
	return 0;
}

int ProbeQueryPing::handleTimeout() {	 
	WARN_LOG("##################### PING QUERY TIMEOUT ###################\n");
	setFinished(true);
	return 0;
}

#ifdef PLANET_LAB_SUPPORT
void ProbeQueryICMP::insertCache(const NodeIdent& inNode, uint32_t latencyUS) {	
	getMerid()->icmpCacheInsert(inNode, latencyUS);
}

int ProbeQueryICMP::init() {
	setStartTime();
	WARN_LOG("ProbeQueryICMP: Sending ICMP ECHO packet\n");
	return getMerid()->sendICMPProbe(getQueryID(), getRemoteNode().addr);		
}

int ProbeQueryICMP::handleTimeout() {	 
	WARN_LOG("##################### ICMP QUERY TIMEOUT ###################\n");
	setFinished(true);
	return 0;
}
#endif

int ProbeQueryGeneric::subscribeLatency(uint64_t in_qid) {
	subscribers.push_back(in_qid);
	return 0;		
}

int ProbeQueryGeneric::handleEvent(
		const NodeIdent& in_remote, const char* inPacket, int packetSize) {
	//	Just push down an empty latency event instead
	vector<NodeIdentLat> tmpVect;
	NodeIdentLat tmpIdent = {in_remote.addr, in_remote.port, 0};
	tmpVect.push_back(tmpIdent);
	return handleLatency(tmpVect);	
}

// 	If we receive an handleLatency, that means the connect completed
//	correctly and we don't need to perform an explict eraseTCPConnection
int ProbeQueryGeneric::handleLatency(
		const vector<NodeIdentLat>& in_remoteNodes) {
	if (in_remoteNodes.size() != 1) {
		return -1;		
	}
	NodeIdent in_remote = {in_remoteNodes[0].addr, in_remoteNodes[0].port};
	//	Note: latency_us is always 0, it's actually measured within the query
	//if (in_remote.addr != remoteNode.addr || 
	//	in_remote.port != remoteNode.port) {
	// Temporary hack, don't check port to support ICMP
	if (in_remote.addr != remoteNode.addr) {
		ERROR_LOG("Received packet from unexpected node\n");
		return -1;
	}
	struct timeval curTime;		
	gettimeofday(&curTime, NULL);
	u_int realLatencyUS = (curTime.tv_sec - startTime.tv_sec) * MICRO_IN_SECOND 
		+ curTime.tv_usec - startTime.tv_usec;		
	NodeIdentLat outNIL = {remoteNode.addr, remoteNode.port, realLatencyUS};
	vector<NodeIdentLat> subVect;
	subVect.push_back(outNIL);
	for (u_int i = 0; i < subscribers.size(); i++) {
		meridProcess->getQueryTable()->notifyQLatency(subscribers[i], subVect);	
	}
	//	Add to cache entry for future use
	insertCache(remoteNode, realLatencyUS);
	finished = true;		
	return 0;
}

HandleClosestGeneric::HandleClosestGeneric(uint64_t id,
							u_short in_betaNumer, u_short in_betaDenom,
							const NodeIdentRendv& in_srcNode, 
							const vector<NodeIdent>& in_remote, 
							MeridianProcess* in_process)
		: 	qid(id), betaNumer(in_betaNumer), betaDenom(in_betaDenom),
			srcNode(in_srcNode), finished(false), meridProcess(in_process) {
	selectedMember.addr = 0;
	selectedMember.port = 0;	
	computeTimeout(MAX_RTT_MS * MICRO_IN_MILLI, &timeoutTV);
	//	Copy all targets over
	for (u_int i = 0; i < in_remote.size(); i++) {
		remoteNodes.insert(in_remote[i]);			
	}
	stateMachine = HC_INIT;
}

int HandleClosestGeneric::init() {
	//gettimeofday(&startTime, NULL);
	set<NodeIdent, ltNodeIdent>::iterator it = remoteNodes.begin();
	for (; it != remoteNodes.end(); it++) {
		uint32_t curLatencyUS;
		if (getLatency(*it, &curLatencyUS) == -1) {
			ProbeQueryGeneric* newQuery =
				createProbeQuery(*it, meridProcess);
			if (meridProcess->getQueryTable()->insertNewQuery(newQuery) == -1) {			
				delete newQuery;
				continue;
			}
			newQuery->subscribeLatency(qid);
			newQuery->init();
		} else {
			remoteLatencies[*it] = curLatencyUS;
		}
	}
	RetInfo curRetInfo(qid, 0, 0);	//	Send back an intermediate info packet
	RealPacket* inPacket = new RealPacket(srcNode);
	if (curRetInfo.createRealPacket(*inPacket) == -1) {
		delete inPacket;			
	} else {
		meridProcess->addOutPacket(inPacket);
	}
	stateMachine = HC_WAIT_FOR_DIRECT_PING;		
	if (remoteLatencies.size() == remoteNodes.size()) {
		//	It's done, tell the query it is
		vector<NodeIdentLat> dummy;
		getMerid()->getQueryTable()->notifyQLatency(getQueryID(), dummy);
	}
	return 0;	
}

int HandleClosestGeneric::handleEvent(
		const NodeIdent& in_remote, 
		const char* inPacket, int packetSize) {
	// Forward certain types of packets backwards,
	// such as RET_RESPONSE, in which case set finished = true				
	if (stateMachine == HC_WAIT_FOR_FIN) {
		BufferWrapper rb(inPacket, packetSize);		
		char queryType;	uint64_t queryID;
		if (Packet::parseHeader(rb, &queryType, &queryID) == -1) {				
			assert(false); // This should not be possible
		}		
		if (queryType == getQueryType()) {			
			//	Query may have gone in a loop, let just say we are the closest
			WARN_LOG("WARNING: Query may have gone in a loop\n");
			RetResponse retPacket(qid, 0, 0, remoteLatencies);	
			//	Note: Don't need to go through rendavous as we
			//	just received packet from in_remote, there should
			//	be a hole in the NAT for the return
			RealPacket* inPacket = new RealPacket(in_remote);
			if (retPacket.createRealPacket(*inPacket) == -1) {
				delete inPacket;			
			} else {
				meridProcess->addOutPacket(inPacket);
			}	
		} else if ((in_remote.addr == selectedMember.addr) &&
				(in_remote.port == selectedMember.port)) {
			WARN_LOG("Received packet from selected ring member\n");			
			if (queryType == RET_RESPONSE) {
				RetResponse* retResp = 
					RetResponse::parse(in_remote, inPacket, packetSize);
				if (retResp == NULL) {
					ERROR_LOG("Malformed packet received\n");
					return -1;
				}	
				RealPacket* inPacket = new RealPacket(srcNode);
				if (retResp->createRealPacket(*inPacket) == -1) {
					delete inPacket;			
				} else {
					meridProcess->addOutPacket(inPacket);
				}
				delete retResp;	// Done with RetResponse
				finished = true;		
			} else if (queryType == RET_ERROR) {
				RetError* retErr = RetError::parse(inPacket, packetSize);
				if (retErr == NULL) {
					ERROR_LOG("Malformed packet received\n");
					return -1;
				}
				RealPacket* inPacket = new RealPacket(srcNode);
				if (retErr->createRealPacket(*inPacket) == -1) {
					delete inPacket;			
				} else {
					meridProcess->addOutPacket(inPacket);
				}
				delete retErr;	// Done with RetResponse
				finished = true;								
			} else if (queryType == RET_INFO) {
				RetInfo* curRetInfo 
					= RetInfo::parse(in_remote, inPacket, packetSize);
				if (curRetInfo == NULL) {
					ERROR_LOG("Malformed packet received\n");
					return -1;					
				}
				RealPacket* inPacket = new RealPacket(srcNode);
				if (curRetInfo->createRealPacket(*inPacket) == -1) {
					delete inPacket;			
				} else {
					meridProcess->addOutPacket(inPacket);
				}
				delete curRetInfo;		
			}
		}		
	}
	return 0;			
}

int HandleClosestGeneric::getMaxAndMinAndAverage(
		const map<NodeIdent, u_int, ltNodeIdent>& inMap,
		u_int* maxValue, u_int* minValue, u_int* avgValue) {			
	if (inMap.size() == 0) {		
		return -1;			
	} else if (inMap.size() == 1) {
		*maxValue = inMap.begin()->second;
		*minValue = inMap.begin()->second;
		*avgValue = inMap.begin()->second;
	} else {
		*maxValue = 0;
		*minValue = UINT_MAX;
		double totalValue = 0.0;
		map<NodeIdent, u_int, ltNodeIdent>::const_iterator mapIt 
			= inMap.begin();
		for (; mapIt != inMap.end(); mapIt++) {
			totalValue += mapIt->second;
			if (mapIt->second > (*maxValue)) {				
				*maxValue = mapIt->second;
			}
			if (mapIt->second < (*minValue)) {
				*minValue = mapIt->second;
			}
		}
		//	The following is just being very careful, probably not ncessary
		long long tmpLL = llround(totalValue / inMap.size());
		if (tmpLL > UINT_MAX) {
			*avgValue = UINT_MAX;	
		} else if (tmpLL <= 0) {
			*avgValue = 0;
		} else {
			*avgValue = (u_int) tmpLL;	
		}
	}
	return 0;
}
		
int HandleClosestGeneric::getMaxAndAverage(
		const map<NodeIdent, u_int, ltNodeIdent>& inMap,
		u_int* maxValue, u_int* avgValue) {			
	if (inMap.size() == 0) {		
		return -1;			
	} else if (inMap.size() == 1) {
		*maxValue = inMap.begin()->second;
		*avgValue = inMap.begin()->second;		
	} else {
		*maxValue = 0;
		double totalValue = 0.0;		
		map<NodeIdent, u_int, ltNodeIdent>::const_iterator mapIt 
			= inMap.begin();
		for (; mapIt != inMap.end(); mapIt++) {
			totalValue += mapIt->second;
			if (mapIt->second > (*maxValue)) {				
				*maxValue = mapIt->second;
			}
		}	
		//	The following is just being very careful, probably not ncessary
		long long tmpLL = llround(totalValue / inMap.size());
		if (tmpLL > UINT_MAX) {
			*avgValue = UINT_MAX;	
		} else if (tmpLL <= 0) {
			*avgValue = 0;
		} else {
			*avgValue = (u_int) tmpLL;	
		}
	}
	return 0;	
}

int HandleClosestGeneric::handleForward() {
	//	We have all the information necessary to make a forwarding decision
	u_int lowestLatUS = UINT_MAX;
	NodeIdent closestMember = {0, 0};
	//	For the lowest latency node by iterating through the ring members
	//	that have returned results back
	map<NodeIdent, map<NodeIdent, u_int, ltNodeIdent>*, ltNodeIdent>::
		iterator it = ringLatencies.begin();	
	for (; it != ringLatencies.end(); it++) {
		u_int dummy, curAvgLatency;
		if (getMaxAndAverage(*(it->second), 
				&dummy, &curAvgLatency) == -1) {
			ERROR_LOG("Incorrect MAX/AVG calculation\n");
			continue;
		}
		if (curAvgLatency < lowestLatUS) {
			lowestLatUS = curAvgLatency;
			closestMember = it->first;
		}
	}	
	double betaRatio = ((double)betaNumer) / ((double)betaDenom);
	if (betaRatio <= 0.0 || betaRatio >= 1.0) {
		ERROR_LOG("Illegal beta parameter\n"); 
		betaRatio = 0.5;
	}
	//	Calculate the forwarding threshold
	u_int latencyThreshold = 0;	
	long long tmpLatThreshold_ll = llround(betaRatio * (double)averageLatUS);
	//	Just to be extra careful with rounding
	if (tmpLatThreshold_ll > UINT_MAX) {
		latencyThreshold = UINT_MAX;
	} else if (tmpLatThreshold_ll < 0) {
		latencyThreshold = 0;	
	} else {
		latencyThreshold = (u_int)tmpLatThreshold_ll;
	}
	WARN_LOG_1("Latency threshold is %d\n", latencyThreshold);
	WARN_LOG_1("Lowest latency is %d\n", lowestLatUS);
	WARN_LOG_1("My latency is %d\n", averageLatUS);	
	if (lowestLatUS == 0 || lowestLatUS > latencyThreshold) {
		//	Did not meet the threshold, return closest so far
		RetResponse* retResp = NULL;
		//	Pick the closest we know right now and return
		if (lowestLatUS < averageLatUS) {
			// Reusing iterator it
			it = ringLatencies.find(closestMember);
			assert(it != ringLatencies.end());
			retResp = new RetResponse(qid, closestMember.addr, 
				closestMember.port, *(it->second));
		} else {
			//	Itself is the closest
			retResp = new RetResponse(qid, 0, 0, remoteLatencies);
		}
		RealPacket* inPacket = new RealPacket(srcNode);
		if (retResp->createRealPacket(*inPacket) == -1) {
			delete inPacket;			
		} else {
			meridProcess->addOutPacket(inPacket);
		}
		delete retResp;	// Done with RetResponse
		finished = true;				
	} else {
#ifdef DEBUG
		u_int netAddr = htonl(closestMember.addr);
		char* remoteString = inet_ntoa(*(struct in_addr*)&(netAddr));			
		WARN_LOG_2("Forwarding to closest member %s:%d\n", 
			remoteString, closestMember.port);
#endif				
		NodeIdent tmpRendvNode = meridProcess->returnRendv();		
		ReqClosestGeneric* reqClosest = createReqClosest(qid, betaNumer, 
			betaDenom, tmpRendvNode.addr, tmpRendvNode.port);
		set<NodeIdent, ltNodeIdent>::iterator it = remoteNodes.begin();
		for (; it != remoteNodes.end(); it++) {
			reqClosest->addTarget(*it);	
		}
		
		NodeIdentRendv tmpRendvOut 
			= {closestMember.addr, closestMember.port, 0, 0};
		set<NodeIdentRendv, ltNodeIdentRendv>::iterator setRendvIt 
			= ringMembers.find(tmpRendvOut);
		if (setRendvIt == ringMembers.end()) {
			ERROR_LOG("Data in HandleClosestGeneric inconsistent\n");
		} else {
			// This gets the rendavous data
			tmpRendvOut = *setRendvIt;
		}		
		RealPacket* inPacket = new RealPacket(tmpRendvOut);
		if (reqClosest->createRealPacket(*inPacket) == -1) {
			delete inPacket;
			finished = true;		
		} else {
			selectedMember = closestMember;
			meridProcess->addOutPacket(inPacket);
			computeTimeout(MAX_RTT_MS * MICRO_IN_MILLI, &timeoutTV);
			stateMachine = HC_WAIT_FOR_FIN;
		}
		delete reqClosest;
	}
	return 0;
}

int HandleClosestGeneric::sendReqProbes() {
	if (stateMachine != HC_WAIT_FOR_DIRECT_PING || 
			remoteLatencies.size() != remoteNodes.size()) {
		return -1;	// More results needed
	}	
	//	Find the longest and average time in the remoteLatenties map
	u_int largestLatUS = 0;
	u_int smallestLatUS = UINT_MAX;
	//if (getMaxAndAverage(remoteLatencies, 
	//		&largestLatUS, &averageLatUS) == -1) {
	if (getMaxAndMinAndAverage(remoteLatencies, 
			&largestLatUS, &smallestLatUS, &averageLatUS) == -1) {
		ERROR_LOG("Incorrect MAX/MIN/AVG calculation\n");
		finished = true;
		return -1;
	}
	WARN_LOG_1("Largest latency is %d\n", largestLatUS);
	double betaRatio = ((double)betaNumer) / ((double)betaDenom);
	WARN_LOG_1("Beta ratio is %0.2f\n", betaRatio);
	if (betaRatio <= 0.0 || betaRatio >= 1.0) {
		ERROR_LOG("Illegal beta parameter\n"); 
		betaRatio = 0.5;
	}
	//	Update timeout
	u_int newTimeoutPeriod = (u_int) ceil(
		((2.0 * betaRatio) + 1.0) * (double)largestLatUS);		
	computeTimeout(newTimeoutPeriod, &timeoutTV);
	//	Change to next state			
	stateMachine = HC_INDIRECT_PING;			
//			if ((meridProcess->getRings()->fillVector(averageLatUS, betaRatio, 
//					ringMembers) == -1) || (ringMembers.size() == 0)) {
	if ((averageLatUS == 0) ||
		//(meridProcess->getRings()->fillVector(averageLatUS, 
		//	averageLatUS, betaRatio, ringMembers) == -1) || 
		(meridProcess->getRings()->fillVector(smallestLatUS, 
			largestLatUS, betaRatio, ringMembers) == -1) || 
		(ringMembers.size() == 0)) {									
		// 0, 0 means itself						
		RetResponse retPacket(qid, 0, 0, remoteLatencies);	
		RealPacket* inPacket = new RealPacket(srcNode);
		if (retPacket.createRealPacket(*inPacket) == -1) {
			delete inPacket;			
		} else {
			meridProcess->addOutPacket(inPacket);
		}
		finished = true;
		return 0;
	}
	//	Send a ReqTCPProbeAverage to each of the ring members
	set<NodeIdentRendv, ltNodeIdentRendv>::iterator setIt = ringMembers.begin();
	for (; setIt != ringMembers.end(); setIt++) {
/*		
		NodeIdent rendvIdent;
		NodeIdentRendv outIdent = {setIt->addr, setIt->port, 0, 0};		
		if (meridProcess->getRings()->rendvLookup(*setIt, rendvIdent) != -1) {
			outIdent.portRendv = rendvIdent.port;
			outIdent.addrRendv = rendvIdent.addr;
		}		
		ReqProbeGeneric* newQuery =
			createReqProbe(outIdent, remoteNodes, meridProcess);
*/			
		ReqProbeGeneric* newQuery =
			createReqProbe(*setIt, remoteNodes, meridProcess);
		if (meridProcess->getQueryTable()->insertNewQuery(
				newQuery) == -1) {			
			delete newQuery;
		} else {
			newQuery->subscribeLatency(qid);
			newQuery->init();
		}
	}
	return 0;
}


int HandleClosestGeneric::handleLatency(
		const vector<NodeIdentLat>& in_remoteNodes) {
	//	Determine action depending on state
	switch (stateMachine) {
	case HC_INIT: {
			return 0;	// Unexpected, just return 0
		} break;
	case HC_WAIT_FOR_DIRECT_PING: {
			if (remoteLatencies.size() == remoteNodes.size()) {				
				return sendReqProbes();	
			}
			//	Must have at least one entry in vector unless all entries
			//	are already accounted for		
			if (in_remoteNodes.size() != 1) {
				return -1;		
			}
			NodeIdent in_remote 
				= {in_remoteNodes[0].addr, in_remoteNodes[0].port};
			u_int latency_us = in_remoteNodes[0].latencyUS;		
			if (remoteNodes.find(in_remote) == remoteNodes.end()) {
				return -1;
			}
			if (remoteLatencies.find(in_remote) != remoteLatencies.end()) {
				ERROR_LOG("Received duplicate results\n");
				return -1;
			}
			remoteLatencies[in_remote] = latency_us;
			if (remoteLatencies.size() != remoteNodes.size()) {
				return 0;	// More results needed
			}
			//	All results in, can send indirect probes
			return sendReqProbes();
		} break;
	case HC_INDIRECT_PING: {
			if (in_remoteNodes.size() < 1) {
				ERROR_LOG("There must be at least one entry\n");
				return -1;
			}
			//	HACK: The first entry tell me where the source is
			NodeIdent curNode
				= {in_remoteNodes[0].addr, in_remoteNodes[0].port};
			//	The comparison function doesn't care about the rendv
			//	part of the NodeIdentRendv structure
			NodeIdentRendv curNodeRendv 
				= {in_remoteNodes[0].addr, in_remoteNodes[0].port, 0, 0};
			//	Has to be a ring member
			if (ringMembers.find(curNodeRendv) == ringMembers.end()) {
				ERROR_LOG("Results from an unexpected node\n");
				return -1;													
			}
			//	This ring member must not already have a latency map
			if (ringLatencies.find(curNode) != ringLatencies.end()) {
				ERROR_LOG("Duplicate response from node\n");
				return -1;
			}			
			map<NodeIdent, u_int, ltNodeIdent>* tmpIdentMap
				= new map<NodeIdent, u_int, ltNodeIdent>();			
			for (u_int i = 1; i < in_remoteNodes.size(); i++) {
				NodeIdent tmpIdent 
					= {in_remoteNodes[i].addr, in_remoteNodes[i].port};
				//	Each result must be part of remoteNodes
				if (remoteNodes.find(tmpIdent) == remoteNodes.end()) {
					ERROR_LOG("Results consist of unexpected nodes\n");
					delete tmpIdentMap;
					return -1;					
				}
				(*tmpIdentMap)[tmpIdent] = in_remoteNodes[i].latencyUS;
			}
			// 	The result size must equal the number of remote nodes			
			if (remoteNodes.size() != tmpIdentMap->size()) {		
				ERROR_LOG("Incorrect number of nodes responded\n");
				delete tmpIdentMap;
				return -1;	
			}
			ringLatencies[curNode] = tmpIdentMap;
			//	If some ring members haven't answered yet, wait for more 
			if (ringMembers.size() != ringLatencies.size()) {
				return 0;	// More responses expected
			}
			return handleForward();
		} break;
	case HC_WAIT_FOR_FIN: {
			return 0;	// Unexpected
		} break;
	default: {
			assert(false);
		} break;					
	}
	return 0;	
}

int HandleClosestGeneric::handleTimeout() {
	switch (stateMachine) {
	case HC_INIT: {
			//	Init wasn't called after being pushed into query table
			finished = true;			
		} break;
	case HC_WAIT_FOR_DIRECT_PING: {
			//	Can't ping every body, return a RET_ERROR			
			RetError retPacket(qid);
			RealPacket* inPacket = new RealPacket(srcNode);
			if (retPacket.createRealPacket(*inPacket) == -1) {
				delete inPacket;			
			} else {
				meridProcess->addOutPacket(inPacket);
			}
			finished = true;			
		} break;
	case HC_INDIRECT_PING: {
			return handleForward();
		} break;
	case HC_WAIT_FOR_FIN: {
			//	Done, nothing to forward back, something screwed up. Send
			//	back an error packet
			RetError retPacket(qid);
			RealPacket* inPacket = new RealPacket(srcNode);
			if (retPacket.createRealPacket(*inPacket) == -1) {
				delete inPacket;			
			} else {
				meridProcess->addOutPacket(inPacket);
			}			
			finished = true;			
		} break;		
	default: {
			assert(false);
		} break;					
	}
	return 0;
}

HandleClosestGeneric::~HandleClosestGeneric() {
	map<NodeIdent, map<NodeIdent, u_int, ltNodeIdent>*, ltNodeIdent>::iterator
		it = ringLatencies.begin();
	for (; it != ringLatencies.end(); it++) {
		if (it->second != NULL) {
			delete it->second;
		}
	}
}

int HandleClosestTCP::getLatency(const NodeIdent& inNode, uint32_t* latencyUS) {
	return getMerid()->tcpCacheGetLatency(inNode, latencyUS);
}

int HandleClosestDNS::getLatency(const NodeIdent& inNode, uint32_t* latencyUS) {
	return getMerid()->dnsCacheGetLatency(inNode, latencyUS);
}

int HandleClosestPing::getLatency(const NodeIdent& inNode, uint32_t* latencyUS){
	return getMerid()->pingCacheGetLatency(inNode, latencyUS);
}

#ifdef PLANET_LAB_SUPPORT
int HandleClosestICMP::getLatency(const NodeIdent& inNode, uint32_t* latencyUS){
	return getMerid()->icmpCacheGetLatency(inNode, latencyUS);
}
#endif


int ReqProbeTCP::init() {
	//gettimeofday(&startTime, NULL);
	NodeIdent tmpRendvNode = getMerid()->returnRendv();
	ReqMeasureTCP tcpPacket(getQueryID(), tmpRendvNode.addr, tmpRendvNode.port);
	set<NodeIdent, ltNodeIdent>* curRemoteNodes = getRemoteNodes();
	set<NodeIdent, ltNodeIdent>::iterator it = curRemoteNodes->begin();
	for (; it != curRemoteNodes->end(); it++) {
		tcpPacket.addTarget(*it);
	}
	RealPacket* inPacket = new RealPacket(getSrcNode());
	if (tcpPacket.createRealPacket(*inPacket) == -1) {
		delete inPacket;			
		return -1;
	}
	getMerid()->addOutPacket(inPacket);
	return 0;
}


int ReqProbeDNS::init() {
	//gettimeofday(&startTime, NULL);
	NodeIdent tmpRendvNode = getMerid()->returnRendv();
	ReqMeasureDNS dnsPacket(getQueryID(), tmpRendvNode.addr, tmpRendvNode.port);
	set<NodeIdent, ltNodeIdent>* curRemoteNodes = getRemoteNodes();
	set<NodeIdent, ltNodeIdent>::iterator it = curRemoteNodes->begin();
	for (; it != curRemoteNodes->end(); it++) {
		dnsPacket.addTarget(*it);
	}
	RealPacket* inPacket = new RealPacket(getSrcNode());
	if (dnsPacket.createRealPacket(*inPacket) == -1) {
		delete inPacket;			
		return -1;
	}
	getMerid()->addOutPacket(inPacket);
	return 0;
}


int ReqProbePing::init() {
	//gettimeofday(&startTime, NULL);
	NodeIdent tmpRendvNode = getMerid()->returnRendv();
	ReqMeasurePing pingPacket(
		getQueryID(), tmpRendvNode.addr, tmpRendvNode.port);
	set<NodeIdent, ltNodeIdent>* curRemoteNodes = getRemoteNodes();
	set<NodeIdent, ltNodeIdent>::iterator it = curRemoteNodes->begin();
	for (; it != curRemoteNodes->end(); it++) {
		pingPacket.addTarget(*it);
	}
	RealPacket* inPacket = new RealPacket(getSrcNode());
	if (pingPacket.createRealPacket(*inPacket) == -1) {
		delete inPacket;			
		return -1;
	}
	getMerid()->addOutPacket(inPacket);
	return 0;
}

#ifdef PLANET_LAB_SUPPORT
int ReqProbeICMP::init() {
	//gettimeofday(&startTime, NULL);
	NodeIdent tmpRendvNode = getMerid()->returnRendv();
	ReqMeasureICMP pingPacket(
		getQueryID(), tmpRendvNode.addr, tmpRendvNode.port);
	set<NodeIdent, ltNodeIdent>* curRemoteNodes = getRemoteNodes();
	set<NodeIdent, ltNodeIdent>::iterator it = curRemoteNodes->begin();
	for (; it != curRemoteNodes->end(); it++) {
		pingPacket.addTarget(*it);
	}
	RealPacket* inPacket = new RealPacket(getSrcNode());
	if (pingPacket.createRealPacket(*inPacket) == -1) {
		delete inPacket;			
		return -1;
	}
	getMerid()->addOutPacket(inPacket);
	return 0;
}
#endif


ReqProbeGeneric::ReqProbeGeneric(const NodeIdentRendv& in_src_node,
								const set<NodeIdent, ltNodeIdent>& in_remote, 
								MeridianProcess* in_process)
		: 	srcNode(in_src_node), finished(false), meridProcess(in_process) {
	qid = meridProcess->getNewQueryID();
	computeTimeout(2 * MAX_RTT_MS * MICRO_IN_MILLI, &timeoutTV);
	//	Copy all targets over
	remoteNodes.insert(in_remote.begin(), in_remote.end());
}

ReqProbeGeneric::ReqProbeGeneric(const NodeIdentRendv& in_src_node,
						const set<NodeIdentConst, ltNodeIdentConst>& in_remote, 
						MeridianProcess* in_process)
		: 	srcNode(in_src_node), finished(false), meridProcess(in_process) {
	qid = meridProcess->getNewQueryID();
	computeTimeout(2 * MAX_RTT_MS * MICRO_IN_MILLI, &timeoutTV);
	//	Copy all targets over
	set<NodeIdentConst, ltNodeIdentConst>::const_iterator it 
		= in_remote.begin();
	for (; it != in_remote.end(); it++) {
		NodeIdent tmp = {it->addr, it->port};
		remoteNodes.insert(tmp);		
	}
}


int ReqProbeGeneric::handleEvent(
		const NodeIdent& in_remote, const char* inPacket, int packetSize) {		
	if (inPacket[0] != RET_PING_REQ) {
		ERROR_LOG("Expecting RET_PING_REQ packet, received something else\n");
		return -1;	// Not pong packet
	}	
	if (in_remote.addr != srcNode.addr || 
		in_remote.port != srcNode.port) {
		ERROR_LOG("Received packet from unexpected node\n");
		return -1;
	}
	RetPing* newRetPing = RetPing::parse(inPacket, packetSize);
	if (newRetPing == NULL) {
		ERROR_LOG("Incorrect packet received\n");
		return -1;
	}	
	const vector<NodeIdentLat>* tmpVectLat = newRetPing->returnNodes();
	//	Not all the nodes are there
	if (tmpVectLat->size() != remoteNodes.size()) {
		ERROR_LOG("Only partial list of nodes returned\n");
		delete newRetPing;
		return -1;
	}
	vector<NodeIdentLat> newTmpVect;
	//	HACK: Add srcNode to the vector before telling subscriber
	NodeIdentLat outNIL = {srcNode.addr, srcNode.port, 0};
	newTmpVect.push_back(outNIL);
	for (u_int i = 0; i < tmpVectLat->size(); i++) {
		newTmpVect.push_back((*tmpVectLat)[i]);	
	}
	//	Tell subscribers
	for (u_int i = 0; i < subscribers.size(); i++) {		
		meridProcess->getQueryTable()->notifyQLatency(
			subscribers[i], newTmpVect);	
	}
	delete newRetPing;	// 	Done with packet
	finished = true;	//	Done with query
	return 0;
}

int ReqProbeGeneric::handleTimeout() {
	NodeIdent tmpIdent = {srcNode.addr, srcNode.port};
	meridProcess->getRings()->eraseNode(tmpIdent);
	finished = true;
	return 0;		
}

int ReqProbeGeneric::subscribeLatency(uint64_t in_qid) {
	subscribers.push_back(in_qid);
	return 0;
}


HandleMCGeneric::HandleMCGeneric(uint64_t id,
							u_short in_betaNumer, u_short in_betaDenom,
							const NodeIdentRendv& in_srcNode, 
							const vector<NodeIdentConst>& in_remote, 
							MeridianProcess* in_process)
		: 	qid(id), betaNumer(in_betaNumer), betaDenom(in_betaDenom),
			srcNode(in_srcNode), finished(false), meridProcess(in_process) {
	selectedMember.addr = 0;
	selectedMember.port = 0;				
	computeTimeout(MAX_RTT_MS * MICRO_IN_MILLI, &timeoutTV);
	//	Copy all targets over
	for (u_int i = 0; i < in_remote.size(); i++) {
		remoteNodes.insert(in_remote[i]);			
	}
	stateMachine = HMC_INIT;
}

int HandleMCGeneric::init() {
	//gettimeofday(&startTime, NULL);
	set<NodeIdentConst, ltNodeIdentConst>::iterator it = remoteNodes.begin();
	for (; it != remoteNodes.end(); it++) {
		NodeIdent tmp = {it->addr, it->port};
		uint32_t curLatencyUS;
		if (getLatency(tmp, &curLatencyUS) == -1) {		
			ProbeQueryGeneric* newQuery =
				createProbeQuery(tmp, meridProcess);
			if (meridProcess->getQueryTable()->insertNewQuery(newQuery) == -1) {			
				delete newQuery;
				continue;
			}
			newQuery->subscribeLatency(qid);
			newQuery->init();
		} else {
			remoteLatencies[tmp] = curLatencyUS;		
		}
	}
	RetInfo curRetInfo(qid, 0, 0);	//	Send back an intermediate info packet
	RealPacket* inPacket = new RealPacket(srcNode);
	if (curRetInfo.createRealPacket(*inPacket) == -1) {
		delete inPacket;			
	} else {
		meridProcess->addOutPacket(inPacket);
	}
	stateMachine = HMC_WAIT_FOR_DIRECT_PING;
	if (remoteLatencies.size() == remoteNodes.size()) {
		//	It's done, tell the query it is
		vector<NodeIdentLat> dummy;
		getMerid()->getQueryTable()->notifyQLatency(getQueryID(), dummy);
	}	
	return 0;	
}

int HandleMCGeneric::handleEvent(
		const NodeIdent& in_remote, 
		const char* inPacket, int packetSize) {
	// Forward certain types of packets backwards,
	// such as RET_RESPONSE, in which case set finished = true				
	if (stateMachine == HMC_WAIT_FOR_FIN) {
		BufferWrapper rb(inPacket, packetSize);		
		char queryType;	uint64_t queryID;
		if (Packet::parseHeader(rb, &queryType, &queryID) == -1) {				
			assert(false); // This should not be possible
		}		
		if (queryType == getQueryType()) {			
			//	Query may have gone in a loop, let just say we are the closest
			WARN_LOG("WARNING: Query may have gone in a loop\n");
			RetResponse retPacket(qid, 0, 0, remoteLatencies);
			//	NOTE: Don't need to use rendavous node as there should be a 
			//	hole in the NAT to in_remote as we just received the packet
			RealPacket* inPacket = new RealPacket(in_remote);
			if (retPacket.createRealPacket(*inPacket) == -1) {
				delete inPacket;			
			} else {
				meridProcess->addOutPacket(inPacket);
			}	
		} else if ((in_remote.addr == selectedMember.addr) &&
				(in_remote.port == selectedMember.port)) {
			WARN_LOG("Received packet from selected ring member\n");			
			if (queryType == RET_RESPONSE) {
				RetResponse* retResp = 
					RetResponse::parse(in_remote, inPacket, packetSize);
				if (retResp == NULL) {
					ERROR_LOG("Malformed packet received\n");
					return -1;
				}	
				RealPacket* inPacket = new RealPacket(srcNode);
				if (retResp->createRealPacket(*inPacket) == -1) {
					delete inPacket;			
				} else {
					meridProcess->addOutPacket(inPacket);
				}
				delete retResp;	// Done with RetResponse
				finished = true;		
			} else if (queryType == RET_ERROR) {
				RetError* retErr = RetError::parse(inPacket, packetSize);
				if (retErr == NULL) {
					ERROR_LOG("Malformed packet received\n");
					return -1;
				}
				RealPacket* inPacket = new RealPacket(srcNode);
				if (retErr->createRealPacket(*inPacket) == -1) {
					delete inPacket;			
				} else {
					meridProcess->addOutPacket(inPacket);
				}
				delete retErr;	// Done with RetResponse
				finished = true;								
			} else if (queryType == RET_INFO) {
				RetInfo* curRetInfo 
					= RetInfo::parse(in_remote, inPacket, packetSize);
				if (curRetInfo == NULL) {
					ERROR_LOG("Malformed packet received\n");
					return -1;					
				}
				RealPacket* inPacket = new RealPacket(srcNode);
				if (curRetInfo->createRealPacket(*inPacket) == -1) {
					delete inPacket;			
				} else {
					meridProcess->addOutPacket(inPacket);
				}
				delete curRetInfo;		
			}
		}		
	}
	return 0;			
}

int HandleMCGeneric::handleForward() {
	//	We have all the information necessary to make a forwarding decision
	u_int lowestLatUS = UINT_MAX;
	NodeIdent closestMember = {0, 0};
	//	For the lowest latency node by iterating through the ring members
	//	that have returned results back
	map<NodeIdent, map<NodeIdent, u_int, ltNodeIdent>*, ltNodeIdent>::
		iterator it = ringLatencies.begin();	
	for (; it != ringLatencies.end(); it++) {
		u_int curAvgLatency;
		if (getAvgSolution(*(it->second), &curAvgLatency) == -1) {
			ERROR_LOG("Incorrect average solution calculation\n");
			continue;			
		}
		if (curAvgLatency < lowestLatUS) {
			lowestLatUS = curAvgLatency;
			closestMember = it->first;
		}
	}	
	double betaRatio = ((double)betaNumer) / ((double)betaDenom);
	if (betaRatio <= 0.0 || betaRatio >= 1.0) {
		ERROR_LOG("Illegal beta parameter\n"); 
		betaRatio = 0.5;
	}
	//	Calculate the forwarding threshold
	u_int latencyThreshold = 0;	
	long long tmpLatThreshold_ll = llround(betaRatio * (double)averageLatUS);
	//	Just to be extra careful with rounding
	if (tmpLatThreshold_ll > UINT_MAX) {
		latencyThreshold = UINT_MAX;
	} else if (tmpLatThreshold_ll < 0) {
		latencyThreshold = 0;	
	} else {
		latencyThreshold = (u_int)tmpLatThreshold_ll;
	}
	WARN_LOG_1("Latency threshold is %d\n", latencyThreshold);
	WARN_LOG_1("Lowest latency is %d\n", lowestLatUS);
	WARN_LOG_1("My latency is %d\n", averageLatUS);
	if (lowestLatUS == 0 || lowestLatUS > latencyThreshold) {
		//	Did not meet the threshold, return closest so far
		RetResponse* retResp = NULL;
		//	Pick the closest we know right now and return
		if (lowestLatUS < averageLatUS) {
			// Reusing iterator it
			it = ringLatencies.find(closestMember);
			assert(it != ringLatencies.end());
			retResp = new RetResponse(qid, closestMember.addr, 
				closestMember.port, *(it->second));
		} else {
			//	Itself is the closest
			retResp = new RetResponse(qid, 0, 0, remoteLatencies);
		}
		RealPacket* inPacket = new RealPacket(srcNode);
		if (retResp->createRealPacket(*inPacket) == -1) {
			delete inPacket;			
		} else {
			meridProcess->addOutPacket(inPacket);
		}
		delete retResp;	// Done with RetResponse
		finished = true;				
	} else {
#ifdef DEBUG		
		u_int netAddr = htonl(closestMember.addr);
		char* remoteString = inet_ntoa(*(struct in_addr*)&(netAddr));			
		WARN_LOG_2("Forwarding to closest member %s:%d\n", 
			remoteString, closestMember.port);
#endif				
		NodeIdent tmpRendvNode = meridProcess->returnRendv();		
		ReqConstraintGeneric* reqMC = createReqConstraint(qid, betaNumer, 
			betaDenom, tmpRendvNode.addr, tmpRendvNode.port);
		set<NodeIdentConst, ltNodeIdentConst>::iterator it 
			= remoteNodes.begin();
		for (; it != remoteNodes.end(); it++) {
			reqMC->addTarget(*it);	
		}
		
		NodeIdentRendv tmpRendvOut 
			= {closestMember.addr, closestMember.port, 0, 0};
		set<NodeIdentRendv, ltNodeIdentRendv>::iterator setRendvIt 
			= ringMembers.find(tmpRendvOut);
		if (setRendvIt == ringMembers.end()) {
			ERROR_LOG("Data in HandleClosestGeneric inconsistent\n");
		} else {
			// This gets the rendavous data
			tmpRendvOut = *setRendvIt;
		}				
		RealPacket* inPacket = new RealPacket(tmpRendvOut);					
		//RealPacket* inPacket = new RealPacket(closestMember);
		if (reqMC->createRealPacket(*inPacket) == -1) {
			delete inPacket;
			finished = true;		
		} else {
			selectedMember = closestMember;
			meridProcess->addOutPacket(inPacket);
			computeTimeout(MAX_RTT_MS * MICRO_IN_MILLI, &timeoutTV);
			stateMachine = HMC_WAIT_FOR_FIN;
		}
		delete reqMC;
	}
	return 0;
}

int HandleMCGeneric::sendReqProbes() {
	if (stateMachine != HMC_WAIT_FOR_DIRECT_PING || 
			remoteLatencies.size() != remoteNodes.size()) {
		return -1;	// More results needed
	}		
	//	Find the longest and average time in the remoteLatenties map
	u_int largestAddUS;
	if (getMaxSolution(remoteLatencies, &largestAddUS, false) == -1) {
		ERROR_LOG("Incorrect MAX calculation\n");
		finished = true;
		return -1;				
	}	
	u_int largestSubUS;
	if (getMaxSolution(remoteLatencies, &largestSubUS, true) == -1) {
		ERROR_LOG("Incorrect MAX calculation\n");
		finished = true;
		return -1;				
	}			
	if (getAvgSolution(remoteLatencies, &averageLatUS) == -1) {
		ERROR_LOG("Incorrect AVG calculation\n");
		finished = true;
		return -1;
	}			
	WARN_LOG_1("Largest latency is %d\n", largestAddUS);
	double betaRatio = ((double)betaNumer) / ((double)betaDenom);
	WARN_LOG_1("Beta ratio is %0.2f\n", betaRatio);
	if (betaRatio <= 0.0 || betaRatio >= 1.0) {
		ERROR_LOG("Illegal beta parameter\n"); 
		betaRatio = 0.5;
	}
	//	Update timeout
	u_int newTimeoutPeriod = (u_int) ceil(
		((2.0 * betaRatio) + 1.0) * (double)largestAddUS);		
	computeTimeout(newTimeoutPeriod, &timeoutTV);
	//	Change to next state			
	stateMachine = HMC_INDIRECT_PING;
	// Have to worry about 0 latencies for multiconstraint			
	if ((averageLatUS == 0) || 
		(meridProcess->getRings()->fillVector(largestSubUS, 
			largestAddUS, betaRatio, ringMembers) == -1) || 
		(ringMembers.size() == 0)) {				
		// 0, 0 means itself						
		RetResponse retPacket(qid, 0, 0, remoteLatencies);	
		RealPacket* inPacket = new RealPacket(srcNode);
		if (retPacket.createRealPacket(*inPacket) == -1) {
			delete inPacket;			
		} else {
			meridProcess->addOutPacket(inPacket);
		}
		finished = true;
		return 0;
	}						
	//	Send a ReqTCPProbeAverage to each of the ring members
	set<NodeIdentRendv, ltNodeIdentRendv>::iterator setIt
		= ringMembers.begin();
	for (; setIt != ringMembers.end(); setIt++) {
/*		
		NodeIdent rendvIdent;
		NodeIdentRendv outIdent = {setIt->addr, setIt->port, 0, 0};
		if (meridProcess->getRings()->rendvLookup(*setIt, rendvIdent) != -1) {
			outIdent.portRendv = rendvIdent.port;
			outIdent.addrRendv = rendvIdent.addr;
		}		
		ReqProbeGeneric* newQuery =
			createReqProbe(outIdent, remoteNodes, meridProcess);
*/
		ReqProbeGeneric* newQuery =
			createReqProbe(*setIt, remoteNodes, meridProcess);
		if (meridProcess->getQueryTable()->insertNewQuery(
				newQuery) == -1) {			
			delete newQuery;
		} else {
			newQuery->subscribeLatency(qid);
			newQuery->init();
		}
	}
	return 0;
}

int HandleMCGeneric::handleLatency(
		const vector<NodeIdentLat>& in_remoteNodes) {
	//	Determine action depending on state
	switch (stateMachine) {
	case HMC_INIT: {
			return 0;	// Unexpected, just return 0
		} break;
	case HMC_WAIT_FOR_DIRECT_PING: {
			if (remoteLatencies.size() == remoteNodes.size()) {				
				return sendReqProbes();	
			}
			//	Must have at least one entry in vector unless all entries
			//	are already accounted for					
			if (in_remoteNodes.size() != 1) {
				return -1;		
			}
			//	NOTE: NodeIdentConst is keyed only on the addr and port
			//	so this will still match
			NodeIdentConst in_remote_const
				= {in_remoteNodes[0].addr, in_remoteNodes[0].port, 0};
			NodeIdent in_remote
				= {in_remote_const.addr, in_remote_const.port};				
			u_int latency_us = in_remoteNodes[0].latencyUS;		
			if (remoteNodes.find(in_remote_const) == remoteNodes.end()) {
				return -1;
			}
			if (remoteLatencies.find(in_remote) != remoteLatencies.end()) {
				ERROR_LOG("Received duplicate results\n");
				return -1;
			}
			remoteLatencies[in_remote] = latency_us;
			if (remoteLatencies.size() != remoteNodes.size()) {
				return 0;	// More results needed
			}			
			return sendReqProbes();			
		} break;
	case HMC_INDIRECT_PING: {
			if (in_remoteNodes.size() < 1) {
				ERROR_LOG("There must be at least one entry\n");
				return -1;
			}
			//	HACK: The first entry tell me where the source is
			NodeIdent curNode
				= {in_remoteNodes[0].addr, in_remoteNodes[0].port};
			NodeIdentRendv curNodeRendv
				= {in_remoteNodes[0].addr, in_remoteNodes[0].port, 0, 0};				
			//	Has to be a ring member
			if (ringMembers.find(curNodeRendv) == ringMembers.end()) {
				ERROR_LOG("Results from an unexpected node\n");
				return -1;													
			}
			//	This ring member must not already have a latency map
			if (ringLatencies.find(curNode) != ringLatencies.end()) {
				ERROR_LOG("Duplicate response from node\n");
				return -1;
			}			
			map<NodeIdent, u_int, ltNodeIdent>* tmpIdentMap
				= new map<NodeIdent, u_int, ltNodeIdent>();			
			for (u_int i = 1; i < in_remoteNodes.size(); i++) {
				NodeIdentConst tmpIdentConst 
					= {in_remoteNodes[i].addr, in_remoteNodes[i].port, 0};
				NodeIdent tmpIdent 
					= {tmpIdentConst.addr, tmpIdentConst.port};					
				//	Each result must be part of remoteNodes
				if (remoteNodes.find(tmpIdentConst) == remoteNodes.end()) {
					ERROR_LOG("Results consist of unexpected nodes\n");
					delete tmpIdentMap;
					return -1;					
				}
				(*tmpIdentMap)[tmpIdent] = in_remoteNodes[i].latencyUS;
			}
			// 	The result size must equal the number of remote nodes			
			if (remoteNodes.size() != tmpIdentMap->size()) {		
				ERROR_LOG("Incorrect number of nodes responded\n");
				delete tmpIdentMap;
				return -1;	
			}
			ringLatencies[curNode] = tmpIdentMap;
			//	If some ring members haven't answered yet, wait for more 
			if (ringMembers.size() != ringLatencies.size()) {
				return 0;	// More responses expected
			}
			return handleForward();
		} break;
	case HMC_WAIT_FOR_FIN: {
			return 0;	// Unexpected
		} break;
	default: {
			assert(false);
		} break;					
	}
	return 0;	
}

int HandleMCGeneric::handleTimeout() {
	switch (stateMachine) {
	case HMC_INIT: {
			//	Init wasn't called after being pushed into query table
			finished = true;			
		} break;
	case HMC_WAIT_FOR_DIRECT_PING: {
			//	Can't ping every body, return a RET_ERROR			
			RetError retPacket(qid);
			RealPacket* inPacket = new RealPacket(srcNode);
			if (retPacket.createRealPacket(*inPacket) == -1) {
				delete inPacket;			
			} else {
				meridProcess->addOutPacket(inPacket);
			}
			finished = true;			
		} break;
	case HMC_INDIRECT_PING: {
			return handleForward();
		} break;
	case HMC_WAIT_FOR_FIN: {
			//	Done, nothing to forward back, something screwed up. Send
			//	back an error packet
			RetError retPacket(qid);
			RealPacket* inPacket = new RealPacket(srcNode);
			if (retPacket.createRealPacket(*inPacket) == -1) {
				delete inPacket;			
			} else {
				meridProcess->addOutPacket(inPacket);
			}			
			finished = true;			
		} break;		
	default: {
			assert(false);
		} break;					
	}
	return 0;
}

HandleMCGeneric::~HandleMCGeneric() {
	map<NodeIdent, map<NodeIdent, u_int, ltNodeIdent>*, ltNodeIdent>::iterator
		it = ringLatencies.begin();
	for (; it != ringLatencies.end(); it++) {
		if (it->second != NULL) {
			delete it->second;
		}
	}
}


int HandleMCTCP::getLatency(const NodeIdent& inNode, uint32_t* latencyUS) {
	return getMerid()->tcpCacheGetLatency(inNode, latencyUS);
}

int HandleMCDNS::getLatency(const NodeIdent& inNode, uint32_t* latencyUS) {
	return getMerid()->dnsCacheGetLatency(inNode, latencyUS);
}

int HandleMCPing::getLatency(const NodeIdent& inNode, uint32_t* latencyUS) {
	return getMerid()->pingCacheGetLatency(inNode, latencyUS);
}

#ifdef PLANET_LAB_SUPPORT
int HandleMCICMP::getLatency(const NodeIdent& inNode, uint32_t* latencyUS) {
	return getMerid()->icmpCacheGetLatency(inNode, latencyUS);
}
#endif

#ifdef MERIDIAN_DSL
DSLReqQuery::DSLReqQuery(MeridianProcess* in_process, 
		const struct timeval& in_timeout, uint16_t in_ttl, 
		const NodeIdentRendv& in_dest, 
		uint64_t redirectQID)
	: 	finished(false), timeoutTV(in_timeout), meridProcess(in_process),
		recvQueryID(redirectQID), destNode(in_dest), ttl(in_ttl) {
	qid = meridProcess->getNewQueryID();		
}

int DSLReqQuery::handleEvent(
		const NodeIdent& in_remote, const char* inPacket, int packetSize) {	
	return meridProcess->getQueryTable()->notifyQPacket(recvQueryID, 
		in_remote, inPacket, packetSize);	
}

int DSLReqQuery::init(
		ParserState* ps, const string* func_name, const ASTNode* param) { 
	NodeIdent rendvInfo = getMerid()->returnRendv();
	//	Get remaining timeout period
	struct timeval cur_time;
	gettimeofday(&cur_time, NULL);
	QueryTable::normalizeTime(cur_time);	
	double timeout_ms = 
		((timeoutTV.tv_sec - cur_time.tv_sec) * 1000.0) +
			((timeoutTV.tv_usec - cur_time.tv_usec) / 1000.0);			
#define MIN_TIMEOUT_MS	10
	if ((uint32_t)timeout_ms >= (1 << 16) || 
			timeout_ms < MIN_TIMEOUT_MS) {
		// Timeout too long or too short
		return -1;								
	}
	if (ttl == 0) {
		return -1;	// Expired, don't bother sending
	}	
	DSLRequestPacket tmpDSLPacket(getQueryID(), 
		(uint16_t)timeout_ms, ttl - 1, rendvInfo.addr, rendvInfo.port);
	RealPacket* tmpPacket = new RealPacket(destNode);
	if (tmpPacket == NULL) {
		return -1;	
	}
	if (tmpDSLPacket.createRealPacket(*tmpPacket) == -1) {
		delete tmpPacket;			
		return -1;
	}
	if (marshal_packet(ps, *tmpPacket, func_name, param) == -1) {
		delete tmpPacket;
		return -1;
	}
	getMerid()->addOutPacket(tmpPacket);
	return 0;
}

#define INSTRUCTIONS_PER_IT	 10000

DSLRecvQuery::DSLRecvQuery(
		ParserState* in_state, MeridianProcess* in_process,
			const NodeIdentRendv& in_src, uint64_t in_ret_qid,
			uint16_t timeout_ms, uint16_t in_ttl) 
		: 	ret_qid(in_ret_qid), finished(false), meridProcess(in_process), 
			ps(in_state), srcNode(in_src), ttl(in_ttl) {
	qid = meridProcess->getNewQueryID();	
	computeTimeout(MIN(2 * MAX_RTT_MS * MICRO_IN_MILLI, 
		timeout_ms * MICRO_IN_MILLI), &timeoutTV);
}

int DSLRecvQuery::handleLatency(const vector<NodeIdentLat>& in_remoteNodes) {
	ps->allocateEvalCount(INSTRUCTIONS_PER_IT);			
	swapcontext(&global_env_thread, ps->get_context());
	switch (ps->parser_state()) {
		case PS_RUNNING: {
			fprintf(stderr, "Exception occurred, exiting thread\n");
			setFinished(true);
		} break;				
		case PS_DONE: {
			setFinished(true);
			DSLReplyPacket tmpReplyPacket(ret_qid);				
			RealPacket* inPacket = new RealPacket(getSrcNode());
			if (inPacket == NULL) {
				return -1;	
			}
			if (tmpReplyPacket.createRealPacket(*inPacket) == -1) {
				delete inPacket;
				return -1;
			}
			// Should always be non-null;
			//if (ps->getQueryReturn() == NULL) {
			//	delete inPacket;
			//	return -1;					
			//}
			if (marshal_ast(ps->getQueryReturn(), inPacket) == -1) {
				delete inPacket;
				return -1;
			}			
			meridProcess->addOutPacket(inPacket);									
		} break;			
		case PS_READY: {
			// Do Nothing
		} break;
		case PS_BLOCKED: {
			//fprintf(stderr, "Should not be executing a blocked thread\n");			
		} break;
	}						
	return 0;	
}
int DSLRecvQuery::handleEvent(const NodeIdent& in_remote, 
		const char* inPacket, int packetSize) {
	ASTNode* retNode = NULL;
	DSLReplyPacket* tmpReplyPacket 
		= DSLReplyPacket::parse(ps, inPacket, packetSize, &retNode);
	if (tmpReplyPacket == NULL) {
		return -1;
	}
	delete tmpReplyPacket;
	// retNode guaranteed to be not NULL after successful parse
	ps->setRPCRecv(retNode);
	ps->set_parser_state(PS_READY);
	getMerid()->addPS(getQueryID());
	return 0;
}	

#include <netdb.h>
#include <errno.h>
#include <ucontext.h>

ASTNode* handleRPC(ParserState* ps, 
		const NodeIdentRendv& dest, string* func_name, ASTNode* paramAST) {						
	MeridianProcess* mp = ps->getMeridProcess();
	if (mp == NULL) {
		return ps->empty_token();	
	}
	DSLRecvQuery* parentQuery = ps->getQuery();
	if (parentQuery == NULL) {
		return ps->empty_token();
	}
	DSLReqQuery* newQ = new DSLReqQuery(mp, parentQuery->timeOut(), 
		parentQuery->getTTL(), dest, parentQuery->getQueryID());
	if (newQ == NULL) {
		return ps->empty_token();		
	}
	if (mp->getQueryTable()->insertNewQuery(newQ) == -1) {			
		delete newQ;
		return ps->empty_token();
	} else {
		// Special interface to init
		newQ->init(ps, func_name, paramAST);
	}
	ps->set_parser_state(PS_BLOCKED);
	swapcontext(ps->get_context(), &global_env_thread);
	// Get return value from ps
	return ps->getRPCRecv();	
}

ASTNode* handleDNSLookup(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	fprintf(stderr, "DNS lookup not supported on this server\n");
	return cur_parser->empty_token();
}

ASTNode* handleGetSelf(ParserState* cur_parser) {
	MeridianProcess* mp = cur_parser->getMeridProcess();
	if (mp == NULL) {
		return cur_parser->empty_token();	
	}
	NodeIdent rendvInfo = mp->returnRendv();
	NodeIdentRendv thisNodeIdent 
		= {0, 0, rendvInfo.addr, rendvInfo.port};
	return createNodeIdent(cur_parser, thisNodeIdent);
}

ASTNode* handleRingGTE(ParserState* cur_parser, ASTNode* cur_node, 
		int recurse_count, bool equal) {
	ASTNode* nextNode = eval(cur_parser, 
		cur_node->val.n_val.n_param_1,	recurse_count + 1);
	if (nextNode->type != DOUBLE_TYPE) {
		fprintf(stderr, "Double type expected\n");
		return cur_parser->empty_token();
	}	
	MeridianProcess* mp = cur_parser->getMeridProcess();
	RingSet* rs = mp->getRings();
	// Collect all satisfying members into this vector
	vector<NodeIdentRendv> outMembers;	
	// Input is in ms, Meridian stores everything as us
	int ringNum 
		= rs->getRingNumber((u_int)floor((nextNode->val.d_val * 1000.0)));				
	const vector<NodeIdent>* ringMember = rs->returnPrimaryRing(ringNum);
	for (u_int i = 0; i < ringMember->size(); i++) {
		u_int latency_us;
		if (rs->getNodeLatency((*ringMember)[i], &latency_us) != -1) {
			bool comparisonValue;
			if (equal) {				
				comparisonValue = 
					((latency_us / 1000.0) >= nextNode->val.d_val);
			} else {
				comparisonValue =
					((latency_us / 1000.0) > nextNode->val.d_val);
			}
			if (comparisonValue) {
				NodeIdentRendv tmpNodeIdent =
					{ (*ringMember)[i].addr, (*ringMember)[i].port, 0, 0};
				NodeIdent rendvNode;
				if (rs->rendvLookup((*ringMember)[i], rendvNode) == 0) {
					tmpNodeIdent.addrRendv = rendvNode.addr;
					tmpNodeIdent.portRendv = rendvNode.port;					
				}
				outMembers.push_back(tmpNodeIdent);
			}
		}
	}
	// Add the rest of the members from the higher rings
	for (int j = ringNum + 1; j < rs->getNumberOfRings(); j++) {
		ringMember = rs->returnPrimaryRing(j);
		for (u_int i = 0; i < ringMember->size(); i++) {
			NodeIdentRendv tmpNodeIdent =
				{ (*ringMember)[i].addr, (*ringMember)[i].port, 0, 0};
			NodeIdent rendvNode;
			if (rs->rendvLookup((*ringMember)[i], rendvNode) == 0) {
				tmpNodeIdent.addrRendv = rendvNode.addr;
				tmpNodeIdent.portRendv = rendvNode.port;					
			}
			outMembers.push_back(tmpNodeIdent);
		}		
	}
	string adtName = "Node";
	ASTNode* newArray 
		= ASTCreate(cur_parser, ARRAY_TYPE, &adtName, ADT_TYPE, 0);
	if (newArray->type == EMPTY_TYPE) {
		fprintf(stderr, "Out of memory\n");
		return cur_parser->empty_token();		
	}
	
	for (u_int i = 0; i < outMembers.size(); i++) {
		ASTNode* newNodeIdent = createNodeIdent(cur_parser, outMembers[i]);
		if (newNodeIdent->type == EMPTY_TYPE) {
			fprintf(stderr, "Out of memory\n");
			return cur_parser->empty_token();			
		}
		newArray->val.a_val.a_vector->push_back(newNodeIdent);
	}
	return newArray;
}

ASTNode* handleRingGT(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	return handleRingGTE(cur_parser, cur_node, recurse_count, false);
}

ASTNode* handleRingGE(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	return handleRingGTE(cur_parser, cur_node, recurse_count, true);
}

ASTNode* handleRingLTE(ParserState* cur_parser, ASTNode* cur_node, 
		int recurse_count, bool equal) {
	ASTNode* nextNode = eval(cur_parser, 
		cur_node->val.n_val.n_param_1,	recurse_count + 1);
	if (nextNode->type != DOUBLE_TYPE) {
		fprintf(stderr, "Double type expected\n");
		return cur_parser->empty_token();
	}	
	MeridianProcess* mp = cur_parser->getMeridProcess();
	RingSet* rs = mp->getRings();
	// Collect all satisfying members into this vector
	vector<NodeIdentRendv> outMembers;	
	// Input is in ms, Meridian stores everything as us
	int ringNum 
		= rs->getRingNumber((u_int)ceil((nextNode->val.d_val * 1000.0)));				
	const vector<NodeIdent>* ringMember = rs->returnPrimaryRing(ringNum);
	for (u_int i = 0; i < ringMember->size(); i++) {
		u_int latency_us;
		if (rs->getNodeLatency((*ringMember)[i], &latency_us) != -1) {
			bool comparisonValue;
			if (equal) {				
				comparisonValue = 
					((latency_us / 1000.0) <= nextNode->val.d_val);
			} else {
				comparisonValue =
					((latency_us / 1000.0) < nextNode->val.d_val);
			}
			if (comparisonValue) {
				NodeIdentRendv tmpNodeIdent =
					{ (*ringMember)[i].addr, (*ringMember)[i].port, 0, 0};
				NodeIdent rendvNode;
				if (rs->rendvLookup((*ringMember)[i], rendvNode) == 0) {
					tmpNodeIdent.addrRendv = rendvNode.addr;
					tmpNodeIdent.portRendv = rendvNode.port;					
				}
				outMembers.push_back(tmpNodeIdent);
			}
		}
	}
	// Add the rest of the members from the higher rings
	for (int j = 0; j < ringNum; j++) {
		ringMember = rs->returnPrimaryRing(j);
		for (u_int i = 0; i < ringMember->size(); i++) {
			NodeIdentRendv tmpNodeIdent =
				{ (*ringMember)[i].addr, (*ringMember)[i].port, 0, 0};
			NodeIdent rendvNode;
			if (rs->rendvLookup((*ringMember)[i], rendvNode) == 0) {
				tmpNodeIdent.addrRendv = rendvNode.addr;
				tmpNodeIdent.portRendv = rendvNode.port;					
			}
			outMembers.push_back(tmpNodeIdent);
		}		
	}
	string adtName = "Node";
	ASTNode* newArray 
		= ASTCreate(cur_parser, ARRAY_TYPE, &adtName, ADT_TYPE, 0);
	if (newArray->type == EMPTY_TYPE) {
		fprintf(stderr, "Out of memory\n");
		return cur_parser->empty_token();		
	}
	
	for (u_int i = 0; i < outMembers.size(); i++) {
		ASTNode* newNodeIdent = createNodeIdent(cur_parser, outMembers[i]);
		if (newNodeIdent->type == EMPTY_TYPE) {
			fprintf(stderr, "Out of memory\n");
			return cur_parser->empty_token();			
		}
		newArray->val.a_val.a_vector->push_back(newNodeIdent);
	}
	return newArray;
}

ASTNode* handleRingLT(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	return handleRingLTE(cur_parser, cur_node, recurse_count, false);
}

ASTNode* handleRingLE(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	return handleRingLTE(cur_parser, cur_node, recurse_count, true);
}


ReqProbeSelfGeneric::ReqProbeSelfGeneric(
				const set<NodeIdent, ltNodeIdent>& in_remote, 
				MeridianProcess* in_process)
		: 	finished(false), meridProcess(in_process) {
	qid = meridProcess->getNewQueryID();
	computeTimeout(2 * MAX_RTT_MS * MICRO_IN_MILLI, &timeoutTV);
	//	Copy all targets over
	remoteNodes.insert(in_remote.begin(), in_remote.end());
}

ReqProbeSelfGeneric::ReqProbeSelfGeneric(
				const set<NodeIdentConst, ltNodeIdentConst>& in_remote, 
				MeridianProcess* in_process)
		: 	finished(false), meridProcess(in_process) {
	qid = meridProcess->getNewQueryID();
	computeTimeout(2 * MAX_RTT_MS * MICRO_IN_MILLI, &timeoutTV);
	//	Copy all targets over
	set<NodeIdentConst, ltNodeIdentConst>::const_iterator it 
		= in_remote.begin();
	for (; it != in_remote.end(); it++) {
		NodeIdent tmp = {it->addr, it->port};
		remoteNodes.insert(tmp);		
	}
}

int ReqProbeSelfGeneric::init() {
	set<NodeIdent, ltNodeIdent>::iterator it = remoteNodes.begin();
	for (; it != remoteNodes.end(); it++) {
		ProbeQueryGeneric* newQuery = createProbeQuery(*it, getMerid());
		if (getMerid()->getQueryTable()->insertNewQuery(newQuery) == -1) {			
			delete newQuery;
			continue;
		}
		newQuery->subscribeLatency(getQueryID());
		newQuery->init();
	}
	return 0;
}

int ReqProbeSelfGeneric::returnResults() {	
	// Done, all results retrieved, create out vector to send to subscribers
	vector<NodeIdentLat> out_remoteNodes;
	// HACK: Insert 0,0 as the first node, to tell the src node
	NodeIdentLat srcIdentLat = {0, 0, 0};
	out_remoteNodes.push_back(srcIdentLat);		
	map<NodeIdent, u_int, ltNodeIdent>::iterator it = remoteLatencies.begin();
	for (; it != remoteLatencies.end(); it++) {
		NodeIdentLat tmpIdentLat = 
			{ (it->first).addr, (it->first).port, it->second }; 
		out_remoteNodes.push_back(tmpIdentLat);
	}	
	for (u_int i = 0; i < subscribers.size(); i++) {
		// Just pass it right along
		meridProcess->getQueryTable()->notifyQLatency(
			subscribers[i], out_remoteNodes);	
	}
	finished = true; // Received results from everybody	
	return 0;	
}

int ReqProbeSelfGeneric::handleLatency(
		const vector<NodeIdentLat>& in_remoteNodes) {
	//	Done (all retrieved from cache). Send return packet
	if (remoteLatencies.size() == remoteNodes.size()) {
		return returnResults();
	}
	//	Not done, therefore, the vector must have at least one entry
	if (in_remoteNodes.size() != 1) {
		return -1;		
	} 
	NodeIdent in_remote = {in_remoteNodes[0].addr, in_remoteNodes[0].port};
	u_int latency_us = in_remoteNodes[0].latencyUS;	
	if (remoteNodes.find(in_remote) == remoteNodes.end()) {
		ERROR_LOG("Received result from unexpected node\n");
		return -1;
	}
	if (remoteLatencies.find(in_remote) != remoteLatencies.end()) {
		ERROR_LOG("Received duplicate results\n");
		return -1;
	}
	remoteLatencies[in_remote] = latency_us;
	if (remoteLatencies.size() != remoteNodes.size()) {
		return 0;	// More results needed
	}	
	return returnResults();
}

int ReqProbeSelfGeneric::subscribeLatency(uint64_t in_qid) {
	subscribers.push_back(in_qid);
	return 0;
}

HandleGetDistanceGeneric::HandleGetDistanceGeneric(
			const vector<NodeIdentRendv>& in_src,	
			const vector<NodeIdentRendv>& in_remote, 
			MeridianProcess* in_process, 
			u_int in_timeout_ms, uint64_t in_prevQID) 
		: finished(false), meridProcess(in_process), prevQID(in_prevQID) {
	//	Set timeout
	qid = meridProcess->getNewQueryID();	
	computeTimeout(MIN(2 * MAX_RTT_MS * MICRO_IN_MILLI, 
		in_timeout_ms * MICRO_IN_MILLI), &timeoutTV);
	// Set srce and dest nodes vectors
	for (u_int i = 0; i < in_src.size(); i++) {
		srcNodes.push_back(in_src[i]);		
	}
	for (u_int i = 0; i < in_remote.size(); i++) {
		NodeIdent tmpIdent = { in_remote[i].addr, in_remote[i].port };
		destNodes.insert(tmpIdent);
		destNodesVect.push_back(tmpIdent);					
	}
	//	Create latency matrix
	if (srcNodes.size() > 0 && destNodesVect.size() > 0) {
		u_int matrixSize = srcNodes.size() * destNodesVect.size();
		latencyMatrixUS = (uint32_t*) malloc(sizeof(uint32_t) * matrixSize);
		//	Init all entries to UINT_MAX
		for (u_int i = 0; i < matrixSize; i++)  {
			*(latencyMatrixUS + i) = UINT_MAX;
		}
	} else {
		latencyMatrixUS = NULL;
	}
}
						
HandleGetDistanceGeneric::~HandleGetDistanceGeneric() {
	if (latencyMatrixUS != NULL) {
		free(latencyMatrixUS);
	}
}	
		
int HandleGetDistanceGeneric::handleLatency(
		const vector<NodeIdentLat>& in_remoteNodes) {	
	if (receivedRow.size() == srcNodes.size()) {
		return returnResults();			
	}		
	if (in_remoteNodes.size() < 1) {
		ERROR_LOG("There must be at least one entry\n");
		return -1;
	}
	//	HACK: The first entry tell me where the source is	
	NodeIdent curNode
		= {in_remoteNodes[0].addr, in_remoteNodes[0].port};
	//	Set values into matrix
	//	Need to iterate through the WHOLE vector, as the entries in 
	//	vector may not be unique
	for (u_int i = 0; i < srcNodes.size(); i++) {	
		if (srcNodes[i].addr == curNode.addr &&
			srcNodes[i].port == curNode.port) {
			if (receivedRow.find(i) != receivedRow.end()) {
				//	This node has already been received before
				continue;					
			}
			receivedRow.insert(i);
			// Note that we skip the first one in in_remoteNodes
			for (u_int j = 1; j < in_remoteNodes.size(); j++) {
				for (u_int k = 0; k < destNodesVect.size(); k++) {
					if (in_remoteNodes[j].addr == destNodesVect[k].addr &&
						in_remoteNodes[j].port == destNodesVect[k].port) {
						// Store the latency
						*(latencyMatrixUS + (i * destNodesVect.size() + k))
							= in_remoteNodes[j].latencyUS;
					}
				}
			}				
		}			
	}
	// Received everything, then just return results
	if (receivedRow.size() == srcNodes.size()) {
		return returnResults();			
	}
	return 0;
}
	
int HandleGetDistanceGeneric::init() {
	// Just end it, no source nodes
	if (srcNodes.size() == 0) {
		vector<NodeIdentLat> dummy;
		getMerid()->getQueryTable()->notifyQLatency(getQueryID(), dummy);						
	} else {
		for (u_int i = 0; i < srcNodes.size(); i++) {
			Query* newQuery = NULL;
			if (srcNodes[i].addr == 0 && srcNodes[i].port == 0) {
				// Local node
				newQuery = createReqSelfProbe(destNodes, getMerid()); 										
			} else {
				newQuery = createReqProbe(
					srcNodes[i], destNodes, getMerid());							
			}
			if (newQuery == NULL) {
				ERROR_LOG("Cannot create query\n");
				return -1;	
			}
			if (getMerid()->getQueryTable()->insertNewQuery(
					newQuery) == -1) {			
				delete newQuery;
			} else {
				newQuery->subscribeLatency(getQueryID());
				newQuery->init();
			}
		}
	}
	return 0;					
}
int HandleGetDistanceGeneric::returnResults() {
	finished = true;	// This query is finished regardless
	QueryTable* qt = getMerid()->getQueryTable();
	if (qt == NULL) {
		return 0;	
	}
	DSLRecvQuery* targetQuery = qt->getDSLRecvQ(prevQID);
	if (targetQuery == NULL) {
		return 0;	// Parent query has already finished
	}
	ParserState* ps = targetQuery->getPS();		
	// Create the NodeIdentLat array to pass back to RPC return
	string tmpADTString = "Measurement";
	ASTNode* tmpArrayNode = 
		ASTCreate(ps, ARRAY_TYPE, &tmpADTString, ADT_TYPE, 0);			
	if (tmpArrayNode->type == EMPTY_TYPE) {
		ps->setQueryReturn(ps->empty_token());
		// Set RPC to empty_token and quit
		return -1;				
	}		
	for (u_int i = 0; i < srcNodes.size(); i++) {
		// This works even if destNodesVect.size() is 0
		ASTNode* thisNode = createNodeIdentLat(ps, srcNodes[i],  
				latencyMatrixUS + (i * destNodesVect.size()), 
				destNodesVect.size());					
		if (thisNode->type == EMPTY_TYPE) {
			ps->setQueryReturn(ps->empty_token());
			return -1;				
		}			
		tmpArrayNode->val.a_val.a_vector->push_back(thisNode);
	}
	ps->setQueryReturn(tmpArrayNode);
	ps->set_parser_state(PS_READY);
	getMerid()->addPS(prevQID);	
	//vector<NodeIdentLat> dummyVect;
	//getMerid()->getQueryTable()->notifyQLatency(prevQID, dummyVect);		
	return 0;		
}


template <class T> 
ASTNode* handleGetDistGeneric(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	MeridianProcess* mp = cur_parser->getMeridProcess();
	if (mp == NULL) {
		return cur_parser->empty_token();	
	}
	Query* parentQuery = cur_parser->getQuery();
	if (parentQuery == NULL) {
		return cur_parser->empty_token();
	}	
	ASTNode* nextNode_1 = cur_parser->empty_token();
	if (cur_node->val.n_val.n_param_1->type != EMPTY_TYPE) {
		nextNode_1 = eval(cur_parser, 
			cur_node->val.n_val.n_param_1, recurse_count + 1);
		if (nextNode_1->type != ARRAY_TYPE ||
			nextNode_1->val.a_val.a_type != ADT_TYPE ||
			*(nextNode_1->val.a_val.a_adt_name) != "Node") {
			fprintf(stderr, "Unexpected type encountered\n");
			return cur_parser->empty_token();
		}
	}
	ASTNode* nextNode_2 = eval(cur_parser, 
		cur_node->val.n_val.n_param_2, recurse_count + 1);
	if (nextNode_2->type != ARRAY_TYPE ||
		nextNode_2->val.a_val.a_type != ADT_TYPE ||
		*(nextNode_2->val.a_val.a_adt_name) != "Node") {
		fprintf(stderr, "Unexpected type encountered\n");
		return cur_parser->empty_token();
	}	
	ASTNode* nextNode_3 = eval(cur_parser, 
		cur_node->val.n_val.n_param_3, recurse_count + 1);
	if (nextNode_3->type != INT_TYPE) {
		fprintf(stderr, "Integer type expected\n");
		return cur_parser->empty_token();
	}	
	u_int timeout_ms;
	if (nextNode_3->val.i_val < 0) {
		struct timeval curTime;
		gettimeofday(&curTime, NULL);
		QueryTable::normalizeTime(curTime);
		struct timeval qTimeout = parentQuery->timeOut();
		timeout_ms = ((qTimeout.tv_sec - curTime.tv_sec) * 1000) + (u_int)
			(ceil((double)(qTimeout.tv_usec - curTime.tv_usec)) / 1000.0);		
	} else {
		timeout_ms = nextNode_3->val.i_val;
	}	
	// Retrieve src and dest nodes from query and insert it into
	// the following vectors
	vector<NodeIdentRendv> srcNodes;
	if (nextNode_1->type == EMPTY_TYPE) {
		MeridianProcess* mp = cur_parser->getMeridProcess();
		if (mp == NULL) {
			return cur_parser->empty_token();	
		}
		NodeIdent rendvInfo = mp->returnRendv();
		NodeIdentRendv tmpIdentRendv = {0, 0, rendvInfo.addr, rendvInfo.port};
		srcNodes.push_back(tmpIdentRendv);
	} else {
		// nextNode_1 must be an array node
		for (u_int i = 0; i < nextNode_1->val.a_val.a_vector->size(); i++) {
			NodeIdentRendv tmpIdentRendv;
			if (createNodeIdent((*(nextNode_1->val.a_val.a_vector))[i], 
					&tmpIdentRendv) == -1) {
				return cur_parser->empty_token();	
			}
			srcNodes.push_back(tmpIdentRendv);
		}
	}
	vector<NodeIdentRendv> destNodes;
	for (u_int i = 0; i < nextNode_2->val.a_val.a_vector->size(); i++) {
		NodeIdentRendv tmpIdentRendv;
		if (createNodeIdent((*(nextNode_2->val.a_val.a_vector))[i], 
				&tmpIdentRendv) == -1) {
			return cur_parser->empty_token();	
		}
		destNodes.push_back(tmpIdentRendv);
	}
	// Create actual query that will handle this	
	HandleGetDistanceGeneric* newQ = new T(
		srcNodes, destNodes, mp, timeout_ms, parentQuery->getQueryID());
	if (newQ == NULL) {
		return cur_parser->empty_token();		
	}
	if (mp->getQueryTable()->insertNewQuery(newQ) == -1) {			
		delete newQ;
		return cur_parser->empty_token();
	} else {
		// Special interface to init
		newQ->init();
	}
	cur_parser->set_parser_state(PS_BLOCKED);
	swapcontext(cur_parser->get_context(), &global_env_thread);
	// See if get_distance was called without a source away
	if (nextNode_1->type == EMPTY_TYPE) {		
		ASTNode* retSingleNode = cur_parser->getQueryReturn();
		if (retSingleNode->type == ARRAY_TYPE) {
			// There should only be one entry			
			if (retSingleNode->val.a_val.a_vector->size() == 1) {
				// Just return the first item
				return (*(retSingleNode->val.a_val.a_vector))[0];
			}
		}
	}	
	// Get return value from ps
	return cur_parser->getQueryReturn();			
}

ASTNode* handleGetDistDNS(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	return handleGetDistGeneric<HandleGetDistanceDNS>(
		cur_parser, cur_node, recurse_count);
}
			
ASTNode* handleGetDistPing(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	return handleGetDistGeneric<HandleGetDistancePing>(
		cur_parser, cur_node, recurse_count);
}

ASTNode* handleGetDistTCP(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	return handleGetDistGeneric<HandleGetDistanceTCP>(
		cur_parser, cur_node, recurse_count);			
}

#ifdef PLANET_LAB_SUPPORT
ASTNode* handleGetDistICMP(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {	
	return handleGetDistGeneric<HandleGetDistanceICMP>(
		cur_parser, cur_node, recurse_count);
}
#endif

#endif

