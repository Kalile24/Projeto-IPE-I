# Projeto Hércules I

**Equipe A2 — IPE I — Instituto Militar de Engenharia — 2026.1**

Catapulta de palitos de picolé com controle programável via Bluetooth Low Energy (BLE).
O sistema permite selecionar a distância de lançamento (0,5 m a 4,0 m) pelo celular e acionar o disparo remotamente.

---

## Estrutura do Projeto

```
hercules-i/
├── README.md
├── firmware/
│   └── hercules_firmware/
│       ├── hercules_firmware.ino   ← Firmware principal (Arduino IDE)
│       └── lookup_table.h          ← Tabela de calibração
├── app/
│   └── hercules-app/               ← Aplicativo React Native (Expo)
│       ├── App.js
│       ├── app.json
│       ├── package.json
│       └── src/
│           ├── screens/
│           ├── components/
│           ├── services/
│           └── constants/
├── calibration/
│   ├── calibrar.py                 ← Script de calibração
│   └── dados_exemplo.csv           ← Dados de teste de exemplo
├── tools/
│   └── teste_ble.py                ← Teste BLE via Linux (sem app)
└── wokwi/
    ├── diagram.json                ← Esquema de simulação Wokwi
    ├── libraries.txt               ← Bibliotecas usadas pelo Wokwi
    └── sketch.ino                  ← Firmware adaptado para simulação
```

---

## Simulação no Wokwi

A pasta `wokwi/` está preparada para rodar pela extensão do Wokwi no VS Code.
O fluxo recomendado neste workspace é: iniciar a simulação pela extensão e enviar comandos pelo console local `tools/wokwi-console.py`.

### Arquivos do projeto

| Arquivo | Função |
|---------|--------|
| `wokwi/sketch.ino` | Sketch principal da simulação |
| `wokwi/diagram.json` | Circuito virtual do ESP32, motor, servo, LED e potenciômetro |
| `wokwi/libraries.txt` | Declara a biblioteca `ESP32Servo` para o compilador do Wokwi web |
| `wokwi/wokwi.toml` | Configuração da extensão VS Code, incluindo porta serial `4000` |
| `wokwi/build/` | Firmware `.bin` e `.elf` já compilados para a extensão |
| `tools/build-wokwi.sh` | Recompila o firmware da simulação |
| `tools/wokwi-console.py` | Console para enviar comandos ao ESP32 simulado |

### Como rodar pela extensão do VS Code

1. No VS Code, abra a pasta `hercules-i/wokwi`.
2. Execute `F1 -> Wokwi: Start Simulator`.
3. Deixe a aba da simulação aberta e rodando.
4. Abra um terminal normal do VS Code.
5. Rode:

```bash
cd "/home/marcos-kalile/Código ESP32/hercules-i"
tools/wokwi-console.py
```

6. No console, envie comandos como:

```text
STATUS
SET:1.50
ARM
FIRE
ABORT
TABELA
CAL:1.50:395
```

Para fazer um teste rápido sem entrar no modo interativo:

```bash
tools/wokwi-console.py --cmd STATUS
```

### Recompilar a simulação

O Arduino CLI foi instalado localmente em `.tools/bin/arduino-cli`, e a configuração local fica em `arduino-cli.yaml`.
Depois de editar `wokwi/sketch.ino`, recompile com:

```bash
cd "/home/marcos-kalile/Código ESP32/hercules-i"
tools/build-wokwi.sh
```

O `wokwi/wokwi.toml` aponta para:

```text
wokwi/build/sketch.ino.bin
wokwi/build/sketch.ino.elf
```

### Diagnóstico da serial

Se `tools/wokwi-console.py` não conectar:

1. Confirme que a simulação está rodando.
2. Confirme que o VS Code abriu a pasta `hercules-i/wokwi`.
3. Confirme que `wokwi/wokwi.toml` contém `rfc2217ServerPort = 4000`.
4. Teste a porta:

```bash
nc -vz 127.0.0.1 4000
```

Se a porta estiver fechada, pare e inicie novamente a simulação com `F1 -> Wokwi: Start Simulator`.

> Nota: nesta simulação, o motor de passo do Wokwi fica ligado direto aos GPIOs. Isso é intencional: o simulador usa lógica digital e não modela a corrente das bobinas. No hardware real, continue usando o driver apropriado.

---

## Diagrama de Pinagem

