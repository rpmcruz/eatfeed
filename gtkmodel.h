// gtkmodel.h
// GtkTreeModel simplifying wrapper

#ifndef MY_MODEL_H
#define MY_MODEL_H

#include <gtk/gtktreemodel.h>

struct TableModel {
	virtual int rowsNb() const = 0;
	virtual int columnsNb() const = 0;
	virtual GType columnType (int col) const = 0;
	virtual void columnValue (int row, int col, GValue *value) = 0;
	virtual void moveRow (int row, int newRow) = 0;

	struct Listener {
		virtual void rowChanged (int row) = 0;
	};
	virtual void setListener (Listener *listener) = 0;
};

#define GTK_TYPE_MY_MODEL (gtk_my_model_get_type ())
#define GTK_MY_MODEL(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_MY_MODEL, GtkMyModel))
#define GTK_IS_MY_MODEL(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_TYPE_MY_MODEL))

struct GtkMyModel
{
	GObject parent;
	TableModel *model;
	struct Listener;
	Listener *listener;
};

struct GtkMyModelClass
{
	GObjectClass parent_class;
};

GtkTreeModel *gtk_my_model_new (TableModel *model);
GType gtk_my_model_get_type (void) G_GNUC_CONST;

int gtk_my_model_get_iter_row (GtkTreeIter *iter);

#endif /*MY_MODEL_H*/

