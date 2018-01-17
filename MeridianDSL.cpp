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
#include <ucontext.h>
#include <stdint.h>
#include <rpc/rpc.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <zlib.h>
#include <float.h>
#include <math.h>
#include "Marshal.h"
#include "MQLState.h"
#include "MeridianDSL.h"

template <class T>
T opr(int op, T param_1, T param_2) {
	switch(op) {
		case '*':
			return (param_1 * param_2);
		case '+':
			return (param_1 + param_2);
		case '-':
			return (param_1 - param_2);	
		case '/':
			return (param_1 / param_2);			
	}
	return 0;
}

template <class T>
int opr_int_ret(int op, T param_1, T param_2) {
	switch(op) {
		case AND_OP:
			return (param_1 && param_2);
		case OR_OP:
			return (param_1 || param_2);	
		case '<':
			return (param_1 < param_2);
		case '>':
			return (param_1 > param_2);
		case LE_OP:
			return (param_1 <= param_2);
		case GE_OP:
			return (param_1 >= param_2);
		case EQ_OP:
			return (param_1 == param_2);
		case NE_OP:
			return (param_1 != param_2);
	}
	return 0;
}

int opr_int_only(int op, int param_1, int param_2) {
	switch(op) {
		case '|':
			return (param_1 | param_2);
		case '&':
			return (param_1 & param_2);
		case '^':
			return (param_1 ^ param_2);
		case LEFT_OP:
			return (param_1 << param_2);
		case RIGHT_OP:
			return (param_1 >> param_2);
	}
	return 0;
}

int marshal_packet(ParserState* ps, RealPacket& inPacket,
		const string* func_name, const ASTNode* paramAST) {
#if 0			
	ASTNode* funcVar = NULL; 
	if (ps->get_var_table()->lookup(*(func_name), &funcVar) == -1) {
		DSL_ERROR("Function name %s not found\n", func_name->c_str());
		return -1;
	}
#endif	
	//	Allocate compression buffer
	uLongf tmpCompBufSize = (2 * ps->input_buffer.get_buf_size()) + 12;
	char* tmpCompBuf = (char*) malloc(tmpCompBufSize);
	if (tmpCompBuf == NULL) {
		DSL_ERROR("Cannot allocate temporary compress buffer\n");
		return -1;	
	}
	if (compress((Bytef*)tmpCompBuf, (uLongf*)&tmpCompBufSize,
			(Bytef*)ps->input_buffer.get_raw_buf(), 
			ps->input_buffer.get_buf_size()) != Z_OK) {
		DSL_ERROR("zlib compression failed\n");
		free(tmpCompBuf);
		return -1;
	}
	//	This packet MUST not have a rendavous host
	// 	Save both original size and compressed size
	inPacket.append_uint(htonl(ps->input_buffer.get_buf_size()));
	inPacket.append_uint(htonl(tmpCompBufSize));			
	inPacket.append_str(tmpCompBuf, tmpCompBufSize);
	free(tmpCompBuf); // Done with compress buffer
	inPacket.append_uint(htonl(func_name->size()));
	inPacket.append_str(func_name->c_str(), func_name->size());
	if (!(inPacket.completeOkay())) {
		DSL_ERROR("Packet creation failure\n");
		return -1;	
	}			
	if (marshal_ast(paramAST, &inPacket) == -1) {
		return -1;
	}
	return 0;
}

ASTNode* arith_operation(ParserState* cur_parser, int op,  
		const ASTNode* a, const ASTNode* b) {
	ASTNode* retVar = cur_parser->get_var_table()->new_stack_ast();
	if (retVar == NULL) {
		return cur_parser->empty_token();	
	}
	if (a->type != INT_TYPE && b->type != DOUBLE_TYPE) {
		DSL_ERROR(
			"Cannot perform arith operation on non int/double type\n");					
		return cur_parser->empty_token();
	}
	if (a->type != b->type) {
		DSL_ERROR("Arith operation on incompatible types\n");
		return cur_parser->empty_token();
	}
	switch (op) {	
	case '%':
		// Special treatement of floats
		switch((retVar->type = a->type)) {
			case INT_TYPE:
				retVar->val.i_val = a->val.i_val % b->val.i_val;
				break;
			case DOUBLE_TYPE:
				retVar->val.d_val = fmod(a->val.d_val, b->val.d_val);
				break;
			default:
				DSL_ERROR("Unknown type encountered (parser error)\n");
				return cur_parser->empty_token();
		}
		break;
	case '&':
	case '^':
	case '|':
	case LEFT_OP:
	case RIGHT_OP:
		// Operates on ints only
		if ((retVar->type = a->type) != INT_TYPE) {
			DSL_ERROR("Cannot operate on double\n");
			return cur_parser->empty_token();			
		}
		retVar->val.i_val = opr_int_only(op, a->val.i_val, b->val.i_val);
		break;
	case AND_OP:
	case OR_OP:
	case '<':
	case '>':
	case LE_OP:
	case GE_OP:
	case EQ_OP:
	case NE_OP:
		// Returns ints only
		retVar->type = INT_TYPE;
		switch (a->type) {
			case INT_TYPE:
				retVar->val.i_val = 
					opr_int_ret<int>(op, a->val.i_val, b->val.i_val);
				break;
			case DOUBLE_TYPE:
				retVar->val.i_val = 
					opr_int_ret<double>(op, a->val.d_val, b->val.d_val);
				break;
			default:
				DSL_ERROR("Unknown type encountered (parser error)\n");
				return cur_parser->empty_token();				
		}		
		break;			
	default:
		switch((retVar->type = a->type)) {
			case INT_TYPE:
				retVar->val.i_val 
					= opr<int>(op, a->val.i_val, b->val.i_val);
				break;
			case DOUBLE_TYPE:
				retVar->val.d_val 
					= opr<double>(op, a->val.d_val, b->val.d_val);
				break;
			default:
				DSL_ERROR("Unknown type encountered (parser error)\n");
				return cur_parser->empty_token();
		}
		break;
	}
	return retVar;
}

ASTNode* ASTCreate(ParserState* cur_parser, const ASTType in_type, 
		const string* adt_name, const ASTType array_type, int array_size) {
	ASTNode* retNode = cur_parser->get_var_table()->ASTCreate(in_type, 
		adt_name, array_type, array_size);
	if (retNode == NULL) {
		return cur_parser->empty_token();
	}
	return retNode;
}

ASTNode* ADTCreateAndCopy(ParserState* ps, const ASTNode* a) {
	switch (a->type) {
		case ARRAY_TYPE: {
			DSL_ERROR("ADTCreateAndCopy currently "
				"does not support array types\n");
			return ps->empty_token();
		}
		case INT_TYPE:
		case DOUBLE_TYPE:
		case STRING_TYPE: {
			ASTNode* tmpNode = ASTCreate(ps, a->type, NULL, VOID_TYPE, 0);
			if (tmpNode->type == EMPTY_TYPE) {
				return ps->empty_token();
			}
			if (ps->get_var_table()->updateADT(tmpNode, a) == -1) {
				return ps->empty_token();
			}
			return tmpNode;				
		}
		case VAR_ADT_TYPE: {
			ASTNode* tmpNode = ASTCreate(ps, ADT_TYPE, 
				a->val.adt_val.adt_type_name, VOID_TYPE, 0);
			if (tmpNode->type == EMPTY_TYPE) {
				return ps->empty_token();
			}
			if (ps->get_var_table()->updateADT(tmpNode, a) == -1) {
				return ps->empty_token();
			}
			return tmpNode;					
		}
		default:
			break;
	}
	DSL_ERROR("Unsupported type encountered in ADTCreateAndCopy\n");	
	return ps->empty_token();	
}


bool ADTEqual(const ASTNode* a, const ASTNode* b) {
	if (a->type != b->type) {
		return false;	
	}
	if (a->type == ARRAY_TYPE) {		
		if (a->val.a_val.a_type != b->val.a_val.a_type) {
			return false;
		}
		if (a->val.a_val.a_type == ADT_TYPE) {
			if (*(a->val.a_val.a_adt_name) != *(b->val.a_val.a_adt_name)) {
				return false;
			}				
		}
		if (a->val.a_val.a_vector->size() != b->val.a_val.a_vector->size()) {
			return false;	
		}
		for (u_int i = 0; i < a->val.a_val.a_vector->size(); i++) {
			if (ADTEqual((*(a->val.a_val.a_vector))[i], 
					(*(b->val.a_val.a_vector))[i]) == false) {
				return false;
			}
		}
		return true;		
	} else if (a->type == VAR_ADT_TYPE) {
		if (*(a->val.adt_val.adt_type_name) != 
				*(b->val.adt_val.adt_type_name)) {
			return false;
		}
		if (a->val.adt_val.adt_map->size() != b->val.adt_val.adt_map->size()) {
			return false;	
		}
		map<string, ASTNode*>::iterator it = a->val.adt_val.adt_map->begin();
		for (; it != a->val.adt_val.adt_map->end(); it++) {
			map<string, ASTNode*>::const_iterator findIt =
				b->val.adt_val.adt_map->find(it->first);
			if (findIt == b->val.adt_val.adt_map->end()) {
				return false;		
			}
			if (ADTEqual(it->second, findIt->second) == false) {
				return false;	
			}
		}
		return true;
	} else if (a->type == INT_TYPE) {
		if (a->val.i_val != b->val.i_val) {
			return false;	
		}
		return true;
	} else if (a->type == DOUBLE_TYPE) {
		if (a->val.d_val != b->val.d_val) {
			return false;	
		}
		return true;			
	} else if (a->type == STRING_TYPE) {
		if (*(a->val.s_val) != *(b->val.s_val)) {
			return false;	
		}
		return true;		
	}
	DSL_ERROR("Unsupported type for ADTEqual\n");
	return false;
}


ASTNode* variable_create(ParserState* cur_parser, const ASTType in_type, 
		const string* adt_name, const ASTType array_type, int array_size,
		const string* in_string, bool overwrite) {
	ASTNode* retVar 
		= ASTCreate(cur_parser, in_type, adt_name, array_type, array_size);
	if (retVar->type == EMPTY_TYPE) {
		return cur_parser->empty_token();
	}
	if (overwrite) {
		cur_parser->get_var_table()->local_remove(*(in_string));
	}
	if (cur_parser->get_var_table()->insert(*(in_string), retVar) == -1) {
		DSL_ERROR("Variable name collision 2\n");
		return cur_parser->empty_token();
	}
	return retVar;
}

int updateBasicType(ASTNode* a, const ASTNode* b) {
	if (a->type != b->type) {
		DSL_ERROR(
			"UpdateBasicType: Assigning incompatible types %d from %d\n", 
			a->type, b->type);
		return -1;
	}
	switch(a->type) {
		case INT_TYPE:
			a->val.i_val = b->val.i_val;
			break;
		case DOUBLE_TYPE:
			a->val.d_val = b->val.d_val;
			break;
		case STRING_TYPE:
			*(a->val.s_val) = *(b->val.s_val);
			break;
		default:
			DSL_ERROR("Not basic type\n");
			return -1;
	}
	return 0;
}

ASTNode* variable_assign(ParserState* cur_parser, 
		const string* in_string, const ASTNode* in_var) {		
	if (cur_parser->get_var_table()->update(*(in_string), in_var) == -1) {
		DSL_ERROR("Cannot update unknown variable %s found\n", 
			in_string->c_str());
		return cur_parser->empty_token();
	}
	ASTNode* retVar = NULL; 
	if (cur_parser->get_var_table()->lookup(*(in_string), &retVar) == -1) {
		DSL_ERROR("Variable name %s not found\n", in_string->c_str());
		return cur_parser->empty_token();
	}
	return retVar;
}

