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

last_seen = {}
NODE_TIMEOUT = 10

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
    mosquitto_path = r"C:\\Program Files\\mosquitto\\mosquitto.exe"
    config_path = r"C:\\Program Files\\mosquitto\\mosquitto.conf"

    if not os.path.exists(mosquitto_path):
        print("❌ Mosquitto não encontrado.")
        return None

    print("🚀 Iniciando Mosquitto...")
    process = subprocess.Popen(
        [mosquitto_path, "-c", config_path, "-v"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT
    )
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

        if all(k in data for k in ('mac', 'parent', 'hops', 'children')):
            mac = data['mac']
            parent = data['parent']
            hops = data['hops']
            children = data.get('children', [])

            with lock:
                # Atualiza o nó principal
                G.add_node(mac, hops=hops, is_root=(parent == "null"))
                last_seen[mac] = time.time()

                # Remove arestas existentes em que esse nó é filho (evita múltiplos pais)
                edges_to_remove = [(u, v) for u, v in G.edges() if v == mac]
                G.remove_edges_from(edges_to_remove)

                # Adiciona aresta do pai real (se não for root)
                if parent != "null":
                    G.add_node(parent)
                    G.add_edge(parent, mac)

                # Adiciona filhos apenas como nós isolados (sem criar arestas)
                for child in children:
                    if child not in G.nodes():
                        G.add_node(child)
                        # O hops será definido depois quando o próprio nó publicar
        else:
            print("⚠️ JSON incompleto:", data)
    except Exception as e:
        print(f"❌ Erro ao processar mensagem: {e}")


def cleanup_inactive_nodes():
    now = time.time()
    to_remove = []
    with lock:
        for node, last in list(last_seen.items()):
            if now - last > NODE_TIMEOUT:
                to_remove.append(node)

        for node in to_remove:
            print(f"🗑️ Removendo nó inativo: {node}")
            G.remove_node(node)
            del last_seen[node]

def get_text_color(rgb):
    r, g, b = [x * 255 for x in rgb[:3]]
    luminance = 0.2126*r + 0.7152*g + 0.0722*b
    return 'white' if luminance < 128 else 'black'

def send_message(msg):
    mqtt_client.publish(MQTT_CONFIG_COMMAND_TOPIC, msg)

def plot_graph():
    plt.ion()
    fig, ax = plt.subplots(figsize=(10, 6))

    while True:
        cleanup_inactive_nodes()
        with lock:
            ax.clear()
            # Adiciona nó fictício "ROUTER" antes de calcular o layout
            root_nodes = [n for n in G.nodes() if G.nodes[n].get("is_root", False)]
            if root_nodes:
                G.add_node("ROUTER", is_router=True)
                for root in root_nodes:
                    G.add_edge("ROUTER", root)

            # Layout agrupado por hops e por pai
            pos = {}
            parent_to_children = {}
            layer_to_nodes = {}

            # Organiza os nós por hops (camadas) e filhos por pai
            for node in G.nodes():
                if G.nodes[node].get("is_router", False):
                    layer = 0
                else:
                    layer = G.nodes[node].get("hops", 1)

                if layer not in layer_to_nodes:
                    layer_to_nodes[layer] = []
                layer_to_nodes[layer].append(node)

                for parent, child in G.edges():
                    if child == node:
                        if parent not in parent_to_children:
                            parent_to_children[parent] = []
                        parent_to_children[parent].append(node)

            # Posicionamento: eixo X por hops, eixo Y por ordem ou alinhado com pai
            x_spacing = 2.5
            y_spacing = 1.5

            for layer in sorted(layer_to_nodes.keys()):
                nodes = layer_to_nodes[layer]
                for i, node in enumerate(nodes):
                    x = layer * x_spacing

                    # Alinha com pai se possível
                    parent = None
                    for p, children in parent_to_children.items():
                        if node in children:
                            parent = p
                            break

                    if parent in pos:
                        y = pos[parent][1] - y_spacing * parent_to_children[parent].index(node)
                    else:
                        y = -i

                    pos[node] = (x, y)

            node_hops = [G.nodes[n].get("hops", 0) for n in G.nodes() if not G.nodes[n].get("is_router", False)]
            max_hops = max(node_hops) if node_hops else 1
            cmap = cm.get_cmap('viridis')
            norm = colors.Normalize(vmin=0, vmax=max_hops)

            node_colors = []
            for n in G.nodes():
                if G.nodes[n].get("is_router", False):
                    node_colors.append("blue")
                elif G.nodes[n].get("is_root", False):
                    node_colors.append("red")
                else:
                    node_colors.append(cmap(norm(G.nodes[n].get("hops", 0))))

            labels = {
                node: "ROUTER" if G.nodes[node].get("is_router", False)
                else f"{node}\nHops:{G.nodes[node].get('hops', '?')}"
                for node in G.nodes()
            }

            font_colors = {
                node: 'white' if G.nodes[node].get("is_router", False)
                else get_text_color(cmap(norm(G.nodes[node].get("hops", 0))))
                for node in G.nodes()
            }

            nx.draw_networkx_edges(
                G, pos, ax=ax,
                arrows=True,
                arrowstyle='-|>',
                arrowsize=25,
                min_source_margin=15,
                min_target_margin=30
            )

            nx.draw_networkx_nodes(G, pos, node_color=node_colors, node_size=3500, ax=ax)

            for node, label in labels.items():
                nx.draw_networkx_labels(
                    G, pos,
                    labels={node: label},
                    font_color=font_colors.get(node, 'black'),
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

print(f"🌐 IP local detectado: {get_local_ip()}")

if __name__ == "__main__":
    get_local_ip()
    main()
