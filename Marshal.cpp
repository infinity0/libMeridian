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

#include <map>
#include "Marshal.h"
#include "RingSet.h"


// This Hash function is used to hash the application name
// into a magic number unique to the application string
//
// Will be cleaned up in the future to not depend on the static variables, 
static uint32_t g_marshal_hash_val;
static bool 	g_marshal_hash_already = false;

uint32_t marshalHashName(char* in_name) {
	if (g_marshal_hash_already) {
		// We already hashed this earlier
		return g_marshal_hash_val;
	}
	u_char mdHash[MD5_DIGEST_LENGTH];
	MD5((u_char*)in_name, strlen(in_name), mdHash);
	// Just take the first 32 bits
	memcpy(&g_marshal_hash_val, mdHash, sizeof(g_marshal_hash_val));
	g_marshal_hash_already = true;
	WARN_LOG_1("Hash name is %s\n", in_name);
	WARN_LOG_1("Hash value is %x\n", g_marshal_hash_val); 
	return g_marshal_hash_val;
}

int InfoPacket::createRealPacket(RealPacket& inPacket) const {
	inPacket.append_char(getPacketType());
	write_id(inPacket);		
	int dummyPos = inPacket.getPayLoadSize();	// Save position in packet
	inPacket.append_uint(0);					// Put in dummy value for now
	uint32_t numRings = rings->getNumberOfRings();
	uint32_t numMembers = 0;
	for (uint32_t i = 0; i < numRings; i++) {
		const vector<NodeIdent>* primRing = rings->returnPrimaryRing(i);			
		if (primRing != NULL && primRing->size() > 0) {
			for (uint32_t j = 0; j < primRing->size(); j++) {
				NodeIdent tmp = (*primRing)[j];
				uint32_t latencyUS;
				if (rings->getNodeLatency(tmp, &latencyUS) == -1) {
					ERROR_LOG("Latency of ring member not avaliable\n");
					continue;
				}
				numMembers++;
				inPacket.append_uint(htonl(i));					
				inPacket.append_uint(htonl(tmp.addr));
				inPacket.append_ushort(htons(tmp.port));
				inPacket.append_uint(htonl(latencyUS));					
			}
		}
	}				
	if (!inPacket.completeOkay()) { 
		return -1; 
	}
	//	Overwrite previous dummy position with real value
	numMembers = htonl(numMembers);
	memcpy(inPacket.getPayLoad() + dummyPos, &numMembers, sizeof(uint32_t));
	return 0;
}

int InfoPacket::parse(const char* buf, int numBytes, 
		map<u_int, vector<NodeIdentLat>*>& inMap) {
	if (inMap.size() > 0) {
		ERROR_LOG("Map param must be initially empty\n"); 
		return -1;		
	}
	BufferWrapper rb(buf, numBytes);
	char queryType = rb.retrieve_char();
	if (rb.error() || queryType != INFO_PACKET) {
		ERROR_LOG("Wrong type received\n");
		return -1;	
	}
	rb.retrieve_uint();	// Skip queryID_1
	rb.retrieve_uint(); // Skip queryID_3
	uint32_t magicNumber = ntohl(rb.retrieve_uint());		
	if (rb.error() || magicNumber != MAGIC_NUMBER) {
		ERROR_LOG("Wrong magic number in packet received\n");		
		return -1;			
	}
	uint32_t numMembers = ntohl(rb.retrieve_uint());
	for (uint32_t i = 0; i < numMembers; i++) {
		NodeIdentLat tmpNode;
		uint32_t in_ring = ntohl(rb.retrieve_uint());
		tmpNode.addr = ntohl(rb.retrieve_uint());
		tmpNode.port = ntohs(rb.retrieve_ushort());
		tmpNode.latencyUS  = ntohl(rb.retrieve_uint());
		map<u_int, vector<NodeIdentLat>*>::iterator it = inMap.find(in_ring); 	
		if (it == inMap.end()) {			
			vector<NodeIdentLat>* tmp = new vector<NodeIdentLat>();
			tmp->push_back(tmpNode);
			inMap[in_ring] = tmp;			
		}  else {
			it->second->push_back(tmpNode);	
		}
	}
	if (rb.error()) {
		map<u_int, vector<NodeIdentLat>*>::iterator it = inMap.begin();
		for (; it != inMap.end(); it++) {
			delete it->second;
		}
		inMap.clear();		
		return -1;
	}
	return 0;	
}