//	NOTE: This is very inefficient, need better implementation later
int param_list_assign(ParserState* cur_parser, const ASTNode* formal, 
		const vector<ASTNode*>* actual_param) {
	if (formal->type == EMPTY_TYPE) {		
		return 0;	// Void type
	}
	vector<ASTNode*>* formal_param = formal->val.p_val.p_vector;	
	if (formal_param->size() != actual_param->size()) {
		DSL_ERROR("Formal and actual parameters don't match\n");
		return -1;
	}	
	for (u_int i = 0; i < formal_param->size(); i++) {
		if ((*formal_param)[i]->type != NEW_VAR_TYPE) {
			return -1;
		}
		// (*formal_param)[i]->val.v_val.v_array_size should always be 0		
		if (variable_create(cur_parser, 
				(*formal_param)[i]->val.v_val.v_type, 
				(*formal_param)[i]->val.v_val.v_adt_name,
				(*formal_param)[i]->val.v_val.v_array_type,
				//(*formal_param)[i]->val.v_val.v_array_size,
				0,				
				(*formal_param)[i]->val.v_val.v_name,
				false)->type == EMPTY_TYPE) {
			return -1;	
		}
		if (variable_assign(cur_parser, 
				(*formal_param)[i]->val.v_val.v_name,
				(*actual_param)[i])->type == EMPTY_TYPE) {
			return -1;		
		}
	}	
	return 0;
}		

ASTNode* mk_empty(ParserState* in_state) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}
	thisNode->type = EMPTY_TYPE;
	return thisNode; //	Don't bother init the ASTVal
}

ASTNode* mk_adt(ParserState* in_state, string* adt_name, ASTNode* param_list) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = DEF_ADT_TYPE;
	thisNode->val.adt_val.adt_type_name = adt_name;
	thisNode->val.adt_val.adt_param = param_list;
	return thisNode;	
}

ASTNode* mk_continue(ParserState* in_state) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = CONTINUE_TYPE;
	return thisNode; //	Don't bother init the ASTVal
}

ASTNode* mk_break(ParserState* in_state) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = BREAK_TYPE;
	return thisNode; //	Don't bother init the ASTVal
}

ASTNode* mk_node_list(ParserState* in_state, ASTNode* a, ASTNode* b) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = AST_TYPE;
	thisNode->val.b_val.b_left_node = a;
	thisNode->val.b_val.b_right_node = b;
	return thisNode;	
}

ASTNode* mk_sep_list(ParserState* in_state, ASTNode* a) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = SEP_TYPE;
	thisNode->val.p_val.p_vector = 
		in_state->get_var_table()->new_stack_vector();
	if (thisNode->val.p_val.p_vector == NULL) {
		return in_state->empty_token();
	}
	thisNode->val.p_val.p_vector->push_back(a);
	return thisNode;	
}

//	Only used by NEW_VAR_ASSIGN_TYPE and RPC_TYPE 
ASTNode* mk_sep_list(ParserState* in_state, vector<ASTNode*>* a) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = SEP_TYPE;
	thisNode->val.p_val.p_vector = a;
	return thisNode;	
}

ASTNode* mk_return(ParserState* in_state, ASTNode* a) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = RETURN_TYPE;
	thisNode->val.u_val.u_node = a;
	return thisNode;
}

ASTNode* mk_context(ParserState* in_state, ASTNode* a) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = CONTEXT_TYPE;
	thisNode->val.u_val.u_node = a;
	return thisNode;	
}

ASTNode* mk_new_var(ParserState* in_state, ASTType in_type, string* a) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = NEW_VAR_TYPE;
	thisNode->val.v_val.v_type = in_type;
	thisNode->val.v_val.v_name = a;
	return thisNode;
}

ASTNode* mk_new_var_array(
		ParserState* in_state, ASTType in_type, string* a, ASTNode* size) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = NEW_VAR_TYPE;
	thisNode->val.v_val.v_type = ARRAY_TYPE;
	thisNode->val.v_val.v_name = a;
	thisNode->val.v_val.v_array_type = in_type;
	thisNode->val.v_val.v_array_size = size;
	return thisNode;
}

ASTNode* mk_new_adt_var_array(
		ParserState* in_state, string* adt_name, string* a, ASTNode* size) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = NEW_VAR_TYPE;
	thisNode->val.v_val.v_type = ARRAY_TYPE;
	thisNode->val.v_val.v_name = a;
	thisNode->val.v_val.v_adt_name = adt_name;	
	thisNode->val.v_val.v_array_type = ADT_TYPE;
	thisNode->val.v_val.v_array_size = size;
	return thisNode;
}

ASTNode* mk_new_adt_var(ParserState* in_state, string* adt_name, string* a) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = NEW_VAR_TYPE;
	thisNode->val.v_val.v_type = ADT_TYPE;
	thisNode->val.v_val.v_name = a;
	thisNode->val.v_val.v_adt_name = adt_name;
	return thisNode;
}

ASTNode* mk_new_adt_var_assign(
		ParserState* in_state, string* adt_name, string* a, ASTNode* b) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = NEW_VAR_ASSIGN_TYPE;
	thisNode->val.v_val.v_type = ADT_TYPE;
	thisNode->val.v_val.v_name = a;
	thisNode->val.v_val.v_adt_name = adt_name;	
	thisNode->val.v_val.v_assign = b;
	return thisNode;
}

ASTNode* mk_new_var_assign(
		ParserState* in_state, ASTType in_type, string* a, ASTNode* b) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = NEW_VAR_ASSIGN_TYPE;
	thisNode->val.v_val.v_type = in_type;
	thisNode->val.v_val.v_name = a;
	thisNode->val.v_val.v_assign = b;
	return thisNode;
}

ASTNode* mk_new_var_array_assign(ParserState* in_state, 
		ASTType in_type, string* a, ASTNode* b, ASTNode* size) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = NEW_VAR_ASSIGN_TYPE;
	thisNode->val.v_val.v_type = ARRAY_TYPE;
	thisNode->val.v_val.v_name = a;
	thisNode->val.v_val.v_array_type = in_type;
	thisNode->val.v_val.v_array_size = size;
	thisNode->val.v_val.v_assign = b;
	return thisNode;
}


ASTNode* mk_new_adt_var_array_assign(ParserState* in_state, 
		string* adt_name, string* a, ASTNode* b, ASTNode* size) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = NEW_VAR_ASSIGN_TYPE;
	thisNode->val.v_val.v_type = ARRAY_TYPE;
	thisNode->val.v_val.v_name = a;
	thisNode->val.v_val.v_adt_name = adt_name;	
	thisNode->val.v_val.v_array_type = ADT_TYPE;
	thisNode->val.v_val.v_array_size = size;
	thisNode->val.v_val.v_assign = b;
	return thisNode;
}

ASTNode* mk_ref_var(ParserState* in_state, string* a) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = REF_VAR_TYPE;
	//	Type info does not need to be init
	thisNode->val.v_val.v_name = a;
	return thisNode;	
}

ASTNode* mk_ref_adt_var(ParserState* in_state, ASTNode* a, string* b) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = REF_VAR_ADT_TYPE;
	//	Type info does not need to be init
	thisNode->val.v_val.v_name_ast = a;
	thisNode->val.v_val.v_name_dot_name = b;
	return thisNode;	
}

ASTNode* mk_ref_array_var(ParserState* in_state, ASTNode* a, ASTNode* b) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = REF_VAR_ARRAY_TYPE;
	//	Type info does not need to be init
	thisNode->val.v_val.v_name_ast = a;
	thisNode->val.v_val.v_access_ast = b;
	return thisNode;	
}

ASTNode* mk_int(ParserState* in_state, int a) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = INT_TYPE;
	thisNode->val.i_val = a;
	return thisNode;
}

ASTNode* mk_double(ParserState* in_state, double a) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = DOUBLE_TYPE;
	thisNode->val.d_val = a;
	return thisNode;
}

ASTNode* mk_native_func_0(ParserState* in_state, int type) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = NATIVE_FUNC_TYPE;
	thisNode->val.n_val.n_type = type;
	return thisNode;
}

ASTNode* mk_native_func_1(
		ParserState* in_state, int type, ASTNode* a) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = NATIVE_FUNC_TYPE;
	thisNode->val.n_val.n_type = type;
	thisNode->val.n_val.n_param_1 = a;
	return thisNode;
}

ASTNode* mk_native_func_2(
		ParserState* in_state, int type, ASTNode* a, ASTNode* b) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = NATIVE_FUNC_TYPE;
	thisNode->val.n_val.n_type = type;
	thisNode->val.n_val.n_param_1 = a;
	thisNode->val.n_val.n_param_2 = b;	
	return thisNode;
}

ASTNode* mk_native_func_3(
		ParserState* in_state, int type, ASTNode* a, ASTNode* b, ASTNode* c) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = NATIVE_FUNC_TYPE;
	thisNode->val.n_val.n_type = type;
	thisNode->val.n_val.n_param_1 = a;
	thisNode->val.n_val.n_param_2 = b;
	thisNode->val.n_val.n_param_3 = c;		
	return thisNode;
}

ASTNode* mk_string(ParserState* in_state, string* a) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = STRING_TYPE;
	// Should create a NEW string, for correct semantics
	thisNode->val.s_val = in_state->get_var_table()->new_stack_string();
	if (thisNode->val.s_val == NULL) {
		return in_state->empty_token();	
	}
	*(thisNode->val.s_val) = *a;
	return thisNode;
}

ASTNode* mk_print(ParserState* in_state, ASTNode* a) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = PRINT_TYPE;
	thisNode->val.u_val.u_node = a;
	return thisNode;	
}

ASTNode* mk_println(ParserState* in_state, ASTNode* a) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = PRINTLN_TYPE;
	thisNode->val.u_val.u_node = a;
	return thisNode;	
}

ASTNode* mk_function_declare(ParserState* in_state, ASTType in_type,
		string* in_type_name, ASTType in_array_type, 
		string* func_name, ASTNode* param_list, 
		ASTNode* statements_node) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = FUNC_DECLARE_TYPE;
	thisNode->val.f_val.f_type = in_type;
	thisNode->val.f_val.f_type_name = in_type_name;
	thisNode->val.f_val.f_array_type = in_array_type;	
	thisNode->val.f_val.f_name = func_name;
	thisNode->val.f_val.f_formal_param = param_list;
	thisNode->val.f_val.f_node = statements_node;
	return thisNode;
}

ASTNode* mk_function_call(ParserState* in_state, ASTType in_type, 
		const string* in_type_name,	ASTType in_array_type, 
		const ASTNode* formal_param_list, 
		ASTNode* actual_param_list, ASTNode* statements_node) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = FUNC_CALL_TYPE;
	thisNode->val.f_val.f_type = in_type;
	thisNode->val.f_val.f_type_name = in_type_name;
	thisNode->val.f_val.f_array_type = in_array_type;		
	thisNode->val.f_val.f_formal_param = formal_param_list;
	thisNode->val.f_val.f_actual_param = actual_param_list;
	thisNode->val.f_val.f_node = statements_node;
	return thisNode;
}

ASTNode* mk_function_ref(
		ParserState* in_state, string* func_name, ASTNode* a) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = FUNC_REF_TYPE;
	thisNode->val.f_val.f_name = func_name;
	thisNode->val.f_val.f_actual_param = a;
	return thisNode;
}
	

ASTNode* mk_unary(ParserState* in_state, int op, ASTNode* a) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = UNARY_TYPE;
	thisNode->val.u_val.u_type = op;
	thisNode->val.u_val.u_node = a;
	return thisNode;
}

ASTNode* mk_arith(ParserState* in_state, int op, ASTNode* a, ASTNode* b) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = BIN_TYPE;
	thisNode->val.b_val.b_type = op;
	thisNode->val.b_val.b_left_node = a;
	thisNode->val.b_val.b_right_node = b;
	//thisNode->val.b_val.b_parser = in_state;
	return thisNode;
}

