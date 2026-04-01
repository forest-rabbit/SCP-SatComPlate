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

def run_simulation(param, is_sat, traffic, log_file):
    command = f"./waf --run-no-build \"ospf --load={param} --topo={is_sat} --linkBandwidth={traffic}\" > {log_file} 2>&1"
    subprocess.run(command, shell=True)

# param_pairs = [(1.0, 10.0), (2.0, 9.0), (3.0, 8.0), (4.0, 7.0), (5.0, 6.0)]  # 不同的负载率集
params = [0.5, 2.0, 3.5]  # 不同的负载率集
is_sats = [1]       # 0:108颗卫星， 1:60颗卫星
# cons_type = 0     # 星座轨道类型（影响路由策略）0: Walker Star(60)    1: Walker Delta(108 500)
# traffics = [100000000, 500000000]  # 100Mbps，500Mbps和10Gbps
traffics = [100000000]  # 100Mbps，500Mbps和10Gbps

# 创建线程
for is_sat in is_sats:
    threads = []
    for traffic in traffics:
        for param in params:
            log_file = f"opsf-output_offeredload_{param}_isSatI_{is_sat}_traffic_{traffic}_WithLinkChange.txt"
            thread = threading.Thread(target=run_simulation, args=(param, is_sat, traffic, log_file))
            threads.append(thread)
            thread.start()

    # 等待当前参数对中的所有线程完成
    for thread in threads:
        thread.join()
    print(f"Completed simulations for parameters: isSatI={is_sat}")

print("All simulations completed.")