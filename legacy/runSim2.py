import threading
import subprocess
import os
import glob

# # 获取当前目录下所有的 .txt 文件
# txt_files = glob.glob("*.txt")

# # 删除每个文件
# for txt_file in txt_files:
#     os.remove(txt_file)
#     print(f"Deleted {txt_file}")

# # 获取当前目录下所有的 .txt 文件
# txt_files = glob.glob("examples/sdn-controller/output/*.txt")

# # 删除每个文件
# for txt_file in txt_files:
#     os.remove(txt_file)
#     print(f"Deleted {txt_file}")

def run_simulation(param, sdn_route, is_sat, cons_type, traffic, log_file):
    command = f"./waf --run-no-build \"openflow-sdn-switch-test --offeredload={param} --SDNRoute={sdn_route} --isSate={is_sat} --consType={cons_type} --linkBandwidth={traffic}\" > {log_file} 2>&1"
    subprocess.run(command, shell=True)

# param_pairs = [(1.0, 10.0), (2.0, 9.0), (3.0, 8.0), (4.0, 7.0), (5.0, 6.0)]  # 不同的负载率集
params = [0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0, 5.5, 6.0]  # 不同的负载率集
sdn_routes = [1]    # 路由方式，0:OSPF  1:本方案路由
is_sats = [1]       # 1:60颗卫星， 2:108颗卫星， 3:500颗卫星
cons_type = 0       # 星座轨道类型（影响路由策略）0: Walker Star(如果上一行为60卫星)    1: Walker Delta(如果上一行为卫星108 500)
# traffics = [100000000, 500000000]  # 100Mbps，500Mbps和10Gbps
traffics = [100000000]  # 100Mbps，500Mbps和10Gbps

# 创建线程
for is_sat in is_sats:
    for sdn_route in sdn_routes:
        threads = []
        for traffic in traffics:
            for param in params:
                log_file = f"output_offeredload_{param}_SDNRoute_{sdn_route}_isSat_{is_sat}_consType_{cons_type}_traffic_{traffic}_WithLinkChange.txt"
                thread = threading.Thread(target=run_simulation, args=(param, sdn_route, is_sat, cons_type, traffic, log_file))
                threads.append(thread)
                thread.start()

        # 等待当前参数对中的所有线程完成
        for thread in threads:
            thread.join()
        print(f"Completed simulations for parameters: SDNRoute={sdn_route}, isSat={is_sat}")

print("All simulations completed.")
