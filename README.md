# xw

基于 ns-3.33 的卫星网络仿真项目。当前主要开发分支是 `jsontopo`，主线实验位于 `examples/link-selection`，用于基于 JsonTopo 动态拓扑运行 `link-test`，并统计业务流性能。

## 项目结构

```text
xw/
├── README.md
├── VERSION
├── waf
├── wscript
├── docs/
│   └── dev-setup.md
├── examples/
│   ├── link-selection/
│   │   ├── link-test.cc
│   │   ├── topo.cc
│   │   ├── topo.h
│   │   ├── topo-json.cc
│   │   ├── topo-json.h
│   │   ├── topo-data.h
│   │   ├── para.cc
│   │   ├── para.h
│   │   ├── cluster.cc
│   │   ├── cluster.h
│   │   ├── access.cc
│   │   ├── access.h
│   │   ├── satrouting.cc
│   │   ├── satrouting.h
│   │   ├── wscript
│   │   ├── readme.txt
│   │   ├── topo(324).csv
│   │   ├── traffic_matrix(324).csv
│   │   └── Topodata/
│   │       ├── nodes_0s.json
│   │       ├── topology_0s.json
│   │       ├── nodes_5s.json
│   │       ├── topology_5s.json
│   │       ├── nodes_7s.json
│   │       ├── topology_7s.json
│   │       └── time_slices.json
│   └── sdn-controller/
├── legacy/
│   ├── runSim1.py
│   ├── runSim2.py
│   └── ospf.py
├── src/
│   └── cluster/
├── scratch/
└── build/              # 本地生成，不提交
```

关键目录：

```text
examples/link-selection/   当前主线实验：JsonTopo + link-test
examples/sdn-controller/   旧的 SDN/OpenFlow/OSPF 相关实验，暂时保留
legacy/                    旧实验批量运行脚本，暂时保留作参考
src/cluster/               项目自定义 ns-3 cluster 模块
docs/dev-setup.md          Ubuntu / VS Code / clangd 开发环境说明
scratch/                   临时实验代码
```

## 当前主线

当前推荐入口是：

```text
examples/link-selection/link-test.cc
```

相关文件：

```text
examples/link-selection/topo.cc        拓扑构建与动态更新
examples/link-selection/topo-json.cc   JsonTopo 文件解析
examples/link-selection/para.cc        默认实验参数
examples/link-selection/wscript        link-test 构建目标
examples/link-selection/readme.txt     link-test 细节说明
```

当前默认参数位于 `examples/link-selection/para.cc`：

```text
_useJsonTopo = true
offeredload = 0.0001
_tranProc = 0
linkBandwidth = 10000000000
totalTimeStep = 110
```

默认 JsonTopo 数据目录：

```text
examples/link-selection/Topodata/
```

默认流量矩阵：

```text
examples/link-selection/traffic_matrix(324).csv
```

## 从零部署

以下步骤假设使用 Ubuntu / WSL Ubuntu。

### 1. 安装系统依赖

```bash
sudo apt update
sudo apt install -y build-essential gcc g++ python3 python3-dev python3-setuptools \
  pkg-config sqlite3 libsqlite3-dev libxml2 libxml2-dev libgsl-dev \
  tcpdump doxygen graphviz valgrind clangd-15
```

当前项目不要求安装 Wireshark。

### 2. 克隆仓库

```bash
mkdir -p ~/project
cd ~/project
git clone git@gitee.com:manchuanjin/xw-clean.git xw
cd xw
```

切换到主开发分支：

```bash
git switch jsontopo
```

如果是第一次克隆，远程默认分支可能是 `main`，需要手动切到 `jsontopo`。

### 3. 创建 Python 环境

项目可以使用 uv 管理本地 Python 环境：

```bash
uv venv --python /usr/bin/python3
source .venv/bin/activate
```

也可以不激活环境，在每条 waf 命令前显式使用：

```bash
PATH="$PWD/.venv/bin:$PATH" ./waf ...
```

推荐使用第二种写法，命令更可复现。

## 构建

首次构建：

```bash
PATH="$PWD/.venv/bin:$PATH" ./waf configure --enable-examples --enable-tests
PATH="$PWD/.venv/bin:$PATH" ./waf build
```

如果修改了 `wscript`、模块依赖，或者迁移了目录后 waf 提示 lock file 无效，重新配置：

```bash
PATH="$PWD/.venv/bin:$PATH" ./waf configure --enable-examples --enable-tests
PATH="$PWD/.venv/bin:$PATH" ./waf build
```

如果需要完全清理后重建：

