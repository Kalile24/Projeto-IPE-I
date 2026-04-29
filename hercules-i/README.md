# Hércules I

**Equipe A2 — IPE I / Instituto Militar de Engenharia — 2026.1**

Catapulta de palitos de picolé com controle via Bluetooth. Selecione a distância no celular (0,5 m a 4,0 m) e dispare remotamente.

---

## Hardware necessário

| Componente | Quantidade | Observação |
|---|---|---|
| ESP32 DevKit V1 | 1 | Qualquer variante com 38 pinos |
| Driver A4988 | 2 | Um para tensionamento, um para disparo |
| Motor NEMA 17 | 2 | ≥ 40 N·cm de torque |
| Microchave endstop | 1 | NA (normalmente aberto) |
| Step-down MP1584 | 1 | Ajustado para 5V |
| 6× pilhas AA | 1 conjunto | 9V bruto |
| Resistores 100kΩ e 10kΩ | 1 cada | Divisor de tensão para ADC |
| LED + resistor 220Ω | 1 | Status visual |
| Capacitor 100µF + 100nF | 2 pares | Um por driver — VMOT obrigatório |

---

## Pinagem

### Motor de tensionamento (elástico)
| ESP32 | A4988 |
|---|---|
| GPIO 26 | STEP |
| GPIO 27 | DIR |
| GPIO 14 | ENABLE |

### Motor de disparo (gatilho)
| ESP32 | A4988 |
|---|---|
| GPIO 18 | STEP |
| GPIO 19 | DIR |
| GPIO 21 | ENABLE |

### Outros
| GPIO | Função |
|---|---|
| 25 | Fim de curso (microchave NA → GND, pullup interno) |
| 34 | ADC bateria (divisor R1=100kΩ / R2=10kΩ) |
| 2 | LED de status |

### Alimentação dos drivers
```
6× AA → [step-down 5V] → VMOT dos dois A4988
                        → 5V dos dois A4988
ESP32 3V3 → VDD dos dois A4988
GND comum entre tudo
```

> **Atenção:** Coloque os capacitores (100µF + 100nF) nos pinos VMOT/GND de cada driver. Sem eles o driver pode queimar ao energizar o motor.

---

## Montagem do circuito

1. **Step-down:** Conecte as pilhas na entrada. Ajuste o trimpot até a saída marcar exatamente 5,0V com as pilhas instaladas.
2. **Drivers A4988:** MS1, MS2, MS3 no GND (passo inteiro). SLEEP e RESET no 3V3.
3. **Motor de tensionamento:** Bobinas A+ A- → 2A 2B do driver_t; B+ B- → 1A 1B.
4. **Motor de disparo:** Mesma lógica de bobinas no driver_d.
5. **Endstop:** Fio 1 em GPIO 25, fio 2 no GND. Posicione no ponto de zero mecânico.
6. **Divisor bateria:** R1=100kΩ em série com R2=10kΩ. Ponto médio (entre R1 e R2) em GPIO 34. Ponta positiva do divisor na bateria (+9V), ponta negativa no GND.

---

## Testar na simulação (Wokwi)

```bash
cd hercules-i
tools/build-wokwi.sh          # compila o sketch da simulação
```

No VS Code: `F1 → Wokwi: Start Simulator`

Em outro terminal:
```bash
tools/wokwi-console.py
```

Sequência de teste:
```
STATUS          → verifica estado inicial
SET:1.50        → define 1,50 m
ARM             → tensiona o elástico (motor 1 gira)
FIRE            → dispara (motor 2 gira e volta)
STATUS          → confirma retorno ao IDLE
```

Para testar o fim de curso: envie `HOME` e pressione o botão verde no diagrama.

---

## Carregar firmware no ESP32

1. Abra `firmware/hercules_firmware/` no Arduino IDE 2.x
2. Ferramentas → Placa → ESP32 Dev Module
3. Ferramentas → Porta → `/dev/ttyUSB0` (ou COM na Windows)
4. Clique **Carregar**

Bibliotecas necessárias (instalar pelo Library Manager):
- **AccelStepper** (Mike McCaulay)

---

## App mobile (MIT App Inventor — Android)

Comunicação via **Bluetooth Clássico SPP** — sem extensões externas, funciona em qualquer Android.

O arquivo `app/HerculesI.aia` é o projeto pronto para importar.

### Como instalar

