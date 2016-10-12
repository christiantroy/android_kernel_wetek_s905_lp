/*
 * drivers/amlogic/ion_dev/dev_ion.c
 *
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
*/


#include <linux/err.h>
#include <android/ion/ion.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <android/ion/ion_priv.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_reserved_mem.h>

MODULE_DESCRIPTION("AMLOGIC ION driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Amlogic SH");

static unsigned debug = 1;
module_param(debug, uint, 0644);
MODULE_PARM_DESC(debug, "activates debug info");

#define dprintk(level, fmt, arg...)             \
	do {                                        \
		if (debug >= level)                     \
			pr_debug("ion-dev: " fmt, ## arg);  \
	} while (0)

/*
 * TODO instead with enum ion_heap_type from ion.h
 */
#define AML_ION_TYPE_SYSTEM	 0
#define AML_ION_TYPE_CARVEOUT   1
#define MAX_HEAP 4

static struct ion_device *idev;
static int num_heaps;
static struct ion_heap **heaps;
static struct ion_platform_heap my_ion_heap[MAX_HEAP];

int dev_ion_probe(struct platform_device *pdev)
{
	int err = 0;

	my_ion_heap[num_heaps].type = ION_HEAP_TYPE_SYSTEM;
	my_ion_heap[num_heaps].id = ION_HEAP_TYPE_SYSTEM;
	my_ion_heap[num_heaps].name = "vmalloc_ion";
	num_heaps++;

	/* init reserved memory */
	err = of_reserved_mem_device_init(&pdev->dev);
	if (err != 0)
		dprintk(1, "failed get reserved memory\n");

	return 0;
}

int dev_ion_remove(struct platform_device *pdev)
{
	struct ion_device *idev = platform_get_drvdata(pdev);
	int i;

	ion_device_destroy(idev);
	for (i = 0; i < num_heaps; i++)
		ion_heap_destroy(heaps[i]);
	kfree(heaps);
	return 0;
}

static const struct of_device_id amlogic_ion_dev_dt_match[] = {
	{ .compatible = "amlogic, ion_dev", },
	{ },
};

static struct platform_driver ion_driver = {
	.probe = dev_ion_probe,
	.remove = dev_ion_remove,
	.driver = {
		.name = "ion_dev",
		.owner = THIS_MODULE,
		.of_match_table = amlogic_ion_dev_dt_match
	}
};

/*
 * reserved memory initialize begin
 */
static int ion_dev_mem_init(struct reserved_mem *rmem, struct device *dev)
{
	int i = 0;
	int err;
	struct platform_device *pdev = to_platform_device(dev);
	my_ion_heap[num_heaps].type = ION_HEAP_TYPE_CARVEOUT;
	my_ion_heap[num_heaps].id = ION_HEAP_TYPE_CARVEOUT;
	my_ion_heap[num_heaps].name = "carveout_ion";
	my_ion_heap[num_heaps].base = (ion_phys_addr_t) rmem->base;
	my_ion_heap[num_heaps].size = rmem->size;

	num_heaps++;
	heaps = kzalloc(sizeof(struct ion_heap *) * num_heaps, GFP_KERNEL);
	idev = ion_device_create(NULL);
	if (IS_ERR_OR_NULL(idev)) {
		kfree(heaps);
		panic(0);
		return PTR_ERR(idev);
	}

	platform_set_drvdata(pdev, idev);

	/* create the heaps as specified in the board file */
	for (i = 0; i < num_heaps; i++) {
		heaps[i] = ion_heap_create(&my_ion_heap[i]);
		if (IS_ERR_OR_NULL(heaps[i])) {
			err = PTR_ERR(heaps[i]);
			goto failed;
		}
		ion_device_add_heap(idev, heaps[i]);
		dprintk(2, "add heap type:%d id:%d\n",
	    my_ion_heap[i].type, my_ion_heap[i].id);
	}

	dprintk(1, "%s, create %d heaps\n", __func__, num_heaps);

	return 0;

failed:
	dprintk(0, "ion heap create failed\n");
	kfree(heaps);
	heaps = NULL;
	panic(0);
	return err;

}

static const struct reserved_mem_ops rmem_ion_dev_ops = {
	.device_init = ion_dev_mem_init,
};

static struct rmem_multi_user rmem_ion_muser = {
	.of_match_table = amlogic_ion_dev_dt_match,
	.ops  = &rmem_ion_dev_ops,
};

static int __init ion_dev_mem_setup(struct reserved_mem *rmem)
{
	of_add_rmem_multi_user(rmem, &rmem_ion_muser);
	pr_info("ion_dev mem setup\n");

	return 0;
}

RESERVEDMEM_OF_DECLARE(ion_dev_mem, "amlogic, idev-mem", ion_dev_mem_setup);
/*
 * reserved memory initialize end
 */

static int __init ion_init(void)
{
	return platform_driver_register(&ion_driver);
}

static void __exit ion_exit(void)
{
	platform_driver_unregister(&ion_driver);
}

module_init(ion_init);
module_exit(ion_exit);
