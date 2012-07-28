/*
 * kinterval.h - Routines for manipulating generic intervals
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 * Copyright (C) 2012 Andrea Righi <andrea@betterlinux.com>
 */

#ifndef _LINUX_KINTERVAL_H
#define _LINUX_KINTERVAL_H

#include <linux/types.h>
#include <linux/rbtree.h>

/**
 * struct kinterval - define a range in an interval tree
 * @start: address representing the start of the range.
 * @end: address representing the end of the range.
 * @subtree_max_end: augmented rbtree data to perform quick lookup of the
 *                   overlapping ranges.
 * @type: type of the interval (defined by the user).
 * @rb: the rbtree node.
 */
struct kinterval {
	u64 start;
	u64 end;
	u64 subtree_max_end;
	unsigned long type;
	struct rb_node rb;
};

/**
 * DECLARE_KINTERVAL_TREE - macro to declare an interval tree
 * @__name: name of the declared interval tree.
 *
 * The tree is an interval tree (augmented rbtree) with tree ordered
 * on starting address. Tree cannot contain multiple entries for differnt
 * ranges which overlap; in case of overlapping ranges new inserted intervals
 * overwrite the old ones (completely or in part, in the second case the old
 * interval is shrinked accordingly).
 *
 * NOTE: all locking issues are left to the caller.
 *
 * Reference:
 * "Introduction to Algorithms" by Cormen, Leiserson, Rivest and Stein.
 */
#define DECLARE_KINTERVAL_TREE(__name) struct rb_root __name

/**
 * DEFINE_KINTERVAL_TREE - macro to define and initialize an interval tree
 * @__name: name of the declared interval tree.
 */
#define DEFINE_KINTERVAL_TREE(__name) \
		struct rb_root __name = RB_ROOT

/**
 * INIT_KINTERVAL_TREE_ROOT - macro to initialize an interval tree
 * @__root: root of the declared interval tree.
 */
#define INIT_KINTERVAL_TREE_ROOT(__root)	\
	do {					\
		(__root)->rb_node = NULL;	\
	} while (0)

/**
 * kinterval_add - define a new range into the interval tree
 * @root: the root of the tree.
 * @start: start of the range to define.
 * @end: end of the range to define.
 * @type: attribute assinged to the range.
 * @flags: type of memory to allocate (see kcalloc).
 */
int kinterval_add(struct rb_root *root, u64 start, u64 end,
			long type, gfp_t flags);

/**
 * kinterval_del - erase a range from the interval tree
 * @root: the root of the tree.
 * @start: start of the range to erase.
 * @end: end of the range to erase.
 * @flags: type of memory to allocate (see kcalloc).
 */
int kinterval_del(struct rb_root *root, u64 start, u64 end, gfp_t flags);

/**
 * kinterval_lookup_range - return the attribute of a range
 * @root: the root of the tree.
 * @start: start of the range to lookup.
 * @end: end of the range to lookup.
 *
 * NOTE: return the type of the lowest match, if the range specified by the
 * arguments overlaps multiple intervals only the type of the first one
 * (lowest) is returned.
 */
long kinterval_lookup_range(struct rb_root *root, u64 start, u64 end);

/**
 * kinterval_lookup - return the attribute of an address
 * @root: the root of the tree.
 * @addr: address to lookup.
 */
static inline long kinterval_lookup(struct rb_root *root, u64 addr)
{
	return kinterval_lookup_range(root, addr, addr + 1);
}

/**
 * kinterval_clear - erase all intervals defined in an interval tree
 * @root: the root of the tree.
 */
void kinterval_clear(struct rb_root *root);

#endif /* _LINUX_KINTERVAL_H */
