# Guia Tecnico - Hercules I

**Equipe A2 - IPE I / Instituto Militar de Engenharia - 2026.1**

Este guia explica a montagem, o firmware, a simulacao, o app Android e os pontos que voce provavelmente vai alterar manualmente. Ele descreve o estado atual do projeto: ESP32, dois motores 28BYJ-48, dois drivers ULN2003 e alimentacao por 4 pilhas AA de 1,5 V.

---

## 1. Arquitetura Atual

O Hercules I usa dois atuadores independentes:

| Subsistema | Funcao | Hardware |
|---|---|---|
| Tensionamento | Puxa o elastico ate a posicao calibrada | 28BYJ-48 + ULN2003 |
| Disparo | Move o gatilho e retorna ao zero | 28BYJ-48 + ULN2003 |
| Controle | Recebe comandos e executa a FSM | ESP32 |
| App | Envia distancia e aborta ciclos | MIT App Inventor + Bluetooth Classico |

Fluxo normal:

1. Coloque manualmente o tensionamento no zero mecanico.
2. Ligue o ESP32 e os drivers.
3. Envie `HOME` para zerar a posicao atual no firmware.
4. Envie `LAUNCH:1.50` ou use o app.
5. O motor 1 tensiona ate a posicao calibrada.
6. A engrenagem/trava mecanica segura a carga.
7. O firmware aguarda 1 s para a trava assentar.
8. O motor 1 retorna ao zero sem soltar a carga.
9. O motor 2 libera a engrenagem/trava e completa o disparo.

---

## 2. Lista de Materiais

| Componente | Quantidade | Observacao |
|---|---:|---|
| ESP32 DevKit V1 | 1 | Controle e Bluetooth |
| Motor 28BYJ-48 | 2 | Preferencialmente 5 V |
| Modulo ULN2003 | 2 | Um por motor |
| Porta-pilhas 4xAA | 1 | 6 V nominal com pilhas alcalinas novas |
| Pilhas AA 1,5 V | 4 | Use novas ou recem-carregadas |
| LED + resistor 220 ohm | 1 | Opcional se usar LED onboard |
| Jumpers | varios | Mantenha GND comum |

Nao ha sensor de fim de curso obrigatorio e nao ha divisor de bateria obrigatorio.

---

## 3. Pinagem

### Motor de Tensionamento

| ESP32 | ULN2003 | Observacao |
|---|---|---|
| GPIO 26 | IN1 | Bobina 1 |
| GPIO 27 | IN2 | Bobina 2 |
| GPIO 14 | IN3 | Bobina 3 |
| GPIO 25 | IN4 | Bobina 4 |

### Motor de Disparo

| ESP32 | ULN2003 | Observacao |
|---|---|---|
| GPIO 18 | IN1 | Bobina 1 |
| GPIO 19 | IN2 | Bobina 2 |
| GPIO 21 | IN3 | Bobina 3 |
| GPIO 22 | IN4 | Bobina 4 |

### Alimentacao

| Ligacao | Destino |
|---|---|
| 4xAA positivo | VCC dos dois ULN2003 |
| 4xAA negativo | GND dos dois ULN2003 |
| GND ESP32 | GND comum dos ULN2003 |
| ESP32 5V/USB | Alimente o ESP32 pelo USB durante desenvolvimento |

O ponto crucial e o GND comum: se o ESP32 e os ULN2003 nao compartilharem GND, os sinais IN1-IN4 podem ficar imprevisiveis.

---

## 4. Sobre Bateria e ADC

O ADC e uma entrada analogica do ESP32 usada para medir tensao. Ele nao e necessario para mover os motores, por isso foi removido do caminho obrigatorio.

Por que simplificar:

| Com ADC | Sem ADC |
|---|---|
| Mede queda de bateria no app/Serial | Menos fios e menos resistores |
| Exige divisor resistivo correto | Montagem mais facil |
| Ajuda diagnostico | Menor chance de erro eletrico |

Recomendacao pratica: use pilhas novas ou carregadas antes de calibrar e antes da demonstracao. A repetibilidade depende da bateria porque pilhas fracas reduzem torque; com torque menor, o motor pode perder passos contra o elastico. Se o alcance comecar a variar, troque as pilhas, envie `HOME`, reposicione o zero e recalibre.

Se no futuro quiser recolocar ADC, use um divisor resistivo para garantir que o GPIO nunca receba mais de 3,3 V. Isso deve ser tratado como melhoria opcional, nao como requisito do prototipo atual.

---

