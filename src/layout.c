/*
 * Geeqie
 * (C) 2006 John Ellis
 * Copyright (C) 2008 The Geeqie Team
 *
 * Author: John Ellis
 *
 * This software is released under the GNU General Public License (GNU GPL).
 * Please read the included file COPYING for more information.
 * This software comes with no warranty of any kind, use at your own risk!
 */

#include "main.h"
#include "layout.h"

#include "color-man.h"
#include "filedata.h"
#include "histogram.h"
#include "image.h"
#include "image-overlay.h"
#include "layout_config.h"
#include "layout_image.h"
#include "layout_util.h"
#include "menu.h"
#include "pixbuf-renderer.h"
#include "pixbuf_util.h"
#include "utilops.h"
#include "view_dir.h"
#include "view_file.h"
#include "ui_fileops.h"
#include "ui_menu.h"
#include "ui_misc.h"
#include "ui_tabcomp.h"
#include "window.h"

#ifdef HAVE_LIRC
#include "lirc.h"
#endif

#define MAINWINDOW_DEF_WIDTH 700
#define MAINWINDOW_DEF_HEIGHT 500

#define MAIN_WINDOW_DIV_HPOS (MAINWINDOW_DEF_WIDTH / 2)
#define MAIN_WINDOW_DIV_VPOS (MAINWINDOW_DEF_HEIGHT / 2)

#define TOOLWINDOW_DEF_WIDTH 260
#define TOOLWINDOW_DEF_HEIGHT 450

#define PROGRESS_WIDTH 150
#define ZOOM_LABEL_WIDTH 64

#define PANE_DIVIDER_SIZE 10


GList *layout_window_list = NULL;


static void layout_list_scroll_to_subpart(LayoutWindow *lw, const gchar *needle);


/*
 *-----------------------------------------------------------------------------
 * misc
 *-----------------------------------------------------------------------------
 */

gint layout_valid(LayoutWindow **lw)
{
	if (*lw == NULL)
		{
		if (layout_window_list) *lw = layout_window_list->data;
		return (*lw != NULL);
		}

	return (g_list_find(layout_window_list, *lw) != NULL);
}

LayoutWindow *layout_find_by_image(ImageWindow *imd)
{
	GList *work;

	work = layout_window_list;
	while (work)
		{
		LayoutWindow *lw = work->data;
		work = work->next;

		if (lw->image == imd) return lw;
		}

	return NULL;
}

LayoutWindow *layout_find_by_image_fd(ImageWindow *imd)
{
	GList *work;

	work = layout_window_list;
	while (work)
		{
		LayoutWindow *lw = work->data;
		work = work->next;
		if (lw->image->image_fd == imd->image_fd)
			return lw;

		}

	return NULL;
}

/*
 *-----------------------------------------------------------------------------
 * menu, toolbar, and dir view
 *-----------------------------------------------------------------------------
 */

static void layout_path_entry_changed_cb(GtkWidget *widget, gpointer data)
{
	LayoutWindow *lw = data;
	gchar *buf;

	if (gtk_combo_box_get_active(GTK_COMBO_BOX(widget)) < 0) return;

	buf = g_strdup(gtk_entry_get_text(GTK_ENTRY(lw->path_entry)));
	if (!buf || (lw->dir_fd && strcmp(buf, lw->dir_fd->path) == 0))
		{
		g_free(buf);
		return;
		}

	layout_set_path(lw, buf);

	g_free(buf);
}

static void layout_path_entry_tab_cb(const gchar *path, gpointer data)
{
	LayoutWindow *lw = data;
	gchar *buf;
	gchar *base;

	buf = g_strdup(path);
	parse_out_relatives(buf);
	base = remove_level_from_path(buf);

	if (isdir(buf))
		{
		if ((!lw->dir_fd || strcmp(lw->dir_fd->path, buf) != 0) && layout_set_path(lw, buf))
			{
			gint pos = -1;
			/* put the G_DIR_SEPARATOR back, if we are in tab completion for a dir and result was path change */
			gtk_editable_insert_text(GTK_EDITABLE(lw->path_entry), G_DIR_SEPARATOR_S, -1, &pos);
			gtk_editable_set_position(GTK_EDITABLE(lw->path_entry),
						  strlen(gtk_entry_get_text(GTK_ENTRY(lw->path_entry))));
			}
		}
	else if (lw->dir_fd && strcmp(lw->dir_fd->path, base) == 0)
		{
		layout_list_scroll_to_subpart(lw, filename_from_path(buf));
		}

	g_free(base);
	g_free(buf);
}

static void layout_path_entry_cb(const gchar *path, gpointer data)
{
	LayoutWindow *lw = data;
	gchar *buf;

	buf = g_strdup(path);
	parse_out_relatives(buf);

	layout_set_path(lw, buf);

	g_free(buf);
}

static void layout_vd_select_cb(ViewDir *vd, const gchar *path, gpointer data)
{
	LayoutWindow *lw = data;

	layout_set_path(lw, path);
}

static GtkWidget *layout_tool_setup(LayoutWindow *lw)
{
	GtkWidget *box;
	GtkWidget *menu_bar;
	GtkWidget *tabcomp;

	box = gtk_vbox_new(FALSE, 0);

	menu_bar = layout_actions_menu_bar(lw);
	gtk_box_pack_start(GTK_BOX(box), menu_bar, FALSE, FALSE, 0);
	gtk_widget_show(menu_bar);

	lw->toolbar = layout_button_bar(lw);
	gtk_box_pack_start(GTK_BOX(box), lw->toolbar, FALSE, FALSE, 0);
	if (!lw->toolbar_hidden) gtk_widget_show(lw->toolbar);

	tabcomp = tab_completion_new_with_history(&lw->path_entry, NULL, "path_list", -1,
						  layout_path_entry_cb, lw);
	tab_completion_add_tab_func(lw->path_entry, layout_path_entry_tab_cb, lw);
	gtk_box_pack_start(GTK_BOX(box), tabcomp, FALSE, FALSE, 0);
	gtk_widget_show(tabcomp);

	g_signal_connect(G_OBJECT(lw->path_entry->parent), "changed",
			 G_CALLBACK(layout_path_entry_changed_cb), lw);

	lw->vd = vd_new(lw->dir_view_type, lw->dir_fd);
	vd_set_layout(lw->vd, lw);
	vd_set_select_func(lw->vd, layout_vd_select_cb, lw);

	lw->dir_view = lw->vd->widget;

	gtk_box_pack_start(GTK_BOX(box), lw->dir_view, TRUE, TRUE, 0);
	gtk_widget_show(lw->dir_view);

	gtk_widget_show(box);

	return box;
}

/*
 *-----------------------------------------------------------------------------
 * sort button (and menu)
 *-----------------------------------------------------------------------------
 */

static void layout_sort_menu_cb(GtkWidget *widget, gpointer data)
{
	LayoutWindow *lw;
	SortType type;

	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) return;

	lw = submenu_item_get_data(widget);
	if (!lw) return;

	type = (SortType)GPOINTER_TO_INT(data);

	layout_sort_set(lw, type, lw->sort_ascend);
}

static void layout_sort_menu_ascend_cb(GtkWidget *widget, gpointer data)
{
	LayoutWindow *lw = data;

	layout_sort_set(lw, lw->sort_method, !lw->sort_ascend);
}

