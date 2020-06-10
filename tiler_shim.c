/*

OpenGL TILER rotation shim

This shim rewrites IOCTLs to use TILER buffers to enable hardware rotation
of fullscreen OpenGL ES applications.

Building:

	$ gcc -shared -fpic -ldl -o tiler_shim.so  -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast  tiler_shim.c

Using:
	
	$ LD_PRELOAD=tiler_shim.so LIBGL_FB=1 an_opengl_application

The LIBGL_FB=1 is for gl4es only, to force fullscreen. In other situations
you will need to enable fullscreen without X11 by other means.

Known issues:

 	- Debugging needs to be tidied up/removed

Copyright 2020 David Shah <dave@ds0.me>
Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.


*/

#define _GNU_SOURCE
#include <dlfcn.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <linux/ioctl.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <drm/omap_drm.h>

int init_done = 0;
int debug_flag = 0;

int  (*libc_ioctl)(int fd, unsigned long request, char *argp);

int test_flag(const char *name) {
	const char *e = getenv(name);
	if (!e)
		return 0;
	return atoi(e);
}

void init(void) {
	if (init_done)
		return;

	debug_flag = test_flag("ROTATE_DEBUG");
	libc_ioctl = dlsym(RTLD_NEXT, "ioctl");

	init_done = 1;
}

int get_rotation_property_key(int fd, int plane) {
#define MAX_PROPS 64
	uint32_t properties[MAX_PROPS];
	uint64_t prop_values[MAX_PROPS];
	struct drm_mode_obj_get_properties get_props;
	get_props.props_ptr = (uint64_t)&properties;
	get_props.prop_values_ptr = (uint64_t)&prop_values;
	get_props.count_props = MAX_PROPS;
	get_props.obj_id = plane;
	get_props.obj_type = DRM_MODE_OBJECT_PLANE;
	int ret = libc_ioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, (char *)&get_props);
	if (ret != 0) {
		printf("get_rotation_property_key DRM_IOCTL_MODE_OBJ_GETPROPERTIES failed: %d\n", ret);
		return ret;
	}

	for (int i = 0; i < get_props.count_props; i++) {

		uint64_t values[MAX_PROPS];
		uint64_t enum_blob[MAX_PROPS];
		struct drm_mode_get_property get_prop;
		get_prop.values_ptr = (uint64_t)&values;
		get_prop.enum_blob_ptr = (uint64_t)&enum_blob;
		get_prop.count_values = MAX_PROPS;
		get_prop.count_enum_blobs = MAX_PROPS;
		get_prop.prop_id = properties[i];
		get_prop.flags = 0;
		ret = libc_ioctl(fd, DRM_IOCTL_MODE_GETPROPERTY, (char *)&get_prop);
		if (ret != 0) {
			printf("get_rotation_property_key DRM_IOCTL_MODE_GETPROPERTY failed: %d\n", ret);
			return ret;
		}
		if (strcmp(get_prop.name, "rotation") == 0) {
			return properties[i];
		}
	}

	printf("get_rotation_property_key: no rotation\n");
	return -1;
}

