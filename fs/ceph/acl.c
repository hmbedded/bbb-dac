/*
 * linux/fs/ceph/acl.c
 *
 * Copyright (C) 2013 Guangliang Zhao, <lucienchao@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
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
 */

#include <linux/ceph/ceph_debug.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/xattr.h>
#include <linux/posix_acl_xattr.h>
#include <linux/posix_acl.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "super.h"

static inline void ceph_set_cached_acl(struct inode *inode,
					int type, struct posix_acl *acl)
{
	struct ceph_inode_info *ci = ceph_inode(inode);

	spin_lock(&ci->i_ceph_lock);
	if (__ceph_caps_issued_mask(ci, CEPH_CAP_XATTR_SHARED, 0))
		set_cached_acl(inode, type, acl);
	spin_unlock(&ci->i_ceph_lock);
}

static inline struct posix_acl *ceph_get_cached_acl(struct inode *inode,
							int type)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct posix_acl *acl = ACL_NOT_CACHED;

	spin_lock(&ci->i_ceph_lock);
	if (__ceph_caps_issued_mask(ci, CEPH_CAP_XATTR_SHARED, 0))
		acl = get_cached_acl(inode, type);
	spin_unlock(&ci->i_ceph_lock);

	return acl;
}

struct posix_acl *ceph_get_acl(struct inode *inode, int type)
{
	int size;
	const char *name;
	char *value = NULL;
	struct posix_acl *acl;

	switch (type) {
	case ACL_TYPE_ACCESS:
		name = POSIX_ACL_XATTR_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		name = POSIX_ACL_XATTR_DEFAULT;
		break;
	default:
		BUG();
	}

	size = __ceph_getxattr(inode, name, "", 0);
	if (size > 0) {
		value = kzalloc(size, GFP_NOFS);
		if (!value)
			return ERR_PTR(-ENOMEM);
		size = __ceph_getxattr(inode, name, value, size);
	}

	if (size > 0)
		acl = posix_acl_from_xattr(&init_user_ns, value, size);
	else if (size == -ERANGE || size == -ENODATA || size == 0)
		acl = NULL;
	else
		acl = ERR_PTR(-EIO);

	kfree(value);

	if (!IS_ERR(acl))
		ceph_set_cached_acl(inode, type, acl);

	return acl;
}

int ceph_set_acl(struct inode *inode, struct posix_acl *acl, int type)
{
	int ret = 0, size = 0;
	const char *name = NULL;
	char *value = NULL;
	struct iattr newattrs;
	umode_t new_mode = inode->i_mode, old_mode = inode->i_mode;
	struct dentry *dentry;

	if (acl) {
		ret = posix_acl_valid(acl);
		if (ret < 0)
			goto out;
	}

	switch (type) {
	case ACL_TYPE_ACCESS:
		name = POSIX_ACL_XATTR_ACCESS;
		if (acl) {
			ret = posix_acl_equiv_mode(acl, &new_mode);
			if (ret < 0)
				goto out;
			if (ret == 0)
				acl = NULL;
		}
		break;
	case ACL_TYPE_DEFAULT:
		if (!S_ISDIR(inode->i_mode)) {
			ret = acl ? -EINVAL : 0;
			goto out;
		}
		name = POSIX_ACL_XATTR_DEFAULT;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	if (acl) {
		size = posix_acl_xattr_size(acl->a_count);
		value = kmalloc(size, GFP_NOFS);
		if (!value) {
			ret = -ENOMEM;
			goto out;
		}

		ret = posix_acl_to_xattr(&init_user_ns, acl, value, size);
		if (ret < 0)
			goto out_free;
	}

	dentry = d_find_alias(inode);
	if (new_mode != old_mode) {
		newattrs.ia_mode = new_mode;
		newattrs.ia_valid = ATTR_MODE;
		ret = ceph_setattr(dentry, &newattrs);
		if (ret)
			goto out_dput;
	}

	ret = __ceph_setxattr(dentry, name, value, size, 0);
	if (ret) {
		if (new_mode != old_mode) {
			newattrs.ia_mode = old_mode;
			newattrs.ia_valid = ATTR_MODE;
			ceph_setattr(dentry, &newattrs);
		}
		goto out_dput;
	}

	ceph_set_cached_acl(inode, type, acl);

out_dput:
	dput(dentry);
out_free:
	kfree(value);
out:
	return ret;
}

int ceph_init_acl(struct dentry *dentry, struct inode *inode, struct inode *dir)
{
	struct posix_acl *default_acl, *acl;
	int error;

	error = posix_acl_create(dir, &inode->i_mode, &default_acl, &acl);
	if (error)
		return error;

	if (!default_acl && !acl)
		cache_no_acl(inode);

	if (default_acl) {
		error = ceph_set_acl(inode, default_acl, ACL_TYPE_DEFAULT);
		posix_acl_release(default_acl);
	}
	if (acl) {
		if (!error)
			error = ceph_set_acl(inode, acl, ACL_TYPE_ACCESS);
		posix_acl_release(acl);
	}
	return error;
}