ASTNode* mk_adt_assign(ParserState* in_state, ASTNode* a, ASTNode* b) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = ASSIGN_ADT_TYPE;
	thisNode->val.b_val.b_left_node = a;
	thisNode->val.b_val.b_right_node = b;
	//thisNode->val.b_val.b_parser = in_state;
	return thisNode;
}

ASTNode* mk_selection(
	ParserState* in_state, ASTNode* eval, ASTNode* a, ASTNode* b) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = IF_TYPE;
	thisNode->val.if_val.if_eval = eval;
	thisNode->val.if_val.if_left_node = a;
	thisNode->val.if_val.if_right_node = b;	
	return thisNode;
}

ASTNode* mk_loop(
	ParserState* in_state, ASTNode* eval, ASTNode* a) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = LOOP_TYPE;
	thisNode->val.l_val.l_eval = eval;
	thisNode->val.l_val.l_node = a;	
	return thisNode;
}

extern ASTNode* mk_for_loop(ParserState* in_state, 
		ASTNode* a, ASTNode* b, ASTNode* c, ASTNode* d) {
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}	
	thisNode->type = FOR_LOOP_TYPE;
	thisNode->val.for_val.for_node_1 = a;
	thisNode->val.for_val.for_node_2 = b;
	thisNode->val.for_val.for_node_3 = c;
	thisNode->val.for_val.for_node_4 = d;
	return thisNode;
}

ASTNode* mk_rpc(ParserState* in_state, 
		ASTNode* dest, string* func_name, ASTNode* paramAST) {	
	ASTNode* thisNode = in_state->get_var_table()->new_stack_ast();
	if (thisNode == NULL) {
		return in_state->empty_token();	
	}
	thisNode->type = RPC_TYPE;
	thisNode->val.rpc_val.rpc_dest = dest;
	thisNode->val.rpc_val.rpc_func_name = func_name; 
		// Func names should have global scope, so we can just pass the ptr	
	thisNode->val.rpc_val.rpc_param = paramAST;
	return thisNode;	
}

ASTNode* unmarshal_ast(ParserState* ps, BufferWrapper* bw) {
	ASTNode* in_node = ps->get_var_table()->new_stack_ast();
	if (in_node == NULL) {
		return ps->empty_token();	
	}	
	in_node->type = (ASTType)(ntohl(bw->retrieve_int()));
	switch (in_node->type) {
	case EMPTY_TYPE: {
			// Do nothing
		} break;		
	case INT_TYPE: {
			in_node->val.i_val = ntohl(bw->retrieve_int());
		}
		break;
	case DOUBLE_TYPE: {
			char* buf = const_cast<char*>(bw->retrieve_buf(sizeof(double)));
			if (bw->error()) { 
				return ps->empty_token(); 
			}			
			XDR xdrsDecode;						
			xdrmem_create(&xdrsDecode, buf, sizeof(double), XDR_DECODE);
			if (xdr_double(&xdrsDecode, &(in_node->val.d_val)) == 0) {
				return ps->empty_token();
			}			
		}
		break;		
	case STRING_TYPE: {			
			u_int string_size = ntohl(bw->retrieve_uint());
			in_node->val.s_val = ps->get_var_table()->new_stack_string();
			if (in_node->val.s_val == NULL) {
				return ps->empty_token();
			}
			const char* buf = bw->retrieve_buf(string_size);
			if (bw->error()) {
				return ps->empty_token();
			}
			in_node->val.s_val->append(buf, string_size);
		}		
		break;
	case ARRAY_TYPE: {
			in_node->val.a_val.a_type = (ASTType)(ntohl(bw->retrieve_int()));			
			// Retrieve string size
			u_int string_size = ntohl(bw->retrieve_uint());
			if (string_size > 0 ) {			
				string* tmp_string = ps->get_var_table()->new_stack_string();
				if (tmp_string == NULL) {
					return ps->empty_token();	
				}
				// Retrieve string
				const char* buf = bw->retrieve_buf(string_size);
				if (bw->error()) {
					return ps->empty_token();
				}			
				tmp_string->append(buf, string_size);
				in_node->val.a_val.a_adt_name = tmp_string;
			}
			// Retrieve vector size
			u_int vector_size = ntohl(bw->retrieve_uint());
			in_node->val.a_val.a_vector 
				= ps->get_var_table()->new_stack_vector();
			if (in_node->val.a_val.a_vector == NULL) {
				return ps->empty_token();
			}
			for (u_int i = 0; i < vector_size; i++) {
				ASTNode* nextNode = unmarshal_ast(ps, bw);
				if (nextNode->type == EMPTY_TYPE) {
					return ps->empty_token();
				}
				in_node->val.a_val.a_vector->push_back(nextNode);
			}
		}
		break;
	case VAR_ADT_TYPE: {
			// Retrieve string size
			u_int string_size = ntohl(bw->retrieve_uint());
			string* tmp_string = ps->get_var_table()->new_stack_string();
			if (tmp_string == NULL) {
				return ps->empty_token();	
			}
			// Retrieve string
			const char* buf = bw->retrieve_buf(string_size);
			if (bw->error()) {
				return ps->empty_token();
			}			
			tmp_string->append(buf, string_size);
			in_node->val.adt_val.adt_type_name = tmp_string;
			//	Get map size
			u_int map_size = ntohl(bw->retrieve_uint());
			//	Create map
			in_node->val.adt_val.adt_map 
				= ps->get_var_table()->new_stack_map();
			if (in_node->val.adt_val.adt_map == NULL) {
				return ps->empty_token();				
			}
			for (u_int i = 0; i < map_size; i++) {
				u_int map_string_size = ntohl(bw->retrieve_uint());
				// Retrieve string
				const char* map_buf = bw->retrieve_buf(map_string_size);
				if (bw->error()) {
					return ps->empty_token();
				}
				string map_string(map_buf, map_string_size);
				ASTNode* map_node = unmarshal_ast(ps, bw);
				if (map_node->type == EMPTY_TYPE) {
					return ps->empty_token();
				}
				(*(in_node->val.adt_val.adt_map))[map_string] = map_node;				
			}
		}
		break;
	case SEP_TYPE: {
			// Retrieve vector size
			u_int vector_size = ntohl(bw->retrieve_uint());
			// Create new stack vector
			in_node->val.p_val.p_vector 
				= ps->get_var_table()->new_stack_vector();
			if (in_node->val.p_val.p_vector == NULL) {
				return ps->empty_token();	
			}
			for (u_int i = 0; i < vector_size; i++) {
				ASTNode* nextNode = unmarshal_ast(ps, bw);
				if (nextNode->type == EMPTY_TYPE) {
					return ps->empty_token();
				}
				in_node->val.p_val.p_vector->push_back(nextNode);
			}
		}
		break;			
	default:
		DSL_ERROR("Cannot marshal non-base type AST\n");
		return ps->empty_token();
	}
	//	Check if we encountered any error
	if (bw->error()) { 
		return ps->empty_token(); 
	}		
	return in_node;
}

int marshal_ast(const ASTNode* in_node, RealPacket* in_packet) {
	//	Append the type of the AST
	in_packet->append_int(htonl(in_node->type));
	switch (in_node->type) {
	case EMPTY_TYPE: {
			// Do nothing
		} break;
	case INT_TYPE: {			
			in_packet->append_int(htonl(in_node->val.i_val));
		}
		break;
	case DOUBLE_TYPE: {
			// I don't know how to encode DOUBLEs, so I'm going to use XDR
			XDR xdrsEncode;
			char buf[sizeof(double)];			
			xdrmem_create(&xdrsEncode, buf, sizeof(double), XDR_ENCODE);
			double tmpDouble = in_node->val.d_val;	// For const reasons
			//if (xdr_double(&xdrsEncode, &(in_node->val.d_val)) == 0) {
			if (xdr_double(&xdrsEncode, &(tmpDouble)) == 0) {
				return -1;
			}
			in_packet->append_str(buf, sizeof(double));			
		}
		break;
	case STRING_TYPE: {
			u_int string_size = in_node->val.s_val->size();
			in_packet->append_uint(htonl(string_size));
			in_packet->append_str(in_node->val.s_val->c_str(), string_size);
		}		
		break;
	case ARRAY_TYPE: {
			// Array entries type
			in_packet->append_int(htonl(in_node->val.a_val.a_type));
			// String length and adt string
			if (in_node->val.a_val.a_type == ADT_TYPE) {
				u_int string_size = in_node->val.a_val.a_adt_name->size();
				in_packet->append_uint(htonl(string_size));			
				in_packet->append_str(in_node->val.a_val.a_adt_name->c_str(), 
					string_size);
			} else {
				in_packet->append_uint(htonl(0));
			}
			//	Vector length and entries in vector
			u_int vector_size = in_node->val.a_val.a_vector->size();
			in_packet->append_uint(htonl(vector_size));
			for (u_int i = 0; i < vector_size; i++) {
				if (marshal_ast(
						(*(in_node->val.a_val.a_vector))[i], in_packet) == -1) {
					DSL_ERROR("Error marshaling ast #%d\n", i);
					return -1;
				}
			}
		}
		break;
	case VAR_ADT_TYPE: {
			// String length and adt string 				
			u_int string_size = in_node->val.adt_val.adt_type_name->size();
			in_packet->append_uint(htonl(string_size));			
			in_packet->append_str(in_node->val.adt_val.adt_type_name->c_str(),
				string_size);
			//	NOTE: adt_param does NOT need to me marshalled, as it is
			//	used in ADT_TYPE, not VAR_ADT_TYPE			
			//	Map size and entries in map
			u_int map_size = in_node->val.adt_val.adt_map->size();
			in_packet->append_uint(htonl(map_size));
			map<string, ASTNode*>::iterator map_it = 
				in_node->val.adt_val.adt_map->begin();
			for (; map_it != in_node->val.adt_val.adt_map->end(); map_it++) {
				u_int map_str_size = (map_it->first).size();
				in_packet->append_uint(htonl(map_str_size));			
				in_packet->append_str((map_it->first).c_str(), map_str_size);				
				if (marshal_ast(map_it->second, in_packet) == -1) {
					DSL_ERROR("Error marshaling ast\n");
					return -1;
				}				
			}		
		}
		break;
	case SEP_TYPE: {
			//	Vector length and entries in vector (note unsigned int)
			u_int vector_size = in_node->val.p_val.p_vector->size();
			in_packet->append_uint(htonl(vector_size));
			for (u_int i = 0; i < vector_size; i++) {
				if (marshal_ast(
						(*(in_node->val.p_val.p_vector))[i], in_packet) == -1) {
					return -1;
				}
			}
		}
		break;	
	default:
		DSL_ERROR("Cannot marshal non-base type AST\n");
		return -1;
	}
	// See if any error occured during packet construction	
	if (!(in_packet->completeOkay())) {
		DSL_ERROR("marshal_ast: packet too small for ast\n");
		return -1;
	}
	return 0;	
}

