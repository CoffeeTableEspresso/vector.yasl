#include <yasl/yasl.h>
#include <yasl/yasl_aux.h>

#include <stdio.h>
#include <string.h>

#define VECTOR_PRE "vector"

static const char *VECTOR_NAME = "vector";

struct YASL_Vector {
	size_t size, count;
	yasl_float items[];
};

void vec_free(struct YASL_State *S, struct YASL_Vector *vec) {
	free(vec);
}

static struct YASL_Vector *vec_alloc(size_t size) {
	struct YASL_Vector *vec = malloc(sizeof(struct YASL_Vector) + size * sizeof(double));
	*vec = (struct YASL_Vector) {
		.size = size,
		.count = 0,
	};
	return vec;
}

static void YASL_pushvec(struct YASL_State *S, struct YASL_Vector *vec) {
	YASL_pushuserdata(S, vec, VECTOR_NAME, (void (*)(struct YASL_State *, void *))vec_free);
	YASL_loadmt(S, VECTOR_PRE);
	YASL_setmt(S);
}

static bool YASL_isnvec(struct YASL_State *S, unsigned n) {
	return YASL_isnuserdata(S, VECTOR_NAME, n);
}

static bool YASLX_isnnum(struct YASL_State *S, unsigned n) {
	return YASL_isnfloat(S, n) || YASL_isnint(S, n);
}

static struct YASL_Vector *YASLX_checknvec(struct YASL_State *S, const char *name, unsigned n) {
	return (struct YASL_Vector *)YASLX_checknuserdata(S, VECTOR_NAME, name, n);
}

static yasl_float YASLX_checknnum(struct YASL_State *S, const char *name, unsigned n) {
	if (YASL_isnint(S, n)) {
		return (yasl_float)YASL_peeknint(S, n);
	}
	return YASLX_checknfloat(S, name, n);
}

static bool match_args(
		struct YASL_State *S,
		bool (*first)(struct YASL_State *, unsigned),
		bool (*second)(struct YASL_State *, unsigned)) {
	return first(S, 0) && second(S, 1);
}

static int YASL_vec_new_list(struct YASL_State *S) {
	YASL_duptop(S);
	YASL_len(S);
	yasl_int len = YASL_popint(S);
	struct YASL_Vector *vec = vec_alloc(len);
	for (size_t i = 0; i < len; i++) {
		YASL_listget(S, i);
		vec->items[i] = YASLX_checknnum(S, "vector", 2);
		YASL_pop(S);
	}
	vec->count = len;
	YASL_pushvec(S, vec);
	return 1;
}

static int YASL_vec_new(struct YASL_State *S) {
	yasl_int len = YASL_peekvargscount(S);
	if (len == 1 && YASL_isnlist(S, 1)) {
		return YASL_vec_new_list(S);
	}
	struct YASL_Vector *vec = vec_alloc(len);
	vec->count = len;
	while (len--) {
		yasl_float val = YASLX_checknnum(S, "vector", len + 1);
		vec->items[len] = val;
	}

	YASL_pushvec(S, vec);
	return 1;
}

static int YASL_vec_len(struct YASL_State *S) {
	struct YASL_Vector *v = YASLX_checknvec(S, "vector.__len", 0);
	YASL_pushint(S, v->count);
	return 1;
}

static int YASL_vec_spread(struct YASL_State *S) {
	struct YASL_Vector *v = YASLX_checknvec(S, "vector.spread", 0);
	for (size_t i = 0; i < v->count; i++) {
		YASL_pushfloat(S, v->items[i]);
	}

	return v->count;
}

