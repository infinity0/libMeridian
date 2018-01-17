#ifndef CLASS_MARSHAL
#define CLASS_MARSHAL

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <sys/param.h>
#include <vector>
#include <map>
#include <openssl/md5.h>
#include "Common.h"

class RingSet;	// Need ot have forward declaration of ringset for infopacket

uint32_t marshalHashName(char* in_name); 

//#define MAGIC_NUMBER				0x0A0B0C0D
#define STRINGIFY(A) # A
#define INDIRECT(A) STRINGIFY(A)
#define MAGIC_NUMBER				marshalHashName(INDIRECT(APPNAME))
#define MAX_UDP_PACKET_SIZE			1400
#define REQ_CONSTRAINT_N_TCP		1
#define REQ_CLOSEST_N_TCP			2
#define REQ_MEASURE_N_TCP			3
#define REQ_CLOSEST_N_MERID_PING	4
#define REQ_MEASURE_N_MERID_PING	5
#define REQ_CLOSEST_N_DNS			6
#define REQ_MEASURE_N_DNS			7
//#define RET_MEASURE_N				8
#define RET_RESPONSE				9
#define GOSSIP						10
#define PUSH						11
#define PULL						12
#define RET_PING_REQ				13
#define PING						14
#define PONG						15
#define CREATE_RENDV				16
#define RET_RENDV					17
#define RET_ERROR					18
#define RET_INFO					19
#define REQ_CONSTRAINT_N_DNS		20
#define REQ_CONSTRAINT_N_PING		21
#define INFO_PACKET					22
#define GOSSIP_PULL					23

#ifdef MERIDIAN_DSL
#define	DSL_REQUEST					24
#define	DSL_REPLY					25
#endif

#ifdef PLANET_LAB_SUPPORT
#define REQ_MEASURE_N_ICMP			26
#define REQ_CLOSEST_N_ICMP			27
#define REQ_CONSTRAINT_N_ICMP		28
#endif

class BufferWrapper {
private:	
	bool 			errorFlag;
	const char* 	buf;
	int 			bufSize;
	int				counter;
	
public:
	BufferWrapper(const char* in_buf, int in_buf_size) 
		: errorFlag(false), buf(in_buf), bufSize(in_buf_size), counter(0) {}
		
	char retrieve_char() {
		if (!errorFlag && ((counter + (int)sizeof(char)) <= bufSize)) {			
			return buf[counter++];						
		} 			
		errorFlag = true;
		return 0;
	}

	const char* retrieve_buf(int in_buf_size) {
		if (!errorFlag && (in_buf_size >= 0) && 
				((counter + in_buf_size) <= bufSize)) {
			const char* retVal = buf + counter;
			counter += in_buf_size;
			return retVal;
		} 			
		errorFlag = true;
		return 0;
	}
	
	uint16_t retrieve_ushort() {
		if (!errorFlag && ((counter + (int)sizeof(uint16_t)) <= bufSize)) {
			uint16_t retVal;
			memcpy(&retVal, buf + counter, sizeof(uint16_t));
			counter += sizeof(uint16_t);
			return retVal;						
		} 			
		errorFlag = true;
		return 0;		
	}	
	
	uint32_t retrieve_uint() {
		if (!errorFlag && ((counter + (int)sizeof(uint32_t)) <= bufSize)) {
			uint32_t retVal;
			memcpy(&retVal, buf + counter, sizeof(uint32_t));
			counter += sizeof(uint32_t);
			return retVal;						
		} 			
		errorFlag = true;
		return 0;		
	}

	int32_t retrieve_int() {
		if (!errorFlag && ((counter + (int)sizeof(int32_t)) <= bufSize)) {
			int32_t retVal;
			memcpy(&retVal, buf + counter, sizeof(int32_t));
			counter += sizeof(int32_t);
			return retVal;						
		} 			
		errorFlag = true;
		return 0;		
	}
	
	uint32_t remainBufSize() {
		assert(bufSize >= counter);
		return (bufSize - counter);	
	}
	
	uint32_t returnPos() {
		return counter;	
	}
	
	bool error() { 
		return errorFlag;	
	}			
};


typedef struct NodeIdent_t {
	uint32_t	addr;
	uint16_t	port;
} NodeIdent;

//	Used in GOSSIP
typedef struct NodeIdentRendv_t {
	uint32_t	addr;
	uint16_t	port;
	uint32_t	addrRendv;
	uint32_t	portRendv;	
} NodeIdentRendv;

typedef struct NodeIdentConst_t {
	uint32_t	addr;
	uint16_t	port;	
	uint32_t	latencyConstMS;
} NodeIdentConst;

typedef struct NodeIdentLat_t {
	uint32_t	addr;
	uint16_t	port;	
	uint32_t	latencyUS;
} NodeIdentLat;

typedef struct MeasuredResult_t {
	uint32_t	addr;
	uint16_t	port;	
	uint32_t	latencyUS;
} MeasuredResult;


struct ltNodeIdent {
	bool operator()(NodeIdent s1, NodeIdent s2) const {
		if ((s1.addr < s2.addr) || 
				((s1.addr == s2.addr) && (s1.port < s2.port))) {
			return true;	
		}
		return false;
	}
};


struct ltNodeIdentConst {
	bool operator()(NodeIdentConst s1, NodeIdentConst s2) const {
		if ((s1.addr < s2.addr) || 
				((s1.addr == s2.addr) && (s1.port < s2.port))) {
			return true;	
		}
		return false;
	}
};

struct ltNodeIdentRendv {
	bool operator()(NodeIdentRendv s1, NodeIdentRendv s2) const {
		if ((s1.addr < s2.addr) || 
				((s1.addr == s2.addr) && (s1.port < s2.port))) {
			return true;	
		}
		return false;
	}
};

class RealPacket {
private:
	char* 			packet;		// 	Payload
	int				size;		//	Payload size
	bool			complete;	//	Determines rather packet complete
	NodeIdentRendv	dest;
	uint32_t		maxPacketSize;
	int			pos;
	
	bool verifySpace(int typeSize) const {
		if ((uint32_t)(size + typeSize) >  maxPacketSize)
			return false;
		return true;
	}
	
public:
	RealPacket(const NodeIdentRendv& in_dest, uint32_t packetSize) : size(0), 
			complete(true), dest(in_dest), maxPacketSize(packetSize), pos(0) {				
		packet = (char*) malloc(sizeof(char) * maxPacketSize);		
	}

	RealPacket(const NodeIdentRendv& in_dest) : size(0), complete(true), 
			dest(in_dest), maxPacketSize(MAX_UDP_PACKET_SIZE), pos(0) {				
		packet = (char*) malloc(sizeof(char) * maxPacketSize);		
	}
	
	RealPacket(const NodeIdent& in_dest) : size(0), 
			complete(true), maxPacketSize(MAX_UDP_PACKET_SIZE), pos(0) {
		dest.addr = in_dest.addr;
		dest.port = in_dest.port;
		dest.addrRendv = 0;
		dest.portRendv = 0;			 		
		packet = (char*) malloc(sizeof(char) * maxPacketSize);		
	}
	
