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
#include <string.h>
#include <stdio.h>
#include <netdb.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/wait.h>
#include <curl/curl.h>
#include "meridian.h"

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX			1024
#endif
#define PORT_ASCII_SIZE_MAX 	5	
#define HOST_PORT_MAX			(HOST_NAME_MAX + PORT_ASCII_SIZE_MAX + 1)
#define MAX_TIMEOUT				10

//	Default values for various parameters
static int merid_port 			= 3964;
static int info_port 			= 0;
static int nodes_per_primary 	= 4;
static int nodes_per_second 	= 4;
static int exponential_base 	= 2;
static int gossip_init_value 	= 5;
static int gossip_init_period 	= 1;
static int gossip_ss_value 		= 30;
static int replace_period 		= 60;
static uint32_t rendavous_addr 	= 0;
static uint16_t rendavous_port 	= 0;
static int detector_interval 	= 10;
static int odht_port 			= 5851; 

// Holds all seed nodes passed as params
static vector<pair<uint32_t, uint16_t>*> param_seeds;
// URL to send to curl detector for liveness 
static char g_url[HOST_PORT_MAX];

int detectorCurl(char* in_url) {
	CURL* curl = curl_easy_init();
	if (!curl) {
		return -1;	// Out of memory
	}
	FILE* nullFile = fopen("/dev/null", "w");
	if (nullFile == NULL) {
		return -1;	// Cannot open /dev/null	
	}
	curl_easy_setopt(curl, CURLOPT_URL, in_url);	
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, nullFile);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);	
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, MAX_TIMEOUT);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, MAX_TIMEOUT);
	CURLcode res = curl_easy_perform(curl);
	int retVal = (res == CURLE_OK) ? 0 : -1; 
	// Cleanup before exiting
	curl_easy_cleanup(curl);
	fclose(nullFile);
	return retVal;
}

int retrieveURL(uint16_t in_port, char* in_url, int url_len) {
	static char cur_host[HOST_NAME_MAX];
	if (gethostname(cur_host, HOST_NAME_MAX) == -1) {
		fprintf(stderr, "Cannot get local hostname\n");
		return -1;
	}	
	// In case of truncation, NULL terminate
	cur_host[HOST_NAME_MAX - 1] = '\0';
	// Generate URL for detector
	int res_url = 
		snprintf(in_url, url_len, "%s:%d", cur_host, in_port);
	if (res_url == -1 || res_url >= url_len) {
		fprintf(stderr, "Cannot generate URL for detector\n");
		return -1;
	}
	// Just in case, ensure NULL terminate
	in_url[url_len - 1] = '\0';
	return 0;
}

void usage() {
	fprintf(stderr,
	"Usage: demoODHT [options] [seed nodes]\n\n"
	"Options:\n"
	"  -t seconds\t\tOpenDHT detector interval in seconds (default: %d)\n"
	"  -q port\t\tOpenDHT status port (default: %d)\n"	
	"  -m port\t\tMeridian port number (default: %d)\n"
	"  -i port\t\tInformation port number (default: %d)\n"
	"  -p size\t\tNumber of nodes in each primary ring (default: %d)\n"
	"  -s size\t\tNumber of nodes in each secondary ring (default: %d)\n"
	"  -e base\t\tThe exponetial base of the ring (default: %d)\n"
	"  -h     \t\tHelp of command line parameters\n\n"
	"  -g init:num:ss\tGossip interval in seconds, separated into initial\n" 
	"                \tperiod, number of initial periods, and steady state\n"
	"                \tperiod (default: %d:%d:%d)\n"
	"  -r interval\t\tReplacement interval length in seconds (default: %d)\n\n"
	"  -d addr:port\t\tAddress and port of rendavous node (default: %d:%d)\n\n"	
	"Seed Nodes should be specified in hostname:port format\n\n",
	detector_interval, odht_port, 
	merid_port, info_port, nodes_per_primary, nodes_per_second, 
	exponential_base, gossip_init_value, gossip_init_period, 
	gossip_ss_value, replace_period, rendavous_addr, rendavous_port);			
}

