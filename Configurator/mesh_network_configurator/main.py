import serial
import json
import time
import threading
import networkx as nx
import matplotlib.pyplot as plt
import re

# Porta serial e baudrate
SERIAL_PORT = 'COM7'
BAUD_RATE = 115200

# Grafo global
G = nx.DiGraph()
lock = threading.Lock()

def read_serial():
    with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1) as ser:
        while True:
            try:
                line = ser.readline().decode(errors='ignore').strip()
                if 'NODE_JSON:' in line:
                    matches = re.findall(r'\{.*?\}', line)
                    for chunk in matches:
                        try:
                            data = json.loads(chunk)
                            print(f"Recebido: {data}")

                            if all(k in data for k in ('mac', 'parent', 'hops')):
                                mac = data['mac']
                                parent = data['parent']
                                hops = data['hops']

                                with lock:
                                    G.add_node(mac, hops=hops)
                                    if parent != "null":
                                        G.add_edge(parent, mac)
                            else:
                                print("⚠️ JSON com campos ausentes:", data)
                        except json.JSONDecodeError:
                            print("⚠️ Erro ao decodificar JSON:", chunk)

            except Exception as e:
                print(f"Erro na leitura da serial: {e}")
            time.sleep(0.1)

def plot_graph():
    plt.ion()
    fig, ax = plt.subplots(figsize=(10, 6))

    while True:
        with lock:
            ax.clear()
            pos = nx.spring_layout(G)
            labels = {
                node: f"{node}\nHops:{data['hops']}" if 'hops' in data else node
                for node, data in G.nodes(data=True)
            }
            nx.draw(
                G, pos,
                with_labels=True,
                labels=labels,
                node_color='skyblue',
                node_size=1800,
                font_size=8,
                ax=ax,
                arrows=True
            )
        plt.pause(1)

def main():
    serial_thread = threading.Thread(target=read_serial, daemon=True)
    serial_thread.start()
    plot_graph()

if __name__ == "__main__":
    main()
