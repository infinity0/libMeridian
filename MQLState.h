#ifndef CLASS_MQL_STATE
#define CLASS_MQL_STATE

#include <string>
#include <set>
#include <map>
#include <vector>
#include <stdint.h>
#include <FlexLexer.h>
#include <ucontext.h>

// Forward typedef and declaration		
typedef struct ASTNode_t ASTNode;		 
class ParserState;
class VarTable;

enum ASTType {	EMPTY_TYPE = 0, 
				INT_TYPE, 
				DOUBLE_TYPE, 
				NEW_VAR_TYPE, 
				REF_VAR_TYPE,
				STRING_TYPE, 
				AST_TYPE, 
				SEP_TYPE, 
				FUNC_REF_TYPE,
				PRINT_TYPE, 
				UNARY_TYPE, 
				BIN_TYPE, 
				IF_TYPE, 
				NEW_VAR_ASSIGN_TYPE, 
				LOOP_TYPE, 
				BREAK_TYPE, 
				CONTINUE_TYPE, 
				CONTEXT_TYPE, 
				FUNC_DECLARE_TYPE, 
				FUNC_CALL_TYPE, 
				VOID_TYPE,
				RETURN_TYPE, 
				DEF_ADT_TYPE,	// Creates ADT type 
				ADT_TYPE,		// Actual type structure references
				VAR_ADT_TYPE,	// Individual instances 
				REF_VAR_ADT_TYPE,
				ASSIGN_ADT_TYPE,
				ARRAY_TYPE,
				REF_VAR_ARRAY_TYPE,
				RPC_TYPE,
				NATIVE_FUNC_TYPE,
				FOR_LOOP_TYPE,
				PRINTLN_TYPE
			};	
			
#include "Marshal.h"

typedef union ASTVal_t {
	double 	 					d_val;
	int32_t	 					i_val;
	string*  					s_val;
	
	//	For native functions (e.g. round, floor)
	struct {
		int						n_type;
		ASTNode*				n_param_1;
		ASTNode*				n_param_2;
		ASTNode*				n_param_3;		
	} n_val;
	
	//	For storing variables
	struct {
		ASTType					v_type;
		string*					v_name;
		ASTNode*				v_name_ast;		
		string*					v_name_dot_name;
		ASTNode*				v_assign;
		ASTType					v_array_type;
		const string*			v_adt_name;
		//int						v_array_size;
		ASTNode*				v_array_size;
		ASTNode*				v_access_ast;
	} v_val;

	//	For binary operations and assign	
	struct {
		int						b_type;
		ASTNode*				b_left_node;
		ASTNode*				b_right_node;
	} b_val;
	
	//	Unary operator, return
	struct {
		int						u_type;
		ASTNode*				u_node;
	} u_val;
	
	// Array type
	struct {
		ASTType					a_type;	
		const string*			a_adt_name;		
		vector<ASTNode*>*		a_vector;
		VarTable*				a_var_table;
	} a_val;
	
	//	Parameter separater type
	struct {
		vector<ASTNode*>*		p_vector;
	} p_val;	
	
	// Function operator
	struct {
		ASTType					f_type;
		const string*			f_type_name;
		const string*			f_name;
		const ASTNode*			f_formal_param;
		ASTNode*				f_actual_param;
		ASTNode*				f_node;
		ASTType					f_array_type;
	} f_val;		
	
	//	loop operator	
	struct {
		//int						l_type;
		ASTNode*				l_eval;
		ASTNode*				l_node;
	} l_val;
	
	//	for loop operator	
	struct {
		ASTNode*				for_node_1;
		ASTNode*				for_node_2;
		ASTNode*				for_node_3;
		ASTNode*				for_node_4;
	} for_val;	
	
	//	Adt type
	struct {
		const string*			adt_type_name;
		ASTNode*				adt_param;
		map<string, ASTNode*>*	adt_map;
	} adt_val;
	
	//	For if statements
	struct {
		ASTNode*		if_eval;
		ASTNode*		if_left_node;
		ASTNode*		if_right_node;				
	} if_val;
	
	struct {
		ASTNode*		rpc_dest;
		string*			rpc_func_name;
		ASTNode*		rpc_param;
	} rpc_val;
} ASTVal;