	RealPacket(const NodeIdent& in_dest, uint32_t packetSize) : size(0), 
			complete(true), maxPacketSize(packetSize), pos(0) {
		dest.addr = in_dest.addr;
		dest.port = in_dest.port;
		dest.addrRendv = 0;
		dest.portRendv = 0;			 		
		packet = (char*) malloc(sizeof(char) * maxPacketSize);		
	}			
	
	~RealPacket() {
		if (packet) free(packet);
	}
	
	int getPos() const			{ return pos;				}	
	void incrPos(int val) 		{ if (val > 0) pos += val;	}
	
	int getPacketSize() const	{ return maxPacketSize;		}
	uint16_t getPort() const 	{ return dest.port; 		}
	uint32_t getAddr() const 	{ return dest.addr; 		}
	
	uint16_t getRendvPort() const 	{ return dest.portRendv; 	}
	uint32_t getRendvAddr() const 	{ return dest.addrRendv; 	}	
		
	char* getPayLoad() const	{ return packet; 			}	
	int getPayLoadSize() const	{ return size;				}
	bool completeOkay() const	{ return complete;			}
	
	void setPayLoadSize(int val)	{
		if (!complete) return;
		if (val < 0 || ((uint32_t)val) > maxPacketSize) {
			complete = false;
		} else {
			size = val;	
		}
	}

	void append_int(int32_t value) {
		if (!complete) return;	// Already failed once
		if ((complete = verifySpace(sizeof(value)))) {
			memcpy(packet + size, &value, sizeof(int32_t));
			size += sizeof(int32_t);
		}
	}
	
	void append_uint(uint32_t value) {
		if (!complete) return;	// Already failed once
		if ((complete = verifySpace(sizeof(value)))) {
			memcpy(packet + size, &value, sizeof(uint32_t));
			size += sizeof(uint32_t);
		}
	}
	
	void append_ushort(uint16_t value) {
		if (!complete) return; // Already failed once
		if ((complete = verifySpace(sizeof(value)))) {	
			memcpy(packet + size, &value, sizeof(uint16_t));
			size += sizeof(uint16_t);			
		}
	}
	
	void append_char(char value) {
		if (!complete) return; // Already failed once
		if ((complete = verifySpace(sizeof(value)))) {	
			packet[size] = value;
			size += sizeof(char);
		}
	}

	void append_str(const char* in_str, int in_size) {
		if (!complete || in_size <= 0) return; 
		if ((complete = verifySpace(in_size))) {
			memcpy(packet + size, in_str, in_size);
			size += in_size;
		}
	}
	
	void append_packet(const RealPacket& in_packet) {
		int in_size = in_packet.getPayLoadSize();
		if (!complete || in_size <= 0) return;
		if ((complete = verifySpace(in_size))) {
			memcpy(packet + size, in_packet.getPayLoad(), in_size);
			size += in_size;
		}		
	}
	
};

class Packet {
private:
	uint32_t 	req_id_1;
	uint32_t 	req_id_2;
public:
	static uint64_t to64(uint32_t id_1, uint32_t id_2) {
		uint64_t tmp = id_1;
		tmp = tmp << 32;
		tmp |= id_2;
		return tmp;		
	}
	
	static void to32(uint64_t id, uint32_t* id_1, uint32_t* id_2) {
		*id_1 = id >> 32;
		*id_2 = id & 0xFFFFFFFF;			
	}
	// Explicitly specify firewall
	Packet(uint64_t id)  {
		to32(id, &req_id_1, &req_id_2); 
	}
	//	Return request id
	uint64_t retReqID() const 	{
		return to64(req_id_1, req_id_2);
	}
	//	Append id to RealPacket
	void write_id(RealPacket& inPacket) const {
		inPacket.append_uint(htonl(req_id_1));
		inPacket.append_uint(htonl(req_id_2));
		inPacket.append_uint(htonl(MAGIC_NUMBER));
	}
	
	static int parseHeader(
			BufferWrapper& rb, char* queryType, uint64_t* queryID) {		
		*queryType = rb.retrieve_char();
		uint32_t queryID_1 = ntohl(rb.retrieve_uint());
		uint32_t queryID_2 = ntohl(rb.retrieve_uint());
		*queryID = to64(queryID_1, queryID_2);	
		uint32_t magicNumber = ntohl(rb.retrieve_uint());		
		if (rb.error()) {
			return -1;			
		}
		if (magicNumber != MAGIC_NUMBER) {
			return -1;	// Cannot be correct packet	
		}
		return 0;	// Normal packet 
	}	
	
	virtual int createRealPacket(RealPacket& inPacket) const = 0;
	virtual char getPacketType() const = 0;			
	virtual ~Packet() {}
};

class RendvHeaderPacket : public Packet {
private:
	uint32_t	rendv_addr;
	uint16_t	rendv_port;
public:
	// Explicitly specify firewall
	RendvHeaderPacket(uint64_t id, uint32_t in_rendv_addr, uint16_t in_rendv_port) 
		: Packet(id), rendv_addr(in_rendv_addr), rendv_port(in_rendv_port) {}		
	//	Get rendavous address and port
	uint32_t getRendvAddr() const		{ return rendv_addr; }
	uint16_t getRendvPort() const	{ return rendv_port; }	
	//	Write header into RealPacket
	void write_rendv(RealPacket& inPacket) const {
		inPacket.append_uint(htonl(rendv_addr));
		inPacket.append_ushort(htons(rendv_port));
	}
	virtual ~RendvHeaderPacket() {}
};


class ReqGeneric : public RendvHeaderPacket {
protected:
	vector<NodeIdent> targets;		
public:	
	ReqGeneric(uint64_t id, uint32_t in_rendv_addr, uint16_t in_rendv_port)  
		: 	RendvHeaderPacket(id, in_rendv_addr, in_rendv_port) {}		

	template <class T>
	static ReqGeneric* parse(const char* buf, int numBytes) {
		BufferWrapper rb(buf, numBytes);
		char queryType = rb.retrieve_char();
		if (rb.error() || queryType != T::type()) {
			ERROR_LOG("Wrong type received\n");
			return NULL;	
		}
		uint32_t queryID_1 = ntohl(rb.retrieve_uint());
		uint32_t queryID_2 = ntohl(rb.retrieve_uint());
		uint64_t queryID = to64(queryID_1, queryID_2);
		uint32_t magicNumber = ntohl(rb.retrieve_uint());		
		uint32_t rendvAddr = ntohl(rb.retrieve_uint());
		uint16_t rendvPort = ntohs(rb.retrieve_ushort());		
		if (rb.error() || magicNumber != MAGIC_NUMBER) {
			ERROR_LOG("Wrong magic number in packet received\n");
			return NULL;			
		}		
		ReqGeneric* ret = new T(queryID, rendvAddr, rendvPort);			
		uint32_t numEntry = ntohl(rb.retrieve_uint());
		NodeIdent tmpIdent;		
		//while (!rb.error() && numEntry-- > 0) {
		for (uint32_t i = 0; (!rb.error() && i < numEntry); i++) { 			
			tmpIdent.addr = ntohl(rb.retrieve_uint());
			tmpIdent.port = ntohs(rb.retrieve_ushort());
			ret->addTarget(tmpIdent);
		}
		if (rb.error()) {
			delete ret;
			return NULL;
		}		
		return ret; 		
	}
	
