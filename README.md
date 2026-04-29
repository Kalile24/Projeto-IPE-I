# Projeto Hercules I

**Equipe A2 - IPE I / Instituto Militar de Engenharia - 2026.1**

Catapulta didatica de palitos de picole controlada por ESP32. O prototipo atual usa dois motores 28BYJ-48 com drivers ULN2003: um motor tensiona o elastico e outro aciona o gatilho. O controle pode ser feito pelo Serial Monitor durante testes ou por um app Android criado no MIT App Inventor via Bluetooth Classico.

Este README e a visao rapida para montar e testar. A documentacao completa esta em [`hercules-i/GUIA_TECNICO.md`](hercules-i/GUIA_TECNICO.md).

---

## Componentes Principais

| Item | Quantidade | Observacao |
|---|---:|---|
| ESP32 DevKit V1 | 1 | Controlador principal |
| Motor de passo 28BYJ-48 | 2 | Tensionamento e disparo |
| Driver ULN2003 | 2 | Um driver por motor |
| Porta-pilhas 4xAA | 1 | 4 pilhas de 1,5 V |
| LED + resistor 220 ohm | 1 | Status visual |
| Fios jumpers | varios | Use GND comum entre ESP32 e drivers |

O projeto atual nao exige sensor de fim de curso nem leitura analogica de bateria. Isso simplifica a montagem, mas exige colocar o mecanismo manualmente no zero antes de ligar ou enviar `HOME`.

---

## Fluxo Rapido

1. Ligue cada 28BYJ-48 ao seu ULN2003.
2. Ligue o GND dos ULN2003 ao GND do ESP32.
3. Alimente os ULN2003 com o banco de 4 pilhas AA.
4. Carregue o firmware em `hercules-i/firmware/hercules_firmware/`.
5. Abra o Serial Monitor em `115200 baud`.
6. Coloque o tensionamento no zero mecanico e envie `HOME`.
7. Teste `STATUS`, depois `LAUNCH:1.50`.
8. Importe `hercules-i/app/HerculesI.aia` no MIT App Inventor para gerar o APK.

Pinagem padrao:

| Funcao | GPIOs |
|---|---|
| Motor tensionamento ULN2003 IN1-IN4 | 26, 27, 14, 25 |
| Motor disparo ULN2003 IN1-IN4 | 18, 19, 21, 22 |
| LED status | 2 |

---

## Comandos

| Comando | Funcao |
|---|---|
| `HOME` | Zera manualmente a posicao atual |
| `STATUS` | Mostra estado, distancia e posicoes dos motores |
| `SET:X.XX` | Define distancia sem armar |
| `ARM` | Tensiona, trava mecanicamente e retorna o motor 1 ao zero |
| `FIRE` | Dispara quando estiver armado |
| `LAUNCH:X.XX` | Executa o ciclo completo: tensiona, retorna motor 1, libera trava |
| `ABORT` | Cancela antes da trava; se estiver `ARMED`, exige `FIRE` ou liberacao manual |
| `CAL:X.XX:N` | Testa novos passos para uma distancia ate reiniciar |

---

## Calibracao

A tabela inicial em `lookup_table.h` e apenas ponto de partida. Depois de montar o hardware real, faca disparos de teste e atualize os passos com:

```bash
cd hercules-i
python3 calibration/calibrar.py --input calibration/dados_exemplo.csv --output firmware/hercules_firmware/lookup_table.h
```

Para testar um valor sem recompilar:

```text
CAL:1.50:410
LAUNCH:1.50
```

---

## Onde Mexer

| Necessidade | Arquivo |
|---|---|
| Firmware ESP32 | `hercules-i/firmware/hercules_firmware/hercules_firmware.ino` |
| Tabela distancia -> passos | `hercules-i/firmware/hercules_firmware/lookup_table.h` |
| Simulacao Wokwi | `hercules-i/wokwi/` |
| App Android | `hercules-i/app/HerculesI.aia` e fonte em `hercules-i/app/hercules-appinventor/` |
| Guia detalhado | `hercules-i/GUIA_TECNICO.md` |

Para detalhes de montagem, alteracoes manuais no firmware/app e criterios de decisao sobre bateria, ADC e fim de curso, leia o guia tecnico.
