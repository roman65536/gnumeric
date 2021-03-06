/* gnumeric-cell-renderer-expr-entry.h
 * Copyright (C) 2002  Andreas J. Guelzow <aguelzow@taliesin.ca>
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __GNUMERIC_CELL_RENDERER_EXPR_ENTRY_H__
#define __GNUMERIC_CELL_RENDERER_EXPR_ENTRY_H__

#include <widgets/gnumeric-cell-renderer-text.h>
#include <widgets/gnumeric-expr-entry.h>
#include <gnumeric-fwd.h>

G_BEGIN_DECLS

#define GNM_CELL_RENDERER_EXPR_ENTRY_TYPE		(gnumeric_cell_renderer_expr_entry_get_type ())
#define GNM_CELL_RENDERER_EXPR_ENTRY(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_CELL_RENDERER_EXPR_ENTRY_TYPE, GnumericCellRendererExprEntry))
#define GNM_IS_CELL_RENDERER_EXPR_ENTRY(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_CELL_RENDERER_EXPR_ENTRY_TYPE))

typedef struct _GnumericCellRendererExprEntry      GnumericCellRendererExprEntry;
typedef struct _GnumericCellRendererExprEntryClass GnumericCellRendererExprEntryClass;

struct _GnumericCellRendererExprEntry {
	GnumericCellRendererText parent;

	WBCGtk *wbcg;
	GnmExprEntry	   *entry;
};

struct _GnumericCellRendererExprEntryClass {
	GnumericCellRendererTextClass parent_class;
};

GType            gnumeric_cell_renderer_expr_entry_get_type (void);
GtkCellRenderer *gnumeric_cell_renderer_expr_entry_new      (WBCGtk *wbcg);
void             gnumeric_cell_renderer_expr_entry_editing_done (GtkCellEditable *entry,
						 GnumericCellRendererExprEntry *celltext);

G_END_DECLS


#endif /* __GNUMERIC_CELL_RENDERER_EXPR_ENTRY_H__ */