	virtual int createRealPacket(RealPacket& inPacket) const {
		uint32_t num_targets = targets.size();
		if (num_targets == 0) {
			return -1;
		}
		inPacket.append_char(getPacketType());
		write_id(inPacket);		
		write_rendv(inPacket);
		inPacket.append_uint(htonl(num_targets));
		for (uint32_t i = 0; i < num_targets; i++) {
			NodeIdent tmp = targets[i];
			inPacket.append_uint(htonl(tmp.addr));
			inPacket.append_ushort(htons(tmp.port));			
		}					
		if (!inPacket.completeOkay()) { 
			return -1; 
		}
		return 0;
	}
	
	void addTarget(const NodeIdent& in_node) {
		targets.push_back(in_node);
	}
	
	const vector<NodeIdent>* returnTargets() {
		return &targets;	
	}
	
	virtual char getPacketType() const = 0;
	virtual ~ReqGeneric() {}			
};


//	Destination cannot be behind firewall
class ReqMeasurePing : public ReqGeneric {
public:
	ReqMeasurePing(uint64_t id, uint32_t in_rendv_addr, uint16_t in_rendv_port)  
		: 	ReqGeneric(id, in_rendv_addr, in_rendv_port) {}
		
	template <class T>
	static ReqGeneric* parse(const char* buf, int numBytes) {
		assert(false);
		return NULL;
	}
		
	static char type() { return REQ_MEASURE_N_MERID_PING; }
	virtual char getPacketType() const	{ return type(); }
	virtual ~ReqMeasurePing() {}			
};


//	Destination cannot be behind firewall
class ReqMeasureTCP : public ReqGeneric {
public:
	ReqMeasureTCP(uint64_t id, uint32_t in_rendv_addr, uint16_t in_rendv_port)  
		: 	ReqGeneric(id, in_rendv_addr, in_rendv_port) {}
		
	template <class T>
	static ReqGeneric* parse(const char* buf, int numBytes) {
		assert(false);
		return NULL;
	}		
	
	static char type() { return REQ_MEASURE_N_TCP; }
	virtual char getPacketType() const	{ return type(); }
	virtual ~ReqMeasureTCP() {}	
};


//	Destination cannot be behind firewall
class ReqMeasureDNS : public ReqGeneric {
public:
	ReqMeasureDNS(uint64_t id, uint32_t in_rendv_addr, uint16_t in_rendv_port)  
		: 	ReqGeneric(id, in_rendv_addr, in_rendv_port) {}
		
	template <class T>
	static ReqGeneric* parse(const char* buf, int numBytes) {
		assert(false);
		return NULL;
	}	
	
	static char type() { return REQ_MEASURE_N_DNS; }
	virtual char getPacketType() const	{ return type(); }
	virtual ~ReqMeasureDNS() {}			
};

#ifdef PLANET_LAB_SUPPORT
//	Destination cannot be behind firewall
class ReqMeasureICMP : public ReqGeneric {
public:
	ReqMeasureICMP(uint64_t id, uint32_t in_rendv_addr, uint16_t in_rendv_port)  
		: 	ReqGeneric(id, in_rendv_addr, in_rendv_port) {}
		
	template <class T>
	static ReqGeneric* parse(const char* buf, int numBytes) {
		assert(false);
		return NULL;
	}	
	
	static char type() { return REQ_MEASURE_N_ICMP; }
	virtual char getPacketType() const	{ return type(); }
	virtual ~ReqMeasureICMP() {}			
};
#endif



class ReqConstraintGeneric : public RendvHeaderPacket {
protected:
	uint16_t betaNum;
	uint16_t betaDen;
	vector<NodeIdentConst> targets;		
public:	
	ReqConstraintGeneric(uint64_t id, uint16_t in_beta_num, uint16_t in_beta_den, 
			uint32_t in_rendv_addr, uint16_t in_rendv_port)  
		: 	RendvHeaderPacket(id, in_rendv_addr, in_rendv_port), 
			betaNum(in_beta_num), betaDen(in_beta_den) {}		

	template <class T>
	static ReqConstraintGeneric* parse(const char* buf, int numBytes) {
		BufferWrapper rb(buf, numBytes);
		char queryType = rb.retrieve_char();
		if (rb.error() || queryType != T::type()) {
			ERROR_LOG("Wrong type received\n");
			return NULL;	
		}
		uint32_t queryID_1 = ntohl(rb.retrieve_uint());
		uint32_t queryID_2 = ntohl(rb.retrieve_uint());
		uint64_t queryID = to64(queryID_1, queryID_2);
		uint32_t magicNumber = ntohl(rb.retrieve_uint());		
		uint32_t rendvAddr = ntohl(rb.retrieve_uint());
		uint16_t rendvPort = ntohs(rb.retrieve_ushort());
		uint16_t in_betaNum = ntohs(rb.retrieve_ushort());
		uint16_t in_betaDen = ntohs(rb.retrieve_ushort());
		if (rb.error() || magicNumber != MAGIC_NUMBER) {
			ERROR_LOG("Wrong magic number in packet received\n");
			return NULL;			
		}		
		ReqConstraintGeneric* ret 
			= new T(queryID, in_betaNum, in_betaDen, rendvAddr, rendvPort);			
		uint32_t numEntry = ntohl(rb.retrieve_uint());
		NodeIdentConst tmpIdent;		
		//while (!rb.error() && numEntry-- > 0) {
		for (uint32_t i = 0; (!rb.error() && i < numEntry); i++) {			
			tmpIdent.addr = ntohl(rb.retrieve_uint());
			tmpIdent.port = ntohs(rb.retrieve_ushort());
			tmpIdent.latencyConstMS = ntohl(rb.retrieve_uint());	
			ret->addTarget(tmpIdent);
		}
		if (rb.error()) {
			delete ret;
			return NULL;
		}		
		return ret; 		
	}
	
	virtual int createRealPacket(RealPacket& inPacket) const {
		uint32_t num_targets = targets.size();
		if (num_targets == 0) {
			return -1;
		}
		inPacket.append_char(getPacketType());
		write_id(inPacket);		
		write_rendv(inPacket);
		inPacket.append_ushort(htons(betaNum));
		inPacket.append_ushort(htons(betaDen));
		inPacket.append_uint(htonl(num_targets));
		for (uint32_t i = 0; i < num_targets; i++) {
			NodeIdentConst tmp = targets[i];
			inPacket.append_uint(htonl(tmp.addr));
			inPacket.append_ushort(htons(tmp.port));
			inPacket.append_uint(htonl(tmp.latencyConstMS));
		}					
		if (!inPacket.completeOkay()) { 
			return -1; 
		}
		return 0;
	}
	
	uint16_t getBetaNumerator(){
		return betaNum;
	}
	
	uint16_t getBetaDenominator() {
		return betaDen;
	}
	
	void addTarget(const NodeIdentConst& in_node) {
		targets.push_back(in_node);
	}
	
	const vector<NodeIdentConst>* returnTargets() {
		return &targets;	
	}
	
	virtual char getPacketType() const = 0;
	virtual ~ReqConstraintGeneric() {}			
};

