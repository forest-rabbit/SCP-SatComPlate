link-test 运行说明（JsonTopo 版本）

1) 入口文件
  - 主程序：examples/link-selection/link-test.cc
  - 拓扑构建：examples/link-selection/topo.cc
  - JSON拓扑解析：examples/link-selection/topo-json.cc
  - 全局参数：examples/link-selection/para.cc
  - waf目标：link-test

2) 当前推荐运行方式
  在仓库根目录执行：

    PATH="$PWD/.venv/bin:$PATH" ./waf --run link-test

  当前 para.cc 中默认启用 JsonTopo：

    _useJsonTopo = true
    offeredload = 0.0001
    _tranProc = 0  // UDP
    linkBandwidth = 10000000000  // 10Gbps
    totalTimeStep = 110

  这组默认参数已经可以作为快速自检配置。实测输出应包含：

    [JSON-TOPO] 节点创建完成
    [JSON-TOPO] 初始化完成
    [TRAFFIC] 读取流量矩阵
    Simulation real - time cost

3) 首次构建
  如果 build 目录不存在，或者修改了 waf / wscript / 模块依赖，可以重新配置：

    PATH="$PWD/.venv/bin:$PATH" ./waf configure --enable-examples --enable-tests
    PATH="$PWD/.venv/bin:$PATH" ./waf build

  如果需要完全清理后重建：

    PATH="$PWD/.venv/bin:$PATH" ./waf clean
    PATH="$PWD/.venv/bin:$PATH" ./waf configure --enable-examples --enable-tests
    PATH="$PWD/.venv/bin:$PATH" ./waf build

4) 可覆盖的命令行参数
  - --offeredload=<double>
      业务负载。当前快速验证推荐使用默认值 0.0001。
  - --linkBandwidth=<bps>
      链路带宽；当 JSON 链路未写带宽时作为兜底值。
  - --tranProtocol=<0|1>
      0=UDP，1=TCP。
  - --useJsonTopo=<true|false>
      是否使用 JsonTopo。默认 true。
  - --nodesJson=<path>
      初始节点 JSON 文件；默认 examples/link-selection/Topodata/nodes_0s.json。
  - --topologyJson=<path>
      初始链路 JSON 文件；默认 examples/link-selection/Topodata/topology_0s.json。
  - --timeSlicesJson=<path>
      时间片索引 JSON 文件；不指定时默认扫描 examples/link-selection/Topodata/。
  - --isSate=<1|2|3|4>
      传统拓扑模式使用；JsonTopo 模式下不决定节点数量。
  - --consType=<0|1>
      传统拓扑模式使用；0=Walker Star，1=Walker Delta。

5) JsonTopo 输入文件
  默认目录：

    examples/link-selection/Topodata/

  默认初始拓扑：

    nodes_0s.json
    topology_0s.json

  当前会自动扫描并加载后续时间片，例如：

    nodes_5s.json
    topology_5s.json
    nodes_7s.json
    topology_7s.json

  运行时日志会显示实际加载的时间片和文件路径。

6) 流量输入
  热点流量模式下（_trafficMode=0），程序读取：

    examples/link-selection/traffic_matrix(324).csv

  当前 JsonTopo 默认节点规模为 66 颗卫星和 10 个地面节点。流量矩阵按卫星业务源宿关系读取。

7) 主流程
  (1) 解析命令行参数并打印关键配置。
  (2) 调用 initTopo() 创建节点、安装协议栈、创建链路并分配地址。
  (3) JsonTopo 模式下加载初始 JSON 拓扑，并按时间片更新节点和链路状态。
  (4) 调用 buildApp() 安装服务器与客户端应用。
  (5) 安装 FlowMonitor，运行仿真到 totalTimeStep。
  (6) 仿真结束后调用 dealSimInfo() 汇总业务流与控制流性能指标。

8) 统计输出
  终端会输出两类统计：

    业务数据性能
    控制信息性能

  指标包括 Tx/Rx 包数、字节数、丢包、时延、吞吐、抖动和丢包率。

9) 传统 CSV 拓扑模式
  如果通过 --useJsonTopo=false 切回传统拓扑，程序会使用：

    examples/link-selection/topo(324).csv

  该模式主要保留兼容旧实验流程；当前新增拓扑数据优先使用 JsonTopo。
