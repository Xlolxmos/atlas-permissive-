/**
 * otg.c - DesignWare USB3 DRD Controller OTG
 *
 * Copyright (c) 2012, Code Aurora Forum. All rights reserved.
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Authors: Ido Shayevitz <idos@codeaurora.org>
 *	    Anton Tikhomirov <av.tikhomirov@samsung.com>
 *	    Minho Lee <minho55.lee@samsung.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/usb/samsung_usb.h>
#include <linux/mfd/samsung/s2mps18-private.h>
#include <linux/suspend.h>
#include <soc/samsung/exynos-pm.h>
#if defined(CONFIG_TYPEC_DEFAULT)
#include <linux/usb/typec.h>
#endif

#include "core.h"
#include "otg.h"
#include "io.h"
#ifdef CONFIG_OF
#include <linux/of_device.h>
#endif
#include "../../base/base.h"
#if defined(CONFIG_USB_PORT_POWER_OPTIMIZATION)
#include "usb_power_notify.h"
#endif

#if IS_ENABLED(CONFIG_USB_NOTIFY_LAYER)
#include <linux/usb_notify.h>
#endif
#define OTG_NO_CONNECT		0
#define OTG_CONNECT_ONLY	1
#define OTG_DEVICE_CONNECT	2

struct dwc3 *g_dwc;

/* -------------------------------------------------------------------------- */
#if defined(CONFIG_TYPEC_DEFAULT)
struct intf_typec {
	/* struct mutex lock; */ /* device lock */
	struct device *dev;
	struct typec_port *port;
	struct typec_capability cap;
	struct typec_partner *partner;
};
#endif

int otg_connection;
static int dwc3_otg_statemachine(struct otg_fsm *fsm)
{
	struct usb_otg *otg = fsm->otg;
	enum usb_otg_state prev_state = otg->state;
	int ret = 0;

	if (fsm->reset) {
		if (otg->state == OTG_STATE_A_HOST) {
			otg_drv_vbus(fsm, 0);
			otg_start_host(fsm, 0);
		} else if (otg->state == OTG_STATE_B_PERIPHERAL) {
			otg_start_gadget(fsm, 0);
		}

		otg->state = OTG_STATE_UNDEFINED;
		goto exit;
	}

	switch (otg->state) {
	case OTG_STATE_UNDEFINED:
		if (fsm->id)
			otg->state = OTG_STATE_B_IDLE;
		else
			otg->state = OTG_STATE_A_IDLE;
		break;
	case OTG_STATE_B_IDLE:
		if (!fsm->id) {
			otg->state = OTG_STATE_A_IDLE;
		} else if (fsm->b_sess_vld) {
			ret = otg_start_gadget(fsm, 1);
			if (!ret)
				otg->state = OTG_STATE_B_PERIPHERAL;
			else
				pr_err("OTG SM: cannot start gadget\n");
		}
		break;
	case OTG_STATE_B_PERIPHERAL:
		if (!fsm->id || !fsm->b_sess_vld) {
			ret = otg_start_gadget(fsm, 0);
			if (!ret)
				otg->state = OTG_STATE_B_IDLE;
			else
				pr_err("OTG SM: cannot stop gadget\n");
		}
		break;
	case OTG_STATE_A_IDLE:
		if (fsm->id) {
			otg->state = OTG_STATE_B_IDLE;
		} else {
			ret = otg_start_host(fsm, 1);
			if (!ret) {
				otg_drv_vbus(fsm, 1);
				otg->state = OTG_STATE_A_HOST;
			} else {
				pr_err("OTG SM: cannot start host\n");
			}
		}
		break;
	case OTG_STATE_A_HOST:
		if (fsm->id) {
			otg_drv_vbus(fsm, 0);
			ret = otg_start_host(fsm, 0);
			if (!ret)
				otg->state = OTG_STATE_A_IDLE;
			else
				pr_err("OTG SM: cannot stop host\n");
		}
		break;
	default:
		pr_err("OTG SM: invalid state\n");
	}

exit:
	if (!ret)
		ret = (otg->state != prev_state);

	pr_debug("OTG SM: %s => %s\n", usb_otg_state_string(prev_state),
		(ret > 0) ? usb_otg_state_string(otg->state) : "(no change)");

	return ret;
}