class ReqConstraintTCP : public ReqConstraintGeneric { 
public:
	ReqConstraintTCP(uint64_t id, uint16_t in_beta_num, uint16_t in_beta_den, 
			uint32_t in_rendv_addr, uint16_t in_rendv_port)  
		: ReqConstraintGeneric(id, in_beta_num, in_beta_den, 
			in_rendv_addr, in_rendv_port) {}

	template <class T>
	static ReqConstraintGeneric* parse(const char* buf, int numBytes) {
		assert(false);
		return NULL;
	}	
	
	static char type() { return REQ_CONSTRAINT_N_TCP; }
	virtual char getPacketType() const	{ return type(); }		
	virtual ~ReqConstraintTCP() {}		
};

class ReqConstraintDNS : public ReqConstraintGeneric { 
public:
	ReqConstraintDNS(uint64_t id, uint16_t in_beta_num, uint16_t in_beta_den, 
			uint32_t in_rendv_addr, uint16_t in_rendv_port)  
		: ReqConstraintGeneric(id, in_beta_num, in_beta_den, 
			in_rendv_addr, in_rendv_port) {}

	template <class T>
	static ReqConstraintGeneric* parse(const char* buf, int numBytes) {
		assert(false);
		return NULL;
	}	
	
	static char type() { return REQ_CONSTRAINT_N_DNS; }
	virtual char getPacketType() const	{ return type(); }			
	virtual ~ReqConstraintDNS() {}		
};

class ReqConstraintPing : public ReqConstraintGeneric { 
public:
	ReqConstraintPing(uint64_t id, uint16_t in_beta_num, uint16_t in_beta_den, 
			uint32_t in_rendv_addr, uint16_t in_rendv_port)  
		: ReqConstraintGeneric(id, in_beta_num, in_beta_den, 
			in_rendv_addr, in_rendv_port) {}

	template <class T>
	static ReqConstraintGeneric* parse(const char* buf, int numBytes) {
		assert(false);
		return NULL;
	}	
	
	static char type() { return REQ_CONSTRAINT_N_PING; }
	virtual char getPacketType() const	{ return type(); }			
	virtual ~ReqConstraintPing() {}		
};

#ifdef PLANET_LAB_SUPPORT
class ReqConstraintICMP : public ReqConstraintGeneric { 
public:
	ReqConstraintICMP(uint64_t id, uint16_t in_beta_num, uint16_t in_beta_den, 
			uint32_t in_rendv_addr, uint16_t in_rendv_port)  
		: ReqConstraintGeneric(id, in_beta_num, in_beta_den, 
			in_rendv_addr, in_rendv_port) {}

	template <class T>
	static ReqConstraintGeneric* parse(const char* buf, int numBytes) {
		assert(false);
		return NULL;
	}	
	
	static char type() { return REQ_CONSTRAINT_N_ICMP; }
	virtual char getPacketType() const	{ return type(); }			
	virtual ~ReqConstraintICMP() {}		
};
#endif



/*
class ReqConstraintTCP : public RendvHeaderPacket {
protected:
	vector<NodeIdentConst> targets;
public:
	ReqConstraintTCP(uint64_t id, uint32_t in_rendv_addr, uint16_t in_rendv_port) 
		: RendvHeaderPacket(id, in_rendv_addr, in_rendv_port) {}
		
	virtual int createRealPacket(RealPacket& inPacket) const {
		uint32_t num_targets = targets.size();
		//	Must have at least one packet
		if (num_targets == 0) { 
			return -1; 
		}
		inPacket.append_char(getPacketType());
		write_id(inPacket);		
		write_rendv(inPacket);
		inPacket.append_uint(htonl(num_targets));
		for (uint32_t i = 0; i < num_targets; i++) {
			NodeIdentConst tmp = targets[i];
			inPacket.append_uint(htonl(tmp.addr));
			inPacket.append_ushort(htons(tmp.port));
			inPacket.append_uint(htonl(tmp.latencyConstMS));
		}
		if (!inPacket.completeOkay()) { 
			return -1; 
		}
		return 0; // Packet completed correctly
	}		
	virtual char getPacketType() const 	{ return REQ_CONSTRAINT_N_TCP; }				
	//	Add TCP server targets		
	void addTarget(uint32_t addr, uint16_t port, uint32_t latencyMS) {
		NodeIdentConst tmp = {addr, port, latencyMS};
		targets.push_back(tmp);
	}
	virtual ~ReqConstraintTCP() {}
};
*/


class ReqClosestGeneric : public RendvHeaderPacket {
protected:
	uint16_t betaNum;
	uint16_t betaDen;
	vector<NodeIdent> targets;		
public:	
	ReqClosestGeneric(uint64_t id, uint16_t in_beta_num, uint16_t in_beta_den, 
			uint32_t in_rendv_addr, uint16_t in_rendv_port)  
		: 	RendvHeaderPacket(id, in_rendv_addr, in_rendv_port), 
			betaNum(in_beta_num), betaDen(in_beta_den) {}		

	template <class T>
	static ReqClosestGeneric* parse(const char* buf, int numBytes) {
		BufferWrapper rb(buf, numBytes);
		char queryType = rb.retrieve_char();
		if (rb.error() || queryType != T::type()) {
			ERROR_LOG("Wrong type received\n");
			return NULL;	
		}
		uint32_t queryID_1 = ntohl(rb.retrieve_uint());
		uint32_t queryID_2 = ntohl(rb.retrieve_uint());
		uint64_t queryID = to64(queryID_1, queryID_2);
		uint32_t magicNumber = ntohl(rb.retrieve_uint());		
		uint32_t rendvAddr = ntohl(rb.retrieve_uint());
		uint16_t rendvPort = ntohs(rb.retrieve_ushort());
		uint16_t in_betaNum = ntohs(rb.retrieve_ushort());
		uint16_t in_betaDen = ntohs(rb.retrieve_ushort());
		if (rb.error() || magicNumber != MAGIC_NUMBER) {
			ERROR_LOG("Wrong magic number in packet received\n");
			return NULL;			
		}		
		ReqClosestGeneric* ret 
			= new T(queryID, in_betaNum, in_betaDen, rendvAddr, rendvPort);			
		uint32_t numEntry = ntohl(rb.retrieve_uint());
		NodeIdent tmpIdent;		
		//while (!rb.error() && numEntry-- > 0) {
		for (uint32_t i = 0; (!rb.error() && i < numEntry); i++) {			
			tmpIdent.addr = ntohl(rb.retrieve_uint());
			tmpIdent.port = ntohs(rb.retrieve_ushort());
			ret->addTarget(tmpIdent);
		}
		if (rb.error()) {
			delete ret;
			return NULL;
		}		
		return ret; 		
	}
	
	virtual int createRealPacket(RealPacket& inPacket) const {
		uint32_t num_targets = targets.size();
		if (num_targets == 0) {
			return -1;
		}
		inPacket.append_char(getPacketType());
		write_id(inPacket);		
		write_rendv(inPacket);
		inPacket.append_ushort(htons(betaNum));
		inPacket.append_ushort(htons(betaDen));
		inPacket.append_uint(htonl(num_targets));
		for (uint32_t i = 0; i < num_targets; i++) {
			NodeIdent tmp = targets[i];
			inPacket.append_uint(htonl(tmp.addr));
			inPacket.append_ushort(htons(tmp.port));			
		}					
		if (!inPacket.completeOkay()) { 
			return -1; 
		}
		return 0;
	}
	
