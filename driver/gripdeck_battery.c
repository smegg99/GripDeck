// gripdeck_battery.c
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/hid.h>
#include <linux/usb.h>
#include <linux/power_supply.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/atomic.h>

#define GRIPDECK_VID               0x1209
#define GRIPDECK_PID               0x2078
#define VENDOR_REPORT_ID           6
#define VENDOR_REPORT_SIZE         32
#define VENDOR_FEATURE_REPORT_SIZE (VENDOR_REPORT_SIZE + 1)
#define PROTOCOL_VERSION           0x01
#define PROTOCOL_MAGIC             0x4744
#define CMD_GET_STATUS             0x02

typedef struct __packed {
    u16 magic;
    u8 protocol_version;
    u8 command;
    u32 sequence;
    u8 payload[24];
} vendor_packet_t;

struct gripdeck_data {
    struct hid_device    *hdev;
    struct usb_interface *intf;
    struct usb_device    *udev;
    struct power_supply  *battery;
    struct delayed_work   work;
    struct mutex          lock;
    atomic_t              seq;
    u16 batt_mv;
    s16 batt_ma;
    u32 to_empty_s;
    u16 charg_mv;
    s16 charg_ma;
    u32 to_full_s;
    u8  capacity;
};

static enum power_supply_property gripdeck_props[] = {
    POWER_SUPPLY_PROP_STATUS,
    POWER_SUPPLY_PROP_VOLTAGE_NOW,
    POWER_SUPPLY_PROP_CURRENT_NOW,
    POWER_SUPPLY_PROP_CAPACITY,
    POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
    POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
};

static int gripdeck_get_property(struct power_supply *psy,
                                 enum power_supply_property psp,
                                 union power_supply_propval *val)
{
    struct gripdeck_data *st = power_supply_get_drvdata(psy);
    int ret = 0;

    mutex_lock(&st->lock);
    switch (psp) {
    case POWER_SUPPLY_PROP_STATUS:
        if (st->capacity >= 100)
            val->intval = POWER_SUPPLY_STATUS_FULL;
        else if (st->charg_ma > 0)
            val->intval = POWER_SUPPLY_STATUS_CHARGING;
        else if (st->batt_ma < 0)
            val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
        else
            val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
        break;
    case POWER_SUPPLY_PROP_VOLTAGE_NOW:
        val->intval = st->batt_mv * 1000;
        break;
    case POWER_SUPPLY_PROP_CURRENT_NOW:
        val->intval = st->batt_ma * 1000;
        break;
    case POWER_SUPPLY_PROP_CAPACITY:
        val->intval = st->capacity;
        break;
    case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
        val->intval = st->to_empty_s;
        break;
    case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
        val->intval = st->to_full_s;
        break;
    default:
        ret = -EINVAL;
    }
    mutex_unlock(&st->lock);
    return ret;
}

static const struct power_supply_desc gripdeck_batt_desc = {
    .name           = "gripdeck_battery",
    .type           = POWER_SUPPLY_TYPE_BATTERY,
    .properties     = gripdeck_props,
    .num_properties = ARRAY_SIZE(gripdeck_props),
    .get_property   = gripdeck_get_property,
};