/* -------------------------------------------------------------------------- */

static struct dwc3_ext_otg_ops *dwc3_otg_exynos_rsw_probe(struct dwc3 *dwc)
{
	struct dwc3_ext_otg_ops *ops;
	bool ext_otg;

	ext_otg = dwc3_exynos_rsw_available(dwc->dev->parent);
	if (!ext_otg)
		return NULL;

	/* Allocate and init otg instance */
	ops = devm_kzalloc(dwc->dev, sizeof(struct dwc3_ext_otg_ops),
			GFP_KERNEL);
	if (!ops) {
		dev_err(dwc->dev, "unable to allocate dwc3_ext_otg_ops\n");
		return NULL;
	}

	ops->setup = dwc3_exynos_rsw_setup;
	ops->exit = dwc3_exynos_rsw_exit;
	ops->start = dwc3_exynos_rsw_start;
	ops->stop = dwc3_exynos_rsw_stop;

	return ops;
}

static void dwc3_otg_set_host_mode(struct dwc3_otg *dotg)
{
	struct dwc3 *dwc = dotg->dwc;
	u32 reg;

	if (dotg->regs) {
		reg = dwc3_readl(dotg->regs, DWC3_OCTL);
		reg &= ~DWC3_OTG_OCTL_PERIMODE;
		dwc3_writel(dotg->regs, DWC3_OCTL, reg);
	} else {
		dwc3_set_mode(dwc, DWC3_GCTL_PRTCAP_HOST);
	}
}

static void dwc3_otg_set_peripheral_mode(struct dwc3_otg *dotg)
{
	struct dwc3 *dwc = dotg->dwc;
	u32 reg;

	if (dotg->regs) {
		reg = dwc3_readl(dotg->regs, DWC3_OCTL);
		reg |= DWC3_OTG_OCTL_PERIMODE;
		dwc3_writel(dotg->regs, DWC3_OCTL, reg);
	} else {
		dwc3_set_mode(dwc, DWC3_GCTL_PRTCAP_DEVICE);
	}
}

static void dwc3_otg_drv_vbus(struct otg_fsm *fsm, int on)
{
	struct dwc3_otg	*dotg = container_of(fsm, struct dwc3_otg, fsm);
	int ret;

	if (IS_ERR(dotg->vbus_reg)) {
		dev_err(dotg->dwc->dev, "vbus regulator is not available\n");
		return;
	}

	if (on)
		ret = regulator_enable(dotg->vbus_reg);
	else
		ret = regulator_disable(dotg->vbus_reg);

	if (ret)
		dev_err(dotg->dwc->dev, "failed to turn Vbus %s\n",
						on ? "on" : "off");
}

int exynos_usbdrd_inform_dp_use(int use, int lane_cnt)
{
	int ret = 0;

	if ((use == 1) && (lane_cnt == 4)) {
		ret = xhci_portsc_set(0);
		udelay(1);
	}
	return ret;
}

void exynos_usbdrd_request_phy_isol(void)
{

}

static void dwc3_int_lock_qos_work(struct work_struct *data)
{
	struct dwc3 *dwc = container_of(data, struct dwc3, int_qos_work);
	struct dwc3_otg	*dotg = dwc->dotg;
	struct device *dev = dotg->dwc->dev;

	/* TODO : set exact lock value from DT */
	if (dotg->pm_qos_int_val) {
		if (dwc->level_val < 0) {
			dev_info(dev, "Unlock QOS...(USB speed <= High speed)");
			pm_qos_update_request(&dotg->pm_qos_int_req, 0);
		} else if (dwc->level_val == 2) {
			dev_info(dev, "lock QOS...(Super speed : Lock 266Mhz)");
			pm_qos_update_request(&dotg->pm_qos_int_req, 266000);
		} else if (dwc->level_val == 1) {
			dev_info(dev, "lock QOS...(Super speed : Lock 400Mhz)");
			/* pm_qos_update_request(&dotg->pm_qos_int_req, 400000); */
		} else if (dwc->level_val == 0) {
			dev_info(dev, "lock QOS...(Super speed PLUS : Lock 400Mhz)");
			/* pm_qos_update_request(&dotg->pm_qos_int_req, 533000); */
		} else {
			dev_err(dev, "Unsupported QOS lock level!!!!");
		}
	}
}

