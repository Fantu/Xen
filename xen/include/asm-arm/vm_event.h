/*
 * vm_event.h: architecture specific vm_event handling routines
 *
 * Copyright (c) 2015 Tamas K Lengyel (tamas@tklengyel.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 */

#ifndef __ASM_ARM_VM_EVENT_H__
#define __ASM_ARM_VM_EVENT_H__

#include <xen/sched.h>

static inline
int vm_event_init_domain(struct domain *d)
{
    /* Not supported on ARM. */
    return 0;
}

static inline
void vm_event_cleanup_domain(struct domain *d)
{
    /* Not supported on ARM. */
}

static inline
void vm_event_toggle_singlestep(struct domain *d, struct vcpu *v)
{
    /* Not supported on ARM. */
}

#endif /* __ASM_ARM_VM_EVENT_H__ */