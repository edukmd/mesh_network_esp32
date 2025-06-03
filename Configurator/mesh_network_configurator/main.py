import json
import threading
import networkx as nx
import matplotlib.pyplot as plt
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
        print(payload)
        data = json.loads(payload)
        print(f"📥 Mensagem recebida: {data}")

        if all(k in data for k in ('mac', 'parent', 'hops')):
            mac = data['mac']
            parent = data['parent']
            hops = data['hops']

            with lock:
                G.add_node(mac, hops=hops)
                if parent != "null" and not G.has_edge(mac, parent):
                    G.add_edge(mac, parent)

        else:
            print("⚠️ JSON incompleto:", data)
    except Exception as e:
        print(f"❌ Erro ao processar mensagem: {e}")

def plot_graph():
    plt.ion()
    fig, ax = plt.subplots(figsize=(10, 6))

    while True:
        with lock:
            ax.clear()
            pos = nx.spring_layout(G, seed=42, k=1.5)

            labels = {
                node: f"{node}\nHops:{attr['hops']}" if 'hops' in attr else node
                for node, attr in G.nodes(data=True)
            }

            nx.draw(
                G, pos,
                with_labels=True,
                labels=labels,
                node_color='skyblue',
                node_size=3500,
                font_size=6,
                ax=ax,
                arrows=True
            )
        plt.pause(1)

def main():
    mqtt_client = mqtt.Client(protocol=mqtt.MQTTv311)

    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message

    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
        print(f"foi")
        mqtt_client.loop_start()
    except Exception as e:
        print(f"❌ Erro ao conectar no broker MQTT: {e}")
        return

    plot_graph()

if __name__ == "__main__":
    main()
