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

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ucontext.h>
//#include <getopt.h>
//	Marshal.h must be included to create the packets
#include "Marshal.h"
//	MeridianProcess.h is not necessary, but contains many
//	static methods that may be useful
#include "MeridianProcess.h"
//	Contains functions that are shared across all the demo applications
#include "MeridianDemo.h"
#include "MeridianDSL.h"

#include "DSLLauncher.h"

void usage() {
	fprintf(stderr, 
		"Usage: DSLLaucnher meridian_node:port program_file function " 
		"[parameters ... ]\n"
		"e.g. DSLLaucnher planetlab1.cs.cornell.edu:3964 closestNode.b "
		"closest slashdot.org\n");
}

int loadProg(ParserState& ps, char* file_name) {
	FILE* in_file = fopen(file_name, "r");
	if (!in_file) {                         
		fprintf(stderr, "Cannot open file %s\n", file_name);
		return -1;
	}
	struct stat f_stat_buf;		
	if (fstat(fileno(in_file), &f_stat_buf) == -1) {
		fprintf(stderr, "Cannot fstat file %s\n", file_name);
		fclose(in_file);
		return -1;
	}
	if (ps.input_buffer.create_buffer(f_stat_buf.st_size) == -1) {    
		fprintf(stderr, "Cannot create buffer\n");
		fclose(in_file);
		return -1;
	} 
	fread(ps.input_buffer.get_raw_buf(), f_stat_buf.st_size, 1, in_file); 
	fclose(in_file);	// Done with the file
	return 0;
}

ASTNode* bruteCreateNodeIdent(ParserState* ps, NodeIdentRendv* in_node) {
	ASTNode* retVar = ps->get_var_table()->new_stack_ast();
	retVar->type = VAR_ADT_TYPE;
	string* tmpStr = ps->get_var_table()->new_stack_string();
	*tmpStr = "Node";	
	retVar->val.adt_val.adt_type_name = tmpStr;
	retVar->val.adt_val.adt_map = ps->get_var_table()->new_stack_map();
	(*(retVar->val.adt_val.adt_map))["addr"] = mk_int(ps, in_node->addr);
	(*(retVar->val.adt_val.adt_map))["port"] = mk_int(ps, in_node->port);
	(*(retVar->val.adt_val.adt_map))["rendvAddr"] 
		= mk_int(ps, in_node->addrRendv);
	(*(retVar->val.adt_val.adt_map))["rendvPort"] 
		= mk_int(ps, in_node->portRendv);
	return retVar;
}

ASTNode* createNodeArray(ParserState* ps) {
	ASTNode* retArray = ps->get_var_table()->new_stack_ast();
	retArray->type = ARRAY_TYPE;
	retArray->val.a_val.a_type = ADT_TYPE;
	string* tmpStr = ps->get_var_table()->new_stack_string();
	*tmpStr = "Node";
	retArray->val.a_val.a_adt_name = tmpStr;
	retArray->val.a_val.a_vector = ps->get_var_table()->new_stack_vector();
	retArray->val.a_val.a_var_table = ps->get_var_table();
	return retArray;
}

ASTNode* createParams(ParserState& ps, int argc, char* argv[]) {
	// Create parameters
	ASTNode* nodeArray = createNodeArray(&ps);
	if (nodeArray->type == EMPTY_TYPE) {
		fprintf(stderr, "Cannot create Node array\n");
		return NULL;	
	}
	ASTNode* sepNode = mk_sep_list(&ps, nodeArray);
	if (sepNode->type == EMPTY_TYPE) {
		fprintf(stderr, "Cannot create sep type\n");
		return NULL;	
	}	
	// Marshal the string array
	for (int i = 0; i < argc; i++) {
		struct hostent* he = gethostbyname(argv[i]);
		if (he == NULL) {
			fprintf(stderr, "gethostbyname failed\n");
			return NULL;				
		}
		NodeIdentRendv tmpNode = 
			{ ntohl(((struct in_addr *)(he->h_addr))->s_addr), 80, 0, 0};
		ASTNode* newNode = bruteCreateNodeIdent(&ps, &tmpNode);
		if (newNode->type == EMPTY_TYPE) {
			fprintf(stderr, "Cannot create string object\n");
			return NULL;				
		} 
		nodeArray->val.a_val.a_vector->push_back(newNode);
	}
	return sepNode;
}

