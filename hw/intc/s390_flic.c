/*
 * QEMU S390x KVM floating interrupt controller (flic)
 *
 * Copyright 2014 IBM Corp.
 * Author(s): Jens Freimann <jfrei@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include <sys/ioctl.h>
#include "qemu/error-report.h"
#include "hw/sysbus.h"
#include "sysemu/kvm.h"
#include "migration/qemu-file.h"
#include "hw/s390x/s390_flic.h"
#include "trace.h"

#define FLIC_SAVE_INITIAL_SIZE getpagesize()
#define FLIC_FAILED (-1UL)
#define FLIC_SAVEVM_VERSION 1

void s390_flic_init(void)
{
    DeviceState *dev;
    int r;

    if (kvm_enabled()) {
        dev = qdev_create(NULL, "s390-flic");
        object_property_add_child(qdev_get_machine(), "s390-flic",
                                OBJECT(dev), NULL);
        r = qdev_init(dev);
        if (r) {
            error_report("flic: couldn't create qdev");
        }
    }
}

/**
 * flic_get_all_irqs - store all pending irqs in buffer
 * @buf: pointer to buffer which is passed to kernel
 * @len: length of buffer
 * @flic: pointer to flic device state
 *
 * Returns: -ENOMEM if buffer is too small,
 * -EINVAL if attr.group is invalid,
 * -EFAULT if copying to userspace failed,
 * on success return number of stored interrupts
 */
static int flic_get_all_irqs(KVMS390FLICState *flic,
                             void *buf, int len)
{
    struct kvm_device_attr attr = {
        .group = KVM_DEV_FLIC_GET_ALL_IRQS,
        .addr = (uint64_t) buf,
        .attr = len,
    };
    int rc;

    rc = ioctl(flic->fd, KVM_GET_DEVICE_ATTR, &attr);

    return rc == -1 ? -errno : rc;
}

static void flic_enable_pfault(KVMS390FLICState *flic)
{
    struct kvm_device_attr attr = {
        .group = KVM_DEV_FLIC_APF_ENABLE,
    };
    int rc;

    rc = ioctl(flic->fd, KVM_SET_DEVICE_ATTR, &attr);

    if (rc) {
        fprintf(stderr, "flic: couldn't enable pfault\n");
    }
}

static void flic_disable_wait_pfault(KVMS390FLICState *flic)
{
    struct kvm_device_attr attr = {
        .group = KVM_DEV_FLIC_APF_DISABLE_WAIT,
    };
    int rc;

    rc = ioctl(flic->fd, KVM_SET_DEVICE_ATTR, &attr);

    if (rc) {
        fprintf(stderr, "flic: couldn't disable pfault\n");
    }
}

/** flic_enqueue_irqs - returns 0 on success
 * @buf: pointer to buffer which is passed to kernel
 * @len: length of buffer
 * @flic: pointer to flic device state
 *
 * Returns: -EINVAL if attr.group is unknown
 */
static int flic_enqueue_irqs(void *buf, uint64_t len,
                            KVMS390FLICState *flic)
{
    int rc;
    struct kvm_device_attr attr = {
        .group = KVM_DEV_FLIC_ENQUEUE,
        .addr = (uint64_t) buf,
        .attr = len,
    };

    rc = ioctl(flic->fd, KVM_SET_DEVICE_ATTR, &attr);

    return rc ? -errno : 0;
}

/**
 * __get_all_irqs - store all pending irqs in buffer
 * @flic: pointer to flic device state
 * @buf: pointer to pointer to a buffer
 * @len: length of buffer
 *
 * Returns: return value of flic_get_all_irqs
 * Note: Retry and increase buffer size until flic_get_all_irqs
 * either returns a value >= 0 or a negative error code.
 * -ENOMEM is an exception, which means the buffer is too small
 * and we should try again. Other negative error codes can be
 * -EFAULT and -EINVAL which we ignore at this point
 */
static int __get_all_irqs(KVMS390FLICState *flic,
                          void **buf, int len)
{
    int r;

    do {
        /* returns -ENOMEM if buffer is too small and number
         * of queued interrupts on success */
        r = flic_get_all_irqs(flic, *buf, len);
        if (r >= 0) {
            break;
        }
        len *= 2;
        *buf = g_try_realloc(*buf, len);
        if (!buf) {
            return -ENOMEM;
        }
    } while (r == -ENOMEM && len <= KVM_S390_FLIC_MAX_BUFFER);

    return r;
}

/**
 * kvm_flic_save - Save pending floating interrupts
 * @f: QEMUFile containing migration state
 * @opaque: pointer to flic device state
 *
 * Note: Pass buf and len to kernel. Start with one page and
 * increase until buffer is sufficient or maxium size is
 * reached
 */
