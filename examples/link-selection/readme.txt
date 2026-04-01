link-test 运行逻辑说明（基于当前代码）

1) 入口与参数
  - 主程序：examples/link-selection/link-test.cc
  - 拓扑构建：examples/link-selection/topo.cc
  - 全局参数：examples/link-selection/para.cc
  - 默认命令：./waf --run link-test
  - 可覆盖参数（命令行）：
    --offeredload=<double>
    --isSate=<1|2|3|4>
    --consType=<0|1>
    --linkBandwidth=<bps>
    --tranProtocol=<0|1>   (0=UDP, 1=TCP)

2) 编译运行指令
  - 编译：
    ./waf clean
    ./waf configure --enable-examples --enable-test
    ./waf build
  - 运行：
    ./waf --run link-test

3) 当前默认配置（para.cc）
  - _isSate = 1：324 星座
  - _scenario = 0：正常场景
  - _isMesh = false：采用“非 mesh 拓扑”
  - _tranProc = 0：UDP
  - offeredload = 0.1
  - linkBandwidth = 10Gbps
  - totalTimeStep = 110s

4) 主流程（main）
  (1) 解析命令行参数并打印关键配置。
  (2) 根据 isSate 推导：
      sates_num / orbit_num / sate_num。
  (3) 调用 initTopo() 构建网络拓扑并分配路由。
  (4) 调用 buildApp() 安装服务器与客户端应用。
  (5) 安装 FlowMonitor，运行仿真到 totalTimeStep。
  (6) 仿真结束后调用 dealSimInfo() 汇总业务流与控制流性能指标。

5) 拓扑构建逻辑（initTopo）
  (1) 根据轨道数(orbit_num)和卫星数(sate_num)创建所有卫星节点，并将它们存储在sates/sateNodes中。
  (2) 为所有节点安装互联网协议栈，使它们能够通信。
  (3) 由于使用非网格拓扑(_isMesh=false)，从文件中读取链路信息（包括源节点、目标节点、延迟和带宽），然后为每条链路设置网络设备，并分配IP地址。
  (4) 生成全局路由表，以便数据包知道如何在网络中传输。
  (5) 定期更新拓扑。

6) 业务生成逻辑（buildApp / installClient）
  (1) 初始化流量矩阵 data。
  (2) 热点流量模式下（_trafficMode=0），读取
      examples/link-selection/traffic_matrix(324).csv。
  (3) 根据传输协议参数(_tranProc或--tranProtocol)给每个节点安装相应的服务端：
      - UDP 模式(_tranProc=0)：UdpServer
      - TCP 模式(_tranProc=1)：PacketSink
  (4) installClient() 遍历源-宿对：
      - 若 data[i][j] == 0 则跳过；
      - 计算发送速率/包数；
      - 创建 UDP/TCP 客户端并在 [0, totalTimeStep]（UDP 发送到 totalTimeStep-10）发包。

7) 统计输出（dealSimInfo）
  - 使用 FlowMonitor 统计：Tx/Rx 包数、丢包、时延、抖动、吞吐。
  - 区分 packetPrio：
    - 0：控制信息
    - 1：业务数据
  - 结果输出到终端。

8) 当前注意事项（与运行错误相关）
  - installClient() 当前按固定接口读取目的地址：
    destIp->GetAddress(1, 0)
  - 若某目的节点不存在接口 1 或该接口未配置地址，会触发 ns-3 断言：
    Attempted to dereference zero pointer
  - 建议改为“遍历接口并选择第一个可用 IPv4 地址”，避免固定索引导致崩溃。

9) 关键输入/输出文件
  - 输入拓扑：examples/link-selection/topo(324).csv
  - 输入流量：examples/link-selection/traffic_matrix(324).csv
