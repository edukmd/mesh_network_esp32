"""
Script para consumir o tópico MQTT "/mesh/tree" com a topologia da rede mesh ESP32
e exibir graficamente as relações pai-filho usando networkx e matplotlib.
"""

import json
import paho.mqtt.client as mqtt
import networkx as nx
import matplotlib.pyplot as plt

BROKER = "broker.hivemq.com"
TOPIC = "/mesh/tree"

def on_connect(client, userdata, flags, rc):
    print("Conectado ao broker MQTT com código:", rc)
    client.subscribe(TOPIC)

def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode())
        print("📥 Dados recebidos:", json.dumps(payload, indent=2))
        exibir_grafo(payload)
    except Exception as e:
        print("Erro ao processar JSON:", e)

def exibir_grafo(dados):
    G = nx.DiGraph()
    for node in dados:
        mac = node["mac"]
        parent = node["parent"]
        G.add_node(mac, hops=node["hops"])
        if parent:
            G.add_edge(parent, mac)

    pos = nx.spring_layout(G)
    labels = {n: f"{n[-5:]}\nHOP {G.nodes[n]['hops']}" for n in G.nodes}

    plt.figure(figsize=(8, 6))
    nx.draw(G, pos, with_labels=False, node_color="lightblue", node_size=2500, arrows=True)
    nx.draw_networkx_labels(G, pos, labels)
    plt.title("Topologia da Rede Mesh ESP32")
    plt.axis("off")
    plt.tight_layout()
    plt.show()

def main():
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(BROKER, 1883, 60)
    client.loop_forever()

if __name__ == "__main__":
    main()