static void layout_sort_menu_hide_cb(GtkWidget *widget, gpointer data)
{
	/* destroy the menu */
	gtk_widget_unref(GTK_WIDGET(widget));
}

static void layout_sort_button_press_cb(GtkWidget *widget, gpointer data)
{
	LayoutWindow *lw = data;
	GtkWidget *menu;
	GdkEvent *event;
	guint32 etime;

	menu = submenu_add_sort(NULL, G_CALLBACK(layout_sort_menu_cb), lw, FALSE, FALSE, TRUE, lw->sort_method);

	/* take ownership of menu */
#ifdef GTK_OBJECT_FLOATING
	/* GTK+ < 2.10 */
	g_object_ref(G_OBJECT(menu));
	gtk_object_sink(GTK_OBJECT(menu));
#else
	/* GTK+ >= 2.10 */
	g_object_ref_sink(G_OBJECT(menu));
#endif

	/* ascending option */
	menu_item_add_divider(menu);
	menu_item_add_check(menu, _("Ascending"), lw->sort_ascend, G_CALLBACK(layout_sort_menu_ascend_cb), lw);

	g_signal_connect(G_OBJECT(menu), "selection_done",
			 G_CALLBACK(layout_sort_menu_hide_cb), NULL);

	event = gtk_get_current_event();
	if (event)
		{
		etime = gdk_event_get_time(event);
		gdk_event_free(event);
		}
	else
		{
		etime = 0;
		}

	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 0, etime);
}

static GtkWidget *layout_sort_button(LayoutWindow *lw)
{
	GtkWidget *button;

	button = gtk_button_new_with_label(sort_type_get_text(lw->sort_method));
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(layout_sort_button_press_cb), lw);
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);

	return button;
}

/*
 *-----------------------------------------------------------------------------
 * color profile button (and menu)
 *-----------------------------------------------------------------------------
 */

static void layout_color_menu_enable_cb(GtkWidget *widget, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_color_profile_set_use(lw, (!layout_image_color_profile_get_use(lw)));
	layout_image_refresh(lw);
}

static void layout_color_menu_use_image_cb(GtkWidget *widget, gpointer data)
{
	LayoutWindow *lw = data;
	gint input, screen, use_image;

	if (!layout_image_color_profile_get(lw, &input, &screen, &use_image)) return;
	layout_image_color_profile_set(lw, input, screen, !use_image);
	layout_image_refresh(lw);
}

#define COLOR_MENU_KEY "color_menu_key"

static void layout_color_menu_input_cb(GtkWidget *widget, gpointer data)
{
	LayoutWindow *lw = data;
	gint type;
	gint input, screen, use_image;

	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) return;

	type = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), COLOR_MENU_KEY));
	if (type < 0 || type >= COLOR_PROFILE_FILE + COLOR_PROFILE_INPUTS) return;

	if (!layout_image_color_profile_get(lw, &input, &screen, &use_image)) return;
	if (type == input) return;

	layout_image_color_profile_set(lw, type, screen, use_image);
	layout_image_refresh(lw);
}

static void layout_color_menu_screen_cb(GtkWidget *widget, gpointer data)
{
	LayoutWindow *lw = data;
	gint type;
	gint input, screen, use_image;

	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) return;

	type = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), COLOR_MENU_KEY));
	if (type < 0 || type > 1) return;

	if (!layout_image_color_profile_get(lw, &input, &screen, &use_image)) return;
	if (type == screen) return;

	layout_image_color_profile_set(lw, input, type, use_image);
	layout_image_refresh(lw);
}

static gchar *layout_color_name_parse(const gchar *name)
{
	if (!name) return g_strdup(_("Empty"));
	return g_strdelimit(g_strdup(name), "_", '-');
}

static void layout_color_button_press_cb(GtkWidget *widget, gpointer data)
{
#ifndef HAVE_LCMS
	gchar *msg = g_strdup_printf(_("This installation of %s was not built with support for color profiles."), GQ_APPNAME);
	file_util_warning_dialog(_("Color profiles not supported"),
				 msg,
				 GTK_STOCK_DIALOG_INFO, widget);
	g_free(msg);
	return;
#else
	LayoutWindow *lw = data;
	GtkWidget *menu;
	GtkWidget *item;
	gchar *buf;
	gint active;
	gint input = 0;
	gint screen = 0;
	gint use_image = 0;
	gint from_image = 0;
	gint i;
	
	if (!layout_image_color_profile_get(lw, &input, &screen, &use_image)) return;

	from_image = use_image && (layout_image_color_profile_get_from_image(lw) != COLOR_PROFILE_NONE);
	menu = popup_menu_short_lived();

	active = layout_image_color_profile_get_use(lw);
	menu_item_add_check(menu, _("Use _color profiles"), active,
			    G_CALLBACK(layout_color_menu_enable_cb), lw);

	menu_item_add_divider(menu);

	item = menu_item_add_check(menu, _("Use profile from _image"), use_image,
			    G_CALLBACK(layout_color_menu_use_image_cb), lw);
	gtk_widget_set_sensitive(item, active);

	for (i = COLOR_PROFILE_SRGB; i < COLOR_PROFILE_FILE; i++)
		{
		const gchar *label;

		switch (i)
			{
			case COLOR_PROFILE_SRGB: 	label = _("sRGB"); break;
			case COLOR_PROFILE_ADOBERGB:	label = _("AdobeRGB compatible"); break;
			default:			label = "fixme"; break;
			}
		buf = g_strdup_printf(_("Input _%d: %s"), i, label);
	  	item = menu_item_add_radio(menu, (i == COLOR_PROFILE_SRGB) ? NULL : item,
				   buf, (input == i),
				   G_CALLBACK(layout_color_menu_input_cb), lw);
		g_free(buf);
		g_object_set_data(G_OBJECT(item), COLOR_MENU_KEY, GINT_TO_POINTER(i));
		gtk_widget_set_sensitive(item, active && !from_image);
		}

	for (i = 0; i < COLOR_PROFILE_INPUTS; i++)
		{
		const gchar *name;
		gchar *end;

		name = options->color_profile.input_name[i];
		if (!name) name = filename_from_path(options->color_profile.input_file[i]);

		end = layout_color_name_parse(name);
		buf = g_strdup_printf(_("Input _%d: %s"), i + COLOR_PROFILE_FILE, end);
		g_free(end);

		item = menu_item_add_radio(menu, item,
					   buf, (i + COLOR_PROFILE_FILE == input),
					   G_CALLBACK(layout_color_menu_input_cb), lw);
		g_free(buf);
		g_object_set_data(G_OBJECT(item), COLOR_MENU_KEY, GINT_TO_POINTER(i + COLOR_PROFILE_FILE));
		gtk_widget_set_sensitive(item, active && options->color_profile.input_file[i] && !from_image);
		}

	menu_item_add_divider(menu);

	item = menu_item_add_radio(menu, NULL,
				   _("Screen sRGB"), (screen == 0),
				   G_CALLBACK(layout_color_menu_screen_cb), lw);

	g_object_set_data(G_OBJECT(item), COLOR_MENU_KEY, GINT_TO_POINTER(0));
	gtk_widget_set_sensitive(item, active);

	item = menu_item_add_radio(menu, item,
				   _("_Screen profile"), (screen == 1),
				   G_CALLBACK(layout_color_menu_screen_cb), lw);
	g_object_set_data(G_OBJECT(item), COLOR_MENU_KEY, GINT_TO_POINTER(1));
	gtk_widget_set_sensitive(item, active && options->color_profile.screen_file);

	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 0, GDK_CURRENT_TIME);
