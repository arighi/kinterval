/*
 * kinterval.c - Routines for manipulating generic intervals
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

#include <linux/init.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/rbtree.h>
#include "kinterval.h"

static struct kmem_cache *kinterval_cachep __read_mostly;

static bool is_interval_overlapping(struct kinterval *node, u64 start, u64 end)
{
        return node->start <= end && start <= node->end;
}

static u64 get_subtree_max_end(struct rb_node *node)
{
	struct kinterval *range;

	if (!node)
		return 0;
	range = rb_entry(node, struct kinterval, rb);

	return range->subtree_max_end;
}

/* Update 'subtree_max_end' for a node, based on node and its children */
static void kinterval_rb_augment_cb(struct rb_node *node, void *__unused)
{
	struct kinterval *range;
	u64 max_end, child_max_end;

	if (!node)
		return;
	range = rb_entry(node, struct kinterval, rb);
	max_end = range->end;

	child_max_end = get_subtree_max_end(node->rb_right);
	if (child_max_end > max_end)
		max_end = child_max_end;

	child_max_end = get_subtree_max_end(node->rb_left);
	if (child_max_end > max_end)
		max_end = child_max_end;

	range->subtree_max_end = max_end;
}

/*
 * Find the lowest overlapping range from the tree.
 *
 * Return NULL if there is no overlap.
 */
static struct kinterval *
kinterval_rb_lowest_match(struct rb_root *root, u64 start, u64 end)
{
	struct rb_node *node = root->rb_node;
	struct kinterval *lowest_match = NULL;

	while (node) {
		struct kinterval *range = rb_entry(node, struct kinterval, rb);

		if (get_subtree_max_end(node->rb_left) > start) {
			/* Lowest overlap if any must be on the left side */
			node = node->rb_left;
		} else if (is_interval_overlapping(range, start, end)) {
			lowest_match = range;
			break;
		} else if (start >= range->start) {
			/* Lowest overlap if any must be on the right side */
			node = node->rb_right;
		} else {
			break;
		}
	}
	return lowest_match;
}

/*
 * Merge two adjacent intervals, if they can be merged next is removed from the
 * tree.
 */
static void kinterval_rb_merge_node(struct rb_root *root,
			struct kinterval *prev, struct kinterval *next)
{
	struct rb_node *deepest;

	if (prev && prev->type == next->type && prev->end == next->start) {
		prev->end = next->end;
		deepest = rb_augment_erase_begin(&next->rb);
		rb_erase(&next->rb, root);
		rb_augment_erase_end(deepest,
				kinterval_rb_augment_cb, NULL);
		kmem_cache_free(kinterval_cachep, next);
	}
}

/*
 * Try to merge a new inserted interval with the previous and the next
 * interval.
 */
static void kinterval_rb_merge(struct rb_root *root, struct kinterval *new)
{
	struct kinterval *next, *prev;
	struct rb_node *node;

	node = rb_prev(&new->rb);
	prev = node ? rb_entry(node, struct kinterval, rb) : NULL;

	node = rb_next(&new->rb);
	next = node ? rb_entry(node, struct kinterval, rb) : NULL;

	if (next)
		kinterval_rb_merge_node(root, new, next);
	if (prev)
		kinterval_rb_merge_node(root, prev, new);
}

static void
kinterval_rb_insert(struct rb_root *root, struct kinterval *new)
{
	struct rb_node **node = &(root->rb_node);
	struct rb_node *parent = NULL;

	while (*node) {
		struct kinterval *range = rb_entry(*node, struct kinterval, rb);

		parent = *node;
		if (new->start <= range->start)
			node = &((*node)->rb_left);
		else if (new->start > range->start)
			node = &((*node)->rb_right);
	}

	rb_link_node(&new->rb, parent, node);
	rb_insert_color(&new->rb, root);
	rb_augment_insert(&new->rb, kinterval_rb_augment_cb, NULL);

	kinterval_rb_merge(root, new);
}

static int kinterval_rb_check_add(struct rb_root *root,
				struct kinterval *new, gfp_t flags)
{
	struct kinterval *old;
	struct rb_node *node, *deepest;

	old = kinterval_rb_lowest_match(root, new->start, new->end);
	node = old ? &old->rb : NULL;

