/*
 * Freescale i.MX drm driver
 *
 * Copyright (C) 2011 Sascha Hauer, Pengutronix
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/component.h>
#include <linux/device.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <drm/drmP.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>

#include "imx-drm.h"

#define MAX_CRTC	4

struct imx_drm_crtc;

struct imx_drm_device {
	struct drm_device			*drm;
	struct imx_drm_crtc			*crtc[MAX_CRTC];
	int					pipes;
	struct drm_fbdev_cma			*fbhelper;
};

struct imx_drm_crtc {
	struct drm_crtc				*crtc;
	int					pipe;
	struct imx_drm_crtc_helper_funcs	imx_drm_helper_funcs;
	void					*cookie;
	int					di_id;
	int					ipu_id;
};

static int legacyfb_depth = 16;
module_param(legacyfb_depth, int, 0444);

int imx_drm_crtc_id(struct imx_drm_crtc *crtc)
{
	return crtc->pipe;
}
EXPORT_SYMBOL_GPL(imx_drm_crtc_id);

static void imx_drm_driver_lastclose(struct drm_device *drm)
{
	struct imx_drm_device *imxdrm = drm->dev_private;

#if IS_ENABLED(CONFIG_DRM_IMX_FB_HELPER)
	if (imxdrm->fbhelper)
		drm_fbdev_cma_restore_mode(imxdrm->fbhelper);
#endif
}

static int imx_drm_driver_unload(struct drm_device *drm)
{
	struct imx_drm_device *imxdrm = drm->dev_private;

	drm_kms_helper_poll_fini(drm);

#if IS_ENABLED(CONFIG_DRM_IMX_FB_HELPER)
	if (imxdrm->fbhelper)
		drm_fbdev_cma_fini(imxdrm->fbhelper);
#endif

	component_unbind_all(drm->dev, drm);

	drm_vblank_cleanup(drm);
	drm_mode_config_cleanup(drm);

	return 0;
}

struct imx_drm_crtc *imx_drm_find_crtc(struct drm_crtc *crtc)
{
	struct imx_drm_device *imxdrm = crtc->dev->dev_private;
	unsigned i;

	for (i = 0; i < MAX_CRTC; i++)
		if (imxdrm->crtc[i] && imxdrm->crtc[i]->crtc == crtc)
			return imxdrm->crtc[i];

	return NULL;
}

int imx_drm_panel_format_pins(struct drm_encoder *encoder,
		u32 interface_pix_fmt, int hsync_pin, int vsync_pin)
{
	struct imx_drm_crtc_helper_funcs *helper;
	struct imx_drm_crtc *imx_crtc;

	imx_crtc = imx_drm_find_crtc(encoder->crtc);
	if (!imx_crtc)
		return -EINVAL;

	helper = &imx_crtc->imx_drm_helper_funcs;
	if (helper->set_interface_pix_fmt)
		return helper->set_interface_pix_fmt(encoder->crtc,
				encoder->encoder_type, interface_pix_fmt,
				hsync_pin, vsync_pin);
	return 0;
}
EXPORT_SYMBOL_GPL(imx_drm_panel_format_pins);

int imx_drm_panel_format(struct drm_encoder *encoder, u32 interface_pix_fmt)
{
	return imx_drm_panel_format_pins(encoder, interface_pix_fmt, 2, 3);
}
EXPORT_SYMBOL_GPL(imx_drm_panel_format);

int imx_drm_crtc_vblank_get(struct imx_drm_crtc *imx_drm_crtc)
{
	return drm_vblank_get(imx_drm_crtc->crtc->dev, imx_drm_crtc->pipe);
}
EXPORT_SYMBOL_GPL(imx_drm_crtc_vblank_get);

void imx_drm_crtc_vblank_put(struct imx_drm_crtc *imx_drm_crtc)
{
	drm_vblank_put(imx_drm_crtc->crtc->dev, imx_drm_crtc->pipe);
}
EXPORT_SYMBOL_GPL(imx_drm_crtc_vblank_put);

void imx_drm_handle_vblank(struct imx_drm_crtc *imx_drm_crtc)
{
	drm_handle_vblank(imx_drm_crtc->crtc->dev, imx_drm_crtc->pipe);
}
EXPORT_SYMBOL_GPL(imx_drm_handle_vblank);

static int imx_drm_enable_vblank(struct drm_device *drm, int crtc)
{
	struct imx_drm_device *imxdrm = drm->dev_private;
	struct imx_drm_crtc *imx_drm_crtc = imxdrm->crtc[crtc];
	int ret;

	if (!imx_drm_crtc)
		return -EINVAL;

	if (!imx_drm_crtc->imx_drm_helper_funcs.enable_vblank)
		return -ENOSYS;

	ret = imx_drm_crtc->imx_drm_helper_funcs.enable_vblank(
			imx_drm_crtc->crtc);

	return ret;
}

static void imx_drm_disable_vblank(struct drm_device *drm, int crtc)
{
	struct imx_drm_device *imxdrm = drm->dev_private;
	struct imx_drm_crtc *imx_drm_crtc = imxdrm->crtc[crtc];

	if (!imx_drm_crtc)
		return;

	if (!imx_drm_crtc->imx_drm_helper_funcs.disable_vblank)
		return;

	imx_drm_crtc->imx_drm_helper_funcs.disable_vblank(imx_drm_crtc->crtc);
}

static void imx_drm_driver_preclose(struct drm_device *drm,
		struct drm_file *file)
{
	int i;

	if (!file->is_master)
		return;

	for (i = 0; i < MAX_CRTC; i++)
		imx_drm_disable_vblank(drm, i);
}

static const struct file_operations imx_drm_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = drm_gem_cma_mmap,
	.poll = drm_poll,
	.read = drm_read,
	.llseek = noop_llseek,
};

int imx_drm_connector_mode_valid(struct drm_connector *connector,
	struct drm_display_mode *mode)
{
	return MODE_OK;
}
EXPORT_SYMBOL(imx_drm_connector_mode_valid);

void imx_drm_connector_destroy(struct drm_connector *connector)
{
	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
}
EXPORT_SYMBOL_GPL(imx_drm_connector_destroy);

void imx_drm_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}
EXPORT_SYMBOL_GPL(imx_drm_encoder_destroy);

static void imx_drm_output_poll_changed(struct drm_device *drm)
{
	struct imx_drm_device *imxdrm = drm->dev_private;

#if IS_ENABLED(CONFIG_DRM_IMX_FB_HELPER)
	if (imxdrm->fbhelper)
		drm_fbdev_cma_hotplug_event(imxdrm->fbhelper);
#endif
}

static struct drm_mode_config_funcs imx_drm_mode_config_funcs = {
	.fb_create = drm_fb_cma_create,
	.output_poll_changed = imx_drm_output_poll_changed,
};

/*
 * Main DRM initialisation. This binds, initialises and registers
 * with DRM the subcomponents of the driver.
 */
