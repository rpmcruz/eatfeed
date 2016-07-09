// EatFeed - a bare bones alternative to bloated and problematic
// tools like RSSOwl and Liferea.
// by Ricardo Cruz <ricardo.pdm.cruz@gmail.com>
// Written in C because unfortunately there are no HTML renderer
// wrappers shipped with the base PyGtk bundle.

#include "feed.h"
#include "gtkmodel.h"
#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>

#ifdef USE_WEBKIT
#include <webkit/webkit.h>
#else
#ifdef USE_LIBGTKHTML
namespace gtk {
#include <libgtkhtml/view/htmlview.h>
};
#endif
#endif

#define VERSION "1.0.2"
#define DEFAULT_WIDTH 780

// utilities

static GtkWidget *create_scrolled_window (GtkWidget *child)
{
	GtkWidget *scroll = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroll),
		GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (scroll), child);
	return scroll;
}

static void scrolledWindowScrollUp (GtkWidget *widget)
{
	GtkScrolledWindow *scroll = GTK_SCROLLED_WINDOW (widget);
	GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment (scroll);
	gtk_adjustment_set_value (vadj, 0);  // scroll up
}

static GtkCellRenderer *appendTextViewColumn (GtkWidget *view, const char *title, int text_col, bool use_markup, int weight_col, int foreground_col, bool expand, bool fixed_size)
{
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *column;
	column = gtk_tree_view_column_new_with_attributes (title, renderer,
		use_markup ? "markup" : "text", text_col, NULL);
	if (weight_col >= 0)
		gtk_tree_view_column_add_attribute (column, renderer, "weight", weight_col);
	if (foreground_col >= 0)
		gtk_tree_view_column_add_attribute (column, renderer, "foreground", foreground_col);
	gtk_tree_view_column_set_resizable (column, TRUE);
	if (fixed_size)
		gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_append_column (GTK_TREE_VIEW (view), column);
	if (expand) {
		g_object_set (renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
		gtk_tree_view_column_set_expand (column, TRUE);
	}
	return renderer;
}

static GtkCellRenderer *appendCheckViewColumn (GtkWidget *view, const char *title, int active_col)
{
	GtkCellRenderer *renderer = gtk_cell_renderer_toggle_new();
	GtkTreeViewColumn *column;
	column = gtk_tree_view_column_new_with_attributes (title, renderer,
		"active", active_col, NULL);
	gtk_tree_view_column_set_resizable (column, FALSE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (view), column);
	return renderer;
}

static GtkCellRenderer *appendIconViewColumn (GtkWidget *view, const char *title, int pixbuf_col, int fixed_width)
{
	GtkCellRenderer *renderer = gtk_cell_renderer_pixbuf_new();
	GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes (
		title, renderer, "pixbuf", pixbuf_col, NULL);
	if (fixed_width > 0) {
		gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
		gtk_tree_view_column_set_fixed_width (column, fixed_width);
	}
	gtk_tree_view_append_column (GTK_TREE_VIEW (view), column);
	return renderer;
}

static void appendMenuItem (GtkMenu *menu, const gchar *stock,
                            GCallback callback, gpointer data)
{
	GtkWidget *item = gtk_image_menu_item_new_from_stock (stock, NULL);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	g_signal_connect (item, "activate", callback, data);
}

static void appendMenuItem (GtkMenu *menu, const char *label, const gchar *stock,
                            GCallback callback, gpointer data)
{
	GtkWidget *item = gtk_image_menu_item_new_with_label (label);
	if (stock)
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
			gtk_image_new_from_stock (stock, GTK_ICON_SIZE_MENU));
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	g_signal_connect (item, "activate", callback, data);
}

static void appendTableWidget (GtkTable *table, const char *label, GtkWidget *widget)
{
	GtkWidget *label_widget = gtk_label_new_with_mnemonic (label);
	gtk_misc_set_alignment (GTK_MISC (label_widget), 0, 0.5);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label_widget), widget);
	if (GTK_IS_ENTRY (widget))
		gtk_entry_set_activates_default (GTK_ENTRY (widget), TRUE);

	int n_cols;
	g_object_get (table, "n-columns", &n_cols, NULL);
	gtk_table_set_col_spacings (table, 6);
	gtk_table_resize (table, n_cols+1, 2);

	gtk_table_attach (table, label_widget, 0, 1, n_cols, n_cols+1, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach_defaults (table, widget, 1, 2, n_cols, n_cols+1);
}

static void open_url (const std::string &url)
{
	if (url.empty()) return;
	std::string cmd;
	cmd.reserve (30 + url.size());
	cmd = "/usr/bin/gnome-open \""; cmd += url; cmd += "\" &";
	system (cmd.c_str());
}

// pixmaps

#include "available.xpm"
#include "empty.xpm"

static GdkPixbuf *get_empty_pixbuf()
{
	static GdkPixbuf *pix = 0;
	if (!pix) {
		pix = gdk_pixbuf_new_from_xpm_data (empty_xpm);
		g_object_ref_sink (pix);
	}
	return pix;
}

static GdkPixbuf *get_available_pixbuf()
{
	static GdkPixbuf *pix = 0;
	if (!pix) {
		pix = gdk_pixbuf_new_from_xpm_data (available_xpm);
		g_object_ref_sink (pix);
	}
	return pix;
}

// News GtkTreeView

class FeedView : public TableModel
{
public:
	struct Listener {
		virtual void newsSelected (News *news) = 0;
	};
	void setListener (Listener *listener)
	{ this->listener = listener; }

private:
	GtkWidget *widget, *view;
	Listener *listener;
	Feed *feed;
	bool unreadToggled;  // ignore selected signal on toggle

	enum Columns { TITLE_COL, DATE_COL, WEIGHT_COL, WEIGHT_DATE_COL, UNREAD_COL,
		TOOLTIP_COL, TOTAL_COLS };

public:
	GtkWidget *getWidget() { return widget; }