void dwc3_otg_qos_lock(struct dwc3 *dwc, int level)
{
	dwc->level_val = level;
	queue_work(dwc->int_qos_lock_wq, &dwc->int_qos_work);
}

/*
    owner 0 - USB
    owner 1 - DP
*/
int dwc3_otg_phy_enable(struct otg_fsm *fsm, int owner, bool on)
{
	struct usb_otg	*otg = fsm->otg;
	struct dwc3_otg	*dotg = container_of(otg, struct dwc3_otg, otg);
	struct dwc3	*dwc = dotg->dwc;
	struct device	*dev = dotg->dwc->dev;
	int ret = 0;
	u8 owner_bit = 0;

	mutex_lock(&dotg->lock);

	dev_info(dev, "%s phy control=%d owner=%d (usb:0 dp:1) on=%d\n",
			__func__, dotg->combo_phy_control, owner, on);

	if (owner > 1)
		goto out;

	owner_bit = (1 << owner);

	if (on) {
		if (dotg->combo_phy_control) {
			dotg->combo_phy_control |= owner_bit;
			goto out;
		} else {
			phy_conn(dwc->usb2_generic_phy, 1);

			if (dotg->pm_qos_int_val)
				pm_qos_update_request(&dotg->pm_qos_int_req,
						dotg->pm_qos_int_val);
			pm_runtime_get_sync(dev);
			ret = dwc3_core_init(dwc);
			if (ret) {
				dev_err(dwc->dev, "%s: failed to reinitialize core\n",
						__func__);
				goto err;
			}
			dotg->combo_phy_control |= owner_bit;
		}
	} else {
		dotg->combo_phy_control &= ~(owner_bit);

		if (dotg->combo_phy_control == 0) {
			dwc3_core_exit(dwc);
err:
			pm_runtime_put_sync_suspend(dev);
			if (dotg->pm_qos_int_val)
				pm_qos_update_request(&dotg->pm_qos_int_req, 0);
			phy_conn(dwc->usb2_generic_phy, 0);
			/*
			 * Need to check why phy init_count has mismatch.
			 * Phy_init is called from usb_phy_roothub_init() in
			 * usb_add_hcd function(usb/core/hcd.c)
			 */
			dwc->usb2_generic_phy->init_count = 0;
			dwc->usb3_generic_phy->init_count = 0;
		}
	}
out:
	mutex_unlock(&dotg->lock);
	return ret;
}
int list_clear = 0;
static int dwc3_otg_start_host(struct otg_fsm *fsm, int on)
{
	struct usb_otg	*otg = fsm->otg;
	struct dwc3_otg	*dotg = container_of(otg, struct dwc3_otg, otg);
	struct dwc3	*dwc = dotg->dwc;
	struct device	*dev = dotg->dwc->dev;
	int ret = 0;
	int ret1 = -1;

	if (!dotg->dwc->xhci) {
		dev_err(dev, "%s: does not have any xhci\n", __func__);
		return -EINVAL;
	}

	dev_info(dev, "Turn %s host\n", on ? "on" : "off");
	if (on) {
		otg_connection = 1;
		ret = dwc3_otg_phy_enable(fsm, 0, on);
		if (ret) {
			dev_err(dwc->dev, "%s: failed to reinitialize core\n",
					__func__);
			goto err1;
		}

		/**
		 * In case there is not a resistance to detect VBUS,
		 * DP/DM controls by S/W are needed at this point.
		 */
		if (dwc->is_not_vbus_pad) {
			phy_set(dwc->usb2_generic_phy,
					SET_DPDM_PULLDOWN, NULL);
			phy_set(dwc->usb3_generic_phy,
					SET_DPDM_PULLDOWN, NULL);
		}

		dwc3_otg_set_host_mode(dotg);
		if (list_clear == 1)
			INIT_LIST_HEAD(&dwc->xhci->dev.links.needs_suppliers);
		ret = platform_device_add(dwc->xhci);
		if (ret) {
			dev_err(dev, "%s: cannot add xhci\n", __func__);
			goto err2;
		}
#if defined(CONFIG_USB_PORT_POWER_OPTIMIZATION)
		register_usb_power_notify();
#endif
#if defined(CONFIG_IF_CB_MANAGER)
		usbpd_set_host_on(dotg->man, on);
#endif
	} else {
		otg_connection = 0;
#if defined(CONFIG_IF_CB_MANAGER)
		usbpd_set_host_on(dotg->man, on);
#endif
#if defined(CONFIG_USB_PORT_POWER_OPTIMIZATION)
		xhci_port_power_set(1, 3);
		unregister_usb_power_notify();
#endif
		if (dotg->dwc3_suspended) {
			pr_info("%s: wait resume completion\n", __func__);
			ret1 = wait_for_completion_timeout(&dotg->resume_cmpl,
							msecs_to_jiffies(5000));
		}

		platform_device_del(dwc->xhci);
		dwc->xhci->dev.p->dead = 0;
		list_clear = 1;

err2:
		ret = dwc3_otg_phy_enable(fsm, 0, on);
	}
err1:
	return ret;
}

