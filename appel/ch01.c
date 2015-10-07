typedef char* string;
typedef struct A_stm_* A_stm;
typedef struct A_exp_* A_exp;
typedef struct A_expList_* A_expList;
typedef enum { A_plus, A_minus, A_times, A_div } A_binop;

struct A_stm_ {
  enum { A_compoundStm, A_assignStm, A_printStm } kind;
  union {
    struct { A_stm stm1, stm2; } compound;
    struct { string id; A_exp exp; } assign;
    struct { A_expList exps; } print;
  } u;
};
A_stm A_CompoundStm(A_stm stm1, A_stm stm2);
A_stm A_AssignStm(string id, A_exp exp);
A_stm A_PrintStm(A_expList exps);

struct A_exp_ {
  enum { A_idExp, A_numExp, A_opExp, A_eseqExp } kind;
  union {
    string id;
    int num;
    struct { A_exp left; A_binop oper; A_exp right; } op;
    struct { A_stm stm; A_exp exp; } eseq;
  } u;
};
A_exp A_IdExp(string id);
A_exp A_NumExp(int num);
A_exp A_OpExp(A_exp left, A_binop oper, A_exp right);
A_exp A_EseqExp(A_stm stm, A_exp exp);

struct A_expList_ {
  A_exp head;
  A_expList next;
};
A_expList A_PairExpList(A_exp head, A_expList next);
A_expList A_LastExpList(A_exp last);


#include <stdlib.h>

A_stm A_CompoundStm(A_stm stm1, A_stm stm2) {
  A_stm s = malloc(sizeof(*s));
  s->kind = A_compoundStm;
  s->u.compound.stm1 = stm1;
  s->u.compound.stm2 = stm2;
  return s;
}

A_stm A_AssignStm(string id, A_exp exp) {
  A_stm s = malloc(sizeof(*s));
  s->kind = A_assignStm;
  s->u.assign.id = id;
  s->u.assign.exp = exp;
  return s;
}

A_stm A_PrintStm(A_expList exps) {
  A_stm s = malloc(sizeof(*s));
  s->kind = A_printStm;
  s->u.print.exps = exps;
  return s;
}

A_exp A_IdExp(string id) {
  A_exp e = malloc(sizeof(*e));
  e->kind = A_idExp;
  e->u.id = id;
  return e;
}

A_exp A_NumExp(int num) {
  A_exp e = malloc(sizeof(*e));
  e->kind = A_numExp;
  e->u.num = num;
  return e;
}

A_exp A_OpExp(A_exp left, A_binop oper, A_exp right) {
  A_exp e = malloc(sizeof(*e));
  e->kind = A_opExp;
  e->u.op.left = left;
  e->u.op.oper = oper;
  e->u.op.right = right;
  return e;
}

A_exp A_EseqExp(A_stm stm, A_exp exp) {
  A_exp e = malloc(sizeof(*e));
  e->kind = A_eseqExp;
  e->u.eseq.stm = stm;
  e->u.eseq.exp = exp;
  return e;
}

A_expList A_PairExpList(A_exp head, A_expList next) {
  A_expList l = malloc(sizeof(*l));
  l->head = head;
  l->next = next;
  return l;
}

A_expList A_LastExpList(A_exp last) {
  A_expList l = malloc(sizeof(*l));
  l->head = last;
  l->next = NULL;
  return l;
}


#include <string.h>

typedef struct table* Table_;
struct table { string id; int value; Table_ tail; };
Table_ Table(string id, int value, Table_ tail) {
  Table_ t = malloc(sizeof(*t));
  t->id = id;
  t->value = value;
  t->tail = tail;
  return t;
}
Table_ update(Table_ t, string id, int value) { return Table(id, value, t); }
int lookup(Table_ t, string id) {
  if (!t) return 0;
  if (!strcmp(t->id, id)) return t->value;
  return lookup(t->tail, id);
}


#include <stdio.h>

struct IntAndTable { int i; Table_ t; };
struct IntAndTable interpExp(A_exp exp, Table_ t);
Table_ interpStm(A_stm stm, Table_ t);
void interp(A_stm stm) {
  interpStm(stm, NULL);
}

Table_ printExpList(A_expList l, Table_ t) {
  if (!l) {
    printf("\n");
    return t;
  }
  struct IntAndTable r = interpExp(l->head, t);
  printf("%d ", r.i);
  return printExpList(l->next, r.t);
}

Table_ interpStm(A_stm stm, Table_ t) {
  switch (stm->kind) {
    case A_compoundStm:
      return interpStm(stm->u.compound.stm2,
                       interpStm(stm->u.compound.stm1, t));
    case A_assignStm: {
      struct IntAndTable r = interpExp(stm->u.assign.exp, t);
      return update(r.t, stm->u.assign.id, r.i);
    }
    case A_printStm:
      return printExpList(stm->u.print.exps, t);
  }
}

int interpOp(int l, A_binop oper, int r) {
  switch (oper) {
    case A_plus:  return l + r;
    case A_minus: return l - r;
    case A_times: return l * r;
    case A_div:   return l / r;
  }
}

struct IntAndTable interpExp(A_exp exp, Table_ t) {
  switch (exp->kind) {
    case A_idExp:
      // FIXME: clang should have a fixit if `struct` is missing here:
      return (struct IntAndTable){lookup(t, exp->u.id), t};
    case A_numExp:
      return (struct IntAndTable){exp->u.num, t};
    case A_opExp: {
      struct IntAndTable l = interpExp(exp->u.op.left, t);
      struct IntAndTable r = interpExp(exp->u.op.right, l.t);
      return (struct IntAndTable){interpOp(l.i, exp->u.op.oper, r.i), r.t};
    }
    case A_eseqExp:
      return interpExp(exp->u.eseq.exp, interpStm(exp->u.eseq.stm, t));
  }
}


int main(void) {
  A_stm prog = A_CompoundStm(
      A_AssignStm("a", A_OpExp(A_NumExp(5), A_plus, A_NumExp(3))),
      A_CompoundStm(
          A_AssignStm("b",
                      A_EseqExp(A_PrintStm(A_PairExpList(
                                    A_IdExp("a"),
                                    A_LastExpList(A_OpExp(A_IdExp("a"), A_minus,
                                                          A_NumExp(1))))),
                                A_OpExp(A_NumExp(10), A_times, A_IdExp("a")))),
          A_PrintStm(A_LastExpList(A_IdExp("b")))));
  interp(prog);
}