#endif /* HAVE_LCMS */
}

static GtkWidget *layout_color_button(LayoutWindow *lw)
{
	GtkWidget *button;
	GtkWidget *image;
	gint enable;

	button = gtk_button_new();
	image = gtk_image_new_from_stock(GTK_STOCK_SELECT_COLOR, GTK_ICON_SIZE_MENU);
	gtk_container_add(GTK_CONTAINER(button), image);
	gtk_widget_show(image);
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(layout_color_button_press_cb), lw);
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);

#ifdef HAVE_LCMS
	enable = (lw->image) ? lw->image->color_profile_enable : FALSE;
#else
	enable = FALSE;
#endif
	gtk_widget_set_sensitive(image, enable);

	return button;
}


/*
 *-----------------------------------------------------------------------------
 * status bar
 *-----------------------------------------------------------------------------
 */

void layout_status_update_progress(LayoutWindow *lw, gdouble val, const gchar *text)
{
	if (!layout_valid(&lw)) return;
	if (!lw->info_progress_bar) return;

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(lw->info_progress_bar), val);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(lw->info_progress_bar), (text) ? text : " ");
}

void layout_status_update_info(LayoutWindow *lw, const gchar *text)
{
	gchar *buf = NULL;

	if (!layout_valid(&lw)) return;

	if (!text)
		{
		guint n;
		gint64 n_bytes = 0;
	
		n = layout_list_count(lw, &n_bytes);
		
		if (n)
			{
			guint s;
			gint64 s_bytes = 0;
			const gchar *ss;

			if (layout_image_slideshow_active(lw))
				{
				if (!layout_image_slideshow_paused(lw))
					{
					ss = _(" Slideshow");
					}
				else
					{
					ss = _(" Paused");
					}
				}
			else
				{
				ss = "";
				}
	
			s = layout_selection_count(lw, &s_bytes);
	
			layout_bars_new_selection(lw, s);
	
			if (s > 0)
				{
				gchar *b = text_from_size_abrev(n_bytes);
				gchar *sb = text_from_size_abrev(s_bytes);
				buf = g_strdup_printf(_("%s, %d files (%s, %d)%s"), b, n, sb, s, ss);
				g_free(b);
				g_free(sb);
				}
			else if (n > 0)
				{
				gchar *b = text_from_size_abrev(n_bytes);
				buf = g_strdup_printf(_("%s, %d files%s"), b, n, ss);
				g_free(b);
				}
			else
				{
				buf = g_strdup_printf(_("%d files%s"), n, ss);
				}
	
			text = buf;
	
			image_osd_update(lw->image);
			}
		else
			{
			text = "";
			}
	}
	
	if (lw->info_status) gtk_label_set_text(GTK_LABEL(lw->info_status), text);
	g_free(buf);
}

void layout_status_update_image(LayoutWindow *lw)
{
	guint64 n;

	if (!layout_valid(&lw) || !lw->image) return;

	n = layout_list_count(lw, NULL);
	
	if (!n)
		{
		gtk_label_set_text(GTK_LABEL(lw->info_zoom), "");
		gtk_label_set_text(GTK_LABEL(lw->info_details), "");
		}
	else
		{
		gchar *text;
		gchar *b;

		text = image_zoom_get_as_text(lw->image);
		gtk_label_set_text(GTK_LABEL(lw->info_zoom), text);
		g_free(text);

		b = image_get_fd(lw->image) ? text_from_size(image_get_fd(lw->image)->size) : g_strdup("0");

		if (lw->image->unknown)
			{
			if (image_get_path(lw->image) && !access_file(image_get_path(lw->image), R_OK))
				{
				text = g_strdup_printf(_("(no read permission) %s bytes"), b);
				}
			else
				{
				text = g_strdup_printf(_("( ? x ? ) %s bytes"), b);
				}
			}
		else
			{
			gint width, height;
	
			image_get_image_size(lw->image, &width, &height);
			text = g_strdup_printf(_("( %d x %d ) %s bytes"),
					       width, height, b);
			}

		g_free(b);
		
		gtk_label_set_text(GTK_LABEL(lw->info_details), text);
		g_free(text);
		}
}

void layout_status_update_all(LayoutWindow *lw)
{
	layout_status_update_progress(lw, 0.0, NULL);
	layout_status_update_info(lw, NULL);
	layout_status_update_image(lw);
}

static GtkWidget *layout_status_label(gchar *text, GtkWidget *box, gint start, gint size, gint expand)
{
	GtkWidget *label;
	GtkWidget *frame;

	frame = gtk_frame_new(NULL);
	if (size) gtk_widget_set_size_request(frame, size, -1);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	if (start)
		{
		gtk_box_pack_start(GTK_BOX(box), frame, expand, expand, 0);
		}
	else
		{
		gtk_box_pack_end(GTK_BOX(box), frame, expand, expand, 0);
		}
	gtk_widget_show(frame);

	label = gtk_label_new(text ? text : "");
	gtk_container_add(GTK_CONTAINER(frame), label);
	gtk_widget_show(label);

	return label;
}

static void layout_status_setup(LayoutWindow *lw, GtkWidget *box, gint small_format)
{
	GtkWidget *hbox;

	if (lw->info_box) return;

	if (small_format)
		{
		lw->info_box = gtk_vbox_new(FALSE, 0);
		}
	else
		{
		lw->info_box = gtk_hbox_new(FALSE, 0);
		}
	gtk_box_pack_end(GTK_BOX(box), lw->info_box, FALSE, FALSE, 0);
	gtk_widget_show(lw->info_box);

	if (small_format)
		{
		hbox = gtk_hbox_new(FALSE, 0);
		gtk_box_pack_start(GTK_BOX(lw->info_box), hbox, FALSE, FALSE, 0);
		gtk_widget_show(hbox);
		}
	else
		{
		hbox = lw->info_box;
		}
	lw->info_progress_bar = gtk_progress_bar_new();
	gtk_widget_set_size_request(lw->info_progress_bar, PROGRESS_WIDTH, -1);
	gtk_box_pack_start(GTK_BOX(hbox), lw->info_progress_bar, FALSE, FALSE, 0);
	gtk_widget_show(lw->info_progress_bar);

	lw->info_sort = layout_sort_button(lw);
	gtk_box_pack_start(GTK_BOX(hbox), lw->info_sort, FALSE, FALSE, 0);
	gtk_widget_show(lw->info_sort);

	lw->info_color = layout_color_button(lw);
	gtk_widget_show(lw->info_color);

	if (small_format) gtk_box_pack_end(GTK_BOX(hbox), lw->info_color, FALSE, FALSE, 0);

	lw->info_status = layout_status_label(NULL, lw->info_box, TRUE, 0, (!small_format));

	if (small_format)
		{
		hbox = gtk_hbox_new(FALSE, 0);
		gtk_box_pack_start(GTK_BOX(lw->info_box), hbox, FALSE, FALSE, 0);
		gtk_widget_show(hbox);
		}
	else
		{
		hbox = lw->info_box;
		}
	lw->info_details = layout_status_label(NULL, hbox, TRUE, 0, TRUE);
	if (!small_format) gtk_box_pack_start(GTK_BOX(hbox), lw->info_color, FALSE, FALSE, 0);
	lw->info_zoom = layout_status_label(NULL, hbox, FALSE, ZOOM_LABEL_WIDTH, FALSE);
}