| Função              | GPIO | Descrição                                      |
|---------------------|------|------------------------------------------------|
| STEP (motor)        | 26   | Pulso de passo — driver A4988/DRV8825          |
| DIR (motor)         | 27   | Direção do motor — driver A4988/DRV8825        |
| ENABLE (motor)      | 14   | Habilita driver (LOW = ativo)                  |
| Servo (disparo)     | 13   | PWM do servo SG90/MG996R                       |
| ADC Bateria         | 34   | Entrada analógica only — divisor resistivo     |
| LED status          | 2    | LED onboard do ESP32                           |

### Divisor resistivo de bateria

```
Banco (9V) ──┬── R1 (100 kΩ) ──┬── GND
             │                  │
             │                  └── GPIO 34 (ADC)
             │
             └── (polo negativo ao GND)

Fator = (R1 + R2) / R2 = (100k + 10k) / 10k = 11
Tensão no pino = V_bat / 11  (deve ficar ≤ 3,3V)
```

---

## Protocolo de Comandos BLE

| Comando            | Ação                                                        |
|--------------------|-------------------------------------------------------------|
| `SET:1.50`         | Define distância alvo = 1,50 m (busca na lookup table)      |
| `ARM`              | Inicia tensionamento do elástico                            |
| `FIRE`             | Dispara (aceito apenas no estado ARMED)                     |
| `ABORT`            | Aborta e retorna o motor ao zero                            |
| `STATUS`           | Responde com estado atual + nível de bateria                |
| `CAL:1.00:260`     | Atualiza lookup table: 1,00 m = 260 passos (RAM)            |

### UUIDs BLE

```
Serviço:    12345678-1234-1234-1234-123456789abc
CMD (W):    12345678-1234-1234-1234-123456789ab1
STATUS (N): 12345678-1234-1234-1234-123456789ab2
```

### Notificações enviadas pelo ESP32

| Mensagem            | Significado                              |
|---------------------|------------------------------------------|
| `TENSIONING:NNN`    | Tensionando com NNN passos               |
| `ARMED`             | Pronto para disparar                     |
| `FIRED`             | Disparo executado                        |
| `RETURNING`         | Motor retornando ao zero                 |
| `IDLE`              | Sistema ocioso                           |
| `BATTERY_LOW:XX%`   | Bateria abaixo de 85% da nominal         |
| `STATUS:...:BAT:XX%`| Resposta ao comando STATUS               |
| `ABORT:TIMEOUT`     | ABORT automático por timeout (30s)       |

---

## Instalação do Firmware

### Pré-requisitos (Arduino IDE)