static int dwc3_otg_start_gadget(struct otg_fsm *fsm, int on)
{
	struct usb_otg	*otg = fsm->otg;
	struct dwc3_otg	*dotg = container_of(otg, struct dwc3_otg, otg);
	struct dwc3	*dwc = dotg->dwc;
	struct device	*dev = dotg->dwc->dev;
	int ret = 0;

	if (!otg->gadget) {
		dev_err(dev, "%s does not have any gadget\n", __func__);
		return -EINVAL;
	}

	dev_info(dev, "Turn %s gadget %s\n",
			on ? "on" : "off", otg->gadget->name);

	if (on) {
		wake_lock(&dotg->wakelock);
		ret = dwc3_otg_phy_enable(fsm, 0, on);
		if (ret) {
			dev_err(dwc->dev, "%s: failed to reinitialize core\n",
					__func__);
			goto err1;
		}

		dwc3_otg_set_peripheral_mode(dotg);
		ret = usb_gadget_vbus_connect(otg->gadget);
		if (ret) {
			dev_err(dwc->dev, "%s: vbus connect failed\n",
					__func__);
			goto err2;
		}

	} else {
		if (dwc->is_not_vbus_pad)
			dwc3_gadget_disconnect_proc(dwc);
		/* avoid missing disconnect interrupt */
		ret = wait_for_completion_timeout(&dwc->disconnect,
				msecs_to_jiffies(200));
		if (!ret) {
			dev_err(dwc->dev, "%s: disconnect completion timeout\n",
					__func__);
			return ret;
		}
		ret = usb_gadget_vbus_disconnect(otg->gadget);
		if (ret)
			dev_err(dwc->dev, "%s: vbus disconnect failed\n",
					__func__);
err2:
		ret = dwc3_otg_phy_enable(fsm, 0, on);
err1:
		wake_unlock(&dotg->wakelock);
	}

	return ret;
}

static struct otg_fsm_ops dwc3_otg_fsm_ops = {
	.drv_vbus	= dwc3_otg_drv_vbus,
	.start_host	= dwc3_otg_start_host,
	.start_gadget	= dwc3_otg_start_gadget,
};

/* -------------------------------------------------------------------------- */

void dwc3_otg_run_sm(struct otg_fsm *fsm)
{
	struct dwc3_otg	*dotg = container_of(fsm, struct dwc3_otg, fsm);
	int		state_changed;

	/* Prevent running SM on early system resume */
	if (!dotg->ready)
		return;

	mutex_lock(&fsm->lock);

	do {
		state_changed = dwc3_otg_statemachine(fsm);
	} while (state_changed > 0);

	mutex_unlock(&fsm->lock);
}

/* Bind/Unbind the peripheral controller driver */
static int dwc3_otg_set_peripheral(struct usb_otg *otg,
				struct usb_gadget *gadget)
{
	struct dwc3_otg	*dotg = container_of(otg, struct dwc3_otg, otg);
	struct otg_fsm	*fsm = &dotg->fsm;
	struct device	*dev = dotg->dwc->dev;

