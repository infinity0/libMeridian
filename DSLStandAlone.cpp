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

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
//#include <setjmp.h>
#include <ucontext.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <list>
#include "Marshal.h"
#include "MQLState.h"
#include "MeridianDSL.h"

void FloatingPointError(int sig_num) {
	if (sig_num == SIGFPE) {
		DSL_ERROR( "Floating point error\n");		
		signal(SIGFPE, FloatingPointError);
		setcontext(&global_env_thread);
		//longjmp(global_env_error, -1);
	}
}

int setNonBlock(int fd) {
	int sockflags;
	if ((sockflags = fcntl(fd, F_GETFL, 0)) != -1){
		sockflags |= O_NONBLOCK;
		return fcntl(fd, F_SETFL, sockflags);
	}
	return -1;
}

ASTNode* handleDNSLookup(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) { 
	ASTNode* nextNode = eval(cur_parser, 
		cur_node->val.n_val.n_param_1,	recurse_count + 1);
	if (nextNode->type != STRING_TYPE) {
		DSL_ERROR( "Double type expected\n");
		return cur_parser->empty_token();
	}
	struct hostent* he 
		= gethostbyname(nextNode->val.s_val->c_str());
	if (he == NULL) {
		DSL_ERROR( "gethostbyname failed\n");
		return cur_parser->empty_token();				
	}
	ASTNode* ret = mk_int(cur_parser, 
		ntohl(((struct in_addr *)(he->h_addr))->s_addr));
	if (ret == NULL) {
		DSL_ERROR( "Out of memory\n");
		ret = cur_parser->empty_token();
	}
	return ret;
}

ASTNode* handleGetSelf(ParserState* cur_parser) {
	DSL_ERROR( "get_self cannot be called on non-Meridian nodes\n");
	return cur_parser->empty_token();	
}

ASTNode* handleRingGT(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	DSL_ERROR( "ring_gt cannot be called on non-Meridian nodes\n");
	return cur_parser->empty_token();	
}

ASTNode* handleRingGE(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	DSL_ERROR( "ring_ge cannot be called on non-Meridian nodes\n");
	return cur_parser->empty_token();	
}

ASTNode* handleRingLT(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	DSL_ERROR( "ring_lt cannot be called on non-Meridian nodes\n");
	return cur_parser->empty_token();	
}

ASTNode* handleRingLE(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	DSL_ERROR( "ring_le cannot be called on non-Meridian nodes\n");
	return cur_parser->empty_token();	
}

ASTNode* handleGetDistTCP(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	DSL_ERROR( "get_distance_tcp "
		"cannot be called on non-Meridian nodes\n");
	return cur_parser->empty_token();	
}

ASTNode* handleGetDistDNS(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	DSL_ERROR( "get_distance_dns "
		"cannot be called on non-Meridian nodes\n");
	return cur_parser->empty_token();	
}

ASTNode* handleGetDistPing(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	DSL_ERROR( "get_distance_ping "
		"cannot be called on non-Meridian nodes\n");
	return cur_parser->empty_token();	
}

#ifdef PLANET_LAB_SUPPORT
ASTNode* handleGetDistICMP(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	DSL_ERROR( "get_distance_icmp "
		"cannot be called on non-Meridian nodes\n");
	return cur_parser->empty_token();	
}
#endif

