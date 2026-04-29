/**
 * Hercules I - Equipe A2 / IPE I / IME 2026.1
 * Firmware v2.0.0 - Abril/2026
 *
 * Hardware real:
 *   ESP32 DevKit V1
 *   2x motores 28BYJ-48
 *   2x drivers ULN2003
 *   Banco de 4 pilhas AA de 1,5 V
 *
 * Comunicacao:
 *   Serial Monitor (115200 baud) - teste pelo PC via USB
 *   Bluetooth Classico SPP       - controle pelo celular Android
 *     Nome: "Hercules-I" | PIN: 1234
 *
 * O projeto nao usa ADC nem fim de curso como requisito. Antes de ligar,
 * coloque manualmente o mecanismo de tensionamento na posicao zero.
 */

#include <BluetoothSerial.h>
#include <AccelStepper.h>

#include "lookup_table.h"

// Motor de tensionamento: IN1, IN2, IN3, IN4 do ULN2003.
#define PIN_T_IN1 26
#define PIN_T_IN2 27
#define PIN_T_IN3 14
#define PIN_T_IN4 25

// Motor de disparo: IN1, IN2, IN3, IN4 do ULN2003.
#define PIN_D_IN1 18
#define PIN_D_IN2 19
#define PIN_D_IN3 21
#define PIN_D_IN4 22

#define PIN_LED 2

// 28BYJ-48 costuma ter 4096 meios-passos por volta no eixo de saida.
// Ajuste estes valores se o seu motor/caixa de reducao se comportar diferente.
#define T_VELOCIDADE_MAX  600.0f
#define T_ACELERACAO      250.0f
#define D_VELOCIDADE_MAX  700.0f
#define D_ACELERACAO      300.0f
#define DISPARO_PASSOS    512
#define DISPARO_DELAY_MS  300

#define ARMED_TIMEOUT_MS 30000
#define LED_LENTO        1000
#define LED_RAPIDO        200

enum Estado { IDLE, TENSIONING, ARMED, FIRING, RETURNING };
Estado estadoAtual = IDLE;

int stepsRAM[TABLE_SIZE];
float distanciaAlvo_m = 1.00f;
int passosSelecionados = 255;
bool autoDisparar = false;

unsigned long tempoDisparo = 0;
unsigned long tempoEntradaARMED = 0;
unsigned long ultimoBlink = 0;
bool estadoLED = false;
bool retornoDisparoIniciado = false;

BluetoothSerial BT;

// Ordem de pinos recomendada pela AccelStepper para motores 4 fios.
AccelStepper motorTensao(AccelStepper::HALF4WIRE, PIN_T_IN1, PIN_T_IN3, PIN_T_IN2, PIN_T_IN4);
AccelStepper motorDisparo(AccelStepper::HALF4WIRE, PIN_D_IN1, PIN_D_IN3, PIN_D_IN2, PIN_D_IN4);

String bufferBT = "";
String bufferSerial = "";

void enviarStatus(const String& msg) {
    BT.println(msg);
    Serial.println(msg);
}

void desenergizarMotores() {
    motorTensao.disableOutputs();
    motorDisparo.disableOutputs();
}

const char* nomeEstado() {
    static const char* nomes[] = {"IDLE", "TENSIONING", "ARMED", "FIRING", "RETURNING"};
    return nomes[estadoAtual];
}

int distanciaParaIndice(float dist_m) {
    if (dist_m < DIST_MIN_M || dist_m > DIST_MAX_M) return -1;
    int idx = (int)round((dist_m - DIST_MIN_M) / DIST_STEP_M);
    if (idx < 0) idx = 0;
    if (idx >= TABLE_SIZE) idx = TABLE_SIZE - 1;
    return idx;
}