	FeedView()
	: listener (NULL), feed (NULL), unreadToggled (false)
	{
		view = gtk_tree_view_new();
		GtkCellRenderer *renderer;
		renderer = appendCheckViewColumn (view, NULL, UNREAD_COL);
		g_signal_connect (renderer, "toggled", G_CALLBACK (unread_toggled_cb), this);
		appendTextViewColumn (view, "Title", TITLE_COL, false,
		                           WEIGHT_COL, -1, true, false);
//		appendTextViewColumn (view, "Author", AUTHOR_COL, false, WEIGHT_COL, -1, false, false);
		appendTextViewColumn (view, "Date", DATE_COL, false, WEIGHT_DATE_COL, -1, false, false);
		gtk_tree_view_set_tooltip_column (GTK_TREE_VIEW (view), TOOLTIP_COL);

		widget = create_scrolled_window (view);

		GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (view));
		gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
		g_signal_connect (selection, "changed", G_CALLBACK (news_selected), this);
		g_signal_connect (view, "row-activated", G_CALLBACK (news_double_clicked), this);
	}

	void setFeed (Feed *feed)
	{
		// destroy previous model before touching the feed pointer
		gtk_tree_view_set_model (GTK_TREE_VIEW (view), NULL);

		this->feed = feed;
		if (feed) {
			GtkTreeModel *model = gtk_my_model_new (this);
			gtk_tree_view_set_model (GTK_TREE_VIEW (view), model);
			g_object_unref (model);
		}
		scrolledWindowScrollUp (widget);
	}

	virtual int rowsNb() const
	{
		int rows;
		for (rows = 0; feed->getNews (rows); rows++) ;
		return rows;
	}

	virtual int columnsNb() const
	{ return TOTAL_COLS; }

	virtual GType columnType (int col) const
	{
		switch ((Columns) col) {
			case TITLE_COL:
			case DATE_COL:
			case TOOLTIP_COL:
				return G_TYPE_STRING;
			case UNREAD_COL:
				return G_TYPE_BOOLEAN;
			case WEIGHT_COL:
			case WEIGHT_DATE_COL:
				return G_TYPE_INT;
			case TOTAL_COLS: break;
		}
		return 0;
	}

	virtual void columnValue (int row, int col, GValue *value)
	{
		News *news = feed->getNews (row);
		g_value_init (value, columnType (col));
		switch ((Columns) col) {
			case TITLE_COL: {
				char *str = g_strdup (news->title().c_str());
				g_value_set_string (value, str);
				break;
			}
			case DATE_COL: {
				const char *str = news->date().c_str();
				if (!*str)
					str = news->updateDate().c_str();
				g_value_set_string (value, g_strdup (str));
				break;
			}
			case UNREAD_COL:
				g_value_set_boolean (value, !news->isRead());
				break;
			case TOOLTIP_COL: {
				std::string tooltip;
				tooltip.reserve (2048);
				gchar *title = g_markup_escape_text (news->title().c_str(), -1);
				tooltip += "<b>Title: </b>";
				tooltip += title;
				g_free (title);
				tooltip += "\n";
				if (!news->date().empty())
					tooltip += "\n<b>Date: </b>" + news->date();
				if (!news->updateDate().empty())
					tooltip += "\n<b>Update: </b>" + news->updateDate();
				if (!news->author().empty())
					tooltip += "\n<b>Author: </b>" + news->author();
				if (!news->categories().empty())
					tooltip += "\n<b>Categories: </b>" + news->categories();
				g_value_set_string (value, g_strdup (tooltip.c_str()));
				break;
			}
			case WEIGHT_DATE_COL:
#if 0
				if (!news->updateDate().empty()) {
					g_value_set_int (value, PANGO_WEIGHT_BOLD);
					break;
				}
#endif
			case WEIGHT_COL: {
				int weight = !news->isRead() ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL;
				g_value_set_int (value, weight);
				break;
			}
			case TOTAL_COLS: break;
		}
	}

	virtual void moveRow (int row, int newRow) {}
	virtual void setListener (TableModel::Listener *listener) {}

private:
	static void news_selected (GtkTreeSelection *selection, FeedView *pThis)
	{
		if (pThis->unreadToggled)
			return;
		GtkTreeModel *model;
		GtkTreeIter iter;
		if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
			int row = gtk_my_model_get_iter_row (&iter);
			News *news = pThis->feed->getNews (row);
			if (pThis->listener)
				pThis->listener->newsSelected (news);
			news->setRead (true);
		}
	}

	static void news_double_clicked (GtkTreeView *view, GtkTreePath *path,
	                                 GtkTreeViewColumn *column, FeedView *pThis)
	{
		GtkTreeModel *model = gtk_tree_view_get_model (view);
		GtkTreeIter iter;
		if (gtk_tree_model_get_iter (model, &iter, path)) {
			int row = gtk_my_model_get_iter_row (&iter);
			News *news = pThis->feed->getNews (row);
			open_url (news->link());
		}
	}

	static gboolean unread_after_cb (gpointer pData)
	{
		FeedView *pThis = (FeedView *) pData;
		pThis->unreadToggled = false;
		return FALSE;
	}

	static void unread_toggled_cb (GtkCellRendererToggle *renderer, char *path_str, FeedView *pThis)
	{
		GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (pThis->view));
		GtkTreeIter iter;
		if (gtk_tree_model_get_iter_from_string (model, &iter, path_str)) {
			int row = gtk_my_model_get_iter_row (&iter);
			News *news = pThis->feed->getNews (row);
			news->setRead (!news->isRead());

			g_idle_add (unread_after_cb, pThis);
			pThis->unreadToggled = true;
		}
	}
};

#include "xmlparser.h"

// Manager GtkTreeView

class ManagerView : public Manager::Listener, TableModel
{
public:
	struct Listener {
		virtual void feedSelected (Feed *feed) = 0;
	};
	void setListener (Listener *listener)
	{ this->listener = listener; }

private:
	GtkWidget *widget, *view;
	Listener *listener;
	TableModel::Listener *model_listener;

	enum Columns { TITLE_COL, TITLE_UNREAD_COL, WEIGHT_COL, COLOR_COL, ICON_COL, TOTAL_COLS };

public:
	GtkWidget *getWidget() { return widget; }

