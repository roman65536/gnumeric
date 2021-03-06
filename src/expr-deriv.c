/*
 * exp-deriv.c : Expression derivation
 *
 * Copyright (C) 2016 Morten Welinder (terra@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <expr-deriv.h>
#include <expr-impl.h>
#include <func.h>
#include <value.h>
#include <sheet.h>
#include <workbook.h>
#include <cell.h>
#include <gutils.h>

/* ------------------------------------------------------------------------- */

static GHashTable *deriv_handlers;

struct DerivInfo {
	GnmExprDerivHandler handler;
	GnmExprDerivFlags flags;
};

/* ------------------------------------------------------------------------- */

struct GnmExprDeriv_ {
	GnmEvalPos var;
};

/**
 * gnm_expr_deriv_info_new: (skip)
 */
GnmExprDeriv *
gnm_expr_deriv_info_new (void)
{
	return g_new0 (GnmExprDeriv, 1);
}

/**
 * gnm_expr_deriv_info_free: (skip)
 */
void
gnm_expr_deriv_info_free (GnmExprDeriv *deriv)
{
	g_free (deriv);
}

void
/**
 * gnm_expr_deriv_info_set_var: (skip)
 */
gnm_expr_deriv_info_set_var (GnmExprDeriv *deriv, GnmEvalPos const *var)
{
	deriv->var = *var;
}

/* ------------------------------------------------------------------------- */

static GnmExpr const *
gnm_value_deriv (GnmValue const *v)
{
	if (VALUE_IS_NUMBER (v))
		return gnm_expr_new_constant (value_new_float (0));
	else
		return NULL;
}

static gboolean
is_any_const (GnmExpr const *e, gnm_float *c)
{
	GnmValue const *v = gnm_expr_get_constant (e);
	if (v && VALUE_IS_FLOAT (v)) {
		*c = value_get_as_float (v);
		return TRUE;
	} else
		return FALSE;
}

static gboolean
is_const (GnmExpr const *e, gnm_float c)
{
	GnmValue const *v = gnm_expr_get_constant (e);
	return v && VALUE_IS_FLOAT (v) && value_get_as_float (v) == c;
}

// Optimizing constructor for "+".  Takes ownership of "l" and "r"
// if the corresponding "copy" argument is false.
//
// Note, that this plays fast-and-loose with semantics when errors are
// involved.
static GnmExpr const *
madd (GnmExpr const *l, gboolean copyl, GnmExpr const *r, gboolean copyr)
{
	if (is_const (l, 0)) {
		if (!copyl) gnm_expr_free (l);
		if (copyr) r = gnm_expr_copy (r);
		return r;
	}

	if (is_const (r, 0)) {
		if (!copyr) gnm_expr_free (r);
		if (copyl) l = gnm_expr_copy (l);
		return l;
	}

	if (copyl) l = gnm_expr_copy (l);
	if (copyr) r = gnm_expr_copy (r);
	return gnm_expr_new_binary (l, GNM_EXPR_OP_ADD, r);
}

// Optimizing constructor for unary "-".  Takes ownership of "l"
// if the corresponding "copy" argument is false.
//
// Note, that this plays fast-and-loose with semantics when errors are
// involved.
static GnmExpr const *
mneg (GnmExpr const *l, gboolean copyl)
{
	gnm_float x;
	if (is_any_const (l, &x)) {
		if (!copyl) gnm_expr_free (l);
		return gnm_expr_new_constant (value_new_float (-x));
	}

	if (copyl) l = gnm_expr_copy (l);
	return gnm_expr_new_unary (GNM_EXPR_OP_UNARY_NEG, l);
}

