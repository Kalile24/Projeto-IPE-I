/**
 * Hércules I — Equipe A2 / IPE I / IME 2026.1
 * Firmware v1.4.0 — Abril/2026
 *
 * COMUNICAÇÃO DUAL:
 *   Serial Monitor (115200 baud) — teste pelo PC via USB
 *   Bluetooth Clássico SPP       — controle pelo celular Android
 *     Nome: "Hercules-I" | PIN: 1234
 *
 * Ambos os canais aceitam os mesmos comandos e recebem as mesmas respostas.
 *
 * PINAGEM:
 *   Motor de tensionamento (NEMA 17 + A4988):
 *     GPIO 26 → STEP | GPIO 27 → DIR | GPIO 14 → ENABLE
 *
 *   Motor de disparo (NEMA 17 + A4988):
 *     GPIO 18 → STEP | GPIO 19 → DIR | GPIO 21 → ENABLE
 *
 *   GPIO 25 → Fim de curso (ativo em LOW, pullup interno)
 *   GPIO 34 → ADC bateria (divisor R1=100kΩ / R2=10kΩ, fator 11×)
 *   GPIO  2 → LED de status
 *
 * ESTADOS FSM:
 *   IDLE → TENSIONING → ARMED → FIRING → RETURNING → IDLE
 *   LAUNCH:X.XX  executa toda a sequência automaticamente
 *   ABORT        interrompe qualquer estado e retorna ao zero
 *
 * COMANDOS:
 *   LAUNCH:X.XX  Sequência completa (tensiona + dispara)
 *   SET:X.XX     Define distância (0.50–4.00 m)
 *   ARM          Tensiona (requer SET antes)
 *   FIRE         Dispara (requer ARMED)
 *   ABORT        Para tudo e retorna ao zero
 *   HOME         Re-executa homing
 *   STATUS       Retorna estado, bateria e posição
 *   CAL:X.XX:N   Atualiza passos na RAM
 */

#include <BluetoothSerial.h>
#include <AccelStepper.h>

#include "lookup_table.h"

// ─── Pinagem ──────────────────────────────────────────────────────────────────
#define PIN_T_STEP    26
#define PIN_T_DIR     27
#define PIN_T_ENABLE  14

#define PIN_D_STEP    18
#define PIN_D_DIR     19
#define PIN_D_ENABLE  21

#define PIN_ENDSTOP   25
#define PIN_ADC_BAT   34
#define PIN_LED        2

// ─── Níveis lógicos ───────────────────────────────────────────────────────────
#define MOTOR_ENABLE_ON   LOW
#define MOTOR_ENABLE_OFF  HIGH
#define ENDSTOP_ATIVO     LOW

// ─── Motor de tensionamento ───────────────────────────────────────────────────
#define T_VELOCIDADE_MAX   500.0f
#define T_ACELERACAO       200.0f
#define T_VELOCIDADE_HOME  200.0f
#define T_HOMING_MAX_STEPS (STEPS_TABLE[TABLE_SIZE - 1] * 2)

// ─── Motor de disparo ─────────────────────────────────────────────────────────
// Ajuste DISPARO_PASSOS conforme o mecanismo físico (200 = 1 volta completa)
#define DISPARO_PASSOS       200
#define DISPARO_VELOCIDADE   800.0f
#define DISPARO_ACELERACAO   400.0f
#define DISPARO_DELAY_MS     300

// ─── Bateria ──────────────────────────────────────────────────────────────────
#define BAT_FATOR           11.0f   // (100k + 10k) / 10k
#define BAT_NOMINAL_V        9.0f
#define BAT_MIN_V            6.0f
#define BAT_CRITICO_FATOR    0.85f
#define BAT_INTERVALO_MS     5000

// ─── Temporização ─────────────────────────────────────────────────────────────
#define ARMED_TIMEOUT_MS   30000
#define LED_LENTO           1000
#define LED_RAPIDO           200

// ─── FSM ──────────────────────────────────────────────────────────────────────
enum Estado { IDLE, HOMING, TENSIONING, ARMED, FIRING, RETURNING };
Estado estadoAtual = IDLE;

// ─── Variáveis globais ────────────────────────────────────────────────────────
int   stepsRAM[TABLE_SIZE];
float distanciaAlvo_m    = 1.00f;
int   passosSelecionados = 255;
bool  autoDisparar       = false;

unsigned long tempoDisparo      = 0;
unsigned long tempoEntradaARMED = 0;
unsigned long ultimaLeituraBat  = 0;
unsigned long ultimoBlink       = 0;

bool  estadoLED      = false;
bool  bateriaCritica = false;
int   sosPasso       = 0;
unsigned long ultimoSOS = 0;

