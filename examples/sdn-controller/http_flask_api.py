from flask import Flask, request, jsonify # type: ignore
import socket
import json
import sys
import threading


sys.stdout.flush()

app = Flask(__name__)

HOST = '127.0.0.1'
PORT = 8080
HEADER_SIZE = 4

# 全局 socket 对象
global_socket = None

# 在应用启动之前创建全局 socket
def create_socket():
    global global_socket
    try:
        global_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        global_socket.connect((HOST, PORT))
        print("与ns3服务器的 socket 连接已建立")
    except Exception as e:
        print(f"无法连接到ns3服务器: {e}")
        global_socket = None

def destroy_socket():
    global global_socket
    try:
        if global_socket:
            global_socket.close()  # 关闭 socket 连接
            print("与ns3服务器的 socket 连接已关闭")
            global_socket = None  # 清空全局 socket 引用
    except Exception as e:
        print(f"关闭 socket 时发生错误: {e}")

def send_message(sock, message):
    # 发送消息
    length = len(message)
    message_header = length.to_bytes(4, byteorder='big')
    
    total_sent = 0
    message_data = message.encode('utf-8')
    sock.sendall(message_header + message_data)
    print(f"客户端\n{message}\n 发送成功！")
    return True

def receive_and_process(sock):
    buffer = b""
    while True:
        data = sock.recv(4096)
        if not data:
            break
        buffer += data

        if len(buffer) >= HEADER_SIZE:
            msg_len = int.from_bytes(buffer[:HEADER_SIZE], byteorder='big')
            if len(buffer) >= HEADER_SIZE + msg_len:
                message = buffer[HEADER_SIZE:HEADER_SIZE + msg_len].decode('utf-8')
                print(f"接收到来自ns3服务器的消息: {message}")
                return message
    return None

# # 一、新建场景
# @app.route('/simulation', methods=['POST'])
# def create_simulation():
#     # 这里的代码应该处理POST请求，生成simulation_id并返回
#     data = request.json
#     # 业务逻辑处理（例如生成simulation_id）
#     # ...
#     return jsonify({"simulation_id": 1}), 200

# 二、添加节点
@app.route('/nodes', methods=['POST'])
def add_nodes():
    # 这里的代码应该处理POST请求，添加节点信息
    # 业务逻辑处理（例如添加节点到数据库）
    # ...
    global global_socket
    # 检查 socket 是否存在
    if not global_socket:
        return jsonify({"status": "failure", "message": "ns3服务器未连接"}), 500

    data = request.json

    # 与ns3通信
    try:
        # 构造发送的数据
        json_data = {
            "func": "nodes",
            "data": data
        }
        json_string = json.dumps(json_data, indent=4)

        # 发送消息到后端
        send_message(global_socket, json_string)

        # 接收后端的响应
        print(f"post_nodes success")
        return jsonify(data), 200

    except Exception as e:
        print(f"与ns3通信时出错: {e}")
        return jsonify({"status": "failure", "message": str(e)}), 500

# 三、面向连接路由更新
# 3.1 面向连接路由创建
@app.route('/route', methods=['POST'])
def create_route():
    global global_socket
    # 检查 socket 是否存在
    if not global_socket:
        return jsonify({"status": "failure", "message": "ns3服务器未连接"}), 500

    data = request.json

    # 与ns3通信
    try:
        # 构造发送的数据
        json_data = {
            "func": "route",
            "data": data
        }
        json_string = json.dumps(json_data, indent=4)

        # 发送消息到后端
        send_message(global_socket, json_string)

        # 接收后端的响应
        response = receive_and_process(global_socket)
        if response:
            try:
                # 将返回的字符串转换为 JSON 数据
                response_json = json.loads(response)
                print(f"post_routes success")
                return jsonify(response_json), 200
            except json.JSONDecodeError:
                # 如果无法解析为 JSON，返回错误信息
                return jsonify({"status": "failure", "message": "无法解析ns3服务器返回的数据"}), 500
        else:
            return jsonify({"status": "failure", "message": "ns3服务器无返回"}), 500

    except Exception as e:
        print(f"与ns3通信时出错: {e}")
        return jsonify({"status": "failure", "message": str(e)}), 500