	uint16_t getBetaNumerator(){
		return betaNum;
	}
	
	uint16_t getBetaDenominator() {
		return betaDen;
	}
	
	void addTarget(const NodeIdent& in_node) {
		targets.push_back(in_node);
	}
	
	const vector<NodeIdent>* returnTargets() {
		return &targets;	
	}
	
	virtual char getPacketType() const = 0;
	virtual ~ReqClosestGeneric() {}			
};

class ReqClosestTCP : public ReqClosestGeneric { 
public:
	ReqClosestTCP(uint64_t id, uint16_t in_beta_num, uint16_t in_beta_den, 
			uint32_t in_rendv_addr, uint16_t in_rendv_port)  
		: ReqClosestGeneric(id, in_beta_num, in_beta_den, 
			in_rendv_addr, in_rendv_port) {}

	template <class T>
	static ReqClosestGeneric* parse(const char* buf, int numBytes) {
		assert(false);
		return NULL;
	}	
	
	static char type() { return REQ_CLOSEST_N_TCP; }
	virtual char getPacketType() const	{ return type(); }			
	virtual ~ReqClosestTCP() {}		
};

class ReqClosestMeridPing : public ReqClosestGeneric { 
public:
	ReqClosestMeridPing(uint64_t id, uint16_t in_beta_num, uint16_t in_beta_den, 
			uint32_t in_rendv_addr, uint16_t in_rendv_port)  
		: ReqClosestGeneric(id, in_beta_num, in_beta_den, 
			in_rendv_addr, in_rendv_port) {} 

	template <class T>
	static ReqClosestGeneric* parse(const char* buf, int numBytes) {
		assert(false);
		return NULL;
	}	
	
	static char type() { return REQ_CLOSEST_N_MERID_PING; }
	virtual char getPacketType() const	{ return type(); }						
	virtual ~ReqClosestMeridPing() {}		
};

class ReqClosestDNS : public ReqClosestGeneric {
public:
	ReqClosestDNS(uint64_t id, uint16_t in_beta_num, uint16_t in_beta_den, 
			uint32_t in_rendv_addr, uint16_t in_rendv_port)  
		: ReqClosestGeneric(id, in_beta_num, in_beta_den, 
			in_rendv_addr, in_rendv_port) {}
		
	template <class T>
	static ReqClosestGeneric* parse(const char* buf, int numBytes) {
		assert(false);
		return NULL;
	}	
	
	static char type() { return REQ_CLOSEST_N_DNS; }
	virtual char getPacketType() const	{ return type(); }					
	virtual ~ReqClosestDNS() {}				
};

#ifdef PLANET_LAB_SUPPORT
class ReqClosestICMP : public ReqClosestGeneric { 
public:
	ReqClosestICMP(uint64_t id, uint16_t in_beta_num, uint16_t in_beta_den, 
			uint32_t in_rendv_addr, uint16_t in_rendv_port)  
		: ReqClosestGeneric(id, in_beta_num, in_beta_den, 
			in_rendv_addr, in_rendv_port) {} 

	template <class T>
	static ReqClosestGeneric* parse(const char* buf, int numBytes) {
		assert(false);
		return NULL;
	}	
	
	static char type() { return REQ_CLOSEST_N_ICMP; }
	virtual char getPacketType() const	{ return type(); }					
	virtual ~ReqClosestICMP() {}		
};
#endif

class RetError : public Packet {
public:
	RetError(uint64_t id) : Packet(id) {}
	
	static RetError* parse(const char* buf, int numBytes) {
		BufferWrapper rb(buf, numBytes);
		char queryType = rb.retrieve_char();
		if (rb.error() || queryType != RET_ERROR) {
			ERROR_LOG("Wrong type received\n");
			return NULL;	
		}
		uint32_t queryID_1 = ntohl(rb.retrieve_uint());
		uint32_t queryID_2 = ntohl(rb.retrieve_uint());
		uint64_t queryID = to64(queryID_1, queryID_2);
		uint32_t magicNumber = ntohl(rb.retrieve_uint());
		if (rb.error() || magicNumber != MAGIC_NUMBER) {
			ERROR_LOG("Wrong magic number in packet received\n");
			return NULL;			
		}
		return new RetError(queryID); 		
	}
	
	virtual int createRealPacket(RealPacket& inPacket) const {
		inPacket.append_char(getPacketType());
		write_id(inPacket);				
		if (!inPacket.completeOkay()) { 
			return -1; 
		}
		return 0; // Packet completed correctly		
	}		
	virtual char getPacketType() const	{ return RET_ERROR; }
	virtual ~RetError() {}		
};


class RetInfo : public Packet {
private:
	uint32_t 	addr;
	uint16_t port;
public:
	RetInfo(uint64_t id, uint32_t in_addr, uint16_t in_port) 
		: Packet(id), addr(in_addr), port(in_port) {}
	
	static RetInfo* parse(
			const NodeIdent& in_remoteNode, const char* buf, int numBytes) {
		BufferWrapper rb(buf, numBytes);
		char queryType = rb.retrieve_char();
		if (rb.error() || queryType != RET_INFO) {
			ERROR_LOG("Wrong type received\n");
			return NULL;	
		}
		uint32_t queryID_1 = ntohl(rb.retrieve_uint());
		uint32_t queryID_2 = ntohl(rb.retrieve_uint());
		uint64_t queryID = to64(queryID_1, queryID_2);
		uint32_t magicNumber = ntohl(rb.retrieve_uint());
		uint32_t in_addr = ntohl(rb.retrieve_uint());
		uint16_t in_port = ntohs(rb.retrieve_ushort());		
		if (rb.error() || magicNumber != MAGIC_NUMBER) {
			ERROR_LOG("Wrong magic number in packet received\n");
			return NULL;			
		}
		if ((in_addr == 0) && (in_port == 0)) {
			in_addr = in_remoteNode.addr;
			in_port = in_remoteNode.port;
		}
		return (new RetInfo(queryID, in_addr, in_port)); 		
	}
	
	NodeIdent getInfoNode() {
		NodeIdent tmp = {addr, port};
		return tmp;
	}
	
	virtual int createRealPacket(RealPacket& inPacket) const {
		inPacket.append_char(getPacketType());
		write_id(inPacket);
		inPacket.append_uint(htonl(addr));
		inPacket.append_ushort(htons(port));		
		if (!inPacket.completeOkay()) { 
			return -1; 
		}
		return 0; // Packet completed correctly		
	}		
	virtual char getPacketType() const	{ return RET_INFO; }
	virtual ~RetInfo() {}		
};



class RetResponse : public Packet {
private:
	uint32_t 					addr;	// Address of solution node
	uint16_t 				port;	// Port of solution node
	vector<NodeIdentLat>	targets;
public:
	RetResponse(uint64_t id, uint32_t in_addr, uint16_t in_port,
		const map<NodeIdent, uint32_t, ltNodeIdent>& in_targets) 
			: Packet(id), addr(in_addr), port(in_port) {
		map<NodeIdent, uint32_t, ltNodeIdent>::const_iterator it 
			= in_targets.begin();
		for (; it != in_targets.end(); it++) {			
			NodeIdentLat tmpIdent 
				= {(it->first).addr, (it->first).port, it->second};
			targets.push_back(tmpIdent);
		}
	}
	