static int imx_drm_driver_load(struct drm_device *drm, unsigned long flags)
{
	struct imx_drm_device *imxdrm;
	struct drm_connector *connector;
	int ret;

	imxdrm = devm_kzalloc(drm->dev, sizeof(*imxdrm), GFP_KERNEL);
	if (!imxdrm)
		return -ENOMEM;

	imxdrm->drm = drm;

	drm->dev_private = imxdrm;

	/*
	 * enable drm irq mode.
	 * - with irq_enabled = true, we can use the vblank feature.
	 *
	 * P.S. note that we wouldn't use drm irq handler but
	 *      just specific driver own one instead because
	 *      drm framework supports only one irq handler and
	 *      drivers can well take care of their interrupts
	 */
	drm->irq_enabled = true;

	/*
	 * set max width and height as default value(4096x4096).
	 * this value would be used to check framebuffer size limitation
	 * at drm_mode_addfb().
	 */
	drm->mode_config.min_width = 64;
	drm->mode_config.min_height = 64;
	drm->mode_config.max_width = 4096;
	drm->mode_config.max_height = 4096;
	drm->mode_config.funcs = &imx_drm_mode_config_funcs;

	drm_mode_config_init(drm);

	ret = drm_vblank_init(drm, MAX_CRTC);
	if (ret)
		goto err_kms;

	/*
	 * with vblank_disable_allowed = true, vblank interrupt will be
	 * disabled by drm timer once a current process gives up ownership
	 * of vblank event. (after drm_vblank_put function is called)
	 */
	drm->vblank_disable_allowed = true;

	/* Now try and bind all our sub-components */
	ret = component_bind_all(drm->dev, drm);
	if (ret)
		goto err_vblank;

	/*
	 * All components are now added, we can publish the connector sysfs
	 * entries to userspace.  This will generate hotplug events and so
	 * userspace will expect to be able to access DRM at this point.
	 */
	list_for_each_entry(connector, &drm->mode_config.connector_list, head) {
		ret = drm_sysfs_connector_add(connector);
		if (ret) {
			dev_err(drm->dev,
				"[CONNECTOR:%d:%s] drm_sysfs_connector_add failed: %d\n",
				connector->base.id,
				drm_get_connector_name(connector), ret);
			goto err_unbind;
		}
	}

	/*
	 * All components are now initialised, so setup the fb helper.
	 * The fb helper takes copies of key hardware information, so the
	 * crtcs/connectors/encoders must not change after this point.
	 */
#if IS_ENABLED(CONFIG_DRM_IMX_FB_HELPER)
	if (legacyfb_depth != 16 && legacyfb_depth != 32) {
		dev_warn(drm->dev, "Invalid legacyfb_depth.  Defaulting to 16bpp\n");
		legacyfb_depth = 16;
	}
	imxdrm->fbhelper = drm_fbdev_cma_init(drm, legacyfb_depth,
				drm->mode_config.num_crtc, 4);
	if (IS_ERR(imxdrm->fbhelper)) {
		ret = PTR_ERR(imxdrm->fbhelper);
		imxdrm->fbhelper = NULL;
		goto err_unbind;
	}
#endif

	drm_kms_helper_poll_init(drm);

	return 0;

err_unbind:
	component_unbind_all(drm->dev, drm);
err_vblank:
	drm_vblank_cleanup(drm);
err_kms:
	drm_mode_config_cleanup(drm);

	return ret;
}

