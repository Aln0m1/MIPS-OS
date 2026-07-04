# MOS 操作系统实验 — Solutions

本项目是 **MIPS OS (MOS)** —— 一个运行在 MIPS Malta 模拟器（QEMU）上的教学操作系统的实验实现代码。

## 项目结构

```
├── kern/               # 内核核心
│   ├── env.c           # 进程（Env / Environment）管理 — 创建、分配 ASID、加载 ELF
│   ├── pmap.c          # 物理内存管理 — 页表、页控制块、TLB 重填
│   ├── sched.c         # 调度器 — Round-Robin 时间片轮转
│   ├── traps.c         # 异常/中断向量表
│   ├── syscall_all.c   # 系统调用实现（putchar, mem_map, exofork, ipc 等）
│   ├── mfutex.c        # 内核态 futex 支持（MFUTEX_WAIT / MFUTEX_WAKE）
│   ├── tlbex.c         # TLB 异常处理
│   ├── genex.S / tlb_asm.S / env_asm.S / entry.S  # 汇编级异常/上下文切换
│   └── machine.c / printk.c / panic.c
│
├── user/               # 用户态程序和库
│   ├── sh.c            # 简易 Shell
│   ├── init.c / icode.c / halt.c / idle.c  # 系统初始进程
│   ├── echo.c / cat.b / ls.c / num.c       # 基础命令
│   ├── fstest.c        # 文件系统功能测试
│   ├── fktest.c        # fork 功能测试
│   ├── testpipe.c / testpiperace.c / testfdsharing.c  # 管道/文件描述符测试
│   ├── pingpong.c      # IPC ping-pong 测试
│   ├── tltest.c        # 用户态线程（mthread）测试
│   ├── devtst.c        # 设备读写测试
│   ├── lib/            # 用户态库
│   │   ├── fork.c / ipc.c / pipe.c / spawn.c    # 进程/IPC 原语
│   │   ├── file.c / fd.c / fsipc.c              # 文件描述符 & 文件系统 IPC
│   │   ├── mthread.c                            # 用户态线程库（基于 exofork）
│   │   ├── syscall_wrap.S / syscall_lib.c       # 系统调用封装
│   │   ├── libos.c / console.c / fprintf.c      # C 运行时支持
│   │   └── entry.S / debugf.c / pageref.c / wait.c
│   └── bare/          # 无 OS 支持的裸机程序（loop.S, put_a/b, overflow.S）
│
├── fs/                 # 文件系统服务
│   ├── fs.c            # 文件系统核心操作（read/write/close/dir_lookup…）
│   ├── serv.c          # 文件系统服务进程（serve_* 请求处理）
│   ├── test.c          # 文件系统测试
│   └── format.c / include.mk
│
├── include/            # 内核公共头文件
│   ├── env.h / pmap.h / mmu.h / trap.h / sched.h  # 核心数据结构
│   ├── syscall.h / mfutex.h / error.h             # 系统调用号 & 错误码
│   ├── types.h / queue.h / bitops.h / string.h    # 基础类型 & 工具
│   ├── elf.h / kclock.h / malta.h / io.h          # 硬件抽象
│   └── machine.h / printk.h / print.h / stackframe.h
│
├── lib/                # 内核库
│   ├── string.c / print.c / elfloader.c
│   └── Makefile
│
├── init/               # 内核启动入口
│   └── start.S / init.c / Makefile
│
├── tests/              # 各 Lab 自动化测试
│   ├── lab1_2 / lab2_1~4 / lab3_1~4 / lab4_1~7 / lab5_1~5 / lab6_1~2
│   └── 每个子目录包含 kernel.mk 和测试源文件
│
├── tools/              # 宿主机工具
│   ├── fsformat.c      # 磁盘镜像生成工具
│   ├── bintoc.c        # 二进制转 C 数组
│   ├── readelf/        # 简易 ELF 解析器
│   └── run_bg.sh / init-gen
│
├── mk/                 # 构建系统片段
│   ├── tests.mk / profiles.mk
│
├── questions           # 2026 文件系统挑战性任务说明书（完整题目）
├── Makefile            # 顶层构建（qemu run/debug/test）
├── kernel.lds          # 内核链接脚本
├── g1.sh / g2.sh       # Git 快速切换/拉取脚本
└── test.c              # Linux 本地 fork 测试（宿主机用，非 MOS 代码）
```

## 构建 & 运行

依赖 `mips-linux-gnu-gcc` 交叉编译器链和 `qemu-system-mips`。

```bash
# 编译全部（Lab=6）
make all

# 指定 Lab 编号编译
make lab=6_2 all

# 运行
make run

# 以特定 Lab 测试镜像运行
make lab=6_2 test run
make lab=5_5 test run

# 调试（QEMU + GDB）
make dbg_run      # 启动 QEMU 等待 GDB 连接
make dbg          # 自动连接 GDB

# 连接 QEMU 串口
make connect
```

## 技术要点

- **架构**：MIPS32 (4Kc)，QEMU malta 板卡，64MB RAM
- **内存管理**：3 级页表 + TLB 软件重填，支持 ASID 复用
- **进程模型**：`struct Env` 表示进程/线程（通过 `exofork` 创建线程），支持线程组（`env_tgid`）
- **调度**：Round-Robin，基于 `env_pri` 时间片
- **同步原语**：内核态 futex（`mfutex`）+ 用户态 `mthread` 线程库（互斥锁 `mthread_mutex_t`）
- **IPC**：基于共享内存的消息传递，支持同步收发
- **文件系统**：简单磁盘 FS（inode 式 `struct File`，直接块 + 间接块），通过 FS 服务进程访问
- **系统调用**：约 30 个（内存映射、进程控制、IPC、设备读写、futex 等）
