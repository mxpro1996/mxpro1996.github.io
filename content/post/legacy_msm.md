---
title: "别了，Legacy QCOM/MSM（初稿）"
date: 2026-10-20
draft: false
tags:
  - Qualcomm
  - PBL
  - e=3
  - CVE-2026-25262
  - 安全启动
  - Legacy MSM
categories:
  - 安全研究
description: "高通 Legacy MSM 信任链的终章"
---

2026年，高通 Legacy MSM 的信任链，正在经历如同世纪末一般的告别。

## 一、Legacy MBN 的裂缝

高通安全启动的起点是 PBL——熔断在掩膜 ROM 中的第一行代码，出厂即固化，**已出货批次内不可修补**。\
(严格来说是RPM PBL和APPS PBL，类似电脑的EC和CPU主从协同关系）

两周前，专注于三星S5（MSM8974
设备）的研究者 Ryan（`@oscardagrach`）披露了 PBL 验签的结构性缺陷：它从 MBN 头部读取签名长度，**却不要求该长度等于公钥模数长度**。单独看只是“宽容”，但当叶子证书的公钥指数是 **e=3** 时，宽容变成致命。

攻击者声明一个短签名长度 `L`（如 48 字节，模数为 256 字节）。因 `s` 很短，`s³ < n`，**模约减不发生**，PBL 得到 `s` 的精确整数立方，再只比较前 `L` 字节。攻击从“在完整模数空间伪造 RSA 签名”降维成“在模 `2^(8L)` 环内求短立方根”——逐位提升，毫秒级完成，**不需要私钥**。

这一缺陷同时覆盖：\
**SBL1**（每次上电验证，伪造即持久化）\
**Sahara programmer**（EDL 模式 USB 下载，即最早执行权）。

高通 2017 年将密钥从 `e=3` 轮换到 `e=65537`，短签名窗口在数学上关闭。但携带 e=3 证书链的 Legacy 设备，缺陷永远留在硅片里。

## 二、各表一枝：ELF Write-What-Where

Kaspersky 在 Black Hat Asia 2026 披露 **CVE-2026-25262**：PBL 解析特制 ELF 时边界校验不足，允许**任意地址写任意数据**。影响 MDM9x07/9x45/9x65、**MSM8909/8916**、MSM8952、SDX50。

触发点在 Sahara 协议——运行在任何 OS、任何权限检查、任何安全机制之前。攻击者上传构造的 ELF，在 ring 0 等价特权下执行代码，**从下方撬开安全启动的根基**。

## 三、逐段崩溃的信任链

两条路径叠加，勾勒出一条逐代拆解的启动链：

- **Krait 时代**（MSM8974、APQ8064、MSM8612 等）：e=3 证书链最密集，一条旧固件证书 + 窗口内 `L`，毫秒构造伪造签名。
- **MSM8909/8916/8939**：两条路径交汇。
- **SDM625/632/660**：XBL/TZ 层级加深，CVE-2020-3619 等 TZ 非安全内存触碰可致提权。
- **SDM845**：CVE-2018-11951（XBL_LOADER 越权执行 ZI 清零）、XBL_SEC 缺锁定、TZ 竞态。攻击不再寻求伪造签名，而是寻找验证者自身的控制流缺口。

## 四、后记
**不能简单看“验证阶段有没有问题？”，不能被“看似什么安全检查都做了”蒙蔽。**\
我也曾尝试 e=3，只考虑了“padding 校验不严格的经典原像变种”，却没审计 **length**。PBL 的缺陷不在 padding 有无，而在**签名长度是否被约束**。“千里之堤溃于蚁穴”，PBL对`mbn->cert_len`的盲目信任，致使完备的sboot机制就此崩塌。

本文不是漏洞清单，是**信任链的安全报告**。PBL 的 Mask ROM 属性决定了，这些Legacy MBN分支机器无路可逃。

高通轮换了密钥、修补了可更新逻辑。但维修店里的旧手机、车载系统、工业 IoT 节点——**信任链的根基已经松动**。不是设备被淘汰了，而是那条启动链，从 PBL 第一行代码开始，就从未像人们以为的那样完整。

**别了，Legacy QCOM/MSM。**

---

## 致谢

1. [A Full-Chain Exploit of an Unfused Qualcomm Device](https://hhj4ck.github.io/qualcomm/secboot-off-qcm2150.html) — [hhj4ck](https://github.com/hhj4ck)

2. [Exploiting Qualcomm EDL Programmers (1): Gaining Access & PBL Internals](https://alephsecurity.com/2018/01/22/qualcomm-edl-1/) — [Aleph Security](https://alephsecurity.com/)

3. [qualcomm_pbl_siglen_bug](https://github.com/oscardagrach/qualcomm_pbl_siglen_bug) — [oscardagrach](https://github.com/oscardagrach)

4. [QualcommSecureBoot-SecondaryExecutableVerifier](https://github.com/TalAloni/QualcommSecureBoot-SecondaryExecutableVerifier) — [Tal Aloni](https://github.com/TalAloni)

5. CVE-2026-25262 report PPT — Kaspersky
