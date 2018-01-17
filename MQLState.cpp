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

#include <assert.h>
#include "MQLState.h"

//#define MAX_POOL_SIZE 10
//extern void flatten_ast(vector<ASTNode*>* list, ASTNode* tree);
extern int updateBasicType(ASTNode* a, const ASTNode* b);

int VarTable::remove_var_map_context() {	
	v_map.clear();
	return 0;
}

int VarTable::remove_AST_context() {
	for (u_int i = 0; i < v_stack_ast.size(); i++) {
		v_ps->delete_ast(v_stack_ast[i]);
	}
	v_stack_ast.clear();
	return 0;	
}

int VarTable::remove_string_context() {
	for (u_int i = 0; i < v_stack_string.size(); i++) {	
		v_ps->delete_string(v_stack_string[i]);
	}
	v_stack_string.clear();
	return 0;	
}

int VarTable::remove_vector_context() {
	for (u_int i = 0; i < v_stack_vector.size(); i++) {
		v_ps->delete_vector(v_stack_vector[i]);
	}
	v_stack_vector.clear();
	return 0;	
}

int VarTable::remove_map_context() {
	for (u_int i = 0; i < v_stack_map.size(); i++) {
		v_ps->delete_map(v_stack_map[i]);			
	}
	v_stack_map.clear();
	return 0;	
}

bool VarTable::local_exists(const map<const string, ASTNode*>* in_map, 
		const string& in_string) {
	ASTNode* temp;
	if (local_lookup(in_map, in_string, &temp) == -1) {
		return false;
	}
	return true;
}

int VarTable::local_lookup(const map<const string, ASTNode*>* in_map,
		const string& in_string, ASTNode** in_type) {
	map<string, ASTNode*>::const_iterator it = in_map->find(in_string);
	if (it != in_map->end()) {
		*in_type = it->second;
		return 0; 
	}
	return -1;	
}	

int VarTable::lookup(const string& in_string, ASTNode** in_type) {
	if (local_lookup(&v_map, in_string, in_type) != -1) {					
		return 0;	
	}
	if (prev_var_table) {
		return prev_var_table->lookup(in_string, in_type);
	}
	return -1;
}

int VarTable::insert(const string& in_string, ASTNode* in_type) {
	if (!local_exists(&v_map, in_string)) {
		v_map[in_string] = in_type;
		return 0;
	}
	return -1;
}

int VarTable::local_remove(const string& in_string) {
	v_map.erase(in_string);
	return 0;
}

int VarTable::update(const string& in_string, const ASTNode* in_var) {
	ASTNode* retVar = NULL; 
	if (lookup(in_string, &retVar) == -1) {
		DSL_ERROR("Variable name %s not found\n", in_string.c_str());
		return -1;
	}
	return updateADT(retVar, in_var);
}

ASTNode* VarTable::new_stack_ast() {
	ASTNode* retAst = v_ps->new_ast();
	if (retAst == NULL) {
		return NULL;	
	}
	v_stack_ast.push_back(retAst);
	return retAst;
}

string* VarTable::new_stack_string() {
	string* retString = v_ps->new_string();
	if (retString == NULL) {
		return NULL;	
	}
	v_stack_string.push_back(retString);
	return retString;
}

vector<ASTNode*>* VarTable::new_stack_vector() {
	vector<ASTNode*>* retVector = v_ps->new_vector();
	if (retVector == NULL) {
		return NULL;	
	}
	v_stack_vector.push_back(retVector);	
	return retVector;
}

map<string, ASTNode*>* VarTable::new_stack_map() {
	map<string, ASTNode*>* retMap = v_ps->new_map();
	if (retMap == NULL) {
		return NULL;	
	}
	v_stack_map.push_back(retMap);	
	return retMap;
}

ASTNode* VarTable::create_basic_type(const ASTType in_type) {
	ASTNode* retVar = new_stack_ast();
	if (retVar == NULL) {
		return NULL;	
	}
	switch((retVar->type = in_type)){
	case INT_TYPE:
		retVar->val.i_val = 0;
		break;
	case DOUBLE_TYPE:
		retVar->val.d_val = 0.0;
		break;
	case STRING_TYPE:
		retVar->val.s_val = new_stack_string();
		break;
	case VOID_TYPE:
		DSL_ERROR("Cannot create void variable\n");
		return NULL;
	default:
		DSL_ERROR(
			"Unexpected type %d in definition (parser error)\n", retVar->type);
		return NULL;			
	}
	return retVar;
}