## 5. Sobre Fim de Curso

O fim de curso foi removido para simplificar a montagem. O firmware atual nao procura uma chave fisica; `HOME` apenas diz: "a posicao atual deve ser considerada zero".

Isso funciona bem quando:

| Condicao | Impacto |
|---|---|
| Voce sempre posiciona o mecanismo no zero antes de ligar | O firmware comeca alinhado |
| O motor nao perde passos durante o ciclo | O retorno por contagem volta ao zero |
| As pilhas estao fortes | Menor risco de escorregar contra o elastico |

Riscos aceitos:

| Situacao | Consequencia |
|---|---|
| O motor perde passos | O zero interno fica diferente do zero fisico |
| O mecanismo e movido com o ESP32 desligado | O firmware nao percebe |
| `ABORT` ocorre durante carga alta | Pode ser necessario zerar manualmente |

Regra de operacao: se qualquer movimento parecer errado, retire carga perigosa, coloque o tensionamento no zero mecanico e envie `HOME`.

Quando voltar a usar fim de curso: se a demonstracao exigir repetibilidade alta, se o elastico for muito forte, ou se varias pessoas forem operar o sistema sem inspecao manual.

---

## 6. Firmware

Arquivo principal: `firmware/hercules_firmware/hercules_firmware.ino`.

Bibliotecas usadas:

| Biblioteca | Uso |
|---|---|
| `BluetoothSerial` | Bluetooth Classico SPP no ESP32 |
| `AccelStepper` | Controle nao bloqueante dos dois motores |

Estados:

| Estado | Significado |
|---|---|
| `IDLE` | Pronto para comando |
| `TENSIONING` | Motor 1 indo ate a posicao de tensionamento |
| `LOCK_SETTLING` | Pausa curta para a engrenagem/trava assentar |
| `RETURNING` | Motor 1 voltando ao zero enquanto a engrenagem segura a carga |
| `ARMED` | Sistema travado mecanicamente, pronto para o motor 2 liberar |
| `FIRING` | Motor 2 liberando a engrenagem/trava, aguardando 1 s e retornando ao zero |

Comandos:

| Comando | Exemplo | Uso |
|---|---|---|
| `HOME` | `HOME` | Zera manualmente a posicao atual |
| `STATUS` | `STATUS` | Retorna estado e posicoes |
| `SET:X.XX` | `SET:1.50` | Seleciona distancia |
| `ARM` | `ARM` | Tensiona, trava e retorna o motor 1 ao zero |
| `FIRE` | `FIRE` | Libera a trava com o motor 2 se estiver armado |
| `LAUNCH:X.XX` | `LAUNCH:1.50` | Faz o ciclo completo automaticamente |
| `ABORT` | `ABORT` | Cancela se ainda nao estiver travado; se `ARMED`, exige `FIRE` ou liberacao manual |
| `CAL:X.XX:N` | `CAL:1.50:410` | Ajusta passos em RAM |

Formato atual de `STATUS`:

```text
STATUS:IDLE:DIST:1.50m:POS_T:0:POS_D:0
```

### Pontos para Alterar Manualmente

| Necessidade | Onde alterar |
|---|---|
| Pinos do tensionamento | `PIN_T_IN1` ate `PIN_T_IN4` |
| Pinos do disparo | `PIN_D_IN1` ate `PIN_D_IN4` |
| Velocidade do tensionamento | `T_VELOCIDADE_MAX` |
| Aceleracao do tensionamento | `T_ACELERACAO` |
| Passos do gatilho | `DISPARO_PASSOS` |
| Pausa entre tensionar e retornar | `LOCK_SETTLE_MS` |
| Pausa entre liberar trava e retorno do motor 2 | `DISPARO_DELAY_MS` |
| Tempo maximo armado | `ARMED_TIMEOUT_MS` |
| Distancia para passos | `lookup_table.h` |

Se um motor girar ao contrario, primeiro troque a ordem dos fios IN1-IN4 no ULN2003. Se preferir corrigir no firmware, altere a ordem de pinos no construtor `AccelStepper`.

---

## 7. Calibracao

A tabela `lookup_table.h` ainda e placeholder. Ela precisa ser calibrada com a mecanica final e com as pilhas em bom estado.

Fluxo recomendado:

1. Coloque o mecanismo no zero e envie `HOME`.
2. Teste uma distancia, por exemplo `ARM`, e confirme que o motor 1 tensiona e volta ao zero.
3. Meça a distancia real.
4. Envie `FIRE` para liberar a trava.
5. Teste novos passos com `CAL:1.50:N`.
6. Quando estiver bom, registre os dados no CSV e gere novo `lookup_table.h`.