typedef struct ASTNode_t {
	ASTType		type;
	ASTVal		val;
};

class ParserState;
class VarTable{
private:
	ParserState*						v_ps;	
	map<const string, ASTNode*> 		v_map;
	vector<ASTNode*> 					v_stack_ast;
	vector<string*> 					v_stack_string;
	vector<vector<ASTNode*>*> 			v_stack_vector;
	vector<map<string, ASTNode*>*> 		v_stack_map;	
	
	VarTable* prev_var_table;	// Pointer to a prev var table that 
								// contains the global variables/declarations
		
	int remove_var_map_context();
	int remove_AST_context();
	int remove_string_context();
	int remove_vector_context();
	int remove_map_context();
	
	static bool local_exists(const map<const string, ASTNode*>* in_map, 
		const string& in_string);	
	static int local_lookup(const map<const string, ASTNode*>* in_map, 
		const string& in_string, ASTNode** in_type);
	ASTNode* create_basic_type(const ASTType in_type);
	
public:
	VarTable(ParserState* in_ps, VarTable* in_prev) 
		: v_ps(in_ps), prev_var_table(in_prev) {		
	}
	
	~VarTable() {
		remove_var_map_context();
		remove_AST_context();
		remove_string_context();
		remove_vector_context();
		remove_map_context();		
	}
	
	int local_remove(const string& in_string);
	int lookup(const string& in_string, ASTNode** in_type);	
	int insert(const string& in_string, ASTNode* in_type);	
	int update(const string& in_string, const ASTNode* in_var);
	
	ASTNode* new_stack_ast();
	string* new_stack_string();
	vector<ASTNode*>* new_stack_vector();
	map<string, ASTNode*>* new_stack_map();
		
	ASTNode* ASTCreate(const ASTType in_type, 
		const string* adt_name, const ASTType array_type, int array_size);			
	int updateADT(ASTNode* retVar, const ASTNode* in_var);
};

#include "MQL.tab.hpp"

class MyFlexLexer : public yyFlexLexer {
public:	
	ParserState* param;
	int scan(ParserState* in_state) {
		param = in_state; 
		return yylex(); 
	}
};

extern ASTNode* mk_empty(ParserState* in_state);
extern ASTNode* mk_break(ParserState* in_state);
extern ASTNode* mk_continue(ParserState* in_state);

class ParserInputBuffer {
private:
	char* buf;
	u_int bufSize;
	u_int bufCounter;
public:
	ParserInputBuffer() : buf(NULL), bufSize(0), bufCounter(0) {}
	~ParserInputBuffer() {
		if (buf) {
			delete_buffer();
		}
	}
	
	int create_buffer(u_int size) {
		if (buf) {
			fprintf(stderr, "Buffer already exists\n");
			return -1;	
		}
		buf = (char*)malloc(sizeof(char) * size);
		if (!buf) {
			return -1;
		}
		bufSize = size;
		return 0;		
	}
	
	int delete_buffer() {
		if (!buf) {
			fprintf(stderr, "No buffer has been created\n");
			return -1;	
		}
		free(buf);		// Free memory
		buf = NULL;
		bufSize = 0;	// Reset size and counter
		bufCounter = 0;
		return 0;
	}
	
	char* get_raw_buf() {
		return buf;	
	}
	
	int get_buf(char** out_buf, u_int size) {		
		*out_buf = buf + bufCounter;
		if (size > (bufSize - bufCounter)) {
			int retVal = bufSize - bufCounter;
			bufCounter = bufSize;
			return retVal; 
		}
		bufCounter += size;
		return size;
	}
	
