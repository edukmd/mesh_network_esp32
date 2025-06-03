import json
import threading
import networkx as nx
import matplotlib.pyplot as plt
import matplotlib.cm as cm
import matplotlib.colors as colors
import paho.mqtt.client as mqtt

# Configurações MQTT
MQTT_BROKER = '192.168.50.208'
MQTT_PORT = 1883
MQTT_TOPIC = 'mesh/network/info'

# Grafo global
G = nx.DiGraph()
lock = threading.Lock()

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
        data = json.loads(payload)
        print(f"📥 Mensagem recebida: {data}")

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
    mqtt_client = mqtt.Client(protocol=mqtt.MQTTv311)
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message

    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
        mqtt_client.loop_start()
    except Exception as e:
        print(f"❌ Erro ao conectar no broker MQTT: {e}")
        return

    plot_graph()

if __name__ == "__main__":
    main()
