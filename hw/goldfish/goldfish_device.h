/* Copyright (C) 2007-2008 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/
#ifndef GOLDFISH_DEVICE_H
#define GOLDFISH_DEVICE_H

#include "exec/address-spaces.h"

struct goldfish_device {
    struct goldfish_device *next;
    struct goldfish_device *prev;
    uint32_t reported_state;
    void *cookie;
    const char *name;
    uint32_t id;
    uint32_t base; // filled in by goldfish_device_add if 0
    uint32_t size;
    uint32_t irq; // filled in by goldfish_device_add if 0
    uint32_t irq_count;
};


void goldfish_device_set_irq(struct goldfish_device *dev, int irq, int level);
int goldfish_device_add(struct goldfish_device *dev, const MemoryRegionOps *ops, void *opaque);

int goldfish_add_device_no_io(struct goldfish_device *dev);

void goldfish_device_init(qemu_irq *pic, uint32_t base, uint32_t size, uint32_t irq, uint32_t irq_count);
int goldfish_device_bus_init(uint32_t base, uint32_t irq);

#ifdef TARGET_I386
/* Maximum IRQ number available for a device on x86. */
#define GFD_MAX_IRQ      16
/* IRQ reserved for keyboard. */
#define GFD_KBD_IRQ      1
/* IRQ reserved for mouse. */
#define GFD_MOUSE_IRQ    12
/* IRQ reserved for error (raising an exception in TB code). */
#define GFD_ERR_IRQ      13
#else
/* Maximum IRQ number available for a device on ARM. */
#define GFD_MAX_IRQ     32
#endif

#endif  /* GOLDFISH_DEVICE_H */
