# Mesh Network ESP32

Projeto de rede mesh utilizando ESP32, que publica informações da rede em um servidor broker MQTT. Esses dados podem ser visualizados por meio de um configurador desenvolvido em Python.

---

## Configurator

Aplicativo de código aberto para visualização gráfica da topologia da rede mesh ESP32.

### Requisitos

- **Linguagem utilizada**:  
  [Python](https://www.python.org/downloads/)

- **Plataforma de desenvolvimento recomendada**:  
  [PyCharm](https://www.jetbrains.com/pt-br/pycharm/download/?section=windows)

- **Broker MQTT utilizado**:  
  [Mosquitto](https://mosquitto.org/download/)

### Instalação dos pacotes

O projeto inclui um arquivo `requirements.txt` que lista todas as dependências necessárias. Para instalar os pacotes, utilize o comando abaixo:

```bash
pip install -r requirements.txt
```

### Configuração do broker Mosquitto

Após instalar o Mosquitto, é necessário modificar seu arquivo de configuração para permitir conexões externas.

1. Acesse a pasta de instalação do Mosquitto (geralmente em:  
   `C:\Program Files\mosquitto` no Windows).
2. Localize o arquivo `mosquitto.conf`.
3. Abra o arquivo com permissões de administrador.
4. Adicione as seguintes linhas ao final do arquivo:

```conf
listener 1883
bind_address 0.0.0.0
allow_anonymous true
```

5. Salve o arquivo e reinicie o serviço do Mosquitto, ou execute o broker novamente utilizando esse arquivo de configuração.

### Configuração do IP no configurador

No arquivo `main.py` do configurador, caso deseje utilizar um IP fixo para o broker MQTT:

1. Comente a linha contendo:
   ```python
   MQTT_BROKER = get_local_ip()
   ```
2. Descomente a linha seguinte e substitua pelo IP do broker:
   ```python
   MQTT_BROKER = "192.168.1.100"  # exemplo
   ```

---

## ESP32

Firmware de código aberto para ESP32, utilizando rede Wi-Fi Mesh. O nó raiz da rede é responsável por transmitir periodicamente dados para o broker MQTT, permitindo que o configurador visualize a topologia completa da rede.

### Requisitos

- **Plataforma de desenvolvimento**:  
  [Visual Studio Code (VSCode)](https://code.visualstudio.com/)

- **Extensão necessária**:  
  [Espressif IDF Extension for VSCode](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension)

- **Framework**:  
  [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)

### Instruções de uso

1. Instale o VSCode e a extensão **Espressif IDF**.
2. Configure o ambiente seguindo o assistente da extensão (ESP-IDF, Python, Git, etc).
3. No `menuconfig`, configure o SSID e senha do seu Wi-Fi (menu: `Example Configuration`).
4. No código, localize a linha:
   ```c
   #define MQTT_IP "mqtt://192.168.50.208"
   ```
   e substitua pelo IP do broker MQTT usado no configurador.
5. Compile o firmware usando o botão **Build** da extensão.
6. Grave o firmware usando o botão **Flash**.


---

### Alternativa: Uso com ESP-IDF (linha de comando)

Caso esteja utilizando apenas o framework ESP-IDF via terminal, siga os passos abaixo:

1. Instale o [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/) no seu sistema.
2. Configure o SSID e a senha do seu Wi-Fi em:
   ```bash
   idf.py menuconfig
   ```
   Vá em `Example Configuration` e defina o SSID e Password corretos.
3. Altere a linha com o IP do broker MQTT:
   ```c
   #define MQTT_IP "mqtt://192.168.50.208"
   ```
   para o IP usado no seu configurador Python/Mosquitto.
4. Compile o projeto com:
   ```bash
   idf.py build
   ```
5. Conecte seu ESP32 via USB e grave o firmware com:
   ```bash
   idf.py -p COM5 flash
   ```
   *(substitua `COM5` pela porta serial correspondente ao seu dispositivo).*
6. Para visualizar logs em tempo real, use:
   ```bash
   idf.py -p COM5 monitor
   ```

---

