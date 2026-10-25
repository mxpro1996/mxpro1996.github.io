---
title: "Git 操作笔记"
date: 2026-10-25T10:00:00+08:00
draft: false
tags: ["Git", "Tips", "Shallow Clone", "Rebase", "分支管理", "历史重写"]
categories: ["开发笔记"]
---

## 1. 按需拉取特定 Commit (Shallow Clone)

通过 `--depth 1` 限定 shallow clone，只 clone 相关的一级深度，这样就可以大大加快 clone 的速度。

```bash
git init
git remote add origin <url>
git fetch --depth 1 origin <sha1>
git checkout FETCH_HEAD
```

> **提示**：这种方式非常适合只需要查看某个特定 commit 代码，且不需要完整历史记录的场景。`FETCH_HEAD` 会指向你刚刚拉取的那个特定 `<sha1>`。

---

## 2. 浅克隆并包含 Submodules

另：如果有仓库 sub modules，还可以用 shallow clone 的方式一并 clone sub modules：

```bash
git clone --branch <sha1/branch/tag> --recursive -j$(nproc) --depth 1 <git repo>
```

**参数解释**：
- `--recursive` (或 `--recurse-submodules`)：递归克隆子模块。
- `-j$(nproc)`：利用 CPU 核心数并行下载子模块，极大提升多子模块仓库的克隆速度（`$(nproc)` 为 Linux 环境下获取 CPU 核心数的命令）。
- `--depth 1`：仅拉取最近一次 commit 的深度。

---

## 3. 进阶：使用 `rebase --onto` 精准迁移提交

`git rebase --onto` 是 Git 中用于**修正分支依赖关系**的利器。当你发现某个功能分支错误地基于了另一个功能分支（而非主分支）时，可以用它将一串提交“裁剪”并“重新嫁接”到正确的基础上。

### 3.1 问题场景

假设 `master` 分支位于提交 `X`，你基于 `master` 创建了 `quickfix1` 分支，并提交了 `q1a`、`q1b`。随后，你又错误地基于 `quickfix1` 创建了 `quickfix2` 分支，并提交了 `q2a`、`q2b`。此时分支结构如下：

```text
o-o-X (master HEAD)
     \
      q1a--q1b (quickfix1 HEAD)
               \
                q2a--q2b (quickfix2 HEAD)
```

问题在于，`quickfix2` 包含了本不属于它的 `quickfix1` 的提交，需要将其修正为基于 `master` 分支。

### 3.2 核心命令与语义

```bash
git rebase --onto <newbase> <upstream> <branch>
```

语义为：将 `<branch>` 分支中，**从 `<upstream>` 之后**开始的所有提交，重新基于 `<newbase>` 进行重放。

### 3.3 解决上述场景的步骤

```bash
# 1. 切换到目标基础分支（新基底）
git checkout master

# 2. 执行 rebase --onto
# 把 quickfix1 之后（即 q2a, q2b）的提交，重新基于 master 重放
git rebase --onto master quickfix1 quickfix2
```

执行后，Git 会定位 `quickfix1..quickfix2` 之间的提交（`q2a`、`q2b`），然后将它们基于 `master` 重新应用，生成新的提交 `q2a'`、`q2b'`。最终分支结构变为：

```text
        q2a'--q2b' (quickfix2 HEAD)
       /
o-o-X (master HEAD)
     \
      q1a--q1b (quickfix1 HEAD)
```

### 3.4 与 `cherry-pick` 的对比

| 方案 | 命令示例 | 优点 | 缺点 |
| :--- | :--- | :--- | :--- |
| **`rebase --onto`** | `git rebase --onto master quickfix1 quickfix2` | **保持历史完整性**，保留提交元数据，可处理复杂依赖 | 会改写提交历史，仅适用于本地私有分支 |
| **逐个 cherry-pick** | `git cherry-pick q2a q2b` | 简单直接，适合少量提交 | **丢失原始元数据**，无法处理复杂依赖关系 |
| **范围 cherry-pick** | `git cherry-pick quickfix1..quickfix2` | 语法简洁 | 无法指定新的基础分支，功能有限 |

> **结论**：在需要保持历史完整性、处理复杂依赖关系并指定新基础分支的场景下，`rebase --onto` 是更推荐的选择。

---

## 4. 高级历史重写：Orphan 抹除 Root + `commit-tree` 嫁接

在某些极端场景下（例如开源前清理内部历史、或者将现有代码无缝嫁接到一个完全不同的历史基线上），我们需要彻底抹除最初的 Root Commit，甚至伪造一个新的历史起点。

