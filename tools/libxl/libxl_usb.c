/*
 * Copyright (C) 2009      Citrix Ltd.
 * Author Vincent Hanquez <vincent.hanquez@eu.citrix.com>
 * Author Stefano Stabellini <stefano.stabellini@eu.citrix.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; version 2.1 only. with the special
 * exception on linking described in file LICENSE.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 */

#include "libxl_osdeps.h" /* must come before any other headers */

#include "libxl_internal.h"

/*
 * /libxl/<domid>/usb/<devid>/{type, protocol, backend, [typeinfo]}
 */
#define USB_INFO_PATH "%s/usb"
#define USB_HOSTDEV_ID "hostdev-%04x-%04x"

/*
 * Just use plain numbers for now.  Replace these with strings at some point.
 */
static char * hostdev_xs_path(libxl__gc *gc, uint32_t domid,
                                    libxl__device_usb *usbdev)
{
    return GCSPRINTF(USB_INFO_PATH "/%s",
                     libxl__xs_libxl_path(gc, domid),
                     GCSPRINTF(USB_HOSTDEV_ID,
                               (uint16_t)usbdev->u.hostdev.hostbus,
                               (uint16_t)usbdev->u.hostdev.hostaddr));
}

static void hostdev_xs_append_params(libxl__gc *gc, libxl__device_usb *usbdev,
                                  flexarray_t *a)
{
    flexarray_append_pair(a, "hostbus",
                          GCSPRINTF("%d",
                                    usbdev->u.hostdev.hostbus));
    flexarray_append_pair(a, "hostaddr",
                          GCSPRINTF("%d",
                                    usbdev->u.hostdev.hostaddr));
}

static int read_hostdev_xenstore_entry(libxl__gc *gc, const char * path,
                                       libxl__device_usb *usbdev)
{
    int rc;
    const char * val;

    rc = libxl__xs_read_checked(gc, XBT_NULL,
                                GCSPRINTF("%s/hostbus", path), &val);
    if (rc) goto out;
    usbdev->u.hostdev.hostbus = atoi(val);
    
    rc = libxl__xs_read_checked(gc, XBT_NULL,
                                 GCSPRINTF("%s/hostaddr", path), &val);
    if (rc) goto out;
    usbdev->u.hostdev.hostaddr = atoi(val);

    rc = 0;
out:
    return rc;
}

static int usb_read_xenstore(libxl__gc *gc, const char * path,
                                           libxl__device_usb *usbdev)
{
    const char *val;
    int rc;

    rc = libxl__xs_read_checked(gc, XBT_NULL,
                                GCSPRINTF("%s/protocol", path), &val);
    if (rc) goto out;
    rc = libxl_usb_protocol_from_string(val, &usbdev->protocol);
    if (rc) goto out;
        
    rc = libxl__xs_read_checked(gc, XBT_NULL,
                                GCSPRINTF("%s/backend_domid", path), &val);
    if (rc) goto out;
    usbdev->backend_domid = atoi(val);
    
    rc = libxl__xs_read_checked(gc, XBT_NULL,
                                GCSPRINTF("%s/type", path), &val);
    if (rc) goto out;
    rc = libxl_device_usb_type_from_string(val, &usbdev->type);
    if (rc) goto out;

    switch (usbdev->type) {
    case LIBXL_DEVICE_USB_TYPE_HOSTDEV:
        rc = read_hostdev_xenstore_entry(gc, path, usbdev);
        if (rc) goto out;
        break;
    default:
        LOG(ERROR, "Internal error (unimplemented type)");
        goto out;
    }

    rc = 0;
out:
    return rc;
}

