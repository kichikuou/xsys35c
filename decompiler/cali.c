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

typedef struct Node {
	enum {
		NODE_NUMBER,
		NODE_VARIABLE,
		NODE_OP,
		NODE_AREF,
	} type;
	int val;
	struct Node *lhs, *rhs;
} Node;

static Node *new_node(int type, int val, Node *lhs, Node *rhs) {
	Node *n = calloc(1, sizeof(Node));
	n->type = type;
	n->val = val;
	n->lhs = lhs;
	n->rhs = rhs;
	return n;
}

static Node *parse_cali(uint8_t **code) {
	Node *stack[256];
	Node **top = stack;
	uint8_t *p = *code;
	for (;;) {
		uint8_t op = *p++;
		switch (op) {
		case OP_END:
			if (--top != stack)
				error("cali: unexpected end of expression");
			*code = p;
			return *top;

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
					error("cali: stack underflow");
				Node *rhs = *--top;
				Node *lhs = *--top;
				*top++ = new_node(NODE_OP, op, lhs, rhs);
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
					Node *index = parse_cali(&p);
					*top++ = new_node(NODE_AREF, var, index, NULL);
				}
				break;

			case OP_C0_MOD:
			case OP_C0_LE:
			case OP_C0_GE:
				{
					if (top - 2 < stack)
						error("cali: stack underflow");
					Node *rhs = *--top;
					Node *lhs = *--top;
					*top++ = new_node(NODE_OP, op, lhs, rhs);
				}
				break;

			default:
				error("cali: unknown code c0 %02x", op);
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
						error("cali: unknown code 00 %02x", val);
				}
				*top++ = new_node(NODE_NUMBER, val, NULL, NULL);
			}
			break;
		}
	}
}

static void print_cali(Node *node, Vector *variables, FILE *out) {
	switch (node->type) {
	case NODE_NUMBER:
		fprintf(out, "%d", node->val);
		break;

	case NODE_VARIABLE:
	case NODE_AREF:
		if (node->val >= variables->len)
			fprintf(out, "VAR%d", node->val);
		else
			fputs(variables->data[node->val], out);
		if (node->type == NODE_AREF) {
			fputc('[', out);
			print_cali(node->lhs, variables, out);
			fputc(']', out);
		}
		break;

	case NODE_OP:
		fputc('(', out);
		print_cali(node->lhs, variables, out);
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
		default:
			error("BUG: unknown operator %d", node->val);
		}
		print_cali(node->rhs, variables, out);
		fputc(')', out);
		break;
	}
}

int cali(uint8_t *p, Vector *variables, FILE *out) {
	uint8_t *endptr = p;
	Node *node = parse_cali(&endptr);
	print_cali(node, variables, out);
	return endptr - p;
}
