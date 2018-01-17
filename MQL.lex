D			[0-9]
L			[a-zA-Z_]

%{
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
#include <string>
#include "MQLState.h"
#include "MQL.tab.hpp"

void count();
int g_parser_line = 1;

#define YY_INPUT(buf,result,max_size) \
   { \
   		ParserState* cur_parser = static_cast<MyFlexLexer*>(this)->param; \
		char* parser_buf; \
		int actual_size = \
			cur_parser->input_buffer.get_buf(&parser_buf, max_size); \
		if (actual_size == 0) { \
			result = YY_NULL; \
		} else { \
			memcpy(buf, parser_buf, actual_size); \
			result = actual_size; \
		} \
	}
%}

%option c++ noyywrap
%%

"sin"				{ count(); return(SIN); }
"cos"				{ count(); return(COS); }
"pow"				{ count(); return(POW); }
"double"			{ count(); return(DOUBLE); }
"int"				{ count(); return(INT); }
"string"			{ count(); return(STRING); }
"void"				{ count(); return(VOID); }
"print"				{ count(); return(PRINT); }
"println"			{ count(); return(PRINTLN); }
"round"				{ count(); return(ROUND); }
"ceil"				{ count(); return(CEIL); }
"floor"				{ count(); return(FLOOR); }
"dbl"				{ count(); return(DBL); }
"if"				{ count(); return(IF); }
"else"				{ count(); return(ELSE); }
"while"				{ count(); return(WHILE); }
"break"				{ count(); return(BREAK); }
"continue"			{ count(); return(CONTINUE); }
"return"			{ count(); return(RETURN); }
"struct"			{ count(); return(STRUCT); }
"rpc"				{ count(); return(RPC); }
"tan"				{ count(); return(TAN); }
"asin"				{ count(); return(ASIN); }
"acos"				{ count(); return(ACOS); }
"atan"				{ count(); return(ATAN); }
"array_size"		{ count(); return(ARRAY_SIZE); }
"log"				{ count(); return(LOG_OP); }
"exp"				{ count(); return(EXP); }
"dns_lookup"		{ count(); return(DNS_LOOKUP); }
"dns_addr"			{ count(); return(DNS_ADDR); }
"get_self"			{ count(); return(GET_SELF); }
"ring_gt"			{ count(); return(RING_GT); }
"ring_ge"			{ count(); return(RING_GE); }
"ring_lt"			{ count(); return(RING_LT); }
"ring_le"			{ count(); return(RING_LE); }
"get_distance_tcp"	{ count(); return(GET_DISTANCE_TCP); }
"get_distance_dns"	{ count(); return(GET_DISTANCE_DNS); }
"get_distance_ping"	{ count(); return(GET_DISTANCE_PING); }
"get_distance_icmp"	{ count(); return(GET_DISTANCE_ICMP); }
"push_back"			{ count(); return(PUSH_BACK); }
"pop_back"			{ count(); return(POP_BACK); }
"array_intersect"	{ count(); return(ARRAY_INTERSECT); }
"for"				{ count(); return(FOR); }
"array_avg"			{ count(); return(ARRAY_AVG); }
"array_max"			{ count(); return(ARRAY_MAX); }
"array_min"			{ count(); return(ARRAY_MIN); }
"array_max_offset"	{ count(); return(ARRAY_MAX_OFFSET); }
"array_min_offset"	{ count(); return(ARRAY_MIN_OFFSET); }
"array_union"		{ count(); return(ARRAY_UNION); }

"||"				{ count(); return(OR_OP); }
"&&"				{ count(); return(AND_OP); }

"^"					{  count(); return('^'); }
"&"					{  count(); return('&'); }
"|"					{  count(); return('|'); }
"-"					{  count(); return('-'); }
"+"					{  count(); return('+'); }
"*"					{  count(); return('*'); }
"/"					{  count(); return('/'); }
"%"					{  count(); return('%'); }
"="					{  count(); return('='); }
";"					{  count(); return(';'); }
"("					{  count(); return('('); }
")"					{  count(); return(')'); }
"{"					{  count(); return('{'); }
"}"					{  count(); return('}'); }
"!"					{  count(); return('!'); }
"<"					{  count(); return('<'); }
">"					{  count(); return('>'); }
"<="				{  count(); return(LE_OP); }
">="				{  count(); return(GE_OP); }
"<<"				{  count(); return(LEFT_OP); }
">>"				{  count(); return(RIGHT_OP); }
"=="				{  count(); return(EQ_OP); }
"!="				{  count(); return(NE_OP); }
","					{  count(); return(','); }
"."					{  count(); return('.'); }
"["					{  count(); return('['); }
"]"					{  count(); return(']'); }

{L}({L}|{D})*		{  
	count();
	ParserState* cur_parser = static_cast<MyFlexLexer*>(this)->param;	
	cur_parser->get_parse_result()->s_val 
		= cur_parser->get_var_table()->new_stack_string();
	*(cur_parser->get_parse_result()->s_val) = YYText();
	return(IDENTIFIER); 
}

{D}+				{  
	count();
	ParserState* cur_parser = static_cast<MyFlexLexer*>(this)->param;	
	cur_parser->get_parse_result()->i_val = atoi(YYText());
	return(INT_CONSTANT); 
}

{D}*"."{D}+			{  
	count();
	ParserState* cur_parser = static_cast<MyFlexLexer*>(this)->param;	
	cur_parser->get_parse_result()->d_val = atof(YYText());
	return(DOUBLE_CONSTANT); 
}

{D}+"."{D}*			{  
	count(); 
	ParserState* cur_parser = static_cast<MyFlexLexer*>(this)->param;	
	cur_parser->get_parse_result()->d_val = atof(YYText());
	return(DOUBLE_CONSTANT); 
}

\"(\\.|[^\\"])*\"	{  
	count();
	ParserState* cur_parser = static_cast<MyFlexLexer*>(this)->param;	
	cur_parser->get_parse_result()->s_val 
		= cur_parser->get_var_table()->new_stack_string();
	// Removes the quote and unquote before assigning	
	*(cur_parser->get_parse_result()->s_val) = YYText() + 1;
	cur_parser->get_parse_result()->s_val->erase(
		cur_parser->get_parse_result()->s_val->size() - 1); 
	return(STRING_LITERAL); 
}

"//"[^\n]*\n		{ count(); }	// C++ style comment

[\n]				{ count(); g_parser_line++; }
[ \t\v\f]			{ count(); }
[\r]				{ count(); }	// Just strip these line breaks

.					{ 	fprintf(stderr, "Unrecognized symbol at %d\n", 
							g_parser_line); 
					}
					
%%

void count() {
	// ECHO;
}