1. Instale o [Arduino IDE 2.x](https://www.arduino.cc/en/software)
2. Adicione o suporte ao ESP32:
   - `Arquivo → Preferências → URLs adicionais:`
   - `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - `Ferramentas → Placa → Gerenciador de Placas → ESP32 → Instalar v3.x`
3. Instale as bibliotecas (Ferramentas → Gerenciar Bibliotecas):
   - `AccelStepper` (by Mike McCauley)
   - `ESP32Servo` (by Kevin Harrington)
   - (BLE já incluso no pacote ESP32)

### Upload do firmware

```bash
# 1. Conecte o ESP32 via USB
ls /dev/ttyUSB*        # Verifica a porta (normalmente /dev/ttyUSB0)

# 2. No Arduino IDE:
#    - Selecione: Ferramentas → Placa → ESP32 Dev Module
#    - Selecione: Ferramentas → Porta → /dev/ttyUSB0
#    - Velocidade: 115200 baud

# 3. Abra a pasta firmware/hercules_firmware/ no Arduino IDE
#    (os dois arquivos .ino e .h devem estar na mesma pasta)

# 4. Clique em "Carregar" (→)
```

> **Importante:** Após o upload, o ESP32 opera de forma completamente autônoma (standalone). Não é necessário manter o cabo USB conectado durante a competição.

---

## Execução do Aplicativo Mobile

### Pré-requisitos

```bash
# Node.js 18+ e npm
node --version   # >= 18.x

# Expo CLI
npm install -g expo-cli

# No Android: habilitar "Modo desenvolvedor" e "Depuração USB"
# Instalar o app Expo Go no celular (para desenvolvimento rápido)
```

### Instalação e execução

```bash
cd app/hercules-app

# Instala dependências
npm install

# Inicia o servidor de desenvolvimento
npx expo start

# Escaneia o QR code com o Expo Go (Android/iOS)
# — ou —
# Pressione 'a' para abrir direto no emulador Android
```

### Build para produção (APK sem Expo Go)

```bash
# Instala EAS CLI
npm install -g eas-cli

# Login na conta Expo
eas login

# Build APK para Android
eas build --platform android --profile preview
```

### Permissões necessárias (Android)

O app solicita automaticamente:
- `BLUETOOTH_SCAN` — para escanear dispositivos BLE
- `BLUETOOTH_CONNECT` — para conectar ao ESP32
- `ACCESS_FINE_LOCATION` — exigido pelo Android para BLE scan

---

## Procedimento de Calibração

### 1. Coleta de dados

Para cada distância alvo, realize **mínimo 5 lançamentos** e registre no CSV:

```csv
distancia_m,passos,distancia_real_m,desvio_lateral_cm
1.50,410,1.47,5.1
1.50,410,1.52,4.3
...
```

### 2. Processamento

```bash
cd calibration

# Instala dependências Python
pip install matplotlib  # Opcional, para gráficos

# Processa dados e gera novo header C++
python calibrar.py --input dados_teste.csv --output lookup_table.h

# Com gráfico:
python calibrar.py --input dados_teste.csv --output lookup_table.h --plot
```

### 3. Atualização do firmware

**Opção A — Recompilar (mais permanente):**
```
1. Copie o lookup_table.h gerado para firmware/hercules_firmware/
2. Recompile e faça o upload via Arduino IDE
```

**Opção B — Via BLE em campo (sem recompilar):**
```
No app, vá em Calibração e envie:  CAL:1.50:395
O valor é atualizado em RAM até a próxima reinicialização.
```

---

## Teste BLE via Terminal Linux (sem app mobile)

```bash
cd tools

# Instala dependência
pip install bleak

# Lista dispositivos BLE próximos
python teste_ble.py --scan

# Testa conexão com o Hercules-I
python teste_ble.py --connect

# Envia comando único
python teste_ble.py --cmd "SET:1.50"
python teste_ble.py --cmd "ARM"
python teste_ble.py --cmd "FIRE"
python teste_ble.py --cmd "ABORT"
python teste_ble.py --cmd "STATUS"

# Monitora notificações por 30s
python teste_ble.py --monitor

# Console interativo (múltiplos comandos)
python teste_ble.py --interativo
```

---

## Simulação Wokwi

Veja as instruções completas na seção **Simulação no Wokwi** no início deste README.
O resumo é: abra `hercules-i/wokwi` no VS Code, inicie com `F1 -> Wokwi: Start Simulator`
e envie comandos por `tools/wokwi-console.py`.

---

## Máquina de Estados (FSM)

```
         SET + ARM                  FIRE
  IDLE ──────────→ TENSIONING → ARMED ──────→ FIRING
   ↑                                              │
   │          ABORT (qualquer estado)             │
   └────────────────────────────────────────── RETURNING
```

| Estado      | LED           | Descrição                              |
|-------------|---------------|----------------------------------------|
| IDLE        | Pisca 1 Hz    | Aguardando comando                     |
| TENSIONING  | Pisca 5 Hz    | Motor tensionando elástico             |
| ARMED       | Fixo aceso    | Aguardando FIRE (timeout: 30s)         |
| FIRING      | Pisca rápido  | Servo liberando gatilho                |
| RETURNING   | Pisca rápido  | Motor retornando ao zero               |

---

## Solução de Problemas

| Problema                    | Causa provável               | Solução                                    |
|-----------------------------|------------------------------|--------------------------------------------|
| App não encontra Hercules-I | BLE desativado / ESP fora de alcance | Ative BT, aproxime o celular (< 5m)   |
| Motor não move              | ENABLE não conectado         | Verifique GPIO 14 → EN do driver           |
| Console Wokwi não conecta   | Simulação parada ou pasta errada no VS Code | Abra `hercules-i/wokwi` e reinicie `Wokwi: Start Simulator` |
| Console conecta sem resposta | Serial não ligada ao monitor | Confira `esp:TX0`/`esp:RX0` em `wokwi/diagram.json` |
| Motor não move no Wokwi     | Firmware antigo ou diagrama antigo | Rode `tools/build-wokwi.sh` e reinicie a simulação |
| Motor perde passos          | Corrente insuficiente        | Ajuste o trimpot do A4988/DRV8825          |
| Servo não libera            | PWM fora do range            | Verifique alimentação 5V do servo          |
| Bateria baixa sem aviso     | Divisor resistivo incorreto  | Verifique R1=100kΩ e R2=10kΩ               |
| ABORT automático frequente  | Timeout 30s expirando        | Normal — envie FIRE dentro de 30s          |

---

## Equipe A2

- **Kalile** — Gerente / Firmware / BLE
- IME 2026.1 — IPE I (Introdução a Projetos de Engenharia I)