ASTNode* handleRPC(ParserState* ps, 
		const NodeIdentRendv& dest, string* func_name, ASTNode* paramAST) {
	RealPacket* in_packet = new RealPacket(dest);
	DSLRequestPacket tmpDSLPacket(1234, 10000, 500, 0, 0);
	if (tmpDSLPacket.createRealPacket(*in_packet) == -1) {
		DSL_ERROR( "Cannot create real packet\n");
		delete in_packet;
		return ps->empty_token();
	}
	if (marshal_packet(ps, *in_packet, func_name, paramAST) == -1) {
		DSL_ERROR( "Cannot marshal packet\n");
		delete in_packet;
		return ps->empty_token();	
	}
	//	If no rendavous required, just use the in_packet
	RealPacket* out_packet = in_packet;
	// Handle firewall host by wrapping it around a PUSH packet
	if (in_packet->getRendvAddr() != 0 || in_packet->getRendvPort() != 0) {
		//	QID for push packet should never be used, just set it to 0
		PushPacket pushPacket(0, in_packet->getAddr(), in_packet->getPort());
		NodeIdent rendvNode 
			= {in_packet->getRendvAddr(), in_packet->getRendvPort()};
		//	This packet MUST not have a rendavous host
		RealPacket* tmpPacket = new RealPacket(rendvNode);
		if (pushPacket.createRealPacket(*tmpPacket) == -1) {
			ERROR_LOG("Cannot create PUSH packet\n");
			delete in_packet;
			delete tmpPacket;
			return ps->empty_token();
		}
		tmpPacket->append_packet(*in_packet);
		out_packet = tmpPacket; // Switch out_packet over
		delete in_packet; 		// Done with in_packet
		// Final check to see if packet is constructed correctly		
		if (!(out_packet->completeOkay())) {			
			ERROR_LOG("Cannot create PUSH packet\n");
			delete out_packet;
			return ps->empty_token();
		}
	}				
	int tmp_socket;			
	if ((tmp_socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		DSL_ERROR( "Cannot create socket\n");
		delete out_packet;
		return ps->empty_token();
	}
	struct sockaddr_in hostAddr;
	hostAddr.sin_family         = AF_INET;
	hostAddr.sin_port           = htons(out_packet->getPort());
	hostAddr.sin_addr.s_addr    = htonl(out_packet->getAddr());
	memset(&(hostAddr.sin_zero), '\0', 8);
	int sendRet = sendto(tmp_socket, out_packet->getPayLoad(),
		out_packet->getPayLoadSize(), 0,
		(struct sockaddr*)&hostAddr, sizeof(struct sockaddr));
	delete out_packet; // Done with out packet
	// TODO: Wait for return value here
	if (sendRet == -1) {
		DSL_ERROR( "Error on RPC send\n");
		return ps->empty_token();
	}
	//	Now wait for reply packet
	struct sockaddr_in theirAddr;
	char buf[MAX_UDP_PACKET_SIZE];
	int addrLen = sizeof(struct sockaddr);
	//	Perform actual recv on socket
	int numBytes = recvfrom(tmp_socket, buf, MAX_UDP_PACKET_SIZE, 0,
		(struct sockaddr*)&theirAddr, (socklen_t*)&addrLen);		
	if (numBytes == -1) {
		perror("Error on recvfrom");
		return ps->empty_token();		
	}
	close(tmp_socket);	
	ASTNode* retNode = NULL;
	DSLReplyPacket* tmpReplyPacket 
		= DSLReplyPacket::parse(ps, buf, numBytes, &retNode);
	delete tmpReplyPacket;
	return retNode;
}

int waitForProgram(int port) {
	int dummy_sock;	// Used only to allow scheduling of processes
	if ((dummy_sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		perror("Cannot create UDP socket");			
		return -1;
	}	
	list<ParserState*> ps_list;
	
	int read_sock;	//	Main UDP socket
	if ((read_sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		perror("Cannot create UDP socket");			
		return -1;
	}
	// increaseSockBuf(read_sock);
	//	Set up to listen to meridian port
	struct sockaddr_in myAddr;
	myAddr.sin_family 		= AF_INET;
	myAddr.sin_port 		= htons(port);
	myAddr.sin_addr.s_addr 	= INADDR_ANY;
	memset(&(myAddr.sin_zero), '\0', 8);
	if (bind(read_sock, (struct sockaddr*)&myAddr, 
			sizeof(struct sockaddr)) == -1) {
		ERROR_LOG("Cannot bind UDP socket to desired port\n");
		close(read_sock);
		return -1;
	}
	//	Make the socket non-blocking
	if (setNonBlock(read_sock) == -1) {
		ERROR_LOG("Cannot set socket to be non-blocking\n");		
		return -1;
	}
	fd_set read_set, write_set;
	FD_ZERO(&read_set); FD_ZERO(&write_set);	
	//	Adding socket to read set 
	FD_SET(read_sock, &read_set);
	int maxFD = MAX(read_sock, dummy_sock);	
	//	Declaring structures that will be reused over and over
	fd_set currentReadSet, currentWriteSet;
	//	Main event driven select loop
	while (true) {	
		// 	TODO: Add proper timeout mechanism
		//	Reset fd_set values
		memcpy(&currentReadSet, &read_set, sizeof(fd_set));
		memcpy(&currentWriteSet, &write_set, sizeof(fd_set));
		
		int selectRet = select(maxFD+1, 
			&currentReadSet, &currentWriteSet, NULL, NULL);			
			
		if (selectRet == -1) {
			if (errno == EINTR) {					
				continue; // Interrupted by signal, retry
			}
			ERROR_LOG("Select returned an error\n");
			return -1;	// Return with error
		}
		
		if (FD_ISSET(read_sock, &currentReadSet)) {
			char buf[MAX_UDP_PACKET_SIZE];
			struct sockaddr_in theirAddr;
			int addrLen = sizeof(struct sockaddr);
			//	Perform actual recv on socket
			int numBytes = recvfrom(read_sock, buf, MAX_UDP_PACKET_SIZE, 0,
				(struct sockaddr*)&theirAddr, (socklen_t*)&addrLen);		
			if (numBytes == -1) {
				perror("Error on recvfrom");
				continue;				
			}
			//NodeIdent remoteNode = {ntohl(theirAddr.sin_addr.s_addr), 
			//						ntohs(theirAddr.sin_port) };
									
			ParserState* ps = new ParserState();			
			//BufferWrapper rb(buf, numBytes);
			DSLRequestPacket* inPacket 
				= DSLRequestPacket::parse(ps, buf, numBytes);
			if (inPacket == NULL) {			
			//if (unmarshal_packet(*ps, rb) == -1) {
				DSL_ERROR( "Error unmarshaling\n");
				delete ps;							
			} else {
				delete inPacket;	// TODO: Need this later
				if (yyparse((void*)ps) != -1) {
					if (ps->save_context() != -1) {
						makecontext(ps->get_context(), 
							(void (*)())(&jmp_eval), 1, ps);
						ps_list.push_back(ps);
						FD_SET(dummy_sock, &write_set);
					} else {
						delete ps;	// Save context failed
					}
				} else {					
					delete ps;	// parse Error	
				}
			}
		}
		
		if (FD_ISSET(dummy_sock, &currentWriteSet)) {
			vector<list<ParserState*>::iterator> delete_vect;
			vector<list<ParserState*>::iterator> clear_vect;
			list<ParserState*>::iterator it = ps_list.begin();

#define MAX_THREADS_PER_ITERATION 5
			for (int itCount = 0; 
					it != ps_list.end() && itCount < MAX_THREADS_PER_ITERATION; 
					it++, itCount++) {				
				ParserState* ps = *it;
				ps->allocateEvalCount(10000);			
				swapcontext(&global_env_thread, ps->get_context());
				switch (ps->parser_state()) {
					case PS_RUNNING: {
						DSL_ERROR( "Exception occurred, exiting thread\n");
						delete_vect.push_back(it);
					} break;				
					case PS_DONE: {				
						//printf("DOOOOOONE\n");
						delete_vect.push_back(it);						
					} break;			
					case PS_READY: {
						ps_list.push_back(ps);
						clear_vect.push_back(it);
					} break;
					case PS_BLOCKED: {
						DSL_ERROR( "Should not be in blocked state\n");
						delete_vect.push_back(it);						
					} break;					
				}				
			}
			//	Just remove it from the list, as we moved 
			//	it to another position
			for (u_int i = 0; i < clear_vect.size(); i++) {
				ps_list.erase(clear_vect[i]);
			}
			
			//	Clear and delete the parser state
			for (u_int i = 0; i < delete_vect.size(); i++) {
				it = delete_vect[i];
				ParserState* ps = *it;
				ps_list.erase(it);
				delete ps;
			}
			//	Turn off trigger if no process need to be executed
			if (ps_list.empty()) {
				FD_CLR(dummy_sock, &write_set);	
			}
		}
	}
	close(read_sock);
	close(dummy_sock);	
	return 0;
}

int main(int argc, char* argv[]) {
	signal(SIGFPE, FloatingPointError);
	
	//ucontext_t cur_context;
	//global_env_error = &cur_context;	
	
	set<ParserState*> ps_set;
	// Iterate through the files to load
	for (int i = 1; i < argc; i++) {
		FILE* in_file = fopen(argv[i], "r");
		if (!in_file) {                         
			DSL_ERROR( "Cannot open file %s\n", argv[i]);
			continue;
		}
		struct stat f_stat_buf;		
		if (fstat(fileno(in_file), &f_stat_buf) == -1) {
			DSL_ERROR( "Cannot fstat file %s\n", argv[i]);
			fclose(in_file);
			continue;
		}		
		ParserState* ps = new ParserState();				
		if (ps->input_buffer.create_buffer(f_stat_buf.st_size) == -1) {    
			DSL_ERROR( "Cannot create buffer\n");
			fclose(in_file);
			delete ps;
			continue;
		} 
		fread(ps->input_buffer.get_raw_buf(), f_stat_buf.st_size, 1, in_file); 
		fclose(in_file);	// Done with the file
		ps->set_func_string("main");
		g_parser_line = 1;	// Reset line count for parser		
		int ret = yyparse((void*)ps);
		if (ret != -1) {
			if (ps->save_context() == -1) {
				delete ps;
				continue;
			}
			//printf( "Creating child fiber\n" );
			makecontext(ps->get_context(), (void (*)())(&jmp_eval), 1, ps);
			ps_set.insert(ps);
		}									
	}
	
	while (ps_set.size() > 0) {
		set<ParserState*>::iterator it = ps_set.begin();
		vector<set<ParserState*>::iterator> delete_vect;		
		for (; it != ps_set.end(); it++) {
			ParserState* ps = *it;
			ps->allocateEvalCount(10000);			
			swapcontext(&global_env_thread, ps->get_context());
			switch (ps->parser_state()) {
				case PS_RUNNING: {
					DSL_ERROR( "Exception occurred, exiting thread\n");
					delete_vect.push_back(it);
				} break;				
				case PS_DONE: {				
					//printf("DOOOOOONE\n");
					delete_vect.push_back(it);
				} break;			
				case PS_READY: {
					//printf("More cycles needed\n");
				} break;
				case PS_BLOCKED: {
					DSL_ERROR( "Should not be in blocked state\n");
					delete_vect.push_back(it);						
				} break;					
			}		
		}
		for (u_int i = 0; i < delete_vect.size(); i++) {
			it = delete_vect[i];
			ParserState* ps = *it;
			ps_set.erase(it);
			delete ps;
		}
	}
	
//	waitForProgram(MERIDIAN_DSL_PORT);
	return 0;
}