# # 3.2 面向连接路由修改
# @app.route('/route', methods=['PUT'])
# def update_route():
#     data = request.json
#     # 业务逻辑处理（例如修改路由）
#     # ...
#     return jsonify({
#         "simulation_id": data["simulation_id"],
#         "srcnode_id": data["srcnode_id"],
#         "dstnode_id": data["dstnode_id"],
#         "tunnel_id": data["tunnel_id"],
#         "links": [1, 2, 3],
#         "backuplinks": [1, 2, 3]
#     }), 200

# 3.3 面向连接路由删除
@app.route('/route', methods=['DELETE'])
def delete_route():
    global global_socket
    # 检查 socket 是否存在
    if not global_socket:
        return jsonify({"status": "failure", "message": "ns3服务器未连接"}), 500

    data = request.json

    # 与ns3通信
    try:
        # 构造发送的数据
        json_data = {
            "func": "delete_route",
            "data": data
        }
        json_string = json.dumps(json_data, indent=4)

        # 发送消息到后端
        send_message(global_socket, json_string)

        # 接收后端的响应
        print(f"delete_route success")
        return jsonify(data), 200

    except Exception as e:
        print(f"与ns3通信时出错: {e}")
        return jsonify({"status": "failure", "message": str(e)}), 500


# 四、全网拓扑
@app.route('/links', methods=['POST'])
def update_links():

    global global_socket
    # 检查 socket 是否存在
    if not global_socket:
        return jsonify({"status": "failure", "message": "ns3服务器未连接"}), 500

    data = request.json

    # 与ns3通信
    try:
        # 构造发送的数据
        json_data = {
            "func": "links",
            "data": data
        }
        json_string = json.dumps(json_data, indent=4)

        # 发送消息到后端
        send_message(global_socket, json_string)

        # 接收后端的响应
        response = receive_and_process(global_socket)
        if response:
            try:
                # 将返回的字符串转换为 JSON 数据
                response_json = json.loads(response)
                print(f"post_links success")
                return jsonify(response_json), 200
            except json.JSONDecodeError:
                # 如果无法解析为 JSON，返回错误信息
                return jsonify({"status": "failure", "message": "无法解析ns3服务器返回的数据"}), 500
        else:
            return jsonify({"status": "failure", "message": "ns3服务器无返回"}), 500

    except Exception as e:
        print(f"与ns3通信时出错: {e}")
        return jsonify({"status": "failure", "message": str(e)}), 500


# 查看分簇
@app.route('/cluster', methods=['GET'])
def view_links():
    global global_socket
    # 检查 socket 是否存在
    if not global_socket:
        return jsonify({"status": "failure", "message": "ns3服务器未连接"}), 500

    # data = request.json

    # 与ns3通信
    try:
        # 构造发送的数据
        json_data = {
            "func": "cluster"
            # "data": data
        }
        json_string = json.dumps(json_data, indent=4)

        # 发送消息到后端
        send_message(global_socket, json_string)

        # 接收后端的响应
        response = receive_and_process(global_socket)
        if response:
            try:
                # 将返回的字符串转换为 JSON 数据
                response_json = json.loads(response)
                print(f"get_cluster success")
                return jsonify(response_json), 200
            except json.JSONDecodeError:
                # 如果无法解析为 JSON，返回错误信息
                return jsonify({"status": "failure", "message": "无法解析ns3服务器返回的数据"}), 500
        else:
            return jsonify({"status": "failure", "message": "ns3服务器无返回"}), 500

    except Exception as e:
        print(f"与ns3通信时出错: {e}")
        return jsonify({"status": "failure", "message": str(e)}), 500

# 五、删除场景
@app.route('/simulation', methods=['DELETE'])
def delete_simulation():
    data = request.json
    # 业务逻辑处理（例如删除场景）
    # ...
    return jsonify(data), 200

if __name__ == '__main__':
    # app.run(debug=True)
    create_socket()
    app.run(host='0.0.0.0', port=5000, threaded=True)
    destroy_socket()