	ManagerView()
	: listener (NULL), model_listener (NULL)
	{
		view = gtk_tree_view_new();
		gtk_tree_view_set_reorderable (GTK_TREE_VIEW (view), TRUE);

		appendIconViewColumn (view, NULL, ICON_COL, 20);
		GtkCellRenderer *renderer;
		renderer = appendTextViewColumn (view, "Feeds",
			TITLE_UNREAD_COL, false, WEIGHT_COL,
			COLOR_COL, true, true);
		gtk_tree_view_set_fixed_height_mode (GTK_TREE_VIEW (view), TRUE);
		g_object_set (renderer, "editable", TRUE, NULL);
		g_signal_connect (renderer, "edited", G_CALLBACK (title_edited_cb), view);
		g_signal_connect (view, "button-press-event", G_CALLBACK (view_pressed_cb), this);
		g_signal_connect (view, "row-activated", G_CALLBACK (feed_double_clicked), this);

		GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (view));
		g_signal_connect (selection, "changed", G_CALLBACK (feed_selected_cb), this);

		widget = create_scrolled_window (view);
		load();

		Manager::get()->addListener (this);
	}

	Feed *getSelected()
	{
		GtkTreeModel *model;
		GtkTreeIter iter;
		GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (view));
		if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
			int row = gtk_my_model_get_iter_row (&iter);
			return Manager::get()->getFeed (row);
		}
		return NULL;
	}

	void selectClear()
	{
		GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (view));
		gtk_tree_selection_unselect_all (selection);
	}

	virtual int rowsNb() const
	{
		int rows;
		for (rows = 0; Manager::get()->getFeed (rows); rows++) ;
		return rows;
	}

	virtual int columnsNb() const
	{ return TOTAL_COLS; }

	virtual GType columnType (int col) const
	{
		switch ((Columns) col) {
			case TITLE_COL:
			case TITLE_UNREAD_COL:
			case COLOR_COL:
				return G_TYPE_STRING;
			case WEIGHT_COL:
				return G_TYPE_INT;
			case ICON_COL:
				return GDK_TYPE_PIXBUF;
			case TOTAL_COLS: break;
		}
		return 0;
	}

	virtual void columnValue (int row, int col, GValue *value)
	{
		Feed *feed = Manager::get()->getFeed (row);
		g_value_init (value, columnType (col));
		switch ((Columns) col) {
			case TITLE_COL: {
				const std::string &title = feed->title();
				g_value_set_string (value, g_strdup (title.c_str()));
				break;
			}
			case TITLE_UNREAD_COL: {
				int unread = feed->unreadNb();
				std::string title = feed->title();
				if (title.empty())
					title = feed->_url();
				char *str;
				if (unread) str = g_strdup_printf ("%s (%d)", title.c_str(), unread);
				else str = g_strdup (title.c_str());
				g_value_set_string (value, str);
				break;
			}
			case WEIGHT_COL: {
				int weight = feed->unreadNb() ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL;
				g_value_set_int (value, weight);
				break;
			}
			case COLOR_COL: {
				const char *color = 0;
				if (!feed->errorMsg().empty())
					color = "red";
				else if (feed->loading())
					color = "gray";
				g_value_set_string (value, color);
				break;
			}
			case ICON_COL: {
				GdkPixbuf *pixbuf = 0;
				const char *stock = GTK_STOCK_FILE; //DIRECTORY;
				if (!feed->errorMsg().empty())
					stock = GTK_STOCK_DIALOG_ERROR;
				else if (feed->loading())
					stock = GTK_STOCK_REFRESH;
				else
					pixbuf = (GdkPixbuf *) feed->iconPixbuf();
				if (!pixbuf) {
					pixbuf = gtk_widget_render_icon (
						widget, stock, GTK_ICON_SIZE_MENU, NULL);

					if (feed->loading()) {
						GdkPixbuf *old = pixbuf;
						pixbuf = gdk_pixbuf_copy (old);
						gdk_pixbuf_saturate_and_pixelate (old, pixbuf, 0.8, TRUE);
						g_object_unref (old);
					}
				}
				g_value_set_object (value, pixbuf);
				break;
			}
			case TOTAL_COLS: break;
		}
	}

	virtual void moveRow (int row, int newRow)
	{
		Feed* feed = Manager::get()->getFeed (row);
		Manager::get()->move (feed, newRow);
	}

	virtual void setListener (TableModel::Listener *listener)
	{ model_listener = listener; }

	void load()
	{
		GtkTreeModel *model = gtk_my_model_new (this);
		gtk_tree_view_set_model (GTK_TREE_VIEW (view), model);
		g_object_unref (model);
	}

	virtual void feedStatusChange (Manager *manager, Feed *feed)
	{
		int row = manager->getFeedNb (feed);
		model_listener->rowChanged (row);
	}

	virtual void feedLoading (Manager *manager, Feed *feed) {}
	virtual void feedLoaded (Manager *manager, Feed *feed) {}
	virtual void feedsLoadingProgress (Manager *manager, float fraction) {}

	virtual void startStructuralChange (Manager *manager)
	{ gtk_tree_view_set_model (GTK_TREE_VIEW (view), NULL); }

	virtual void endStructuralChange (Manager *manager)
	{ load(); }