int ioctl(int fd, unsigned long request, char *argp) {
	init();
	if (((request >> (_IOC_TYPESHIFT)) & _IOC_TYPEMASK) == 'd') {
		if (debug_flag && request != 1075602496)
			printf("ioctl %d [%02x] %lu\n", fd, (request >> _IOC_NRSHIFT) & _IOC_NRMASK, request);

		if (request == DRM_IOCTL_MODE_ADDFB) {
			struct drm_mode_fb_cmd *cmd = (struct drm_mode_fb_cmd *)argp;
			printf("addfb [%d] %dx%d %d %d %d %d\n", cmd->fb_id, cmd->width, cmd->height, cmd->pitch, cmd->bpp, cmd->depth, cmd->handle);
		}

		if (request == DRM_IOCTL_MODE_CREATE_DUMB) {
			/*
				Intercept DRM_IOCTL_MODE_CREATE_DUMB and instead call the device-specific
				DRM_IOCTL_OMAP_GEM_NEW ioctl, with arguments set up for a TILER-compatible buffer
				and a fixed width of 8192 (which seems to be required afaics)
			*/
			struct drm_mode_create_dumb *orig = (struct drm_mode_create_dumb *)argp;
			struct drm_omap_gem_new gem_new;

			printf("intercept create_dumb %ux%ux%u\n", orig->width, orig->height, orig->bpp);
			
			int sixteen_bpp = 0;
			if (orig->bpp == 32) {
				sixteen_bpp = 0;
			} else if (orig->bpp == 16) {
				sixteen_bpp = 1;
			} else {
				printf("unsupported bpp %d!\n", orig->bpp);
			}

			int fixed_width = 8192;
			gem_new.size.tiled.width = fixed_width;
			gem_new.size.tiled.height = orig->height;
			gem_new.flags = (sixteen_bpp ? OMAP_BO_TILED_16 : OMAP_BO_TILED_32) | OMAP_BO_WC | OMAP_BO_SCANOUT;
			int result = libc_ioctl(fd, DRM_IOCTL_OMAP_GEM_NEW, (char *) &gem_new);
			orig->handle = gem_new.handle;
			orig->pitch = sixteen_bpp ? (2 * fixed_width) : (4 * fixed_width);
			orig->size = orig->pitch * orig->height;
			printf("   created tiled buffer with handle %u\n", orig->handle);
			/*
				After creating the buffer, set the rotation property on all planes
				This seems to need the atomic API, and so is a bit intricate,
				first needing to set the atomic capability
			*/

			struct drm_set_client_cap atomic_cap;
			atomic_cap.capability = DRM_CLIENT_CAP_ATOMIC;
			atomic_cap.value = 1;
			libc_ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, (char *) &atomic_cap);

			uint32_t planes[16];
			struct drm_mode_get_plane_res plane_res;
			plane_res.count_planes = 16;
			plane_res.plane_id_ptr = (uint64_t)planes;
			libc_ioctl(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, (char *) &plane_res);
			printf("   found %d plane resources\n", plane_res.count_planes);
			for (int i = 0; i < plane_res.count_planes; i++) {
				int plane_id = planes[i];
				printf("        setting rotation for plane %d\n", plane_id);
				struct drm_mode_atomic mode_atomic;
				uint32_t obj[1];
				uint32_t num_props[1];
				uint32_t props[1];
				uint64_t values[1];
				obj[0] = plane_id;
				num_props[0] = 1;
				int rot_prop = get_rotation_property_key(fd, plane_id);
				if (rot_prop < 0)
					continue;
				printf("rotate prop for plane %d: %d\n", plane_id, rot_prop);
				props[0] = rot_prop;
				values[0] = DRM_MODE_ROTATE_270;

				mode_atomic.flags = DRM_MODE_ATOMIC_NONBLOCK;
				mode_atomic.count_objs = 1;
				mode_atomic.objs_ptr = (uint64_t)obj;
				mode_atomic.count_props_ptr = (uint64_t)num_props;
				mode_atomic.props_ptr = (uint64_t)props;
				mode_atomic.prop_values_ptr = (uint64_t)values;
				mode_atomic.reserved = 0;
				mode_atomic.user_data = 0;
				int a_result = libc_ioctl(fd, DRM_IOCTL_MODE_ATOMIC, (char *) &mode_atomic);
				if (a_result != 0)
					printf("rotate set for plane %d failed: %d %d\n", plane_id, a_result, errno);
			}

			return result;
		}

		if (request == DRM_IOCTL_MODE_SETCRTC) {
			/*
				Intercept DRM_IOCTL_MODE_SETCRTC to swap width and height
			*/
			struct drm_mode_crtc *crtc = (struct drm_mode_crtc *) argp;
			printf("mode_setcrtc: %dx%d %d %d\n", crtc->mode.hdisplay, crtc->mode.vdisplay, crtc->mode.htotal, crtc->mode.vtotal);
			int temp = crtc->mode.hdisplay;
			crtc->mode.hdisplay = crtc->mode.vdisplay;
			crtc->mode.vdisplay = temp;
		}

	}
	int result = libc_ioctl(fd, request, argp);

	if (request == DRM_IOCTL_MODE_GETPROPERTY) {
		struct drm_mode_get_property *prop = (struct drm_mode_get_property *) argp;
		printf("get_property %s\n", prop->name);
	}

	if (request == DRM_IOCTL_MODE_OBJ_GETPROPERTIES) {
		/*
			For debugging only
		*/
		struct drm_mode_obj_get_properties *prop = (struct drm_mode_obj_get_properties *) argp;
		printf("mode_obj_get_property: [%d %llu %llu]\n", prop->count_props, prop->props_ptr, prop->prop_values_ptr);
		uint32_t *props = (uint32_t *)prop->props_ptr;
		uint64_t *values = (uint64_t *)prop->prop_values_ptr;
		if (props != NULL && values != NULL) {
			for (int i = 0; i < prop->count_props; i++) {
				printf("       %u: %llu\n", props[i], values[i]);
			}
		}
	}

	if (request == DRM_IOCTL_MODE_GETCRTC) {
		/*
			Intercept DRM_IOCTL_MODE_SETCRTC to swap width and height
		*/
		struct drm_mode_crtc *crtc = (struct drm_mode_crtc *) argp;
		printf("mode_getcrtc: %dx%d %d %d\n", crtc->mode.hdisplay, crtc->mode.vdisplay, crtc->mode.htotal, crtc->mode.vtotal);
		int temp = crtc->mode.hdisplay;
		crtc->mode.hdisplay = crtc->mode.vdisplay;
		crtc->mode.vdisplay = temp;
	}

	return result;
}
