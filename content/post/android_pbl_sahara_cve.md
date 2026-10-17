---
title: "CVE-2026-25262 （本文待修订）"
date: 2026-10-17T14:30:00+08:00
draft: false
tags: ["Qualcomm", "Sahara", "BootROM", "CVE", "Secure Boot", "逆向"]
categories: ["安全研究", "固件分析"]
summary: "高通 Legacy-Sahara 处理ELF镜像，签名段存在的任意写。"
---

## 概述

CVE-2026-25262 是高通 Sahara 下载协议在 BootROM 阶段的一个逻辑缺陷。核心问题在于 `sahara_handle_elf_image()` 对"证书段"（cert-segment）的验证流程存在**判定条件写反**与**地址重定向不校验**两个问题叠加，导致攻击者可以在不通过哈希认证的情况下，把任意数据写入 BootROM 认可的物理地址，并通过改写函数表、页表与 ELF 入口劫持执行流。

本文基于公开技术笔记整理，涵盖受影响平台、漏洞根因、三段式利用链与 BootROM 内存布局参考。

## 受影响设备

| 平台系列 | 具体型号 |
|---|---|
| MDM 系列 | MDM9x07、9x45、9x65 |
| MSM 系列 | MSM8909、8916、8939、8952 |
| SDX 系列 | SDX50 |
| 其他 | MSM8612（待确认） |

上述平台的 BootROM 在 Sahara 协议处理 ELF 镜像时共享同一套证书段验证逻辑，因此受同一缺陷影响。

## 背景：Sahara 协议与证书段

Sahara 是高通 BootROM 对外提供的一种 USB 下载协议，用于在设备进入 EDL（Emergency Download）模式后加载 Firehose loader 或引导镜像。其基本流程是：

1. 主机与设备握手，协商版本与模式；
2. 设备进入 `image_tx` 状态，主机发送 ELF 格式的引导镜像；
3. BootROM 解析 ELF program header，逐段校验并加载；
4. 校验通过后跳转到镜像入口。

在高通的设计中，ELF 的 program header 里有一个特殊段被称为**证书段**（cert-segment），通过 `p_flags` 的 `PF_MASKOS & 2` 标记识别。证书段同时承载两项职责：

- **证书**：用于签名认证（`pbl_hashAuth`）；
- **哈希表**：用于后续数据段的哈希比对（`pbl_hashCheck`）。

正常流程下，BootROM 先定位证书段、认证证书，再加载其他段，最后用证书段中的哈希表校验所有数据段。**证书段是整个信任链的锚点**，一旦它的地址或内容被操纵，后续校验就形同虚设。

## 漏洞根因

### 1. `sahara_handle_elf_image()` 的判定条件写反

原始逻辑中，BootROM 会先定位证书段，然后对其做地址校验：

```c
if (!ctx->ops->validate_address_for_certSeg(ctx->target_paddr, padded_filesz)
    && ctx->target_paddr == 0)
    return 38;
```

这里用的是 **`&&`** 而不是 **`||`**。设计意图是：如果证书段的地址校验失败，**或者**目标地址为 0（跳过段 0 的非法 phdr），就返回错误。但写成 `&&` 后，只有**两个条件同时成立**才会报错。

这意味着：

- 只要 `target_paddr != 0`，**即使 `validate_address_for_certSeg` 返回 0（校验失败），也不会返回错误**；
- 流程继续往下走，直接进入 `sahara_read_usb_bulk()`，把主机数据写到 `target_paddr`。

换句话说，**证书段的地址校验被完全绕过**，攻击者可以把证书段写到任意非零物理地址。

### 2. `validate_address_for_certSeg()` 的地址重定向

再看地址校验函数本身：

```c
int validate_address_for_certSeg(_DWORD *p_vaddr, unsigned int size)
{
  int ret = 0;
  const int PREADDR = 0x20963C;

  if ( PREADDR + size < 0xfffffff ){
    if ( p_vaddr && size <= 0xEFFE ){
      ret = 1;
      *p_vaddr = PREADDR;   // 强制重定向
    }
  }
  return ret;
}
```

这个函数的设计意图是：**把证书段强制重定向到 BootROM 内部的 `0x20963C`**，并且限制大小不超过 `0xEFFE`（约 60KB），防止证书段过大覆盖关键区域。

但结合上一条的 `&&` 缺陷，实际效果变成：

- 攻击者提供的 `target_paddr` 只要非零，**这个重定向根本不会发生**；
- 数据被写到攻击者指定的地址，而不是 `0x20963C`；
- `size <= 0xEFFE` 的限制虽然还在，但 4byte->60KB 足够覆盖函数表、页表等关键结构。

## 三段式利用链
```text
Sahara 握手（以8939/16为例）
  │
  ├─ Stage1: 写 ops 表到 0x08003014
  │    ├─ 收到 Discontinued-RX (realsize<60KB)
  │    ├─ 发送 cmd_reset()
  │    └─ 进入被 hook 的 rst_cb1() -> 重放sahara_handle_elf_image
  │
  ├─ Stage2: 构造证书段绕过 + MMU （或利用WARM_REBOOT的0x01940000）
  │    ├─ phdr[1].p_vaddr = 0x00200200, 写入 0x08000C0E
  │    ├─ 追加 ops2 段，覆盖 0x08003014
  │    ├─ 追加空段，触发 nextRound
  │    └─ TLB_Flush + D-Cache clean
  │
  └─ Stage3: 跳转 payload
       ├─ BLX 0x08005050
```

## 参考

- 技术笔记原文：`CVE-2026-25262.txt`（Revise 26.09.26）
- ARM Architecture Reference Manual, ARMv7-A/R, Short-descriptor translation table format
- Qualcomm Sahara Protocol Documentation（公开版本）

---

*本文基于公开技术笔记整理，仅用于安全研究与防御目的。请勿将所述技术用于未经授权的设备。*