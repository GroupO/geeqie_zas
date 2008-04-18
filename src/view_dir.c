/*
 * Geeqie
 * (C) 2008 Vladimir Nadvornik
 *
 * Author: Laurent Monin
 *
 * This software is released under the GNU General Public License (GNU GPL).
 * Please read the included file COPYING for more information.
 * This software comes with no warranty of any kind, use at your own risk!
 */

#include "main.h"
#include "view_dir.h"

#include "dnd.h"
#include "dupe.h"
#include "filelist.h"
#include "layout_image.h"
#include "layout_util.h"
#include "ui_bookmark.h"
#include "ui_fileops.h"
#include "ui_tree_edit.h"
#include "ui_menu.h"
#include "utilops.h"
#include "view_dir_list.h"
#include "view_dir_tree.h"

GtkRadioActionEntry menu_view_dir_radio_entries[] = {
  { "FolderList",	NULL,		N_("_List"),		"<meta>L",	NULL, DIRVIEW_LIST },
  { "FolderTree",	NULL,		N_("_Tree"),		"<control>T",	NULL, DIRVIEW_TREE },
};

static void vd_destroy_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;

	if (vd->popup)
		{
		g_signal_handlers_disconnect_matched(G_OBJECT(vd->popup), G_SIGNAL_MATCH_DATA,
						     0, 0, 0, NULL, vd);
		gtk_widget_destroy(vd->popup);
		}

	switch(vd->type)
	{
	case DIRVIEW_LIST: vdlist_destroy_cb(widget, data); break;
	case DIRVIEW_TREE: vdtree_destroy_cb(widget, data); break;
	}
	
	if (vd->pf) folder_icons_free(vd->pf);
	if (vd->drop_list) filelist_free(vd->drop_list);

	if (vd->path) g_free(vd->path);
	if (vd->info) g_free(vd->info);

	g_free(vd);
}

ViewDir *vd_new(DirViewType type, const gchar *path)
{
	ViewDir *vd = g_new0(ViewDir, 1);

	vd->path = NULL;
	vd->click_fd = NULL;

	vd->drop_fd = NULL;
	vd->drop_list = NULL;
	vd->drop_scroll_id = -1;
	vd->drop_list = NULL;

	vd->popup = NULL;

	vd->dnd_drop_leave_func = NULL;
	vd->dnd_drop_update_func = NULL;

	vd->widget = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(vd->widget), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(vd->widget),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);

	vd->pf = folder_icons_new();

	switch(type)
	{
	case DIRVIEW_LIST: vd = vdlist_new(vd, path); break;
	case DIRVIEW_TREE: vd = vdtree_new(vd, path); break;
	}

	gtk_container_add(GTK_CONTAINER(vd->widget), vd->view);
	
	vd_dnd_init(vd);

	g_signal_connect(G_OBJECT(vd->view), "row_activated",
			 G_CALLBACK(vd_activate_cb), vd);
	g_signal_connect(G_OBJECT(vd->widget), "destroy",
			 G_CALLBACK(vd_destroy_cb), vd);
	g_signal_connect(G_OBJECT(vd->view), "key_press_event",
			 G_CALLBACK(vd_press_key_cb), vd);
	g_signal_connect(G_OBJECT(vd->view), "button_press_event",
			 G_CALLBACK(vd_press_cb), vd);
	g_signal_connect(G_OBJECT(vd->view), "button_release_event",
			 G_CALLBACK(vd_release_cb), vd);

	if (path) vd_set_path(vd, path);

	gtk_widget_show(vd->view);

	return vd;
}
	
void vd_set_select_func(ViewDir *vd,
                        void (*func)(ViewDir *vd, const gchar *path, gpointer data), gpointer data)
{
        vd->select_func = func;
        vd->select_data = data;
}

void vd_set_layout(ViewDir *vd, LayoutWindow *layout)
{
	vd->layout = layout;
}

gint vd_set_path(ViewDir *vd, const gchar *path)
{
	gint ret = FALSE;

	switch(vd->type)
	{
	case DIRVIEW_LIST: ret = vdlist_set_path(vd, path); break;
	case DIRVIEW_TREE: ret = vdtree_set_path(vd, path); break;
	}

	return ret;
}

