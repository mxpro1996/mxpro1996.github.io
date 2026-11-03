---
title: "压榨SoC：无存储设备的价值"
date: 2026-11-03
tags: ["UEFI", "EDL", "Firehose", "RTOS", "SDCC", "裸机", "Lumia"]
categories: ["嵌入式", "折腾"]
---

> 死关羽可怕，活关羽也可怕，死了又活更可怕。

一台彻底死掉的设备不可怕——扔就完了。一台活着的设备也不可怕——正常用。**可怕的是那种半死半活的设备**：屏幕亮着，USB枚举着，但eMMC/UFS被你一风枪吹没了，焊盘还掉了两片。它看起来死了，实际上还活着。它看起来活着，实际上跑不了OS。

这种设备最讨厌，也最有意思。


## 一、先拆件，再谈技术

风枪温度没控好，焊盘掉一片，eMCP没换上去，LPDDR和Flash一起归零，机器变砖。

大多数人这时候会想扔。但扔之前—— **先拆件。**

屏幕总成、听筒、喇叭、振动马达、摄像头模组、天线、卡槽、外壳卡扣。Lumia那一代机器的外壳和屏幕做工不差，同型号备用机总有需要续命的。整机挂闲鱼不如拆件回血。

但如果你的目标不是修，而是折腾，那就别急着拆。先确认一件事：

**SoC还活着吗？**

电源管理还在，USB还能枚举，SoC没死，这台“无存储设备”就还有第二条命。

## 二、本质是单片机

没有DDR没有Flash，就觉得它废了。

把SOC的壳剥掉看看：

- 几个CPU核心（Cortex-A7/A53/A72）
- 片上SRAM：IMEM、IRAM、TCM，几百KB量级
- 总线、中断控制器、时钟、电源管理
- 外设：SDC、USB、UART、SPI、I2C、GPIO、定时器

**去掉DDR和Flash，它就是一颗IRAM比STM32H7还大的单片机。**

八核A53当单片机跑Zephyr，浪费吗？浪费。但如果你手上只有这块板子，浪费也是一种用法。至少它比Nucleo能干的事多：USB Host/Device、显示、音频、外挂SPI Flash或者SD卡跑个简单文件系统。

**死掉的设备不可怕，它只是变成了别的设备。**

## 三、EDL/SD Boot：换个思路

问题来了：没有Flash，怎么把代码放进SoC的SRAM里跑？

答案是EDL和SD卡启动。高通的BootROM在eMMC/UFS不可用时，仍然可以通过USB进EDL模式，加载Firehose programmer到SRAM，然后用peek/poke读写内存。

但bkerler的`edl.py`是为“有存储的设备”设计的，直接拿来用会卡住。

### 3.1 绕过`configure()`

`edl.py`连接设备后调`configure()`枚举存储类型、读分区表。没有eMMC/UFS的设备，这一步会超时或失败。

改法：加一个`--no-storage`参数，检测到存储枚举失败就直接进`peek/poke`模式。或者更粗暴——`configure()`里加个try/except，异常了就当没有存储继续往下走。

### 3.2 修订`peek/poke`的XML报文

`peek/poke`听起来很爽，但**SRAM容量是硬限制**。有时候peek最大只能0x32字节（不是0x320，就是0x32），这是因为：

- Firehose programmer在SRAM里运行，自身占用一部分；
- peek/poke的XML报文和数据缓冲区都要从SRAM里分配；
- SRAM剩下多少，决定了一次peek/poke能搬多少字节。

**0x32字节一次，意味着你不能一口气dump PBL，也不能一次性把整段payload写进SRAM。**

这时候要做**流式分段**：

```python
# 伪代码：分段peek
def stream_peek(edl, start_addr, total_len, chunk=0x32):
    buf = bytearray()
    for offset in range(0, total_len, chunk):
        n = min(chunk, total_len - offset)
        data = edl.peek(start_addr + offset, n)
        buf += data
        print(f'\r{offset}/{total_len}', end='')
    return bytes(buf)
```

### 3.3 跳过`storageInit`

**unfused设备**（工程机或者没熔断eFuse的机器）：Firehose programmer本身可以改。找到对应芯片的programmer（`.mbn`文件），二进制编辑器打开，找到`storageInit`相关的字符串或函数调用，patch掉。`edl.py`的`--skipStorageInit`参数在某些版本上不好用——programmer内部还是会尝试初始化存储，失败后才返回。直接patch programmer更彻底。

**fused设备**（零售机，eFuse已熔断）：programmer是签名的，改不了。这时候只能找programmer里的“任意执行”BUG——某个命令处理畸形输入时跳转到可控地址，把payload注入进去。这类BUG在高通Firehose实现里不是没有，但需要具体版本具体分析。