void atualizarLED() {
    unsigned long agora = millis();

    switch (estadoAtual) {
        case IDLE:
            if (agora - ultimoBlink >= LED_LENTO / 2) {
                estadoLED = !estadoLED;
                digitalWrite(PIN_LED, estadoLED);
                ultimoBlink = agora;
            }
            break;
        case TENSIONING:
            if (agora - ultimoBlink >= LED_RAPIDO / 2) {
                estadoLED = !estadoLED;
                digitalWrite(PIN_LED, estadoLED);
                ultimoBlink = agora;
            }
            break;
        case ARMED:
            digitalWrite(PIN_LED, HIGH);
            estadoLED = true;
            break;
        case FIRING:
        case RETURNING:
            if (agora - ultimoBlink >= 50) {
                estadoLED = !estadoLED;
                digitalWrite(PIN_LED, estadoLED);
                ultimoBlink = agora;
            }
            break;
    }
}

void iniciarRetorno() {
    motorTensao.enableOutputs();
    motorTensao.moveTo(0);
    estadoAtual = RETURNING;
}

void zerarPosicaoManual() {
    motorTensao.stop();
    motorDisparo.stop();
    motorTensao.setCurrentPosition(0);
    motorDisparo.setCurrentPosition(0);
    autoDisparar = false;
    retornoDisparoIniciado = false;
    estadoAtual = IDLE;
    desenergizarMotores();
    enviarStatus("HOME_OK:ZERO_MANUAL");
}

void iniciarDisparo() {
    motorDisparo.enableOutputs();
    motorDisparo.setCurrentPosition(0);
    motorDisparo.moveTo(DISPARO_PASSOS);
    tempoDisparo = millis();
    retornoDisparoIniciado = false;
    estadoAtual = FIRING;
}

void processarComando(const String& cmd) {
    Serial.println("[CMD] " + cmd);

    if (cmd.startsWith("LAUNCH:")) {
        if (estadoAtual != IDLE) { enviarStatus("ERROR:BUSY"); return; }
        float dist = cmd.substring(7).toFloat();
        int idx = distanciaParaIndice(dist);
        if (idx < 0) { enviarStatus("ERROR:DIST_INVALIDA"); return; }
        distanciaAlvo_m = dist;
        passosSelecionados = stepsRAM[idx];
        autoDisparar = true;
        motorTensao.enableOutputs();
        motorTensao.moveTo(passosSelecionados);
        estadoAtual = TENSIONING;
        enviarStatus("LAUNCH:" + String(dist, 2) + "m");
        return;
    }

    if (cmd.startsWith("SET:")) {
        if (estadoAtual != IDLE) { enviarStatus("ERROR:BUSY"); return; }
        float dist = cmd.substring(4).toFloat();
        int idx = distanciaParaIndice(dist);
        if (idx < 0) { enviarStatus("ERROR:DIST_INVALIDA"); return; }
        distanciaAlvo_m = dist;
        passosSelecionados = stepsRAM[idx];
        enviarStatus("SET_OK:" + String(dist, 2) + "m:" + String(passosSelecionados));
        return;
    }

    if (cmd == "ARM") {
        if (estadoAtual != IDLE) { enviarStatus("ERROR:ESTADO_INVALIDO"); return; }
        motorTensao.enableOutputs();
        motorTensao.moveTo(passosSelecionados);
        estadoAtual = TENSIONING;
        enviarStatus("TENSIONING:" + String(passosSelecionados));
        return;
    }

    if (cmd == "FIRE") {
        if (estadoAtual != ARMED) { enviarStatus("ERROR:NAO_ARMADO"); return; }
        iniciarDisparo();
        enviarStatus("FIRING");
        return;
    }

    if (cmd == "ABORT") {
        autoDisparar = false;
        retornoDisparoIniciado = false;
        motorDisparo.stop();
        motorDisparo.moveTo(0);
        iniciarRetorno();
        enviarStatus("ABORT:OK");
        enviarStatus("RETURNING");
        return;
    }

    if (cmd == "HOME") {
        zerarPosicaoManual();
        return;
    }

    if (cmd == "STATUS") {
        enviarStatus("STATUS:" + String(nomeEstado()) +
                     ":DIST:" + String(distanciaAlvo_m, 2) + "m" +
                     ":POS_T:" + String(motorTensao.currentPosition()) +
                     ":POS_D:" + String(motorDisparo.currentPosition()));
        return;
    }

    if (cmd.startsWith("CAL:")) {
        int p = cmd.indexOf(':', 4);
        if (p < 0) { enviarStatus("ERROR:CAL_FORMATO"); return; }
        float dist = cmd.substring(4, p).toFloat();
        int novosPassos = cmd.substring(p + 1).toInt();
        int idx = distanciaParaIndice(dist);
        if (idx < 0) { enviarStatus("ERROR:CAL_DIST"); return; }
        if (novosPassos <= 0 || novosPassos > 99999) { enviarStatus("ERROR:CAL_PASSOS"); return; }
        stepsRAM[idx] = novosPassos;
        enviarStatus("CAL_OK:" + String(dist, 2) + "m:" + String(novosPassos));
        return;
    }

    enviarStatus("ERROR:CMD_DESCONHECIDO");
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Hercules I v2.0.0 - 28BYJ-48 + ULN2003 ===\n");

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);
    estadoLED = true;
    ultimoBlink = millis();

    for (int i = 0; i < TABLE_SIZE; i++) stepsRAM[i] = STEPS_TABLE[i];

    motorTensao.setMaxSpeed(T_VELOCIDADE_MAX);
    motorTensao.setAcceleration(T_ACELERACAO);
    motorTensao.setCurrentPosition(0);

    motorDisparo.setMaxSpeed(D_VELOCIDADE_MAX);
    motorDisparo.setAcceleration(D_ACELERACAO);
    motorDisparo.setCurrentPosition(0);

    desenergizarMotores();

    BT.begin("Hercules-I");

    Serial.println("Antes de ligar, coloque o tensionamento no zero mecanico.");
    Serial.println("Comandos: LAUNCH:X.XX  SET:X.XX  ARM  FIRE  ABORT  HOME  STATUS  CAL:X.XX:N");
    Serial.println("Bluetooth: emparelhe 'Hercules-I' no Android (PIN: 1234)\n");
    enviarStatus("IDLE");
}