int arrayLargestOffset(ASTType type, 
		const vector<ASTNode*>* curVect, int* offset) {
	//	Empty INT or DOUBLE array gets offset of -1
	if ((type == INT_TYPE || type == DOUBLE_TYPE) && curVect->size() == 0) {
		*offset = -1;
		return 0;
	}
	if (type == INT_TYPE) {
		int maxValue = INT_MIN;
		*offset = 0;				
		for (u_int i = 0; i < curVect->size(); i++) {
			ASTNode* curVectNode = (*curVect)[i];
			if (curVectNode->type != INT_TYPE) {
				DSL_ERROR("Ill-formed array\n");
				return -1;
			}			
			if (curVectNode->val.i_val > maxValue) {
				maxValue = curVectNode->val.i_val;
				*offset = i;
			}
		}
		return 0;
	} else if (type == DOUBLE_TYPE) {
		double maxValue = -INFINITY;
		*offset = 0;
		for (u_int i = 0; i < curVect->size(); i++) {
			ASTNode* curVectNode = (*curVect)[i];
			if (curVectNode->type != DOUBLE_TYPE) {
				DSL_ERROR("Ill-formed array\n");
				return -1;
			}
			if (curVectNode->val.d_val > maxValue) {
				maxValue = curVectNode->val.d_val;
				*offset = i;						
			}
		}
		return 0;				
	}
	DSL_ERROR("Array type must be int or double\n");
	return -1;	
}

int arraySmallestOffset(ASTType type, 
		const vector<ASTNode*>* curVect, int* offset) {
	//	Empty INT or DOUBLE array gets offset of -1
	if ((type == INT_TYPE || type == DOUBLE_TYPE) && curVect->size() == 0) {
		*offset = -1;
		return 0;
	}
	if (type == INT_TYPE) {
		int minValue = INT_MAX;
		*offset = 0;				
		for (u_int i = 0; i < curVect->size(); i++) {
			ASTNode* curVectNode = (*curVect)[i];
			if (curVectNode->type != INT_TYPE) {
				DSL_ERROR("Ill-formed array\n");
				return -1;
			}			
			if (curVectNode->val.i_val < minValue) {
				minValue = curVectNode->val.i_val;
				*offset = i;
			}
		}
		return 0;
	} else if (type == DOUBLE_TYPE) {
		double minValue = INFINITY;
		*offset = 0;
		for (u_int i = 0; i < curVect->size(); i++) {
			ASTNode* curVectNode = (*curVect)[i];
			if (curVectNode->type != DOUBLE_TYPE) {
				DSL_ERROR("Ill-formed array\n");
				return -1;
			}
			if (curVectNode->val.d_val < minValue) {
				minValue = curVectNode->val.d_val;
				*offset = i;						
			}
		}
		return 0;				
	}
	DSL_ERROR("Array type must be int or double\n");
	return -1;	
}

int uniqueAdd(ParserState* ps, 
		vector<ASTNode*>* retVect, const ASTNode* in_node) {
	for (u_int i = 0; i < retVect->size(); i++) {
		if (ADTEqual(in_node, (*retVect)[i])) {
			return 0;
		}
	}
	ASTNode* tmpNode = ADTCreateAndCopy(ps, in_node);
	if (tmpNode->type == EMPTY_TYPE) {
		return -1;
	}
	retVect->push_back(tmpNode);
	return 0;			
}

int uniqueAdd(ParserState* ps, 
		vector<ASTNode*>* retVect, const vector<ASTNode*>* in_vect) {
	for (u_int i = 0; i < in_vect->size(); i++) {
		if (uniqueAdd(ps, retVect, (*in_vect)[i]) == -1) {
			return -1;	
		}
	}
	return 0;
}

int unmarshal_packet(ParserState& ps, BufferWrapper& bw) {
	// Actual program text size
	uLongf prog_size = ntohl(bw.retrieve_uint());
#define MAX_PROGRAM_SIZE	10000
	if (prog_size > MAX_PROGRAM_SIZE) {
		DSL_ERROR("unmarshal_packet: received program size too large\n");
		return -1;	
	}
	// Compressed size store in packet
	u_int comp_prog_size = ntohl(bw.retrieve_uint());
	//const char* buf = bw.retrieve_buf(prog_size);
	const char* buf = bw.retrieve_buf(comp_prog_size);
	if (bw.error()) {
		return -1;
	}
	// Retrieve function name that will be called
	u_int string_size = ntohl(bw.retrieve_uint());
	const char* func_name = bw.retrieve_buf(string_size);
	if (bw.error()) {
		return -1;
	}
	ps.set_func_string(func_name);			// Set func name to be called		
	ps.set_param(unmarshal_ast(&ps, &bw));	// Unmarshal parameters
	// Allocate temp buffer 	
	char* tmpCompBuf = (char*)malloc(prog_size);
	if (tmpCompBuf == NULL) {
		DSL_ERROR("Cannot allocate temporary decompress buffer\n");
		return -1;	
	}	
	if (uncompress((Bytef*)tmpCompBuf, (uLongf*)&prog_size, 
			(Bytef*)buf, comp_prog_size) != Z_OK) {
		DSL_ERROR("zlib decompression failed\n");
		free(tmpCompBuf);
		return -1;
	}	
	// Copy program over to separate buffer	
	if (ps.input_buffer.create_buffer(prog_size) == -1) {    
		DSL_ERROR("Cannot create buffer\n");
		free(tmpCompBuf);
		return -1;
	}
	//memcpy(ps.input_buffer.get_raw_buf(), buf, prog_size);
	memcpy(ps.input_buffer.get_raw_buf(), tmpCompBuf, prog_size);
	free(tmpCompBuf);	// Done with buffer	
	//ps.input_buffer.delete_buffer();
	return 0;
}