private:
	static void feed_selected_cb (GtkTreeSelection *selection, ManagerView *pThis)
	{
		if (pThis->listener)
			pThis->listener->feedSelected (pThis->getSelected());
	}

	static void feed_double_clicked (GtkTreeView *view, GtkTreePath *path,
	                                 GtkTreeViewColumn *column, ManagerView *pThis)
	{
		GtkTreeModel *model = gtk_tree_view_get_model (view);
		GtkTreeIter iter;
		if (gtk_tree_model_get_iter (model, &iter, path)) {
			int row = gtk_my_model_get_iter_row (&iter);
			Feed *feed = Manager::get()->getFeed (row);
			open_url (feed->link());
		}
	}

	static void edit_activate_cb (GtkMenuItem *item, ManagerView *pThis)
	{
		GtkTreeView *view = GTK_TREE_VIEW (pThis->view);
		GtkTreeViewColumn *column = gtk_tree_view_get_column (view, 1);
		GtkTreeSelection *selection = gtk_tree_view_get_selection (view);
		GtkTreeIter iter;
		GtkTreeModel *model;
		if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
			GtkTreePath *path = gtk_tree_model_get_path (model, &iter);
			gtk_tree_view_set_cursor (view, path, column, TRUE);
			gtk_tree_path_free (path);
		}
	}

	static void remove_activate_cb (GtkWidget *widget, ManagerView *pThis)
	{
		Manager::get()->removeFeed (pThis->getSelected());
	}

	static void title_edited_cb (GtkCellRendererText *renderer, gchar *path,
	                             gchar *text, GtkTreeView *view)
	{
		GtkTreeModel *model = gtk_tree_view_get_model (view);
		GtkTreeIter iter;
		gtk_tree_model_get_iter_from_string (model, &iter, path);

		gchar *old_text;
		gtk_tree_model_get (model, &iter, TITLE_UNREAD_COL, &old_text, -1);
		if (strcmp (old_text, text) != 0) {
			int row = gtk_my_model_get_iter_row (&iter);
			Feed *feed = Manager::get()->getFeed (row);
			feed->setUserTitle (text);
		}
		g_free (old_text);
	}

	static gboolean view_pressed_cb (GtkWidget *view, GdkEventButton *event, ManagerView *pThis)
	{
		if (event->button == 3) {
			gboolean inreach;
			inreach = gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (view),
				event->x, event->y, 0, 0, 0, 0);
			if (inreach) {
				// hack to make it select the item
				event->button = 1;
				gtk_widget_event (view, (GdkEvent *) event);

				static GtkWidget *menu = 0;
				if (!menu) {
					menu = gtk_menu_new();
					appendMenuItem (GTK_MENU (menu), "Rename", GTK_STOCK_EDIT,
						G_CALLBACK (edit_activate_cb), pThis);
					appendMenuItem (GTK_MENU (menu), GTK_STOCK_REMOVE,
						G_CALLBACK (remove_activate_cb), pThis);
					gtk_widget_show_all (menu);
				}
				gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, 3, event->time);
			}
			return TRUE;
		}
		return FALSE;
	}
};

// HTML

#ifdef USE_WEBKIT
#warning "using webkit"

class HtmlView
{
public:
	struct Listener {
		virtual void over_url (const char *url) = 0;
	};
	void setListener (Listener *listener)
	{ this->listener = listener; }

private:
	GtkWidget *widget, *html;
	Listener *listener;
#if 0
	GTimeVal set_text_time;
#endif

public:
	GtkWidget *getWidget() { return widget; }

	HtmlView()
	: listener (NULL)
	{
		html = webkit_web_view_new();
		widget = create_scrolled_window (html);
		g_signal_connect (html, "populate-popup", G_CALLBACK (populate_popup_cb), this);
		g_signal_connect (html, "hovering-over-link", G_CALLBACK (over_link_cb), this);
		g_signal_connect (html, "navigation-requested",
			G_CALLBACK (navigation_requested_cb), this);  // url clicked
	}

	void setText (const std::string &text, const std::string &base_uri = "")
	{
#if 0
		g_get_current_time (&set_text_time);
#endif
		std::string t (text);

#if 1
		// remove flash as webkit hangs
		// <object> is used for Internet Explorer, <embed> for Netscape
		const char *withhold[] = { "<object", "</object>", "<embed", "</embed>", "<script", "</script>", 0 };
		for (int s = 0; withhold[s]; s += 2)
			for (std::string::size_type i = t.find (withhold[s]); i != std::string::npos;
				 i = t.find (withhold[s], i+1)) {
				std::string::size_type j = t.find (withhold[s+1], i) + strlen (withhold[s+1]);
				t.erase (i, j - i);
				if (s <= 3)
					t.insert (i, "<p><center><b>(Flash object omitted.)</b></center></p>");
			}
#endif

		// HACK: since webkit is not handling base_uri properly
//		webkit_web_view_load_html_string (view, t.c_str(), base_uri.c_str());

		const char *src_tags[] = { "href=", "src=", 0 };
		for (int s = 0; src_tags[s]; s++)
			for (std::string::size_type i = t.find (src_tags[s]); i != std::string::npos;
				 i = t.find (src_tags[s], i+1)) {
				int j = i + strlen (src_tags[s]);
				if (t.compare (j, 1, "\"") == 0)
					j += 1;
				else if (t.compare (j, 6, "&quot;") == 0)
					j += 6;
				if (t.compare (j, 7, "http://") != 0 && t.compare (j, 6, "ftp://") != 0)
					t.insert (j, base_uri);
			}

		WebKitWebView *view = WEBKIT_WEB_VIEW (html);
		webkit_web_view_load_html_string (view, t.c_str(), "");

		scrolledWindowScrollUp (widget);
	}

private:
	static void copy_activate_cb (GtkMenuItem *item, HtmlView *pThis)
	{
		webkit_web_view_copy_clipboard (WEBKIT_WEB_VIEW (pThis->html));
	}

	static std::string selectedText (WebKitWebView *view)
	{
		// HACK: webkit doesn't seem to provide a method to get selected text...
		webkit_web_view_copy_clipboard (view);
		GdkAtom atom = gdk_atom_intern_static_string ("CLIPBOARD");
		GtkClipboard *clipboard = gtk_clipboard_get (atom);
		gchar *text = gtk_clipboard_wait_for_text (clipboard);
		if (text) {
			std::string str (text);
			g_free (text);
			gtk_clipboard_clear (clipboard);
			return str;
		}
		gtk_widget_error_bell (GTK_WIDGET (view));
		return "";
	}

	static void define_activate_cb (GtkMenuItem *item, HtmlView *pThis)
	{
		std::string text = selectedText (WEBKIT_WEB_VIEW (pThis->html));
		std::string::size_type i = text.find (" ");
		if (i != std::string::npos && i != text.size()-1 /*forgive last white-space*/)
			gtk_widget_error_bell (pThis->html);
		else {
			//std::string url ("http://www.thefreedictionary.com/");
			std::string url ("http://dictionary.reference.com/browse/");
			url += text;
			open_url (url);
		}
	}

