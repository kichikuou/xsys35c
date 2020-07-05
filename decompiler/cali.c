/* Copyright (C) 2020 <KichikuouChrome@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
*/
#include "xsys35dc.h"
#include <stdlib.h>
#include <string.h>

#define NODE_POOL_SIZE 1024

static Cali node_pool[NODE_POOL_SIZE];
static Cali *free_node;

static Cali *new_node(int type, int val, Cali *lhs, Cali *rhs) {
	Cali *n = (free_node > node_pool) ? --free_node : calloc(1, sizeof(Cali));
	n->type = type;
	n->val = val;
	n->lhs = lhs;
	n->rhs = rhs;
	return n;
}

static Cali *parse(const uint8_t **code, bool is_lhs) {
	Cali *stack[256];
	Cali **top = stack;
	const uint8_t *p = *code;
	do {
		uint8_t op = *p++;
		switch (op) {
		case OP_END:
			if (top == stack)
				error_at(p, "empty expression");
			while (top - 2 >= stack) {
				// Rance 2, Oudou Yuusha
				warning_at(p, "unexpected end of expression");
				Cali *rhs = *--top;
				Cali *lhs = *--top;
				*top++ = new_node(NODE_OP, OP_END, lhs, rhs);
			}
			*code = p;
			return *--top;

		case OP_AND:
		case OP_OR:
		case OP_XOR:
		case OP_MUL:
		case OP_DIV:
		case OP_ADD:
		case OP_SUB:
		case OP_EQ:
		case OP_LT:
		case OP_GT:
		case OP_NE:
			{
				if (top - 2 < stack)
					error_at(p, "stack underflow");
				Cali *rhs = *--top;
				Cali *lhs = *--top;
				if (op == OP_ADD &&
					lhs->type == NODE_NUMBER && lhs->val == 16383 &&
					rhs->type == NODE_NUMBER && rhs->val <= 65535-16383) {
					lhs->val += rhs->val;
					*top++ = lhs;
				} else {
					*top++ = new_node(NODE_OP, op, lhs, rhs);
				}
			}
			break;

		case 0xc0:
			op = *p++;
			if (op >= 0x40) {
				*top++ = new_node(NODE_VARIABLE, op, NULL, NULL);
				break;
			}
			switch (op) {
			case OP_C0_INDEX:
				{
					int var = p[0] << 8 | p[1];
					p += 2;
					Cali *index = parse(&p, false);
					*top++ = new_node(NODE_AREF, var, index, NULL);
				}
				break;

			case OP_C0_MOD:
			case OP_C0_LE:
			case OP_C0_GE:
				{
					if (top - 2 < stack)
						error_at(p, "stack underflow");
					Cali *rhs = *--top;
					Cali *lhs = *--top;
					*top++ = new_node(NODE_OP, op, lhs, rhs);
				}
				break;

			default:
				error_at(p, "unknown code c0 %02x", op);
				break;
			}
			break;

		default:
			if (op & 0x80) {
				int var = op & 0x3f;
				if (op > 0xc0)
					var = var << 8 | *p++;
				*top++ = new_node(NODE_VARIABLE, var, NULL, NULL);
			} else {
				int val = op & 0x3f;
				if (op < 0x40) {
					val = val << 8 | *p++;
					if (val <= 0x33)
						error_at(p, "unknown code 00 %02x", val);
				}
				*top++ = new_node(NODE_NUMBER, val, NULL, NULL);
			}
			break;
		}
	} while (!is_lhs);

	if (top == stack)
		error_at(p, "empty expression");
	if (--top != stack)
		warning_at(p, "unexpected end of expression");
	Cali *node = *top;
	if (node->type != NODE_VARIABLE && node->type != NODE_AREF)
		error_at(p, "unexpected left-hand-side for assignment %d", node->type);
	*code = p;
	return node;
}

static int precedence(int op) {
	switch (op) {
	case OP_MUL:    return 4;
	case OP_DIV:    return 4;
	case OP_C0_MOD: return 4;
	case OP_ADD:    return 3;
	case OP_SUB:    return 3;
	case OP_AND:    return 2;
	case OP_OR:     return 2;
	case OP_XOR:    return 2;
	case OP_LT:     return 1;
	case OP_GT:     return 1;
	case OP_C0_LE:  return 1;
	case OP_C0_GE:  return 1;
	case OP_EQ:     return 0;
	case OP_NE:     return 0;
	case OP_END:    return 0;
	default:
		error("BUG: unknown operator %d", op);
	}
}

Cali *parse_cali(const uint8_t **code, bool is_lhs) {
	free_node = node_pool + NODE_POOL_SIZE;
	return parse(code, is_lhs);
}

void print_cali_prec(Cali *node, int out_prec, Vector *variables, FILE *out) {
	switch (node->type) {
	case NODE_NUMBER:
		fprintf(out, "%d", node->val);
		break;

	case NODE_VARIABLE:
	case NODE_AREF:
		while (variables->len <= node->val)
			vec_push(variables, NULL);
		if (!variables->data[node->val]) {
			char buf[10];
			sprintf(buf, "VAR%d", node->val);
			variables->data[node->val] = strdup(buf);
		}
		fputs(variables->data[node->val], out);
		if (node->type == NODE_AREF) {
			fputc('[', out);
			print_cali_prec(node->lhs, 0, variables, out);
			fputc(']', out);
		}
		break;

	case NODE_OP:
		{
			int prec = precedence(node->val);
			if (out_prec > prec)
				fputc('(', out);
			print_cali_prec(node->lhs, prec, variables, out);
			switch (node->val) {
			case OP_AND:   fputs(" & ", out); break;
			case OP_OR:    fputs(" | ", out); break;
			case OP_XOR:   fputs(" ^ ", out); break;
			case OP_MUL:   fputs(" * ", out); break;
			case OP_DIV:   fputs(" / ", out); break;
			case OP_ADD:   fputs(" + ", out); break;
			case OP_SUB:   fputs(" - ", out); break;
			case OP_EQ:    fputs(" = ", out); break;
			case OP_LT:    fputs(" < ", out); break;
			case OP_GT:    fputs(" > ", out); break;
			case OP_NE:    fputs(" \\ ", out); break;
			case OP_C0_MOD:fputs(" % ", out); break;
			case OP_C0_LE: fputs(" <= ", out); break;
			case OP_C0_GE: fputs(" >= ", out); break;
			case OP_END:   fputs(" $ ", out); break;
			default:
				error("BUG: unknown operator %d", node->val);
			}
			print_cali_prec(node->rhs, prec + 1, variables, out);
			if (out_prec > prec)
				fputc(')', out);
			break;
		}
	}
}

void print_cali(Cali *node, Vector *variables, FILE *out) {
	print_cali_prec(node, 0, variables, out);
}