ASTNode* VarTable::ASTCreate(const ASTType in_type, 
		const string* adt_name, const ASTType array_type, int array_size) {
	ASTNode* retVar = NULL;
	if (in_type == ADT_TYPE) {
		if (!adt_name) {
			DSL_ERROR(
				"ADT_TYPE specified without ADT name (parser error)\n");
			return NULL;
		}
		//	Lookup ADT
		ASTNode* adtNode = NULL;
		if (lookup(*(adt_name), &adtNode) == -1
				|| adtNode->type != ADT_TYPE) {
			DSL_ERROR("Cannot find specified ADT\n");
			return NULL;
		}
		// Create variables for its members
		if (adtNode->val.adt_val.adt_param->type != SEP_TYPE) {
			DSL_ERROR("ADT contains no entries\n");
			return NULL;			
		}		
		retVar = new_stack_ast();
		if (retVar == NULL) {
			return NULL;	
		}
		retVar->type = VAR_ADT_TYPE;
		//	Type names should have global scope in current system
		//retVar->val.adt_val.adt_type_name = adt_name;
		retVar->val.adt_val.adt_type_name 
			= adtNode->val.adt_val.adt_type_name;
		retVar->val.adt_val.adt_map = new_stack_map();
		vector<ASTNode*>* tmpList 
			= adtNode->val.adt_val.adt_param->val.p_val.p_vector;		
		for (u_int i = 0; i < tmpList->size(); i++) {
			ASTNode* field_node = (*tmpList)[i];

			int tmp_array_size = 0;
			if (field_node->val.v_val.v_type == ARRAY_TYPE) {
				if (field_node->val.v_val.v_array_size->type != INT_TYPE) {
					DSL_ERROR("Arrays in structs can only "
						"have integer literals as sizes");
					return NULL;
				}
				tmp_array_size = field_node->val.v_val.v_array_size->val.i_val;
			}
			ASTNode* tmpNode = 
				ASTCreate(field_node->val.v_val.v_type,
					field_node->val.v_val.v_adt_name,
					field_node->val.v_val.v_array_type,
					//field_node->val.v_val.v_array_size);
					tmp_array_size);
			if (tmpNode == NULL) {
				DSL_ERROR("Create fields failed\n");
				return NULL;
			}
			(*(retVar->val.adt_val.adt_map))
				[*(field_node->val.v_val.v_name)] = tmpNode;
		}		
	} else if (in_type == ARRAY_TYPE) {
		if (array_size < 0) {
			DSL_ERROR("Array size must be >= 0\n");
			return NULL;	
		}
		const string* real_adt_name = NULL;
		if (array_type == ADT_TYPE) {
			//	Lookup ADT
			ASTNode* adtNode = NULL;
			if (lookup(*(adt_name), &adtNode) == -1
					|| adtNode->type != ADT_TYPE) {
				DSL_ERROR("Cannot find specified ADT\n");
				return NULL;
			}
			real_adt_name = adtNode->val.adt_val.adt_type_name;
		}			
		retVar = new_stack_ast();
		if (retVar == NULL) {
			return NULL;	
		}
		retVar->type = ARRAY_TYPE;
		//	Type names should have global scope in current system
		retVar->val.a_val.a_type = array_type;
		//retVar->val.a_val.a_adt_name = adt_name;
		retVar->val.a_val.a_adt_name = real_adt_name;
		retVar->val.a_val.a_var_table = this;
		retVar->val.a_val.a_vector = new_stack_vector();	
		for (int i = 0; i < array_size; i++) {
			ASTNode* tmpAST = ASTCreate(array_type, adt_name, EMPTY_TYPE, 0);
			if (tmpAST == NULL) {
				return NULL;
				//return tmpAST;
			}				
			retVar->val.a_val.a_vector->push_back(tmpAST);
		}		
	} else {
	 	retVar = create_basic_type(in_type);
	}
	return retVar;
}

