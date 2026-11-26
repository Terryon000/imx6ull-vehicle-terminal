#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/sysfs.h>
#include <linux/miscdevice.h>

#define DEV_DTS_COMPATIBLE  "liteon,ap3216c"
#define DEV_NAME            "ap3216c"

#define AP3216C_SYS_CONFIG  0x00    // 系统配置(复位/使能)
#define AP3216C_IR_LOW      0x0A    // IR 数据低字节(10 位有效)
#define AP3216C_IR_HIGH     0x0B
#define AP3216C_ALS_LOW     0x0C    // ALS 数据低字节(16 位有效)
#define AP3216C_ALS_HIGH    0x0D
#define AP3216C_PS_LOW      0x0E    // PS 数据低字节(10 位有效)
#define AP3216C_PS_HIGH     0x0F

struct ap3216c_dev {
    struct i2c_client *client;
    struct miscdevice misc;
};

static int ap3216c_read_reg(struct ap3216c_dev *ap, u8 reg)
{
    int ret = i2c_smbus_read_byte_data(ap->client, reg);

    if (ret < 0)
        dev_err(&ap->client->dev, "Failed to read register 0x%02x\n", reg);
    return ret;     // 负数 = 错误码
}

static int ap3216c_write_reg(struct ap3216c_dev *ap, u8 reg, u8 val)
{
    int ret = i2c_smbus_write_byte_data(ap->client, reg, val);

    if (ret < 0)
        dev_err(&ap->client->dev, "Failed to write register 0x%02x\n", reg);
    return ret;
}

// ALS 16 位:两个字节全有效
static ssize_t als_show(struct device *dev, struct device_attribute *attr,
                        char *buf)
{
    struct ap3216c_dev *ap = dev_get_drvdata(dev);
    int lo = ap3216c_read_reg(ap, AP3216C_ALS_LOW);
    int hi = ap3216c_read_reg(ap, AP3216C_ALS_HIGH);

    if (lo < 0) return lo;
    if (hi < 0) return hi;
    return sprintf(buf, "%d\n", (hi << 8) | lo);
}
static DEVICE_ATTR(als, S_IRUGO, als_show, NULL);

// PS/IR 10 位:高字节仅低 2 位有效(以数据手册为准)
static ssize_t ps_show(struct device *dev, struct device_attribute *attr,
                       char *buf)
{
    struct ap3216c_dev *ap = dev_get_drvdata(dev);
    int lo = ap3216c_read_reg(ap, AP3216C_PS_LOW);
    int hi = ap3216c_read_reg(ap, AP3216C_PS_HIGH);

    if (lo < 0) return lo;
    if (hi < 0) return hi;
    return sprintf(buf, "%d\n", ((hi & 0x03) << 8) | lo);
}
static DEVICE_ATTR(ps, S_IRUGO, ps_show, NULL);

static ssize_t ir_show(struct device *dev, struct device_attribute *attr,
                       char *buf)
{
    struct ap3216c_dev *ap = dev_get_drvdata(dev);
    int lo = ap3216c_read_reg(ap, AP3216C_IR_LOW);
    int hi = ap3216c_read_reg(ap, AP3216C_IR_HIGH);

    if (lo < 0) return lo;
    if (hi < 0) return hi;
    return sprintf(buf, "%d\n", ((hi & 0x03) << 8) | lo);
}
static DEVICE_ATTR(ir, S_IRUGO, ir_show, NULL);

static struct attribute *ap3216c_attrs[] = {
    &dev_attr_als.attr,
    &dev_attr_ps.attr,
    &dev_attr_ir.attr,
    NULL,
};
static struct attribute_group ap3216c_attr_group = {
    .attrs = ap3216c_attrs,
};

static const struct of_device_id ap3216c_of_match[] = {
    { .compatible = DEV_DTS_COMPATIBLE },
    { }
};
MODULE_DEVICE_TABLE(of, ap3216c_of_match);

static const struct i2c_device_id ap3216c_id[] = {
    { DEV_NAME, 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, ap3216c_id);

static int ap3216c_probe(struct i2c_client *client,
                         const struct i2c_device_id *id)
{
    struct ap3216c_dev *ap;
    int ret;

    // 1. 检查控制器能力
    if (!i2c_check_functionality(client->adapter,
                                 I2C_FUNC_SMBUS_BYTE_DATA)) {
        dev_err(&client->dev, "SMBus byte access not supported\n");
        return -EIO;
    }

    // 2. 分配私有结构,client 由内核按设备树 reg 属性创建好传入
    ap = devm_kzalloc(&client->dev, sizeof(*ap), GFP_KERNEL);
    if (!ap)
        return -ENOMEM;
    ap->client = client;
    dev_set_drvdata(&client->dev, ap);

    // 3. 初始化芯片:复位 → 等 50ms → 使能 ALS+PS+IR(位含义查数据手册)
    ret = ap3216c_write_reg(ap, AP3216C_SYS_CONFIG, 0x04);
    if (ret) {
        dev_err(&client->dev, "chip reset failed: %d\n", ret);
        return ret;
    }
    msleep(50);
    ret = ap3216c_write_reg(ap, AP3216C_SYS_CONFIG, 0x03);
    if (ret) {
        dev_err(&client->dev, "chip enable failed: %d\n", ret);
        return ret;
    }

    // 4. 挂 misc class:原版 QT 读 /sys/class/misc/ap3216c/{als,ps,ir},
    //    这里用 misc 只为固定用户态路径、QT 零修改,不为 /dev 节点
    ap->misc.minor = MISC_DYNAMIC_MINOR;
    ap->misc.name = DEV_NAME;
    ret = misc_register(&ap->misc);
    if (ret) {
        dev_err(&client->dev, "misc_register failed: %d\n", ret);
        return ret;
    }

    // 5. 在 misc 设备上暴露 sysfs 属性
    dev_set_drvdata(ap->misc.this_device, ap);
    ret = sysfs_create_group(&ap->misc.this_device->kobj, &ap3216c_attr_group);
    if (ret) {
        dev_err(&client->dev, "sysfs_create_group failed: %d\n", ret);
        misc_deregister(&ap->misc);
        return ret;
    }

    dev_info(&client->dev, "ap3216c probed, addr 0x%02x\n", client->addr);
    return 0;
}

static int ap3216c_remove(struct i2c_client *client)
{
    struct ap3216c_dev *ap = dev_get_drvdata(&client->dev);

    sysfs_remove_group(&ap->misc.this_device->kobj, &ap3216c_attr_group);
    misc_deregister(&ap->misc);
    ap3216c_write_reg(ap, AP3216C_SYS_CONFIG, 0x00);    // 关测量

    return 0;
}

static struct i2c_driver ap3216c_driver = {
    .driver = {
        .name = DEV_NAME,
        .of_match_table = ap3216c_of_match,
    },
    .probe    = ap3216c_probe,
    .remove   = ap3216c_remove,
    .id_table = ap3216c_id,
};
module_i2c_driver(ap3216c_driver);

MODULE_AUTHOR("Terry");
MODULE_DESCRIPTION("AP3216C ALS/PS/IR sensor driver");
MODULE_LICENSE("GPL");
