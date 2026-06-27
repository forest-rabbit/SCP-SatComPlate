# xw

基于 ns-3.33 的卫星网络仿真项目。当前主要开发分支是 `jsontopo`，主线实验位于 `examples/link-selection`，用于基于 JsonTopo 动态拓扑运行 `link-test`，并统计业务流性能。

数据说明：当前 `examples/link-selection/Topodata/` 中的 JSON 文件是为了调试 JsonTopo 流程自行生成的测试/示例数据，不是甲方提供的真实数据。后续接入甲方真实拓扑数据时，应将符合 JsonTopo 格式的节点、链路和时间片文件放入该目录，或通过 `--nodesJson`、`--topologyJson`、`--timeSlicesJson` 显式指定数据路径。

甲方数据建议放置与命名：

```text
examples/link-selection/Topodata/
├── nodes_0s.json        # 必需：初始节点状态
├── topology_0s.json     # 必需：初始链路状态
├── nodes_5s.json        # 可选：5s 节点状态更新
├── topology_5s.json     # 可选：5s 链路状态更新
├── nodes_10s.json       # 可选：10s 节点状态更新
├── topology_10s.json    # 可选：10s 链路状态更新
└── time_slices.json     # 可选：显式时间片索引
```

推荐命名规则：

```text
nodes_<time>s.json
topology_<time>s.json
```

其中 `<time>` 是仿真秒数，例如 `5s`、`7s`、`10s`。如果不提供 `time_slices.json`，程序会自动扫描 `Topodata/`，按上述命名规则配对时间片。

节点文件基本格式：

```json
{
  "nodes": [
    {
      "node_id": 0,
      "node_type": "sat",
      "is_cluster": 1,
      "cluster_id": 1,
      "is_cluster_head": 0
    }
  ]
}
```

链路文件基本格式：

```json
{
  "links": [
    {
      "node1_id": 0,
      "node2_id": 1,
      "type": "sat",
      "delay": 8000,
      "link_bandwidth": 10000000,
      "link_load_up": 0,
      "link_load_down": 0
    }
  ]
}
```

字段约定：

```text
node_id             节点 ID。链路中的 node1_id/node2_id 应引用这里的 node_id
node_type           节点类型，推荐使用 sat 或 ground/gs
is_cluster          是否参与分簇，0 或 1
cluster_id          簇编号
is_cluster_head     是否为簇首，0 或 1
node1_id/node2_id   链路两端节点 ID
type                链路类型，例如 sat、feeder、ground
delay               链路时延，单位为微秒 us
link_bandwidth      链路带宽，单位为 kbps
link_load_up        上行负载，单位为 kbps，可填 0
link_load_down      下行负载，单位为 kbps，可填 0
```

运行期的 `nodes_*.json` 用于更新已有节点的簇信息；当前代码不支持在仿真运行中途通过时间片新增 ns-3 节点。

## 项目结构

```text
xw/
├── README.md                    # 项目总说明：部署、构建、运行、目录和分支约定
├── VERSION                      # ns-3 版本号，当前为 3.33
├── waf                          # ns-3 waf 构建入口
├── wscript                      # ns-3 顶层构建脚本
├── docs/
│   └── dev-setup.md             # Ubuntu / VS Code / clangd 开发环境说明
├── examples/
│   ├── link-selection/
│   │   ├── link-test.cc         # 当前主线仿真入口，负责解析参数、构建应用、运行仿真和输出统计
│   │   ├── topo.cc              # 拓扑创建、链路安装、路由生成和 JsonTopo 时间片更新
│   │   ├── topo.h               # topo.cc 的接口声明
│   │   ├── topo-json.cc         # JsonTopo 节点、链路、时间片文件解析
│   │   ├── topo-json.h          # topo-json.cc 的接口声明
│   │   ├── topo-data.h          # JsonTopo 使用的数据结构定义
│   │   ├── para.cc              # 默认实验参数，例如负载、带宽、协议、是否启用 JsonTopo
│   │   ├── para.h               # 全局参数声明
│   │   ├── cluster.cc           # link-selection 实验中的分簇相关逻辑
│   │   ├── cluster.h            # cluster.cc 的接口声明
│   │   ├── access.cc            # 接入/辅助逻辑
│   │   ├── access.h             # access.cc 的接口声明
│   │   ├── satrouting.cc        # 卫星路由相关逻辑
│   │   ├── satrouting.h         # satrouting.cc 的接口声明
│   │   ├── wscript              # link-test 的 waf 构建脚本
│   │   ├── README.md            # link-test 运行细节说明
│   │   ├── topo(324).csv        # 传统 CSV 拓扑模式使用的拓扑输入
│   │   ├── traffic_matrix(324).csv # 当前实验默认读取的流量矩阵
│   │   └── Topodata/            # JsonTopo 示例数据：当前为自行生成的测试数据，不是甲方真实数据
│   │       ├── nodes_0s.json    # 0s 节点状态示例
│   │       ├── topology_0s.json # 0s 链路状态示例
│   │       ├── nodes_5s.json    # 5s 节点状态示例
│   │       ├── topology_5s.json # 5s 链路状态示例
│   │       ├── nodes_7s.json    # 7s 节点状态示例
│   │       ├── topology_7s.json # 7s 链路状态示例
│   │       └── time_slices.json # 可选时间片索引文件
│   └── sdn-controller/          # 旧 SDN/OpenFlow/OSPF 相关实验，暂时保留
├── archive/
│   ├── legacy-code/
│   │   ├── runSim1.py          # 旧批量运行脚本，暂不作为当前主线入口
│   │   ├── runSim2.py          # 旧批量运行脚本，暂不作为当前主线入口
│   │   └── ospf.py             # 旧 OSPF 批量运行脚本，暂时保留
│   └── local-output/      # 本地输出归档，不提交
├── src/
│   └── cluster/                  # 项目自定义 ns-3 cluster 模块
├── scratch/                      # 临时实验代码
└── build/                        # 本地构建产物，不提交
```

关键目录：

```text
examples/link-selection/   当前主线实验：JsonTopo + link-test
examples/sdn-controller/   旧的 SDN/OpenFlow/OSPF 相关实验，暂时保留
archive/                   暂不使用但保留的旧代码和本地输出
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
examples/link-selection/README.md     link-test 细节说明
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
examples/link-selection/README.md
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

`archive/legacy-code/` 中的脚本暂时保留，不作为当前主线入口：

```text
archive/legacy-code/runSim1.py
archive/legacy-code/runSim2.py
archive/legacy-code/ospf.py
```

其中 `ospf.py` 以及 `examples/sdn-controller/ospf.cc` 先保留，后续确认用途后再决定是否整理或删除。

`examples/sdn-controller/` 是旧 SDN/OpenFlow/OSPF 实验目录，也暂时保留。

本地旧输出统一放在：

```text
archive/local-output/
```

该目录已被 `.gitignore` 忽略，不会提交到远程仓库。

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
