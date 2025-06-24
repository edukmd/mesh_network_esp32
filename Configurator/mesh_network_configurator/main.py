import json
import threading
import networkx as nx
import matplotlib.pyplot as plt
import matplotlib.colors as colors
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import paho.mqtt.client as mqtt
import subprocess
import os
import time
import socket
import tkinter as tk
from tkinter import ttk

G = nx.DiGraph()
G.add_node("ROUTER", is_router=True)

lock = threading.Lock()
mqtt_client = None
last_seen = {}
NODE_TIMEOUT = 20
last_node_snapshot = set()
running = True  # Flag para controlar encerramento seguro
selected_node_mac = None
selected_node_mac_draw = False
highlight_timer_id = None
root = None
ping_timers = {}
ping_latencies = {}  # mac -> tempo decorrido do ping (float)




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

#MQTT_BROKER = get_local_ip()
MQTT_BROKER = "192.168.50.208"
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
        data = json.loads(payload)
        # Se for resposta pong
        if data.get("type") == "pong" and "mac" in data:
            pong_mac = data["mac"]
            if pong_mac in ping_timers:
                elapsed = (time.time() - ping_timers[pong_mac]) * 1000
                ping_latencies[pong_mac] = elapsed  # salva para exibir
                print(f"🏓 Ping para {pong_mac} respondido em {elapsed:.0f} ms")
                del ping_timers[pong_mac]
            else:
                print(f"🏓 Pong recebido de {pong_mac}, mas não foi feito ping.")
            return




        if all(k in data for k in ('mac', 'parent', 'hops', 'children')):
            mac = data['mac']
            parent = data['parent']
            hops = data['hops']
            children = data.get('children', [])

            with lock:
                G.add_node(mac, hops=hops, is_root=(parent == "null"))
                last_seen[mac] = time.time()
                edges_to_remove = [(u, v) for u, v in G.edges() if v == mac]
                G.remove_edges_from(edges_to_remove)
                if parent != "null":
                    G.add_node(parent)
                    G.add_edge(parent, mac)
                for child in children:
                    if child not in G.nodes:
                        G.add_node(child)
                    last_seen[child] = time.time()
        else:
            print("⚠️ JSON incompleto:", data)
    except Exception as e:
        print(f"❌ Erro ao processar mensagem: {e}")

def cleanup_inactive_nodes():
    if NODE_TIMEOUT == 0:
        return  # 👈 timeout infinito, não remove nada
    now = time.time()
    with lock:
        for node, last in list(last_seen.items()):
            if now - last > NODE_TIMEOUT:
                print(f"🗑️ Removendo nó inativo/incompleto: {node}")
                G.remove_node(node)
                del last_seen[node]


def get_text_color(rgb):
    r, g, b = [x * 255 for x in rgb[:3]]
    luminance = 0.2126*r + 0.7152*g + 0.0722*b
    return 'white' if luminance < 128 else 'black'

def send_message(msg):
    mqtt_client.publish(MQTT_CONFIG_COMMAND_TOPIC, msg)