int handleParams(int argc, char* argv[]) {
	int option_index = 0;
	static struct option long_options[] = {
		{"merid_port", 1, NULL, 1},
		{"info_port", 1, NULL, 2},
		{"primary_ring_size", 1, NULL, 3},
		{"secondary_ring_size", 1, NULL, 4},
		{"exponential_base", 1, NULL, 5},
		{"gossip_interval", 1, NULL, 6},
		{"replacement_interval", 1, NULL, 7},
		{"help", 0, NULL, 8},
		{"d", 1, NULL, 9},
		{"t", 1, NULL, 10},
		{"q", 1, NULL, 11},
		{0, 0, 0, 0}
	};
	// 	Start parsing parameters 
	while (true) {
		int c = getopt_long_only(argc, argv, "", long_options, &option_index);
		if (c == -1) {
			break;	// Done
		}
		switch (c) {
		case 1:
			merid_port = atoi(optarg);
			break;
		case 2:
			info_port = atoi(optarg);
			break;
		case 3:
			nodes_per_primary = atoi(optarg);
			break;
		case 4:
			nodes_per_second = atoi(optarg);
			break;
		case 5:
			exponential_base = atoi(optarg);
			break;
		case 6: {
				if (optarg == NULL) {
					fprintf(stderr, "Invalid format specified for gossip\n");
					return -1;					
				}
				int len = strlen(optarg);
				if (len <= 0) {
					fprintf(stderr, "Invalid format specified for gossip\n");
					return -1;
				}
				len++;	// Include the null pointer
				char* tmp = (char*) malloc(sizeof(char) * len);
				if (!tmp) {
					fprintf(stderr, "Malloc returned an error\n");
					return -1;
				}
				memcpy(tmp, optarg, len);				
				char* initValue = strchr(tmp, ':');
				if (!initValue) {
					fprintf(stderr, "Invalid format specified for gossip\n");
					free(tmp);
					return -1;
				}
				*initValue = '\0';	// Set ':' to NULL
				initValue++;		// and then skip it
				gossip_init_value = atoi(tmp);				
				char* initPeriod = strchr(initValue, ':');
				if (!initPeriod) {
					fprintf(stderr, "Invalid format specified for gossip\n");
					free(tmp);
					return -1;
				}
				*initPeriod = '\0';	// Set ':' to NULL
				initPeriod++;		// and then skip it
				gossip_init_period = atoi(initValue);
				gossip_ss_value = atoi(initPeriod);
				free(tmp);
			}
			break;
		case 7:
			replace_period = atoi(optarg);
			break;
		case 8:
			usage();
			return -1;
		case 9: {
				static char hostname[HOST_NAME_MAX];
				char* tmpStrPtr = strchr(optarg, ':');
				if (tmpStrPtr == NULL) {
					fprintf(stderr, "Invalid rendavous parameter," 
						" should be hostname:port\n");
					return -1;
				}
				memcpy(hostname, optarg, tmpStrPtr - optarg);
				hostname[tmpStrPtr - optarg] = '\0';
				u_short remotePort = (u_short) atoi(tmpStrPtr + 1);
				//	Get the add of remote host
				struct hostent * tmp = gethostbyname(hostname);
				if (tmp == NULL) {
					fprintf(stderr, "Can not resolve hostname %s\n", optarg);
					return -1;
				}
				if (tmp->h_addr_list != NULL) {
					rendavous_addr = 
						ntohl(((struct in_addr *)(tmp->h_addr))->s_addr);
					rendavous_port = remotePort; 
				}
			}
			break;
		case 10:
			detector_interval = atoi(optarg);
			break;
		case 11:
			odht_port = atoi(optarg);
			break;
		case '?':
			usage();
			return -1;
		default:
			fprintf(stderr, "Unrecognized character %d returned \n", c);
			break;
		}
	}
	if (merid_port == 0) {
		fprintf(stderr, "Meridian port must be specified\n");
		return -1;
	}
	// Retrieve the URL for the detector
	if (retrieveURL(odht_port, g_url, sizeof(g_url)) == -1) {
		fprintf(stderr, "Cannot retrieve URL for detector\n");
		return -1;
	}
	//	Load seed nodes from param	
	if (optind < argc) {
		while (optind < argc) {		
			static char hostname[HOST_NAME_MAX];
			char* tmpStrPtr = strchr(argv[optind], ':');
			if (tmpStrPtr == NULL) {
				fprintf(stderr, "Invalid parameter, should be hostname:port\n");
				return -1;
			}
			memcpy(hostname, argv[optind], tmpStrPtr - argv[optind]);
			hostname[tmpStrPtr - argv[optind]] = '\0';
			u_short remotePort = (u_short) atoi(tmpStrPtr + 1);
			//	Get the addr of remote host
			struct hostent * tmp = gethostbyname(hostname);
			if (tmp == NULL) {
				fprintf(stderr, "Can not resolve hostname %s\n", argv[optind]);
				return -1;
			}
			if (tmp->h_addr_list != NULL) {			
				param_seeds.push_back(new pair<uint32_t, uint16_t>(
					ntohl(((struct in_addr *)(tmp->h_addr))->s_addr), 
					remotePort));
			}
			optind++;
		}
	}		
	return 0;
}

int main(int argc, char* argv[]) {
	if (handleParams(argc, argv) == -1) {
		return -1;
	}
	//if (daemon(0,0) == -1) {
	//	fprintf(stderr, "Cannot make process into daemon\n");
	//	return -1;	
	//}	
	meridian mInst(merid_port, info_port, 
		nodes_per_primary, nodes_per_second, exponential_base);	
	mInst.setRendavousNode(rendavous_addr, rendavous_port);		
	mInst.setGossipInterval(gossip_init_value, 
		gossip_init_period, gossip_ss_value);
	mInst.setReplaceInterval(replace_period);	
	// Load seeds
	for (u_int i = 0; i < param_seeds.size(); i++) {
		mInst.addSeedNode(param_seeds[i]->first, param_seeds[i]->second); 
	}
	bool ODHTRunning = false;
	while (true) {		
		int curl_res = detectorCurl(g_url);
		if (curl_res == -1 && ODHTRunning) {
			printf("OpenDHT not running, stopping Meridian\n");			
			mInst.stop();
			ODHTRunning = false;
		} else if (curl_res == 0 && !ODHTRunning) {
			printf("OpenDHT (again) running, starting Meridian\n");
			mInst.start();
			ODHTRunning = true;
		}		
		sleep(detector_interval);
	}
	// Cleanup and exit
	for (u_int i = 0; i < param_seeds.size(); i++) {
		delete param_seeds[i];
	}
	param_seeds.clear();
	return 0;
}
