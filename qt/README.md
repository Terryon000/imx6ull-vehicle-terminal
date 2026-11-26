# QT 应用移植说明

QT 座舱界面基于[正点原子开源车载终端项目](https://blog.csdn.net/black_sneak/article/details/131889750)(Linux + QT)移植改造。本仓库不包含原版 QT 源码,仅记录移植适配工作。

## 移植改动点

1. **平台插件**:正点原子 rootfs 环境 → 本板 linuxfb 直写 framebuffer + tslib 触摸校准
2. **环境固化**:9 条环境变量(XDG_RUNTIME_DIR、QT_QPA_PLATFORM=linuxfb、QT_QPA_FB_TSLIB=1、QT_QPA_FB_DISABLE_INPUT=1、QT_QPA_GENERIC_PLUGINS=tslib、TSLIB_* 系列)固化进 run.sh 启动脚本
3. **传感器对接**:AP3216C 路径对齐 `/sys/class/misc/ap3216c/{als,ps,ir}`(自写驱动挂 misc class,QT 原代码零修改)
4. **按键**:接入 input 子系统(官方 keys 节点),QT 经 QSocketNotifier 读事件
5. **裁剪**:在线功能(百度语音/地图/天气/音乐搜索,token 过期)注释,离线部分保留

## 运行

```sh
/root/vehicle/run.sh   # 环境变量 + 启动 VehicleTerminal
```