	static void google_activate_cb (GtkMenuItem *item, HtmlView *pThis)
	{
		std::string text = selectedText (WEBKIT_WEB_VIEW (pThis->html));
		for (unsigned int i = 0; i < text.length(); i++) {
			if (text[i] == ' ')
				text[i] = '+';
			else if (text[i] == '\"') {
				text[i] = '\\';
				text.insert (++i, "\"");
			}
		}
		std::string url ("http://www.google.com/search?q=");
		url += text;
		open_url (url);
	}

	static void populate_popup_cb (WebKitWebView *view, GtkMenu *menu, HtmlView *pThis)
	{
		GList *items = gtk_container_get_children (GTK_CONTAINER (menu));
		for (GList *i = items; i; i = i->next)
			gtk_container_remove (GTK_CONTAINER (menu), GTK_WIDGET (i->data));
		g_list_free (items);

		if (webkit_web_view_can_copy_clipboard (WEBKIT_WEB_VIEW (pThis->html))) {
			appendMenuItem (menu, GTK_STOCK_COPY, G_CALLBACK (copy_activate_cb), pThis);
			appendMenuItem (menu, "Google", GTK_STOCK_FIND,
				G_CALLBACK (google_activate_cb), pThis);
			appendMenuItem (menu, "Define", GTK_STOCK_FIND,
				G_CALLBACK (define_activate_cb), pThis);
		}
		gtk_widget_show_all (GTK_WIDGET (menu));
	}

	static void over_link_cb (WebKitWebView *view, const gchar *title,
	                          const gchar *uri, HtmlView *pThis)
	{
		if (pThis->listener)
			pThis->listener->over_url (uri);
	}

	static WebKitNavigationResponse navigation_requested_cb (
		WebKitWebView *web_view, WebKitWebFrame *frame, WebKitNetworkRequest *request,
		HtmlView *pThis)
	{
		const gchar *uri = webkit_network_request_get_uri (request);
#if 0
		// it has happened that "navigation-requested" was signaled without click
		// so make sure the user had time to click on something before open url
		GTimeVal now, &old = pThis->set_text_time;
		g_get_current_time (&now);
fprintf (stderr, "old: %ld . %ld - new: %ld . %ld\n", now.tv_sec, now.tv_usec, old.tv_sec, old.tv_usec);
		if (now.tv_sec - old.tv_sec > 0 || now.tv_usec - old.tv_usec > 20) {
			const gchar *uri = webkit_network_request_get_uri (request);
			open_url (uri);
		}
#else
		open_url (uri);
#endif
		return WEBKIT_NAVIGATION_RESPONSE_IGNORE;
    }
};

#else
#ifdef USE_LIBGTKHTML
#warning "using libgtkhtml"

class HtmlView
{
public:
	struct Listener {
		virtual void over_url (const char *url) = 0;
	};
	void setListener (Listener *listener)
	{ this->listener = listener; }

private:
	GtkWidget *widget, *html;
	Listener *listener;

public:
	GtkWidget *getWidget() { return widget; }

	HtmlView()
	: listener (NULL)
	{
		html = gtk::html_view_new();
		widget = create_scrolled_window (html);
		setText ("");
		g_signal_connect (html, "on-url", G_CALLBACK (on_url_cb), this);
	}

	void setText (const std::string &text, const std::string &base_uri = "")
	{
		const char *t = text.empty() ? "<p></p>" : text.c_str();
		gtk::HtmlDocument *document = gtk::html_document_new();
		g_signal_connect (document, "request-url", G_CALLBACK (request_url_cb), this);
		g_signal_connect (document, "link-clicked", G_CALLBACK (link_clicked_cb), this);
		gtk::html_document_open_stream (document, "text/html");
		gtk::html_document_write_stream (document, t, -1);
		gtk::html_document_close_stream (document);
		gtk::html_view_set_document ((gtk::HtmlView *) html, document);

		GtkAdjustment *vadj = gtk_layout_get_vadjustment (GTK_LAYOUT (html));
		gtk_adjustment_set_value (vadj, 0);  // scroll up
	}

private:
	static size_t download_write_cb (char *buffer, size_t size, size_t nitems, void *data)
	{
		gdk_threads_enter();
		gtk::HtmlStream *stream = (gtk::HtmlStream *) data;
		size_t len = size * nitems;
		gtk::html_stream_write (stream, buffer, len);
		gdk_flush();
		gdk_threads_leave();
		return len;
	}

	static void request_url_cb (gtk::HtmlDocument *document, const gchar *url,
	                            gtk::HtmlStream *stream, HtmlView *pThis)
	{ download_thread (url, download_write_cb, stream); }

	static void link_clicked_cb (gtk::HtmlDocument *document, const gchar *url, HtmlView *pThis)
	{
		open_url (url);
	}

	static void on_url_cb (gtk::HtmlView *view, const gchar *url, HtmlView *pThis)
	{
		if (pThis->listener)
			pThis->listener->over_url (url);
	}
};

#else
#error "No compatible HTML renderer detected: install webkit."
#endif
#endif

// Team up the window and the status icon
class Window
{
public:
	struct Listener {
		virtual void windowShow() = 0;
		virtual void windowHide() = 0;
	};
	void setListener (Listener *listener)
	{ this->listener = listener; }

private:
	GtkWidget *window;
	GtkStatusIcon *statusicon;
	Listener *listener;
	int x, y;  // re-position window on old pos on show()

public:
	Window()
	: listener (NULL), x (-1), y (-1)
	{
		window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
		setTitle ("");
		gtk_window_set_role (GTK_WINDOW (window), "eatfeed");
		int height = gdk_screen_get_height (gdk_screen_get_default());
		gtk_window_set_default_size (GTK_WINDOW (window), DEFAULT_WIDTH, height);
		g_signal_connect (window, "delete-event", G_CALLBACK (delete_event_cb), this);
		g_signal_connect (window, "window-state-event", G_CALLBACK (window_state_event_cb), this);

		statusicon = gtk_status_icon_new();
		g_signal_connect (statusicon, "activate", G_CALLBACK (status_activate_cb), this);
	}

	~Window()
	{
		g_object_unref (statusicon);
	}

	void setTitle (const std::string &title)
	{
		const char *str = title.c_str();
		if (title.empty())
			str = "Eat Feed";
		gtk_window_set_title (GTK_WINDOW (window), str);
	}