static void gripdeck_update_work(struct work_struct *work)
{
    struct gripdeck_data *st = container_of(to_delayed_work(work),
                                            struct gripdeck_data, work);
    uint8_t *buf;
    vendor_packet_t packet;
    int ret;
    unsigned int seq = atomic_inc_return(&st->seq);

    memset(&packet, 0, sizeof(packet));
    packet.magic = cpu_to_le16(PROTOCOL_MAGIC);
    packet.protocol_version = PROTOCOL_VERSION;
    packet.command = CMD_GET_STATUS;
    packet.sequence = cpu_to_le32(seq);

    buf = kmalloc(VENDOR_FEATURE_REPORT_SIZE, GFP_KERNEL);
    if (!buf)
      goto resched;

    buf[0] = VENDOR_REPORT_ID;
    memcpy(&buf[1], &packet, sizeof(packet));
    ret = hid_hw_raw_request(st->hdev, VENDOR_REPORT_ID,
                              buf, VENDOR_FEATURE_REPORT_SIZE,
                              HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
    if (ret < 0)
        goto free_buf;

    memset(buf, 0, VENDOR_FEATURE_REPORT_SIZE);
    buf[0] = VENDOR_REPORT_ID;
    ret = hid_hw_raw_request(st->hdev, VENDOR_REPORT_ID,
                              buf, VENDOR_FEATURE_REPORT_SIZE,
                              HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
    if (ret < 0)
        goto free_buf;

    mutex_lock(&st->lock);
    st->batt_mv     = le16_to_cpu(*(u16 *)(buf + 9));
    st->batt_ma     = le16_to_cpu(*(s16 *)(buf + 11));
    st->to_empty_s  = le32_to_cpu(*(u32 *)(buf + 13));
    st->charg_mv    = le16_to_cpu(*(u16 *)(buf + 17));
    st->charg_ma    = le16_to_cpu(*(s16 *)(buf + 19));
    st->to_full_s   = le32_to_cpu(*(u32 *)(buf + 21));
    st->capacity    = *(u8 *)(buf + 25);
    mutex_unlock(&st->lock);

    power_supply_changed(st->battery);

free_buf:
    kfree(buf);
resched:
    schedule_delayed_work(&st->work, msecs_to_jiffies(2000));
}

static int gripdeck_hid_probe(struct hid_device *hdev,
                              const struct hid_device_id *id)
{
    struct gripdeck_data *st;
    struct power_supply_config cfg = {};
    int ret;

    st = kzalloc(sizeof(*st), GFP_KERNEL);
    if (!st)
        return -ENOMEM;
    mutex_init(&st->lock);
    atomic_set(&st->seq, 0);

    st->hdev = hdev;
    st->intf = to_usb_interface(hdev->dev.parent);
    st->udev = interface_to_usbdev(st->intf);
    st->intf = to_usb_interface(hdev->dev.parent);

    cfg.drv_data = st;
    st->battery = power_supply_register(&hdev->dev,
                                        &gripdeck_batt_desc,
                                        &cfg);
    if (IS_ERR(st->battery)) {
        ret = PTR_ERR(st->battery);
        goto err_free;
    }

    INIT_DELAYED_WORK(&st->work, gripdeck_update_work);
    schedule_delayed_work(&st->work, msecs_to_jiffies(2000));

    ret = hid_parse(hdev);
    if (ret)
        goto err_ps;

    ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
    if (ret)
        goto err_ps;

    hid_set_drvdata(hdev, st);
    dev_info(&hdev->dev, "GripDeck HID battery driver loaded\n");
    return 0;

err_ps:
    cancel_delayed_work_sync(&st->work);
    power_supply_unregister(st->battery);
err_free:
    kfree(st);
    return ret;
}

static void gripdeck_hid_remove(struct hid_device *hdev)
{
    struct gripdeck_data *st = hid_get_drvdata(hdev);

    cancel_delayed_work_sync(&st->work);
    power_supply_unregister(st->battery);
    hid_hw_stop(hdev);
    kfree(st);
    dev_info(&hdev->dev, "GripDeck HID battery driver unloaded\n");
}

static const struct hid_device_id gripdeck_hid_table[] = {
    { HID_USB_DEVICE(GRIPDECK_VID, GRIPDECK_PID) },
    { }
};
MODULE_DEVICE_TABLE(hid, gripdeck_hid_table);

static struct hid_driver gripdeck_hid_driver = {
    .name   = "gripdeck_battery",
    .id_table = gripdeck_hid_table,
    .probe  = gripdeck_hid_probe,
    .remove = gripdeck_hid_remove,
};

module_hid_driver(gripdeck_hid_driver);

MODULE_AUTHOR("smegg99");
MODULE_DESCRIPTION("GripDeck Controller HID-based battery driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
