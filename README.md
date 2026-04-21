# Projeto Hércules I

**Equipe A2 — IPE I — Instituto Militar de Engenharia — 2026.1**

Catapulta de palitos de picolé com controle programável via Bluetooth Low Energy (BLE).
O sistema permite selecionar a distância de lançamento (0,5 m a 4,0 m) pelo celular e acionar o disparo remotamente.

![Hardware](https://img.shields.io/badge/Hardware-ESP32%20%2B%20A4988%20%2B%20NEMA17-blue)
![Firmware](https://img.shields.io/badge/Firmware-v1.1.0-brightgreen)
![App](https://img.shields.io/badge/App-React%20Native%20%2B%20Expo-blueviolet)
![Simulação](https://img.shields.io/badge/Simulação-Wokwi-orange)

---

## 🎯 Sumário Executivo

| Aspecto | Status |
|---------|--------|
| **Firmware** | v1.1.0 — Non-blocking FSM, homing, mutex FreeRTOS |
| **Hardware** | ESP32 + A4988 + NEMA 17 + SG90 + Endstop NC + Bateria 9V |
| **Simulação** | Wokwi — A4988 + AccelStepper (idêntico ao real) |
| **Alcance** | 0,5 m a 4,0 m (via lookup table calibrável) |
| **Controle** | BLE (app mobile) ou Serial Monitor (testes) |

---

## 📚 Índice Rápido

1. **[Comece aqui](#comece-aqui)** — Setup e quick start
2. **[Simulação no Wokwi](#simulação-no-wokwi)** — Testar sem hardware
3. **[Instalação do Firmware](#instalação-do-firmware)** — Upload no ESP32
4. **[App Mobile](#execução-do-aplicativo-mobile)** — React Native
5. **[Calibração](#procedimento-de-calibração)** — Ajustar distâncias
6. **[Circuito e Hardware](#avaliação-do-circuito)** — Esquemático e componentes
7. **[Troubleshooting](#solução-de-problemas)** — Diagnosticar problemas

---

## 🚀 Comece aqui

### Opção 1: Simular no Wokwi (5 minutos)
```bash
# No VS Code:
F1 → Wokwi: Start Simulator
# Abra o Serial Monitor → Digite: SET:1.50  ARM  FIRE  ABORT  HOME
```

### Opção 2: Carregar no ESP32 (15 minutos)
```bash
# Requisitos: Arduino IDE 2.x + ESP32 + USB
# 1. Abra: hercules-i/firmware/hercules_firmware/
# 2. Ferramentas → Placa → ESP32 Dev Module
# 3. Ferramentas → Porta → /dev/ttyUSB0
# 4. Clique "Carregar" (→)
```

### Opção 3: Testar com App Mobile (30 minutos)
```bash
cd hercules-i/app/hercules-app
npm install
npx expo start
# Escaneie QR com Expo Go (Android/iOS)
```

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

## Changelog do Firmware

### v1.1.0 — Abril/2026

- **Controle de motor não-bloqueante:** loop principal nunca trava em `while`; motor é executado iterativamente em cada ciclo do `loop()`, eliminando risco de reset pelo Task Watchdog Timer (TWDT) do ESP32.
- **ABORT seguro entre tasks:** flag `volatile abortSolicitado` permite que a task BLE solicite abort sem chamar diretamente funções de hardware, corrigindo race condition com a task principal.
- **Mutex FreeRTOS no buffer de comandos:** `xSemaphoreCreateMutex()` protege `bufferComando`/`novoComando`, garantindo que um comando não seja corrompido por escrita concorrente da task BLE.
- **`posicaoMotor` removida:** variável morta eliminada; posição rastreada internamente pelo `AccelStepper::currentPosition()`.
- **Motor habilitado durante ARMED:** driver permanece ativo no estado ARMED para evitar deriva do motor causada pela tensão do elástico. Impacto: aquecimento moderado do driver por até 30 s (tempo do timeout).
- **Homing com fim de curso:** sequência de homing executada no `setup()` e via comando `HOME`. Motor recua até acionar o microswitch (GPIO 25) e zera a posição — garante reprodutibilidade entre ciclos.
- **Delay do FIRING não-bloqueante:** substituído `delay(500)` por timestamp (`millis()`), mantendo o loop responsivo durante a atuação do servo.
- **Novo estado HOMING** adicionado à FSM.
- **Novo comando BLE `HOME`:** re-executa homing em campo sem precisar reinicializar o ESP32.

---

## Diagrama de Pinagem

| Função              | GPIO | Descrição                                      |
|---------------------|------|------------------------------------------------|
| STEP (motor)        | 26   | Pulso de passo — driver A4988/DRV8825          |
| DIR (motor)         | 27   | Direção do motor — driver A4988/DRV8825        |
| ENABLE (motor)      | 14   | Habilita driver (LOW = ativo)                  |
| Servo (disparo)     | 13   | PWM do servo SG90/MG996R                       |
| Fim de curso        | 25   | Microswitch NC — homing (INPUT_PULLUP)         |
| ADC Bateria         | 34   | Entrada analógica only — divisor resistivo     |
| LED status          | 2    | LED onboard do ESP32                           |

### Divisor resistivo de bateria

```
Banco (9V) ── R1 (100 kΩ) ──┬── R2 (10 kΩ) ── GND
                             └── GPIO 34 (ADC)

Fator = (R1 + R2) / R2 = (100k + 10k) / 10k = 11
Tensão no pino = V_bat / 11  (≤ 3,3 V para o ADC do ESP32)
```

---

## Protocolo de Comandos BLE

| Comando            | Estado exigido | Ação                                                     |
|--------------------|----------------|----------------------------------------------------------|
| `SET:1.50`         | IDLE           | Define distância alvo = 1,50 m (busca na lookup table)   |
| `ARM`              | IDLE           | Inicia tensionamento do elástico                         |
| `FIRE`             | ARMED          | Dispara (aceito apenas no estado ARMED)                  |
| `ABORT`            | Qualquer       | Interrompe operação e retorna motor ao zero              |
| `HOME`             | IDLE           | Re-executa homing via fim de curso                       |
| `STATUS`           | Qualquer       | Responde com estado, posição, bateria e distância alvo   |
| `CAL:1.00:260`     | Qualquer       | Atualiza lookup table: 1,00 m = 260 passos (RAM)         |

### UUIDs BLE

```
Serviço:    12345678-1234-1234-1234-123456789abc
CMD (W):    12345678-1234-1234-1234-123456789ab1
STATUS (N): 12345678-1234-1234-1234-123456789ab2
```

### Notificações enviadas pelo ESP32

| Mensagem                  | Significado                                      |
|---------------------------|--------------------------------------------------|
| `HOMING`                  | Sequência de homing iniciada                     |
| `HOME_OK`                 | Posição zero estabelecida com sucesso            |
| `HOME_WARN:ENDSTOP_...`   | Homing concluído mas endstop não detectado       |
| `TENSIONING:NNN`          | Tensionando com NNN passos                       |
| `ARMED`                   | Pronto para disparar                             |
| `FIRED`                   | Disparo executado                                |
| `RETURNING`               | Motor retornando ao zero                         |
| `IDLE`                    | Sistema ocioso, pronto para próximo ciclo        |
| `BATTERY_LOW:XX%`         | Bateria abaixo de 85% da nominal                 |
| `STATUS:...:POS:NNN`      | Resposta ao comando STATUS (inclui posição)      |
| `ABORT:TIMEOUT`           | ABORT automático por timeout de 30 s em ARMED    |
| `ABORT:RETURNING`         | ABORT solicitado — motor retornando              |

---

## Máquina de Estados (FSM)

```
                   (setup)
                      │
                   HOMING
                      │ endstop acionado
                      ↓
         SET + ARM                  FIRE
  IDLE ──────────→ TENSIONING → ARMED ──────→ FIRING
   ↑                                              │
   │          ABORT (qualquer estado)             │ (após 500 ms)
   └──────────────── RETURNING ←─────────────────┘
```

| Estado      | LED           | Motor         | Descrição                              |
|-------------|---------------|---------------|----------------------------------------|
| IDLE        | Pisca 1 Hz    | Desabilitado  | Aguardando comando                     |
| HOMING      | Pisca 5 Hz    | Habilitado    | Recuando até o fim de curso            |
| TENSIONING  | Pisca 5 Hz    | Habilitado    | Motor tensionando elástico             |
| ARMED       | Fixo aceso    | **Habilitado**| Aguardando FIRE (timeout: 30 s)        |
| FIRING      | Pisca rápido  | Habilitado    | Servo liberando gatilho                |
| RETURNING   | Pisca rápido  | Habilitado    | Motor retornando ao zero               |

> **Por que o motor fica habilitado em ARMED?** Para evitar que a tensão do elástico faça o motor recuar (back-drive), o que deslocaria a posição e afetaria a reprodutibilidade do lançamento. O driver dissipa calor moderado durante esse período (máx. 30 s).

---

## Avaliação do Circuito

### Torque do motor de passo

O NEMA 17 típico (ex: 17HS4401) possui **~40 N·cm de torque de retenção** e ~28 N·cm em movimento.

Assumindo conexão via carretel/polia no eixo com raio ~5 mm:

```
F = torque / raio = 0,28 N·m / 0,005 m ≈ 56 N
```

Para elásticos de laboratório leves, isso pode ser suficiente. Para alcances superiores a 3 m com elásticos mais resistentes, o torque provavelmente será insuficiente.

**Como verificar:** meça a força do elástico no ponto de máxima tensão (1485 passos) com um dinamômetro. Se F > 50 N, considere uma das soluções abaixo:

| Solução | Ganho | Custo |
|---------|-------|-------|
| Reduzir microstepping para full-step | Até 4× mais torque | Vibração maior |
| Redução por engrenagem worm 10:1 | ~10× mais torque | Velocidade reduzida |
| Trocar para NEMA 23 | ~120 N·cm | Troca de driver e estrutura |

### Padronização da posição de tensionamento

**Problema:** sem sensor de referência, qualquer reset de energia ou perda de passo causa deriva de posição acumulada entre ciclos.

**Solução implementada (v1.1.0):** homing via microswitch NC (GPIO 25) executado a cada inicialização e disponível pelo comando `HOME`. O motor recua até acionar o switch e zera a posição internamente via `motor.setCurrentPosition(0)`.

**Recomendação de hardware:** use um microswitch NC como o **Omron SS-5GL** ou similar, montado no ponto de retorno máximo do mecanismo (posição de repouso da mola/elástico totalmente relaxado).

### Problemas críticos identificados no circuito

| Problema | Risco | Solução |
|----------|-------|---------|
| Sem capacitor no VMOT do driver | Picos de back-EMF ao acionar o motor podem queimar o A4988/DRV8825 | Adicionar 100 µF eletrolítico + 100 nF cerâmico em paralelo no pino VMOT |
| Servo alimentado pelo 5V do ESP32 | MG996R consome até 2,5 A em stall — brownout no ESP32 | Alimentar o servo diretamente pela saída do step-down, não pelo pino 5V do ESP32 |
| Sem resistor na linha PWM do servo | Descarga eletrostática pode queimar o GPIO 13 | Adicionar 220 Ω em série entre GPIO 13 e o sinal PWM |
| Sem fim de curso (v1.0) | Posição perdida a cada reset | Corrigido na v1.1.0: microswitch NC no GPIO 25 |

---

## Esquemático para EasyEDA

O Wokwi serve para **simulação funcional**. Para documentação do circuito real com alimentação, decoupling e proteções, use o **[EasyEDA](https://easyeda.com)** (gratuito, web-based).

### Lista de Componentes (BOM)

| Ref  | Componente           | Valor / Modelo              | Qtd |
|------|----------------------|-----------------------------|-----|
| U1   | ESP32 DevKit V1      | ESP32-WROOM-32              | 1   |
| U2   | Driver motor de passo| A4988 ou DRV8825            | 1   |
| U3   | Conversor step-down  | MP1584 (módulo)             | 1   |
| M1   | Motor de passo       | NEMA 17, 4 fios             | 1   |
| SV1  | Servo motor          | SG90 ou MG996R              | 1   |
| SW1  | Microswitch fim de curso | NC — ex: Omron SS-5GL   | 1   |
| BT1  | Porta-pilhas         | 6× AA (9 V total)           | 1   |
| C1   | Capacitor eletrolítico | 100 µF / 16 V             | 1   |
| C2   | Capacitor cerâmico   | 100 nF                      | 1   |
| R1   | Divisor bateria (alto) | 100 kΩ                    | 1   |
| R2   | Divisor bateria (baixo) | 10 kΩ                    | 1   |
| R3   | Proteção PWM servo   | 220 Ω                       | 1   |
| R4   | Limitador LED        | 220 Ω                       | 1   |
| R5   | Pullup endstop       | 10 kΩ                       | 1   |
| LED1 | LED de status        | 5 mm, amarelo               | 1   |

### Redes de Alimentação

Crie quatro símbolos de power no EasyEDA:

- **+9V** — saída positiva do porta-pilhas BT1
- **+5V** — saída regulada do step-down U3
- **+3V3** — pino 3V3 do ESP32 (lógica do driver e pullups)
- **GND** — terra comum de todo o circuito

### Conexões por Bloco

**Bloco 1 — Alimentação**

```
BT1(+) → +9V
BT1(−) → GND

U3(VIN)  ← +9V
U3(GND)  ← GND
U3(VOUT) → +5V      (ajustar trimpot para 5,0 V)

ESP32(5V)  ← +5V
ESP32(GND) ← GND
```

**Bloco 2 — Driver A4988/DRV8825**

```
U2(VMOT)  ← +9V
U2(GND)   ← GND
C1(100µF) entre +9V e GND   ← próximo ao pino VMOT
C2(100nF) entre +9V e GND   ← em paralelo com C1

U2(VDD)   ← +3V3
U2(GND)   ← GND

U2(STEP)  ← ESP32(GPIO 26)
U2(DIR)   ← ESP32(GPIO 27)
U2(EN)    ← ESP32(GPIO 14)    [ativo em LOW]

U2(SLEEP) ─┐
U2(RESET) ─┴── +3V3            [driver sempre ativo]

U2(MS1)   ← GND    [full-step — para máximo torque]
U2(MS2)   ← GND
U2(MS3)   ← GND
```

> Para microstepping, consulte a tabela do datasheet do A4988 e conecte MS1/MS2/MS3 conforme o passo desejado. Full-step oferece torque máximo; 1/16 step reduz para ~25% do torque.

**Bloco 3 — Motor NEMA 17**

```
U2(1A) ← M1(Bobina A+)    [fio vermelho — verificar datasheet do motor]
U2(1B) ← M1(Bobina A−)    [fio azul]
U2(2A) ← M1(Bobina B+)    [fio verde]
U2(2B) ← M1(Bobina B−)    [fio preto]
```

**Bloco 4 — Servo**

```
SV1(VCC)    ← +5V            [alimentação direta do step-down]
SV1(GND)    ← GND
SV1(Signal) ← R3(220Ω) ← ESP32(GPIO 13)
```

**Bloco 5 — Fim de Curso (Endstop)**

```
SW1(pino NC) ← ESP32(GPIO 25)
SW1(pino C)  ← GND

R5(10kΩ): entre +3V3 e ESP32(GPIO 25)    [pullup externo]
```

> O firmware configura `INPUT_PULLUP` interno no GPIO 25, tornando R5 opcional. O pullup externo é recomendado para maior imunidade a ruído em ambiente de competição.

**Bloco 6 — Divisor de Tensão (Monitor de Bateria)**

```
+9V ── R1(100kΩ) ──┬── R2(10kΩ) ── GND
                   └── ESP32(GPIO 34)
```

> GPIO 34 é entrada analógica only. Nunca conectar saídas neste pino.

**Bloco 7 — LED de Status**

```
ESP32(GPIO 2) ── R4(220Ω) ── LED1(anodo)
                              LED1(catodo) ── GND
```

### VREF do A4988 (limitação de corrente)

O pino VREF do A4988 controla a corrente máxima nas bobinas. Conecte um trimpot de 10 kΩ entre +5V e GND, com o cursor no pino VREF. A fórmula para o A4988 com R_sense = 0,1 Ω:

```
I_max = VREF / (8 × R_sense)

Para I = 1,0 A → VREF = 0,8 V
Para I = 1,5 A → VREF = 1,2 V
```

Ajuste com o motor aquecendo durante 5 minutos e meça a temperatura do driver. O A4988 suporta até 70 °C sem dissipador.

### Dicas para o EasyEDA

1. Pesquise componentes por: `ESP32 DevKit`, `A4988`, `NEMA17`, `SS-5GL`.
2. Coloque um símbolo `PWR_FLAG` nos nets `+9V`, `+5V` e `GND` para evitar erros de ERC.
3. Organize o esquemático em regiões com caixas de texto: *Alimentação*, *Controle*, *Driver + Motor*, *Atuadores*.
4. Exporte: **File → Export → PDF** para documentação, ou **Export → Netlist** para fabricação de PCB.

---

## Avaliação do Circuito

### Torque do Motor (NEMA 17)
- **Holding torque:** ~40 N·cm (típico do 17HS4401)
- **Running torque:** ~28 N·cm a 9V
- **Força linear (raio 5mm):** ~56 N
- **Recomendação:** Suficiente para elásticos leves. Para alcances >3m, considere gear reduction (worm 10:1) ou NEMA 23.

### Padronização de Posição
✅ **Resolvido no firmware v1.1.0:** comando `HOME` executa homing via endstop NC (GPIO 25), calibrando o zero reprodutível antes de cada ciclo.

### Problemas do Circuito Original
| Problema | Risco | Solução |
|----------|-------|---------|
| Sem capacitor VMOT | Back-EMF queima A4988 | 100 µF + 100 nF em paralelo |
| Servo alimentado do ESP32 | Brownout se MG996R | Alimentar direto do step-down |
| Sem proteção GPIO PWM | ESD danifica pino | Resistor 220Ω na linha PWM |
| Sem endstop | Posição perdida | Adicionado (GPIO 25, NC) |

### Esquemático Completo em EasyEDA
Para recriar o circuito real em [EasyEDA](https://easyeda.com), use esta **BOM e conexões:**

**Lista de Componentes (BOM)**

| Ref | Componente | Valor | Qtd |
|-----|-----------|-------|-----|
| U1 | ESP32 DevKit V1 | ESP32-WROOM-32 | 1 |
| U2 | Driver motor | A4988 ou DRV8825 | 1 |
| U3 | Conversor step-down | MP1584 (módulo) | 1 |
| M1 | Motor de passo | NEMA 17 (4 fios) | 1 |
| SV1 | Servo | SG90 ou MG996R | 1 |
| SW1 | Endstop | Microswitch NC | 1 |
| BT1 | Bateria | 6× AA (9V) | 1 |
| C1 | Capacitor | 100 µF / 16V | 1 |
| C2 | Capacitor | 100 nF | 1 |
| R1 | Resistor divisor | 100 kΩ | 1 |
| R2 | Resistor divisor | 10 kΩ | 1 |
| R3 | Proteção servo | 220 Ω | 1 |
| R4 | Limitador LED | 220 Ω | 1 |
| R5 | Pullup endstop | 10 kΩ | 1 |
| LED1 | LED status | 5mm amarelo | 1 |

**Redes de Alimentação:** Crie em EasyEDA: `+9V`, `+5V`, `+3V3`, `GND`

**Bloco 1 — Alimentação**
```
BT1(+) ──→ +9V
BT1(−) ──→ GND
U3(VIN)  ← +9V
U3(VOUT) → +5V (ajustar trimpot para 5,0V)
```

**Bloco 2 — Driver A4988**
```
U2(VMOT) ← +9V,  C1(100µF) paralelo com C2(100nF) aqui
U2(VDD)  ← +3V3
U2(STEP) ← ESP32(GPIO26)
U2(DIR)  ← ESP32(GPIO27)
U2(EN)   ← ESP32(GPIO14)
U2(SLP/RST) → +3V3 (driver sempre ativo)
U2(MS1/MS2/MS3) → GND (full-step para máximo torque)
```

**Bloco 3 — Motor NEMA 17**
```
U2(1A) ← M1(A+),  U2(1B) ← M1(A−)
U2(2A) ← M1(B+),  U2(2B) ← M1(B−)
```

**Bloco 4 — Servo**
```
SV1(VCC) ← +5V (alimentação separada do ESP32 se possível)
SV1(SIG) ← R3(220Ω) ← ESP32(GPIO13)
SV1(GND) ← GND
```

**Bloco 5 — Endstop (NC)**
```
SW1(NC)  ← ESP32(GPIO25)
R5(10kΩ) entre +3V3 e GPIO25 (pullup externo — firmware já ativa pullup interno)
SW1(outro lado) ← GND
```

**Bloco 6 — Monitor de Bateria**
```
+9V ── R1(100kΩ) ──┬── R2(10kΩ) ── GND
                   └── ESP32(GPIO34)
```

**Bloco 7 — LED de Status**
```
ESP32(GPIO2) ── R4(220Ω) ── LED1(anodo) ── LED1(catodo) ── GND
```

---

## Simulação no Wokwi

A pasta `wokwi/` está preparada para rodar pela extensão do Wokwi no VS Code.

### Arquivos do projeto

| Arquivo | Função |
|---------|--------|
| `wokwi/sketch.ino` | Sketch principal da simulação (v1.1.0) |
| `wokwi/diagram.json` | Circuito virtual: ESP32, motor, servo, LED, potenciômetro e botão endstop |
| `wokwi/libraries.txt` | Declara a biblioteca `ESP32Servo` para o compilador do Wokwi web |
| `wokwi/wokwi.toml` | Configuração da extensão VS Code, incluindo porta serial `4000` |
| `wokwi/build/` | Firmware `.bin` e `.elf` já compilados para a extensão |
| `tools/build-wokwi.sh` | Recompila o firmware da simulação |
| `tools/wokwi-console.py` | Console para enviar comandos ao ESP32 simulado |

### Componentes no diagrama Wokwi

| ID | Tipo | Função |
|----|------|--------|
| `esp` | ESP32 DevKit V1 | Microcontrolador principal |
| `motor` | wokwi-stepper-motor | Motor de passo bipolar |
| `servo` | wokwi-servo | Servo de disparo |
| `led_status` | wokwi-led (amarelo) | LED de status |
| `pot_bat` | wokwi-potentiometer | Simula nível de bateria (GPIO 34) |
| `endstop` | wokwi-pushbutton (verde) | Simula fim de curso (GPIO 25) |

### Como rodar pela extensão do VS Code

1. No VS Code, abra a pasta `hercules-i/wokwi`.
2. Execute `F1 → Wokwi: Start Simulator`.
3. Abra um terminal e rode:

```bash
cd "/home/marcos-kalile/Código ESP32/hercules-i"
tools/wokwi-console.py
```

4. No console, envie comandos:

```
STATUS
SET:1.50
HOME          ← pressione o botão Endstop no simulador quando solicitado
ARM
FIRE
ABORT
TABELA
CAL:1.50:395
```

### Simulando o homing no Wokwi

O comando `HOME` (ou o homing automático do `setup`) moverá o motor para trás e aguardará o botão **Endstop** (verde) ser pressionado no diagrama. Pressione-o para simular o motor chegando na posição home. A posição zero será estabelecida e o sistema transitará para IDLE.

### Fidelidade ao hardware real

A simulação v1.1.0 usa o componente **`wokwi-a4988`** entre o ESP32 e o motor, e o sketch usa **`AccelStepper` no modo `DRIVER`** — exatamente a mesma API do firmware de produção. O fluxo de sinais é idêntico:

```
ESP32 GPIO26 (STEP) ──→ A4988 ──→ Motor (bobinas A+/A-/B+/B-)
ESP32 GPIO27 (DIR)  ──→ A4988
ESP32 GPIO14 (EN)   ──→ A4988
```

Diferenças inevitáveis em relação ao hardware real:

| Aspecto | Simulação Wokwi | Hardware real |
|---------|----------------|---------------|
| Comunicação | Serial Monitor | BLE (ESP32 stack) |
| VMOT do A4988 | 5V (ESP32 VIN) | 9V (banco de pilhas) |
| Bateria | Potenciômetro (0–3,3V) | Divisor R1/R2 (saída 0–0,82V para 9V) |
| Race condition | N/A (single-task) | Mutex FreeRTOS |

### Recompilar a simulação

```bash
cd "/home/marcos-kalile/Código ESP32/hercules-i"
tools/build-wokwi.sh
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
   - BLE já incluso no pacote ESP32

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

> Após o upload, o ESP32 opera de forma completamente autônoma. Não é necessário manter o cabo USB conectado durante a competição.

---

## Execução do Aplicativo Mobile

### Pré-requisitos

```bash
node --version   # >= 18.x
npm install -g expo-cli
```

### Instalação e execução

```bash
cd app/hercules-app
npm install
npx expo start
# Escanear o QR code com o Expo Go (Android/iOS)
```

### Build para produção (APK sem Expo Go)

```bash
npm install -g eas-cli
eas login
eas build --platform android --profile preview
```

---

## Procedimento de Calibração

### 1. Coleta de dados

Para cada distância alvo, realize **mínimo 5 lançamentos** e registre no CSV:

```csv
distancia_m,passos,distancia_real_m,desvio_lateral_cm
1.50,410,1.47,5.1
1.50,410,1.52,4.3
```

### 2. Processamento

```bash
cd calibration
pip install matplotlib   # Opcional, para gráficos
python calibrar.py --input dados_teste.csv --output lookup_table.h
python calibrar.py --input dados_teste.csv --output lookup_table.h --plot
```

### 3. Atualização do firmware

**Opção A — Recompilar:**
```
1. Copie o lookup_table.h gerado para firmware/hercules_firmware/
2. Recompile e faça o upload via Arduino IDE
```

**Opção B — Via BLE em campo:**
```
No app, vá em Calibração e envie:  CAL:1.50:395
O valor é atualizado em RAM até a próxima reinicialização.
```

---

## Teste BLE via Terminal Linux

```bash
cd tools
pip install bleak

python teste_ble.py --scan
python teste_ble.py --connect
python teste_ble.py --cmd "SET:1.50"
python teste_ble.py --cmd "ARM"
python teste_ble.py --cmd "FIRE"
python teste_ble.py --cmd "ABORT"
python teste_ble.py --cmd "HOME"
python teste_ble.py --cmd "STATUS"
python teste_ble.py --monitor
python teste_ble.py --interativo
```

---

## Solução de Problemas

| Problema | Causa provável | Solução |
|----------|---------------|---------|
| App não encontra Hercules-I | BLE desativado / ESP fora de alcance | Ative BT, aproxime o celular (< 5 m) |
| Motor não move | ENABLE não conectado | Verifique GPIO 14 → EN do driver |
| Motor gira em sentido único | Driver A4988/DRV8825 sem alimentação VMOT | Verifique +9V no pino VMOT e capacitor C1 (100 µF) |
| Motor perde passos | Corrente insuficiente ou VREF incorreto | Ajuste o trimpot VREF do A4988/DRV8825 |
| Motor não retorna ao zero | Posição perdida (sem homing) | Execute `HOME` antes de armar; verifique microswitch GPIO 25 |
| ESP32 reinicia ao acionar servo | Brownout — servo no 5V do ESP32 | Alimentar servo direto pela saída do step-down |
| Driver A4988 queima | Sem capacitor no VMOT | Adicionar 100 µF eletrolítico + 100 nF cerâmico no VMOT |
| Console Wokwi não conecta | Simulação parada ou pasta errada | Abra `hercules-i/wokwi` e reinicie `Wokwi: Start Simulator` |
| Motor não move no Wokwi | Firmware antigo | Rode `tools/build-wokwi.sh` e reinicie a simulação |
| Homing não para no Wokwi | Botão Endstop não pressionado | Pressione o botão verde no diagrama durante o homing |
| Servo não libera | PWM fora do range ou sem alimentação | Verifique +5V no SV1(VCC) e resistor R3 (220 Ω) |
| Bateria baixa sem aviso | Divisor resistivo incorreto | Verifique R1 = 100 kΩ e R2 = 10 kΩ |
| ABORT automático frequente | Timeout de 30 s expirando em ARMED | Normal — envie FIRE dentro de 30 s após ARM |

---

## Equipe A2

- **Kalile** — Gerente / Firmware / BLE
- IME 2026.1 — IPE I (Introdução a Projetos de Engenharia I)
