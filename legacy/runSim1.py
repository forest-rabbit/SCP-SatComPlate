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

def run_simulation(param, is_change, clusterMode, is_sat, cons_type, traffic, log_file):
    command = f"./waf --run-no-build \"link-test --offeredload={param} --link_change={is_change} --clusterMode={clusterMode} --isSate={is_sat} --consType={cons_type} --linkBandwidth={traffic}\" > {log_file} 2>&1"
    subprocess.run(command, shell=True)

# param_pairs = [(1.0, 10.0), (2.0, 9.0), (3.0, 8.0), (4.0, 7.0), (5.0, 6.0)]  # 不同的负载率集
# params = [round(x * 0.5, 2) for x in range(1, 13)]  # 0.5到6.0，步长0.5
# params = [0.01, 0.02, 0.03, 0.04, 0.05, 0.06, 0.07, 0.08, 0.09, 0.1, 0.2, 0.3, 0.4, 0.5]  # 不同的负载率集
# params = [0.0002, 0.00044, 0.0006, 0.0008, 0.0015, 0.0012, 0.0014, 0.0016,  0.0018,  0.002, 0.0022, 0.0024, 0.0026, 0.0028]  # 不同的负载率集
params = [0.0002, 0.0004, 0.0006, 0.0008, 0.001, 0.0012, 0.0014, 0.0016, 0.0018, 0.002]  # 不同的负载率集
is_change = 1  # -1:正常mesh   0: 无罚函数，1: 有罚函数  2：NSGAII  
clusterMode = 1  # 0:双层分簇 1：链路利用率 2：轨道分簇
is_sats = [2]       # 1:66颗卫星， 2:108颗卫星， 3:500颗卫星
cons_type = 1       # 星座轨道类型（影响路由策略）0: Walker Star(66)    1: Walker Delta(108 500)
# traffics = [100000000, 500000000]  # 100Mbps，500Mbps和10Gbps
traffics = [100000000]  # 100Mbps，500Mbps和10Gbps

# 创建线程
for is_sat in is_sats:
    threads = []
    for traffic in traffics:
        for param in params:
            log_file = f"output_offeredload_{param}_link_change{is_change}_clusterMode{clusterMode}_isSat_{is_sat}_consType_{cons_type}_traffic_{traffic}_WithLinkChange.txt"
            thread = threading.Thread(target=run_simulation, args=(param, is_change, clusterMode, is_sat, cons_type, traffic, log_file))
            threads.append(thread)
            thread.start()

    # 等待当前参数对中的所有线程完成
    for thread in threads:
        thread.join()
    print(f"Completed simulations for parameters: isSat={is_sat}")

print("All simulations completed.")