static void kvm_flic_save(QEMUFile *f, void *opaque)
{
    KVMS390FLICState *flic = opaque;
    int len = FLIC_SAVE_INITIAL_SIZE;
    void *buf;
    int count;

    flic_disable_wait_pfault((struct KVMS390FLICState *) opaque);

    buf = g_try_malloc0(len);
    if (!buf) {
        /* Storing FLIC_FAILED into the count field here will cause the
         * target system to fail when attempting to load irqs from the
         * migration state */
        error_report("flic: couldn't allocate memory");
        qemu_put_be64(f, FLIC_FAILED);
        return;
    }

    count = __get_all_irqs(flic, &buf, len);
    if (count < 0) {
        error_report("flic: couldn't retrieve irqs from kernel, rc %d",
                     count);
        /* Storing FLIC_FAILED into the count field here will cause the
         * target system to fail when attempting to load irqs from the
         * migration state */
        qemu_put_be64(f, FLIC_FAILED);
    } else {
        qemu_put_be64(f, count);
        qemu_put_buffer(f, (uint8_t *) buf,
                        count * sizeof(struct kvm_s390_irq));
    }
    g_free(buf);
}

/**
 * kvm_flic_load - Load pending floating interrupts
 * @f: QEMUFile containing migration state
 * @opaque: pointer to flic device state
 * @version_id: version id for migration
 *
 * Returns: value of flic_enqueue_irqs, -EINVAL on error
 * Note: Do nothing when no interrupts where stored
 * in QEMUFile
 */
static int kvm_flic_load(QEMUFile *f, void *opaque, int version_id)
{
    uint64_t len = 0;
    uint64_t count = 0;
    void *buf = NULL;
    int r = 0;

    if (version_id != FLIC_SAVEVM_VERSION) {
        r = -EINVAL;
        goto out;
    }

    flic_enable_pfault((struct KVMS390FLICState *) opaque);

    count = qemu_get_be64(f);
    len = count * sizeof(struct kvm_s390_irq);
    if (count == FLIC_FAILED) {
        r = -EINVAL;
        goto out;
    }
    if (count == 0) {
        r = 0;
        goto out;
    }
    buf = g_try_malloc0(len);
    if (!buf) {
        r = -ENOMEM;
        goto out;
    }

    if (qemu_get_buffer(f, (uint8_t *) buf, len) != len) {
        r = -EINVAL;
        goto out_free;
    }
    r = flic_enqueue_irqs(buf, len, (struct KVMS390FLICState *) opaque);

out_free:
    g_free(buf);
out:
    return r;
}

static void kvm_s390_flic_realize(DeviceState *dev, Error **errp)
{
    KVMS390FLICState *flic_state = KVM_S390_FLIC(dev);
    struct kvm_create_device cd = {0};
    int ret;

    flic_state->fd = -1;
    if (!kvm_check_extension(kvm_state, KVM_CAP_DEVICE_CTRL)) {
        trace_flic_no_device_api(errno);
        return;
    }

    cd.type = KVM_DEV_TYPE_FLIC;
    ret = kvm_vm_ioctl(kvm_state, KVM_CREATE_DEVICE, &cd);
    if (ret < 0) {
        trace_flic_create_device(errno);
        return;
    }
    flic_state->fd = cd.fd;

    /* Register savevm handler for floating interrupts */
    register_savevm(NULL, "s390-flic", 0, 1, kvm_flic_save,
                    kvm_flic_load, (void *) flic_state);
}

static void kvm_s390_flic_unrealize(DeviceState *dev, Error **errp)
{
    KVMS390FLICState *flic_state = KVM_S390_FLIC(dev);

    unregister_savevm(DEVICE(flic_state), "s390-flic", flic_state);
}

static void kvm_s390_flic_reset(DeviceState *dev)
{
    KVMS390FLICState *flic = KVM_S390_FLIC(dev);
    struct kvm_device_attr attr = {
        .group = KVM_DEV_FLIC_CLEAR_IRQS,
    };
    int rc = 0;

    if (flic->fd == -1) {
        return;
    }

    flic_disable_wait_pfault(flic);

    rc = ioctl(flic->fd, KVM_SET_DEVICE_ATTR, &attr);
    if (rc) {
        trace_flic_reset_failed(errno);
    }

    flic_enable_pfault(flic);
}

static void kvm_s390_flic_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = kvm_s390_flic_realize;
    dc->unrealize = kvm_s390_flic_unrealize;
    dc->reset = kvm_s390_flic_reset;
}

static const TypeInfo kvm_s390_flic_info = {
    .name          = TYPE_KVM_S390_FLIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(KVMS390FLICState),
    .class_init    = kvm_s390_flic_class_init,
};

static void kvm_s390_flic_register_types(void)
{
    type_register_static(&kvm_s390_flic_info);
}

type_init(kvm_s390_flic_register_types)
