# 设备树改动

> 基于韦东山 `100ask_imx6ull-14x14.dts`(内核 4.9.88 源码树)。

## 1. DHT11 节点(根节点下,platform 设备)

```dts
dht11 {
    compatible = "my,dht11";
    pinctrl-names = "default";
    pinctrl-0 = <&pinctrl_dht11>;
    dht11-gpios = <&gpio4 21 GPIO_ACTIVE_HIGH>;
    status = "okay";
};
```

- 引脚:GPIO4_IO21(J5 扩展口 pin15),正点原子原引脚 GPIO1_IO03 在本板未引出
- 匹配:compatible ↔ 驱动 `of_match_table`(`drivers/dht11/dht11_dri.c` 的 `DEV_DTS_COMPATIBLE` 宏),必须一致

## 2. AP3216C 节点(I2C1 子节点,i2c 设备)

```dts
&i2c1 {
    clock-frequency = <100000>;
    pinctrl-names = "default";
    pinctrl-0 = <&pinctrl_i2c1>;
    status = "okay";

    ap3216c@1e {
        compatible = "liteon,ap3216c";
        reg = <0x1e>;
    };
};
```

- 官方 dts 的 `&i2c1` 为空块,节点为新增;板载器件(wm8960/gt9xx)在 I2C2
- 地址:`reg = <0x1e>` 由 I2C 框架创建 `i2c_client`,`client->addr` 自动填充,驱动内不写死地址
- 总线注意:aliases 映射 `i2c0 → &i2c1`,AP3216C 在 `/dev/i2c-0`

## 部署

```sh
make dtbs
# dtb 部署到 boot 分区(/dev/mmcblk1p2)同名覆盖
# 验证:
ls /proc/device-tree/ | grep dht11
ls /proc/device-tree/soc/aips-bus@2100000/i2c@21a0000/   # 应见 ap3216c@1e
```