float tensaoBateria_V   = 9.0f;
int   percentualBateria = 100;

// ─── Hardware ─────────────────────────────────────────────────────────────────
BluetoothSerial BT;
AccelStepper motorTensao(AccelStepper::DRIVER,  PIN_T_STEP, PIN_T_DIR);
AccelStepper motorDisparo(AccelStepper::DRIVER, PIN_D_STEP, PIN_D_DIR);
String bufferBT     = "";
String bufferSerial = "";  // Leitura de comandos via Serial (teste pelo PC)

// ─── Funções auxiliares ───────────────────────────────────────────────────────

// Envia mensagem pelo Bluetooth E pelo Serial simultaneamente.
// Permite testar pelo PC (Serial Monitor) sem precisar do celular.
void enviarStatus(const String& msg) {
    BT.println(msg);
    Serial.println(msg);
}

void habilitarMotorT()   { digitalWrite(PIN_T_ENABLE, MOTOR_ENABLE_ON);  }
void desabilitarMotorT() { digitalWrite(PIN_T_ENABLE, MOTOR_ENABLE_OFF); }
void habilitarMotorD()   { digitalWrite(PIN_D_ENABLE, MOTOR_ENABLE_ON);  }
void desabilitarMotorD() { digitalWrite(PIN_D_ENABLE, MOTOR_ENABLE_OFF); }

bool endstopAcionado() {
    return digitalRead(PIN_ENDSTOP) == ENDSTOP_ATIVO;
}

int distanciaParaIndice(float dist_m) {
    if (dist_m < DIST_MIN_M || dist_m > DIST_MAX_M) return -1;
    int idx = (int)round((dist_m - DIST_MIN_M) / DIST_STEP_M);
    if (idx < 0)           idx = 0;
    if (idx >= TABLE_SIZE) idx = TABLE_SIZE - 1;
    return idx;
}

void lerBateria() {
    int adc = analogRead(PIN_ADC_BAT);
    tensaoBateria_V = ((float)adc / 4095.0f) * 3.3f * BAT_FATOR;
    percentualBateria = (int)(((tensaoBateria_V - BAT_MIN_V) / (BAT_NOMINAL_V - BAT_MIN_V)) * 100.0f);
    if (percentualBateria < 0)   percentualBateria = 0;
    if (percentualBateria > 100) percentualBateria = 100;
    bateriaCritica = (tensaoBateria_V < BAT_NOMINAL_V * BAT_CRITICO_FATOR);
    if (bateriaCritica) enviarStatus("BATTERY_LOW:" + String(percentualBateria) + "%");
}

void atualizarLED() {
    unsigned long agora = millis();
    if (bateriaCritica) {
        static const int sos[] = {
            100,100, 100,100, 100,300,
            300,100, 300,100, 300,300,
            100,100, 100,100, 100,700
        };
        static const int n = sizeof(sos) / sizeof(sos[0]);
        if (agora - ultimoSOS >= (unsigned long)sos[sosPasso]) {
            estadoLED = !estadoLED;
            digitalWrite(PIN_LED, estadoLED);
            ultimoSOS = agora;
            sosPasso  = (sosPasso + 1) % n;
        }
        return;
    }
    switch (estadoAtual) {
        case IDLE:
            if (agora - ultimoBlink >= LED_LENTO / 2) {
                estadoLED = !estadoLED; digitalWrite(PIN_LED, estadoLED); ultimoBlink = agora;
            }
            break;
        case HOMING: case TENSIONING:
            if (agora - ultimoBlink >= LED_RAPIDO / 2) {
                estadoLED = !estadoLED; digitalWrite(PIN_LED, estadoLED); ultimoBlink = agora;
            }
            break;
        case ARMED:
            digitalWrite(PIN_LED, HIGH); estadoLED = true;
            break;
        case FIRING: case RETURNING:
            if (agora - ultimoBlink >= 50) {
                estadoLED = !estadoLED; digitalWrite(PIN_LED, estadoLED); ultimoBlink = agora;
            }
            break;
    }
}

void iniciarRetorno() {
    habilitarMotorT();
    motorTensao.moveTo(0);
    estadoAtual = RETURNING;
}

