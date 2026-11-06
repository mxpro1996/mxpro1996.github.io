---
title: "基于DiskGUID的改机检测"
date: 2026-11-06
tags: ["GPT", "刷机", "MTK"]
description: "刷完机功能莫名失效，签名查半天没毛病。问题出在重建 GPT 那一步。"
---

刷机老流程：读 `gpt_main0.bin` → 转 `partition.txt` → 改大小 → 重建 → 刷回。系统起来了，扩容也生效了，看着没事。\
过两天某个存储绑定的功能挂了，签名查半天没毛病——重建 GPT 那一步，工具默认换了新的 DiskGUID。

## DiskGUID 怎么丢的

`partition.txt` 只描述分区，不描述磁盘，没地方放 DiskGUID。重建只能新建表，新建就随机化。`sgdisk`、`parted`、一键脚本默认都这样，除非显式 `--disk-guid=<原值>`。主备分别生成还会不一致，严格实现直接判损坏。

MTK 更明显：scatter 是纯文本，没有 DiskGUID 字段，SP Flash 每次格式化线刷都临时建表。

**MTK 上 DiskGUID 变了 ≈ 设备被线刷过**，信噪比比高通干净得多。
 
### 翻车场景

**只备份布局**：导出 `/etc/partitions` 写进 scatter.txt 去刷。

**语义丢失**: 备份了gpt分区头，但是扩容/全格刷机过程中忘记guid被滚动了，没有保留原guid中招。

## 拿它做检测

只绑 DiskGUID 没用，`sgdisk -U <原值>` 就绕了。得把身份、布局、语义一起绑：

```
expected = H(
    disk_guid
    || first_usable_lba || last_usable_lba
    || num_partition_entries
    || sort(每条: type_guid || unique_guid || start_lba || end_lba || attributes || name)
)
```

## 附录
```c
#pragma pack(push, 1)

typedef struct {
    uint8_t  signature[8];        // "EFI PART"
    uint32_t revision;
    uint32_t header_size;         // 92
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t my_lba;              // 1
    uint64_t alternate_lba;       // 末尾 LBA
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t  disk_guid[16];       // 0x38
    uint64_t partition_entry_lba; // 2
    uint32_t num_partition_entries;
    uint32_t size_of_partition_entry;   // 128
    uint32_t partition_entry_array_crc32;
/*
字段分四类：
- **身份**：`disk_guid`、`unique_guid`
- **布局**：`first/last_usable_lba`、`starting/ending_lba`、`num_partition_entries`
- **语义**：`type_guid`、`name`、`attributes`
- **CRC**：只防损坏，改完重算就对了，别当篡改检测
*/
} gpt_header_t;                   // 92 字节

typedef struct {
    uint8_t  type_guid[16];
    uint8_t  unique_guid[16];
    uint64_t starting_lba;
    uint64_t ending_lba;
    uint64_t attributes;
    uint16_t name[36];            // 72 字节 UTF-16LE
} gpt_entry_t;                    // 128 字节
#pragma pack(pop)
// 主表在 LBA 1，条目从 LBA 2 起，末尾一份备份。ImHex 直接看：

GPT_Header header @ 0x200;   // disk_guid 在 0x238
```