/*
 * imx_drm_add_crtc - add a new crtc
 *
 * The return value if !NULL is a cookie for the caller to pass to
 * imx_drm_remove_crtc later.
 */
int imx_drm_add_crtc(struct drm_device *drm, struct drm_crtc *crtc,
		struct imx_drm_crtc **new_crtc,
		const struct imx_drm_crtc_helper_funcs *imx_drm_helper_funcs,
		void *cookie, int ipu_id, int di_id)
{
	struct imx_drm_device *imxdrm = drm->dev_private;
	struct imx_drm_crtc *imx_drm_crtc;
	int ret;

	/*
	 * The vblank arrays are dimensioned by MAX_CRTC - we can't
	 * pass IDs greater than this to those functions.
	 */
	if (imxdrm->pipes >= MAX_CRTC)
		return -EINVAL;

	if (imxdrm->drm->open_count)
		return -EBUSY;

	imx_drm_crtc = kzalloc(sizeof(*imx_drm_crtc), GFP_KERNEL);
	if (!imx_drm_crtc)
		return -ENOMEM;

	imx_drm_crtc->imx_drm_helper_funcs = *imx_drm_helper_funcs;
	imx_drm_crtc->pipe = imxdrm->pipes++;
	imx_drm_crtc->cookie = cookie;
	imx_drm_crtc->di_id = di_id;
	imx_drm_crtc->ipu_id = ipu_id;
	imx_drm_crtc->crtc = crtc;

	imxdrm->crtc[imx_drm_crtc->pipe] = imx_drm_crtc;

	*new_crtc = imx_drm_crtc;

	ret = drm_mode_crtc_set_gamma_size(imx_drm_crtc->crtc, 256);
	if (ret)
		goto err_register;

	drm_crtc_helper_add(crtc,
			imx_drm_crtc->imx_drm_helper_funcs.crtc_helper_funcs);

	drm_crtc_init(drm, crtc,
			imx_drm_crtc->imx_drm_helper_funcs.crtc_funcs);

	return 0;

err_register:
	imxdrm->crtc[imx_drm_crtc->pipe] = NULL;
	kfree(imx_drm_crtc);
	return ret;
}
EXPORT_SYMBOL_GPL(imx_drm_add_crtc);

