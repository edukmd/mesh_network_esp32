import json
import threading
import networkx as nx
import matplotlib.pyplot as plt
import matplotlib.cm as cm
import matplotlib.colors as colors
import paho.mqtt.client as mqtt
import subprocess
import os
import time
import socket

# Grafo global
G = nx.DiGraph()
lock = threading.Lock()
global mqtt_client


# Configurações MQTT
def get_local_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        local_ip = s.getsockname()[0]
        s.close()
        return local_ip
    except Exception as e:
        print(f"❌ Erro ao obter IP local: {e}")
        return "127.0.0.1"

MQTT_BROKER = get_local_ip()
MQTT_PORT = 1883
MQTT_TOPIC = "mesh/network/info"
MQTT_CONFIG_COMMAND_TOPIC = "mesh/cmd"
def start_mosquitto():
    mosquitto_path = r"C:\Program Files\mosquitto\mosquitto.exe"
    config_path = r"C:\Program Files\mosquitto\mosquitto.conf"

    if not os.path.exists(mosquitto_path):
        print("❌ Mosquitto não encontrado.")
        return None

    print("🚀 Iniciando Mosquitto...")
    process = subprocess.Popen(
        [mosquitto_path, "-c", config_path, "-v"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT
    )
    # Aguardar um tempo para garantir que o broker inicie
    time.sleep(1)
    return process

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("✅ Conectado ao broker MQTT")
        client.subscribe(MQTT_TOPIC)
        print(f"📡 Inscrito no tópico: {MQTT_TOPIC}")
    else:
        print(f"❌ Falha na conexão. Código de retorno: {rc}")

def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode()
        print(f"📥 Mensagem recebida: {payload}")
        data = json.loads(payload)


        if all(k in data for k in ('mac', 'parent', 'hops')):
            mac = data['mac']
            parent = data['parent']
            hops = data['hops']

            with lock:
                G.add_node(mac, hops=hops)
                if parent != "null":
                    G.add_edge(mac, parent)
        else:
            print("⚠️ JSON incompleto:", data)
    except Exception as e:
        print(f"❌ Erro ao processar mensagem: {e}")

def get_text_color(rgb):
    r, g, b = [x * 255 for x in rgb[:3]]  # valores de 0–255
    luminance = 0.2126*r + 0.7152*g + 0.0722*b
    return 'white' if luminance < 128 else 'black'

def send_message(msg):
        mqtt_client.publish(MQTT_CONFIG_COMMAND_TOPIC, msg)


def plot_graph():
    plt.ion()
    fig, ax = plt.subplots(figsize=(10, 6))

    while True:
        with lock:
            ax.clear()
            pos = nx.spring_layout(G, seed=42, k=1.5)

            # Obter hops para cada nó (usar 0 como fallback)
            node_hops = [G.nodes[n].get("hops", 0) for n in G.nodes()]
            max_hops = max(node_hops) if node_hops else 1
            cmap = cm.get_cmap('viridis')

            norm = colors.Normalize(vmin=0, vmax=max_hops)
            node_colors = [cmap(norm(h)) for h in node_hops]

            labels = {
                node: f"{node}\nHops:{G.nodes[node]['hops']}" if 'hops' in G.nodes[node] else node
                for node in G.nodes()
            }

            font_colors = {
                node: get_text_color(cmap(norm(G.nodes[node].get("hops", 0))))
                for node in G.nodes()
            }

            nx.draw_networkx_edges(G, pos, ax=ax, arrows=True)
            nx.draw_networkx_nodes(G, pos, node_color=node_colors, node_size=3500, ax=ax)

            for node, label in labels.items():
                nx.draw_networkx_labels(
                    G, pos,
                    labels={node: label},
                    font_color=font_colors[node],
                    font_size=6,
                    ax=ax
                )

        plt.pause(1)

def main():
    global mqtt_client
    mosquitto_process = start_mosquitto()

    mqtt_client = mqtt.Client(protocol=mqtt.MQTTv311)
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message

    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
        mqtt_client.loop_start()
    except Exception as e:
        print(f"❌ Erro ao conectar no broker MQTT: {e}")
        if mosquitto_process:
            mosquitto_process.terminate()
        return

    try:
        plot_graph()
    finally:
        if mosquitto_process:
            mosquitto_process.terminate()





# Exemplo de uso
print(f"🌐 IP local detectado: {get_local_ip()}")


if __name__ == "__main__":
    get_local_ip()
    main()