```bash
PATH="$PWD/.venv/bin:$PATH" ./waf clean
PATH="$PWD/.venv/bin:$PATH" ./waf configure --enable-examples --enable-tests
PATH="$PWD/.venv/bin:$PATH" ./waf build
```

构建成功后会生成：

```text
build/
build/compile_commands.json
```

这些文件是本地生成内容，不提交到 Git。

## 运行主线实验

快速自检：

```bash
PATH="$PWD/.venv/bin:$PATH" ./waf --run link-test
```

正常输出应包含：

```text
[RUN] 实验参数
[JSON-TOPO] 节点创建完成
[JSON-TOPO] 初始化完成
[TRAFFIC] 读取流量矩阵
Simulation real - time cost
业务数据性能
```

显式指定默认负载：

```bash
PATH="$PWD/.venv/bin:$PATH" ./waf --run "link-test --offeredload=0.0001"
```

可覆盖参数：

```text
--offeredload=<double>
--linkBandwidth=<bps>
--tranProtocol=<0|1>
--useJsonTopo=<true|false>
--nodesJson=<path>
--topologyJson=<path>
--timeSlicesJson=<path>
--isSate=<1|2|3|4>
--consType=<0|1>
```

示例：指定初始 JsonTopo 文件：

```bash
PATH="$PWD/.venv/bin:$PATH" ./waf --run \
  "link-test --nodesJson=examples/link-selection/Topodata/nodes_0s.json --topologyJson=examples/link-selection/Topodata/topology_0s.json"
```

示例：切回传统 CSV 拓扑模式：

```bash
PATH="$PWD/.venv/bin:$PATH" ./waf --run "link-test --useJsonTopo=false"
```

更多 `link-test` 运行细节见：

```text
examples/link-selection/readme.txt
```

## VS Code 开发

推荐从 WSL Ubuntu 中打开项目：

```bash
cd ~/project/xw
code .
```

推荐安装的 WSL 侧扩展：

```text
clangd
C/C++
Python
Pylance
ShellCheck
Markdown All in One
GitLens
Error Lens
```

C++ 跳转依赖 `clangd-15` 和 `build/compile_commands.json`。如果 Ctrl+Click 无法跳转：

```bash
sudo apt install -y clangd-15
PATH="$PWD/.venv/bin:$PATH" ./waf configure --enable-examples --enable-tests
PATH="$PWD/.venv/bin:$PATH" ./waf build
```

然后在 VS Code 执行：

```text
Developer: Reload Window
```

项目本地 VS Code 设置可以指向：

```text
/usr/bin/clangd-15
build/compile_commands.json
```

## 旧实验与保留内容

`legacy/` 中的脚本暂时保留，不作为当前主线入口：

```text
legacy/runSim1.py
legacy/runSim2.py
legacy/ospf.py
```

其中 `ospf.py` 以及 `examples/sdn-controller/ospf.cc` 先保留，后续确认用途后再决定是否整理或删除。

`examples/sdn-controller/` 是旧 SDN/OpenFlow/OSPF 实验目录，也暂时保留。

## 分支约定

远程仓库保留两个分支：

```text
main       稳定分支
jsontopo   当前主要开发分支
```

日常开发：

```bash
git switch jsontopo
git status
```

提交：

```bash
git add <files>
git commit -m "type: short description"
git push
```

同步远程更新：

```bash
git switch jsontopo
git pull --ff-only
```

## 生成文件

以下内容属于本地生成文件、缓存或仿真输出，不应提交：

```text
build/
.cache/
.waf3-*/
.lock-waf*
__pycache__/
*.tr
*.pcap
output/
output.txt
output1.txt
RlinkUtilization.csv
clusterNetworkInf.csv
adj_list.txt
no_cluster.txt
```

这些规则已经写入 `.gitignore`。

## 常见问题

### waf 提示 invalid lock file

通常是项目目录移动或 build 缓存记录了旧路径。重新配置即可：

```bash
PATH="$PWD/.venv/bin:$PATH" ./waf configure --enable-examples --enable-tests
```

### build 目录很大

`build/` 是 ns-3 编译产物，可能达到数 GB。它不提交到 Git，可以删除后重新构建：

```bash
rm -rf build
PATH="$PWD/.venv/bin:$PATH" ./waf configure --enable-examples --enable-tests
PATH="$PWD/.venv/bin:$PATH" ./waf build
```

### Gitee 提示存在大文件

当前仓库中 `examples/link-selection/traffic_matrix(324).csv` 约 86 MB。它是主线实验输入文件，目前保留在 Git 中。