	if (gadget) {
		dev_info(dev, "Binding gadget %s\n", gadget->name);

		otg->gadget = gadget;
	} else {
		dev_info(dev, "Unbinding gadget\n");

		mutex_lock(&fsm->lock);

		if (otg->state == OTG_STATE_B_PERIPHERAL) {
			/* Reset OTG Statemachine */
			fsm->reset = 1;
			dwc3_otg_statemachine(fsm);
			fsm->reset = 0;
		}
		otg->gadget = NULL;

		mutex_unlock(&fsm->lock);

		dwc3_otg_run_sm(fsm);
	}

	return 0;
}

static int dwc3_otg_get_id_state(struct dwc3_otg *dotg)
{
	u32 reg = dwc3_readl(dotg->regs, DWC3_OSTS);

	return !!(reg & DWC3_OTG_OSTS_CONIDSTS);
}

static int dwc3_otg_get_b_sess_state(struct dwc3_otg *dotg)
{
	u32 reg = dwc3_readl(dotg->regs, DWC3_OSTS);

	return !!(reg & DWC3_OTG_OSTS_BSESVALID);
}

static irqreturn_t dwc3_otg_interrupt(int irq, void *_dotg)
{
	struct dwc3_otg	*dotg = (struct dwc3_otg *)_dotg;
	struct otg_fsm	*fsm = &dotg->fsm;
	u32 oevt, handled_events = 0;
	irqreturn_t ret = IRQ_NONE;

	oevt = dwc3_readl(dotg->regs, DWC3_OEVT);

	/* ID */
	if (oevt & DWC3_OEVTEN_OTGCONIDSTSCHNGEVNT) {
		fsm->id = dwc3_otg_get_id_state(dotg);
		handled_events |= DWC3_OEVTEN_OTGCONIDSTSCHNGEVNT;
	}

	/* VBus */
	if (oevt & DWC3_OEVTEN_OTGBDEVVBUSCHNGEVNT) {
		fsm->b_sess_vld = dwc3_otg_get_b_sess_state(dotg);
		handled_events |= DWC3_OEVTEN_OTGBDEVVBUSCHNGEVNT;
	}

	if (handled_events) {
		dwc3_writel(dotg->regs, DWC3_OEVT, handled_events);
		ret = IRQ_WAKE_THREAD;
	}

	return ret;
}

static irqreturn_t dwc3_otg_thread_interrupt(int irq, void *_dotg)
{
	struct dwc3_otg	*dotg = (struct dwc3_otg *)_dotg;

	dwc3_otg_run_sm(&dotg->fsm);

	return IRQ_HANDLED;
}

static void dwc3_otg_enable_irq(struct dwc3_otg *dotg)
{
	/* Enable only connector ID status & VBUS change events */
	dwc3_writel(dotg->regs, DWC3_OEVTEN,
			DWC3_OEVTEN_OTGCONIDSTSCHNGEVNT |
			DWC3_OEVTEN_OTGBDEVVBUSCHNGEVNT);
}

static void dwc3_otg_disable_irq(struct dwc3_otg *dotg)
{
	dwc3_writel(dotg->regs, DWC3_OEVTEN, 0x0);
}

static void dwc3_otg_reset(struct dwc3_otg *dotg)
{
	/*
	 * OCFG[2] - OTG-Version = 0
	 * OCFG[1] - HNPCap = 0
	 * OCFG[0] - SRPCap = 0
	 */
	dwc3_writel(dotg->regs, DWC3_OCFG, 0x0);

	/*
	 * OCTL[6] - PeriMode = 1
	 * OCTL[5] - PrtPwrCtl = 0
	 * OCTL[4] - HNPReq = 0
	 * OCTL[3] - SesReq = 0
	 * OCTL[2] - TermSelDLPulse = 0
	 * OCTL[1] - DevSetHNPEn = 0
	 * OCTL[0] - HstSetHNPEn = 0
	 */
	dwc3_writel(dotg->regs, DWC3_OCTL, DWC3_OTG_OCTL_PERIMODE);

	/* Clear all otg events (interrupts) indications  */
	dwc3_writel(dotg->regs, DWC3_OEVT, DWC3_OEVT_CLEAR_ALL);
}

/* -------------------------------------------------------------------------- */