	void setIcon (GdkPixbuf *pixbuf)
	{
		gtk_status_icon_set_from_pixbuf (statusicon, pixbuf);
		gtk_window_set_icon (GTK_WINDOW (window), pixbuf);
	}

	void setChild (GtkWidget *child)
	{
		gtk_container_add (GTK_CONTAINER (window), child);
	}

	void setPopup (GtkWidget *menu)
	{
		gtk_widget_show_all (menu);
		g_signal_connect (statusicon, "popup-menu", G_CALLBACK (status_popup_cb), menu);
	}

	void setTooltip (const gchar *tooltip)
	{
		gtk_status_icon_set_tooltip (statusicon, tooltip);
	}

	void show()
	{
		bool visible = GTK_WIDGET_VISIBLE (window);
		if (!visible && listener)
			listener->windowShow();
		gtk_window_present (GTK_WINDOW (window));
		if (!visible && x != -1)
			gtk_window_move (GTK_WINDOW (window), x, y);
	}

	void hide()
	{
		bool visible = GTK_WIDGET_VISIBLE (window);
		if (visible)
			gtk_window_get_position (GTK_WINDOW (window), &x, &y);
		gtk_widget_hide (window);
		if (visible && listener)
			listener->windowHide();
	}

	void notify()
	{
		gtk_status_icon_set_blinking (statusicon, TRUE);
		g_timeout_add_seconds_full (G_PRIORITY_LOW, 15, blink_timeout, statusicon, NULL);
	}

private:
	static gboolean blink_timeout (gpointer data)
	{
		gtk_status_icon_set_blinking (GTK_STATUS_ICON (data), FALSE);
		return FALSE;
	}

	static gboolean delete_event_cb (GtkWidget *widget, GdkEvent *event, Window *pThis)
	{
		pThis->hide();
		return TRUE;
	}

	static gboolean window_state_event_cb (GtkWidget *widget, GdkEventWindowState *event,
	                                       Window *pThis)
	{
		// hide window when minimized, so user can just press the panel entry to hide it...
		if ((event->changed_mask & GDK_WINDOW_STATE_ICONIFIED) &&
		    (event->new_window_state & GDK_WINDOW_STATE_ICONIFIED)) {
			pThis->hide();
			gtk_window_deiconify (GTK_WINDOW (widget));
		}
		return FALSE;
	}

	static void status_activate_cb (GtkStatusIcon *status, Window *pThis)
	{
		GtkWindow *window = GTK_WINDOW (pThis->window);
		if (GTK_WIDGET_VISIBLE (window) && gtk_window_is_active (window))
			pThis->hide();
		else
			pThis->show();
	}

	static void status_popup_cb (GtkStatusIcon *status_icon, guint button,
	                             guint event_time, GtkMenu *menu)
	{
		gtk_menu_popup (menu, NULL, NULL, gtk_status_icon_position_menu,
		                status_icon, button, event_time);
	}
};

class App : public FeedView::Listener, ManagerView::Listener, HtmlView::Listener, Manager::Listener, Window::Listener
{
	HtmlView *html;
	FeedView *news;
	ManagerView *feeds;
	GtkWidget *statusbar, *progressbar, *refresh_button, *refresh_item;
	Window *window;

	virtual void newsSelected (News *news)
	{
		std::string text;
		const std::string &title = news->title();
		const std::string &author = news->author();
		const std::string &summary = news->summary();
		text.reserve (title.size() + author.size() + summary.size() + 200);
		text += "<p><b>" + title + "</b>";
		if (!author.empty())
			text += "<br /><small>by " + author + "</small>";
		text += "</p>\n";
		text += summary;
		text += "\n<p><a href=\"" + news->link() + "\">more</a></p>";
		html->setText (text, news->from()->link());
	}

	virtual void feedSelected (Feed *feed)
	{
		news->setFeed (feed);
		if (feed) {
			window->setTitle (feed->title());

			std::string text ("<html><p><b>");
			text += feed->oriTitle().empty() ? feed->title() : feed->oriTitle();
			text += "</b></p>\n";
			if (!feed->logo().empty())
				text += "<center><img src=\"" + feed->logo() + "\"></img></center>";
			if (!feed->description().empty())
				text += "<p>" + feed->description() + "</p>\n";
			if (!feed->author().empty())
				text += "<p>by " + feed->author() + "</p>\n";
			if (!feed->link().empty())
				text += "<p><a href=\"" + feed->link() + "\">" + feed->link() + "</a></p>\n";
			if (!feed->errorMsg().empty()) {
				text += "<br/><br/><p><font color=\"red\"><b>Error: </b></font>";
				text += feed->errorMsg() + "</p>\n";
			}
			text += "</html>";
			html->setText (text);
		}
		else {
			window->setTitle ("");
			html->setText ("");
		}
	}

	virtual void over_url (const char *url)
	{
		GtkStatusbar *s = GTK_STATUSBAR (statusbar);
		guint id = gtk_statusbar_get_context_id (s, "url");
		if (url)
			gtk_statusbar_push (s, id, url);
		else {
			// a msg might get stuck...
			gtk_statusbar_pop (s, id); gtk_statusbar_pop (s, id);
		}
	}

	static gboolean blink_timeout (gpointer data)
	{
		gtk_status_icon_set_blinking (GTK_STATUS_ICON (data), FALSE);
		return FALSE;
	}

	virtual void feedStatusChange (Manager *manager, Feed *feed)
	{
		int unread = manager->unreadNb();
		if (unread) {
			gchar *tooltip = g_strdup_printf ("%d fresh news", unread);
			window->setTooltip (tooltip);
			g_free (tooltip);
			window->setIcon (get_available_pixbuf());
		}
		else {
			window->setTooltip ("No unread news");
			window->setIcon (get_empty_pixbuf());
		}
	}