1. Acesse [ai2.appinventor.mit.edu](https://ai2.appinventor.mit.edu)
2. **Projects → Import project (.aia)** → selecione `app/HerculesI.aia`
3. **Build → Android App (.apk)** → instale no celular

> Mais rápido para testes: instale **MIT AI2 Companion** (Play Store) e clique **Connect → AI Companion** no site — abre o app na hora sem gerar APK.

### Emparelhar o ESP32 antes de usar

1. No Android: Configurações → Bluetooth → Procurar dispositivos
2. Selecione **Hercules-I** → PIN: **1234**
3. Abra o app → toque **CONECTAR** → selecione Hercules-I na lista

### Fluxo de uso

```
1. CONECTAR → seleciona Hercules-I na lista de dispositivos emparelhados
2. Ajuste a distância (slider ou botões 0.5 / 1.0 / 2.0 / 3.0 / 4.0 m)
3. LANÇAR → catapulta tensiona e dispara automaticamente
4. ABORTAR a qualquer momento retorna ao zero
```

Estado atual (TENSIONING, ARMED, FIRING…) e bateria atualizam a cada 3 segundos.

---

## Estados da FSM

```
IDLE → (ARM) → TENSIONING → (motor chega) → ARMED
ARMED → (FIRE) → FIRING → (motor disparo) → RETURNING → IDLE
Qualquer estado → (ABORT) → RETURNING → IDLE
IDLE → (HOME) → HOMING → IDLE
```

LED de status:
- **Piscando lento (1 Hz):** IDLE
- **Piscando rápido (5 Hz):** TENSIONING / HOMING
- **Aceso fixo:** ARMED
- **Piscando muito rápido:** FIRING / RETURNING
- **SOS em Morse:** bateria crítica (< 85% de 9V)

---

## Protocolo BLE

**Serviço:** `12345678-1234-1234-1234-123456789abc`

| Characterística | UUID | Direção | Exemplos |
|---|---|---|---|
| Comando | `...ab1` | Write | `SET:1.50`, `ARM`, `FIRE`, `ABORT` |
| Status | `...ab2` | Notify | `ARMED`, `FIRED`, `IDLE` |

Comandos completos:
```
SET:X.XX        Distância (0.50 a 4.00 m, passo 0.25 m)
ARM             Inicia tensionamento
FIRE            Aciona disparo
ABORT           Para tudo e retorna ao zero
HOME            Re-executa homing
STATUS          Retorna estado, bateria e posição
CAL:X.XX:NNN    Atualiza passos na RAM (ex: CAL:1.50:395)
```

---

## Calibração

Após testes reais, colete medições e gere uma nova tabela:

```bash
python calibration/calibrar.py --input dados_teste.csv --output firmware/hercules_firmware/lookup_table.h
```

Formato do CSV:
```
distancia_m,passos,distancia_real_m,desvio_lateral_cm
1.50,410,1.47,5
```

Também é possível calibrar em campo sem recompilar:
```
CAL:1.50:395    → define 1,50 m = 395 passos (persiste até reiniciar)
```

---

## Motor de disparo — ajuste físico

O motor de disparo faz **200 passos** (1 volta completa com passo inteiro) para liberar o gatilho. Ajuste a constante `DISPARO_PASSOS` no firmware conforme o mecanismo físico:

```cpp
// hercules_firmware.ino
#define DISPARO_PASSOS  200   // Aumente se o gatilho não liberar completamente
```

Para testar isoladamente na simulação: coloque em ARMED e envie `FIRE` — o motor de disparo (NEMA 17 direito no diagrama) deve girar e retornar.

---

## Solução de problemas

| Sintoma | Causa provável | Solução |
|---|---|---|
| Motor não gira | ENABLE no nível errado | Confirme `MOTOR_ENABLE_ON LOW` no código |
| Homing sem parar | Endstop não detectado | Verifique fiação GPIO 25 → chave → GND |
| Motor treme / perde passo | Corrente do driver mal ajustada | Ajuste o trimpot do A4988 (Vref = I × 8 × Rs) |
| BLE não aparece | ESP32 sem alimentação estável | Use USB + fonte externa para VMOT separados |
| App não conecta | Permissões Bluetooth negadas | Configurações → Apps → Expo Go → Permissões → Bluetooth |
| Motor de disparo não retorna | `DISPARO_DELAY_MS` muito curto | Aumente o valor no firmware |

---

## Estrutura do projeto

```
hercules-i/
├── firmware/
│   └── hercules_firmware/
│       ├── hercules_firmware.ino   ← Firmware principal
│       └── lookup_table.h          ← Tabela de calibração
├── wokwi/
│   ├── sketch.ino                  ← Firmware da simulação
│   ├── diagram.json                ← Circuito virtual
│   └── wokwi.toml
├── app/
│   └── hercules-app/               ← App React Native (Expo)
├── calibration/
│   ├── calibrar.py                 ← Gerador de lookup_table.h
│   └── dados_exemplo.csv
└── tools/
    ├── build-wokwi.sh              ← Compila simulação
    ├── wokwi-console.py            ← Console serial para Wokwi
    └── teste_ble.py                ← Teste BLE via Linux
```