def plot_graph(ax, canvas):
    cleanup_inactive_nodes()
    with lock:
        ax.clear()
        root_nodes = [n for n in G.nodes() if G.nodes[n].get("is_root", False)]
        if root_nodes:
            G.add_node("ROUTER", is_router=True)
            for root in root_nodes:
                G.add_edge("ROUTER", root)

        pos = {}
        parent_to_children = {}
        layer_to_nodes = {}

        for node in G.nodes():
            layer = 0 if G.nodes[node].get("is_router", False) else G.nodes[node].get("hops", 1)
            layer_to_nodes.setdefault(layer, []).append(node)
            for parent, child in G.edges():
                if child == node:
                    parent_to_children.setdefault(parent, []).append(node)

        x_spacing, y_spacing = 2.5, 1.5
        for layer in sorted(layer_to_nodes):
            nodes = layer_to_nodes[layer]
            for i, node in enumerate(nodes):
                x = layer * x_spacing
                parent = next((p for p, c in parent_to_children.items() if node in c), None)
                y = pos[parent][1] - y_spacing * parent_to_children[parent].index(node) if parent in pos else -i
                pos[node] = (x, y)

        node_hops = [G.nodes[n].get("hops", 0) for n in G.nodes() if not G.nodes[n].get("is_router", False)]
        max_hops = max(node_hops) if node_hops else 1
        cmap = plt.get_cmap('viridis')

        norm = colors.Normalize(vmin=0, vmax=max_hops)

        node_colors = []
        for n in G.nodes():
            if G.nodes[n].get("is_router", False):
                node_colors.append("blue")
            elif G.nodes[n].get("is_root", False):
                node_colors.append("red")
            else:
                node_colors.append(cmap(norm(G.nodes[n].get("hops", 0))))

        labels = {}
        for node in G.nodes():
            if G.nodes[node].get("is_router", False):
                labels[node] = "ROUTER"
            else:
                hops = G.nodes[node].get("hops", "?")
                label = f"{node}\nHops:{hops}"
                if node in ping_latencies:
                    latency = ping_latencies[node]
                    label += f"\nPing:{latency:.0f}ms"
                labels[node] = label


        font_colors = {
            node: 'white' if G.nodes[node].get("is_router", False)
            else get_text_color(cmap(norm(G.nodes[node].get("hops", 0))))
            for node in G.nodes()
        }

        nx.draw_networkx_edges(G, pos, ax=ax, arrows=True, arrowstyle='-|>', arrowsize=25, min_source_margin=15, min_target_margin=30)
        nx.draw_networkx_nodes(G, pos, node_color=node_colors, node_size=3500, ax=ax)
        for node, label in labels.items():
            nx.draw_networkx_labels(G, pos, labels={node: label}, font_color=font_colors.get(node, 'black'), font_size=6, ax=ax)

    # Calcula limites com margem extra para evitar corte das bolinhas
    x_vals = [p[0] for p in pos.values()]
    y_vals = [p[1] for p in pos.values()]

    if x_vals and y_vals:
        x_min, x_max = min(x_vals), max(x_vals)
        y_min, y_max = min(y_vals), max(y_vals)
        margin_x = 1.0  # espaço horizontal extra
        margin_y = 1.0  # espaço vertical extra

        ax.set_xlim(x_min - margin_x, x_max + margin_x)
        ax.set_ylim(y_min - margin_y, y_max + margin_y)

    # Destacar nó selecionado com círculo extra
    if selected_node_mac and selected_node_mac_draw and selected_node_mac in pos:
        x, y = pos[selected_node_mac]
        ax.scatter(x, y, s=5000, facecolors='none', edgecolors='dodgerblue', linewidths=2, zorder=5)

    canvas.draw()

    ax.axis("off")  # remove os eixos
    plt.subplots_adjust(left=0.02, right=0.98, top=0.98, bottom=0.02)


def enviar_config(entry_intervalo, entry_maxfilhos, entry_timeout):
    global NODE_TIMEOUT
    try:
        intervalo = int(entry_intervalo.get())
        max_filhos = int(entry_maxfilhos.get())
        NODE_TIMEOUT = int(entry_timeout.get())
        msg = json.dumps({"interval": intervalo, "max_children": max_filhos})
        send_message(msg)
        print(f"📤 Config enviado: {msg} | 🕒 Timeout atualizado para {NODE_TIMEOUT}s")
    except ValueError:
        print("❌ Valores inválidos.")

def enviar_ping():
    global selected_node_mac
    if selected_node_mac:
        print("ping no {}".format(selected_node_mac))
        ping_timers[selected_node_mac] = time.time()  # Marca tempo de envio
        msg = json.dumps({"target": selected_node_mac, "action": "ping"})
        send_message(msg)
        print(f"📤 Comando de ping enviado para {selected_node_mac}")


def atualizar_lista_nos(listbox):
    global last_node_snapshot
    if NODE_TIMEOUT == 0:
        return  # 👈 não atualiza a lista se o timeout é infinito

    with lock:
        current_snapshot = set()
        display_lines = []

        for node in sorted(G.nodes(), key=lambda n: G.nodes[n].get("hops", float("inf"))):
            if G.nodes[node].get("is_router", False):
                continue
            hops = G.nodes[node].get("hops", "?")
            if G.nodes[node].get("is_root", False):
                label = f"{node} ✅ ROOT (Hop {hops})"
            else:
                label = f"{node} 📶 CHILD (Hop {hops})"
            display_lines.append(label)
            current_snapshot.add(label)

        if current_snapshot != last_node_snapshot:
            listbox.delete(0, tk.END)
            for label in display_lines:
                listbox.insert(tk.END, label)
            last_node_snapshot = current_snapshot


