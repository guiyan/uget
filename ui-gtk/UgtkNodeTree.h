/*
 *
 *   Copyright (C) 2012-2020 by C.H. Huang
 *   plushuang.tw@gmail.com
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *  ---
 *
 *  In addition, as a special exception, the copyright holders give
 *  permission to link the code of portions of this program with the
 *  OpenSSL library under certain conditions as described in each
 *  individual source file, and distribute linked combinations
 *  including the two.
 *  You must obey the GNU Lesser General Public License in all respects
 *  for all of the code used other than OpenSSL.  If you modify
 *  file(s) with this exception, you may extend this exception to your
 *  version of the file(s), but you are not obligated to do so.  If you
 *  do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source
 *  files in the program, then also delete it here.
 *
 */

// GListModel implementation for UgetNode children (with optional prefix).

#ifndef UGTK_NODE_TREE_H
#define UGTK_NODE_TREE_H

#include <gtk/gtk.h>
#include <UgetNode.h>
#include <UgtkNodeObject.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UGTK_TYPE_NODE_TREE              (ugtk_node_tree_get_type ())
#define UGTK_NODE_TREE(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), UGTK_TYPE_NODE_TREE, UgtkNodeTree))
#define UGTK_IS_NODE_TREE(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), UGTK_TYPE_NODE_TREE))

typedef struct UgtkNodeTree       UgtkNodeTree;
typedef struct UgtkNodeTreeClass  UgtkNodeTreeClass;

// GListModel for UgetNode: shows prefix children + root children (flat).
struct UgtkNodeTree
{
	GObject     parent;

	UgetNode*   root;
	guint       last_count;

	struct {
		UgetNode* root;
		gint      len;
	} prefix;

	// canonical UgtkNodeObject per row node: get_item() must keep item
	// identity stable while the model is unchanged (GListModel
	// contract), and the notification bridge needs to reach the very
	// object instance the view's row widgets are bound to.
	GHashTable* obj_cache;
};

struct UgtkNodeTreeClass
{
	GObjectClass  parent_class;
};

UgtkNodeTree*  ugtk_node_tree_new (UgetNode* root, gboolean list_only);

GType  ugtk_node_tree_get_type (void);
void   ugtk_node_tree_set_prefix (UgtkNodeTree* utree, UgetNode* prefix_root, gint prefix_len);

// Canonical UgtkNodeObject wrapper for 'node' (as returned by
// get_item()), or NULL when the node is not a row of this tree.
// Returns a full reference (g_object_ref-style).
UgtkNodeObject*  ugtk_node_tree_find_object (UgtkNodeTree* utree, UgetNode* node);

// Notify the model that items changed
void  ugtk_node_tree_refresh (UgtkNodeTree* utree);
// Emit a precise change and re-sync last_count with the live tree.
void  ugtk_node_tree_notify (UgtkNodeTree* utree, guint position, guint removed, guint added);

#ifdef __cplusplus
}
#endif

#endif // UGTK_NODE_TREE_H