void vd_refresh(ViewDir *vd)
{
	switch(vd->type)
	{
	case DIRVIEW_LIST: return vdlist_refresh(vd);
	case DIRVIEW_TREE: return vdtree_refresh(vd);
	}
}

const gchar *vd_row_get_path(ViewDir *vd, gint row)
{
	const gchar *ret = NULL;

	switch(vd->type)
	{
	case DIRVIEW_LIST: ret = vdlist_row_get_path(vd, row); break;
	case DIRVIEW_TREE: ret = vdtree_row_get_path(vd, row); break;
	}

	return ret;
}

void vd_select_row(ViewDir *vd, FileData *fd)
{
	switch(vd->type)
	{
	case DIRVIEW_LIST: vdlist_select_row(vd, fd); break;
	case DIRVIEW_TREE: vdtree_select_row(vd, fd); break;
	}
}

gint vd_find_row(ViewDir *vd, FileData *fd, GtkTreeIter *iter)
{
	gint ret = FALSE;

	switch(vd->type)
	{
	case DIRVIEW_LIST: ret = vdlist_find_row(vd, fd, iter); break;
	case DIRVIEW_TREE: ret = vdtree_find_row(vd, fd, iter, NULL); break;
	}

	return ret;
}

FileData *vd_get_fd_from_tree_path(ViewDir *vd, GtkTreeView *tview, GtkTreePath *tpath)
{
	GtkTreeIter iter;
	FileData *fd = NULL;
	GtkTreeModel *store;

	store = gtk_tree_view_get_model(tview);
	gtk_tree_model_get_iter(store, &iter, tpath);
	switch (vd->type)
		{
		case DIRVIEW_LIST:
			gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &fd, -1);
			break;
		case DIRVIEW_TREE:
			{
			NodeData *nd;
			gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &nd, -1);
			fd = (nd) ? nd->fd : NULL;
			};
			break;
		}

	return fd;
}

static gint vd_rename_cb(TreeEditData *td, const gchar *old, const gchar *new, gpointer data)
{
	ViewDir *vd = data;
	FileData *fd;
	gchar *old_path;
	gchar *new_path;
	gchar *base;

	fd = vd_get_fd_from_tree_path(vd, GTK_TREE_VIEW(vd->view), td->path);
	if (!fd) return FALSE;

	old_path = g_strdup(fd->path);

	base = remove_level_from_path(old_path);
	new_path = concat_dir_and_file(base, new);
	g_free(base);

	if (file_util_rename_dir(fd, new_path, vd->view))
		{

		if (vd->type == DIRVIEW_TREE) vdtree_populate_path(vd, new_path, TRUE, TRUE);
		if (vd->layout && strcmp(vd->path, old_path) == 0)
			{
			layout_set_path(vd->layout, new_path);
			}
		else
			{
			if (vd->type == DIRVIEW_LIST) vd_refresh(vd);
			}
		}

	g_free(old_path);
	g_free(new_path);

	return FALSE;
}

static void vd_rename_by_data(ViewDir *vd, FileData *fd)
{
	GtkTreeModel *store;
	GtkTreePath *tpath;
	GtkTreeIter iter;

	if (!fd || vd_find_row(vd, fd, &iter) < 0) return;
	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));
	tpath = gtk_tree_model_get_path(store, &iter);

	tree_edit_by_path(GTK_TREE_VIEW(vd->view), tpath, 0, fd->name,
			  vd_rename_cb, vd);
	gtk_tree_path_free(tpath);
}


void vd_color_set(ViewDir *vd, FileData *fd, gint color_set)
{
	GtkTreeModel *store;
	GtkTreeIter iter;

	if (vd_find_row(vd, fd, &iter) < 0) return;
	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));

	switch(vd->type)
	{
	case DIRVIEW_LIST:
		gtk_list_store_set(GTK_LIST_STORE(store), &iter, DIR_COLUMN_COLOR, color_set, -1);
		break;
	case DIRVIEW_TREE:
		gtk_tree_store_set(GTK_TREE_STORE(store), &iter, DIR_COLUMN_COLOR, color_set, -1);
		break;
	}
}