static ssize_t
dwc3_otg_show_state(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dwc3	*dwc = dev_get_drvdata(dev);
	struct usb_otg	*otg = &dwc->dotg->otg;

	return snprintf(buf, PAGE_SIZE, "%s\n",
			usb_otg_state_string(otg->state));
}

static DEVICE_ATTR(state, S_IRUSR | S_IRGRP,
	dwc3_otg_show_state, NULL);

static ssize_t
dwc3_otg_show_b_sess(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dwc3	*dwc = dev_get_drvdata(dev);
	struct otg_fsm	*fsm = &dwc->dotg->fsm;

	return snprintf(buf, PAGE_SIZE, "%d\n", fsm->b_sess_vld);
}

static ssize_t
dwc3_otg_store_b_sess(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t n)
{
	struct dwc3	*dwc = dev_get_drvdata(dev);
	struct otg_fsm	*fsm = &dwc->dotg->fsm;
	int		b_sess_vld;


	if (sscanf(buf, "%d", &b_sess_vld) != 1){
		return -EINVAL;
	}
#if IS_ENABLED(CONFIG_USB_NOTIFY_LAYER)
	if (is_blocked(get_otg_notify(), NOTIFY_BLOCK_TYPE_CLIENT))
		return NOTIFY_OK;
#endif

	fsm->b_sess_vld = !!b_sess_vld;

	dwc3_otg_run_sm(fsm);
	return n;
}

static DEVICE_ATTR(b_sess, S_IWUSR | S_IRUSR | S_IRGRP,
	dwc3_otg_show_b_sess, dwc3_otg_store_b_sess);

static ssize_t
dwc3_otg_show_id(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dwc3	*dwc = dev_get_drvdata(dev);
	struct otg_fsm	*fsm = &dwc->dotg->fsm;

	return snprintf(buf, PAGE_SIZE, "%d\n", fsm->id);
}

static ssize_t
dwc3_otg_store_id(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t n)
{
	struct dwc3	*dwc = dev_get_drvdata(dev);
	struct otg_fsm	*fsm = &dwc->dotg->fsm;
	int id;

	if (sscanf(buf, "%d", &id) != 1)
		return -EINVAL;

#if IS_ENABLED(CONFIG_USB_NOTIFY_LAYER)
	if (is_blocked(get_otg_notify(), NOTIFY_BLOCK_TYPE_HOST))
		return NOTIFY_OK;
#endif

	fsm->id = !!id;

	dwc3_otg_run_sm(fsm);

	return n;
}

static DEVICE_ATTR(id, S_IWUSR | S_IRUSR | S_IRGRP,
	dwc3_otg_show_id, dwc3_otg_store_id);

static struct attribute *dwc3_otg_attributes[] = {
	&dev_attr_id.attr,
	&dev_attr_b_sess.attr,
	&dev_attr_state.attr,
	NULL
};

static const struct attribute_group dwc3_otg_attr_group = {
	.attrs = dwc3_otg_attributes,
};

/**
 * dwc3_otg_start
 * @dwc: pointer to our controller context structure
 */
int dwc3_otg_start(struct dwc3 *dwc)
{
	struct dwc3_otg	*dotg = dwc->dotg;
	struct otg_fsm	*fsm = &dotg->fsm;
	int		ret;

	if (dotg->ext_otg_ops) {
		ret = dwc3_ext_otg_start(dotg);
		if (ret) {
			dev_err(dwc->dev, "failed to start external OTG\n");
			return ret;
		}
	} else {
		dotg->regs = dwc->regs;

		dwc3_otg_reset(dotg);

		dotg->fsm.id = dwc3_otg_get_id_state(dotg);
		dotg->fsm.b_sess_vld = dwc3_otg_get_b_sess_state(dotg);

		dotg->irq = platform_get_irq(to_platform_device(dwc->dev), 0);
		ret = devm_request_threaded_irq(dwc->dev, dotg->irq,
				dwc3_otg_interrupt,
				dwc3_otg_thread_interrupt,
				IRQF_SHARED, "dwc3-otg", dotg);
		if (ret) {
			dev_err(dwc->dev, "failed to request irq #%d --> %d\n",
					dotg->irq, ret);
			return ret;
		}

		dwc3_otg_enable_irq(dotg);
	}

	dotg->ready = 1;

	dwc3_otg_run_sm(fsm);

	return 0;
}

