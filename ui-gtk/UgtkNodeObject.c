/*
 *
 *   Copyright (C) 2026 by Ozgur As
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

#include <UgtkNodeObject.h>

G_DEFINE_TYPE (UgtkNodeObject, ugtk_node_object, G_TYPE_OBJECT)

static void ugtk_node_object_class_init (UgtkNodeObjectClass* klass)
{
}

static void ugtk_node_object_init (UgtkNodeObject* self)
{
	self->node = NULL;
}

UgtkNodeObject* ugtk_node_object_new (UgetNode* node)
{
	UgtkNodeObject* obj;

	ugtk_node_object_track_init ();
	obj = g_object_new (UGTK_TYPE_NODE_OBJECT, NULL);
	obj->node = node;
	return obj;
}

// ---------------------------------------------------------------
// Node lifetime tracking.
//
// GtkListItem / selection models cache UgtkNodeObject which wraps a
// raw UgetNode pointer.  The C node can be freed while GTK still
// holds the wrapper (deleted downloads, recycled clones, etc.)
// which caused heap-use-after-free crashes (bind_* in UgtkNodeView).
//
// The library notifies allocation/freeing of every heap UgetNode;
// we keep a "live" set, and get_node_from_item() refuses nodes that
// are no longer alive.

static GHashTable*  node_live_table = NULL;

static void ugtk_node_track_alloc (UgetNode* node)
{
	g_hash_table_add (node_live_table, node);
}

static void ugtk_node_track_free (UgetNode* node)
{
	g_hash_table_remove (node_live_table, node);
}

gboolean  ugtk_node_object_is_live (UgetNode* node)
{
	if (node_live_table == NULL)
		return TRUE;    // tracking disabled: assume alive
	return g_hash_table_contains (node_live_table, node);
}

void  ugtk_node_object_track_init (void)
{
	if (node_live_table)
		return;
	node_live_table = g_hash_table_new (g_direct_hash, g_direct_equal);
	uget_node_set_track_funcs (ugtk_node_track_alloc, ugtk_node_track_free);
}
