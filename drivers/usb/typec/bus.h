/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __USB_TYPEC_ALTMODE_H__
#define __USB_TYPEC_ALTMODE_H__

#include <linux/usb/typec_altmode.h>
#include <linux/usb/typec_mux.h>

struct bus_type;

struct altmode {
	unsigned int			id;
	struct typec_altmode		adev;
	struct typec_mux		*mux;

	enum typec_port_data		roles;

	struct attribute		*attrs[5];
	char				group_name[8];
	struct attribute_group		group;
	const struct attribute_group	*groups[2];

	struct altmode			*partner;
	struct altmode			*plug[2];

	struct blocking_notifier_head	nh;
};

#define to_altmode(d) container_of(d, struct altmode, adev)

extern struct bus_type typec_bus;
extern const struct device_type typec_altmode_dev_type;
extern const struct device_type typec_port_dev_type;

#define is_typec_altmode(_dev_) (_dev_->type == &typec_altmode_dev_type)
#define is_typec_port(_dev_) (_dev_->type == &typec_port_dev_type)

extern struct class typec_mux_class;

struct typec_switch {
	struct device *dev;
    struct list_head entry;
    int (*set)(struct typec_switch *sw, enum typec_orientation orientation);
};

struct typec_mux {
	struct device *dev;
    struct list_head entry;
    int (*set)(struct typec_mux *mux, int state);
};

#endif /* __USB_TYPEC_ALTMODE_H__ */