### 4.1 抹除 Root 提交 (Orphan)

`git checkout --orphan <new_branch>` 可以创建一个没有任何父提交的新分支，但会保留当前工作区的文件。

```bash
# 创建一个全新的无历史分支
git checkout --orphan clean-history

# 此时所有文件都在暂存区，你可以选择全部提交，或者只提交部分文件
git commit -m "Initial commit (Clean history)"

# 删除原来的 master 分支（如果新分支要作为主分支）
git branch -D master
git branch -m master
```

### 4.2 使用 `commit-tree` 手动嫁接历史

如果你不想完全丢弃历史，而是想**把现有的某一段历史，嫁接到另一个完全不同的提交上**（这相当于以前修改 `.git/info/grafts` 文件的现代做法），可以使用 `commit-tree` 配合 `git replace --graft`。

```bash
# 场景：想把当前 HEAD 的父节点替换为另一个 <new-parent-sha>
# 1. 获取当前 HEAD 的 tree 对象
tree_sha=$(git rev-parse HEAD^{tree})

# 2. 用该 tree 和新的父提交，伪造一个新的 commit 对象
new_commit=$(echo "Grafted commit" | git commit-tree $tree_sha -p <new-parent-sha>)

# 3. 将当前分支重置到这个伪造的新提交上
git reset --hard $new_commit
```

更简单、非破坏性的方式（Git 1.6.5+）是使用 `git replace --graft`：

```bash
# 将 <commit> 的父节点替换为 <new-parent>，而不改变原本的 SHA
git replace --graft <commit> <new-parent>
# 如果想让改变永久生效，需要执行：
git filter-repo --force  # 或者旧版的 git filter-branch
```

---

## 5. 全新起点：Orphan 分支 Clean Start

如果你接手了一个项目，想要**完全丢弃所有黑历史**，仅保留当前状态的代码，作为一个全新仓库的开端，Orphan 分支是最干净的做法。

### 操作步骤

```bash
# 1. 创建并切换到一个没有任何历史记录的孤儿分支
git checkout --orphan clean-start

# 2. 将所有文件添加到暂存区
git add -A

# 3. 创建第一个提交（新的 Root）
git commit -m "chore: Initial clean commit"

# 4. 删除原来带有历史记录的分支（假设叫 main）
git branch -D main

# 5. 将当前孤儿分支重命名为 main
git branch -m main
```

> **注意**：如果你的仓库里包含了很多不需要的编译产物或临时文件，建议在 `git add -A` 之前，先执行 `git rm -rf --cached .` 清空暂存区，再重新 `git add` 需要的文件。

---

## 6. 交互式变基：`rebase -i HEAD~N` 整理提交

`git rebase -i` 是日常开发中最常用的历史整理工具，它允许你在推送代码前，对最近的 N 个提交进行合并、修改、排序或删除。

### 6.1 基本用法

```bash
# 整理最近 3 个提交
git rebase -i HEAD~3

# 或者指定某个 commit 之后的所有提交
git rebase -i <commit-sha>
```

执行后，Git 会打开一个文本编辑器，列出相关的提交列表：

```text
pick 1a2b3c4 feat: 添加登录页面
pick 5d6e7f8 fix: 修复按钮样式
pick 9g0h1i2 feat: 添加 API 请求
```

### 6.2 常用指令 (Commands)

修改 `pick` 为对应的指令即可操作：

| 指令 | 简写 | 作用 |
| :--- | :--- | :--- |
| **`pick`** | `p` | 保留该提交（默认） |
| **`reword`** | `r` | 保留提交，但修改提交信息（Message） |
| **`edit`** | `e` | 暂停变基，允许你修改该提交的文件内容（修改完后需执行 `git commit --amend` 和 `git rebase --continue`） |
| **`squash`** | `s` | 将该提交合并到上一个提交中，并允许你编辑合并后的提交信息 |
| **`fixup`** | `f` | 将该提交合并到上一个提交中，但**丢弃**该提交的信息（适合微小的拼写修复） |
| **`drop`** | `d` | 彻底删除该提交 |

### 6.3 经典场景：合并多余的提交

如果你在本地提交了 3 次，其实只是修复了一个 bug（`feat -> fix -> fix`），想合并成 1 个提交：

1. 执行 `git rebase -i HEAD~3`。
2. 将后两个 `pick` 改为 `squash` (或 `fixup`，如果你不需要它们的 commit message)。
3. 保存退出，Git 会提示你修改合并后的提交信息。