static int YASL_vec_tostr(struct YASL_State *S) {
	struct YASL_Vector *vec = YASLX_checknvec(S, "vector.tostr", 0);
	if (vec->count == 0) {
		YASL_pushlit(S, "vector()");
		return 1;
	}

	size_t buffer_size = 8;
	size_t buffer_count = 0;
	char *buffer = malloc(buffer_size);
	strcpy(buffer, "vector(");
	buffer_count += strlen("vector(");

	for (size_t i = 0; i < vec->count; i++) {
		const char *tmp = YASL_floattostr(vec->items[i]);
		const size_t len = strlen(tmp);
		buffer_size += len + 2;
		buffer = realloc(buffer, buffer_size);
		strcpy(buffer + buffer_count, tmp);
		buffer_count += len;
		free(tmp);
		buffer[buffer_count++] = ',';
		buffer[buffer_count++] = ' ';
	}

	buffer_count -= 2;
	buffer[buffer_count++] = ')';
	buffer[buffer_count++] = '\0';

	YASL_pushzstr(S, buffer);
	return 1;
}

static int YASL_vec_tolist(struct YASL_State *S) {
	struct YASL_Vector *v = YASLX_checknvec(S, "vector.tolist", 0);

	YASL_pushlist(S);
	for (size_t i = 0; i < v->count; i++) {
		YASL_pushfloat(S, v->items[i]);
		YASL_listpush(S);
	}

	return 1;
}

#define DEFINE_BINOP(name, op) \
static int YASL_vec_vv_##name(struct YASL_State *S) { \
	struct YASL_Vector *a = YASLX_checknvec(S, "vector." #name, 0);\
	struct YASL_Vector *b = YASLX_checknvec(S, "vector." #name, 1);\
\
	if (a->count != b->count) {\
		YASL_print_err(S, "ValueError: " #op " expects two vectors of the same len, got two vectors of lens %ld and %ld.", a->count, b->count);\
		YASL_throw_err(S, YASL_VALUE_ERROR);\
	}\
\
	const size_t len = a->count;\
\
	struct YASL_Vector *c = vec_alloc(len);\
	for (size_t i = 0; i < len; i++) {\
		c->items[i] = a->items[i] op b->items[i];\
	}\
	c->count = len;\
	YASL_pushvec(S, c);\
	return 1;\
}\
static int YASL_vec_vn_##name(struct YASL_State *S) {\
	struct YASL_Vector *a = YASLX_checknvec(S, "vector." #name, 0);\
	yasl_float n = YASLX_checknnum(S, "vector." #name, 1);\
\
	struct YASL_Vector *v = vec_alloc(a->count);\
	for (size_t i = 0; i < a->count; i++) {\
		v->items[i] = a->items[i] op n;\
	}\
	v->count = a->count;\
	YASL_pushvec(S, v);\
	return 1;\
}\
static int YASL_vec_##name(struct YASL_State *S) {\
	if (match_args(S, &YASL_isnvec, &YASL_isnvec)) {\
		return YASL_vec_vv_##name(S);\
	}\
	if (match_args(S, &YASL_isnvec, &YASLX_isnnum)) {\
		return YASL_vec_vn_##name(S);\
	}\
	YASL_print_err(S, "bad args");\
	YASL_throw_err(S, YASL_TYPE_ERROR);\
}


DEFINE_BINOP(__add, +)
DEFINE_BINOP(__sub, -)
DEFINE_BINOP(__mul, *)
DEFINE_BINOP(__div, /)

int YASL_load_dyn_lib(struct YASL_State *S) {
	YASL_pushtable(S);
	YASL_registermt(S, VECTOR_PRE);

	struct YASLX_function functions[] = {
		{ "__len", &YASL_vec_len, 1 },
		{ "__add", &YASL_vec___add, 2 },
		{ "__sub", &YASL_vec___sub, 2 },
		{ "__mul", &YASL_vec___mul, 2 },
		{ "__div", &YASL_vec___div, 2 },
		{ "spread", &YASL_vec_spread, 1 },
		{ "tostr", &YASL_vec_tostr, 1 },
		{ "tolist", &YASL_vec_tolist, 1 },
		{ NULL, NULL, 0 }
	};

	YASL_loadmt(S, VECTOR_PRE);
	YASLX_tablesetfunctions(S, functions);

	YASL_pushcfunction(S, YASL_vec_new, -1);
	return 1;
}