	NodeIdent getResponse() {
		NodeIdent tmp = {addr, port};
		return tmp;
	}
	
	const vector<NodeIdentLat>* getTargets() {
		return &targets;	
	}
	
	static RetResponse* parse(
			const NodeIdent& in_remote, const char* buf, int numBytes) {
		BufferWrapper rb(buf, numBytes);
		char queryType = rb.retrieve_char();
		if (rb.error() || queryType != RET_RESPONSE) {
			ERROR_LOG("Wrong type received\n");
			return NULL;	
		}
		uint32_t queryID_1 = ntohl(rb.retrieve_uint());
		uint32_t queryID_2 = ntohl(rb.retrieve_uint());
		uint64_t queryID = to64(queryID_1, queryID_2);
		uint32_t magicNumber = ntohl(rb.retrieve_uint());
		if (rb.error() || magicNumber != MAGIC_NUMBER) {
			ERROR_LOG("Wrong magic number in packet received\n");
			return NULL;			
		}		
		uint32_t closestAddr = ntohl(rb.retrieve_uint());
		uint16_t closestPort = ntohs(rb.retrieve_ushort());
		if ((closestAddr == 0) && (closestPort == 0)) {
			closestAddr = in_remote.addr;
			closestPort = in_remote.port;
		}
		uint32_t numEntry = ntohl(rb.retrieve_uint());
		map<NodeIdent, uint32_t, ltNodeIdent> tmpMap;				
		NodeIdent tmpIdent;		
		//while (!rb.error() && numEntry-- > 0) {
		for (uint32_t i = 0; (!rb.error() && i < numEntry); i++) {			
			tmpIdent.addr = ntohl(rb.retrieve_uint());
			tmpIdent.port = ntohs(rb.retrieve_ushort());
			uint32_t latencyUS = ntohl(rb.retrieve_uint());
			tmpMap[tmpIdent] = latencyUS;
		}
		if (rb.error()) {
			return NULL;
		}
		RetResponse* ret 
			= new RetResponse(queryID, closestAddr, closestPort, tmpMap);
		return ret; 		
	}	
	
	virtual int createRealPacket(RealPacket& inPacket) const {
		inPacket.append_char(getPacketType());
		write_id(inPacket);
		inPacket.append_uint(htonl(addr));
		inPacket.append_ushort(htons(port));		
		inPacket.append_uint(htonl(targets.size()));
		for (uint32_t i = 0; i < targets.size(); i++) {
			NodeIdentLat tmpIdent = targets[i];
			inPacket.append_uint(htonl(tmpIdent.addr));
			inPacket.append_ushort(htons(tmpIdent.port));
			inPacket.append_uint(htonl(tmpIdent.latencyUS));
		}
		//inPacket.append_uint(htonl(addr));
		//inPacket.append_ushort(htons(port));
		if (!inPacket.completeOkay()) { 
			return -1; 
		}
		return 0; // Packet completed correctly		
	}	
	virtual char getPacketType() const	{ return RET_RESPONSE; }
	virtual ~RetResponse() {}	
};


class GossipPacketGeneric : public RendvHeaderPacket {
protected:
	vector<NodeIdentRendv> targets;	
public:
	GossipPacketGeneric(uint64_t id, uint32_t in_rendv_addr, 
			uint16_t in_rendv_port) 
		: RendvHeaderPacket(id, in_rendv_addr, in_rendv_port) {} 					
	
	template <class T>
	static GossipPacketGeneric* parse(const char* buf, int numBytes) {
		BufferWrapper rb(buf, numBytes);
		char queryType = rb.retrieve_char();
		if (rb.error() || queryType != T::type()) {
			return NULL;	
		}
		uint32_t queryID_1 = ntohl(rb.retrieve_uint());
		uint32_t queryID_2 = ntohl(rb.retrieve_uint());
		uint64_t queryID = to64(queryID_1, queryID_2);
		uint32_t magicNumber = ntohl(rb.retrieve_uint());		
		uint32_t outerRAddr = ntohl(rb.retrieve_uint());
		uint16_t outerRPort = ntohs(rb.retrieve_ushort());		
		if (rb.error() || magicNumber != MAGIC_NUMBER) {
			return NULL;			
		}		
		GossipPacketGeneric* ret = new T(queryID, outerRAddr, outerRPort);
		uint32_t numEntry = ntohl(rb.retrieve_uint());		
		for (uint32_t i = 0; (!rb.error() && i < numEntry); i++) {			
			uint32_t addr = ntohl(rb.retrieve_uint());
			uint16_t port = ntohs(rb.retrieve_ushort());
			uint32_t rAddr = ntohl(rb.retrieve_uint());
			uint16_t rPort = ntohs(rb.retrieve_ushort());
			ret->addNode(addr, port, rAddr, rPort);
		}
		if (rb.error()) {
			delete ret;
			return NULL;
		}		
		return ret; 		
	}
	
	const vector<NodeIdentRendv>* returnTargets() {
		return &targets;	
	}
	
	virtual int createRealPacket(RealPacket& inPacket) const {
		uint32_t num_targets = targets.size();
		//	Must have at least one packet
		inPacket.append_char(getPacketType());
		write_id(inPacket);	
		write_rendv(inPacket);
		inPacket.append_uint(htonl(num_targets));
		for (uint32_t i = 0; i < num_targets; i++) {
			NodeIdentRendv tmp = targets[i];
			inPacket.append_uint(htonl(tmp.addr));
			inPacket.append_ushort(htons(tmp.port));
			inPacket.append_uint(htonl(tmp.addrRendv));
			inPacket.append_ushort(htons(tmp.portRendv));			
		}
		if (!inPacket.completeOkay()) { 
			return -1; 
		}
		return 0; // Packet completed correctly		
	}
		
	virtual char getPacketType() const = 0;
	
	void addNode(uint32_t addr, uint16_t port, 
			uint32_t rendv_addr, uint16_t rendv_port) {
		NodeIdentRendv tmp = {addr, port, rendv_addr, rendv_port};
		targets.push_back(tmp);
	}
	virtual ~GossipPacketGeneric() {}	
};

class GossipPacketPush : public GossipPacketGeneric {
public:
	GossipPacketPush(uint64_t id, uint32_t in_rendv_addr, 
			uint16_t in_rendv_port) 
		: GossipPacketGeneric(id, in_rendv_addr, in_rendv_port) {}		
	virtual ~GossipPacketPush() {}	
	
	template <class T>
	static GossipPacketGeneric* parse(const char* buf, int numBytes) {
		assert(false);
		return NULL;
	}
	
	static char type() { return GOSSIP; }
	virtual char getPacketType() const { return type(); }	
};

class GossipPacketPull : public GossipPacketGeneric {
public:
	GossipPacketPull(uint64_t id, uint32_t in_rendv_addr, 
			uint16_t in_rendv_port) 
		: GossipPacketGeneric(id, in_rendv_addr, in_rendv_port) {}		
	virtual ~GossipPacketPull() {}

	template <class T>
	static GossipPacketGeneric* parse(const char* buf, int numBytes) {
		assert(false);
		return NULL;
	}	
	