void loop() {
    unsigned long agora = millis();

    atualizarLED();

    if (estadoAtual == ARMED && agora - tempoEntradaARMED >= ARMED_TIMEOUT_MS) {
        Serial.println("[FSM] Timeout ARMED - retorno automatico.");
        autoDisparar = false;
        iniciarRetorno();
        enviarStatus("ABORT:TIMEOUT");
        enviarStatus("RETURNING");
    }

    if (estadoAtual == TENSIONING || estadoAtual == RETURNING) {
        if (motorTensao.distanceToGo() != 0) {
            motorTensao.run();
        } else if (estadoAtual == TENSIONING) {
            estadoAtual = ARMED;
            tempoEntradaARMED = agora;
            enviarStatus("ARMED");
            if (autoDisparar) {
                autoDisparar = false;
                iniciarDisparo();
                enviarStatus("FIRING");
            }
        } else {
            motorTensao.disableOutputs();
            estadoAtual = IDLE;
            enviarStatus("IDLE");
        }
    }

    if (estadoAtual == FIRING) {
        if (motorDisparo.distanceToGo() != 0) {
            motorDisparo.run();
        } else if (!retornoDisparoIniciado) {
            if (agora - tempoDisparo >= DISPARO_DELAY_MS) {
                motorDisparo.moveTo(0);
                retornoDisparoIniciado = true;
            }
        } else {
            motorDisparo.disableOutputs();
            enviarStatus("FIRED");
            iniciarRetorno();
            enviarStatus("RETURNING");
        }
    }

    while (BT.available()) {
        char c = (char)BT.read();
        if (c == '\n' || c == '\r') {
            bufferBT.trim();
            if (bufferBT.length() > 0) {
                processarComando(bufferBT);
                bufferBT = "";
            }
        } else {
            bufferBT += c;
        }
    }

    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            bufferSerial.trim();
            if (bufferSerial.length() > 0) {
                processarComando(bufferSerial);
                bufferSerial = "";
            }
        } else {
            bufferSerial += c;
        }
    }
}