	u_int get_buf_size() {
		return bufSize;	
	}	
};

class Query;
class DSLRecvQuery;
class MeridianProcess;

enum PSState {PS_DONE, PS_READY, PS_RUNNING, PS_BLOCKED};

class ParserState {
friend class VarTable;
private:
	YYSTYPE* 			parse_result;	
	MyFlexLexer* 		lexer;
	ASTNode* 			empty_node;
	ASTNode* 			break_node;
	ASTNode* 			continue_node;
	ASTNode* 			start_node;	
	string 				ret_string;		//	Stores the special $RETURN$ symbol	
	vector<VarTable*> 	var_table;
	string				func_string;
	ASTNode*			param_node;
	NodeIdent			caller_id;
	int					evalCount;
	PSState				state;
	void*				context_stack;
	ucontext_t			parse_context;
	//ASTNode* 			rpc_return;
	ASTNode*			query_return;
	DSLRecvQuery*		queryPtr;
	MeridianProcess*	meridProcess;
	ASTNode*			rpc_recv;

	int 				ASTAllocationCount;	
	
	
	void delete_map(map<string, ASTNode*>* in_map) 		{	delete in_map; }
	void delete_vector(vector<ASTNode*>* in_vect) 		{	delete in_vect; }
	void delete_string(string* in_str) 					{	delete in_str; }
	void delete_ast(ASTNode* in_ast)  {
		ASTAllocationCount--;
		free(in_ast);	
	}
	// This functions are the only ones that ParserState can return NULL
	// They should not be called directly except by the VarTable and the
	// return value should be checked 	
	map<string, ASTNode*>* new_map() {
		map<string, ASTNode*>* retMap = new map<string, ASTNode*>();
		return retMap;
	}	
	vector<ASTNode*>* new_vector() {
		vector<ASTNode*>* retVect = new vector<ASTNode*>();
		return retVect;
	}	
	string* new_string() {
		string* retStr = new string();
		return retStr;
	}	
#define MAX_AST_PER_STACK	100000	
	ASTNode* new_ast() {		
		if (++ASTAllocationCount > MAX_AST_PER_STACK) {
			fprintf(stderr, "Maximum allocation count reached\n");
			return NULL;
		}
		ASTNode* retNode = (ASTNode*)malloc(sizeof(ASTNode));
		return retNode;
	}		

public:	
	void set_parse_result(YYSTYPE* in_result) 	{ parse_result = in_result; }
	YYSTYPE* get_parse_result() 				{ return parse_result; }
	MyFlexLexer* get_lex() 						{ return lexer; }
	VarTable* get_var_table() 					{ return var_table.back(); }	
	ASTNode* empty_token()						{ return empty_node; }
	ASTNode* break_token()						{ return break_node; }
	ASTNode* continue_token()					{ return continue_node; }
	const string* return_string()				{ return &ret_string; }
	
	//void set_call_id(const NodeIdent& in_call)	{ caller_id = in_call; }
	//NodeIdent get_call_id() 					{ return caller_id; }
	
	void set_parser_state(PSState in_state)		{ state = in_state; }
	PSState parser_state() const				{ return state; }

	ParserInputBuffer	input_buffer;
	//ucontext_t 			save_context;
	int save_context() {
		if (context_stack != NULL) {
			return -1;	
		}
#define FIBER_STACK	(256 * 1024)				
		if ((context_stack = malloc(FIBER_STACK)) == NULL) {
			return -1;	
		}		
		// Get the current execution context
		getcontext(&(parse_context));			
		// Modify the context to a new stack
		parse_context.uc_link = 0;		
		parse_context.uc_stack.ss_sp = context_stack;
		parse_context.uc_stack.ss_size = FIBER_STACK;
		parse_context.uc_stack.ss_flags = 0;
		return 0;
	}
	
	ucontext_t* get_context() {
		if (context_stack == NULL) {
			return NULL;	
		}
		return &parse_context;
	}
	
