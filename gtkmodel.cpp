// gtkfeed.cpp

#include "gtkmodel.h"
#include <gtk/gtk.h>

static inline int get_iter_row (GtkTreeIter *iter)
{ return GPOINTER_TO_INT (iter->user_data); }

static inline void set_iter_row (GtkTreeIter *iter, int row)
{ iter->user_data = GINT_TO_POINTER (row); }

int gtk_my_model_get_iter_row (GtkTreeIter *iter)
{ return get_iter_row (iter); }

static int gtk_my_model_rows_nb (GtkTreeModel *model)
{
	GtkMyModel *mmodel = GTK_MY_MODEL (model);
	return mmodel->model->rowsNb();
}

static GtkTreeModelFlags gtk_my_model_get_flags (GtkTreeModel *model)
{
	return (GtkTreeModelFlags) GTK_TREE_MODEL_LIST_ONLY;
}

static gboolean gtk_my_model_get_iter (GtkTreeModel *model, GtkTreeIter *iter, GtkTreePath  *path)
{
	int row = *gtk_tree_path_get_indices (path);
	set_iter_row (iter, row);
	return row < gtk_my_model_rows_nb (model);
}

static GtkTreePath *gtk_my_model_get_path (GtkTreeModel *model, GtkTreeIter *iter)
{
	int row = get_iter_row (iter);
	GtkTreePath *path = gtk_tree_path_new();
	gtk_tree_path_append_index (path, row);
	return path;
}

static gboolean gtk_my_model_iter_next (GtkTreeModel *model, GtkTreeIter *iter)
{
	int row = get_iter_row (iter) + 1;
	set_iter_row (iter, row);
	return row < gtk_my_model_rows_nb (model);
}

static gint gtk_my_model_get_n_columns (GtkTreeModel *model)
{
	GtkMyModel *mmodel = GTK_MY_MODEL (model);
	return mmodel->model->columnsNb();
}

static GType gtk_my_model_get_column_type (GtkTreeModel *model, gint column)
{
	GtkMyModel *mmodel = GTK_MY_MODEL (model);
	return mmodel->model->columnType (column);
}

static void gtk_my_model_get_value (GtkTreeModel *model, GtkTreeIter *iter, gint col, GValue *value)
{
	GtkMyModel *mmodel = GTK_MY_MODEL (model);
	int row = get_iter_row (iter);
	mmodel->model->columnValue (row, col, value);
}

static gboolean gtk_my_model_iter_parent (GtkTreeModel *model, GtkTreeIter *iter, GtkTreeIter  *child)
{ return FALSE; }

static gboolean gtk_my_model_iter_has_child (GtkTreeModel *model, GtkTreeIter *iter)
{ return FALSE; }

static gint gtk_my_model_iter_n_children (GtkTreeModel *model, GtkTreeIter *iter)
{ return 0; }

static gboolean gtk_my_model_iter_nth_child (GtkTreeModel *model, GtkTreeIter *iter, GtkTreeIter *parent, gint n)
{ return FALSE; }

static gboolean gtk_my_model_iter_children (GtkTreeModel *model, GtkTreeIter *iter, GtkTreeIter  *parent)
{ return FALSE; }


struct GtkMyModel::Listener : public TableModel::Listener
{
	Listener (TableModel *table, GtkTreeModel *model)
	: model (model)
	{ table->setListener (this); }

	GtkTreeModel *model;

private:
	virtual void rowChanged (int row)
	{
		GtkTreeIter iter;
		set_iter_row (&iter, row);
		GtkTreePath *path = gtk_my_model_get_path (model, &iter);
		gtk_tree_model_row_changed (model, path, &iter);
		gtk_tree_path_free (path);
	}
};

static void gtk_my_model_tree_model_init (GtkTreeModelIface *iface);
static void gtk_my_model_drag_source_init (GtkTreeDragSourceIface *iface);
static void gtk_my_model_drag_dest_init (GtkTreeDragDestIface *iface);

G_DEFINE_TYPE_WITH_CODE (GtkMyModel, gtk_my_model, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL, gtk_my_model_tree_model_init)
	G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_DRAG_SOURCE, gtk_my_model_drag_source_init)
	G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_DRAG_DEST, gtk_my_model_drag_dest_init))