// First param is addr:port, second is program file, third is function,
// the rest are string parameters
int main(int argc, char* argv[]) {
	if (argc < 4) {
		usage(); // Not enough parameters;
		return -1;
	}
	//	Get the node we would like to send it
	char* meridNode = argv[1];
	NodeIdent remoteNode;	// Now we fill the remoteNode struct, which holds
							// the ip and port of the Meridian node that 
							// we will contact
	if (parseHostAndPort(meridNode, remoteNode) == -1) {
		return -1;
	}
	ParserState ps;
	//	Load the program from disk
	char* progArg = argv[2];
	if (loadProg(ps, progArg) == -1) {
		fprintf(stderr, "Cannot read program file %s\n", progArg);
		return -1;
	}	
	string func_name = argv[3];
	//	Create the socket
	int meridSock;
	if ((meridSock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		perror("Cannot create UDP socket");
		return -1;
	}
	//	Bind the socket to any port. The early binding is to 
	//	determine ahead of time what port is used. The port
	//	number is used in generating query ids.	
	struct sockaddr_in myAddr;
	myAddr.sin_family 		= AF_INET;
	myAddr.sin_port 		= 0;
	myAddr.sin_addr.s_addr 	= INADDR_ANY;
	memset(&(myAddr.sin_zero), '\0', 8);
	if (bind(meridSock, (struct sockaddr*)&myAddr, 
			sizeof(struct sockaddr)) == -1) {
		perror("Cannot bind UDP socket to desired port");
		close(meridSock);
		return -1;
	}	
	srand(time(NULL));
	uint64_t queryNum = getNewQueryID(meridSock);
	ASTNode* paramNode = createParams(ps, argc - 4, &(argv[4]));
	if (paramNode == NULL) {
		close(meridSock);
		return -1;
	}	
	//	Create the packet
	RealPacket in_packet(remoteNode);	
#define DEFAULT_TIMEOUT		10000
#define DEFAULT_TTL			500
	DSLRequestPacket tmpDSLPacket(
		queryNum, DEFAULT_TIMEOUT, DEFAULT_TTL, 0, 0);
	if (tmpDSLPacket.createRealPacket(in_packet) == -1) {
		fprintf(stderr, "Cannot create real packet\n");
		close(meridSock);
		return -1;		
	}
	if (marshal_packet(&ps, in_packet, &func_name, paramNode) == -1) {
		fprintf(stderr, "Cannot marshal packet\n");
		close(meridSock);
		return -1;	
	}
	cout << "Sending query " << queryNum << endl;	
	//printf("Sending query %llu\n", queryNum);
	//	Set start time to determine the amount of time require to
	//	satisfy the query
	struct timeval startTime;
	gettimeofday(&startTime, NULL);
	struct sockaddr_in hostAddr;
	hostAddr.sin_family         = AF_INET;
	hostAddr.sin_port           = htons(in_packet.getPort());
	hostAddr.sin_addr.s_addr    = htonl(in_packet.getAddr());
	memset(&(hostAddr.sin_zero), '\0', 8);
	int sendRet = sendto(meridSock, in_packet.getPayLoad(),
		in_packet.getPayLoadSize(), 0,
		(struct sockaddr*)&hostAddr, sizeof(struct sockaddr));
	// TODO: Wait for return value here
	if (sendRet == -1) {
		fprintf(stderr, "Error on RPC send\n");
		close(meridSock);
		return -1;
	}	
	//	Now wait for reply packet
	struct sockaddr_in theirAddr;
	char buf[MAX_UDP_PACKET_SIZE];
	int addrLen = sizeof(struct sockaddr);
	//	Perform actual recv on socket
	int numBytes = recvfrom(meridSock, buf, MAX_UDP_PACKET_SIZE, 0,
		(struct sockaddr*)&theirAddr, (socklen_t*)&addrLen);		
	if (numBytes == -1) {
		perror("Error on recvfrom");
		close(meridSock);
		return -1;		
	}
	close(meridSock);	// Done with socket
	ASTNode* retNode = NULL;
	DSLReplyPacket* tmpReplyPacket 
		= DSLReplyPacket::parse(&ps, buf, numBytes, &retNode);
	delete tmpReplyPacket;
	Measurement retMeasure;
	if (createMeasurement(retNode, &retMeasure) == 0) {
		uint32_t tmpAddr = htonl(retMeasure.addr);
		char* addrStr = inet_ntoa(*((struct in_addr*)&tmpAddr));		
		printf("Query returned node %s:%d\n", addrStr, retMeasure.port);
		for (u_int i = 0; i < retMeasure.distance.size(); i++) {
			printf("--> %0.2f\n",  retMeasure.distance[i]);
		}
	}
	// Else returned a value that I don't know how to handle
	struct timeval endTime;
	gettimeofday(&endTime, NULL);
	printf("Time to complete query: %0.2f ms\n",
		((endTime.tv_sec - startTime.tv_sec) * 1000.0) +
		((endTime.tv_usec - startTime.tv_usec) / 1000.0));
	return 0;
}