	static char type() { return GOSSIP_PULL; }
	virtual char getPacketType() const { return type(); }	
};


class PullPacket : public Packet {
protected:
	uint32_t	srcIP;
	uint16_t	srcPort;
	uint32_t	payLoadSize;
public:
	PullPacket(uint64_t id, uint32_t in_src_ip, 
		uint16_t in_src_port, uint32_t in_payLoadSize) 		
		: 	Packet(id), srcIP(in_src_ip), srcPort(in_src_port),  
			payLoadSize(in_payLoadSize)	{}
			
	virtual int createRealPacket(RealPacket& inPacket) const {
		inPacket.append_char(getPacketType());
		write_id(inPacket);						
		inPacket.append_uint(htonl(srcIP));
		inPacket.append_ushort(htons(srcPort));
		inPacket.append_uint(htonl(payLoadSize));		
		if (!inPacket.completeOkay()) { 
			return -1; 
		}
		return 0;
	}
	
	static RealPacket* parse(RealPacket& inPacket, NodeIdent& srcNode) {				
		BufferWrapper rb(inPacket.getPayLoad(), inPacket.getPayLoadSize());
		char queryType = rb.retrieve_char();
		if (rb.error() || queryType != PULL) {
			return NULL;	
		}
		rb.retrieve_uint();	// Skipping queryid_1
		rb.retrieve_uint();	// Skipping queryid_2		
		uint32_t magicNumber = ntohl(rb.retrieve_uint());
		uint32_t in_srcIP = ntohl(rb.retrieve_uint());
		uint16_t in_srcPort = ntohs(rb.retrieve_ushort());
		uint32_t in_payLoadSize = ntohl(rb.retrieve_uint()); 		
		if (rb.error() || magicNumber != MAGIC_NUMBER) {
			return NULL;			
		}
		if (rb.remainBufSize() < in_payLoadSize) {
			return NULL;
		}
		// Destination doesn't matter, since it is not actually being sent
		NodeIdent dummy = {0, 0};										
		RealPacket* newPacket = new RealPacket(dummy, in_payLoadSize);
		if (newPacket != NULL) {
			srcNode.addr = in_srcIP;
			srcNode.port = in_srcPort;			
			memcpy(newPacket->getPayLoad(), inPacket.getPayLoad() + 
				rb.returnPos(), in_payLoadSize);
			newPacket->setPayLoadSize(in_payLoadSize);
		}
		//	Regardless of whether the "new RealPacket" succeeded or not,
		//	return newPacket
		if (rb.remainBufSize() > in_payLoadSize) {
			//printf("Must perform memmove\n");
			//	Move end parts of the buffer into beginning part
			//	Must use memmove, the buffer might overlap
			memmove(inPacket.getPayLoad(), inPacket.getPayLoad() + 
				rb.returnPos() + in_payLoadSize,  
				inPacket.getPayLoadSize() - (rb.returnPos() + in_payLoadSize));				
			inPacket.setPayLoadSize(
				inPacket.getPayLoadSize() - (rb.returnPos() + in_payLoadSize));
		} else {
			inPacket.setPayLoadSize(0);	
		}
		return newPacket;
	}	
	
	virtual char getPacketType() const	{ return PULL; }
	virtual ~PullPacket() {}			
};

class PushPacket : public Packet {
protected:
	uint32_t 	destIP;
	uint16_t	destPort;
public:
	PushPacket(uint64_t id, uint32_t in_dest_ip, uint16_t in_dest_port) 
		: Packet(id), destIP(in_dest_ip), destPort(in_dest_port) {}
	virtual int createRealPacket(RealPacket& inPacket) const {
		inPacket.append_char(getPacketType());
		write_id(inPacket);				
		inPacket.append_uint(htonl(destIP));
		inPacket.append_ushort(htons(destPort));				
		if (!inPacket.completeOkay()) { 
			return -1; 
		}
		return 0;
	}
	
	static RealPacket* parse(
			const NodeIdent& remoteNode, const char* buf, int numBytes) {
		BufferWrapper rb(buf, numBytes);
		char queryType = rb.retrieve_char();
		if (rb.error() || queryType != PUSH) {
			return NULL;	
		}
		rb.retrieve_uint();	// Skipping queryid_1
		rb.retrieve_uint();	// Skipping queryid_2		
		uint32_t magicNumber = ntohl(rb.retrieve_uint());
		uint32_t in_destIP = ntohl(rb.retrieve_uint());
		uint16_t in_destPort = ntohs(rb.retrieve_ushort()); 		
		if (rb.error() || magicNumber != MAGIC_NUMBER) {
			return NULL;			
		}
		//	Create a ret packet
		NodeIdent destIdent = {in_destIP, in_destPort};
		RealPacket* retPacket = new RealPacket(destIdent);
		PullPacket tmpPull(0, remoteNode.addr, 
			remoteNode.port, rb.remainBufSize());
		if (tmpPull.createRealPacket(*retPacket) == -1) {
			delete retPacket;
			return NULL;
		}
		//	Append the original packet
		retPacket->append_str(buf + rb.returnPos(), rb.remainBufSize());
		if (!(retPacket->completeOkay())) {
			delete retPacket;
			return NULL;
		}
		return retPacket;
	}	
	
	virtual char getPacketType() const 	{ return PUSH; }
	virtual ~PushPacket() {}
};

class RetPing : public Packet {
protected:
	vector<NodeIdentLat> nodes;
public:
	RetPing(uint64_t id) : Packet(id) {}
	
	static RetPing* parse(const char* buf, int numBytes) {
		BufferWrapper rb(buf, numBytes);
		char queryType = rb.retrieve_char();
		if (rb.error() || queryType != RET_PING_REQ) {
			return NULL;	
		}
		uint32_t queryID_1 = ntohl(rb.retrieve_uint());
		uint32_t queryID_2 = ntohl(rb.retrieve_uint());
		uint64_t queryID = to64(queryID_1, queryID_2);
		uint32_t magicNumber = ntohl(rb.retrieve_uint());				
		if (rb.error() || magicNumber != MAGIC_NUMBER) {
			return NULL;			
		}		
		RetPing* ret = new RetPing(queryID);
		uint32_t numEntry = ntohl(rb.retrieve_uint());
		NodeIdent tmpIdent;		
		//while (!rb.error() && numEntry-- > 0) {
		for (uint32_t i = 0; (!rb.error() && i < numEntry); i++) {	
			tmpIdent.addr = ntohl(rb.retrieve_uint());
			tmpIdent.port = ntohs(rb.retrieve_ushort());
			uint32_t latencyUS = ntohl(rb.retrieve_uint());
			ret->addNode(tmpIdent, latencyUS);
		}
		if (rb.error()) {
			delete ret;
			return NULL;
		}		
		return ret; 		
	}
	
	const vector<NodeIdentLat>* returnNodes() {
		return &nodes;	
	}		
	
	virtual int createRealPacket(RealPacket& inPacket) const {
		uint32_t num_nodes = nodes.size();
		inPacket.append_char(getPacketType());
		write_id(inPacket);				
		inPacket.append_uint(htonl(num_nodes));
		for (uint32_t i = 0; i < num_nodes; i++) {
			NodeIdentLat tmp = nodes[i];
			inPacket.append_uint(htonl(tmp.addr));
			inPacket.append_ushort(htons(tmp.port));
			inPacket.append_uint(htonl(tmp.latencyUS));			
		}		
		if (!inPacket.completeOkay()) { 
			return -1; 
		}
		return 0;
	}
	