/*
 * imx_drm_remove_crtc - remove a crtc
 */
int imx_drm_remove_crtc(struct imx_drm_crtc *imx_drm_crtc)
{
	struct imx_drm_device *imxdrm = imx_drm_crtc->crtc->dev->dev_private;

	drm_crtc_cleanup(imx_drm_crtc->crtc);

	imxdrm->crtc[imx_drm_crtc->pipe] = NULL;

	kfree(imx_drm_crtc);

	return 0;
}
EXPORT_SYMBOL_GPL(imx_drm_remove_crtc);

/*
 * Find the DRM CRTC possible mask for the device node cookie/id.
 *
 * The encoder possible masks are defined by their position in the
 * mode_config crtc_list.  This means that CRTCs must not be added
 * or removed once the DRM device has been fully initialised.
 */
static uint32_t imx_drm_find_crtc_mask(struct imx_drm_device *imxdrm,
	void *cookie, int id)
{
	unsigned i;

	for (i = 0; i < MAX_CRTC; i++) {
		struct imx_drm_crtc *imx_drm_crtc = imxdrm->crtc[i];
		if (imx_drm_crtc && imx_drm_crtc->di_id == id &&
		    imx_drm_crtc->cookie == cookie)
			return drm_helper_crtc_possible_mask(imx_drm_crtc->crtc);
	}

	return 0;
}

int imx_drm_encoder_parse_of(struct drm_device *drm,
	struct drm_encoder *encoder, struct device_node *np)
{
	struct imx_drm_device *imxdrm = drm->dev_private;
	uint32_t crtc_mask = 0;
	int i, ret = 0;

	for (i = 0; !ret; i++) {
		struct of_phandle_args args;
		uint32_t mask;
		int id;

		ret = of_parse_phandle_with_args(np, "crtcs", "#crtc-cells", i,
						 &args);
		if (ret == -ENOENT)
			break;
		if (ret < 0)
			return ret;

		id = args.args_count > 0 ? args.args[0] : 0;
		mask = imx_drm_find_crtc_mask(imxdrm, args.np, id);
		of_node_put(args.np);

		/*
		 * If we failed to find the CRTC(s) which this encoder is
		 * supposed to be connected to, it's because the CRTC has
		 * not been registered yet.  Defer probing, and hope that
		 * the required CRTC is added later.
		 */
		if (mask == 0)
			return -EPROBE_DEFER;

		crtc_mask |= mask;
	}

	encoder->possible_crtcs = crtc_mask;

	/* FIXME: this is the mask of outputs which can clone this output. */
	encoder->possible_clones = ~0;

	return 0;
}
EXPORT_SYMBOL_GPL(imx_drm_encoder_parse_of);

int imx_drm_encoder_get_mux_id(struct drm_encoder *encoder)
{
	struct imx_drm_crtc *imx_crtc = imx_drm_find_crtc(encoder->crtc);

	return imx_crtc ? imx_crtc->ipu_id * 2 + imx_crtc->di_id : -EINVAL;
}
EXPORT_SYMBOL_GPL(imx_drm_encoder_get_mux_id);