static int usb_add_xenstore(libxl__gc *gc, uint32_t domid,
                            libxl__device_usb *usbdev)
{
    libxl_ctx *ctx = libxl__gc_owner(gc);
    flexarray_t *dev;
    char *dev_path;
    xs_transaction_t t = 0;
    struct xs_permissions noperm[1];
    int rc = 0;

    noperm[0].id = 0;
    noperm[0].perms = XS_PERM_NONE;

    dev = flexarray_make(gc, 16, 1);

    flexarray_append_pair(dev, "protocol",
                          GCSPRINTF("%s",
                              libxl_usb_protocol_to_string(usbdev->protocol)));

    flexarray_append_pair(dev, "backend_domid",
                          GCSPRINTF("%d", usbdev->backend_domid));

    flexarray_append_pair(dev, "type",
                          GCSPRINTF("%s",
                               libxl_device_usb_type_to_string(usbdev->type)));

    switch (usbdev->type) {
    case LIBXL_DEVICE_USB_TYPE_HOSTDEV:
        dev_path = hostdev_xs_path(gc, domid, usbdev);
        hostdev_xs_append_params(gc, usbdev, dev);
        break;
    default:
        LOG(ERROR, "Invalid device type: %d", usbdev->type);
        return ERROR_FAIL;
    }

    LIBXL__LOG(ctx, LIBXL__LOG_DEBUG, "Adding new usb device to xenstore");

    for(;;) {
        rc = libxl__xs_transaction_start(gc, &t);
        if (rc) goto out;

        /* Helpfully, mkdir returns 0 on failure, 1 on success */
        if (!libxl__xs_mkdir(gc, t, dev_path, noperm, ARRAY_SIZE(noperm))) {
            rc = ERROR_FAIL;
            goto out;
        }

        /* And libxl__xs_writev *always* returns 0 no matter what */
        libxl__xs_writev(gc, t, dev_path,
                         libxl__xs_kvs_of_flexarray(gc, dev, dev->count));

        rc = libxl__xs_transaction_commit(gc, &t);
        if (!rc) break;
        if (rc <0) goto out;
    }

out:
    return rc;
}

static int usb_remove_xenstore(libxl__gc *gc, uint32_t domid,
                               libxl__device_usb *usbdev)
{
    libxl_ctx *ctx = libxl__gc_owner(gc);
    char *dev_path;

    LIBXL__LOG(ctx, LIBXL__LOG_DEBUG, "Removing device from xenstore");

    switch (usbdev->type) {
    case LIBXL_DEVICE_USB_TYPE_HOSTDEV:
        dev_path = hostdev_xs_path(gc, domid, usbdev);
        break;
    default:
        LOG(ERROR, "Invalid device type: %d", usbdev->type);
        return ERROR_FAIL;
    }

    xs_rm(ctx->xsh, XBT_NULL, dev_path);

    return 0;
}

static int get_assigned_devices(libxl__gc *gc, uint32_t domid,
                                libxl__device_usb **list, int *num)
{
    char **devlist, *dompath;
    unsigned int nd = 0;
    int i, rc = 0;

    *list = NULL;
    *num = 0;

    dompath = GCSPRINTF(USB_INFO_PATH,
                        libxl__xs_libxl_path(gc, domid));

    devlist = libxl__xs_directory(gc, XBT_NULL, dompath, &nd);
    if (!devlist)
        goto out;

    *list = libxl__calloc(gc, nd, sizeof(libxl__device_usb));

    for(i = 0; i < nd; i++) {
        char *path;

        path = GCSPRINTF("%s/%s", dompath, devlist[i]);
        
        rc = usb_read_xenstore(gc, path, (*list) + i);
        if (rc) {
            *list = NULL;
            *num = 0;
            goto out;
        }
    }

    *num = nd;

out:
    return rc;
}

static int is_usbdev_type_hostdev_equal(libxl__device_usb *a,
                                        libxl__device_usb *b)
{
    if (!memcmp(&a->u.hostdev, &b->u.hostdev, sizeof(a->u.hostdev)))
        return 1;
    else
        return 0;
}

static int is_usbdev_in_array(libxl__device_usb *assigned, int num_assigned,
                              libxl__device_usb *dev)
{
    int i;

    /* Devices are the same if:
     * - They have the same backend_domid
     * - They are of the same type
     * - Their types match
     * In theory, someone could try to pass the same device through
     * via both PVUSB and qemu; not comparing protocol will prevent that.
     */
    for(i = 0; i < num_assigned; i++) {
        if (assigned[i].type != dev->type
            || assigned[i].backend_domid != dev->backend_domid)
            continue;

        switch (dev->type) {
        case LIBXL_DEVICE_USB_TYPE_HOSTDEV:
            if (!is_usbdev_type_hostdev_equal(dev, assigned+i))
                continue;
        }

        return 1;
    }

    return 0;
}

