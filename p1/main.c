/**
 * Tony Givargis
 * Copyright (C), 2023
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * main.c
 */

#include "jitc.h"
#include "parser.h"
#include "system.h"
#include "math.h"

/*
	export LD_LIBRARY_PATH=.
	export LD_LIBRARY_PATH=/home/shuyuanf/projects/p1
*/

static void
reflect(const struct parser_dag *dag, FILE *file)
{
	if (dag)
	{
		reflect(dag->left, file);
		reflect(dag->right, file);
		if (PARSER_DAG_VAL == dag->op)
		{
			fprintf(file,
					"\tdouble t%d = %f;\n",
					dag->id,
					dag->val);
		}
		else if (PARSER_DAG_NEG == dag->op)
		{
			fprintf(file,
					"\tdouble t%d = - t%d;\n",
					dag->id,
					dag->right->id);
		}
		else if (PARSER_DAG_MUL == dag->op)
		{
			fprintf(file,
					"\tdouble t%d = t%d * t%d;\n",
					dag->id,
					dag->left->id,
					dag->right->id);
		}
		else if (PARSER_DAG_DIV == dag->op)
		{
			fprintf(file,
					"\tdouble t%d = t%d ? (t%d / t%d) : 0.0;\n",
					dag->id,
					dag->right->id,
					dag->left->id,
					dag->right->id);
		}
		else if (PARSER_DAG_MOD == dag->op)
		{
			fprintf(file,
					"\tdouble t%d = t%d ? mod(t%d, t%d) : 0.0;\n",
					dag->id,
					dag->right->id,
					dag->left->id,
					dag->right->id);
		}
		else if (PARSER_DAG_ADD == dag->op)
		{
			fprintf(file,
					"\tdouble t%d = t%d + t%d;\n",
					dag->id,
					dag->left->id,
					dag->right->id);
		}
		else if (PARSER_DAG_SUB == dag->op)
		{
			fprintf(file,
					"\tdouble t%d = t%d - t%d;\n",
					dag->id,
					dag->left->id,
					dag->right->id);
		}
		else
		{
			EXIT("software");
		}
	}
}

double mod(double x1, double x2)
{
	return fmod(x1, x2);
}

double sigmoid(double x)
{
	return 1.0 / (1.0 + exp(-x));
}

static void
generate(const struct parser_dag *dag, FILE *file)
{
	/* fmod function */
	fprintf(file, "double (*mod)(double,double) = (double (*)(double,double))%lu;\n\n", (uintptr_t)mod);

	/* sigmoid definition */
	fprintf(file, "double (*sigmoid)(double) = (double (*)(double))%lu;\n\n", (uintptr_t)sigmoid);

	fprintf(file, "double evaluate_expression(void) {\n");
	reflect(dag, file);
	fprintf(file, "\treturn t%d;\n}\n\n", dag->id);

	/* evaluate expression only */
	/*fprintf(file, "double evaluate(void) {\n");
	fprintf(file, "return evaluate_expression();\n}\n\n");*/

	/* evaluate with sigmoid */
	fprintf(file, "double evaluate(void) {\n");
	fprintf(file, "\tdouble x = evaluate_expression();\n");
	fprintf(file, "\treturn sigmoid(x);\n}\n");
}

typedef double (*evaluate_t)(void);

int main(int argc, char *argv[])
{
	const char *SOFILE = "out.so";
	const char *CFILE = "out.c";
	struct parser *parser;
	struct jitc *jitc;
	evaluate_t fnc;
	FILE *file;

	/* usage */

	if (2 != argc)
	{
		printf("usage: %s expression\n", argv[0]);
		return -1;
	}

	/* parse */

	if (!(parser = parser_open(argv[1])))
	{
		TRACE(0);
		return -1;
	}

	/* generate C */

	if (!(file = fopen(CFILE, "w")))
	{
		TRACE("fopen()");
		return -1;
	}
	generate(parser_dag(parser), file);
	parser_close(parser);
	fclose(file);

	/* JIT compile */

	if (jitc_compile(CFILE, SOFILE))
	{
		file_delete(CFILE);
		TRACE(0);
		return -1;
	}
	file_delete(CFILE);

	/* dynamic load */

	if (!(jitc = jitc_open(SOFILE)) ||
		!(fnc = (evaluate_t)jitc_lookup(jitc, "evaluate")))
	{
		file_delete(SOFILE);
		jitc_close(jitc);
		TRACE(0);
		return -1;
	}
	printf("%f\n", fnc());

	/* done */

	file_delete(SOFILE);
	jitc_close(jitc);
	return 0;
}