	while (node) {
		old = rb_entry(node, struct kinterval, rb);
		node = rb_next(&old->rb);

		/* Check all the possible matches within the range */
		if (old->start >= new->end)
			break;

		/*
		 * Interval is overlapping another one, shrink the old interval
		 * accordingly.
		 */
		if (new->start == old->start && new->end == old->end) {
			/*
			 * Exact match, just update the type:
			 *
			 * old
			 * |___________________|
			 * new
			 * |___________________|
			 */
			old->type = new->type;
			kmem_cache_free(kinterval_cachep, new);
			return 0;
		} else if (new->start <= old->start && new->end >= old->end) {
			/*
			 * New range completely overwrites the old one:
			 *
			 *      old
			 *      |________|
			 * new
			 * |___________________|
			 *
			 * Replace old with new.
			 */
			deepest = rb_augment_erase_begin(&old->rb);
			rb_erase(&old->rb, root);
			rb_augment_erase_end(deepest, kinterval_rb_augment_cb,
						NULL);
			kmem_cache_free(kinterval_cachep, old);
		} else if (new->start <= old->start && new->end <= old->end) {
			/*
			 * Update the start of the interval:
			 *
			 * - before:
			 *
			 *       old
			 *       |_____________|
			 * new
			 * |___________|
			 *
			 * - after:
			 *
			 * new         old
			 * |___________|_______|
			 */
			deepest = rb_augment_erase_begin(&old->rb);
			rb_erase(&old->rb, root);
			rb_augment_erase_end(deepest, kinterval_rb_augment_cb,
						NULL);
			old->start = new->end + 1;
			old->subtree_max_end = old->end;
			kinterval_rb_insert(root, old);
			break;
		} else if (new->start >= old->start && new->end >= old->end) {
			/*
			 * Update the end of the interval:
			 *
			 * - before:
			 *
			 * old
			 * |_____________|
			 *          new
			 *          |___________|
			 *
			 * - after:
			 *
			 * old      new
			 * |________|__________|
			 */
			deepest = rb_augment_erase_begin(&old->rb);
			rb_erase(&old->rb, root);
			rb_augment_erase_end(deepest, kinterval_rb_augment_cb,
						NULL);
			old->end = new->start;
			old->subtree_max_end = old->end;
			kinterval_rb_insert(root, old);
		} else if (new->start >= old->start && new->end <= old->end) {
			struct kinterval *prev;

			if (new->type == old->type) {
				/* Same type, just drop the new element */
				kmem_cache_free(kinterval_cachep, new);
				return 0;
			}
			/*
			 * Insert the new interval in the middle of another
			 * one.
			 *
			 * - before:
			 *
			 * old
			 * |___________________|
			 *       new
			 *       |_______|
			 *
			 * - after:
			 *
			 * prev  new     old
			 * |_____|_______|_____|
			 */
			prev = kmem_cache_zalloc(kinterval_cachep, flags);
			if (unlikely(!prev))
				return -ENOMEM;

			deepest = rb_augment_erase_begin(&old->rb);
			rb_erase(&old->rb, root);
			rb_augment_erase_end(deepest, kinterval_rb_augment_cb,
						NULL);

			prev->start = old->start;
			old->start = new->end;
			prev->end = new->start;
			prev->type = old->type;

			old->subtree_max_end = old->end;
			kinterval_rb_insert(root, old);

			new->subtree_max_end = new->end;
			kinterval_rb_insert(root, new);

			prev->subtree_max_end = prev->end;
			kinterval_rb_insert(root, prev);
			return 0;
		}
	}
	new->subtree_max_end = new->end;
	kinterval_rb_insert(root, new);

	return 0;
}

int kinterval_add(struct rb_root *root, u64 start, u64 end,
			long type, gfp_t flags)
{
	struct kinterval *range;
	int ret;

	if (end <= start)
		return -EINVAL;
	range = kmem_cache_zalloc(kinterval_cachep, flags);
	if (unlikely(!range))
		return -ENOMEM;
	range->start = start;
	range->end = end;
	range->type = type;

	ret = kinterval_rb_check_add(root, range, flags);
	if (unlikely(ret < 0))
		kmem_cache_free(kinterval_cachep, range);

	return ret;
}
EXPORT_SYMBOL(kinterval_add);