static void gtk_my_model_init (GtkMyModel *model)
{
}

static void gtk_my_model_finalize (GObject *object)
{
	GtkMyModel *mmodel = GTK_MY_MODEL (object);
	delete mmodel->listener;
	mmodel->listener = 0;
	G_OBJECT_CLASS (gtk_my_model_parent_class)->finalize (object);
}

static gboolean gtk_my_model_row_draggable (GtkTreeDragSource *drag_source,
                                            GtkTreePath       *path)
{ return TRUE; }
  
static gboolean gtk_my_model_drag_data_delete (GtkTreeDragSource *drag_source,
                                               GtkTreePath       *path)
{ return FALSE; }

static gboolean gtk_my_model_drag_data_get (GtkTreeDragSource *drag_source,
                            GtkTreePath *path, GtkSelectionData *selection_data)
{
  if (gtk_tree_set_row_drag_data (selection_data, GTK_TREE_MODEL (drag_source), path))
		return TRUE;
	return FALSE;
}

static gboolean gtk_my_model_drag_data_received (GtkTreeDragDest *drag_dest,
                             GtkTreePath *dst_path, GtkSelectionData *selection_data)
{
	// move feed from src_path to the position of dst_path
	GtkTreePath *src_path = NULL;
	GtkTreeModel *model = GTK_TREE_MODEL (drag_dest), *src_model;
	GtkMyModel *mmodel = GTK_MY_MODEL (model);
	gboolean ret = FALSE;
	if (gtk_tree_get_row_drag_data (selection_data, &src_model, &src_path))
		if (src_model == model) {
			int new_row = *gtk_tree_path_get_indices (dst_path);
			GtkTreeIter src_iter;
			if (gtk_my_model_get_iter (model, &src_iter, src_path)) {
				int row = get_iter_row (&src_iter);
				mmodel->model->moveRow (row, new_row);
				ret = TRUE;
			}
		}
	if (src_path)
		gtk_tree_path_free (src_path);
	return ret;
}

static gboolean gtk_my_model_row_drop_possible (GtkTreeDragDest *drag_dest,
                       GtkTreePath *dest_path, GtkSelectionData *selection_data)
{ return TRUE; }

static void gtk_my_model_drag_source_init (GtkTreeDragSourceIface *iface)
{
	iface->row_draggable = gtk_my_model_row_draggable;
	iface->drag_data_delete = gtk_my_model_drag_data_delete;
	iface->drag_data_get = gtk_my_model_drag_data_get;
}

static void gtk_my_model_drag_dest_init (GtkTreeDragDestIface *iface)
{
	iface->drag_data_received = gtk_my_model_drag_data_received;
	iface->row_drop_possible = gtk_my_model_row_drop_possible;
}

GtkTreeModel *gtk_my_model_new (TableModel *table)
{
	GtkTreeModel *model = (GtkTreeModel *) g_object_new (GTK_TYPE_MY_MODEL, NULL);
	GtkMyModel *mmodel = GTK_MY_MODEL (model);
	mmodel->model = table;
	mmodel->listener = new GtkMyModel::Listener (table, model);
	return model;
}

static void gtk_my_model_class_init (GtkMyModelClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = gtk_my_model_finalize;
}

static void gtk_my_model_tree_model_init (GtkTreeModelIface *iface)
{
	iface->get_flags = gtk_my_model_get_flags;
	iface->get_iter = gtk_my_model_get_iter;
	iface->get_path = gtk_my_model_get_path;
	iface->iter_next = gtk_my_model_iter_next;
	iface->iter_children = gtk_my_model_iter_children;
	iface->iter_has_child = gtk_my_model_iter_has_child;
	iface->iter_n_children = gtk_my_model_iter_n_children;
	iface->iter_nth_child = gtk_my_model_iter_nth_child;
	iface->iter_parent = gtk_my_model_iter_parent;
	iface->get_n_columns = gtk_my_model_get_n_columns;
	iface->get_column_type = gtk_my_model_get_column_type;
	iface->get_value = gtk_my_model_get_value;
}