/*
 *-----------------------------------------------------------------------------
 * views
 *-----------------------------------------------------------------------------
 */

static GtkWidget *layout_tools_new(LayoutWindow *lw)
{
	lw->dir_view = layout_tool_setup(lw);
	return lw->dir_view;
}

static void layout_list_status_cb(ViewFile *vf, gpointer data)
{
	LayoutWindow *lw = data;

	layout_status_update_info(lw, NULL);
}

static void layout_list_thumb_cb(ViewFile *vf, gdouble val, const gchar *text, gpointer data)
{
	LayoutWindow *lw = data;

	layout_status_update_progress(lw, val, text);
}

static GtkWidget *layout_list_new(LayoutWindow *lw)
{
	lw->vf = vf_new(lw->file_view_type, NULL);
	vf_set_layout(lw->vf, lw);

	vf_set_status_func(lw->vf, layout_list_status_cb, lw);
	vf_set_thumb_status_func(lw->vf, layout_list_thumb_cb, lw);

	vf_marks_set(lw->vf, lw->marks_enabled);

	switch (lw->file_view_type)
	{
	case FILEVIEW_ICON:
		break;
	case FILEVIEW_LIST:
		vf_thumb_set(lw->vf, lw->thumbs_enabled);
		break;
	}

	return lw->vf->widget;
}

static void layout_list_sync_thumb(LayoutWindow *lw)
{
	if (lw->vf) vf_thumb_set(lw->vf, lw->thumbs_enabled);
}

static void layout_list_sync_marks(LayoutWindow *lw)
{
	if (lw->vf) vf_marks_set(lw->vf, lw->marks_enabled);
}

static void layout_list_scroll_to_subpart(LayoutWindow *lw, const gchar *needle)
{
	if (!lw) return;
#if 0
	if (lw->vf) vf_scroll_to_subpart(lw->vf, needle);
#endif
}

GList *layout_list(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return NULL;

	if (lw->vf) return vf_get_list(lw->vf);

	return NULL;
}

guint layout_list_count(LayoutWindow *lw, gint64 *bytes)
{
	if (!layout_valid(&lw)) return 0;

	if (lw->vf) return vf_count(lw->vf, bytes);

	return 0;
}

FileData *layout_list_get_fd(LayoutWindow *lw, gint index)
{
	if (!layout_valid(&lw)) return NULL;

	if (lw->vf) return vf_index_get_data(lw->vf, index);

	return NULL;
}

gint layout_list_get_index(LayoutWindow *lw, FileData *fd)
{
	if (!layout_valid(&lw) || !fd) return -1;

	if (lw->vf) return vf_index_by_path(lw->vf, fd->path);

	return -1;
}

void layout_list_sync_fd(LayoutWindow *lw, FileData *fd)
{
	if (!layout_valid(&lw)) return;

	if (lw->vf) vf_select_by_fd(lw->vf, fd);
}

static void layout_list_sync_sort(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	if (lw->vf) vf_sort_set(lw->vf, lw->sort_method, lw->sort_ascend);
}

GList *layout_selection_list(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return NULL;

	if (layout_image_get_collection(lw, NULL))
		{
		FileData *fd;

		fd = layout_image_get_fd(lw);
		if (fd) return g_list_append(NULL, file_data_ref(fd));
		return NULL;
		}

	if (lw->vf) return vf_selection_get_list(lw->vf);

	return NULL;
}

GList *layout_selection_list_by_index(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return NULL;

	if (lw->vf) return vf_selection_get_list_by_index(lw->vf);

	return NULL;
}

guint layout_selection_count(LayoutWindow *lw, gint64 *bytes)
{
	if (!layout_valid(&lw)) return 0;

	if (lw->vf) return vf_selection_count(lw->vf, bytes);

	return 0;
}

void layout_select_all(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	if (lw->vf) vf_select_all(lw->vf);
}

void layout_select_none(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	if (lw->vf) vf_select_none(lw->vf);
}

void layout_select_invert(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	if (lw->vf) vf_select_invert(lw->vf);
}

void layout_mark_to_selection(LayoutWindow *lw, gint mark, MarkToSelectionMode mode)
{
	if (!layout_valid(&lw)) return;

	if (lw->vf) vf_mark_to_selection(lw->vf, mark, mode);
}

void layout_selection_to_mark(LayoutWindow *lw, gint mark, SelectionToMarkMode mode)
{
	if (!layout_valid(&lw)) return;

	if (lw->vf) vf_selection_to_mark(lw->vf, mark, mode);

	layout_status_update_info(lw, NULL); /* osd in fullscreen mode */
}

/*
 *-----------------------------------------------------------------------------
 * access
 *-----------------------------------------------------------------------------
 */

const gchar *layout_get_path(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return NULL;
	return lw->dir_fd ? lw->dir_fd->path : NULL;
}

static void layout_sync_path(LayoutWindow *lw)
{
	if (!lw->dir_fd) return;

	if (lw->path_entry) gtk_entry_set_text(GTK_ENTRY(lw->path_entry), lw->dir_fd->path);
	if (lw->vd) vd_set_fd(lw->vd, lw->dir_fd);

	if (lw->vf) vf_set_fd(lw->vf, lw->dir_fd);
}

gint layout_set_path(LayoutWindow *lw, const gchar *path)
{
	FileData *fd;
	if (!path) return FALSE;
	fd = file_data_new_simple(path);
	gint ret = layout_set_fd(lw, fd);
	file_data_unref(fd);
	return ret;
}


gint layout_set_fd(LayoutWindow *lw, FileData *fd)
{
	gint have_file = FALSE;

	if (!layout_valid(&lw)) return FALSE;

	if (!fd || !isname(fd->path)) return FALSE;
	if (lw->dir_fd && fd == lw->dir_fd)
		{
		return TRUE;
		}

	if (isdir(fd->path))
		{
		if (lw->dir_fd)
			{
			file_data_unregister_real_time_monitor(lw->dir_fd);
			file_data_unref(lw->dir_fd);
			}
		lw->dir_fd = file_data_ref(fd);
		file_data_register_real_time_monitor(fd);
		}
	else
		{
		gchar *base;

		base = remove_level_from_path(fd->path);
		if (lw->dir_fd && strcmp(lw->dir_fd->path, base) == 0)
			{
			g_free(base);
			}
		else if (isdir(base))
			{
			if (lw->dir_fd)
				{
				file_data_unregister_real_time_monitor(lw->dir_fd);
				file_data_unref(lw->dir_fd);
				}
			lw->dir_fd = file_data_new_simple(base);
			file_data_register_real_time_monitor(lw->dir_fd);
			g_free(base);
			}
		else
			{
			g_free(base);
			return FALSE;
			}
		if (isfile(fd->path)) have_file = TRUE;
		}

	if (lw->path_entry) tab_completion_append_to_history(lw->path_entry, lw->dir_fd->path);
	layout_sync_path(lw);

	if (have_file)
		{
		gint row;

		row = layout_list_get_index(lw, fd);
		if (row >= 0)
			{
			layout_image_set_index(lw, row);
			}
		else
			{
			layout_image_set_fd(lw, fd);
			}
		}
	else if (!options->lazy_image_sync)
		{
		layout_image_set_index(lw, 0);
		}

	return TRUE;
}