	void addNode(const NodeIdent& in_node, uint32_t in_latency_us) {
		NodeIdentLat tmp = {in_node.addr, in_node.port, in_latency_us};
		nodes.push_back(tmp);
	}		
	virtual char getPacketType() const	{ return RET_PING_REQ; }
	virtual ~RetPing() {}		
};

class PingPacket : public Packet {
public:
	PingPacket(uint64_t id) : Packet(id) {}
	virtual int createRealPacket(RealPacket& inPacket) const {
		inPacket.append_char(getPacketType());
		write_id(inPacket);					
		if (!inPacket.completeOkay()) { 
			return -1; 
		}
		return 0;		
	}
	virtual char getPacketType() const	{ return PING; }
	virtual ~PingPacket() {}		
};

class PongPacket : public Packet {
public:
	PongPacket(uint64_t id) : Packet(id) {}
	virtual int createRealPacket(RealPacket& inPacket) const {
		inPacket.append_char(getPacketType());
		write_id(inPacket);				
		if (!inPacket.completeOkay()) { 
			return -1; 
		}
		return 0;		
	}
	virtual char getPacketType() const	{ return PONG; }
	virtual ~PongPacket() {}		
};

class CreateRendv : public Packet {
public:
	CreateRendv(uint64_t id) : Packet(id) {}
	virtual int createRealPacket(RealPacket& inPacket) const {
		inPacket.append_char(getPacketType());
		write_id(inPacket);					
		if (!inPacket.completeOkay()) { 
			return -1; 
		}
		return 0;		
	}
	virtual char getPacketType() const	{ return CREATE_RENDV; }
	virtual ~CreateRendv() {}		
};

class RetRendv : public Packet {
public:
	RetRendv(uint64_t id) : Packet(id) {}
	virtual int createRealPacket(RealPacket& inPacket) const {
		inPacket.append_char(getPacketType());
		write_id(inPacket);					
		if (!inPacket.completeOkay()) { 
			return -1; 
		}
		return 0;		
	}
	virtual char getPacketType() const	{ return RET_RENDV; }
	virtual ~RetRendv() {}		
}; 

class InfoPacket : public Packet {
private:
	const RingSet* rings;
public:
	InfoPacket(uint64_t id, const RingSet* in_rings) : 
		Packet(id), rings(in_rings) {}
		
	virtual int createRealPacket(RealPacket& inPacket) const;
	static int parse(const char* buf, int numBytes, 
		map<u_int, vector<NodeIdentLat>*>& inMap);	
	virtual char getPacketType() const	{ return INFO_PACKET; }
	virtual ~InfoPacket() {}			
};

#ifdef MERIDIAN_DSL
#include "MQLState.h"
#include "MeridianDSL.h"

class DSLReplyPacket : public Packet {
public:	
	DSLReplyPacket(uint64_t id) : Packet(id) {}		
		
	static DSLReplyPacket* parse(ParserState* ps, 
			const char* buf, int numBytes, ASTNode** ret_node) {
		BufferWrapper rb(buf, numBytes);
		char queryType = rb.retrieve_char();
		if (rb.error() || queryType != DSL_REPLY) {
			ERROR_LOG("Wrong type received\n");
			return NULL;	
		}
		uint32_t queryID_1 = ntohl(rb.retrieve_uint());
		uint32_t queryID_2 = ntohl(rb.retrieve_uint());
		uint64_t queryID = to64(queryID_1, queryID_2);
		uint32_t magicNumber = ntohl(rb.retrieve_uint());
		if (rb.error() || magicNumber != MAGIC_NUMBER) {
			ERROR_LOG("Wrong magic number in packet received\n");
			return NULL;			
		}		
		DSLReplyPacket* ret = new DSLReplyPacket(queryID);
		*ret_node = unmarshal_ast(ps, &rb);		
		return ret; 		
	}

	//	Note: Need to append actual payload using the marshal_packet call
	//	This just creates the necessary Meridian headers
	virtual int createRealPacket(RealPacket& inPacket) const {
		inPacket.append_char(getPacketType());
		write_id(inPacket);
		if (!inPacket.completeOkay()) { 
			return -1; 
		}
		return 0;
	}
	
	virtual char getPacketType() const {
		return DSL_REPLY;	
	}
	
	virtual ~DSLReplyPacket() {}	
};

class DSLRequestPacket : public RendvHeaderPacket {
private:
	uint16_t ms_remain;
	uint16_t ttl;
public:	
	DSLRequestPacket(uint64_t id, uint16_t in_ms_remain, uint16_t in_ttl,
			uint32_t in_rendv_addr, uint16_t in_rendv_port)  
		: 	RendvHeaderPacket(id, in_rendv_addr, in_rendv_port), 
			ms_remain(in_ms_remain), ttl(in_ttl) {}		
	
	static DSLRequestPacket* parse(
			ParserState* ps, const char* buf, int numBytes) {
		BufferWrapper rb(buf, numBytes);
		char queryType = rb.retrieve_char();
		if (rb.error() || queryType != DSL_REQUEST) {
			ERROR_LOG("Wrong type received\n");
			return NULL;	
		}
		uint32_t queryID_1 = ntohl(rb.retrieve_uint());
		uint32_t queryID_2 = ntohl(rb.retrieve_uint());
		uint64_t queryID = to64(queryID_1, queryID_2);
		uint32_t magicNumber = ntohl(rb.retrieve_uint());		
		uint32_t rendvAddr = ntohl(rb.retrieve_uint());
		uint16_t rendvPort = ntohs(rb.retrieve_ushort());
		uint16_t cur_remain = ntohs(rb.retrieve_ushort()); 
		uint16_t cur_ttl = ntohs(rb.retrieve_ushort());	
		if (rb.error() || magicNumber != MAGIC_NUMBER) {
			ERROR_LOG("Wrong magic number in packet received\n");
			return NULL;			
		}		
		DSLRequestPacket* ret = new DSLRequestPacket(
			queryID, cur_remain, cur_ttl, rendvAddr, rendvPort);		
		if (unmarshal_packet(*ps, rb) == -1 || rb.error()) {
			delete ret;
			return NULL;
		}
		return ret; 		
	}

	//	Note: Need to append actual payload using the marshal_packet call
	//	This just creates the necessary Meridian headers
	virtual int createRealPacket(RealPacket& inPacket) const {
		inPacket.append_char(getPacketType());
		write_id(inPacket);		
		write_rendv(inPacket);
		inPacket.append_ushort(htons(ms_remain));
		inPacket.append_ushort(htons(ttl));
		if (!inPacket.completeOkay()) { 
			return -1; 
		}
		return 0;
	}
	
	virtual char getPacketType() const {
		return DSL_REQUEST;	
	}
	
	uint16_t timeout_ms() const {
		return ms_remain;
	}
	
	uint16_t getTTL() const {
		return ttl;
	}
	
	virtual ~DSLRequestPacket() {}			
};
#endif

#endif