static int kinterval_rb_check_del(struct rb_root *root,
				u64 start, u64 end, gfp_t flags)
{
	struct kinterval *old;
	struct rb_node *node, *deepest;

	old = kinterval_rb_lowest_match(root, start, end);
	node = old ? &old->rb : NULL;

	while (node) {
		old = rb_entry(node, struct kinterval, rb);
		node = rb_next(&old->rb);

		/* Check all the possible matches within the range */
		if (old->start >= end)
			break;

		if (start <= old->start && end >= old->end) {
			/*
			 * Completely erase the old range:
			 *
			 *      old
			 *      |________|
			 * erase
			 * |___________________|
			 */
			deepest = rb_augment_erase_begin(&old->rb);
			rb_erase(&old->rb, root);
			rb_augment_erase_end(deepest, kinterval_rb_augment_cb,
						NULL);
			kmem_cache_free(kinterval_cachep, old);
		} else if (start <= old->start && end <= old->end) {
			/*
			 * Trim the beginning of an interval:
			 *
			 * - before:
			 *
			 *       old
			 *       |_____________|
			 * erase
			 * |___________|
			 *
			 * - after:
			 *
			 *             old
			 *             |_______|
			 */
			deepest = rb_augment_erase_begin(&old->rb);
			rb_erase(&old->rb, root);
			rb_augment_erase_end(deepest, kinterval_rb_augment_cb,
						NULL);
			old->start = end;
			old->subtree_max_end = old->end;
			kinterval_rb_insert(root, old);
			break;
		} else if (start >= old->start && end >= old->end) {
			/*
			 * Trim the end of an interval:
			 *
			 * - before:
			 *
			 * old
			 * |_____________|
			 *          erase
			 *          |___________|
			 *
			 * - after:
			 *
			 * old
			 * |________|
			 */
			deepest = rb_augment_erase_begin(&old->rb);
			rb_erase(&old->rb, root);
			rb_augment_erase_end(deepest, kinterval_rb_augment_cb,
						NULL);
			old->end = start;
			old->subtree_max_end = old->end;
			kinterval_rb_insert(root, old);
		} else if (start >= old->start && end <= old->end) {
			struct kinterval *prev;

			/*
			 * Trim the middle of an interval:
			 *
			 * - before:
			 *
			 * old
			 * |___________________|
			 *       erase
			 *       |_______|
			 *
			 * - after:
			 *
			 * prev          old
			 * |_____|       |_____|
			 */
			prev = kmem_cache_zalloc(kinterval_cachep, flags);
			if (unlikely(!prev))
				return -ENOMEM;

			deepest = rb_augment_erase_begin(&old->rb);
			rb_erase(&old->rb, root);
			rb_augment_erase_end(deepest, kinterval_rb_augment_cb,
						NULL);

			prev->start = old->start;
			old->start = end;
			prev->end = start;
			prev->type = old->type;

			old->subtree_max_end = old->end;
			kinterval_rb_insert(root, old);

			prev->subtree_max_end = prev->end;
			kinterval_rb_insert(root, prev);
			break;
		}
	}
	return 0;
}

int kinterval_del(struct rb_root *root, u64 start, u64 end, gfp_t flags)
{
	if (end <= start)
		return -EINVAL;
	return kinterval_rb_check_del(root, start, end, flags);
}
EXPORT_SYMBOL(kinterval_del);

void kinterval_clear(struct rb_root *root)
{
	struct kinterval *range;
	struct rb_node *node;

	node = rb_first(root);
	while (node) {
		range = rb_entry(node, struct kinterval, rb);
#ifdef DEBUG
		printk(KERN_INFO "start=%llu end=%llu type=%lu\n",
					range->start, range->end, range->type);
#endif
		node = rb_next(&range->rb);
		rb_erase(&range->rb, root);
		kmem_cache_free(kinterval_cachep, range);
	}
}
EXPORT_SYMBOL(kinterval_clear);

long kinterval_lookup_range(struct rb_root *root, u64 start, u64 end)
{
	struct kinterval *range;

	if (end <= start)
		return -EINVAL;
	range = kinterval_rb_lowest_match(root, start, end);
	return range ? range->type : -ENOENT;
}
EXPORT_SYMBOL(kinterval_lookup_range);

static int __init kinterval_init(void)
{
	kinterval_cachep = kmem_cache_create("kinterval_cache",
					sizeof(struct kinterval),
					0, 0, NULL);
	if (unlikely(!kinterval_cachep)) {
		printk(KERN_ERR "kinterval: failed to create slab cache\n");
		return -ENOMEM;
	}
	return 0;
}

static void __exit kinterval_exit(void)
{
	kmem_cache_destroy(kinterval_cachep);
}

module_init(kinterval_init);
module_exit(kinterval_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Generic interval ranges");
MODULE_AUTHOR("Andrea Righi <andrea@betterlinux.com>");