/**
 * dwc3_otg_stop
 * @dwc: pointer to our controller context structure
 */
void dwc3_otg_stop(struct dwc3 *dwc)
{
	struct dwc3_otg         *dotg = dwc->dotg;

	if (dotg->ext_otg_ops) {
		dwc3_ext_otg_stop(dotg);
	} else {
		dwc3_otg_disable_irq(dotg);
		free_irq(dotg->irq, dotg);
	}

	dotg->ready = 0;
}

/* -------------------------------------------------------------------------- */
u32 otg_is_connect(void)
{
	if (!otg_connection)
		return OTG_NO_CONNECT;
#if defined(CONFIG_USB_PORT_POWER_OPTIMIZATION)
	else if (is_otg_only)
		return OTG_CONNECT_ONLY;
#endif
	else
		return OTG_DEVICE_CONNECT;
}
EXPORT_SYMBOL_GPL(otg_is_connect);

u32 get_speed_and_disu1u2(void)
{
	u32 reg;
	struct dwc3 *dwc = g_dwc;

	/* Disable U1/U2 */
	reg = dwc3_readl(dwc->regs, DWC3_DCTL);
	reg &= ~(DWC3_DCTL_INITU1ENA | DWC3_DCTL_ACCEPTU1ENA |
			DWC3_DCTL_INITU2ENA | DWC3_DCTL_ACCEPTU2ENA);
	dwc3_writel(dwc->regs, DWC3_DCTL, reg);

	return dwc->level_val;
}
EXPORT_SYMBOL_GPL(get_speed_and_disu1u2);