void vd_popup_destroy_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;

	vd_color_set(vd, vd->click_fd, FALSE);
	vd->click_fd = NULL;
	vd->popup = NULL;

	vd_color_set(vd, vd->drop_fd, FALSE);
	filelist_free(vd->drop_list);
	vd->drop_list = NULL;
	vd->drop_fd = NULL;
}

/*
 *-----------------------------------------------------------------------------
 * drop menu (from dnd)
 *-----------------------------------------------------------------------------
 */

static void vd_drop_menu_copy_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;
	const gchar *path;
	GList *list;

	if (!vd->drop_fd) return;

	path = vd->drop_fd->path;
	list = vd->drop_list;
	vd->drop_list = NULL;

	file_util_copy_simple(list, path);
}

static void vd_drop_menu_move_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;
	const gchar *path;
	GList *list;

	if (!vd->drop_fd) return;

	path = vd->drop_fd->path;
	list = vd->drop_list;

	vd->drop_list = NULL;

	file_util_move_simple(list, path);
}

GtkWidget *vd_drop_menu(ViewDir *vd, gint active)
{
	GtkWidget *menu;

	menu = popup_menu_short_lived();
	g_signal_connect(G_OBJECT(menu), "destroy",
			 G_CALLBACK(vd_popup_destroy_cb), vd);

	menu_item_add_stock_sensitive(menu, _("_Copy"), GTK_STOCK_COPY, active,
				      G_CALLBACK(vd_drop_menu_copy_cb), vd);
	menu_item_add_sensitive(menu, _("_Move"), active, G_CALLBACK(vd_drop_menu_move_cb), vd);

	menu_item_add_divider(menu);
	menu_item_add_stock(menu, _("Cancel"), GTK_STOCK_CANCEL, NULL, vd);

	return menu;
}

/*
 *-----------------------------------------------------------------------------
 * pop-up menu
 *-----------------------------------------------------------------------------
 */ 

static void vd_pop_menu_up_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;
	gchar *path;

	if (!vd->path || strcmp(vd->path, "/") == 0) return;
	path = remove_level_from_path(vd->path);

	if (vd->select_func)
		{
		vd->select_func(vd, path, vd->select_data);
		}

	g_free(path);
}

static void vd_pop_menu_slide_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;
	gchar *path;

	if (!vd->layout) return;
	if (!vd->click_fd) return;

	path = vd->click_fd->path;

	layout_set_path(vd->layout, path);
	layout_select_none(vd->layout);
	layout_image_slideshow_stop(vd->layout);
	layout_image_slideshow_start(vd->layout);
}

static void vd_pop_menu_slide_rec_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;
	gchar *path;
	GList *list;

	if (!vd->layout) return;
	if (!vd->click_fd) return;

	path = vd->click_fd->path;

	list = filelist_recursive(path);

	layout_image_slideshow_stop(vd->layout);
	layout_image_slideshow_start_from_list(vd->layout, list);
}

static void vd_pop_menu_dupe(ViewDir *vd, gint recursive)
{
	DupeWindow *dw;
	GList *list = NULL;

	if (!vd->click_fd) return;

	if (recursive)
		{
		list = g_list_append(list, file_data_ref(vd->click_fd));
		}
	else
		{
		filelist_read(vd->click_fd->path, &list, NULL);
		list = filelist_filter(list, FALSE);
		}

	dw = dupe_window_new(DUPE_MATCH_NAME);
	dupe_window_add_files(dw, list, recursive);

	filelist_free(list);
}

static void vd_pop_menu_dupe_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;
	vd_pop_menu_dupe(vd, FALSE);
}

static void vd_pop_menu_dupe_rec_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;
	vd_pop_menu_dupe(vd, TRUE);
}

static void vd_pop_menu_delete_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;

	if (!vd->click_fd) return;
	file_util_delete_dir(vd->click_fd, vd->widget);
}

