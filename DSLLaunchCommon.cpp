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

#include <map>
#include <vector>

#include "Marshal.h"
#include "MeridianDSL.h"
#include "DSLLauncher.h"

// Stub, never used
ASTNode* handleDNSLookup(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) { 
	return NULL;
}

ASTNode* handleGetSelf(ParserState* cur_parser) {
	return NULL;
}

ASTNode* handleRingGT(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	return NULL;
}

ASTNode* handleRingGE(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	return NULL;
}

ASTNode* handleRingLT(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	return NULL;
}

ASTNode* handleRingLE(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	return NULL;
}

ASTNode* handleGetDistTCP(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	return NULL;
}

ASTNode* handleGetDistDNS(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	return NULL;
}

ASTNode* handleGetDistPing(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	return NULL;
}

#ifdef PLANET_LAB_SUPPORT
ASTNode* handleGetDistICMP(
		ParserState* cur_parser, ASTNode* cur_node, int recurse_count) {
	return NULL;
}
#endif

ASTNode* handleRPC(ParserState* ps, 
		const NodeIdentRendv& dest, string* func_name, ASTNode* paramAST) {
	return NULL;
}

int createMeasurement(ASTNode* in_ast, Measurement* in_Measure) {
	if (in_ast->type != VAR_ADT_TYPE) {
		return -1;	
	}
	if (*(in_ast->val.adt_val.adt_type_name) != "Measurement") {
		return -1;
	}
	int32_t tmpVal;
	if (fillNodeIdentField("addr", in_ast->val.adt_val.adt_map, 
			&tmpVal) == -1) {				
		return -1;
	}
	in_Measure->addr = (uint32_t)tmpVal;	
	if (fillNodeIdentField("port", in_ast->val.adt_val.adt_map,
			&tmpVal) == -1) {
		return -1;
	}
	in_Measure->port = (uint16_t)tmpVal;
	if (fillNodeIdentField("rendvAddr", in_ast->val.adt_val.adt_map,
			&tmpVal) == -1) {
		return -1;
	}
	in_Measure->rendvAddr = (uint32_t)tmpVal;
	if (fillNodeIdentField("rendvPort", in_ast->val.adt_val.adt_map,
			&tmpVal) == -1) {		
		return -1;
	}
	in_Measure->rendvPort = (uint16_t)tmpVal;	
	// Now load array
	map<string, ASTNode*>::const_iterator findIt 
		= in_ast->val.adt_val.adt_map->find("distance");
	if (findIt == in_ast->val.adt_val.adt_map->end() || 
		findIt->second->type != ARRAY_TYPE) {
		return -1;	
	}
	vector<ASTNode*>* a_val = findIt->second->val.a_val.a_vector;	
	for (u_int i = 0; i < a_val->size(); i++) {
		if ((*a_val)[i]->type != DOUBLE_TYPE) {
			return -1;	
		}
		in_Measure->distance.push_back((*a_val)[i]->val.d_val);		
	}
	return 0;
}

