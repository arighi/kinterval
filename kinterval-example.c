/*
 * Generic interval example
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
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/cryptohash.h>
#include <linux/percpu-defs.h>

#include "kinterval.h"

enum __mem_type {
	PAGE_CACHE_NORMAL,
	PAGE_CACHE_NOREUSE,
};

/* Extended rbtree to store all ranges */
static DEFINE_KINTERVAL_TREE(kinterval_tree);

/* Protect the kinterval_tree tree */
static DEFINE_MUTEX(kinterval_lock);

static const char procfs_name[] = "kinterval";

static struct proc_dir_entry *procfs_file;

static char *range_attr_name(unsigned long flags)
{
	switch (flags) {
	case PAGE_CACHE_NORMAL:
		return "normal";
	case PAGE_CACHE_NOREUSE:
		return "noreuse";
	default:
		return "(none)";
	}
}

static unsigned int get_random_int(void)
{
	__u32 hash, random;
	unsigned int ret;

	hash = get_cycles();
	md5_transform(&hash, &random);
	ret = hash;

	return ret;
}

static void kinterval_dump(struct seq_file *m)
{
	struct kinterval *range;
	struct rb_node *node;

	node = rb_first(&kinterval_tree);
	while (node) {
		range = rb_entry(node, struct kinterval, rb);
		seq_printf(m, "  start=%llu end=%llu type=%lu (%s)\n",
					range->start, range->end, range->type,
					range_attr_name(range->type));
		node = rb_next(&range->rb);
	}
}

static int procfs_read(struct seq_file *m, void *v)
{
	unsigned int test_addr = get_random_int() % 10000;
	long type;

	seq_puts(m, "tree dump:\n");
	kinterval_dump(m);

	mutex_lock(&kinterval_lock);
	type = kinterval_lookup(&kinterval_tree, test_addr);
	mutex_unlock(&kinterval_lock);

	seq_printf(m, "address %u: type %#lx %s\n",
			test_addr, type, range_attr_name(type));

	return 0;
}

static int procfs_open(struct inode *inode, struct file *file)
{
	int ret;
	int i;

	mutex_lock(&kinterval_lock);
	for (i = 0; i < 1000; i++) {
		int start, end, type;

		start = get_random_int() % 10000;
		end = get_random_int() % 10000;
		type = get_random_int() % 2;

		if (i & 1)
			kinterval_add(&kinterval_tree, min(start, end), max(start, end),
						type, GFP_KERNEL);
		else
			kinterval_del(&kinterval_tree, min(start, end), max(start, end),
						GFP_KERNEL);
	}

	ret = single_open(file, procfs_read, NULL);
	if (ret < 0)
		kinterval_clear(&kinterval_tree);
	mutex_unlock(&kinterval_lock);

	return ret;
}

static int procfs_release(struct inode *inode, struct file *file)
{
	mutex_lock(&kinterval_lock);
	kinterval_clear(&kinterval_tree);
	mutex_unlock(&kinterval_lock);

	return 0;
}

static const struct file_operations procfs_fops = {
	.open		= procfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= procfs_release,
};

static int __init kinterval_example_init(void)
{
	procfs_file = proc_create(procfs_name, 0666, NULL, &procfs_fops);
	if (unlikely(!procfs_file))
		return -ENOMEM;
	return 0;
}

static void __exit kinterval_example_exit(void)
{
	remove_proc_entry(procfs_name, NULL);
}

module_init(kinterval_example_init);
module_exit(kinterval_example_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("kernel interval example");
MODULE_AUTHOR("Andrea Righi <andrea@betterlinux.com>");