static void vd_pop_menu_dir_view_as_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;
	DirViewType new_type = DIRVIEW_LIST;

	if (!vd->layout) return;

	switch(vd->type)
	{
	case DIRVIEW_LIST: new_type = DIRVIEW_TREE; break;
	case DIRVIEW_TREE: new_type = DIRVIEW_LIST; break;
	}
	
	layout_views_set(vd->layout, new_type, vd->layout->icon_view);
}

static void vd_pop_menu_refresh_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;

	if (vd->layout) layout_refresh(vd->layout);
}

static void vd_toggle_show_hidden_files_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;

	options->file_filter.show_hidden_files = !options->file_filter.show_hidden_files;
	if (vd->layout) layout_refresh(vd->layout);
}

static void vd_pop_menu_new_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;
	const gchar *path = NULL;
	gchar *new_path;
	gchar *buf;

	switch(vd->type)
		{
		case DIRVIEW_LIST:
			{
			if (!vd->path) return;
			path = vd->path;
			};
			break;
		case DIRVIEW_TREE:
			{
			if (!vd->click_fd) return;
			path = vd->click_fd->path;
			};
			break;
		}

	buf = concat_dir_and_file(path, _("new_folder"));
	new_path = unique_filename(buf, NULL, NULL, FALSE);
	g_free(buf);
	if (!new_path) return;

	if (!mkdir_utf8(new_path, 0755))
		{
		gchar *text;

		text = g_strdup_printf(_("Unable to create folder:\n%s"), new_path);
		file_util_warning_dialog(_("Error creating folder"), text, GTK_STOCK_DIALOG_ERROR, vd->view);
		g_free(text);
		}
	else
		{
		FileData *fd = NULL;

		switch(vd->type)
			{
			case DIRVIEW_LIST:
				{
				vd_refresh(vd);
				fd = vdlist_row_by_path(vd, new_path, NULL);
				};
				break;
			case DIRVIEW_TREE:
				fd = vdtree_populate_path(vd, new_path, TRUE, TRUE);
				break;
			}
		vd_rename_by_data(vd, fd);
		}

	g_free(new_path);
}

static void vd_pop_menu_rename_cb(GtkWidget *widget, gpointer data)
{
	ViewDir *vd = data;
	
	vd_rename_by_data(vd, vd->click_fd);
}

GtkWidget *vd_pop_menu(ViewDir *vd, FileData *fd)
{
	GtkWidget *menu;
	gint active;
	gint rename_delete_active = FALSE;
	gint new_folder_active = FALSE;

	active = (fd != NULL);
	switch(vd->type)
		{
		case DIRVIEW_LIST:
			{
			/* check using . (always row 0) */
			new_folder_active = (vd->path && access_file(vd->path , W_OK | X_OK));

			/* ignore .. and . */
			rename_delete_active = (new_folder_active && fd &&
				strcmp(fd->name, ".") != 0 &&
	  			strcmp(fd->name, "..") != 0 &&
	  			access_file(fd->path, W_OK | X_OK));
			};
			break;
		case DIRVIEW_TREE:
			{
			if (fd)
				{
				gchar *parent;
				new_folder_active = (fd && access_file(fd->path, W_OK | X_OK));
				parent = remove_level_from_path(fd->path);
				rename_delete_active = access_file(parent, W_OK | X_OK);
				g_free(parent);
				};
			}
			break;
		}

	menu = popup_menu_short_lived();
	g_signal_connect(G_OBJECT(menu), "destroy",
			 G_CALLBACK(vd_popup_destroy_cb), vd);

	menu_item_add_stock_sensitive(menu, _("_Up to parent"), GTK_STOCK_GO_UP,
				      (vd->path && strcmp(vd->path, "/") != 0),
				      G_CALLBACK(vd_pop_menu_up_cb), vd);

	menu_item_add_divider(menu);
	menu_item_add_sensitive(menu, _("_Slideshow"), active,
				G_CALLBACK(vd_pop_menu_slide_cb), vd);
	menu_item_add_sensitive(menu, _("Slideshow recursive"), active,
				G_CALLBACK(vd_pop_menu_slide_rec_cb), vd);

	menu_item_add_divider(menu);
	menu_item_add_stock_sensitive(menu, _("Find _duplicates..."), GTK_STOCK_FIND, active,
				      G_CALLBACK(vd_pop_menu_dupe_cb), vd);
	menu_item_add_stock_sensitive(menu, _("Find duplicates recursive..."), GTK_STOCK_FIND, active,
				      G_CALLBACK(vd_pop_menu_dupe_rec_cb), vd);

	menu_item_add_divider(menu);

	menu_item_add_sensitive(menu, _("_New folder..."), new_folder_active,
				G_CALLBACK(vd_pop_menu_new_cb), vd);

	menu_item_add_sensitive(menu, _("_Rename..."), rename_delete_active,
				G_CALLBACK(vd_pop_menu_rename_cb), vd);
	menu_item_add_stock_sensitive(menu, _("_Delete..."), GTK_STOCK_DELETE, rename_delete_active,
				      G_CALLBACK(vd_pop_menu_delete_cb), vd);

	menu_item_add_divider(menu);
	/* FIXME */
	menu_item_add_check(menu, _("View as _tree"), vd->type,
			    G_CALLBACK(vd_pop_menu_dir_view_as_cb), vd);
	menu_item_add_check(menu, _("Show _hidden files"), options->file_filter.show_hidden_files,
			    G_CALLBACK(vd_toggle_show_hidden_files_cb), vd);

	menu_item_add_stock(menu, _("Re_fresh"), GTK_STOCK_REFRESH,
			    G_CALLBACK(vd_pop_menu_refresh_cb), vd);

	return menu;
}