void executarHoming() {
    Serial.println("[HOMING] Iniciando...");
    enviarStatus("HOMING");
    estadoAtual = HOMING;

    if (endstopAcionado()) {
        motorTensao.setCurrentPosition(0);
        desabilitarMotorT();
        estadoAtual = IDLE;
        Serial.println("[HOMING] Endstop já ativo — zero confirmado.");
        enviarStatus("HOME_OK");
        return;
    }

    habilitarMotorT();
    motorTensao.setMaxSpeed(T_VELOCIDADE_HOME);
    motorTensao.move(-T_HOMING_MAX_STEPS);

    while (!endstopAcionado() && motorTensao.distanceToGo() != 0) {
        motorTensao.run();
        yield();
    }

    motorTensao.stop();
    motorTensao.setCurrentPosition(0);
    motorTensao.setMaxSpeed(T_VELOCIDADE_MAX);
    desabilitarMotorT();
    estadoAtual = IDLE;

    if (endstopAcionado()) {
        Serial.println("[HOMING] Posição zero estabelecida.");
        enviarStatus("HOME_OK");
    } else {
        Serial.println("[HOMING] AVISO: endstop não detectado.");
        enviarStatus("HOME_WARN");
    }
}

// ─── Processamento de comandos ────────────────────────────────────────────────

void processarComando(const String& cmd) {
    Serial.println("[CMD] " + cmd);

    // LAUNCH:X.XX — tensiona e dispara automaticamente
    if (cmd.startsWith("LAUNCH:")) {
        if (estadoAtual != IDLE) { enviarStatus("ERROR:BUSY"); return; }
        float dist = cmd.substring(7).toFloat();
        int idx = distanciaParaIndice(dist);
        if (idx < 0) { enviarStatus("ERROR:DIST_INVALIDA"); return; }
        distanciaAlvo_m    = dist;
        passosSelecionados = stepsRAM[idx];
        autoDisparar = true;
        habilitarMotorT();
        motorTensao.moveTo(passosSelecionados);
        estadoAtual = TENSIONING;
        enviarStatus("LAUNCH:" + String(dist, 2) + "m");
        return;
    }

    // SET:X.XX
    if (cmd.startsWith("SET:")) {
        if (estadoAtual != IDLE) { enviarStatus("ERROR:BUSY"); return; }
        float dist = cmd.substring(4).toFloat();
        int idx = distanciaParaIndice(dist);
        if (idx < 0) { enviarStatus("ERROR:DIST_INVALIDA"); return; }
        distanciaAlvo_m    = dist;
        passosSelecionados = stepsRAM[idx];
        enviarStatus("SET_OK:" + String(dist, 2) + "m");
        return;
    }

    // ARM
    if (cmd == "ARM") {
        if (estadoAtual != IDLE) { enviarStatus("ERROR:ESTADO_INVALIDO"); return; }
        habilitarMotorT();
        motorTensao.moveTo(passosSelecionados);
        estadoAtual = TENSIONING;
        enviarStatus("TENSIONING:" + String(passosSelecionados));
        return;
    }

    // FIRE
    if (cmd == "FIRE") {
        if (estadoAtual != ARMED) { enviarStatus("ERROR:NAO_ARMADO"); return; }
        habilitarMotorD();
        motorDisparo.move(DISPARO_PASSOS);
        tempoDisparo = millis();
        estadoAtual  = FIRING;
        return;
    }

    // ABORT
    if (cmd == "ABORT") {
        autoDisparar = false;
        motorDisparo.stop();
        motorDisparo.setCurrentPosition(0);
        desabilitarMotorD();
        iniciarRetorno();
        enviarStatus("ABORT:OK");
        enviarStatus("RETURNING");
        return;
    }

    // HOME
    if (cmd == "HOME") {
        if (estadoAtual != IDLE) { enviarStatus("ERROR:BUSY"); return; }
        executarHoming();
        return;
    }

    // STATUS
    if (cmd == "STATUS") {
        const char* nomes[] = {"IDLE","HOMING","TENSIONING","ARMED","FIRING","RETURNING"};
        enviarStatus("STATUS:" + String(nomes[estadoAtual]) +
                     ":BAT:" + String(percentualBateria) + "%" +
                     ":" + String(tensaoBateria_V, 1) + "V" +
                     ":DIST:" + String(distanciaAlvo_m, 2) + "m" +
                     ":POS:" + String(motorTensao.currentPosition()));
        return;
    }

    // CAL:X.XX:NNN
    if (cmd.startsWith("CAL:")) {
        int p = cmd.indexOf(':', 4);
        if (p < 0) { enviarStatus("ERROR:CAL_FORMATO"); return; }
        float dist        = cmd.substring(4, p).toFloat();
        int   novosPassos = cmd.substring(p + 1).toInt();
        int   idx         = distanciaParaIndice(dist);
        if (idx < 0)                                { enviarStatus("ERROR:CAL_DIST");   return; }
        if (novosPassos <= 0 || novosPassos > 9999) { enviarStatus("ERROR:CAL_PASSOS"); return; }
        stepsRAM[idx] = novosPassos;
        enviarStatus("CAL_OK:" + String(dist, 2) + "m:" + String(novosPassos));
        return;
    }

    enviarStatus("ERROR:CMD_DESCONHECIDO");
}