	void set_param(ASTNode* in_node) {
		if (in_node != NULL) {
			param_node = in_node;	
		}
	}
	
	ASTNode* get_param() {
		return param_node;	
	}
	
	void set_func_string(const char* in_string) {
		if (in_string != NULL) {
			func_string = in_string;
		}
	}
	
	const string* get_func_string() {
		return &func_string;
	}
			
	int new_var_table(VarTable** old_table) {
		VarTable* new_table = NULL;
		if (var_table.empty()) {
			*old_table = NULL;
			// Creating global context
			new_table = new VarTable(this, NULL);
		} else {
			*old_table = var_table.back();
			// Keeping global context
			new_table = new VarTable(this, var_table[0]);
		}
		if (new_table == NULL) {
			return -1;	
		}
		var_table.push_back(new_table);
		return 0;
	}
	
	int new_context() {
		if (var_table.empty()) {
			fprintf(stderr, "Cannot create new "
				"context without previous var table (parser error)");
			return -1;
		} 
		// Keeping previous context
		VarTable* new_table = new VarTable(this, var_table.back());
		if (new_table == NULL) {
			return -1;	
		}
		var_table.push_back(new_table);
		return 0;			
	}
	
	int remove_context() {
		return remove_var_table();	
	}
	
	int remove_var_table() {
		if (var_table.empty()) {
			return -1;
		}
		VarTable* oldest = var_table.back();
		var_table.pop_back();
		delete oldest;
		return 0;
	}
	
	void set_start(ASTNode* in_node) {
		if (in_node) {	// Just in case
			start_node = in_node;
		}
	}
	
	ASTNode* get_start() {
		return start_node;	
	}
	
	void allocateEvalCount(int in_eval) {
		evalCount = in_eval;
	}
	
	void decrementEvalCount() {
		evalCount--;	
	}
	
	int getEvalCount() {
		return evalCount;	
	}
#if 0	
	void setRPCReturn(ASTNode* in_rpc) {
		if (in_rpc) {
			rpc_return = in_rpc;	
		}
	}
	
	ASTNode* getRPCReturn() {
		return rpc_return;	
	}
#endif
	void setQueryReturn(ASTNode* in_ast) {
		if (in_ast) {
			query_return = in_ast;	
		}
	}
	
	ASTNode* getQueryReturn() {
		return query_return;	
	}
	
	
	void setRPCRecv(ASTNode* in_rpc) {
		if (in_rpc) {
			rpc_recv = in_rpc;	
		}
	}
	
	ASTNode* getRPCRecv() {
		return rpc_recv;	
	}	
	
	void setQuery(DSLRecvQuery* in_query) {
		queryPtr = in_query;
	}
	
	DSLRecvQuery* getQuery() {
		return queryPtr;	
	}
	
	void setMeridProcess(MeridianProcess* in_process) {
		meridProcess = in_process;	
	}
	
	MeridianProcess* getMeridProcess() {
		return meridProcess;	
	}
		
	ParserState() : parse_result(NULL), evalCount(0),  
			state(PS_READY), context_stack(NULL), queryPtr(NULL), 
			meridProcess(NULL), ASTAllocationCount(0) {		
		//	This symbol cannot ever actuall be used by the user,
		//	so there can not be a collision
		ret_string = "$RETURN_VALUE$";
		lexer = new MyFlexLexer();
		//	VarTable must be created before mk_empty is called
		VarTable* temp;
		new_var_table(&temp);
		empty_node = mk_empty(this);
		start_node = empty_node;
		query_return = empty_node;
		rpc_recv = empty_node;
		set_param(empty_node);
		break_node = mk_break(this);
		continue_node = mk_continue(this);
		caller_id.addr = 0; caller_id.port = 0;
	}
	
	~ParserState(){
		if (lexer) delete lexer;
		while (remove_var_table() != -1);
		if (context_stack) free(context_stack);
	}
};

#endif