ASTNode* evalNativeFunctions(
		ParserState* ps, ASTNode* cur_node, int recurse_count) {
	switch (cur_node->val.n_val.n_type) {
		case ROUND:	{	
			ASTNode* nextNode = eval(ps, 
				cur_node->val.n_val.n_param_1,	recurse_count + 1);
			if (nextNode->type != DOUBLE_TYPE) {
				DSL_ERROR("Double type expected\n");
				return ps->empty_token();
			}
			ASTNode* ret = mk_int(ps, lrint(nextNode->val.d_val));
/*					
			if (ret == NULL) {
				DSL_ERROR("Out of memory\n");
				ret = ps->empty_token();
			}
*/			
			return ret;
		}			
		case CEIL: {	
			ASTNode* nextNode = eval(ps, 
				cur_node->val.n_val.n_param_1,	recurse_count + 1);
			if (nextNode->type != DOUBLE_TYPE) {
				DSL_ERROR("Double type expected\n");
				return ps->empty_token();
			}
			ASTNode* ret = mk_int(ps, (int)ceil(nextNode->val.d_val));
/*			
			if (ret == NULL) {
				DSL_ERROR("Out of memory\n");
				ret = ps->empty_token();
			}
*/			
			return ret;
		}
		case FLOOR: {	
			ASTNode* nextNode = eval(ps, 
				cur_node->val.n_val.n_param_1,	recurse_count + 1);
			if (nextNode->type != DOUBLE_TYPE) {
				DSL_ERROR("Double type expected\n");
				return ps->empty_token();
			}
			ASTNode* ret = mk_int(ps, (int)floor(nextNode->val.d_val));
/*			
			if (ret == NULL) {
				DSL_ERROR("Out of memory\n");
				ret = ps->empty_token();
			}
*/			
			return ret;
		}		
		case SIN: {	
			ASTNode* nextNode = eval(ps, 
				cur_node->val.n_val.n_param_1,	recurse_count + 1);
			if (nextNode->type != DOUBLE_TYPE) {
				DSL_ERROR("Double type expected\n");
				return ps->empty_token();
			}
			ASTNode* ret = mk_double(ps, sin(nextNode->val.d_val));
/*			
			if (ret == NULL) {
				DSL_ERROR("Out of memory\n");
				ret = ps->empty_token();
			}
*/			
			return ret;
		}		
		case COS: {	
			ASTNode* nextNode = eval(ps, 
				cur_node->val.n_val.n_param_1,	recurse_count + 1);
			if (nextNode->type != DOUBLE_TYPE) {
				DSL_ERROR("Double type expected\n");
				return ps->empty_token();
			}
			ASTNode* ret = mk_double(ps, cos(nextNode->val.d_val));
/*			
			if (ret == NULL) {
				DSL_ERROR("Out of memory\n");
				ret = ps->empty_token();
			}
*/			
			return ret;
		}
		case TAN: {	
			ASTNode* nextNode = eval(ps, 
				cur_node->val.n_val.n_param_1,	recurse_count + 1);
			if (nextNode->type != DOUBLE_TYPE) {
				DSL_ERROR("Double type expected\n");
				return ps->empty_token();
			}
			ASTNode* ret = mk_double(ps, tan(nextNode->val.d_val));
/*			
			if (ret == NULL) {
				DSL_ERROR("Out of memory\n");
				ret = ps->empty_token();
			}
*/			
			return ret;
		}
		case ASIN: {	
			ASTNode* nextNode = eval(ps, 
				cur_node->val.n_val.n_param_1,	recurse_count + 1);
			if (nextNode->type != DOUBLE_TYPE) {
				DSL_ERROR("Double type expected\n");
				return ps->empty_token();
			}
			ASTNode* ret = mk_double(ps, asin(nextNode->val.d_val));
/*			
			if (ret == NULL) {
				DSL_ERROR("Out of memory\n");
				ret = ps->empty_token();
			}
*/			
			return ret;
		}		
		case ACOS: {	
			ASTNode* nextNode = eval(ps, 
				cur_node->val.n_val.n_param_1,	recurse_count + 1);
			if (nextNode->type != DOUBLE_TYPE) {
				DSL_ERROR("Double type expected\n");
				return ps->empty_token();
			}
			ASTNode* ret = mk_double(ps, acos(nextNode->val.d_val));
/*			
			if (ret == NULL) {
				DSL_ERROR("Out of memory\n");
				ret = ps->empty_token();
			}
*/			
			return ret;
		}
		case ATAN: {	
			ASTNode* nextNode = eval(ps, 
				cur_node->val.n_val.n_param_1,	recurse_count + 1);
			if (nextNode->type != DOUBLE_TYPE) {
				DSL_ERROR("Double type expected\n");
				return ps->empty_token();
			}
			ASTNode* ret = mk_double(ps, atan(nextNode->val.d_val));
/*			
			if (ret == NULL) {
				DSL_ERROR("Out of memory\n");
				ret = ps->empty_token();
			}
*/			
			return ret;
		}
		case LOG_OP: {	
			ASTNode* nextNode = eval(ps, 
				cur_node->val.n_val.n_param_1,	recurse_count + 1);
			if (nextNode->type != DOUBLE_TYPE) {
				DSL_ERROR("Double type expected\n");
				return ps->empty_token();
			}
			ASTNode* ret = mk_double(ps, log(nextNode->val.d_val));
/*			
			if (ret == NULL) {
				DSL_ERROR("Out of memory\n");
				ret = ps->empty_token();
			}
*/			
			return ret;
		}	
		case EXP: {	
			ASTNode* nextNode = eval(ps, 
				cur_node->val.n_val.n_param_1,	recurse_count + 1);
			if (nextNode->type != DOUBLE_TYPE) {
				DSL_ERROR("Double type expected\n");
				return ps->empty_token();
			}
			ASTNode* ret = mk_double(ps, exp(nextNode->val.d_val));
/*			
			if (ret == NULL) {
				DSL_ERROR("Out of memory\n");
				ret = ps->empty_token();
			}
*/			
			return ret;
		}			
		case POW: {	
			ASTNode* nextNode_1 = eval(ps, 
				cur_node->val.n_val.n_param_1,	recurse_count + 1);
			ASTNode* nextNode_2 = eval(ps, 
				cur_node->val.n_val.n_param_2,	recurse_count + 1);					
			if (nextNode_1->type != DOUBLE_TYPE || 
				nextNode_2->type != DOUBLE_TYPE) {
				DSL_ERROR("Double type expected\n");
				return ps->empty_token();
			}
			ASTNode* ret = mk_double(ps, 
				pow(nextNode_1->val.d_val, nextNode_2->val.d_val));
/*				
			if (ret == NULL) {
				DSL_ERROR("Out of memory\n");
				ret = ps->empty_token();
			}
*/			
			return ret;
		}			
		case DBL: {	
			ASTNode* nextNode = eval(ps, 
				cur_node->val.n_val.n_param_1,	recurse_count + 1);
			if (nextNode->type != INT_TYPE) {
				DSL_ERROR("Integer type expected\n");
				return ps->empty_token();
			}
			ASTNode* ret = mk_double(ps, (double)(nextNode->val.i_val));
/*			
			if (ret == NULL) {
				DSL_ERROR("Out of memory\n");
				ret = ps->empty_token();
			}
*/			
			return ret;
		}
		case ARRAY_SIZE: {	
			ASTNode* nextNode = eval(ps, 
				cur_node->val.n_val.n_param_1,	recurse_count + 1);
			if (nextNode->type != ARRAY_TYPE) {
				DSL_ERROR("Array type expected\n");
				return ps->empty_token();
			}
			ASTNode* ret = mk_int(ps, nextNode->val.a_val.a_vector->size());
/*			
			if (ret == NULL) {
				DSL_ERROR("Out of memory\n");
				ret = ps->empty_token();
			}
*/			
			return ret;
		}
		case DNS_LOOKUP: {
			return handleDNSLookup(ps, cur_node, recurse_count);		
		}
		case DNS_ADDR: {	
			ASTNode* nextNode = eval(ps, 
				cur_node->val.n_val.n_param_1,	recurse_count + 1);
			if (nextNode->type != INT_TYPE) {
				DSL_ERROR("Double type expected\n");
				return ps->empty_token();
			}
			uint32_t tmpAddr = htonl(nextNode->val.i_val);
			char* addrStr = inet_ntoa(*((struct in_addr*)&tmpAddr));
			if (addrStr == NULL) {
				DSL_ERROR("inet_ntoa failed\n");
				return ps->empty_token();				
			}
			string tmpString = addrStr;	
			// mk_string creates a copy of the string;	
			ASTNode* ret = mk_string(ps, &tmpString);
/*			
			if (ret == NULL) {
				DSL_ERROR("Out of memory\n");
				ret = ps->empty_token();
			}
*/			
			return ret;
		}
		case PUSH_BACK: {	
			ASTNode* nextNode_1 = eval(ps, 
				cur_node->val.n_val.n_param_1, recurse_count + 1);
			ASTNode* nextNode_2 = eval(ps, 
				cur_node->val.n_val.n_param_2, recurse_count + 1);				
			if (nextNode_1->type != ARRAY_TYPE) {
				DSL_ERROR("Array type expected\n");
				return ps->empty_token();
			}
			if (nextNode_1->val.a_val.a_type == ADT_TYPE) {
				if (*(nextNode_1->val.a_val.a_adt_name) != 
						*(nextNode_2->val.adt_val.adt_type_name)) {
					DSL_ERROR("Wrong array type encountered\n");					
					return ps->empty_token();
				}
			} else if (nextNode_1->val.a_val.a_type != nextNode_2->type) {
				DSL_ERROR("Wrong array type encountered\n");
				return ps->empty_token();											
			}			
			ASTNode* newASTNode = 
				nextNode_1->val.a_val.a_var_table->ASTCreate(
					nextNode_1->val.a_val.a_type, 
					nextNode_1->val.a_val.a_adt_name, 
					EMPTY_TYPE, 0);
			if (newASTNode == NULL) {
				return ps->empty_token();			
			}
			if (ps->get_var_table()->updateADT(newASTNode, nextNode_2) == -1) {
				DSL_ERROR("Array entry assign failed\n");
				return ps->empty_token();	
			}
			nextNode_1->val.a_val.a_vector->push_back(newASTNode);
			return ps->empty_token();
		}
		case POP_BACK: {
			ASTNode* nextNode = eval(ps, 
				cur_node->val.n_val.n_param_1, recurse_count + 1);
			if (nextNode->type != ARRAY_TYPE) {
				DSL_ERROR("Array type expected\n");
				return ps->empty_token();
			}
			if (!(nextNode->val.a_val.a_vector->empty())) {
				nextNode->val.a_val.a_vector->pop_back();
			}
			return ps->empty_token();
		}			
		case GET_SELF: {
			return handleGetSelf(ps);
		}
		case RING_GT: {
			return handleRingGT(ps, cur_node, recurse_count);
		}
		case RING_GE: {
			return handleRingGE(ps, cur_node, recurse_count);
		}
		case RING_LT: {
			return handleRingLT(ps, cur_node, recurse_count);
		}
		case RING_LE: {
			return handleRingLE(ps, cur_node, recurse_count);
		}		
		case GET_DISTANCE_TCP: {
			return handleGetDistTCP(ps, cur_node, recurse_count);
		}
		case GET_DISTANCE_DNS: {
			return handleGetDistDNS(ps, cur_node, recurse_count);
		}
		case GET_DISTANCE_PING: {
			return handleGetDistPing(ps, cur_node, recurse_count);
		}
		case GET_DISTANCE_ICMP: {
#ifdef PLANET_LAB_SUPPORT
			return handleGetDistICMP(ps, cur_node, recurse_count);
#else
			DSL_ERROR("ICMP not supported\n");
			return ps->empty_token();
#endif
		}
		case ARRAY_INTERSECT: {
			ASTNode* nextNode_1 = eval(ps, 
				cur_node->val.n_val.n_param_1, recurse_count + 1);
			ASTNode* nextNode_2 = eval(ps, 
				cur_node->val.n_val.n_param_2, recurse_count + 1);				
			if (nextNode_1->type != ARRAY_TYPE) {
				DSL_ERROR("Array type expected\n");
				return ps->empty_token();
			}
			if (nextNode_1->type != nextNode_2->type) {
				DSL_ERROR("Arrays must be of the same type\n");
				return ps->empty_token();					
			}
			if (nextNode_1->val.a_val.a_type == ADT_TYPE) {
				if (*(nextNode_1->val.a_val.a_adt_name) 
						!= *(nextNode_2->val.a_val.a_adt_name)) {
					DSL_ERROR("Arrays must be of the same type\n");
					return ps->empty_token();					
				}
			}
			//	Create return array
			ASTNode* retVar = ASTCreate(ps, ARRAY_TYPE, 
					nextNode_1->val.a_val.a_adt_name, 
					nextNode_1->val.a_val.a_type, 0);					
			if (retVar->type == EMPTY_TYPE) {
				return ps->empty_token();	
			}			
			vector<ASTNode*>* vect_1 = nextNode_1->val.a_val.a_vector;
			vector<ASTNode*>* vect_2 = nextNode_2->val.a_val.a_vector;
			for (u_int i = 0; i < vect_1->size(); i++) {
				for (u_int j = 0; j < vect_2->size(); j++) {				
					if (ADTEqual((*vect_1)[i], (*vect_2)[j])) {						
						if (uniqueAdd(ps, retVar->val.a_val.a_vector, 
								(*vect_1)[i]) == -1) {
							return ps->empty_token();
						}
						break;						
					}
				}
			}
			return retVar;
		}
		case ARRAY_MAX_OFFSET: {			
			ASTNode* nextNode = eval(ps, 
				cur_node->val.n_val.n_param_1, recurse_count + 1);				
			if (nextNode->type != ARRAY_TYPE) {
				DSL_ERROR("Array type expected\n");
				return ps->empty_token();
			}
			vector<ASTNode*>* curVect = nextNode->val.a_val.a_vector;
			int offset = 0;
			if (arrayLargestOffset(
					nextNode->val.a_val.a_type, curVect, &offset) == -1) {
				return ps->empty_token();
			}
			return mk_int(ps, offset);
		}
		case ARRAY_MIN_OFFSET: {
			ASTNode* nextNode = eval(ps, 
				cur_node->val.n_val.n_param_1, recurse_count + 1);				
			if (nextNode->type != ARRAY_TYPE) {
				DSL_ERROR("Array type expected\n");
				return ps->empty_token();
			}
			vector<ASTNode*>* curVect = nextNode->val.a_val.a_vector;
			int offset = 0;
			if (arraySmallestOffset(
					nextNode->val.a_val.a_type, curVect, &offset) == -1) {
				return ps->empty_token();
			}
			return mk_int(ps, offset);			
		}			
		case ARRAY_AVG: {
			ASTNode* nextNode = eval(ps, 
				cur_node->val.n_val.n_param_1, recurse_count + 1);				
			if (nextNode->type != ARRAY_TYPE) {
				DSL_ERROR("Array type expected\n");
				return ps->empty_token();
			}
			vector<ASTNode*>* curVect = nextNode->val.a_val.a_vector;
			if (curVect->size() == 0) {
				return mk_double(ps, 0.0);
			} 			
			if (nextNode->val.a_val.a_type == INT_TYPE) {
				double total = 0.0;				
				for (u_int i = 0; i < curVect->size(); i++) {
					ASTNode* curVectNode = (*curVect)[i];
					if (curVectNode->type != INT_TYPE) {
						DSL_ERROR("Ill-formed array\n");
						return ps->empty_token();	
					}
					total += (double)(curVectNode->val.i_val);
				}
				return mk_double(ps, total / (double)(curVect->size()));
			} else if (nextNode->val.a_val.a_type == DOUBLE_TYPE) {
				double total = 0.0;				
				for (u_int i = 0; i < curVect->size(); i++) {
					ASTNode* curVectNode = (*curVect)[i];
					if (curVectNode->type != DOUBLE_TYPE) {
						DSL_ERROR("Ill-formed array\n");
						return ps->empty_token();	
					}
					total += curVectNode->val.d_val;
				}
				return mk_double(ps, total / (double)(curVect->size()));
			} 
			DSL_ERROR("Must be int or double array\n");
			return ps->empty_token();
		}
		case ARRAY_MAX: {
			ASTNode* nextNode = eval(ps, 
				cur_node->val.n_val.n_param_1, recurse_count + 1);				
			if (nextNode->type != ARRAY_TYPE) {
				DSL_ERROR("Array type expected\n");
				return ps->empty_token();
			}
			vector<ASTNode*>* curVect = nextNode->val.a_val.a_vector;
			int offset = 0;
			if (arrayLargestOffset(
					nextNode->val.a_val.a_type, curVect, &offset) == -1) {
				return ps->empty_token();
			}
			// This should not be possible
			if (offset >= (int)(curVect->size())) {				
				return ps->empty_token(); 
			}
			switch (nextNode->val.a_val.a_type) {
				case INT_TYPE: {
					if (offset == -1) {
						return mk_int(ps, INT_MIN);
					}
					return mk_int(ps, (*curVect)[offset]->val.i_val);
				}
				case DOUBLE_TYPE: {
					if (offset == -1) {
						return mk_double(ps, -INFINITY);
					}
					return mk_double(ps, (*curVect)[offset]->val.d_val);
				}				
				default: 
					break;
			}
			return ps->empty_token();			
		}				
		case ARRAY_MIN: {
			ASTNode* nextNode = eval(ps, 
				cur_node->val.n_val.n_param_1, recurse_count + 1);				
			if (nextNode->type != ARRAY_TYPE) {
				DSL_ERROR("Array type expected\n");
				return ps->empty_token();
			}
			vector<ASTNode*>* curVect = nextNode->val.a_val.a_vector;
			int offset = 0;
			if (arraySmallestOffset(
					nextNode->val.a_val.a_type, curVect, &offset) == -1) {
				return ps->empty_token();
			}
			if (offset >= (int)(curVect->size())) {
				return ps->empty_token();
			}
			// This should not be possible
			switch (nextNode->val.a_val.a_type) {
				case INT_TYPE: {
					if (offset == -1) {
						return mk_int(ps, INT_MAX);
					}
					return mk_int(ps, (*curVect)[offset]->val.i_val);
				}
				case DOUBLE_TYPE: {
					if (offset == -1) {
						return mk_double(ps, INFINITY);
					}
					return mk_double(ps, (*curVect)[offset]->val.d_val);
				}				
				default: 
					break;
			}
			return ps->empty_token();	
		}
		case ARRAY_UNION: {
			ASTNode* nextNode_1 = eval(ps, 
				cur_node->val.n_val.n_param_1, recurse_count + 1);
			ASTNode* nextNode_2 = eval(ps, 
				cur_node->val.n_val.n_param_2, recurse_count + 1);				
			if (nextNode_1->type != ARRAY_TYPE) {
				DSL_ERROR("Array type expected\n");
				return ps->empty_token();
			}
			if (nextNode_1->type != nextNode_2->type) {
				DSL_ERROR("Arrays must be of the same type\n");
				return ps->empty_token();					
			}
			if (nextNode_1->val.a_val.a_type == ADT_TYPE) {
				if (*(nextNode_1->val.a_val.a_adt_name) 
						!= *(nextNode_2->val.a_val.a_adt_name)) {
					DSL_ERROR("Arrays must be of the same type\n");
					return ps->empty_token();					
				}
			}
			//	Create return array
			ASTNode* retVar = ASTCreate(ps, ARRAY_TYPE, 
					nextNode_1->val.a_val.a_adt_name, 
					nextNode_1->val.a_val.a_type, 0);					
			if (retVar->type == EMPTY_TYPE) {
				return ps->empty_token();	
			}
			vector<ASTNode*>* retVect = retVar->val.a_val.a_vector;
			if (uniqueAdd(ps, retVect, nextNode_1->val.a_val.a_vector) == -1) {
				return ps->empty_token();
			}
			if (uniqueAdd(ps, retVect, nextNode_2->val.a_val.a_vector) == -1) {
				return ps->empty_token();
			}			
			return retVar;
		}			
		default: {
			DSL_ERROR("Unexpected native function called\n");
		}			
	}
	return ps->empty_token();
}