	// recreate FeedView model when loading news
	virtual void feedLoading (Manager *manager, Feed *feed)
	{
		if (feeds->getSelected() == feed)
			news->setFeed (NULL);
	}
	virtual void feedLoaded (Manager *manager, Feed *feed)
	{
		if (feeds->getSelected() == feed)
			news->setFeed (feed);

		GtkStatusbar *s = GTK_STATUSBAR (statusbar);
		guint id = gtk_statusbar_get_context_id (s, "loaded");
		std::string str = feed->title() + " loaded";
		gtk_statusbar_pop (s, id);
		gtk_statusbar_push (s, id, str.c_str());
	}

	virtual void feedsLoadingProgress (Manager *manager, float fraction)
	{
		if (fraction == 1) {
			gtk_widget_set_sensitive (refresh_button, TRUE);
			gtk_widget_set_sensitive (refresh_item, TRUE);
			gtk_widget_hide (progressbar);

			GtkStatusbar *s = GTK_STATUSBAR (statusbar);
			guint id = gtk_statusbar_get_context_id (s, "loaded");
			gtk_statusbar_pop (GTK_STATUSBAR (statusbar), id);

			static int old_unread = 0;
			int unread = manager->unreadNb();
			if (old_unread < unread)
				window->notify();
			old_unread = unread;
		}
		else {
			gtk_widget_set_sensitive (refresh_button, FALSE);
			gtk_widget_set_sensitive (refresh_item, FALSE);
			gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progressbar), fraction);
			gtk_widget_show (progressbar);
		}
	}

	virtual void startStructuralChange (Manager *manager)
	{
		news->setFeed (NULL);
	}

	virtual void endStructuralChange (Manager *manager) {}

	virtual void windowShow() {}

	virtual void windowHide()
	{
		feeds->selectClear();
	}

public:
	explicit App()
	{
		html = new HtmlView();
		html->setListener (this);
		news = new FeedView();
		news->setListener (this);
		feeds = new ManagerView();
		feeds->setListener (this);

		window = new Window();
		window->setListener (this);
		GtkWidget *vpaned = gtk_vpaned_new();
		gtk_paned_pack1 (GTK_PANED (vpaned), news->getWidget(), FALSE, FALSE);
		gtk_paned_pack2 (GTK_PANED (vpaned), html->getWidget(), TRUE, FALSE);
		gtk_paned_set_position (GTK_PANED (vpaned), 120);

		GtkWidget *hpaned = gtk_hpaned_new();
		gtk_paned_pack1 (GTK_PANED (hpaned), feeds->getWidget(), FALSE, FALSE);
		gtk_paned_pack2 (GTK_PANED (hpaned), vpaned, TRUE, FALSE);
		gtk_paned_set_position (GTK_PANED (hpaned), 160);

		GtkWidget *toolbar = gtk_toolbar_new();
		appendToolbar (toolbar, GTK_STOCK_ADD, "Add feed", true, G_CALLBACK (add_clicked_cb));
		refresh_button = appendToolbar (toolbar, GTK_STOCK_REFRESH, "Refresh all", true, G_CALLBACK (refresh_clicked_cb));
		gtk_toolbar_insert (GTK_TOOLBAR (toolbar), gtk_separator_tool_item_new(), -1);
		appendToolbar (toolbar, GTK_STOCK_ABOUT, NULL, false, G_CALLBACK (about_clicked_cb));
		appendToolbar (toolbar, GTK_STOCK_QUIT, NULL, false, G_CALLBACK (gtk_main_quit));

		statusbar = gtk_statusbar_new();
		progressbar = gtk_progress_bar_new();
		gtk_widget_set_size_request (progressbar, -1, 1);
		gtk_box_pack_start (GTK_BOX (statusbar), progressbar, FALSE, TRUE, 0);

		GtkWidget *vbox = gtk_vbox_new (FALSE, 0);
		gtk_box_pack_start (GTK_BOX (vbox), toolbar, FALSE, TRUE, 0);
		gtk_box_pack_start (GTK_BOX (vbox), hpaned, TRUE, TRUE, 0);
		gtk_box_pack_start (GTK_BOX (vbox), statusbar, FALSE, TRUE, 0);

		gtk_widget_show_all (vbox);
		window->setChild (vbox);

		GtkWidget *menu = gtk_menu_new(), *item;
		refresh_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_REFRESH, NULL);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), refresh_item);
		g_signal_connect (refresh_item, "activate", G_CALLBACK (refresh_clicked_cb), this);
		item = gtk_image_menu_item_new_from_stock (GTK_STOCK_QUIT, NULL);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		g_signal_connect (item, "activate", G_CALLBACK (gtk_main_quit), NULL);
		window->setPopup (menu);
		window->setIcon (get_empty_pixbuf());

		gtk_widget_hide (progressbar);
		Manager::get()->addListener (this);
		Manager::get()->refreshAll();
	}

	~App()
	{
		delete window;
		delete html;
		delete news;
		delete feeds;
	}

	void show()
	{ window->show(); }

