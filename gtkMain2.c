/*
 * Copyright © 2018 Benjamin Otte
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Benjamin Otte <otte@gnome.org>
 */

#include "config.h"

#include "gtklistitemmanagerprivate.h"

#include "gtklistitembaseprivate.h"
#include "gtklistitemwidgetprivate.h"
#include "gtksectionmodel.h"
#include "gtkwidgetprivate.h"

typedef struct _GtkListItemChange GtkListItemChange;

struct _GtkListItemManager
{
  GObject parent_instance;

  GtkWidget *widget;
  GtkSelectionModel *model;
  gboolean has_sections;

  GtkRbTree *items;
  GSList *trackers;

  GtkListTile * (* split_func) (GtkWidget *, GtkListTile *, guint);
  GtkListItemBase * (* create_widget) (GtkWidget *);
  void (* prepare_section) (GtkWidget *, GtkListTile *, guint);
  GtkListHeaderBase * (* create_header_widget) (GtkWidget *);
};

struct _GtkListItemManagerClass
{
  GObjectClass parent_class;
};

struct _GtkListItemTracker
{
  guint position;
  GtkListItemBase *widget;
  guint n_before;
  guint n_after;
};

struct _GtkListItemChange
{
  GHashTable *deleted_items;
  GQueue recycled_items;
  GQueue recycled_headers;
};

G_DEFINE_TYPE (GtkListItemManager, gtk_list_item_manager, G_TYPE_OBJECT)

static void
gtk_list_item_change_init (GtkListItemChange *change)
{
  change->deleted_items = NULL;
  g_queue_init (&change->recycled_items);
  g_queue_init (&change->recycled_headers);
}

static void
gtk_list_item_change_finish (GtkListItemChange *change)
{
  GtkWidget *widget;

  g_clear_pointer (&change->deleted_items, g_hash_table_destroy);

  while ((widget = g_queue_pop_head (&change->recycled_items)))
    gtk_widget_unparent (widget);
  while ((widget = g_queue_pop_head (&change->recycled_headers)))
    gtk_widget_unparent (widget);
}

static void
gtk_list_item_change_recycle (GtkListItemChange *change,
                              GtkListItemBase   *widget)
{
  g_queue_push_tail (&change->recycled_items, widget);
}

static void
gtk_list_item_change_clear_header (GtkListItemChange  *change,
                                   GtkWidget         **widget)
{
  if (*widget == NULL)
    return;

  g_assert (GTK_IS_LIST_HEADER_BASE (*widget));
  g_queue_push_tail (&change->recycled_headers, *widget);
  *widget = NULL;
}

static void
gtk_list_item_change_release (GtkListItemChange *change,
                              GtkListItemBase   *widget)
{
  if (change->deleted_items == NULL)
    change->deleted_items = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) gtk_widget_unparent);

  if (!g_hash_table_replace (change->deleted_items, gtk_list_item_base_get_item (GTK_LIST_ITEM_BASE (widget)), widget))
    {
      g_warning ("Duplicate item detected in list. Picking one randomly.");
    }
}

static GtkListItemBase *
gtk_list_item_change_find (GtkListItemChange *change,
                           gpointer           item)
{
  gpointer result;

  if (change->deleted_items && g_hash_table_steal_extended (change->deleted_items, item, NULL, &result))
    return result;

  return NULL;
}

static GtkListItemBase *
gtk_list_item_change_get (GtkListItemChange *change,
                          gpointer           item)
{
  GtkListItemBase *result;

  result = gtk_list_item_change_find (change, item);
  if (result)
    return result;

  result = g_queue_pop_head (&change->recycled_items);
  if (result)
    return result;

  return NULL;
}

static GtkListHeaderBase *
gtk_list_item_change_get_header (GtkListItemChange *change)
{
  return g_queue_pop_head (&change->recycled_headers);
}

static void
potentially_empty_rectangle_union (cairo_rectangle_int_t       *self,
                                   const cairo_rectangle_int_t *area)
{
  if (area->width <= 0 || area->height <= 0)
    return;

  if (self->width <= 0 || self->height <= 0)
    {
      *self = *area;
      return;
    }

  gdk_rectangle_union (self, area, self);
}