void addGlobalModule(ParserState* ps) {
	string* adt_name 	= NULL;	
	string* fields_1 	= NULL;
	string* fields_2 	= NULL;
	string* fields_3 	= NULL;
	string* fields_4 	= NULL;
	string* fields_5 	= NULL;
	ASTNode* adt_fields	= NULL;	
	//	Set struct name
	if ((adt_name = ps->get_var_table()->new_stack_string()) == NULL) return;
	*adt_name = "Node";
	//	Name of struct fields	
	if ((fields_1 = ps->get_var_table()->new_stack_string()) == NULL) return;
	*fields_1 = "addr";
	if ((fields_2 = ps->get_var_table()->new_stack_string()) == NULL) return;
	*fields_2 = "port";
	if ((fields_3 = ps->get_var_table()->new_stack_string()) == NULL) return;
	*fields_3 = "rendvAddr";
	if ((fields_4 = ps->get_var_table()->new_stack_string()) == NULL) return;
	*fields_4 = "rendvPort";	
	//	Create fields AST
	adt_fields = mk_sep_list(ps, 
		mk_new_var(ps, INT_TYPE, fields_1));
	adt_fields->val.p_val.p_vector->push_back(
		mk_new_var(ps, INT_TYPE, fields_2));
	adt_fields->val.p_val.p_vector->push_back(
		mk_new_var(ps, INT_TYPE, fields_3));
	adt_fields->val.p_val.p_vector->push_back(
		mk_new_var(ps, INT_TYPE, fields_4));		
	// Add ADT to ps	
	eval(ps, mk_adt(ps, adt_name, adt_fields), 0);

	//	Set struct name
	if ((adt_name = ps->get_var_table()->new_stack_string()) == NULL) return;
	*adt_name = "Measurement";
	//	Name of struct fields	
	if ((fields_1 = ps->get_var_table()->new_stack_string()) == NULL) return;
	*fields_1 = "addr";
	if ((fields_2 = ps->get_var_table()->new_stack_string()) == NULL) return;
	*fields_2 = "port";
	if ((fields_3 = ps->get_var_table()->new_stack_string()) == NULL) return;
	*fields_3 = "rendvAddr";
	if ((fields_4 = ps->get_var_table()->new_stack_string()) == NULL) return;
	*fields_4 = "rendvPort";	
	if ((fields_5 = ps->get_var_table()->new_stack_string()) == NULL) return;
	//*fields_5 = "latency_ms";
	*fields_5 = "distance";
	//	Create fields AST
	adt_fields = mk_sep_list(ps, 
		mk_new_var(ps, INT_TYPE, fields_1));
	adt_fields->val.p_val.p_vector->push_back(
		mk_new_var(ps, INT_TYPE, fields_2));
	adt_fields->val.p_val.p_vector->push_back(
		mk_new_var(ps, INT_TYPE, fields_3));
	adt_fields->val.p_val.p_vector->push_back(
		mk_new_var(ps, INT_TYPE, fields_4));		
	adt_fields->val.p_val.p_vector->push_back(
		mk_new_var_array(ps, DOUBLE_TYPE, fields_5, mk_int(ps, 0))); 	
	// Add ADT to ps
	eval(ps, mk_adt(ps, adt_name, adt_fields), 0);		
}

int fillNodeIdentField(
		const char* in_string, const map<string, ASTNode*>* inMap, 
		int32_t* val) {
	map<string, ASTNode*>::const_iterator findIt = inMap->find(in_string);
	if (findIt == inMap->end() || findIt->second->type != INT_TYPE) {
		return -1;	
	}		
	*val = findIt->second->val.i_val;
	return 0;
}

int createNodeIdent(ASTNode* in_ast, NodeIdentRendv* in_NodeIdent) {
	if (in_ast->type != VAR_ADT_TYPE) {
		return -1;	
	}
	if (*(in_ast->val.adt_val.adt_type_name) != "Node") {
		return -1;
	}
	int32_t tmpVal;
	if (fillNodeIdentField("addr", in_ast->val.adt_val.adt_map, 
			&tmpVal) == -1) {				
		return -1;
	}
	in_NodeIdent->addr = (uint32_t)tmpVal;	
	if (fillNodeIdentField("port", in_ast->val.adt_val.adt_map,
			&tmpVal) == -1) {
		return -1;
	}
	in_NodeIdent->port = (uint16_t)tmpVal;
	if (fillNodeIdentField("rendvAddr", in_ast->val.adt_val.adt_map,
			&tmpVal) == -1) {
		return -1;
	}
	in_NodeIdent->addrRendv = (uint32_t)tmpVal;
	if (fillNodeIdentField("rendvPort", in_ast->val.adt_val.adt_map,
			&tmpVal) == -1) {		
		return -1;
	}
	in_NodeIdent->portRendv = (uint16_t)tmpVal;
	return 0;
}

// Returns empty on error
ASTNode* createNodeIdent(ParserState* ps, const NodeIdentRendv& in_ident) {
	string adtName = "Node";
	//	Lookup ADT
	ASTNode* adtNode = NULL;
	if (ps->get_var_table()->lookup(adtName, &adtNode) == -1
			|| adtNode->type != ADT_TYPE) {
		DSL_ERROR("Cannot find specified ADT\n");
		return ps->empty_token();
	}	
	ASTNode* retVar = ASTCreate(ps, ADT_TYPE, &adtName, VOID_TYPE, 0);
	if (retVar->type == EMPTY_TYPE) {
		return ps->empty_token();	
	}	
	vector<ASTNode*>* tmpList 
		= adtNode->val.adt_val.adt_param->val.p_val.p_vector;		
	for (u_int i = 0; i < tmpList->size(); i++) {
		ASTNode* field_node = (*tmpList)[i];
		string* curField = field_node->val.v_val.v_name;		
		map<string, ASTNode*>::iterator it = 
			retVar->val.adt_val.adt_map->find(*curField);
		if (it == retVar->val.adt_val.adt_map->end()) {
			DSL_ERROR("Cannot find required field type in Node\n");
			return ps->empty_token();			
		}
		ASTNode* tmpNode = it->second;
		if (tmpNode->type != INT_TYPE) {
			DSL_ERROR("Field of unexpected type\n");
			return ps->empty_token();
		}
		if (*curField == "addr") {
			tmpNode->val.i_val = in_ident.addr;	
		} else if (*curField == "port") {
			tmpNode->val.i_val = in_ident.port;			
		} else if (*curField == "rendvAddr") {
			tmpNode->val.i_val = in_ident.addrRendv;
		} else if (*curField == "rendvPort") {
			tmpNode->val.i_val = in_ident.portRendv;
		} else {
			DSL_ERROR("Unexpected field\n");
			return ps->empty_token();
		}				
	}
	return retVar;
}

// Returns empty on error
ASTNode* createNodeIdentLat(ParserState* ps, const NodeIdentRendv& in_ident, 
		const uint32_t* lat_us, u_int lat_size) {
	string adtName = "Measurement";			
	//	Lookup ADT
	ASTNode* adtNode = NULL;
	if (ps->get_var_table()->lookup(adtName, &adtNode) == -1
			|| adtNode->type != ADT_TYPE) {
		DSL_ERROR("Cannot find specified ADT\n");
		return ps->empty_token();
	}	
	ASTNode* retVar = ASTCreate(ps, ADT_TYPE, &adtName, VOID_TYPE, 0);
	if (retVar->type == EMPTY_TYPE) {
		return ps->empty_token();	
	}	
	vector<ASTNode*>* tmpList 
		= adtNode->val.adt_val.adt_param->val.p_val.p_vector;		
	for (u_int i = 0; i < tmpList->size(); i++) {
		ASTNode* field_node = (*tmpList)[i];
		string* curField = field_node->val.v_val.v_name;		
		map<string, ASTNode*>::iterator it = 
			retVar->val.adt_val.adt_map->find(*curField);
		if (it == retVar->val.adt_val.adt_map->end()) {
			DSL_ERROR(
				"Cannot find required field type in Measurement\n");
			return ps->empty_token();			
		}
		ASTNode* tmpNode = it->second;
		if (tmpNode->type == INT_TYPE) {
			if (*curField == "addr") {
				tmpNode->val.i_val = in_ident.addr;	
			} else if (*curField == "port") {
				tmpNode->val.i_val = in_ident.port;			
			} else if (*curField == "rendvAddr") {
				tmpNode->val.i_val = in_ident.addrRendv;
			} else if (*curField == "rendvPort") {
				tmpNode->val.i_val = in_ident.portRendv;		 
			} else {
				DSL_ERROR("Unexpected field\n");
				return ps->empty_token();
			}
		} else if (tmpNode->type == ARRAY_TYPE && 
				tmpNode->val.a_val.a_type == DOUBLE_TYPE) {
			//if (*curField == "latency_ms") {
			if (*curField == "distance") {
				//for (u_int j = 0; j < lat_us.size(); j++) {
				for (u_int j = 0; j < lat_size; j++) {
					ASTNode* tmpDoubleNode = 
						ASTCreate(ps, DOUBLE_TYPE, NULL, VOID_TYPE, 0);
					if (tmpDoubleNode->type == EMPTY_TYPE) {
						return ps->empty_token();
					}
					//	Lat contains latency in us, where distance is in ms
					//tmpDoubleNode->val.d_val = (lat_us[j] / 1000.0);
					tmpDoubleNode->val.d_val = (*(lat_us + j) / 1000.0);
					tmpNode->val.a_val.a_vector->push_back(tmpDoubleNode);
				}
			}				
		} else {
			DSL_ERROR("Unexpected field\n");
			return ps->empty_token();			
		}
	}
	return retVar;
}

