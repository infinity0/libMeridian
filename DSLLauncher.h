#ifndef DSL_LAUNCHER
#define DSL_LAUNCHER

typedef struct Measurement_t {
	uint32_t addr;
	uint16_t port;
	uint32_t rendvAddr;
	uint16_t rendvPort;
	vector<double> distance;
} Measurement;

// This is in MeridianDSL
extern int fillNodeIdentField(const char* in_string, 
	const map<string, ASTNode*>* inMap, int32_t* val);

extern int createMeasurement(ASTNode* in_ast, Measurement* in_Measure); 

#endif