static void layout_refresh_lists(LayoutWindow *lw)
{
	if (lw->vd) vd_refresh(lw->vd);

	if (lw->vf) vf_refresh(lw->vf);
}

void layout_refresh(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	DEBUG_1("layout refresh");

	layout_refresh_lists(lw);

	if (lw->image) layout_image_refresh(lw);
}

void layout_thumb_set(LayoutWindow *lw, gint enable)
{
	if (!layout_valid(&lw)) return;

	if (lw->thumbs_enabled == enable) return;

	lw->thumbs_enabled = enable;

	layout_util_sync_thumb(lw);
	layout_list_sync_thumb(lw);
}

void layout_marks_set(LayoutWindow *lw, gint enable)
{
	if (!layout_valid(&lw)) return;

	if (lw->marks_enabled == enable) return;

	lw->marks_enabled = enable;

//	layout_util_sync_marks(lw);
	layout_list_sync_marks(lw);
}

gint layout_thumb_get(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return FALSE;

	return lw->thumbs_enabled;
}

gint layout_marks_get(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return FALSE;

	return lw->marks_enabled;
}

void layout_sort_set(LayoutWindow *lw, SortType type, gint ascend)
{
	if (!layout_valid(&lw)) return;
	if (lw->sort_method == type && lw->sort_ascend == ascend) return;

	lw->sort_method = type;
	lw->sort_ascend = ascend;

	if (lw->info_sort) gtk_label_set_text(GTK_LABEL(GTK_BIN(lw->info_sort)->child),
					      sort_type_get_text(type));
	layout_list_sync_sort(lw);
}

gint layout_sort_get(LayoutWindow *lw, SortType *type, gint *ascend)
{
	if (!layout_valid(&lw)) return FALSE;

	if (type) *type = lw->sort_method;
	if (ascend) *ascend = lw->sort_ascend;

	return TRUE;
}

gint layout_geometry_get(LayoutWindow *lw, gint *x, gint *y, gint *w, gint *h)
{
	if (!layout_valid(&lw)) return FALSE;

	gdk_window_get_root_origin(lw->window->window, x, y);
	gdk_drawable_get_size(lw->window->window, w, h);

	return TRUE;
}

gint layout_geometry_get_dividers(LayoutWindow *lw, gint *h, gint *v)
{
	if (!layout_valid(&lw)) return FALSE;

	if (lw->h_pane && GTK_PANED(lw->h_pane)->child1->allocation.x >= 0)
		{
		*h = GTK_PANED(lw->h_pane)->child1->allocation.width;
		}
	else if (h != &lw->div_h)
		{
		*h = lw->div_h;
		}

	if (lw->v_pane && GTK_PANED(lw->v_pane)->child1->allocation.x >= 0)
		{
		*v = GTK_PANED(lw->v_pane)->child1->allocation.height;
		}
	else if (v != &lw->div_v)
		{
		*v = lw->div_v;
		}

	return TRUE;
}

void layout_views_set(LayoutWindow *lw, DirViewType dir_view_type, FileViewType file_view_type)
{
	if (!layout_valid(&lw)) return;

	if (lw->dir_view_type == dir_view_type && lw->file_view_type == file_view_type) return;

	lw->dir_view_type = dir_view_type;
	lw->file_view_type = file_view_type;

	layout_style_set(lw, -1, NULL);
}

gint layout_views_get(LayoutWindow *lw, DirViewType *dir_view_type, FileViewType *file_view_type)
{
	if (!layout_valid(&lw)) return FALSE;

	*dir_view_type = lw->dir_view_type;
	*file_view_type = lw->file_view_type;

	return TRUE;
}

/*
 *-----------------------------------------------------------------------------
 * location utils
 *-----------------------------------------------------------------------------
 */

static gint layout_location_single(LayoutLocation l)
{
	return (l == LAYOUT_LEFT ||
		l == LAYOUT_RIGHT ||
		l == LAYOUT_TOP ||
		l == LAYOUT_BOTTOM);
}

static gint layout_location_vertical(LayoutLocation l)
{
	return (l & LAYOUT_TOP ||
		l & LAYOUT_BOTTOM);
}

static gint layout_location_first(LayoutLocation l)
{
	return (l & LAYOUT_TOP ||
		l & LAYOUT_LEFT);
}

static LayoutLocation layout_grid_compass(LayoutWindow *lw)
{
	if (layout_location_single(lw->dir_location)) return lw->dir_location;
	if (layout_location_single(lw->file_location)) return lw->file_location;
	return lw->image_location;
}

static void layout_location_compute(LayoutLocation l1, LayoutLocation l2,
				    GtkWidget *s1, GtkWidget *s2,
				    GtkWidget **d1, GtkWidget **d2)
{
	LayoutLocation l;

	l = l1 & l2;	/* get common compass direction */
	l = l1 - l;	/* remove it */

	if (layout_location_first(l))
		{
		*d1 = s1;
		*d2 = s2;
		}
	else
		{
		*d1 = s2;
		*d2 = s1;
		}
}

/*
 *-----------------------------------------------------------------------------
 * tools window (for floating/hidden)
 *-----------------------------------------------------------------------------
 */

gint layout_geometry_get_tools(LayoutWindow *lw, gint *x, gint *y, gint *w, gint *h, gint *divider_pos)
{
	if (!layout_valid(&lw)) return FALSE;

	if (!lw->tools || !GTK_WIDGET_VISIBLE(lw->tools))
		{
		/* use the stored values (sort of breaks success return value) */

		*divider_pos = lw->div_float;

		return FALSE;
		}

	gdk_window_get_root_origin(lw->tools->window, x, y);
	gdk_drawable_get_size(lw->tools->window, w, h);

	if (GTK_IS_VPANED(lw->tools_pane))
		{
		*divider_pos = GTK_PANED(lw->tools_pane)->child1->allocation.height;
		}
	else
		{
		*divider_pos = GTK_PANED(lw->tools_pane)->child1->allocation.width;
		}

	return TRUE;
}

static void layout_tools_geometry_sync(LayoutWindow *lw)
{
	layout_geometry_get_tools(lw, &options->layout.float_window.x, &options->layout.float_window.x,
				  &options->layout.float_window.w, &options->layout.float_window.h, &lw->div_float);
}

static void layout_tools_hide(LayoutWindow *lw, gint hide)
{
	if (!lw->tools) return;

	if (hide)
		{
		if (GTK_WIDGET_VISIBLE(lw->tools))
			{
			layout_tools_geometry_sync(lw);
			gtk_widget_hide(lw->tools);
			}
		}
	else
		{
		if (!GTK_WIDGET_VISIBLE(lw->tools))
			{
			gtk_widget_show(lw->tools);
			if (lw->vf) vf_refresh(lw->vf);
			}
		}

	lw->tools_hidden = hide;
}