// Optimizing constructor for "-".  Takes ownership of "l" and "r"
// if the corresponding "copy" argument is false.
//
// Note, that this plays fast-and-loose with semantics when errors are
// involved.
static GnmExpr const *
msub (GnmExpr const *l, gboolean copyl, GnmExpr const *r, gboolean copyr)
{
	if (is_const (r, 0)) {
		if (!copyr) gnm_expr_free (r);
		if (copyl) l = gnm_expr_copy (l);
		return l;
	}

	if (is_const (l, 0)) {
		if (!copyl) gnm_expr_free (l);
		return mneg (r, copyr);
	}

	if (copyl) l = gnm_expr_copy (l);
	if (copyr) r = gnm_expr_copy (r);
	return gnm_expr_new_binary (l, GNM_EXPR_OP_SUB, r);
}

// Optimizing constructor for "*".  Takes ownership of "l" and "r"
// if the corresponding "copy" argument is false.
//
// Note, that this plays fast-and-loose with semantics when errors are
// involved.
static GnmExpr const *
mmul (GnmExpr const *l, gboolean copyl, GnmExpr const *r, gboolean copyr)
{
	if (is_const (l, 1) || is_const (r, 0)) {
		if (!copyl) gnm_expr_free (l);
		if (copyr) r = gnm_expr_copy (r);
		return r;
	}

	if (is_const (l, 0) || is_const (r, 1)) {
		if (!copyr) gnm_expr_free (r);
		if (copyl) l = gnm_expr_copy (l);
		return l;
	}

	if (is_const (l, -1)) {
		if (!copyl) gnm_expr_free (l);
		return mneg (r, copyr);
	}

	if (copyl) l = gnm_expr_copy (l);
	if (copyr) r = gnm_expr_copy (r);
	return gnm_expr_new_binary (l, GNM_EXPR_OP_MULT, r);
}

// Optimizing constructor for "/".  Takes ownership of "l" and "r"
// if the corresponding "copy" argument is false.
//
// Note, that this plays fast-and-loose with semantics when errors are
// involved.
static GnmExpr const *
mdiv (GnmExpr const *l, gboolean copyl, GnmExpr const *r, gboolean copyr)
{
	if (is_const (l, 0) || is_const (r, 1)) {
		if (!copyr) gnm_expr_free (r);
		if (copyl) l = gnm_expr_copy (l);
		return l;
	}

	if (copyl) l = gnm_expr_copy (l);
	if (copyr) r = gnm_expr_copy (r);
	return gnm_expr_new_binary (l, GNM_EXPR_OP_DIV, r);
}

// Optimizing constructor for "^".  Takes ownership of "l" and "r"
// if the corresponding "copy" argument is false.
//
// Note, that this plays fast-and-loose with semantics when errors are
// involved.
static GnmExpr const *
mexp (GnmExpr const *l, gboolean copyl, GnmExpr const *r, gboolean copyr)
{
	if (is_const (r, 1)) {
		if (!copyr) gnm_expr_free (r);
		if (copyl) l = gnm_expr_copy (l);
		return l;
	}

	if (copyl) l = gnm_expr_copy (l);
	if (copyr) r = gnm_expr_copy (r);
	return gnm_expr_new_binary (l, GNM_EXPR_OP_EXP, r);
}

/* ------------------------------------------------------------------------- */

struct cb_arg_collect {
	GnmExprList *args;
	GnmCellRef const *cr0;
	GnmEvalPos const *ep;
};

static GnmValue *
cb_arg_collect (GnmCellIter const *iter, gpointer user_)
{
	struct cb_arg_collect *user = user_;
	GnmCell const *cell = iter->cell;
	GnmCellRef cr;
	GnmParsePos pp;
	gnm_cellref_init (&cr, user->cr0->sheet,
			  cell->pos.col, cell->pos.row,
			  FALSE);
	parse_pos_init_evalpos (&pp, user->ep);
	// gnm_cellref_set_col_ar (&cr, &pp, user->cr0->col_relative);
	// gnm_cellref_set_row_ar (&cr, &pp, user->cr0->row_relative);
	user->args = gnm_expr_list_prepend
		(user->args,
		 (gpointer)gnm_expr_new_cellref (&cr));
	return NULL;
}

/**
 * gnm_expr_deriv_collect:
 * @expr: expression
 * @ep: evaluation position
 * @info: extra information, not currently used
 *
 * Returns: (transfer full): list of expressions expanded from @expr
 */