/*
 *-----------------------------------------------------------------------------
 * dnd
 *-----------------------------------------------------------------------------
 */

static GtkTargetEntry vd_dnd_drop_types[] = {
	{ "text/uri-list", 0, TARGET_URI_LIST }
};
static gint vd_dnd_drop_types_count = 1;

static void vd_dest_set(ViewDir *vd, gint enable)
{
	if (enable)
		{
		gtk_drag_dest_set(vd->view,
				  GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_DROP,
				  vd_dnd_drop_types, vd_dnd_drop_types_count,
				  GDK_ACTION_MOVE | GDK_ACTION_COPY);
		}
	else
		{
		gtk_drag_dest_unset(vd->view);
		}
}

static void vd_dnd_get(GtkWidget *widget, GdkDragContext *context,
			   GtkSelectionData *selection_data, guint info,
			   guint time, gpointer data)
{
	ViewDir *vd = data;
	GList *list;
	gchar *uritext = NULL;
	gint length = 0;

	if (!vd->click_fd) return;

	switch (info)
		{
		case TARGET_URI_LIST:
		case TARGET_TEXT_PLAIN:
			list = g_list_prepend(NULL, vd->click_fd);
			uritext = uri_text_from_filelist(list, &length, (info == TARGET_TEXT_PLAIN));
			g_list_free(list);
			break;
		}
	if (uritext)
		{
		gtk_selection_data_set (selection_data, selection_data->target,
				8, (guchar *)uritext, length);
		g_free(uritext);
		}
}

static void vd_dnd_begin(GtkWidget *widget, GdkDragContext *context, gpointer data)
{
	ViewDir *vd = data;

	vd_color_set(vd, vd->click_fd, TRUE);
	vd_dest_set(vd, FALSE);
}

static void vd_dnd_end(GtkWidget *widget, GdkDragContext *context, gpointer data)
{
	ViewDir *vd = data;

	vd_color_set(vd, vd->click_fd, FALSE);

	if (vd->type == DIRVIEW_LIST && context->action == GDK_ACTION_MOVE)
		{
		vd_refresh(vd);
		}
	vd_dest_set(vd, TRUE);
}

static void vd_dnd_drop_receive(GtkWidget *widget,
				    GdkDragContext *context, gint x, gint y,
				    GtkSelectionData *selection_data, guint info,
				    guint time, gpointer data)
{
	ViewDir *vd = data;
	GtkTreePath *tpath;
	FileData *fd = NULL;

	vd->click_fd = NULL;

	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), x, y,
					  &tpath, NULL, NULL, NULL))
		{
		fd = vd_get_fd_from_tree_path(vd, GTK_TREE_VIEW(widget), tpath);
		gtk_tree_path_free(tpath);
		}

	if (!fd) return;

	if (info == TARGET_URI_LIST)
		{
		GList *list;
		gint active;

		list = uri_filelist_from_text((gchar *)selection_data->data, TRUE);
		if (!list) return;

		active = access_file(fd->path, W_OK | X_OK);

		vd_color_set(vd, fd, TRUE);
		vd->popup = vd_drop_menu(vd, active);
		gtk_menu_popup(GTK_MENU(vd->popup), NULL, NULL, NULL, NULL, 0, time);

		vd->drop_fd = fd;
		vd->drop_list = list;
		}
}

