# Hércules I

**Equipe A2 — IPE I / Instituto Militar de Engenharia — 2026.1**

Catapulta de palitos de picolé com controle via Bluetooth. Selecione a distância (0,5 m a 4,0 m) e dispare pelo celular.

---

## Lista de materiais

| Componente | Qtd |
|---|---|
| ESP32 DevKit V1 | 1 |
| Driver A4988 | 2 |
| Motor NEMA 17 (≥ 40 N·cm) | 2 |
| Microchave fim de curso (NA) | 1 |
| Conversor step-down MP1584 | 1 |
| 6× pilhas AA | 1 conjunto |
| Resistor 100 kΩ | 1 |
| Resistor 10 kΩ | 1 |
| Capacitor 100 µF + 100 nF | 2 pares (1 por driver) |
| LED + resistor 220 Ω | 1 |

---

## Pinagem

| GPIO | Função |
|---|---|
| 26 | Motor tensionamento — STEP |
| 27 | Motor tensionamento — DIR |
| 14 | Motor tensionamento — ENABLE |
| 18 | Motor disparo — STEP |
| 19 | Motor disparo — DIR |
| 21 | Motor disparo — ENABLE |
| 25 | Fim de curso (NA → GND, pullup interno) |
| 34 | ADC bateria (divisor 100 kΩ / 10 kΩ) |
| 2 | LED de status |

> O motor de disparo **não precisa de fim de curso** — ele gira um número fixo de passos e retorna à posição zero por contagem interna.

---

## Montagem

1. **Step-down:** ligue as pilhas na entrada e ajuste o trimpot para **5,0 V** na saída (meça com multímetro com as pilhas instaladas).
2. **A4988 — configuração de microstepping:** MS1, MS2, MS3 → GND (passo inteiro). SLEEP e RESET → 3V3.
3. **A4988 — alimentação:** VMOT e GND do motor → saída do step-down (5 V). VDD → 3V3 do ESP32. GND comum.
4. **Capacitores:** solde 100 µF + 100 nF entre VMOT e GND de **cada** driver. Sem eles o driver queima ao energizar.
5. **Motores:** conecte as bobinas conforme a identificação do cabo (A+/A−/B+/B−). Se o motor girar ao contrário, inverta um par de bobinas.
6. **Fim de curso:** um fio em GPIO 25, outro no GND. Posicione no ponto de zero mecânico do motor de tensionamento.
7. **Divisor de bateria:** R1 = 100 kΩ em série com R2 = 10 kΩ. O ponto médio vai ao GPIO 34. R1 liga à bateria (+9 V) e R2 ao GND.

> O Wokwi (`wokwi/diagram.json`) mostra todas as ligações de forma visual — use como referência para a montagem física.

---

## Testando pelo PC (Serial Monitor)

O firmware imprime tudo no Serial (115200 baud) e aceita os mesmos comandos pelo Serial Monitor do Arduino IDE. **Teste assim antes de usar o celular.**

1. Carregue o firmware no ESP32 (Arduino IDE → Carregar)
2. Abra o Serial Monitor (115200 baud, terminação LF)
3. Digite os comandos:

```
STATUS          → mostra estado atual e bateria
LAUNCH:1.50     → tensiona e dispara em 1,50 m (sequência completa)
ABORT           → para tudo e retorna ao zero
HOME            → executa homing (acione o fim de curso quando pedido)
```

Todos os comandos disponíveis:

| Comando | Função |
|---|---|
| `LAUNCH:X.XX` | Sequência completa — tensiona e dispara |
| `SET:X.XX` | Define distância sem armar |
| `ARM` | Arma (requer SET antes) |
| `FIRE` | Dispara (requer ARMED) |
| `ABORT` | Para tudo, retorna ao zero |
| `HOME` | Re-executa homing |
| `STATUS` | Estado, bateria e posição |
| `CAL:X.XX:N` | Ajusta passos na RAM (ex: `CAL:1.50:395`) |

---

## App Android (MIT App Inventor)

Comunicação via **Bluetooth Clássico** — sem extensões, funciona em qualquer Android.

### Instalação

1. Acesse [ai2.appinventor.mit.edu](https://ai2.appinventor.mit.edu)
2. **Projects → Import project (.aia)** → selecione `app/HerculesI.aia`
3. **Build → Android App (.apk)** → instale no celular

> Teste rápido sem gerar APK: instale **MIT AI2 Companion** (Play Store) e use **Connect → AI Companion**.

### Emparelhamento (fazer uma vez)

1. Ligue o ESP32 → aguarde o LED piscar
2. Android → Configurações → Bluetooth → selecione **Hercules-I** → PIN: **1234**

### Uso

1. Abra o app → **CONECTAR** → selecione Hercules-I
2. Escolha a distância (slider ou botões rápidos)
3. **LANÇAR** → catapulta tensiona e dispara automaticamente
4. **ABORTAR** cancela e retorna ao zero a qualquer momento

---

## Calibração

Após testes reais, ajuste a tabela de passos:

```bash
python calibration/calibrar.py --input dados_teste.csv --output firmware/hercules_firmware/lookup_table.h
```

Formato do CSV:
```
distancia_m,passos,distancia_real_m,desvio_lateral_cm
1.50,410,1.47,5
```

Ou ajuste em campo sem recompilar:
```
CAL:1.50:395    → define 1,50 m = 395 passos (persiste até reiniciar)
```

---

## LED de status

| Padrão | Estado |
|---|---|
| Pisca lento (1 Hz) | IDLE — aguardando comando |
| Pisca rápido (5 Hz) | TENSIONING / HOMING |
| Aceso fixo | ARMED — pronto para disparar |
| Pisca muito rápido | FIRING / RETURNING |
| SOS Morse | Bateria crítica (< 7,65 V) |

---

## Ajuste do motor de disparo

O motor de disparo gira **200 passos** (1 volta completa) para liberar o gatilho. Se o mecanismo não liberar completamente, aumente este valor no firmware:

```cpp
#define DISPARO_PASSOS  200   // ajuste conforme o mecanismo físico
```

---

## Problemas comuns

| Sintoma | Solução |
|---|---|
| Motor não gira | Verifique ENABLE — deve estar em LOW para ativar |
| Homing não para | Verifique fiação: GPIO 25 → chave → GND |
| Motor treme ou perde passo | Ajuste o trimpot do A4988 (Vref = I_motor × 8 × Rs) |
| Bluetooth não aparece | Aguarde ~5 s após ligar o ESP32 antes de procurar |
| App não conecta | Refaça o emparelhamento no Android |

---

## Estrutura do projeto

```
hercules-i/
├── firmware/hercules_firmware/
│   ├── hercules_firmware.ino   ← Firmware principal (Arduino IDE)
│   └── lookup_table.h          ← Tabela distância → passos
├── wokwi/
│   ├── sketch.ino              ← Simulação (Serial puro)
│   ├── diagram.json            ← Circuito virtual (referência de montagem)
│   └── wokwi.toml
├── app/
│   └── HerculesI.aia           ← App MIT App Inventor
├── calibration/
│   ├── calibrar.py
│   └── dados_exemplo.csv
└── tools/
    ├── build-wokwi.sh
    └── wokwi-console.py
```