static gint layout_tools_delete_cb(GtkWidget *widget, GdkEventAny *event, gpointer data)
{
	LayoutWindow *lw = data;

	layout_tools_float_toggle(lw);

	return TRUE;
}

static void layout_tools_setup(LayoutWindow *lw, GtkWidget *tools, GtkWidget *files)
{
	GtkWidget *vbox;
	GtkWidget *w1, *w2;
	gint vertical;
	gint new_window = FALSE;

	vertical = (layout_location_single(lw->image_location) && !layout_location_vertical(lw->image_location)) ||
		   (!layout_location_single(lw->image_location) && layout_location_vertical(layout_grid_compass(lw)));
#if 0
	layout_location_compute(lw->dir_location, lw->file_location,
				tools, files, &w1, &w2);
#endif
	/* for now, tools/dir are always first in order */
	w1 = tools;
	w2 = files;

	if (!lw->tools)
		{
		GdkGeometry geometry;
		GdkWindowHints hints;

		lw->tools = window_new(GTK_WINDOW_TOPLEVEL, "tools", PIXBUF_INLINE_ICON_TOOLS, NULL, _("Tools"));
		g_signal_connect(G_OBJECT(lw->tools), "delete_event",
				 G_CALLBACK(layout_tools_delete_cb), lw);
		layout_keyboard_init(lw, lw->tools);

		if (options->layout.save_window_positions)
			{
			hints = GDK_HINT_USER_POS;
			}
		else
			{
			hints = 0;
			}

		geometry.min_width = 32;
		geometry.min_height = 32;
		geometry.base_width = TOOLWINDOW_DEF_WIDTH;
		geometry.base_height = TOOLWINDOW_DEF_HEIGHT;
		gtk_window_set_geometry_hints(GTK_WINDOW(lw->tools), NULL, &geometry,
					      GDK_HINT_MIN_SIZE | GDK_HINT_BASE_SIZE | hints);


		gtk_window_set_resizable(GTK_WINDOW(lw->tools), TRUE);
		gtk_container_set_border_width(GTK_CONTAINER(lw->tools), 0);

		new_window = TRUE;
		}
	else
		{
		layout_tools_geometry_sync(lw);
		/* dump the contents */
		gtk_widget_destroy(GTK_BIN(lw->tools)->child);
		}

	layout_actions_add_window(lw, lw->tools);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(lw->tools), vbox);
	gtk_widget_show(vbox);

	layout_status_setup(lw, vbox, TRUE);

	if (vertical)
		{
		lw->tools_pane = gtk_vpaned_new();
		}
	else
		{
		lw->tools_pane = gtk_hpaned_new();
		}
	gtk_box_pack_start(GTK_BOX(vbox), lw->tools_pane, TRUE, TRUE, 0);
	gtk_widget_show(lw->tools_pane);

	gtk_paned_pack1(GTK_PANED(lw->tools_pane), w1, FALSE, TRUE);
	gtk_paned_pack2(GTK_PANED(lw->tools_pane), w2, TRUE, TRUE);

	gtk_widget_show(tools);
	gtk_widget_show(files);

	if (new_window)
		{
		if (options->layout.save_window_positions)
			{
			gtk_window_set_default_size(GTK_WINDOW(lw->tools), options->layout.float_window.w, options->layout.float_window.h);
			gtk_window_move(GTK_WINDOW(lw->tools), options->layout.float_window.x, options->layout.float_window.y);
			}
		else
			{
			if (vertical)
				{
				gtk_window_set_default_size(GTK_WINDOW(lw->tools),
							    TOOLWINDOW_DEF_WIDTH, TOOLWINDOW_DEF_HEIGHT);
				}
			else
				{
				gtk_window_set_default_size(GTK_WINDOW(lw->tools),
							    TOOLWINDOW_DEF_HEIGHT, TOOLWINDOW_DEF_WIDTH);
				}
			}
		}

	if (!options->layout.save_window_positions)
		{
		if (vertical)
			{
			lw->div_float = MAIN_WINDOW_DIV_VPOS;
			}
		else
			{
			lw->div_float = MAIN_WINDOW_DIV_HPOS;
			}
		}

	gtk_paned_set_position(GTK_PANED(lw->tools_pane), lw->div_float);
}

/*
 *-----------------------------------------------------------------------------
 * glue (layout arrangement)
 *-----------------------------------------------------------------------------
 */

static void layout_grid_compute(LayoutWindow *lw,
				GtkWidget *image, GtkWidget *tools, GtkWidget *files,
				GtkWidget **w1, GtkWidget **w2, GtkWidget **w3)
{
	/* heh, this was fun */

	if (layout_location_single(lw->dir_location))
		{
		if (layout_location_first(lw->dir_location))
			{
			*w1 = tools;
			layout_location_compute(lw->file_location, lw->image_location, files, image, w2, w3);
			}
		else
			{
			*w3 = tools;
			layout_location_compute(lw->file_location, lw->image_location, files, image, w1, w2);
			}
		}
	else if (layout_location_single(lw->file_location))
		{
		if (layout_location_first(lw->file_location))
			{
			*w1 = files;
			layout_location_compute(lw->dir_location, lw->image_location, tools, image, w2, w3);
			}
		else
			{
			*w3 = files;
			layout_location_compute(lw->dir_location, lw->image_location, tools, image, w1, w2);
			}
		}
	else
		{
		/* image */
		if (layout_location_first(lw->image_location))
			{
			*w1 = image;
			layout_location_compute(lw->file_location, lw->dir_location, files, tools, w2, w3);
			}
		else
			{
			*w3 = image;
			layout_location_compute(lw->file_location, lw->dir_location, files, tools, w1, w2);
			}
		}
}

void layout_split_change(LayoutWindow *lw, ImageSplitMode mode)
{
	GtkWidget *image;
	gint i;

	for (i = 0; i < MAX_SPLIT_IMAGES; i++)
		{
		if (lw->split_images[i])
			{
			gtk_widget_hide(lw->split_images[i]->widget);
			if (lw->split_images[i]->widget->parent != lw->utility_box)
				gtk_container_remove(GTK_CONTAINER(lw->split_images[i]->widget->parent), lw->split_images[i]->widget);
			}
		}
	gtk_container_remove(GTK_CONTAINER(lw->utility_box), lw->split_image_widget);

	image = layout_image_setup_split(lw, mode);

	gtk_box_pack_start(GTK_BOX(lw->utility_box), image, TRUE, TRUE, 0);
	gtk_box_reorder_child(GTK_BOX(lw->utility_box), image, 0);
	gtk_widget_show(image);
}

