/*
 *
 *   Copyright (C) 2026 by Ozgur As
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

#include <UgtkNodeTree.h>
#include <UgtkNodeObject.h>

// GListModel interface
static GType    list_model_get_item_type (GListModel* list);
static guint    list_model_get_n_items   (GListModel* list);
static gpointer list_model_get_item      (GListModel* list, guint position);

static void ugtk_node_tree_list_model_init (GListModelInterface* iface)
{
	iface->get_item_type = list_model_get_item_type;
	iface->get_n_items   = list_model_get_n_items;
	iface->get_item      = list_model_get_item;
}

G_DEFINE_TYPE_WITH_CODE (UgtkNodeTree, ugtk_node_tree, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, ugtk_node_tree_list_model_init))

static void ugtk_node_tree_class_init (UgtkNodeTreeClass* klass)
{
}

static void ugtk_node_tree_init (UgtkNodeTree* utree)
{
	utree->root = NULL;
	utree->last_count = 0;
	utree->prefix.root = NULL;
	utree->prefix.len = 0;
}

// --- helpers ---

static gint ugtk_node_tree_prefix_count (UgtkNodeTree* utree)
{
	gint n;

	if (utree->prefix.root == NULL)
		return 0;
	n = utree->prefix.len;
	if (n > utree->prefix.root->n_children || n == 0)
		n = utree->prefix.root->n_children;
	return n;
}

static gint ugtk_node_tree_count (UgtkNodeTree* utree)
{
	gint n;

	n = ugtk_node_tree_prefix_count (utree);
	if (utree->root)
		n += utree->root->n_children;
	return n;
}

static UgetNode* ugtk_node_tree_nth (UgtkNodeTree* utree, guint position)
{
	gint n_prefix;

	n_prefix = ugtk_node_tree_prefix_count (utree);

	if ((gint)position < n_prefix)
		return uget_node_nth_child (utree->prefix.root, position);

	position -= n_prefix;
	if (utree->root == NULL)
		return NULL;
	return uget_node_nth_child (utree->root, position);
}

// --- GListModel interface ---

static GType list_model_get_item_type (GListModel* list)
{
	return UGTK_TYPE_NODE_OBJECT;
}

static guint list_model_get_n_items (GListModel* list)
{
	return ugtk_node_tree_count (UGTK_NODE_TREE (list));
}

static gpointer list_model_get_item (GListModel* list, guint position)
{
	UgtkNodeTree* utree = UGTK_NODE_TREE (list);
	UgetNode* node;

	node = ugtk_node_tree_nth (utree, position);
	if (node == NULL)
		return NULL;
	return ugtk_node_tree_find_object (utree, node);
}

// --- canonical row objects -------------------------------------------
//
// get_item() used to construct a fresh UgtkNodeObject on every call,
// which broke the GListModel identity contract ("the same item must
// be returned for the same position while unchanged") and made it
// impossible to reach the object that row widgets are bound to.  Keep
// one canonical wrapper per node; entries self-remove when the
// wrapper is finalized (weak notify, the cache holds no reference).

static gboolean ugtk_node_tree_cache_is_dead (gpointer key, gpointer value, gpointer user)
{
	return value == user;
}

static void ugtk_node_tree_cache_weak (gpointer data, GObject* dead)
{
	UgtkNodeTree* utree = data;

	// entries are keyed by node, the value is the wrapper: the dead
	// wrapper reaches us only as 'dead', so match on the value side.
	if (utree->obj_cache)
		g_hash_table_foreach_remove (utree->obj_cache,
				ugtk_node_tree_cache_is_dead, dead);
}

UgtkNodeObject* ugtk_node_tree_find_object (UgtkNodeTree* utree, UgetNode* node)
{
	UgtkNodeObject* obj;

	if (utree->obj_cache == NULL) {
		utree->obj_cache = g_hash_table_new (
				g_direct_hash, g_direct_equal);
	}
	obj = g_hash_table_lookup (utree->obj_cache, node);
	if (obj == NULL) {
		// first reference hands ownership to the caller; the cache
		// keeps only a weak pointer (g_object_weak_ref above).
		obj = ugtk_node_object_new (node);
		g_hash_table_insert (utree->obj_cache, node, obj);
		g_object_weak_ref (G_OBJECT (obj),
				ugtk_node_tree_cache_weak, utree);
		return obj;
	}
	return g_object_ref (obj);
}

// --- public API ---

UgtkNodeTree* ugtk_node_tree_new (UgetNode* root, gboolean list_only)
{
	UgtkNodeTree* utree;

	utree = g_object_new (UGTK_TYPE_NODE_TREE, NULL);
	utree->root = root;
	// list_only was for GtkTreeModel flags; no longer needed for GListModel
	return utree;
}

void ugtk_node_tree_set_prefix (UgtkNodeTree* utree, UgetNode* prefix_root, gint prefix_len)
{
	utree->prefix.root = prefix_root;
	utree->prefix.len = prefix_len;
}

void ugtk_node_tree_refresh (UgtkNodeTree* utree)
{
	guint old_n = utree->last_count;
	guint new_n = ugtk_node_tree_count (utree);
	utree->last_count = new_n;
	g_list_model_items_changed (G_LIST_MODEL (utree), 0, old_n, new_n);
}

// Used by the node notification bridge (UgtkApp-callback.c).  The
// removed/added numbers it computes from node events may drift from
// what get_n_items() reports (the same node can move/be reported
// through several virtual trees), so re-sync last_count with the
// live tree after emitting.
void ugtk_node_tree_notify (UgtkNodeTree* utree, guint position, guint removed, guint added)
{
	g_list_model_items_changed (G_LIST_MODEL (utree), position, removed, added);
	utree->last_count = ugtk_node_tree_count (utree);
}