static void vd_dnd_drop_update(ViewDir *vd, gint x, gint y)
{
	GtkTreePath *tpath;
	FileData *fd = NULL;

	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vd->view), x, y,
					  &tpath, NULL, NULL, NULL))
		{
		fd = vd_get_fd_from_tree_path(vd, GTK_TREE_VIEW(vd->view), tpath);
		gtk_tree_path_free(tpath);
		}

	if (fd != vd->drop_fd)
		{
		vd_color_set(vd, vd->drop_fd, FALSE);
		vd_color_set(vd, fd, TRUE);
		if (fd && vd->dnd_drop_update_func) vd->dnd_drop_update_func(vd);
		}

	vd->drop_fd = fd;
}

void vd_dnd_drop_scroll_cancel(ViewDir *vd)
{
	if (vd->drop_scroll_id != -1) g_source_remove(vd->drop_scroll_id);
	vd->drop_scroll_id = -1;
}

static gint vd_auto_scroll_idle_cb(gpointer data)
{
	ViewDir *vd = data;

	if (vd->drop_fd)
		{
		GdkWindow *window;
		gint x, y;
		gint w, h;

		window = vd->view->window;
		gdk_window_get_pointer(window, &x, &y, NULL);
		gdk_drawable_get_size(window, &w, &h);
		if (x >= 0 && x < w && y >= 0 && y < h)
			{
			vd_dnd_drop_update(vd, x, y);
			}
		}

	vd->drop_scroll_id = -1;
	return FALSE;
}

static gint vd_auto_scroll_notify_cb(GtkWidget *widget, gint x, gint y, gpointer data)
{
	ViewDir *vd = data;

	if (!vd->drop_fd || vd->drop_list) return FALSE;

	if (vd->drop_scroll_id == -1) vd->drop_scroll_id = g_idle_add(vd_auto_scroll_idle_cb, vd);

	return TRUE;
}

static gint vd_dnd_drop_motion(GtkWidget *widget, GdkDragContext *context,
				   gint x, gint y, guint time, gpointer data)
{
        ViewDir *vd = data;

	vd->click_fd = NULL;

	if (gtk_drag_get_source_widget(context) == vd->view)
		{
		/* from same window */
		gdk_drag_status(context, 0, time);
		return TRUE;
		}
	else
		{
		gdk_drag_status(context, context->suggested_action, time);
		}

	vd_dnd_drop_update(vd, x, y);

	if (vd->drop_fd)
		{
		GtkAdjustment *adj = gtk_tree_view_get_vadjustment(GTK_TREE_VIEW(vd->view));
		widget_auto_scroll_start(vd->view, adj, -1, -1, vd_auto_scroll_notify_cb, vd);
		}

	return FALSE;
}

static void vd_dnd_drop_leave(GtkWidget *widget, GdkDragContext *context, guint time, gpointer data)
{
	ViewDir *vd = data;

	if (vd->drop_fd != vd->click_fd) vd_color_set(vd, vd->drop_fd, FALSE);

	vd->drop_fd = NULL;

	if (vd->dnd_drop_leave_func) vd->dnd_drop_leave_func(vd);
}