static void layout_grid_setup(LayoutWindow *lw)
{
	gint priority_location;
	GtkWidget *h;
	GtkWidget *v;
	GtkWidget *w1, *w2, *w3;

	GtkWidget *image;
	GtkWidget *tools;
	GtkWidget *files;

	layout_actions_setup(lw);
	layout_actions_add_window(lw, lw->window);

	lw->group_box = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(lw->main_box), lw->group_box, TRUE, TRUE, 0);
	gtk_widget_show(lw->group_box);

	priority_location = layout_grid_compass(lw);

	image = layout_image_setup_split(lw, lw->split_mode);

	tools = layout_tools_new(lw);
	files = layout_list_new(lw);

	image = layout_bars_prepare(lw, image);

	if (lw->tools_float || lw->tools_hidden)
		{
		gtk_box_pack_start(GTK_BOX(lw->group_box), image, TRUE, TRUE, 0);
		gtk_widget_show(image);

		layout_tools_setup(lw, tools, files);

		gtk_widget_grab_focus(lw->image->widget);

		return;
		}
	else if (lw->tools)
		{
		layout_tools_geometry_sync(lw);
		gtk_widget_destroy(lw->tools);
		lw->tools = NULL;
		lw->tools_pane = NULL;
		}

	layout_status_setup(lw, lw->group_box, FALSE);

	layout_grid_compute(lw, image, tools, files, &w1, &w2, &w3);

	v = lw->v_pane = gtk_vpaned_new();

	h = lw->h_pane = gtk_hpaned_new();

	if (!layout_location_vertical(priority_location))
		{
		GtkWidget *tmp;

		tmp = v;
		v = h;
		h = tmp;
		}

	gtk_box_pack_start(GTK_BOX(lw->group_box), v, TRUE, TRUE, 0);

	if (!layout_location_first(priority_location))
		{
		gtk_paned_pack1(GTK_PANED(v), h, FALSE, TRUE);
		gtk_paned_pack2(GTK_PANED(v), w3, TRUE, TRUE);

		gtk_paned_pack1(GTK_PANED(h), w1, FALSE, TRUE);
		gtk_paned_pack2(GTK_PANED(h), w2, TRUE, TRUE);
		}
	else
		{
		gtk_paned_pack1(GTK_PANED(v), w1, FALSE, TRUE);
		gtk_paned_pack2(GTK_PANED(v), h, TRUE, TRUE);

		gtk_paned_pack1(GTK_PANED(h), w2, FALSE, TRUE);
		gtk_paned_pack2(GTK_PANED(h), w3, TRUE, TRUE);
		}

	gtk_widget_show(image);
	gtk_widget_show(tools);
	gtk_widget_show(files);

	gtk_widget_show(v);
	gtk_widget_show(h);

	/* fix to have image pane visible when it is left and priority widget */
	if (lw->div_h == -1 &&
	    w1 == image &&
	    !layout_location_vertical(priority_location) &&
	    layout_location_first(priority_location))
		{
		gtk_widget_set_size_request(image, 200, -1);
		}

	gtk_paned_set_position(GTK_PANED(lw->h_pane), lw->div_h);
	gtk_paned_set_position(GTK_PANED(lw->v_pane), lw->div_v);

	gtk_widget_grab_focus(lw->image->widget);
}

void layout_style_set(LayoutWindow *lw, gint style, const gchar *order)
{
	FileData *dir_fd;
	gint i;

	if (!layout_valid(&lw)) return;

	if (style != -1)
		{
		LayoutLocation d, f, i;

		layout_config_parse(style, order, &d,  &f, &i);

		if (lw->dir_location == d &&
		    lw->file_location == f &&
		    lw->image_location == i) return;

		lw->dir_location = d;
		lw->file_location = f;
		lw->image_location = i;
		}

	/* remember state */

	layout_image_slideshow_stop(lw);
	layout_image_full_screen_stop(lw);

	dir_fd = lw->dir_fd;
	if (dir_fd) file_data_unregister_real_time_monitor(dir_fd);
	lw->dir_fd = NULL;
	lw->image = NULL;
	lw->utility_box = NULL;

	layout_geometry_get_dividers(lw, &lw->div_h, &lw->div_v);

	/* clear it all */

	for (i = 0; i < MAX_SPLIT_IMAGES; i++)
		{
		if (lw->split_images[i])
			{
			gtk_widget_hide(lw->split_images[i]->widget);
			gtk_container_remove(GTK_CONTAINER(lw->split_images[i]->widget->parent), lw->split_images[i]->widget);
			}
		}

	lw->h_pane = NULL;
	lw->v_pane = NULL;

	lw->toolbar = NULL;
	lw->thumb_button = NULL;
	lw->path_entry = NULL;
	lw->dir_view = NULL;
	lw->vd = NULL;

	lw->file_view = NULL;
	lw->vf = NULL;
	lw->vf = NULL;

	lw->info_box = NULL;
	lw->info_progress_bar = NULL;
	lw->info_sort = NULL;
	lw->info_color = NULL;
	lw->info_status = NULL;
	lw->info_details = NULL;
	lw->info_zoom = NULL;

	if (lw->ui_manager) g_object_unref(lw->ui_manager);
	lw->ui_manager = NULL;
	lw->action_group = NULL;

	gtk_container_remove(GTK_CONTAINER(lw->main_box), lw->group_box);
	lw->group_box = NULL;

	/* re-fill */

	layout_grid_setup(lw);
	layout_tools_hide(lw, lw->tools_hidden);

	layout_list_sync_sort(lw);
	layout_util_sync(lw);
	layout_status_update_all(lw);

	/* sync */

	if (image_get_fd(lw->image))
		{
		layout_set_fd(lw, image_get_fd(lw->image));
		}
	else
		{
		layout_set_fd(lw, dir_fd);
		}
	image_top_window_set_sync(lw->image, (lw->tools_float || lw->tools_hidden));

	/* clean up */

	file_data_unref(dir_fd);
}

void layout_styles_update(void)
{
	GList *work;

	work = layout_window_list;
	while (work)
		{
		LayoutWindow *lw = work->data;
		work = work->next;

		layout_style_set(lw, options->layout.style, options->layout.order);
		}
}

void layout_colors_update(void)
{
	GList *work;

	work = layout_window_list;
	while (work)
		{
		LayoutWindow *lw = work->data;
		work = work->next;

		if (!lw->image) continue;
		image_background_set_color(lw->image, options->image.use_custom_border_color ? &options->image.border_color : NULL);
		}
}

void layout_tools_float_toggle(LayoutWindow *lw)
{
	gint popped;

	if (!lw) return;

	if (!lw->tools_hidden)
		{
		popped = !lw->tools_float;
		}
	else
		{
		popped = TRUE;
		}

	if (lw->tools_float == popped)
		{
		if (popped && lw->tools_hidden)
			{
			layout_tools_float_set(lw, popped, FALSE);
			}
		}
	else
		{
		if (lw->tools_float)
			{
			layout_tools_float_set(lw, FALSE, FALSE);
			}
		else
			{
			layout_tools_float_set(lw, TRUE, FALSE);
			}
		}
}

void layout_tools_hide_toggle(LayoutWindow *lw)
{
	if (!lw) return;

	layout_tools_float_set(lw, lw->tools_float, !lw->tools_hidden);
}

void layout_tools_float_set(LayoutWindow *lw, gint popped, gint hidden)
{
	if (!layout_valid(&lw)) return;

	if (lw->tools_float == popped && lw->tools_hidden == hidden) return;

	if (lw->tools_float == popped && lw->tools_float && lw->tools)
		{
		layout_tools_hide(lw, hidden);
		return;
		}

	lw->tools_float = popped;
	lw->tools_hidden = hidden;

	layout_style_set(lw, -1, NULL);
}

