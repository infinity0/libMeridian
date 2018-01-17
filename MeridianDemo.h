#ifndef MERIDIAN_DEMO_COMMON
#define MERIDIAN_DEMO_COMMON

//	Returns a query id based on the address and port
//	of the current node
uint64_t getNewQueryID(int sockFD) {
	char hostname[HOST_NAME_MAX];
	gethostname(hostname, HOST_NAME_MAX);
	hostname[HOST_NAME_MAX - 1] = '\0';
	struct hostent* he = gethostbyname(hostname);
	if (he == NULL) {
		perror("Cannot resolve localhost\n");
		return Packet::to64(rand(), rand());
	}
	u_int localAddr = ntohl(((struct in_addr *)(he->h_addr))->s_addr);
	u_short randVal = rand() % USHRT_MAX;
	struct sockaddr_in addr;
	socklen_t sockSize = sizeof(struct sockaddr_in);
	if (getsockname(sockFD, (struct sockaddr *)&addr, 
		&sockSize) == -1) {
		perror("Getsockname error\n");
		return Packet::to64(rand(), rand());
	}
	//	Concat with port
	u_int secondParam = ntohs(addr.sin_port);
	secondParam = secondParam << 16;
	secondParam |= randVal;
	//	Concat with address		
	return Packet::to64(localAddr, secondParam);
}

//	For parsing command line input
int parseHostAndPort(const char* nodeStr, NodeIdent& remoteNode) {
	char hostname[HOST_NAME_MAX];
	//	Hostname and port separated by ':'
	const char* tmpStrPtr = strchr(nodeStr, ':');
	if (tmpStrPtr == NULL) {
		fprintf(stderr, "Invalid parameter, should be hostname:port\n");
		return -1;
	}
	memcpy(hostname, nodeStr, tmpStrPtr - nodeStr);
	hostname[tmpStrPtr - nodeStr] = '\0';
	remoteNode.port = (u_short) atoi(tmpStrPtr + 1);
	//	Get the add of remote host
	struct hostent * tmp = gethostbyname(hostname);
	if (tmp == NULL || tmp->h_addr_list == NULL) {
		fprintf(stderr, "Can not resolve hostname of meridan node\n");
		return -1;
	}
	remoteNode.addr = 
		ntohl(((struct in_addr *)(tmp->h_addr))->s_addr);
	return 0;
}

#endif