Comando:

```bash
python3 calibration/calibrar.py --input dados_teste.csv --output firmware/hercules_firmware/lookup_table.h
```

Formato do CSV:

```csv
distancia_m,passos,distancia_real_m,desvio_lateral_cm
1.50,410,1.47,5
```

---

## 8. App Android

O app oficial esta em `app/HerculesI.aia` e foi feito no MIT App Inventor.

Fluxo:

1. Acesse `ai2.appinventor.mit.edu`.
2. Importe `app/HerculesI.aia`.
3. Conecte pelo AI Companion ou gere APK.
4. Emparelhe o Android com `Hercules-I`.
5. Use o slider ou botoes rapidos e toque em `LANCAR`.

O app envia:

| Acao | Comando |
|---|---|
| Botao lancar | `LAUNCH:X.XX` |
| Botao abortar | `ABORT` |
| Clock a cada 3 s | `STATUS` |

O app nao depende mais de `BAT:`. O antigo label de bateria virou aviso para usar pilhas carregadas e zerar manualmente.

### Como Alterar Manualmente o App

| Alteracao | Onde mexer no App Inventor |
|---|---|
| Texto do aviso | componente `LabelAviso` |
| Botoes 0.5 m, 1.0 m, etc. | componentes `Btn05`, `Btn10`, `Btn20`, `Btn30`, `Btn40` |
| Distancia do slider | blocos `distanciaM` e `SliderDistancia` |
| Comando de lancamento | bloco do evento `BotaoLancar.Click` |
| Leitura do estado | evento `Clock1.Timer` |

Se o firmware mudar o formato de `STATUS`, o app ainda exibira a linha recebida em `LabelEstado`. So sera necessario alterar os blocos se voce quiser extrair campos especificos.

Depois de alterar no App Inventor, exporte novamente o `.aia` para manter o repositório sincronizado.

---

## 9. Simulacao Wokwi

Arquivos:

| Arquivo | Uso |
|---|---|
| `wokwi/sketch.ino` | Simula a FSM por Serial |
| `wokwi/diagram.json` | Diagrama virtual |
| `tools/build-wokwi.sh` | Compila a simulacao |
| `tools/wokwi-console.py` | Console RFC2217 para comandos |

No Wokwi, os motores passam por drivers STEP/DIR apenas como adaptadores visuais do simulador. Isso garante que o contador de passos exibido volte corretamente para zero. No hardware real, use ULN2003 entre ESP32 e cada motor 28BYJ-48.

Teste:

```bash
tools/build-wokwi.sh
```

Depois abra `wokwi/` no simulador e envie:

```text
HOME
STATUS
LAUNCH:1.50
ABORT
```

---

## 10. Troubleshooting

| Sintoma | Causa provavel | Acao |
|---|---|---|
| Motor nao gira | GND nao comum ou VCC ausente no ULN2003 | Confira GND comum e 4xAA |
| Motor vibra sem girar | Ordem IN1-IN4 incorreta | Troque ordem dos fios ou ajuste construtor |
| Alcance varia muito | Pilhas fracas ou perda de passos | Troque pilhas, zere manualmente e recalibre |
| Motor 1 tensiona mas nao volta antes do disparo | Firmware/binario antigo no ESP32 ou Wokwi | Recompile e carregue a versao atual |
| Retorno nao chega no zero fisico | Perda de passos acumulada | Reposicione manualmente e envie `HOME` |
| `ABORT` nao solta quando esta `ARMED` | A carga esta travada mecanicamente | Use `FIRE` ou libere manualmente com seguranca |
| App conecta mas nao controla | Bluetooth pareado errado ou comando sem newline | Repareie `Hercules-I` e teste pelo Serial |
| Disparo nao libera gatilho | `DISPARO_PASSOS` baixo | Aumente `DISPARO_PASSOS` aos poucos |

---

## 11. Estrutura

```text
hercules-i/
├── GUIA_TECNICO.md
├── firmware/hercules_firmware/
│   ├── hercules_firmware.ino
│   └── lookup_table.h
├── wokwi/
│   ├── sketch.ino
│   ├── diagram.json
│   └── wokwi.toml
├── app/
│   ├── HerculesI.aia
│   └── hercules-appinventor/
├── calibration/
│   ├── calibrar.py
│   └── dados_exemplo.csv
└── tools/
    ├── build-wokwi.sh
    └── wokwi-console.py
```
