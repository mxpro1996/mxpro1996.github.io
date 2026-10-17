---
title: "[WIP] IMHEX 反编译+Pattern匹配"
date: 2026-10-09T10:24:58+08:00
draft: false
---

平时拿 IMHEX 就当 010 Editor 用，看看 hex 写写 hexpat，反编译功能压根没点开过。

前两天拆 MBN，随手试了下还行。复杂函数该回还是得回，不指望它做深度分析。Pattern 那边倒是本来就在用。语法像 C 结构体，写二进制解析挺顺手。

```cpp
#pragma endian little

struct MbnHeader {
    u32 code_word;       // 0x00
    u32 magic;           // 0x04
    u32 image_type;      // 0x08
    u8  _pad[0x08];      // 0x0C - 0x13
    u32 header_size;     // 0x14
    u32 load_addr;       // 0x18
    u32 body_size;       // 0x1C
    u32 code_size;       // 0x20
    u32 sig_addr;        // 0x24
    u32 sig_len;         // 0x28
    u32 cert_addr;       // 0x2C
    u32 cert_len;        // 0x30
};

MbnHeader mbn @ 0x00;
```
| 偏移 | 字段 | 备注 |
|---|---|---|
| `0x00` | CodeWord | `D1 DC 4B 84` |
| `0x04` | Magic | `34 10 D7 73` |
| `0x08` | Image Type | `0x0D` eHostDL，`0x15` SBL1，`5A 43 0B 7D` 说明真头在前面（常见 `0x2800`），`FF FF FF FF` 特殊值 |
| `0x14` | Header Size | 一般 `0x50` |
| `0x18` | Loading Address | 执行起始地址 |
| `0x1C` | Body Size | Code + Signature + Cert 的总长，qboot 按这个读 |
| `0x20` | Code Size | |
| `0x24` | Signature Address | |
| `0x28` | Signature Length | 通常 256 |
| `0x2C` | Cert Store Address | 加载后的地址，不是文件偏移 |
| `0x30` | Cert Store Length | |

参考：
- <https://github.com/TalAloni/QualcommSecureBoot-SecondaryExecutableVerifier>
- <https://docs.werwolv.net/pattern-language/>