static const struct drm_ioctl_desc imx_drm_ioctls[] = {
	/* none so far */
};

static struct drm_driver imx_drm_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_GEM | DRIVER_PRIME,
	.load			= imx_drm_driver_load,
	.unload			= imx_drm_driver_unload,
	.lastclose		= imx_drm_driver_lastclose,
	.preclose		= imx_drm_driver_preclose,
	.gem_free_object	= drm_gem_cma_free_object,
	.gem_vm_ops		= &drm_gem_cma_vm_ops,
	.dumb_create		= drm_gem_cma_dumb_create,
	.dumb_map_offset	= drm_gem_cma_dumb_map_offset,
	.dumb_destroy		= drm_gem_dumb_destroy,

	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_import	= drm_gem_prime_import,
	.gem_prime_export	= drm_gem_prime_export,
	.gem_prime_get_sg_table	= drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap		= drm_gem_cma_prime_vmap,
	.gem_prime_vunmap	= drm_gem_cma_prime_vunmap,
	.gem_prime_mmap		= drm_gem_cma_prime_mmap,
	.get_vblank_counter	= drm_vblank_count,
	.enable_vblank		= imx_drm_enable_vblank,
	.disable_vblank		= imx_drm_disable_vblank,
	.ioctls			= imx_drm_ioctls,
	.num_ioctls		= ARRAY_SIZE(imx_drm_ioctls),
	.fops			= &imx_drm_driver_fops,
	.name			= "imx-drm",
	.desc			= "i.MX DRM graphics",
	.date			= "20120507",
	.major			= 1,
	.minor			= 0,
	.patchlevel		= 0,
};

static int compare_parent_of(struct device *dev, void *data)
{
	struct of_phandle_args *args = data;
	return dev->parent && dev->parent->of_node == args->np;
}

static int compare_of(struct device *dev, void *data)
{
	return dev->of_node == data;
}

static int imx_drm_add_components(struct device *master, struct master *m)
{
	struct device_node *np = master->of_node;
	unsigned i;
	int ret;

	for (i = 0; ; i++) {
		struct of_phandle_args args;

		ret = of_parse_phandle_with_fixed_args(np, "crtcs", 1,
						       i, &args);
		if (ret)
			break;

		ret = component_master_add_child(m, compare_parent_of, &args);
		of_node_put(args.np);

		if (ret)
			return ret;
	}

	for (i = 0; ; i++) {
		struct device_node *node;

		node = of_parse_phandle(np, "connectors", i);
		if (!node)
			break;

		ret = component_master_add_child(m, compare_of, node);
		of_node_put(node);

		if (ret)
			return ret;
	}
	return 0;
}

static int imx_drm_bind(struct device *dev)
{
	return drm_platform_init(&imx_drm_driver, to_platform_device(dev));
}

static void imx_drm_unbind(struct device *dev)
{
	drm_platform_exit(&imx_drm_driver, to_platform_device(dev));
}

static const struct component_master_ops imx_drm_ops = {
	.add_components = imx_drm_add_components,
	.bind = imx_drm_bind,
	.unbind = imx_drm_unbind,
};

static int imx_drm_platform_probe(struct platform_device *pdev)
{
	int ret;

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	return component_master_add(&pdev->dev, &imx_drm_ops);
}

static int imx_drm_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &imx_drm_ops);
	return 0;
}

static const struct of_device_id imx_drm_dt_ids[] = {
	{ .compatible = "fsl,imx-drm", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, imx_drm_dt_ids);

static struct platform_driver imx_drm_pdrv = {
	.probe		= imx_drm_platform_probe,
	.remove		= imx_drm_platform_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "imx-drm",
		.of_match_table = imx_drm_dt_ids,
	},
};
module_platform_driver(imx_drm_pdrv);

MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_DESCRIPTION("i.MX drm driver core");
MODULE_LICENSE("GPL");