GnmExprList *
gnm_expr_deriv_collect (GnmExpr const *expr,
			GnmEvalPos const *ep,
			GnmExprDeriv *info)
{
	struct cb_arg_collect user;
	int i;

	user.args = NULL;
	user.ep = ep;
	for (i = 0; i < expr->func.argc; i++) {
		GnmExpr const *e = expr->func.argv[i];
		GnmValue const *v = gnm_expr_get_constant (e);

		if (!v || !VALUE_IS_CELLRANGE (v)) {
			user.args = gnm_expr_list_prepend
				(user.args, (gpointer)gnm_expr_copy (e));
			continue;
		}

		user.cr0 = &value_get_rangeref (v)->a;
		workbook_foreach_cell_in_range (ep, v,
						CELL_ITER_IGNORE_BLANK,
						cb_arg_collect,
						&user);
	}

	return user.args;
}

/* ------------------------------------------------------------------------- */

#define MAYBE_FREE(e) do { if (e) gnm_expr_free (e); } while (0)

#define COMMON_BINARY_START						\
	GnmExpr const *a = expr->binary.value_a; /* Not owned */	\
	GnmExpr const *da = gnm_expr_deriv (a, ep, info);		\
	GnmExpr const *b = expr->binary.value_b; /* Not owned */	\
	GnmExpr const *db = gnm_expr_deriv (b, ep, info);		\
	if (!da || !db) {						\
		MAYBE_FREE (da);					\
		MAYBE_FREE (db);					\
		return NULL;						\
	} else {

#define COMMON_BINARY_END }


GnmExpr const *
gnm_expr_deriv (GnmExpr const *expr,
		GnmEvalPos const *ep,
		GnmExprDeriv *info)
{
	GnmExprOp op = GNM_EXPR_GET_OPER (expr);

	switch (op) {
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_NAME:
	case GNM_EXPR_OP_ARRAY_CORNER:
	case GNM_EXPR_OP_ARRAY_ELEM:
	case GNM_EXPR_OP_SET:

	case GNM_EXPR_OP_EQUAL:
	case GNM_EXPR_OP_GT:
	case GNM_EXPR_OP_LT:
	case GNM_EXPR_OP_GTE:
	case GNM_EXPR_OP_LTE:
	case GNM_EXPR_OP_NOT_EQUAL:
	case GNM_EXPR_OP_CAT:
	case GNM_EXPR_OP_PERCENTAGE:
		// Bail
		return NULL;

	case GNM_EXPR_OP_PAREN:
	case GNM_EXPR_OP_UNARY_PLUS:
		return gnm_expr_deriv (expr->unary.value, ep, info);

	case GNM_EXPR_OP_UNARY_NEG: {
		GnmExpr const *d = gnm_expr_deriv (expr->unary.value, ep, info);
		return d ? mneg (d, 0) : NULL;
	}

	case GNM_EXPR_OP_ADD: {
		COMMON_BINARY_START
		return madd (da, 0, db, 0);
		COMMON_BINARY_END
	}

	case GNM_EXPR_OP_SUB: {
		COMMON_BINARY_START
		return msub (da, 0, db, 0);
		COMMON_BINARY_END
	}

	case GNM_EXPR_OP_MULT: {
		COMMON_BINARY_START
		GnmExpr const *t1 = mmul (da, 0, b, 1);
		GnmExpr const *t2 = mmul (a, 1, db, 0);
		return madd (t1, 0, t2, 0);
		COMMON_BINARY_END
	}

	case GNM_EXPR_OP_DIV: {
		COMMON_BINARY_START
		GnmExpr const *t1 = mmul (da, 0, b, 1);
		GnmExpr const *t2 = mmul (a, 1, db, 0);
		GnmExpr const *d = msub (t1, 0, t2, 0);
		GnmExpr const *n = mmul (b, 1, b, 1);
		return mdiv (d, 0, n, 0);
		COMMON_BINARY_END
	}

	case GNM_EXPR_OP_EXP: {
		COMMON_BINARY_START
		GnmFunc *fln = gnm_func_lookup ("ln", NULL);
		GnmValue const *vb = gnm_expr_get_constant (b);
		if (vb && VALUE_IS_FLOAT (vb)) {
			GnmExpr const *bm1 = gnm_expr_new_constant (value_new_float (value_get_as_float (vb) - 1));
			GnmExpr const *t1 = mexp (a, 1, bm1, 0);
			gnm_expr_free (db);
			return mmul (mmul (b, 1, t1, 0), 0, da, 0);
		} else if (fln) {
			// a^b = exp(b*log(a))
			// (a^b)' = a^b * (a'*b/a + b'*ln(a))
			GnmExpr const *t1 = mdiv (mmul (da, 0, b, 1), 0, a, 1);
			GnmExpr const *t2 = mmul
				(db, 0,
				 gnm_expr_new_funcall1 (fln, gnm_expr_copy (a)), 0);
			GnmExpr const *s = madd (t1, 0, t2, 0);
			return mmul (expr, 1, s, 0);
		} else
			return NULL;
		COMMON_BINARY_END
	}

	case GNM_EXPR_OP_FUNCALL: {
		GnmFunc *f = gnm_expr_get_func_def (expr);
		struct DerivInfo const *di = deriv_handlers
			? g_hash_table_lookup (deriv_handlers, f)
			: NULL;
		GnmExpr const *res = di
			? di->handler (expr, ep, info)
			: NULL;
		if (!res)
			return NULL;

		if (di->flags & GNM_EXPR_DERIV_CHAIN) {
			GnmExpr const *e2 =
				gnm_expr_deriv (gnm_expr_get_func_arg (expr, 0), ep, info);
			if (!e2) {
				gnm_expr_free (res);
				return NULL;
			}
			res = mmul (res, 0, e2, 0);
		}

		return res;
	}

	case GNM_EXPR_OP_CONSTANT:
		return gnm_value_deriv (expr->constant.value);

	case GNM_EXPR_OP_CELLREF: {
		GnmCellRef r;
		Sheet *sheet;
		GnmCell *cell;
		GnmEvalPos ep2;
		GnmExpr const *res;
		GnmExprTop const *texpr;
		GnmExprTop const *texpr2;
		GnmExprRelocateInfo rinfo;

		gnm_cellref_make_abs (&r, &expr->cellref.ref, ep);
		sheet = eval_sheet (r.sheet, ep->sheet);

		if (sheet == info->var.sheet &&
		    r.col == info->var.eval.col &&
		    r.row == info->var.eval.row)
			return gnm_expr_new_constant (value_new_float (1));

		cell = sheet_cell_get (sheet, r.col, r.row);
		if (!cell)
			return gnm_expr_new_constant (value_new_float (1));
		if (!gnm_cell_has_expr (cell))
			return gnm_value_deriv (cell->value);

		eval_pos_init_cell (&ep2, cell);
		res = gnm_expr_deriv (cell->base.texpr->expr, &ep2, info);
		if (!res)
			return NULL;

		// The just-computed derivative is relative to the wrong
		// position.

		texpr = gnm_expr_top_new (res);
		parse_pos_init_evalpos (&rinfo.pos, &ep2);
		rinfo.reloc_type = GNM_EXPR_RELOCATE_MOVE_RANGE;
		rinfo.origin.start = rinfo.origin.end = ep2.eval;
		rinfo.origin_sheet = ep2.sheet;
		rinfo.target_sheet = ep->sheet;
		rinfo.col_offset = ep->eval.col - ep2.eval.col;
		rinfo.row_offset = ep->eval.row - ep2.eval.row;
		texpr2 = gnm_expr_top_relocate (texpr, &rinfo, FALSE);

		if (texpr2) {
			res = gnm_expr_copy (texpr2->expr);
			gnm_expr_top_unref (texpr2);
		} else {
			res = gnm_expr_copy (texpr->expr);
		}
		gnm_expr_top_unref (texpr);

		return res;
	}

#ifndef DEBUG_SWITCH_ENUM
	default:
		g_assert_not_reached ();
		break;
#endif
	}
}

/* ------------------------------------------------------------------------- */

GnmExprTop const *
gnm_expr_top_deriv (GnmExprTop const *texpr,
		    GnmEvalPos const *ep,
		    GnmExprDeriv *info)
{
	GnmExpr const *expr;

	g_return_val_if_fail (GNM_IS_EXPR_TOP (texpr), NULL);
	g_return_val_if_fail (ep != NULL, NULL);
	g_return_val_if_fail (info != NULL, NULL);

	expr = gnm_expr_deriv (texpr->expr, ep, info);
	if (gnm_debug_flag ("deriv")) {
		GnmParsePos pp;
		char *s;
		Sheet *sheet = ep->sheet;
		GnmConventions const *convs = sheet_get_conventions (sheet);

		parse_pos_init_evalpos (&pp, ep);
		s = gnm_expr_top_as_string (texpr, &pp, convs);
		g_printerr ("Derivative of %s with respect to %s:%s",
			    s, parsepos_as_string (&pp),
			    expr ? "\n" : " cannot compute.\n");
		g_free (s);
		if (expr) {
			s = gnm_expr_as_string (expr, &pp, convs);
			g_printerr ("%s\n\n", s);
			g_free (s);
		}
	}

	return gnm_expr_top_new (expr);
}

GnmExprTop const *
gnm_expr_cell_deriv (GnmCell *y, GnmCell *x)
{
	GnmExprTop const *res;
	GnmEvalPos ep, var;
	GnmExprDeriv *info;

	g_return_val_if_fail (y != NULL, NULL);
	g_return_val_if_fail (gnm_cell_has_expr (y), NULL);
	g_return_val_if_fail (x != NULL, NULL);

	eval_pos_init_cell (&ep, y);

	info = gnm_expr_deriv_info_new ();
	eval_pos_init_cell (&var, x);
	gnm_expr_deriv_info_set_var (info, &var);

	res = gnm_expr_top_deriv (y->base.texpr, &ep, info);

	gnm_expr_deriv_info_free (info);

	return res;
}

gnm_float
gnm_expr_cell_deriv_value (GnmCell *y, GnmCell *x)
{
	GnmExprTop const *dydx;
	GnmValue *v;
	gnm_float res;
	GnmEvalPos ep;

	g_return_val_if_fail (y != NULL, gnm_nan);
	g_return_val_if_fail (x != NULL, gnm_nan);

	dydx = gnm_expr_cell_deriv (y, x);
	if (!dydx)
		return gnm_nan;

	eval_pos_init_cell (&ep, y);
	v = gnm_expr_top_eval (dydx, &ep, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	res = VALUE_IS_NUMBER (v) ? value_get_as_float (v) : gnm_nan;

	value_release (v);
	gnm_expr_top_unref (dydx);

	return res;
}

/* ------------------------------------------------------------------------- */

/**
 * gnm_expr_deriv_install_handler:
 * @func: the function being given a handler
 * @h: (scope async): #GnmExprDerivHandler
 * @flags: 
 */
void
gnm_expr_deriv_install_handler (GnmFunc *func, GnmExprDerivHandler h,
				GnmExprDerivFlags flags)
{
	struct DerivInfo *data;

	if (!deriv_handlers) {
		deriv_handlers = g_hash_table_new_full
			(g_direct_hash, g_direct_equal,
			 NULL, g_free);
	}

	data = g_new (struct DerivInfo, 1);
	data->handler = h;
	data->flags = flags;

	g_hash_table_replace (deriv_handlers, func, data);
}

void
gnm_expr_deriv_uninstall_handler (GnmFunc *func)
{
	g_hash_table_remove (deriv_handlers, func);
}

/* ------------------------------------------------------------------------- */

void
_gnm_expr_deriv_shutdown (void)
{
	if (deriv_handlers) {
		g_hash_table_destroy (deriv_handlers);
		deriv_handlers = NULL;
	}
}

/* ------------------------------------------------------------------------- */
