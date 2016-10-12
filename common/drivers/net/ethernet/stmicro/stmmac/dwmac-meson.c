/*Amlogic Meson DWMAC glue layer
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/device.h>
#include <linux/ethtool.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/delay.h>
#include <linux/of_device.h>
#include <linux/gpio/consumer.h>
#include <linux/stmmac.h>
#include "stmmac.h"
#define ETHMAC_SPEED_100	BIT(1)

struct meson_dwmac {
	struct device *dev;
	void __iomem *reg;
};

static void meson_dwmac_fix_mac_speed(void *priv, unsigned int speed)
{
}
static void __iomem *network_interface_setup(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct gpio_desc *gdesc;
	struct pinctrl *pin_ctl;
	struct resource *res;
	u32 mc_val;
	void __iomem *addr = NULL;

	/*map reg0 and reg 1 addr.*/
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	addr = devm_ioremap_resource(dev, res);

	PREG_ETH_REG0 = addr;
	PREG_ETH_REG1 = addr+4;
	pr_debug("REG0:REG1 = %p :%p\n", PREG_ETH_REG0, PREG_ETH_REG1);

	/* Get mec mode & ting value  set it in cbus2050 */
	pr_debug("mem start:0x%llx , %llx , [%p]\n", (long long)res->start,
					(long long)(res->end - res->start),
					addr);
	if (of_property_read_u32(np, "mc_val", &mc_val)) {
		pr_debug("detect cbus[2050]=null, plesae setting val\n");
		pr_debug(" IF RGMII setting 0x7d21 else rmii setting 0x1000");
	} else {
		pr_debug("Ethernet :got mc_val 0x%x .set it\n", mc_val);
			writel(mc_val, addr);
	}

	pin_ctl = devm_pinctrl_get_select(&pdev->dev, "eth_pins");
	pr_debug("Ethernet: pinmux setup ok\n");
	/* reset pin choose pull high 100ms than pull low */
	gdesc = gpiod_get(&pdev->dev, "rst_pin");
	/*
	if (!IS_ERR(gdesc)) {
		gpiod_direction_output(gdesc, 0);
		mdelay(10);
		gpiod_direction_output(gdesc, 1);
		mdelay(30);
	}
	*/
	pr_debug("Ethernet: gpio reset ok\n");
	return addr;
}

static void *meson_dwmac_setup(struct platform_device *pdev)
{
	struct meson_dwmac *dwmac;

	dwmac = devm_kzalloc(&pdev->dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac)
		return ERR_PTR(-ENOMEM);

	dwmac->reg = network_interface_setup(pdev);
	if (IS_ERR(dwmac->reg))
		return dwmac->reg;

	return dwmac;
}

const struct stmmac_of_data meson_dwmac_data = {
	.setup = meson_dwmac_setup,
	.fix_mac_speed = meson_dwmac_fix_mac_speed,
};
