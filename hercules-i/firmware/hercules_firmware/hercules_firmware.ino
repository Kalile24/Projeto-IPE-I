/**
 * Hércules I — Equipe A2 / IPE I / IME 2026.1
 * Firmware v1.2.0 — Abril/2026
 *
 * PINAGEM:
 *   Motor de tensionamento (NEMA 17 + A4988):
 *     GPIO 26 → STEP
 *     GPIO 27 → DIR
 *     GPIO 14 → ENABLE (ativo em LOW)
 *
 *   Motor de disparo (NEMA 17 + A4988):
 *     GPIO 18 → STEP
 *     GPIO 19 → DIR
 *     GPIO 21 → ENABLE (ativo em LOW)
 *
 *   GPIO 25 → Fim de curso (endstop) — ativo em LOW, pullup interno
 *   GPIO 34 → ADC bateria — divisor R1=100kΩ / R2=10kΩ (fator 11×)
 *   GPIO  2 → LED de status
 *
 * ALIMENTAÇÃO:
 *   6× AA (9V) → step-down MP1584 → 5V lógica
 *   Tensão mínima operacional: 6V (bateria descarregada)
 *
 * ESTADOS DA FSM:
 *   IDLE → TENSIONING → ARMED → FIRING → RETURNING → IDLE
 *   HOMING pode ser chamado de IDLE a qualquer momento.
 *   ABORT pode ser acionado de qualquer estado e retorna ao IDLE.
 *
 * MOTOR DE DISPARO:
 *   Substitui o servo SG90. Faz uma rotação controlada para liberar
 *   o mecanismo de gatilho. Após o disparo, retorna à posição zero.
 *   Passos para disparo: DISPARO_PASSOS (configurável abaixo).
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <AccelStepper.h>

#include "lookup_table.h"

// ─── UUIDs BLE ────────────────────────────────────────────────────────────────
#define SERVICE_UUID     "12345678-1234-1234-1234-123456789abc"
#define CMD_CHAR_UUID    "12345678-1234-1234-1234-123456789ab1"
#define STATUS_CHAR_UUID "12345678-1234-1234-1234-123456789ab2"

// ─── Pinagem ──────────────────────────────────────────────────────────────────
#define PIN_T_STEP    26   // Motor tensionamento — STEP
#define PIN_T_DIR     27   // Motor tensionamento — DIR
#define PIN_T_ENABLE  14   // Motor tensionamento — ENABLE

#define PIN_D_STEP    18   // Motor disparo — STEP
#define PIN_D_DIR     19   // Motor disparo — DIR
#define PIN_D_ENABLE  21   // Motor disparo — ENABLE

#define PIN_ENDSTOP   25   // Fim de curso (ativo em LOW)
#define PIN_ADC_BAT   34   // ADC bateria
#define PIN_LED        2   // LED status

// ─── Níveis lógicos ───────────────────────────────────────────────────────────
// Endstop com NO ligado ao GND: ativo em LOW.
// Para NC, troque ENDSTOP_ACTIVE_LEVEL para HIGH.
#define MOTOR_ENABLE_ON   LOW
#define MOTOR_ENABLE_OFF  HIGH
#define ENDSTOP_ATIVO     LOW

// ─── Parâmetros do motor de tensionamento ─────────────────────────────────────
#define T_VELOCIDADE_MAX   500.0f
#define T_ACELERACAO       200.0f
#define T_VELOCIDADE_HOME  200.0f
#define T_HOMING_MAX_STEPS (STEPS_TABLE[TABLE_SIZE - 1] * 2)

// ─── Parâmetros do motor de disparo ───────────────────────────────────────────
// Quantos passos o motor gira para liberar o gatilho.
// Ajuste conforme o mecanismo físico.
#define DISPARO_PASSOS        200
#define DISPARO_VELOCIDADE    800.0f
#define DISPARO_ACELERACAO    400.0f

// Tempo de espera após o motor de disparo completar a rotação,
// antes de iniciar o retorno do motor de tensionamento.
#define DISPARO_DELAY_MS      300

// ─── Bateria ──────────────────────────────────────────────────────────────────
#define BAT_R1_KOHM         100.0f
#define BAT_R2_KOHM          10.0f
#define BAT_FATOR           ((BAT_R1_KOHM + BAT_R2_KOHM) / BAT_R2_KOHM)
#define BAT_NOMINAL_V         9.0f
#define BAT_MIN_V             6.0f
#define BAT_CRITICO_FATOR     0.85f
#define BAT_LEITURA_INTERVALO 5000

// ─── Timeouts e LED ───────────────────────────────────────────────────────────
#define ARMED_TIMEOUT_MS   30000
#define LED_LENTO           1000
#define LED_RAPIDO           200

// ─── FSM ──────────────────────────────────────────────────────────────────────
enum Estado { IDLE, HOMING, TENSIONING, ARMED, FIRING, RETURNING };
volatile Estado estadoAtual = IDLE;

// ─── Variáveis globais ────────────────────────────────────────────────────────
int   stepsRAM[TABLE_SIZE];
float distanciaAlvo_m    = 1.00f;
int   passosSelecionados = 255;

volatile bool abortSolicitado = false;  // Sinaliza abort da task BLE para o loop

unsigned long tempoDisparo      = 0;
unsigned long tempoEntradaARMED = 0;
unsigned long ultimaLeituraBat  = 0;
unsigned long ultimoBlink       = 0;

bool  estadoLED     = false;
bool  bateriaCritica = false;
int   sosPasso      = 0;
unsigned long ultimoSOS = 0;

float tensaoBateria_V   = 9.0f;
int   percentualBateria = 100;

// ─── BLE ──────────────────────────────────────────────────────────────────────
BLEServer*         pServer          = nullptr;
BLECharacteristic* pCmdChar         = nullptr;
BLECharacteristic* pStatusChar      = nullptr;
bool               clienteConectado = false;
volatile bool      novoComando      = false;
String             bufferComando    = "";
SemaphoreHandle_t  cmdMutex         = nullptr;

// ─── Hardware ─────────────────────────────────────────────────────────────────
AccelStepper motorTensao(AccelStepper::DRIVER, PIN_T_STEP, PIN_T_DIR);
AccelStepper motorDisparo(AccelStepper::DRIVER, PIN_D_STEP, PIN_D_DIR);

// ─── Funções auxiliares ───────────────────────────────────────────────────────

void enviarStatus(const String& msg) {
    if (!clienteConectado || pStatusChar == nullptr) return;
    pStatusChar->setValue(msg.c_str());
    pStatusChar->notify();
    Serial.print("[BLE TX] ");
    Serial.println(msg);
}

void habilitarMotorT()    { digitalWrite(PIN_T_ENABLE, MOTOR_ENABLE_ON);  }
void desabilitarMotorT()  { digitalWrite(PIN_T_ENABLE, MOTOR_ENABLE_OFF); }
void habilitarMotorD()    { digitalWrite(PIN_D_ENABLE, MOTOR_ENABLE_ON);  }
void desabilitarMotorD()  { digitalWrite(PIN_D_ENABLE, MOTOR_ENABLE_OFF); }

bool endstopAcionado() {
    return digitalRead(PIN_ENDSTOP) == ENDSTOP_ATIVO;
}

int distanciaParaIndice(float dist_m) {
    if (dist_m < DIST_MIN_M || dist_m > DIST_MAX_M) return -1;
    int idx = (int)round((dist_m - DIST_MIN_M) / DIST_STEP_M);
    if (idx < 0)          idx = 0;
    if (idx >= TABLE_SIZE) idx = TABLE_SIZE - 1;
    return idx;
}

void lerBateria() {
    int adcBruto = analogRead(PIN_ADC_BAT);
    tensaoBateria_V = ((float)adcBruto / 4095.0f) * 3.3f * BAT_FATOR;
    percentualBateria = (int)(((tensaoBateria_V - BAT_MIN_V) / (BAT_NOMINAL_V - BAT_MIN_V)) * 100.0f);
    if (percentualBateria < 0)   percentualBateria = 0;
    if (percentualBateria > 100) percentualBateria = 100;
    bateriaCritica = (tensaoBateria_V < BAT_NOMINAL_V * BAT_CRITICO_FATOR);
    if (bateriaCritica) {
        enviarStatus("BATTERY_LOW:" + String(percentualBateria) + "%");
    }
}

void atualizarLED() {
    unsigned long agora = millis();

    // Bateria crítica: pisca SOS em Morse (· · · — — — · · ·)
    if (bateriaCritica) {
        static const int sos[] = {
            100,100, 100,100, 100,300,   // S: · · ·
            300,100, 300,100, 300,300,   // O: — — —
            100,100, 100,100, 100,700    // S: · · ·  + pausa
        };
        static const int numSos = sizeof(sos) / sizeof(sos[0]);
        if (agora - ultimoSOS >= (unsigned long)sos[sosPasso]) {
            estadoLED = !estadoLED;
            digitalWrite(PIN_LED, estadoLED);
            ultimoSOS = agora;
            sosPasso  = (sosPasso + 1) % numSos;
        }
        return;
    }

    switch (estadoAtual) {
        case IDLE:
            if (agora - ultimoBlink >= LED_LENTO / 2) {
                estadoLED = !estadoLED;
                digitalWrite(PIN_LED, estadoLED);
                ultimoBlink = agora;
            }
            break;
        case HOMING:
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

// Inicia o retorno do motor de tensionamento ao zero (não-bloqueante).
// O loop() executa motorTensao.run() e transiciona para IDLE ao chegar em 0.
void iniciarRetorno() {
    habilitarMotorT();
    motorTensao.moveTo(0);
    estadoAtual = RETURNING;
}

// Homing bloqueante: recua o motor de tensionamento até o fim de curso ser acionado.
// Chamado no setup() e pelo comando HOME (apenas em IDLE).
// yield() previne reset do watchdog e permite a task BLE rodar.
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
        Serial.println("[HOMING] AVISO: endstop não detectado — verifique a fiação.");
        enviarStatus("HOME_WARN:ENDSTOP_NAO_DETECTADO");
    }
}

// ─── Callbacks BLE ────────────────────────────────────────────────────────────

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer*) override {
        clienteConectado = true;
        Serial.println("[BLE] Cliente conectado.");
    }
    void onDisconnect(BLEServer*) override {
        clienteConectado = false;
        Serial.println("[BLE] Cliente desconectado.");
        if (estadoAtual != IDLE) abortSolicitado = true;
        BLEDevice::startAdvertising();
    }
};

class CmdCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pChar) override {
        String valor = pChar->getValue();
        if (valor.length() > 0 && cmdMutex != nullptr) {
            // Timeout curto: se mutex não disponível em 10ms, descarta o comando
            if (xSemaphoreTake(cmdMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                bufferComando = valor;
                novoComando   = true;
                xSemaphoreGive(cmdMutex);
            }
            Serial.print("[BLE RX] ");
            Serial.println(valor);
        }
    }
};

// ─── Processamento de comandos ────────────────────────────────────────────────

void processarComando(const String& cmd) {
    Serial.print("[CMD] ");
    Serial.println(cmd);

    // SET:X.XX — seleciona distância de lançamento
    if (cmd.startsWith("SET:")) {
        if (estadoAtual != IDLE) { enviarStatus("ERROR:BUSY"); return; }
        float dist = cmd.substring(4).toFloat();
        int idx = distanciaParaIndice(dist);
        if (idx < 0) { enviarStatus("ERROR:DIST_INVALIDA"); return; }
        distanciaAlvo_m    = dist;
        passosSelecionados = stepsRAM[idx];
        enviarStatus("SET_OK:" + String(dist, 2) + "m:" + String(passosSelecionados) + "passos");
        return;
    }

    // ARM — tensiona o elástico (não-bloqueante)
    if (cmd == "ARM") {
        if (estadoAtual != IDLE) { enviarStatus("ERROR:ESTADO_INVALIDO"); return; }
        habilitarMotorT();
        motorTensao.moveTo(passosSelecionados);
        estadoAtual = TENSIONING;
        enviarStatus("TENSIONING:" + String(passosSelecionados));
        return;
    }

    // FIRE — aciona o motor de disparo para liberar o mecanismo
    if (cmd == "FIRE") {
        if (estadoAtual != ARMED) { enviarStatus("ERROR:NAO_ARMADO"); return; }
        habilitarMotorD();
        motorDisparo.move(DISPARO_PASSOS);
        tempoDisparo = millis();
        estadoAtual  = FIRING;
        Serial.println("[FSM] FIRING — motor de disparo acionado.");
        return;
    }

    // ABORT — para tudo e retorna ao zero
    if (cmd == "ABORT") {
        enviarStatus("ABORT:OK");
        // Reseta motor de disparo antes de retornar
        motorDisparo.stop();
        motorDisparo.setCurrentPosition(0);
        desabilitarMotorD();
        iniciarRetorno();
        enviarStatus("RETURNING");
        return;
    }

    // HOME — re-executa homing (apenas de IDLE; bloqueante por design)
    if (cmd == "HOME") {
        if (estadoAtual != IDLE) { enviarStatus("ERROR:BUSY"); return; }
        executarHoming();
        return;
    }

    // STATUS — telemetria atual
    if (cmd == "STATUS") {
        const char* nomes[] = {"IDLE","HOMING","TENSIONING","ARMED","FIRING","RETURNING"};
        String resp = "STATUS:" + String(nomes[estadoAtual]) +
                      ":BAT:" + String(percentualBateria) + "%" +
                      ":" + String(tensaoBateria_V, 2) + "V" +
                      ":DIST:" + String(distanciaAlvo_m, 2) + "m" +
                      ":POS:" + String(motorTensao.currentPosition());
        enviarStatus(resp);
        return;
    }

    // CAL:X.XX:NNN — atualiza lookup table em RAM (sem recompilação)
    if (cmd.startsWith("CAL:")) {
        int p = cmd.indexOf(':', 4);
        if (p < 0) { enviarStatus("ERROR:CAL_FORMATO"); return; }
        float dist        = cmd.substring(4, p).toFloat();
        int   novosPassos = cmd.substring(p + 1).toInt();
        int   idx         = distanciaParaIndice(dist);
        if (idx < 0)                                { enviarStatus("ERROR:CAL_DIST"); return; }
        if (novosPassos <= 0 || novosPassos > 9999) { enviarStatus("ERROR:CAL_PASSOS"); return; }
        stepsRAM[idx] = novosPassos;
        enviarStatus("CAL_OK:" + String(dist, 2) + "m:" + String(novosPassos) + "passos");
        return;
    }

    enviarStatus("ERROR:CMD_DESCONHECIDO");
}

// ─── Setup ────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Hercules I — Equipe A2 / IME 2026.1 — v1.2.0 ===\n");

    // Pinos de controle
    pinMode(PIN_LED,      OUTPUT);
    pinMode(PIN_T_ENABLE, OUTPUT);
    pinMode(PIN_D_ENABLE, OUTPUT);
    pinMode(PIN_ENDSTOP,  INPUT_PULLUP);

    desabilitarMotorT();
    desabilitarMotorD();

    // Carrega lookup table
    for (int i = 0; i < TABLE_SIZE; i++) stepsRAM[i] = STEPS_TABLE[i];

    // Motor de tensionamento
    motorTensao.setMaxSpeed(T_VELOCIDADE_MAX);
    motorTensao.setAcceleration(T_ACELERACAO);
    motorTensao.setCurrentPosition(0);

    // Motor de disparo — velocidade maior pois a rotação é curta e rápida
    motorDisparo.setMaxSpeed(DISPARO_VELOCIDADE);
    motorDisparo.setAcceleration(DISPARO_ACELERACAO);
    motorDisparo.setCurrentPosition(0);

    // ADC bateria
    analogReadResolution(12);
    lerBateria();
    Serial.printf("[INIT] Bateria: %.2fV (%d%%)\n", tensaoBateria_V, percentualBateria);

    // Mutex para proteger bufferComando entre task BLE e loop principal
    cmdMutex = xSemaphoreCreateMutex();

    // Homing: estabelece posição zero reprodutível antes de aceitar comandos
    executarHoming();

    // Inicializa BLE
    BLEDevice::init("Hercules-I");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    BLEService* pService = pServer->createService(SERVICE_UUID);

    pCmdChar = pService->createCharacteristic(CMD_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
    pCmdChar->setCallbacks(new CmdCallbacks());

    pStatusChar = pService->createCharacteristic(STATUS_CHAR_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    pStatusChar->addDescriptor(new BLE2902());

    pService->start();

    BLEAdvertising* pAdv = BLEDevice::getAdvertising();
    pAdv->addServiceUUID(SERVICE_UUID);
    pAdv->setScanResponse(true);
    pAdv->setMinPreferred(0x06);
    BLEDevice::startAdvertising();

    Serial.println("[BLE] Anunciando como 'Hercules-I'...");
    Serial.println("[INIT] Pronto — IDLE\n");
}

// ─── Loop principal ────────────────────────────────────────────────────────────

void loop() {
    unsigned long agora = millis();

    atualizarLED();

    // Leitura periódica da bateria
    if (agora - ultimaLeituraBat >= BAT_LEITURA_INTERVALO) {
        ultimaLeituraBat = agora;
        lerBateria();
    }

    // Timeout de segurança: 30s em ARMED dispara ABORT automático
    if (estadoAtual == ARMED && agora - tempoEntradaARMED >= ARMED_TIMEOUT_MS) {
        Serial.println("[FSM] Timeout ARMED — ABORT automático.");
        enviarStatus("ABORT:TIMEOUT");
        iniciarRetorno();
        enviarStatus("RETURNING");
    }

    // ── Motor de tensionamento (não-bloqueante) ────────────────────────────────
    if (estadoAtual == TENSIONING || estadoAtual == RETURNING) {
        if (motorTensao.distanceToGo() != 0) {
            motorTensao.run();
        } else {
            if (estadoAtual == TENSIONING) {
                // Motor permanece habilitado em ARMED para evitar deriva pelo elástico.
                estadoAtual = ARMED;
                tempoEntradaARMED = agora;
                enviarStatus("ARMED");
                Serial.println("[FSM] ARMED — aguardando FIRE.");
            } else {
                desabilitarMotorT();
                estadoAtual = IDLE;
                enviarStatus("IDLE");
                Serial.println("[FSM] IDLE — pronto para próximo lançamento.");
            }
        }
    }

    // ── Motor de disparo (não-bloqueante) ─────────────────────────────────────
    if (estadoAtual == FIRING) {
        if (motorDisparo.distanceToGo() != 0) {
            motorDisparo.run();
        } else {
            // Rotação do disparo completa — aguarda DISPARO_DELAY_MS antes de retornar
            if (agora - tempoDisparo >= DISPARO_DELAY_MS) {
                // Retorna motor de disparo ao zero
                motorDisparo.moveTo(0);
                while (motorDisparo.distanceToGo() != 0) {
                    motorDisparo.run();
                    yield();
                }
                desabilitarMotorD();

                enviarStatus("FIRED");
                iniciarRetorno();
                enviarStatus("RETURNING");
                Serial.println("[FSM] FIRED — retornando.");
            }
        }
    }

    // ── Abort sinalizado pela task BLE (desconexão ou comando ABORT) ──────────
    if (abortSolicitado) {
        abortSolicitado = false;
        motorDisparo.stop();
        motorDisparo.setCurrentPosition(0);
        desabilitarMotorD();
        iniciarRetorno();
        enviarStatus("ABORT:RETURNING");
        Serial.println("[FSM] Abort — retornando ao zero.");
    }

    // ── Leitura de comandos BLE (com mutex) ───────────────────────────────────
    if (novoComando) {
        String cmdCopy;
        if (xSemaphoreTake(cmdMutex, 0) == pdTRUE) {
            novoComando   = false;
            cmdCopy       = bufferComando;
            bufferComando = "";
            xSemaphoreGive(cmdMutex);
        }
        if (cmdCopy.length() > 0) processarComando(cmdCopy);
    }
}
