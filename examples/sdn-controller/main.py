import requests
import json
import os
import time

# 定义 URL 和对应的 JSON 文件路径
url_data_map = [
    {   "url": "http://127.0.0.1:5000/nodes", 
        "method": "POST",
        "file": "json_data/nodes.json"
    },
    {   "url": "http://127.0.0.1:5000/links", 
        "method": "POST",
        "file": "json_data/links.json"
    },
    {   "url": "http://127.0.0.1:5000/route", 
        "method": "POST",
        "file": "json_data/route.json"
    },
    {   "url": "http://127.0.0.1:5000/route", 
        "method": "DELETE",
        "file": "json_data/delete_route.json"
    },
    {   "url": "http://127.0.0.1:5000/cluster", 
        "method": "GET",
        "file": "json_data/delete_route.json"
    }
]

# 遍历每个 URL 和对应的请求信息
for item in url_data_map:
    url = item["url"]
    method = item["method"]
    file_path = item.get("file")

    # 如果需要从文件读取数据，先检查文件是否存在
    data = None
    if file_path:
        if not os.path.exists(file_path):
            print(f"File {file_path} not found, skipping...")
            continue

        # 读取 JSON 文件内容
        with open(file_path, "r") as f:
            try:
                data = json.load(f)
            except json.JSONDecodeError as e:
                print(f"Error decoding JSON from file {file_path}: {e}")
                continue

    # 根据请求方法发送对应的请求
    try:
        if method == "POST":
            response = requests.post(url, json=data)
        elif method == "GET":
            response = requests.get(url)  # GET 请求使用 params
        elif method == "DELETE":
            response = requests.delete(url, json=data)  # DELETE 请求也支持发送 JSON 数据
        else:
            print(f"Unsupported method {method}, skipping...")
            continue

        # 打印请求和响应信息
        print(f"{method} {url}")
        print("Status Code:", response.status_code)

        # 检查响应是否为 JSON
        if response.headers.get('Content-Type') == 'application/json':
            print("Response:", response.json())
        else:
            print("Response Text:", response.text)

    except requests.exceptions.RequestException as e:
        print(f"HTTP Error while connecting to {url}: {e}")
    
    time.sleep(1)  # 添加 1 秒延迟

# response = requests.post(url,json=data)
# # response = requests.delete(url,json=data)

# print("Status Code:", response.status_code)
# print("Response:", response.json())