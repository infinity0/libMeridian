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

#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <unistd.h>
#include <assert.h>
#include "MeridianProcess.h"
#include "meridian.h"

meridian::meridian(uint16_t meridian_port, uint16_t info_port, 
			u_int nodes_per_primary_ring, u_int nodes_per_secondary_ring, 
			int exponential_base) 
		: 	childPID(-1), g_meridian_port(meridian_port), 
			g_info_port(info_port), g_prim_size(nodes_per_primary_ring), 
			g_second_size(nodes_per_secondary_ring), 
			g_ring_base(exponential_base), g_initGossipInterval_s(0), 
			g_numInitIntervalRemain(0), g_ssGossipInterval_s(5), 
			g_replaceInterval_s(10), g_rendvAddr(0), g_rendvPort(0) {		
	pipeFD[0] = -1;
	pipeFD[1] = -1;		
}

meridian::~meridian() { 
	stop(); 
}

void meridian::setGossipInterval(u_int initial_s, u_int initial_length, 
								u_int steady_state_s) {
	g_initGossipInterval_s = initial_s;
	g_numInitIntervalRemain = initial_length;
	g_ssGossipInterval_s = steady_state_s;
}
	
void meridian::setReplaceInterval(u_int seconds) 	{ 
	g_replaceInterval_s = seconds; 
}	

void meridian::setRendavousNode(uint32_t addr, uint16_t port) {
	g_rendvAddr = addr;
	g_rendvPort = port;	
}
	
void meridian::addSeedNode(uint32_t addr, uint16_t port) {
	NodeIdent tmp = {addr, port};
	seedNodes.push_back(tmp);
}
	
int meridian::start() {
	if (childPID != -1) {
		return -1;	// Have to call stop first
	}
	if (pipe(pipeFD) == -1) {
		pipeFD[0] = -1;
		pipeFD[1] = -1;
		return -1;
	}
	pid_t forkRet = fork();
	if (forkRet == -1) {
		// Close both pipes
		close(pipeFD[0]);
		pipeFD[0] = -1;
		close(pipeFD[1]);			
		pipeFD[1] = -1;
		return -1;
	} else if (forkRet != 0) {
		//  Parent host
		childPID = forkRet;
		// Cannnot close pipeFD[0], or else if the child process fails
		// the parent process will get a sigpipe when it calls stop
		return 0;
	}
	close(pipeFD[1]);   // Child doesn't need to write to pipe
	pipeFD[1] = -1;
	MeridianProcess* meridInstance = new MeridianProcess(g_meridian_port, 
			g_info_port, g_prim_size, g_second_size, g_ring_base, pipeFD[0]);
	meridInstance->setRendavousNode(g_rendvAddr, g_rendvPort);
	meridInstance->setGossipInterval(g_initGossipInterval_s, 
		g_numInitIntervalRemain, g_ssGossipInterval_s);
	meridInstance->setReplaceInterval(g_replaceInterval_s);
	for (u_int i = 0; i < seedNodes.size(); i++) {
		meridInstance->addSeedNode(seedNodes[i].addr, seedNodes[i].port);
	}
	meridInstance->start();
	delete meridInstance;
	exit(0);
}
	
int meridian::stop() {
	if (childPID != -1) {
		char exitValue = 0;
		assert(pipeFD[1] != -1);
		write(pipeFD[1], &exitValue, sizeof(char));
		int status;
		waitpid(childPID, &status, 0);
		assert(pipeFD[0] != -1);
		close(pipeFD[0]);
		close(pipeFD[1]);
		pipeFD[1] = -1;			
		childPID = -1;      // So subsequent calls will be ignored
	}		
	return 0;	
}