gint layout_tools_float_get(LayoutWindow *lw, gint *popped, gint *hidden)
{
	if (!layout_valid(&lw)) return FALSE;

	*popped = lw->tools_float;
	*hidden = lw->tools_hidden;

	return TRUE;
}

void layout_toolbar_toggle(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;
	if (!lw->toolbar) return;

	lw->toolbar_hidden = !lw->toolbar_hidden;

	if (lw->toolbar_hidden)
		{
		if (GTK_WIDGET_VISIBLE(lw->toolbar)) gtk_widget_hide(lw->toolbar);
		}
	else
		{
		if (!GTK_WIDGET_VISIBLE(lw->toolbar)) gtk_widget_show(lw->toolbar);
		}
}

gint layout_toolbar_hidden(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return TRUE;

	return lw->toolbar_hidden;
}

/*
 *-----------------------------------------------------------------------------
 * base
 *-----------------------------------------------------------------------------
 */

void layout_close(LayoutWindow *lw)
{
	if (layout_window_list && layout_window_list->next)
		{
		layout_free(lw);
		}
	else
		{
		exit_program();
		}
}

void layout_free(LayoutWindow *lw)
{
	if (!lw) return;

	layout_window_list = g_list_remove(layout_window_list, lw);

	
	layout_bars_close(lw);

	gtk_widget_destroy(lw->window);
	
	if (lw->split_image_sizegroup) g_object_unref(lw->split_image_sizegroup);

	file_data_unregister_notify_func(layout_image_notify_cb, lw);

	if (lw->dir_fd) 
		{
		file_data_unregister_real_time_monitor(lw->dir_fd);
		file_data_unref(lw->dir_fd);
		}

	g_free(lw);
}

static gint layout_delete_cb(GtkWidget *widget, GdkEventAny *event, gpointer data)
{
	LayoutWindow *lw = data;

	layout_close(lw);
	return TRUE;
}

LayoutWindow *layout_new(FileData *dir_fd, gint popped, gint hidden)
{
	return layout_new_with_geometry(dir_fd, popped, hidden, NULL);
}

LayoutWindow *layout_new_with_geometry(FileData *dir_fd, gint popped, gint hidden,
				       const gchar *geometry)
{
	LayoutWindow *lw;
	GdkGeometry hint;
	GdkWindowHints hint_mask;

	lw = g_new0(LayoutWindow, 1);

	lw->thumbs_enabled = options->layout.show_thumbnails;
	lw->marks_enabled = options->layout.show_marks;
	lw->sort_method = SORT_NAME;
	lw->sort_ascend = TRUE;

	lw->tools_float = popped;
	lw->tools_hidden = hidden;

	lw->toolbar_hidden = options->layout.toolbar_hidden;

	lw->utility_box = NULL;
	lw->bar_sort = NULL;
	lw->bar_sort_enabled = options->panels.sort.enabled;

	lw->bar_exif = NULL;
	lw->bar_exif_enabled = options->panels.exif.enabled;
	lw->bar_exif_advanced = FALSE;
	
	lw->bar_info = NULL;
	lw->bar_info_enabled = options->panels.info.enabled;

	/* default layout */

	layout_config_parse(options->layout.style, options->layout.order,
			    &lw->dir_location,  &lw->file_location, &lw->image_location);
	lw->dir_view_type = CLAMP(options->layout.dir_view_type, 0, VIEW_DIR_TYPES_COUNT - 1);
	lw->file_view_type = CLAMP(options->layout.file_view_type, 0, VIEW_FILE_TYPES_COUNT - 1);

	/* divider positions */

	if (options->layout.save_window_positions)
		{
		lw->div_h = options->layout.main_window.hdivider_pos;
		lw->div_v = options->layout.main_window.vdivider_pos;
		lw->div_float = options->layout.float_window.vdivider_pos;
		lw->bar_exif_width = options->panels.exif.width;
		lw->bar_info_width = options->panels.info.width;
		}
	else
		{
		lw->div_h = MAIN_WINDOW_DIV_HPOS;
		lw->div_v = MAIN_WINDOW_DIV_VPOS;
		lw->div_float = MAIN_WINDOW_DIV_VPOS;
		lw->bar_exif_width = PANEL_DEFAULT_WIDTH;
		lw->bar_info_width = PANEL_DEFAULT_WIDTH;
		}

	/* window */

	lw->window = window_new(GTK_WINDOW_TOPLEVEL, GQ_WMCLASS, NULL, NULL, NULL);
	gtk_window_set_resizable(GTK_WINDOW(lw->window), TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(lw->window), 0);

	if (options->layout.save_window_positions)
		{
		hint_mask = GDK_HINT_USER_POS;
		}
	else
		{
		hint_mask = 0;
		}

	hint.min_width = 32;
	hint.min_height = 32;
	hint.base_width = 0;
	hint.base_height = 0;
	gtk_window_set_geometry_hints(GTK_WINDOW(lw->window), NULL, &hint,
				      GDK_HINT_MIN_SIZE | GDK_HINT_BASE_SIZE | hint_mask);

	if (options->layout.save_window_positions)
		{
		gtk_window_set_default_size(GTK_WINDOW(lw->window), options->layout.main_window.w, options->layout.main_window.h);
		if (!layout_window_list)
			{
			gtk_window_move(GTK_WINDOW(lw->window), options->layout.main_window.x, options->layout.main_window.y);
			if (options->layout.main_window.maximized) gtk_window_maximize(GTK_WINDOW(lw->window));
			}
		}
	else
		{
		gtk_window_set_default_size(GTK_WINDOW(lw->window), MAINWINDOW_DEF_WIDTH, MAINWINDOW_DEF_HEIGHT);
		}

	g_signal_connect(G_OBJECT(lw->window), "delete_event",
			 G_CALLBACK(layout_delete_cb), lw);

	layout_keyboard_init(lw, lw->window);

#ifdef HAVE_LIRC
	layout_image_lirc_init(lw);
#endif

	lw->main_box = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(lw->window), lw->main_box);
	gtk_widget_show(lw->main_box);

	layout_grid_setup(lw);
	image_top_window_set_sync(lw->image, (lw->tools_float || lw->tools_hidden));

	layout_util_sync(lw);
	layout_status_update_all(lw);

	if (dir_fd)
		{
		layout_set_fd(lw, dir_fd);
		}
	else
		{
		GdkPixbuf *pixbuf;

		pixbuf = pixbuf_inline(PIXBUF_INLINE_LOGO);
		image_change_pixbuf(lw->image, pixbuf, 1.0);
		gdk_pixbuf_unref(pixbuf);
		}

	if (geometry)
		{
		if (!gtk_window_parse_geometry(GTK_WINDOW(lw->window), geometry))
			{
			log_printf("%s", _("Invalid geometry\n"));
			}
		}

	gtk_widget_show(lw->window);
	layout_tools_hide(lw, lw->tools_hidden);

	layout_window_list = g_list_append(layout_window_list, lw);

	file_data_register_notify_func(layout_image_notify_cb, lw, NOTIFY_PRIORITY_LOW);

	return lw;
}