void jmp_eval(ParserState* ps) {
	// Evaluate global variables
	ps->allocateEvalCount(-1);	// -1 means no limit
	// Global structs implemented in the language are added in here	
	addGlobalModule(ps);		
	// Add global structures and functions	
	eval(ps, ps->get_start(), 0);					
	ASTNode* main_node;
	if (ps->get_var_table()->lookup(
		*(ps->get_func_string()), &main_node) == -1) {
		DSL_ERROR("No main function found\n");
	} else {
		//	Set actual parameter
		main_node->val.f_val.f_actual_param = ps->get_param();
		ps->allocateEvalCount(10000);
		ASTNode* this_node = eval(ps, main_node, 0);
		ps->setQueryReturn(this_node);
	}	
	ps->set_parser_state(PS_DONE);
	setcontext(&global_env_thread);
}

ASTNode* eval(ParserState* ps, ASTNode* cur_node, int recurse_count) {
	if (recurse_count > MAX_RECURSE_COUNT) {
		DSL_ERROR("Maximum recurse count reached\n");
		return ps->empty_token();
	}
	if (ps->getEvalCount() != -1) {
		while (ps->getEvalCount() == 0) {
			ps->set_parser_state(PS_READY);
			swapcontext(ps->get_context(), &global_env_thread);
		}		
		ps->decrementEvalCount();		
	}	
	ps->set_parser_state(PS_RUNNING);	
	//	Determine what type of instruction is being evaluated
	switch(cur_node->type) {
		case EMPTY_TYPE: {
			//printf("Empty\n");
			return ps->empty_token();
		}
		case INT_TYPE: {
			//printf("int of value %d\n", cur_node->val.i_val);
			return cur_node;
		}
		case DOUBLE_TYPE: {
			//printf("double of value %0.2f\n", cur_node->val.d_val);
			return cur_node;
		}
		case VOID_TYPE: {
			DSL_ERROR("Cannot have a void type object (parser error)\n");
			return cur_node;
		}
		case VAR_ADT_TYPE: {
			//printf("identifier reference named %s\n", 
			//		cur_node->val.adt_val.adt_type_name->c_str());					
			return cur_node;			
		}
		case ARRAY_TYPE: {
			return cur_node;
		}		
		case NEW_VAR_TYPE: {
			//	Get array size by evaluating the expression
			int tmp_array_size = 0;
			if (cur_node->val.v_val.v_type == ARRAY_TYPE) {
				ASTNode* getArraySize = eval(
					ps, cur_node->val.v_val.v_array_size, recurse_count + 1);
				if (getArraySize->type != INT_TYPE) {
					return ps->empty_token();	
				}
				tmp_array_size = getArraySize->val.i_val;
			}
			ASTNode* newAst = variable_create(ps, 
				cur_node->val.v_val.v_type, 
				cur_node->val.v_val.v_adt_name,
				cur_node->val.v_val.v_array_type,
				//cur_node->val.v_val.v_array_size,
				tmp_array_size,					
				cur_node->val.v_val.v_name,
				false);				
			if (newAst->type == EMPTY_TYPE) {
				DSL_ERROR("Cannot create variable %s not found\n", 
					cur_node->val.v_val.v_name->c_str());		
			}			
			return newAst;
		}		
		case NEW_VAR_ASSIGN_TYPE: {		
			//printf("identifier declaration named %s with assignment\n", 
			//		cur_node->val.v_val.v_name->c_str());
			ASTNode* evalAst = NULL;
			//if (cur_node->val.v_val.v_type == ADT_TYPE && 
			if (cur_node->val.v_val.v_assign->type == SEP_TYPE) {
				vector<ASTNode*>* newArray = 
					ps->get_var_table()->new_stack_vector();
				if (newArray == NULL) {
					return ps->empty_token();					
				}
				vector<ASTNode*>* evalArray	=
					cur_node->val.v_val.v_assign->val.p_val.p_vector;
				for (u_int i = 0; i < evalArray->size(); i++) {
					newArray->push_back(
						eval(ps, (*evalArray)[i], recurse_count + 1));
				}				
				evalAst = mk_sep_list(ps, newArray);
			} else {
				evalAst = 
					eval(ps, cur_node->val.v_val.v_assign, recurse_count + 1);
			}
			if (evalAst->type == EMPTY_TYPE) {
				return evalAst;
			}
			// Create the actual variable after evaluation expression			
			ASTNode* newAst = variable_create(ps, 
					cur_node->val.v_val.v_type, 
					cur_node->val.v_val.v_adt_name,
					cur_node->val.v_val.v_array_type,
					//cur_node->val.v_val.v_array_size,
					// Should always be 0
					0,
					cur_node->val.v_val.v_name, false);
					
			if (newAst->type == EMPTY_TYPE) {
				DSL_ERROR("Cannot create variable %s not found\n", 
					cur_node->val.v_val.v_name->c_str());		
			}
			//	Assign the variable to the value generated by eval
			if (ps->get_var_table()->updateADT(newAst, evalAst) == -1) {
				DSL_ERROR("Assign failed on ADT\n");
				return ps->empty_token();
			}
			return newAst;			
			//variable_assign(ps,	
			//		cur_node->val.v_val.v_name, evalAst);
			//if (ret_node->type == EMPTY_TYPE) {
			//	DSL_ERROR("Error performing assign operation\n");
			//}			
			//return ret_node;
		}		
		case REF_VAR_TYPE: {
			//printf("identifier reference named %s\n", 
			//		cur_node->val.v_val.v_name->c_str());		
			ASTNode* retVar = NULL;
			if (ps->get_var_table()->lookup(
					*(cur_node->val.v_val.v_name), &retVar) == -1) {
				DSL_ERROR("Variable name %s not found\n", 
					cur_node->val.v_val.v_name->c_str());
				return ps->empty_token();
			}
			return retVar;
		}
		case REF_VAR_ARRAY_TYPE: {
			ASTNode* accessVar = 
				eval(ps, cur_node->val.v_val.v_access_ast, recurse_count + 1);				
			ASTNode* retVar = 
				eval(ps, cur_node->val.v_val.v_name_ast, recurse_count + 1);
			if (retVar->type != ARRAY_TYPE || accessVar->type != INT_TYPE) {
				DSL_ERROR("Invalid array access expression\n");
				return ps->empty_token();
			}
			if (accessVar->val.i_val < 0 || ((u_int)accessVar->val.i_val) 
					>= retVar->val.a_val.a_vector->size()) {
				DSL_ERROR("Array out of bounds\n");
				return ps->empty_token();	
			}
			return (*(retVar->val.a_val.a_vector))[accessVar->val.i_val];	
		}
		case REF_VAR_ADT_TYPE: {		
			ASTNode* retVar = 
				eval(ps, cur_node->val.v_val.v_name_ast, recurse_count + 1);
				
			if (retVar->type != VAR_ADT_TYPE) {				
				return ps->empty_token();
			}
			map<string, ASTNode*>::iterator findIt = 
				retVar->val.adt_val.adt_map->find(
					*(cur_node->val.v_val.v_name_dot_name));
			if (findIt == retVar->val.adt_val.adt_map->end()) {
				DSL_ERROR(
					"The field %s does not exist in ADT variable\n",
					cur_node->val.v_val.v_name_dot_name->c_str());			
				return ps->empty_token();
			}
			return findIt->second;
		}		
		case STRING_TYPE: {		
			//printf("String literal named %s\n", 
			//		cur_node->val.s_val->c_str());
			return cur_node;
		}
		case SEP_TYPE: {
			DSL_ERROR(
				"Should never call eval on SEP_TYPE (parser error)\n");
			return ps->empty_token();
		}
		case AST_TYPE: {
			//printf("AST node\n");
			ASTNode* this_node;
			this_node = 
				eval(ps, cur_node->val.b_val.b_right_node, recurse_count + 1);
			if (this_node->type == BREAK_TYPE ||
				this_node->type == CONTINUE_TYPE ||
				this_node->type == RETURN_TYPE) return this_node;
			this_node = 
				eval(ps, cur_node->val.b_val.b_left_node, recurse_count + 1);
			if (this_node->type == BREAK_TYPE ||
				this_node->type == CONTINUE_TYPE ||
				this_node->type == RETURN_TYPE) return this_node;
			return ps->empty_token();
		}
		case CONTEXT_TYPE: {
			//if (ps->get_var_table()->new_context() == -1) {
			if (ps->new_context() == -1) {				
				DSL_ERROR("Error creating context\n");
				return ps->empty_token();
			}
			ASTNode* this_node = 
				eval(ps, cur_node->val.u_val.u_node, recurse_count + 1);			
			//if (ps->get_var_table()->remove_context() == -1) {
			if (ps->remove_context() == -1) {				
				DSL_ERROR("Error deleting old context\n");
				return ps->empty_token();
			}
			if (this_node->type == BREAK_TYPE ||
				this_node->type == CONTINUE_TYPE ||
				this_node->type == RETURN_TYPE) return this_node;						
			return ps->empty_token();			
		}	
		case PRINT_TYPE: {
			ASTNode* newNode = 
				eval(ps, cur_node->val.u_val.u_node, recurse_count + 1);
			if (newNode->type == EMPTY_TYPE) {
				return newNode;
			}
			switch (newNode->type) {
				case INT_TYPE:
					printf("%d", newNode->val.i_val);
					break;
				case DOUBLE_TYPE:
					printf("%0.2f", newNode->val.d_val);
					break;                                                
				case STRING_TYPE: 
					printf("%s", (newNode->val.s_val)->c_str());
					break;
				default:
					DSL_ERROR(
						"Unknown type %d encountered (parser error)\n", 
						newNode->type);
					return ps->empty_token();					
			}
			return newNode;
		}
		case PRINTLN_TYPE: {
			ASTNode* newNode = 
				eval(ps, cur_node->val.u_val.u_node, recurse_count + 1);
			if (newNode->type == EMPTY_TYPE) {
				return newNode;
			}
			switch (newNode->type) {
				case INT_TYPE:
					printf("%d\n", newNode->val.i_val);
					break;
				case DOUBLE_TYPE:
					printf("%0.2f\n", newNode->val.d_val);
					break;                                                
				case STRING_TYPE: 
					printf("%s\n", (newNode->val.s_val)->c_str());
					break;
				default:
					DSL_ERROR(
						"Unknown type %d encountered (parser error)\n", 
						newNode->type);
					return ps->empty_token();					
			}
			return newNode;
		}		
		case UNARY_TYPE: {
			//printf("Unary operator\n");
			ASTNode* newNode = 
				eval(ps, cur_node->val.u_val.u_node, recurse_count + 1);
			if (newNode->type == EMPTY_TYPE) {
				return newNode;
			}
			if (cur_node->val.u_val.u_type == '-') {					
				switch(newNode->type) {
					case INT_TYPE:
						newNode->val.i_val = -1 * newNode->val.i_val;
						break;
					case DOUBLE_TYPE:
						newNode->val.d_val = -1.0 * newNode->val.d_val;
						break;
					case STRING_TYPE:
						DSL_ERROR(
							"Cannot perform unary operation on string\n");
						return ps->empty_token();
					default:
						DSL_ERROR(
							"Unknown type encountered (parser error)\n");
						return ps->empty_token();			
				}
			} else if (cur_node->val.u_val.u_type == '!') {
				if (newNode->type != INT_TYPE) {
					DSL_ERROR(
						"Cannot perform unary operation on non-integers\n");
					return ps->empty_token();						
				}
				newNode->val.i_val = !(newNode->val.i_val);
			} else {
				DSL_ERROR("Unknown type encountered (parser error)\n");
				return ps->empty_token();			
			}
			return newNode;			
		}			
		case BIN_TYPE: {
			//printf("Binary operator\n");
			ASTNode* r_node = 
				eval(ps, cur_node->val.b_val.b_right_node, recurse_count + 1);
			ASTNode* l_node = 
				eval(ps, cur_node->val.b_val.b_left_node, recurse_count + 1);			
			if (r_node->type == EMPTY_TYPE) { 
				return r_node;
			}
			if (l_node->type == EMPTY_TYPE) {
				return l_node;
			}
			ASTNode* retNode = arith_operation(ps, cur_node->val.b_val.b_type,
				l_node, r_node);
			if (retNode->type == EMPTY_TYPE) {
				DSL_ERROR("Error performing arith operation\n");
			}
			return retNode;
		}		
		case ASSIGN_ADT_TYPE: {
			//printf("assign operator\n");
			ASTNode* r_node = 
				eval(ps, cur_node->val.b_val.b_right_node, recurse_count + 1);
			ASTNode* l_node = 
				eval(ps, cur_node->val.b_val.b_left_node, recurse_count + 1);
			if (r_node->type == EMPTY_TYPE) { 
				return r_node;
			}
			if (l_node->type == EMPTY_TYPE) {
				return l_node;
			}
			if (ps->get_var_table()->updateADT(l_node, r_node) == -1) {
				DSL_ERROR("Assign failed on ADT\n");
				return ps->empty_token();
			}
			return l_node;
		}
		case RPC_TYPE: {
			ASTNode* destNode = 
				eval(ps, cur_node->val.rpc_val.rpc_dest, recurse_count + 1);

			if (destNode->type != VAR_ADT_TYPE ||
					*(destNode->val.adt_val.adt_type_name) != "Node") {
				DSL_ERROR("Destination must be a Node\n");
				return ps->empty_token();
			}

			ASTNode* addrAST = 
				(*(destNode->val.adt_val.adt_map))["addr"];
			ASTNode* portAST = 
				(*(destNode->val.adt_val.adt_map))["port"];
			ASTNode* rendvAddrAST = 
				(*(destNode->val.adt_val.adt_map))["rendvAddr"];
			ASTNode* rendvPortAST = 
				(*(destNode->val.adt_val.adt_map))["rendvPort"];
#if 0	
			// Should be an unnecessary check				
			if (!addrAST || !portAST || !rendvAddrAST || !rendvPortAST) {
				DSL_ERROR("Node definition inconsistent\n");
				return ps->empty_token();				
			}
			
			if (addrAST->type != INT_TYPE ||
				portAST->type != INT_TYPE ||
				rendvAddrAST->type != INT_TYPE ||
				rendvPortAST->type != INT_TYPE) {
				return ps->empty_token();		
			}
#endif						
			
			NodeIdentRendv tmpNodeIdent =  { 	
				addrAST->val.i_val, portAST->val.i_val,
				rendvAddrAST->val.i_val, rendvPortAST->val.i_val };
			
			vector<ASTNode*> actual_param;			
			// Currently can't handle return values
			if (cur_node->val.rpc_val.rpc_param->type == SEP_TYPE) {
				// IMPORTANT: Must save vector to f_param, as the eval can
				// actually change the value of the actual_parameter pointer
				vector<ASTNode*>* f_param = 
					cur_node->val.rpc_val.rpc_param->val.p_val.p_vector;
				for (u_int i = 0; i < f_param->size(); i++) {
					actual_param.push_back(
						eval(ps, (*f_param)[i], recurse_count + 1));
				}
			}
			ASTNode* sep_node;
			if (actual_param.size() == 0) {
				sep_node = ps->empty_token();				
			} else {
				sep_node = mk_sep_list(ps, &actual_param);			
			}
			return handleRPC(ps, tmpNodeIdent, 
				cur_node->val.rpc_val.rpc_func_name, sep_node);			
//			return handleRPC(ps, destNode->val.s_val, 
//				cur_node->val.rpc_val.rpc_func_name, sep_node);			
			//return ps->empty_token();
		}		
		case IF_TYPE: {
			//printf("if statement\n");
			ASTNode* e_node = 
				eval(ps, cur_node->val.if_val.if_eval, recurse_count + 1);				
			if (e_node->type != INT_TYPE) {
				return ps->empty_token();	// Must be int type to eval
			}
			ASTNode* this_node;
			if (e_node->val.i_val) {
				this_node = eval(ps, 
					cur_node->val.if_val.if_left_node, recurse_count + 1);
			} else {
				this_node = eval(ps, 
					cur_node->val.if_val.if_right_node, recurse_count + 1);
			}
			if (this_node->type == BREAK_TYPE ||
				this_node->type == CONTINUE_TYPE ||
				this_node->type == RETURN_TYPE) return this_node;			
			return ps->empty_token();		
		}
		case LOOP_TYPE: {
			//printf("loop statement\n");
			ASTNode* e_node = 
				eval(ps, cur_node->val.l_val.l_eval, recurse_count + 1);
			while (e_node->type == INT_TYPE && e_node->val.i_val) {
				ASTNode* this_node = 
					eval(ps, cur_node->val.l_val.l_node, recurse_count + 1);
				if (this_node->type == RETURN_TYPE) {
					return this_node;
				} else if (this_node->type == BREAK_TYPE) {
					break;
				}
				// Continues are ignored, as it has essentially been performed				
				e_node = 
					eval(ps, cur_node->val.l_val.l_eval, recurse_count + 1);
			}
			return ps->empty_token();								
		}
		case FOR_LOOP_TYPE: {
			// Initiator
			eval(ps, cur_node->val.for_val.for_node_1, recurse_count + 1);
			// Check to see if we should enter loop
			ASTNode* e_node = 
				eval(ps, cur_node->val.for_val.for_node_2, recurse_count + 1);
			while (e_node->type == INT_TYPE && e_node->val.i_val) {
				// Run actual compound statement
				ASTNode* this_node = eval(ps, 
					cur_node->val.for_val.for_node_4, recurse_count + 1);
				if (this_node->type == RETURN_TYPE) {
					return this_node;
				} else if (this_node->type == BREAK_TYPE) {
					break;
				}				
				// Continues are ignored, as it has essentially been performed
				// Evaluate 3rd statement
				eval(ps, cur_node->val.for_val.for_node_3, recurse_count + 1);
				// Check to see if we should still loop				
				e_node = eval(
					ps, cur_node->val.for_val.for_node_2, recurse_count + 1);
			}
			return ps->empty_token();								
		}		
		case BREAK_TYPE: {
			//printf("Break statement\n");
			return cur_node;
		}
		case CONTINUE_TYPE: { 
			//printf("Continue statement\n");
			return cur_node;
		}
		case FUNC_DECLARE_TYPE: {
			//printf("function declaration named %s\n", 
			//		cur_node->val.f_val.f_name->c_str());					
			ASTNode* new_node = mk_function_call(ps, 
				cur_node->val.f_val.f_type, cur_node->val.f_val.f_type_name,
				cur_node->val.f_val.f_array_type,
				cur_node->val.f_val.f_formal_param, ps->empty_token(), 
				cur_node->val.f_val.f_node);			
			//	Need to still fill in actual_parameters to perform call				
			if (ps->get_var_table()->insert(
					*(cur_node->val.f_val.f_name), new_node) == -1) {
				DSL_ERROR(
					"Function declaration clashes with another symbol\n");				
			}					
			return ps->empty_token();
		}		
		case FUNC_REF_TYPE: {
			ASTNode* call_func = NULL;
			if (ps->get_var_table()->lookup(
				*(cur_node->val.f_val.f_name), &call_func) == -1) {
				DSL_ERROR(
					"Cannot find function reference\n");				
				return 	ps->empty_token();
			}
			//	Transfer actual parameters
			call_func->val.f_val.f_actual_param = 
				cur_node->val.f_val.f_actual_param;			
			return eval(ps, call_func, recurse_count + 1);
		} 
		case FUNC_CALL_TYPE: {
			//	Create return type in the calling function's scope
			ASTNode* tmp_node = NULL;			 
			if (cur_node->val.f_val.f_type != VOID_TYPE) {				
				// 	NOTE: The overwrite flag must be on
				//	NOTE: Array size is always 0 initially
				tmp_node = ASTCreate(ps, cur_node->val.f_val.f_type,
					cur_node->val.f_val.f_type_name,
					cur_node->val.f_val.f_array_type, 0);
			}
			//	Push evaluated actual parameters into actual_param vector
			vector<ASTNode*> actual_param;
			if (cur_node->val.f_val.f_actual_param->type == SEP_TYPE) {
				// IMPORTANT: Must save vector to f_param, as the eval can
				// actually change the value of the actual_parameter pointer
				vector<ASTNode*>* f_param = 
					cur_node->val.f_val.f_actual_param->val.p_val.p_vector;
				for (u_int i = 0; i < f_param->size(); i++) {
					actual_param.push_back(
						eval(ps, (*f_param)[i], recurse_count + 1));
				}
			}
			//	Create new var table that can only access prev var_table's 
			//	global variables
			VarTable* old_table = NULL;
			if (ps->new_var_table(&old_table) == -1 || old_table == NULL) {
			//if (ps->new_var_table() == -1) {
				DSL_ERROR("Error creating function context\n");
				return ps->empty_token();
			}
			//	Insert the return AST into the current scope
			ASTNode* returnNode = NULL;
			if (tmp_node) {	// Create return value variable
				// NOTE: Array size always initially 0
				returnNode = variable_create(ps, 
					cur_node->val.f_val.f_type,
					cur_node->val.f_val.f_type_name,
					cur_node->val.f_val.f_array_type,
					0, ps->return_string(), false);
				//printf("Return node type is %d\n", 
				//	returnNode->type);				
			}
			//	Assign formal param to actual param
			ASTNode* this_node = NULL;
			if (param_list_assign(ps, cur_node->val.f_val.f_formal_param, 
					&actual_param) == -1) {
				DSL_ERROR("Error assigning to parameter list\n");
				this_node = ps->empty_token();				
			} else {
				// Evaluate function
				this_node = 
					eval(ps, cur_node->val.f_val.f_node, recurse_count + 1);
			}			
			if (tmp_node) {
				// Copy return value to temp variable. This is necessary
				// due to scoping unfortunately
				//printf("Return type is %d\n", returnNode->type);
				if (old_table->updateADT(
						tmp_node, returnNode) == -1) {
					DSL_ERROR(
						"Cannot update $TEMP_VALUE$ variable (parser error)\n");						
				}
				//printf("Pass tmp\n");
			}			
			//	Go back to previous var table
			if (ps->remove_var_table() == -1) {				
				DSL_ERROR("Error deleting function context\n");
				return ps->empty_token();
			}
			//	If a continue or a break floated up to here, error
			if (this_node->type == CONTINUE_TYPE) {
				DSL_ERROR("Continue called outside of a loop\n");
				return ps->empty_token();
			} else if (this_node->type == BREAK_TYPE) {
				DSL_ERROR("Break called outside of a loop\n");
				return ps->empty_token();
			}									
			if (tmp_node) {	
				return tmp_node; // A return value was given				
			}
			return ps->empty_token();
		}	
		case RETURN_TYPE: {
			ASTNode* e_node = 
				eval(ps, cur_node->val.u_val.u_node, recurse_count + 1);
			if (e_node->type != EMPTY_TYPE) {				
				variable_assign(ps, ps->return_string(), e_node);
			}			
			return cur_node;			
		}
		case DEF_ADT_TYPE: {
			cur_node->type = ADT_TYPE;
			if (ps->get_var_table()->insert(
					*(cur_node->val.adt_val.adt_type_name), cur_node) == -1) {
				DSL_ERROR(
					"ADT declaration clashes with another symbol\n");				
			}
			return ps->empty_token();			
		}
		case ADT_TYPE: {
			DSL_ERROR("Cannot eval ADT_TYPE\n");
			return ps->empty_token();
		}
		case NATIVE_FUNC_TYPE: {
			return evalNativeFunctions(ps, cur_node, recurse_count);		
		}
	}
	return ps->empty_token();
}