static int dwc3_otg_pm_notifier(struct notifier_block *nb,
		unsigned long action, void *nb_data)
{
	struct dwc3_otg *dotg
		= container_of(nb, struct dwc3_otg, pm_nb);

	switch (action) {
	case PM_SUSPEND_PREPARE:
		pr_info("%s suspend prepare\n", __func__);
		dotg->dwc3_suspended = 1;
		reinit_completion(&dotg->resume_cmpl);
		break;
	case PM_POST_SUSPEND:
		pr_info("%s post suspend\n", __func__);
		dotg->dwc3_suspended = 0;
		complete(&dotg->resume_cmpl);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

int dwc3_otg_init(struct dwc3 *dwc)
{
	struct dwc3_otg *dotg;
	struct dwc3_ext_otg_ops *ops = NULL;
	int ret = 0;
#if defined(CONFIG_TYPEC_DEFAULT)
	struct intf_typec	*typec;
	struct typec_partner_desc partner;
#endif
#if defined(CONFIG_IF_CB_MANAGER)
	struct usb_dev *usb_d;
#endif

	dev_info(dwc->dev, "%s\n", __func__);

	/* EXYNOS SoCs don't have HW OTG, but support SW OTG. */
	ops = dwc3_otg_exynos_rsw_probe(dwc);
	if (!ops)
		return 0;

	g_dwc = dwc;

	/* Allocate and init otg instance */
	dotg = devm_kzalloc(dwc->dev, sizeof(struct dwc3_otg), GFP_KERNEL);
	if (!dotg) {
		dev_err(dwc->dev, "unable to allocate dwc3_otg\n");
		return -ENOMEM;
	}

	/* This reference is used by dwc3 modules for checking otg existance */
	dwc->dotg = dotg;
	dotg->dwc = dwc;

	ret = of_property_read_u32(dwc->dev->of_node,"ldos", &dotg->ldos);
	if (ret < 0) {
		dev_err(dwc->dev, "can't get ldo information\n");
		return -EINVAL;
	}

 	ret = of_property_read_u32(dwc->dev->of_node,
				"usb-pm-qos-int", &dotg->pm_qos_int_val);
	if (ret < 0) {
		dev_err(dwc->dev, "couldn't read usb-pm-qos-int %s node, error = %d\n",
					dwc->dev->of_node->name, ret);
		dotg->pm_qos_int_val = 0;
	} else {
		pm_qos_add_request(&dotg->pm_qos_int_req,
					PM_QOS_DEVICE_THROUGHPUT, 0);
	}

	dotg->ext_otg_ops = ops;

	dotg->otg.set_peripheral = dwc3_otg_set_peripheral;
	dotg->otg.set_host = NULL;

	dotg->otg.state = OTG_STATE_UNDEFINED;

	mutex_init(&dotg->fsm.lock);
	dotg->fsm.ops = &dwc3_otg_fsm_ops;
	dotg->fsm.otg = &dotg->otg;

	dotg->vbus_reg = devm_regulator_get(dwc->dev->parent, "dwc3-vbus");
	if (IS_ERR(dotg->vbus_reg))
		dev_err(dwc->dev, "failed to obtain vbus regulator\n");

	if (dotg->ext_otg_ops) {
		ret = dwc3_ext_otg_setup(dotg);
		if (ret) {
			dev_err(dwc->dev, "failed to setup OTG\n");
			return ret;
		}
	}

#if defined(CONFIG_TYPEC_DEFAULT)
	typec = devm_kzalloc(dwc->dev, sizeof(*typec), GFP_KERNEL);
	if (!typec)
		return -ENOMEM;

	/* mutex_init(&md05->lock); */
	typec->dev = dwc->dev;

	typec->cap.type = TYPEC_PORT_DRP;
	typec->cap.revision = USB_TYPEC_REV_1_2;
	typec->cap.pd_revision = 0x312;
	typec->cap.prefer_role = TYPEC_NO_PREFERRED_ROLE;

	typec->port = typec_register_port(dwc->dev, &typec->cap);
	if (!typec->port)
		return -ENODEV;

	typec_set_data_role(typec->port, TYPEC_DEVICE);
	typec_set_pwr_role(typec->port, TYPEC_SINK);
	typec_set_pwr_opmode(typec->port, TYPEC_PWR_MODE_USB);

	memset(&partner, 0, sizeof(struct typec_partner_desc));

	dotg->typec = typec;

	typec->partner = typec_register_partner(typec->port, &partner);
	if (!dotg->typec->partner)
		dev_err(dwc->dev, "failed register partner\n");
#endif

	wake_lock_init(&dotg->wakelock, WAKE_LOCK_SUSPEND, "dwc3-otg");
	mutex_init(&dotg->lock);

	ret = sysfs_create_group(&dwc->dev->kobj, &dwc3_otg_attr_group);
	if (ret)
		dev_err(dwc->dev, "failed to create dwc3 otg attributes\n");

	/* Enable LDO initially */
	phy_conn(dwc->usb2_generic_phy, 1);

	init_completion(&dotg->resume_cmpl);
	dotg->dwc3_suspended = 0;
	/* register_usb_is_connect(otg_is_connect); */
	dotg->pm_nb.notifier_call = dwc3_otg_pm_notifier;
	register_pm_notifier(&dotg->pm_nb);

	dwc->int_qos_lock_wq = create_singlethread_workqueue("usb_int_qos_wq");
	INIT_WORK(&dwc->int_qos_work, dwc3_int_lock_qos_work);
#if defined(CONFIG_IF_CB_MANAGER)
	usb_d = kzalloc(sizeof(struct usb_dev), GFP_KERNEL);
	if (!usb_d)
		return -ENOMEM;

	usb_d->ops = NULL;
	usb_d->data = (void *)dotg;
	dotg->man = register_usb(usb_d);
#endif

	return 0;
}

void dwc3_otg_exit(struct dwc3 *dwc)
{
	struct dwc3_otg *dotg = dwc->dotg;

	if (!dotg->ext_otg_ops)
		return;

#if defined(CONFIG_TYPEC_DEFAULT)
	typec_unregister_partner(dotg->typec->partner);
	typec_unregister_port(dotg->typec->port);
#endif
#if defined(CONFIG_IF_CB_MANAGER)
	kfree(dotg->man->usb_d);
#endif
	unregister_pm_notifier(&dotg->pm_nb);

	dwc3_ext_otg_exit(dotg);

	sysfs_remove_group(&dwc->dev->kobj, &dwc3_otg_attr_group);
	wake_lock_destroy(&dotg->wakelock);
	free_irq(dotg->irq, dotg);
	dotg->otg.state = OTG_STATE_UNDEFINED;
	kfree(dotg);
	dwc->dotg = NULL;
}