int VarTable::updateADT(ASTNode* retVar, const ASTNode* in_var) {	
	if (retVar->type == VAR_ADT_TYPE && in_var->type == SEP_TYPE) {
		// Handling initialization of ADT with parameter list
		ASTNode* adt_name = NULL;
		if (lookup(
				*(retVar->val.adt_val.adt_type_name), &adt_name) == -1) {
			DSL_ERROR("Struct definition not found (parser error)\n");
			return -1;				
		}
		if (adt_name->val.adt_val.adt_param->type != SEP_TYPE) {
			DSL_ERROR(
				"Struct definition contains no entries (parser error)\n");
			return -1;									
		}
		vector<ASTNode*>* tmpList 
			= adt_name->val.adt_val.adt_param->val.p_val.p_vector;
		vector<ASTNode*>* paramListVector = in_var->val.p_val.p_vector;
		if (tmpList->size() != paramListVector->size()) {
			DSL_ERROR("Initiation list of incorrect size/type\n");
			return -1;
		}				
		for (u_int i = 0; i < tmpList->size(); i++) {					
			string* tmpString = (*tmpList)[i]->val.v_val.v_name;
			map<string, ASTNode*>::iterator findItA = 
				retVar->val.adt_val.adt_map->find(*tmpString);
			if (findItA	== retVar->val.adt_val.adt_map->end()) {
				DSL_ERROR(
					"Struct definition inconsistent (parser error)\n");
				return -1;							
			}
			//printf("Param type is %d\n", (*paramListVector)[i]->type);		
			if (updateADT(
					findItA->second, (*paramListVector)[i]) == -1) {
				return -1;
			}		
		}
		return 0;
	} 
	
	if (retVar->type == ARRAY_TYPE && in_var->type == SEP_TYPE) {
		retVar->val.a_val.a_vector->clear();
		vector<ASTNode*>* paramListVector = in_var->val.p_val.p_vector;
		for (u_int i = 0; i < paramListVector->size(); i++) {
			ASTNode* newASTNode = 
				retVar->val.a_val.a_var_table->ASTCreate(
					retVar->val.a_val.a_type, retVar->val.a_val.a_adt_name, 
					EMPTY_TYPE, 0);
			if (newASTNode == NULL) {
				return -1;				
			}
			if (updateADT(newASTNode, (*paramListVector)[i]) == -1) {
				DSL_ERROR("Array entry assign failed\n");
				return -1;	
			}
			retVar->val.a_val.a_vector->push_back(newASTNode);
		}
		return 0;
	}
	
	if (in_var->type != retVar->type) {
		DSL_ERROR("Assigning incompatible types %d from %d\n",
			retVar->type, in_var->type);		
		return -1;
	}
	//	Not a evaluation list initiation
	switch(retVar->type) {
		case INT_TYPE:
		case DOUBLE_TYPE:
		case STRING_TYPE:
			if (updateBasicType(retVar, in_var) == -1) {
				return -1;		
			}
			break;
		case ARRAY_TYPE: {
			if (retVar->val.a_val.a_type != in_var->val.a_val.a_type) {
				DSL_ERROR("Assigning incompatible types %d from %d\n",
					retVar->val.a_val.a_type, in_var->val.a_val.a_type);
				return -1;
			}			
			if (retVar->val.a_val.a_type == ADT_TYPE) {
				if (*(retVar->val.a_val.a_adt_name) != 
						*(in_var->val.a_val.a_adt_name)) {
					DSL_ERROR("Assigning incompatible types 1\n");
					return -1;
				}
			}
			retVar->val.a_val.a_vector->clear();
			for (u_int i = 0; i < (in_var->val.a_val.a_vector->size()); i++) {
				// NOTE: Arrays cannot store another array directly
				// Can be done by wrapping it with a struct
				ASTNode* newASTNode = retVar->val.a_val.a_var_table->ASTCreate(
						retVar->val.a_val.a_type, retVar->val.a_val.a_adt_name,
						EMPTY_TYPE, 0);
				if (newASTNode == NULL) {
					DSL_ERROR("ASTCreate failed\n");
					return -1;
				}
				ASTNode* insertNode = (*(in_var->val.a_val.a_vector))[i];
				if (updateADT(newASTNode, insertNode) == -1) {
					DSL_ERROR("Array entry assign failed\n");
					return -1;	
				}
				//printf("Pass\n");
				retVar->val.a_val.a_vector->push_back(newASTNode);				
			}
			break;
		}
		case VAR_ADT_TYPE: {			
			//retVar->val.adt_val.adt_name = adt_name;
			// Create variables for its members
			if (*(retVar->val.adt_val.adt_type_name) !=
					*(in_var->val.adt_val.adt_type_name)) {
				DSL_ERROR("Incorrect struct type assignment\n");
				return -1;
			}
			ASTNode* adt_name = NULL;
			if (lookup(
					*(retVar->val.adt_val.adt_type_name), &adt_name) == -1) {
				DSL_ERROR("Struct definition not found (parser error)\n");
				return -1;				
			}
			if (adt_name->val.adt_val.adt_param->type != SEP_TYPE) {
				DSL_ERROR(
					"Struct definition contains no entries (parser error)\n");
				return -1;									
			}
			vector<ASTNode*>* tmpList 
				= adt_name->val.adt_val.adt_param->val.p_val.p_vector;			
			for (u_int i = 0; i < tmpList->size(); i++) {
				string* tmpString = (*tmpList)[i]->val.v_val.v_name;
				//printf("Assign field name %s\n", tmpString->c_str());
				map<string, ASTNode*>::iterator findItA = 
					retVar->val.adt_val.adt_map->find(*tmpString);
				map<string, ASTNode*>::iterator findItB = 
					in_var->val.adt_val.adt_map->find(*tmpString);
				if (findItA	== retVar->val.adt_val.adt_map->end() ||
						findItB	== in_var->val.adt_val.adt_map->end() ) {
					DSL_ERROR(
						"Struct definition inconsistent (parser error)\n");
					return -1;							
				}
				if (updateADT(findItA->second, findItB->second) == -1) {
					return -1;
				}				
			}
			break;
		}
		default:
			DSL_ERROR("Unexpected type encountered (parser error)\n");
			return -1;			
	}
	return 0;
}