private:
	GtkWidget *appendToolbar (GtkWidget *toolbar, const char *stock_id, const char *label,
	                          bool show_label, GCallback callback)
	{
		GtkToolItem *button = gtk_tool_button_new_from_stock (stock_id);
		std::string tooltip;
		if (label)
			tooltip = label;
		else {
			GtkStockItem item;
			if (gtk_stock_lookup (stock_id, &item)) {
				tooltip.reserve (strlen (item.label));
				for (int i = 0; i < item.label[i]; i++)
					if (item.label[i] != '_')
						tooltip += item.label[i];
			}
		}
		gtk_tool_item_set_is_important (button, show_label);
		if (!show_label)
			gtk_widget_set_tooltip_text (GTK_WIDGET (button), tooltip.c_str());
		gtk_tool_button_set_label (GTK_TOOL_BUTTON (button), label);
		gtk_toolbar_insert (GTK_TOOLBAR (toolbar), button, -1);
		g_signal_connect (button, "clicked", callback, this);
		return GTK_WIDGET (button);
	}

	static void add_clicked_cb (GtkToolButton *button, App *pThis)
	{
		GtkWidget *dialog = gtk_dialog_new_with_buttons (
			"", GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (button))),
			GtkDialogFlags (GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT),
			GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT, GTK_STOCK_ADD, GTK_RESPONSE_ACCEPT, NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
		gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

		GtkWidget *table = gtk_table_new (0, 0, FALSE);
		//GtkWidget *title_entry = gtk_entry_new();
		//appendTableWidget (GTK_TABLE (table), "_Name:", title_entry);
		GtkWidget *url_entry = gtk_entry_new();
		appendTableWidget (GTK_TABLE (table), "_Address:", url_entry);

		GtkWidget *codeset_combo = gtk_combo_box_entry_new_text();
		gtk_combo_box_append_text (GTK_COMBO_BOX (codeset_combo), "UTF-8");
		gtk_combo_box_append_text (GTK_COMBO_BOX (codeset_combo), "ISO-8859-1");
		gtk_combo_box_append_text (GTK_COMBO_BOX (codeset_combo), "ISO-8859-15");
		gtk_combo_box_set_active (GTK_COMBO_BOX (codeset_combo), 0);

		GtkWidget *more_table = gtk_table_new (0, 0, FALSE);
		appendTableWidget (GTK_TABLE (more_table), "Code_set:", codeset_combo);
		GtkWidget *expander = gtk_expander_new ("_More"), *align;
		gtk_expander_set_use_underline (GTK_EXPANDER (expander), TRUE);
		align = gtk_alignment_new (0, 0, 1, 1);
		gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 25, 0);
		gtk_container_add (GTK_CONTAINER (align), more_table);
		gtk_container_add (GTK_CONTAINER (expander), align);

		GtkWidget *vbox, *hbox, *label, *icon;
		vbox = gtk_vbox_new (FALSE, 6);
		label = gtk_label_new ("<b>Add feed</b>");
		gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
		gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
		gtk_misc_set_padding (GTK_MISC (label), 0, 6);
		gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, TRUE, 0);
		gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, TRUE, 0);
		gtk_box_pack_start (GTK_BOX (vbox), expander, FALSE, TRUE, 0);
		hbox = gtk_hbox_new (FALSE, 6);
		icon = gtk_image_new_from_stock (GTK_STOCK_ADD, GTK_ICON_SIZE_DIALOG);
		align = gtk_alignment_new (0, 0, 1, 0);
		gtk_container_add (GTK_CONTAINER (align), icon);
		gtk_box_pack_start (GTK_BOX (hbox), align, FALSE, TRUE, 0);
		gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);

		GtkWidget *box = GTK_DIALOG (dialog)->vbox;
		gtk_container_add (GTK_CONTAINER (box), hbox);
		gtk_widget_show_all (box);

		if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
			//const gchar *title = gtk_entry_get_text (GTK_ENTRY (title_entry));
			const gchar *url = gtk_entry_get_text (GTK_ENTRY (url_entry));
			gchar *codeset = gtk_combo_box_get_active_text (GTK_COMBO_BOX (codeset_combo));
			const char *_codeset = codeset ? codeset : "";
			if (gtk_combo_box_get_active (GTK_COMBO_BOX (codeset_combo)) <= 0)
				_codeset = "";

			Manager::get()->addFeed (url, "", _codeset)->refresh();
			if (codeset)
				g_free (codeset);
		}
		gtk_widget_destroy (dialog);
	}

	static void refresh_clicked_cb (GtkWidget *widget, App *pThis)
	{
		pThis->feeds->selectClear();
		Manager::get()->refreshAll();
	}

	static void about_dialog_activate_link_cb (
		GtkAboutDialog *about, const gchar *link, gpointer data)
	{ open_url (link); }

	static void about_clicked_cb (GtkToolButton *button, App *pThis)
	{
		GtkWidget *dialog = gtk_about_dialog_new();
		GtkAboutDialog *about = GTK_ABOUT_DIALOG (dialog);
		gtk_about_dialog_set_program_name (about, "Eat Feed");
		gtk_about_dialog_set_version (about, VERSION);
		const gchar *authors[] = { "Ricardo Cruz <rpmcruz@alunos.dcc.fc.up.pt>", 0 };
		gtk_about_dialog_set_authors (about, authors);
		gtk_about_dialog_set_logo (about, get_available_pixbuf());
		gtk_about_dialog_set_copyright (about, "(C) 2009 Ricardo Cruz");
		gtk_about_dialog_set_comments (about, "A bare bones news feed aggregator.");
		gtk_about_dialog_set_license (about,
			"This program is available under the GNU General Public License.\n"
			"http://www.gnu.org/licenses/gpl.html");
		gtk_about_dialog_set_url_hook (about_dialog_activate_link_cb, NULL, NULL);
		gtk_about_dialog_set_website_label (about, "Website");
		gtk_about_dialog_set_website (about, "http://www.alunos.dcc.fc.up.pt/~c0607045/trash/eatfeed/");
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
	}
};

int main (int argc, char *argv[])
{
	// FIXME: we shouldn't allow multiple EatFeed instances to run
	// This is particularly troublesome when adding a feed from Firefox

	g_thread_init (NULL);
	gdk_threads_init();

	gtk_init (&argc, &argv);

	bool hide = false;
	for (int i = 1; i < argc; i++) {
		if (!strcmp (argv[i], "--version"))
			printf ("Eat Feed v %s\nby Ricardo Cruz <rpmcruz@alunos.dcc.fc.up.pt>", VERSION);
		else if (!strcmp (argv[i], "--hide"))
			hide = true;
		else if (!strcmp (argv[i], "--delay"))
			sleep (30);
		else if (!strcmp (argv[i], "--help")) {
			printf ("%s [ADDRESS] [OPTIONS]\n"
				"\t--version\tPrint version number\n"
				"\t--hide\tOnly show status icon: useful for the startup launch\n"
				"\t--delay\tIdles for 30 secs: useful for the startup launch\n"
				"\t\twhen connecting to a wireless network\n"
				, argv[0]);
			exit (0);
		}
		else if (!strncmp (argv[i], "http://", 7) || !strncmp (argv[i], "feed://", 7))
			Manager::get()->addFeed (argv[i], "");
		else {
			printf ("I don't understand '%s'. Try --help.\n", argv[i]);
			exit(-1);
		}
	}

	App app;
	if (!hide)
		app.show();
	gdk_threads_enter();
	gtk_main();
	gdk_threads_leave();
	return 0;
}