// ─── Setup ────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Hercules I v1.3.0 — Equipe A2 / IME 2026.1 ===\n");

    pinMode(PIN_LED,      OUTPUT);
    pinMode(PIN_T_ENABLE, OUTPUT);
    pinMode(PIN_D_ENABLE, OUTPUT);
    pinMode(PIN_ENDSTOP,  INPUT_PULLUP);
    desabilitarMotorT();
    desabilitarMotorD();

    for (int i = 0; i < TABLE_SIZE; i++) stepsRAM[i] = STEPS_TABLE[i];

    motorTensao.setMaxSpeed(T_VELOCIDADE_MAX);
    motorTensao.setAcceleration(T_ACELERACAO);
    motorTensao.setCurrentPosition(0);

    motorDisparo.setMaxSpeed(DISPARO_VELOCIDADE);
    motorDisparo.setAcceleration(DISPARO_ACELERACAO);
    motorDisparo.setCurrentPosition(0);

    analogReadResolution(12);
    lerBateria();
    Serial.printf("[INIT] Bateria: %.1fV (%d%%)\n", tensaoBateria_V, percentualBateria);

    executarHoming();

    BT.begin("Hercules-I");

    Serial.println("Pronto. Comandos aceitos pelo Serial Monitor e pelo Bluetooth.");
    Serial.println("  LAUNCH:X.XX  SET:X.XX  ARM  FIRE  ABORT  HOME  STATUS  CAL:X.XX:N");
    Serial.println("Bluetooth: emparelhe 'Hercules-I' no Android (PIN: 1234)\n");
}

// ─── Loop principal ────────────────────────────────────────────────────────────

void loop() {
    unsigned long agora = millis();

    atualizarLED();

    if (agora - ultimaLeituraBat >= BAT_INTERVALO_MS) {
        ultimaLeituraBat = agora;
        lerBateria();
    }

    // Timeout de 30s em ARMED
    if (estadoAtual == ARMED && agora - tempoEntradaARMED >= ARMED_TIMEOUT_MS) {
        Serial.println("[FSM] Timeout ARMED — ABORT automático.");
        autoDisparar = false;
        iniciarRetorno();
        enviarStatus("ABORT:TIMEOUT");
        enviarStatus("RETURNING");
    }

    // ── Motor de tensionamento ────────────────────────────────────────────────
    if (estadoAtual == TENSIONING || estadoAtual == RETURNING) {
        if (motorTensao.distanceToGo() != 0) {
            motorTensao.run();
        } else {
            if (estadoAtual == TENSIONING) {
                estadoAtual = ARMED;
                tempoEntradaARMED = agora;
                enviarStatus("ARMED");
                Serial.println("[FSM] ARMED.");
                if (autoDisparar) {
                    autoDisparar = false;
                    habilitarMotorD();
                    motorDisparo.move(DISPARO_PASSOS);
                    tempoDisparo = agora;
                    estadoAtual  = FIRING;
                    Serial.println("[FSM] FIRING — disparo automático.");
                }
            } else {
                desabilitarMotorT();
                estadoAtual = IDLE;
                enviarStatus("IDLE");
                Serial.println("[FSM] IDLE — pronto.");
            }
        }
    }

    // ── Motor de disparo ──────────────────────────────────────────────────────
    if (estadoAtual == FIRING) {
        if (motorDisparo.distanceToGo() != 0) {
            motorDisparo.run();
        } else if (agora - tempoDisparo >= DISPARO_DELAY_MS) {
            motorDisparo.moveTo(0);
            while (motorDisparo.distanceToGo() != 0) { motorDisparo.run(); yield(); }
            desabilitarMotorD();
            enviarStatus("FIRED");
            iniciarRetorno();
            enviarStatus("RETURNING");
            Serial.println("[FSM] FIRED — retornando.");
        }
    }

    // ── Leitura de comandos via Bluetooth ────────────────────────────────────
    while (BT.available()) {
        char c = (char)BT.read();
        if (c == '\n' || c == '\r') {
            bufferBT.trim();
            if (bufferBT.length() > 0) { processarComando(bufferBT); bufferBT = ""; }
        } else { bufferBT += c; }
    }

    // ── Leitura de comandos via Serial Monitor (teste pelo PC) ───────────────
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            bufferSerial.trim();
            if (bufferSerial.length() > 0) { processarComando(bufferSerial); bufferSerial = ""; }
        } else { bufferSerial += c; }
    }
}