static void
gtk_list_item_manager_augment_node (GtkRbTree *tree,
                                    gpointer   node_augment,
                                    gpointer   node,
                                    gpointer   left,
                                    gpointer   right)
{
  GtkListTile *tile = node;
  GtkListTileAugment *aug = node_augment;

  aug->n_items = tile->n_items;
  aug->area = tile->area;

  switch (tile->type)
  {
    case GTK_LIST_TILE_HEADER:
    case GTK_LIST_TILE_UNMATCHED_HEADER:
      aug->has_header = TRUE;
      aug->has_footer = FALSE;
      break;
    case GTK_LIST_TILE_FOOTER:
    case GTK_LIST_TILE_UNMATCHED_FOOTER:
      aug->has_header = FALSE;
      aug->has_footer = TRUE;
      break;
    case GTK_LIST_TILE_ITEM:
    case GTK_LIST_TILE_REMOVED:
      aug->has_header = FALSE;
      aug->has_footer = FALSE;
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  if (left)
    {
      GtkListTileAugment *left_aug = gtk_rb_tree_get_augment (tree, left);

      aug->n_items += left_aug->n_items;
      aug->has_header |= left_aug->has_header;
      aug->has_footer |= left_aug->has_footer;
      potentially_empty_rectangle_union (&aug->area, &left_aug->area);
    }

  if (right)
    {
      GtkListTileAugment *right_aug = gtk_rb_tree_get_augment (tree, right);

      aug->n_items += right_aug->n_items;
      aug->has_header |= right_aug->has_header;
      aug->has_footer |= right_aug->has_footer;
      potentially_empty_rectangle_union (&aug->area, &right_aug->area);
    }
}

static void
gtk_list_item_manager_clear_node (gpointer _tile)
{
  GtkListTile *tile G_GNUC_UNUSED = _tile;

  g_assert (tile->widget == NULL);
}

GtkListItemManager *
gtk_list_item_manager_new (GtkWidget          *widget,
                           GtkListTile *       (* split_func) (GtkWidget *, GtkListTile *, guint),
                           GtkListItemBase *   (* create_widget) (GtkWidget *),
                           void                (* prepare_section) (GtkWidget *, GtkListTile *, guint),
                           GtkListHeaderBase * (* create_header_widget) (GtkWidget *))
{
  GtkListItemManager *self;

  g_return_val_if_fail (GTK_IS_WIDGET (widget), NULL);

  self = g_object_new (GTK_TYPE_LIST_ITEM_MANAGER, NULL);

  /* not taking a ref because the widget refs us */
  self->widget = widget;
  self->split_func = split_func;
  self->create_widget = create_widget;
  self->prepare_section = prepare_section;
  self->create_header_widget = create_header_widget;

  self->items = gtk_rb_tree_new_for_size (sizeof (GtkListTile),
                                          sizeof (GtkListTileAugment),
                                          gtk_list_item_manager_augment_node,
                                          gtk_list_item_manager_clear_node,
                                          NULL);

  return self;
}

static gboolean
gtk_list_item_manager_has_sections (GtkListItemManager *self)
{
  if (self->model == NULL || !self->has_sections)
    return FALSE;

  return GTK_IS_SECTION_MODEL (self->model);
}

void
gtk_list_item_manager_get_tile_bounds (GtkListItemManager *self,
                                       GdkRectangle       *out_bounds)
{
  GtkListTile *tile;
  GtkListTileAugment *aug;

  tile = gtk_rb_tree_get_root (self->items);
  if (tile == NULL)
    {
      *out_bounds = (GdkRectangle) { 0, 0, 0, 0 };
      return;
    }

  aug = gtk_rb_tree_get_augment (self->items, tile);
  *out_bounds = aug->area;
}

gpointer
gtk_list_item_manager_get_first (GtkListItemManager *self)
{
  return gtk_rb_tree_get_first (self->items);
}

gpointer
gtk_list_item_manager_get_last (GtkListItemManager *self)
{
  return gtk_rb_tree_get_last (self->items);
}

gpointer
gtk_list_item_manager_get_root (GtkListItemManager *self)
{
  return gtk_rb_tree_get_root (self->items);
}

/*
 * gtk_list_item_manager_get_nth:
 * @self: a `GtkListItemManager`
 * @position: position of the item
 * @offset: (out): offset into the returned tile
 *
 * Looks up the GtkListTile that represents @position.
 *
 * If a the returned tile represents multiple rows, the @offset into
 * the returned tile for @position will be set. If the returned tile
 * represents a row with an existing widget, @offset will always be 0.
 *
 * Returns: (type GtkListTile): the tile for @position or
 *   %NULL if position is out of range
 **/
gpointer
gtk_list_item_manager_get_nth (GtkListItemManager *self,
                               guint               position,
                               guint              *offset)
{
  GtkListTile *tile, *tmp;

  tile = gtk_rb_tree_get_root (self->items);

  while (tile)
    {
      tmp = gtk_rb_tree_node_get_left (tile);
      if (tmp)
        {
          GtkListTileAugment *aug = gtk_rb_tree_get_augment (self->items, tmp);
          if (position < aug->n_items)
            {
              tile = tmp;
              continue;
            }
          position -= aug->n_items;
        }

      if (position < tile->n_items)
        break;
      position -= tile->n_items;

      tile = gtk_rb_tree_node_get_right (tile);
    }

  if (offset)
    *offset = tile ? position : 0;

  return tile;
}

static GtkListTile *
gtk_list_tile_get_header (GtkListItemManager *self,
                          GtkListTile        *tile)
{
  GtkListTileAugment *aug;
  GtkListTile *other;
  gboolean check_right = FALSE;

  while (TRUE)
    {
      if (check_right)
        {
          other = gtk_rb_tree_node_get_right (tile);
          if (other)
            {
              aug = gtk_rb_tree_get_augment (self->items, other);
              if (aug->has_header)
                {
                  check_right = TRUE;
                  tile = other;
                  continue;
                }
            }
        }

      if (tile->type == GTK_LIST_TILE_HEADER ||
          tile->type == GTK_LIST_TILE_UNMATCHED_HEADER)
        return tile;

      other = gtk_rb_tree_node_get_left (tile);
      if (other)
        {
          aug = gtk_rb_tree_get_augment (self->items, other);
          if (aug->has_header)
            {
              check_right = TRUE;
              tile = other;
              continue;
            }
        }

      while ((other = gtk_rb_tree_node_get_parent (tile)))
        {
          if (gtk_rb_tree_node_get_right (other) == tile)
            break;
          tile = other;
        }
      tile = other;
      check_right = FALSE;
    }
}

static GtkListTile *
gtk_list_tile_get_footer (GtkListItemManager *self,
                          GtkListTile        *tile)