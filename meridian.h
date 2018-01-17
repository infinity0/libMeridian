#ifndef CLASS_MERIDIAN
#define CLASS_MERIDIAN 

using namespace std;

#include <stdint.h>
#include <vector>
#include "Marshal.h"

class meridian {
private:
	int 				pipeFD[2];
	pid_t				childPID;
	vector<NodeIdent> 	seedNodes;
	u_short 			g_meridian_port; 
	u_short 			g_info_port;
	u_int 				g_prim_size;
	u_int 				g_second_size;
	int 				g_ring_base;
	u_int				g_initGossipInterval_s;
	u_int				g_numInitIntervalRemain;
	u_int				g_ssGossipInterval_s;
	u_int				g_replaceInterval_s;
	uint32_t			g_rendvAddr;
	uint16_t			g_rendvPort;
	

public:
	/**************************************************************************
		Only provided constructor of Meridian
	
		Description of Params:
		----------------------
		meridian_port: 				Port that Meridian listens on
		info_port:					TCP port where info of rings can be queried
									(0 means this service is off)
		nodes_per_primary_ring		Number of nodes in each primary ring
		nodes_per_secondary_ring	Number of nodes in each secondary ring
		exponential_base			Exponetial base of ring		
	**************************************************************************/
	meridian(
		uint16_t	meridian_port,
		uint16_t 	info_port,
		u_int 		nodes_per_primary_ring,
		u_int 		nodes_per_secondary_ring,
		int 		exponential_base);
	
		
	/**************************************************************************
		Default destructor. All sockets will be closed when this is called
	**************************************************************************/
	~meridian();
	

	/**************************************************************************
		Sets the gossiping rate of Meridian
	
		Description of Params:
		----------------------
		initial_s: 					Initial gossip period in seconds
		initial_length:				Number of initial gossip periods
		steady_state_s				Steady state gossip period in seconds
	**************************************************************************/	
	void setGossipInterval(
		u_int 	initial_s, 
		u_int 	initial_length, 
		u_int 	steady_state_s);
	
		
	/**************************************************************************
		Sets the replacement rate of Meridian
		
		Description of Params:
		----------------------
		seconds: 					Replacement period in seconds
	**************************************************************************/
	void setReplaceInterval(u_int seconds);
	
	
	/**************************************************************************
		Add initial seed nodes 
		
		Description of Params:
		----------------------
		addr: 						IP address of the seed node
		port:						Meridian port of seed node
	**************************************************************************/	
	void addSeedNode(uint32_t addr, uint16_t port);
	
	/**************************************************************************
		For nodes behind firewalls, set the rendavous node to connect to
		at start up. All traffic (except direct latency measurements) are
		routed through the rendavous node. Latency measurements are always
		initiated from the firewalled node
		
		Description of Params:
		----------------------
		addr: 						IP address of the rendavous node
		port:						Meridian port of rendavous node
	**************************************************************************/	
	void setRendavousNode(uint32_t addr, uint16_t port);	
	
	/**************************************************************************
		Starts the meridian service. Note that subsequent calls to 
		setGossipInterval and setReplaceInterval are ignored
	**************************************************************************/	
	int start();
	
	
	/**************************************************************************
		Stops the meridian service. Can restart it by calling start.
	**************************************************************************/
	int stop();
};

#endif