static void usbdev_ext_to_int(libxl__device_usb *dev_int,
                              libxl_device_usb *dev_ext)
{
    dev_int->protocol = dev_ext->protocol;

    if (dev_ext->backend_domid == LIBXL_DEVICE_USB_BACKEND_DEFAULT)
        dev_int->backend_domid = 0;
    else
        dev_int->backend_domid = dev_ext->backend_domid;

    dev_int->type = dev_ext->type;
    memcpy(&dev_int->u, &dev_ext->u, sizeof(dev_ext->u));
}

static void usbdev_int_to_ext(libxl_device_usb *dev_ext,
                              libxl__device_usb *dev_int)
{
    dev_ext->protocol = dev_int->protocol;

    dev_ext->backend_domid = dev_int->backend_domid;

    dev_ext->type = dev_int->type;
    memcpy(&dev_ext->u, &dev_int->u, sizeof(dev_ext->u));
}

/*
 * USB add
 */
static int do_usb_add(libxl__gc *gc, uint32_t domid,
                      libxl__device_usb *usbdev)
{
    int rc;

    switch (usbdev->protocol) {
    case LIBXL_USB_PROTOCOL_DEVICEMODEL:
        switch (libxl__device_model_version_running(gc, domid)) {
        case LIBXL_DEVICE_MODEL_VERSION_QEMU_XEN_TRADITIONAL:
            LOG(ERROR, "usb-add not yet implemented for qemu-traditional");
            return ERROR_INVAL;
        case LIBXL_DEVICE_MODEL_VERSION_QEMU_XEN:
            rc = libxl__qmp_usb_add(gc, domid, usbdev);
            if (rc)
                goto out;
            break;
        default:
            return ERROR_INVAL;
        }
        break;
    default:
        return ERROR_FAIL;
    }

    rc = usb_add_xenstore(gc, domid, usbdev);
out:
    return rc;
}



static int libxl__device_usb_add(libxl__gc *gc, uint32_t domid,
                                 libxl_device_usb *dev_ext)
{
    libxl_ctx *ctx = libxl__gc_owner(gc);
    libxl__device_usb *assigned, _usbdev, *usbdev;
    int rc = ERROR_FAIL, num_assigned;
    libxl_domain_type domtype = libxl__domain_type(gc, domid);

    /* Interpret incoming */
    usbdev = &_usbdev;

    usbdev->target_domid = domid;
    usbdev->dm_domid = libxl_get_stubdom_id(ctx, domid);

    usbdev_ext_to_int(usbdev, dev_ext);

    if (usbdev->protocol == LIBXL_USB_PROTOCOL_AUTO) {
        if (domtype == LIBXL_DOMAIN_TYPE_PV) {
            usbdev->protocol = LIBXL_USB_PROTOCOL_PV;
        } else if (domtype == LIBXL_DOMAIN_TYPE_HVM) {
            /* FIXME: See if we can detect PV frontend */
            usbdev->protocol = LIBXL_USB_PROTOCOL_DEVICEMODEL;
        }
    }

    /* Check to make sure we're doing something that's impemented */
    if (usbdev->protocol != LIBXL_USB_PROTOCOL_DEVICEMODEL) {
        rc = ERROR_FAIL;
        LOG(ERROR, "Protocol not implemented");
        goto out;
    }

    if (usbdev->dm_domid != 0) {
        rc = ERROR_FAIL;
        LOG(ERROR, "Stubdoms not yet supported");
        goto out;
    }

    /* Double-check to make sure it's not already assigned */
    rc = get_assigned_devices(gc, domid, &assigned, &num_assigned);
    if (rc) {
        LOG(ERROR, "cannot determine if device is assigned,"
            " refusing to continue");
        goto out;
    }
    if (is_usbdev_in_array(assigned, num_assigned, usbdev)) {
        LOG(ERROR, "USB device already attached to a domain");
        rc = ERROR_FAIL;
        goto out;
    }

    /* Do the add */
    if (do_usb_add(gc, domid, usbdev))
        rc = ERROR_FAIL;

out:
    return rc;
}

int libxl_device_usb_add(libxl_ctx *ctx, uint32_t domid,
                         libxl_device_usb *usbdev,
                         const libxl_asyncop_how *ao_how)
{
    AO_CREATE(ctx, domid, ao_how);
    int rc;
    rc = libxl__device_usb_add(gc, domid, usbdev);
    libxl__ao_complete(egc, ao, rc);
    return AO_INPROGRESS;
}

