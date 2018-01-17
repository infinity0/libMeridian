#ifndef HEADER_MERIDIAN_DSL
#define HEADER_MERIDIAN_DSL

#include <setjmp.h>
#include "Marshal.h"

extern ucontext_t global_env_thread;
//extern jmp_buf global_env_pc;
extern int g_parser_line;
extern int yyparse(void*);

#define MERIDIAN_DSL_PORT 	3964
#define MAX_RECURSE_COUNT	1000

//	Makes a ASTNode used during parsing		
extern ASTNode* mk_empty(ParserState* in_state);
extern ASTNode* mk_break(ParserState* in_state);
extern ASTNode* mk_context(ParserState* in_state, ASTNode* a);
extern ASTNode* mk_continue(ParserState* in_state);
extern ASTNode* mk_node_list(ParserState* in_state, ASTNode* a, ASTNode* b);
extern ASTNode* mk_sep_list(ParserState* in_state, ASTNode* a);
extern ASTNode* mk_type(ParserState* in_state, ASTType in_type);
extern ASTNode* mk_new_var(ParserState* in_state, ASTType in_type, string* a);		
extern ASTNode* mk_ref_var(ParserState* in_state, string* a);
extern ASTNode* mk_int(ParserState* in_state, int a);
extern ASTNode* mk_double(ParserState* in_state, double a);
extern ASTNode* mk_string(ParserState* in_state, string* a);
extern ASTNode* mk_print(ParserState* in_state, ASTNode* a);
extern ASTNode* mk_println(ParserState* in_state, ASTNode* a);
extern ASTNode* mk_unary(ParserState* in_state, int op, ASTNode* a);
extern ASTNode* mk_arith(ParserState* in_state, int op, ASTNode* a, ASTNode* b);
extern ASTNode* mk_selection(
		ParserState* in_state, ASTNode* eval, ASTNode* a, ASTNode* b);
extern ASTNode* mk_new_var_assign(
		ParserState* in_state, ASTType in_type, string* a, ASTNode* b);	
extern ASTNode* mk_loop(ParserState* in_state, ASTNode* eval, ASTNode* a);
extern ASTNode* mk_for_loop(ParserState* in_state, 
		ASTNode* a, ASTNode* b, ASTNode* c, ASTNode* d);
extern ASTNode* mk_function_declare(ParserState* in_state, ASTType in_type,
		string* in_type_name, ASTType in_array_type, 
		string* func_name, ASTNode* param_list, 
		ASTNode* statements_node);
extern ASTNode* mk_function_call(ParserState* in_state, ASTType in_type, 
		const string* in_type_name,	ASTType in_array_type, 
		const ASTNode* formal_param_list, 
		ASTNode* actual_param_list, ASTNode* statements_node);
extern ASTNode* mk_function_ref(ParserState* in_state, string* func_name, ASTNode* a);
extern ASTNode* mk_return(ParserState* in_state, ASTNode* a);	
extern ASTNode* mk_adt(ParserState* in_state, string* adt_name, ASTNode* param_list);
extern ASTNode* mk_new_adt_var(ParserState* in_state, string* adt_name, string* a);
extern ASTNode* mk_new_adt_var_assign(
		ParserState* in_state, string* adt_name, string* a, ASTNode* b);
extern ASTNode* mk_ref_adt_var(ParserState* in_state, ASTNode* a, string* b);
extern ASTNode* mk_ref_array_var(ParserState* in_state, ASTNode* a, ASTNode* b);
extern ASTNode* mk_adt_assign(ParserState* in_state, ASTNode* a, ASTNode* b);
extern ASTNode* mk_new_var_array(
		ParserState* in_state, ASTType in_type, string* a, ASTNode* size);
extern ASTNode* mk_new_adt_var_array(
		ParserState* in_state, string* adt_name, string* a, ASTNode* size);		
extern ASTNode* mk_new_var_array_assign(ParserState* in_state, 
		ASTType in_type, string* a, ASTNode* b, ASTNode* size);		
extern ASTNode* mk_new_adt_var_array_assign(ParserState* in_state, 
		string* adt_name, string* a, ASTNode* b, ASTNode* size);				
extern ASTNode* mk_rpc(ParserState* in_state,
		ASTNode* dest, string* func_name, ASTNode* paramAST);
extern ASTNode* mk_native_func_0(
		ParserState* in_state, int type);
extern ASTNode* mk_native_func_1(
		ParserState* in_state, int type, ASTNode* a);		
extern ASTNode* mk_native_func_2(
		ParserState* in_state, int type, ASTNode* a, ASTNode* b);
extern ASTNode* mk_native_func_3(
		ParserState* in_state, int type, ASTNode* a, ASTNode* b, ASTNode* c);		

// Used for marshalling and unmarshaling primitives
extern int marshal_ast(const ASTNode* in_node, RealPacket* in_packet);
extern ASTNode* unmarshal_ast(ParserState* ps, BufferWrapper* bw);

// Performs RPC (handleRPC performs marshalling as well)
extern ASTNode* handleRPC(ParserState* ps, 
		const NodeIdentRendv& dest, string* func_name, ASTNode* paramAST);
extern int marshal_packet(ParserState* ps, RealPacket& inPacket,
		const string* func_name, const ASTNode* paramAST);		
extern int unmarshal_packet(ParserState& ps, BufferWrapper& bw);
		
// Used in evaluating the constructed AST
extern ASTNode* evalNativeFunctions(
		ParserState* ps, ASTNode* cur_node, int recurse_count);		
extern ASTNode* eval(ParserState* ps, ASTNode* cur_node, int recurse_count);
extern void jmp_eval(ParserState* ps);

// Helper functions
extern ASTNode* ASTCreate(ParserState* cur_parser, const ASTType in_type, 
		const string* adt_name, const ASTType array_type, int array_size);
extern ASTNode* arith_operation(ParserState* cur_parser, int op,  
		const ASTNode* a, const ASTNode* b);
extern ASTNode* variable_create(ParserState* cur_parser, const ASTType in_type, 
		const string* adt_name, const string* in_string, bool overwrite);
extern ASTNode* variable_assign(ParserState* cur_parser, 
		const string* in_string, const ASTNode* in_var);
extern ASTNode* handleDNSLookup(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count);
extern ASTNode* handleGetSelf(ParserState* cur_parser);
extern ASTNode* handleRingGT(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count);
extern ASTNode* handleRingGE(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count);
extern ASTNode* handleRingLT(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count);
extern ASTNode* handleRingLE(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count);
extern ASTNode* handleGetDistTCP(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count);
extern ASTNode* handleGetDistDNS(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count);
extern ASTNode* handleGetDistPing(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count); 
#ifdef PLANET_LAB_SUPPORT
extern ASTNode* handleGetDistICMP(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count); 		
#endif
extern ASTNode* createNodeIdent(
		ParserState* ps, const NodeIdentRendv& in_ident);		
//extern ASTNode* createNodeIdentLat(ParserState* ps, 
//		const NodeIdentRendv& in_ident, const vector<uint32_t>& lat_us);
extern ASTNode* createNodeIdentLat(ParserState* ps, const 
		NodeIdentRendv& in_ident, const uint32_t* lat_us, u_int lat_size);
extern int createNodeIdent(ASTNode* in_array, NodeIdentRendv* in_NodeIdent);
extern bool ADTEqual(const ASTNode* a, const ASTNode* b);
extern ASTNode* ADTCreateAndCopy(ParserState* ps, const ASTNode* a);
#endif