void vd_dnd_init(ViewDir *vd)
{
	gtk_drag_source_set(vd->view, GDK_BUTTON1_MASK | GDK_BUTTON2_MASK,
			    dnd_file_drag_types, dnd_file_drag_types_count,
			    GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_ASK);
	g_signal_connect(G_OBJECT(vd->view), "drag_data_get",
			 G_CALLBACK(vd_dnd_get), vd);
	g_signal_connect(G_OBJECT(vd->view), "drag_begin",
			 G_CALLBACK(vd_dnd_begin), vd);
	g_signal_connect(G_OBJECT(vd->view), "drag_end",
			 G_CALLBACK(vd_dnd_end), vd);

	vd_dest_set(vd, TRUE);
	g_signal_connect(G_OBJECT(vd->view), "drag_data_received",
			 G_CALLBACK(vd_dnd_drop_receive), vd);
	g_signal_connect(G_OBJECT(vd->view), "drag_motion",
			 G_CALLBACK(vd_dnd_drop_motion), vd);
	g_signal_connect(G_OBJECT(vd->view), "drag_leave",
			 G_CALLBACK(vd_dnd_drop_leave), vd);
}

/*
 *----------------------------------------------------------------------------
 * callbacks
 *----------------------------------------------------------------------------
 */

void vd_menu_position_cb(GtkMenu *menu, gint *x, gint *y, gboolean *push_in, gpointer data)
{
	ViewDir *vd = data;
	GtkTreeModel *store;
	GtkTreeIter iter;
	GtkTreePath *tpath;
	gint cw, ch;

	if (vd_find_row(vd, vd->click_fd, &iter) < 0) return;
	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));
	tpath = gtk_tree_model_get_path(store, &iter);
	tree_view_get_cell_clamped(GTK_TREE_VIEW(vd->view), tpath, 0, TRUE, x, y, &cw, &ch);
	gtk_tree_path_free(tpath);
	*y += ch;
	popup_menu_position_clamp(menu, x, y, 0);
}

void vd_activate_cb(GtkTreeView *tview, GtkTreePath *tpath, GtkTreeViewColumn *column, gpointer data)
{
	ViewDir *vd = data;
	FileData *fd = vd_get_fd_from_tree_path(vd, tview, tpath);

	vd_select_row(vd, fd);
}

static GdkColor *vd_color_shifted(GtkWidget *widget)
{
	static GdkColor color;
	static GtkWidget *done = NULL;

	if (done != widget)
		{
		GtkStyle *style;

		style = gtk_widget_get_style(widget);
		memcpy(&color, &style->base[GTK_STATE_NORMAL], sizeof(color));
		shift_color(&color, -1, 0);
		done = widget;
		}

	return &color;
}

void vd_color_cb(GtkTreeViewColumn *tree_column, GtkCellRenderer *cell,
		 GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer data)
{
	ViewDir *vd = data;
	gboolean set;

	gtk_tree_model_get(tree_model, iter, DIR_COLUMN_COLOR, &set, -1);
	g_object_set(G_OBJECT(cell),
		     "cell-background-gdk", vd_color_shifted(vd->view),
		     "cell-background-set", set, NULL);
}

gint vd_release_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	ViewDir *vd = data;
	GtkTreePath *tpath;
	FileData *fd = NULL;

	if (!vd->click_fd) return FALSE;
	vd_color_set(vd, vd->click_fd, FALSE);

	if (bevent->button != 1) return TRUE;

	if ((bevent->x != 0 || bevent->y != 0) &&
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), bevent->x, bevent->y,
					  &tpath, NULL, NULL, NULL))
		{
		fd = vd_get_fd_from_tree_path(vd, GTK_TREE_VIEW(widget), tpath);
		gtk_tree_path_free(tpath);
		}

	if (fd && vd->click_fd == fd)
		{
		vdlist_select_row(vd, vd->click_fd);
		}

	return FALSE;
}

gint vd_press_key_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	ViewDir *vd = data;
	gint ret = FALSE;

	switch(vd->type)
	{
	case DIRVIEW_LIST: ret = vdlist_press_key_cb(widget, event, data); break;
	case DIRVIEW_TREE: ret = vdtree_press_key_cb(widget, event, data); break;
	}

	return ret;
}

gint vd_press_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	ViewDir *vd = data;
	gint ret = FALSE;

	switch(vd->type)
	{
	case DIRVIEW_LIST: ret = vdlist_press_cb(widget, bevent, data); break;
	case DIRVIEW_TREE: ret = vdtree_press_cb(widget, bevent, data); break;
	}

	return ret;
}