/*
 * USB remove
 */
static int do_usb_remove(libxl__gc *gc, uint32_t domid,
                         libxl__device_usb *usbdev)
{
    int rc;

    switch (usbdev->protocol) {
    case LIBXL_USB_PROTOCOL_DEVICEMODEL:
        switch (libxl__device_model_version_running(gc, domid)) {
        case LIBXL_DEVICE_MODEL_VERSION_QEMU_XEN_TRADITIONAL:
            LOG(ERROR, "usb-remove not yet implemented for qemu-traditional");
            return ERROR_INVAL;
        case LIBXL_DEVICE_MODEL_VERSION_QEMU_XEN:
            rc = libxl__qmp_usb_remove(gc, domid, usbdev);
            if (rc)
                goto out;
            break;
        default:
            return ERROR_INVAL;
        }
        break;
    default:
        return ERROR_FAIL;
    }

    rc = usb_remove_xenstore(gc, domid, usbdev);
out:
    return rc;
}
static int libxl__device_usb_remove(libxl__gc *gc, uint32_t domid,
                                    libxl_device_usb *dev_ext)
{
    libxl_ctx *ctx = libxl__gc_owner(gc);
    libxl__device_usb *assigned, _usbdev, *usbdev;
    int rc = ERROR_FAIL, num_assigned;
    libxl_domain_type domtype = libxl__domain_type(gc, domid);

    /* Interpret incoming */
    usbdev = &_usbdev;

    usbdev->target_domid = domid;
    usbdev->dm_domid = libxl_get_stubdom_id(ctx, domid);

    usbdev_ext_to_int(usbdev, dev_ext);

    if (usbdev->protocol == LIBXL_USB_PROTOCOL_AUTO) {
        if (domtype == LIBXL_DOMAIN_TYPE_PV) {
            usbdev->protocol = LIBXL_USB_PROTOCOL_PV;
        } else if (domtype == LIBXL_DOMAIN_TYPE_HVM) {
            /* FIXME: See if we can detect PV frontend */
            usbdev->protocol = LIBXL_USB_PROTOCOL_DEVICEMODEL;
        }
    }

    /* Check to make sure we're doing something that's impemented */
    if (usbdev->protocol != LIBXL_USB_PROTOCOL_DEVICEMODEL) {
        rc = ERROR_FAIL;
        LOG(ERROR, "Protocol not implemented");
        goto out;
    }

    if (usbdev->dm_domid != 0) {
        rc = ERROR_FAIL;
        LOG(ERROR, "Stubdoms not yet supported");
        goto out;
    }

    /* Double-check to make sure it's not already assigned */
    rc = get_assigned_devices(gc, domid, &assigned, &num_assigned);
    if (rc) {
        LOG(ERROR, "cannot determine if device is assigned,"
            " refusing to continue");
        goto out;
    }
    if (!is_usbdev_in_array(assigned, num_assigned, usbdev)) {
        LOG(ERROR, "USB device doesn't seem to be attached to the domain");
        rc = ERROR_FAIL;
        goto out;
    }

    /* Do the remove */
    if (do_usb_remove(gc, domid, usbdev))
        rc = ERROR_FAIL;

out:
    return rc;
}

int libxl_device_usb_remove(libxl_ctx *ctx, uint32_t domid,
                            libxl_device_usb *usbdev,
                            const libxl_asyncop_how *ao_how)
{
    AO_CREATE(ctx, domid, ao_how);
    int rc;
    rc = libxl__device_usb_remove(gc, domid, usbdev);
    libxl__ao_complete(egc, ao, rc);
    return AO_INPROGRESS;
}


libxl_device_usb *libxl_device_usb_list(libxl_ctx *ctx,
                                        uint32_t domid, int *num)
{
    GC_INIT(ctx);
    libxl__device_usb *devint;
    libxl_device_usb *devext = NULL;
    int n = 0, i, rc;

    rc = get_assigned_devices(gc, domid, &devint, &n);
    if (rc) {
        LOG(ERROR, "Could not get assigned devices");
        goto out;
    }

    if (n == 0)
        goto out;

    devext = libxl__calloc(NOGC, n, sizeof(libxl_device_usb));

    for (i = 0; i < n; i++)
        usbdev_int_to_ext(devext + i, devint + i);

    *num = n;
out:
    GC_FREE;
    return devext;
}