def ao_marcar_no(event):
    global selected_node_mac, highlight_timer_id, selected_node_mac_draw
    widget = event.widget
    if not widget.curselection():
        return
    index = widget.curselection()[0]
    texto = widget.get(index)
    mac = texto.split()[0]
    selected_node_mac = mac
    selected_node_mac_draw = True

    # Agendar remoção do círculo após 3 segundos
    if highlight_timer_id is not None:
        root.after_cancel(highlight_timer_id)
    highlight_timer_id = root.after(3000, limpar_destaque)  # 3000 ms = 3s

def limpar_destaque():
    global selected_node_mac, highlight_timer_id, selected_node_mac_draw
    selected_node_mac_draw = False
    highlight_timer_id = None


def ao_selecionar_no(event):
    widget = event.widget
    if not widget.curselection():
        return
    index = widget.curselection()[0]
    texto = widget.get(index)
    mac = texto.split()[0]
    msg = json.dumps({"target": mac, "action": "blink"})
    send_message(msg)
    print(f"📤 Comando de piscar LED enviado para {mac}")


def main():
    after_id = None
    global mqtt_client, running, root
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

    root = tk.Tk()
    root.title("Mesh Network Viewer")

    main_frame = ttk.Frame(root)
    main_frame.pack(fill=tk.BOTH, expand=True)

    left_panel = ttk.Frame(main_frame, width=200)
    left_panel.pack(side=tk.LEFT, fill=tk.Y, padx=5, pady=5)
    ttk.Label(left_panel, text="🌐 Nós ativos:").pack(anchor=tk.W)

    listbox_nodes = tk.Listbox(left_panel, width=40)
    listbox_nodes.bind("<<ListboxSelect>>", ao_marcar_no)  # Clique simples: só destaca
    listbox_nodes.bind("<Double-Button-1>", ao_selecionar_no)  # Duplo clique: envia comando

    listbox_nodes.pack(fill=tk.BOTH, expand=True)

    right_panel = ttk.Frame(main_frame)
    right_panel.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

    fig, ax = plt.subplots(figsize=(10, 7))
    canvas = FigureCanvasTkAgg(fig, master=right_panel)
    canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

    frame = ttk.Frame(root, padding=10)
    frame.pack(fill=tk.X)

    ttk.Label(frame, text="Intervalo de envio (ms):").pack(side=tk.LEFT)
    entry_intervalo = ttk.Entry(frame, width=10)
    entry_intervalo.pack(side=tk.LEFT, padx=5)
    entry_intervalo.insert(0, "5000")

    ttk.Label(frame, text="Máx. filhos:").pack(side=tk.LEFT)
    entry_maxfilhos = ttk.Entry(frame, width=5)
    entry_maxfilhos.pack(side=tk.LEFT, padx=5)
    entry_maxfilhos.insert(0, "1")

    ttk.Label(frame, text="Node Timeout (s):").pack(side=tk.LEFT)
    entry_timeout = ttk.Entry(frame, width=5)
    entry_timeout.pack(side=tk.LEFT, padx=5)
    entry_timeout.insert(0, str(NODE_TIMEOUT))

    ttk.Button(
        frame,
        text="Configurar",
        command=lambda: enviar_config(entry_intervalo, entry_maxfilhos, entry_timeout)
    ).pack(side=tk.LEFT, padx=10)

    ttk.Button(
        frame,
        text="Ping",
        command=lambda: enviar_ping()
    ).pack(side=tk.RIGHT, padx=10)

    def atualizar_interface():
        plot_graph(ax, canvas)
        atualizar_lista_nos(listbox_nodes)

    def agendar_atualizacao():
        nonlocal after_id
        if running:
            atualizar_interface()
            after_id = root.after(100, agendar_atualizacao)

    def on_close():
        global running
        running = False
        if after_id is not None:
            try:
                root.after_cancel(after_id)
            except:
                pass
        if mosquitto_process:
            mosquitto_process.terminate()
        root.quit()  # <- Sai imediatamente da mainloop
        root.destroy()  # <- Destroi a janela e libera recursos

    root.protocol("WM_DELETE_WINDOW", on_close)

    agendar_atualizacao()
    root.mainloop()

    print("✅ Aplicação finalizada com sucesso.")

print(f"🌐 IP local detectado: {get_local_ip()}")

if __name__ == "__main__":
    main()
