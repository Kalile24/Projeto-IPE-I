/**
 * Projeto Hércules I — Equipe A2
 * IPE I — Instituto Militar de Engenharia — 2026.1
 *
 * Arquivo: hercules_firmware.ino
 * Descrição: Firmware principal da catapulta programável Hércules I.
 * Autor: Kalile (Gerente / Firmware)
 * Versão: 1.1.0 — Abril/2026
 *
 * Correções v1.1.0:
 *   - Controle de motor totalmente não-bloqueante (sem while em loop principal)
 *   - ABORT seguro via flag volátil: pode interromper tensionamento a qualquer momento
 *   - Mutex FreeRTOS protege bufferComando contra race condition entre tasks BLE/main
 *   - posicaoMotor removida — posição rastreada internamente pelo AccelStepper
 *   - Motor mantido HABILITADO no estado ARMED para evitar deriva por força do elástico
 *   - Homing via fim de curso (GPIO 25) garante posição zero reprodutível entre ciclos
 *   - Delay do FIRING tratado por timestamp (não-bloqueante)
 *
 * === PINAGEM COMPLETA =====================================================
 *
 *  GPIO 26  → STEP   (driver A4988/DRV8825)
 *  GPIO 27  → DIR    (driver A4988/DRV8825)
 *  GPIO 14  → ENABLE (driver A4988/DRV8825) — ativo em LOW
 *  GPIO 13  → Sinal PWM do servo (SG90 / MG996R)
 *  GPIO 25  → Fim de curso / endstop (NC, ativo em LOW, pullup interno)
 *  GPIO 34  → ADC monitoramento de bateria (entrada analógica only)
 *  GPIO  2  → LED de status onboard
 *
 *  Alimentação:
 *    6× AA (9V bruto) → step-down MP1584 → 5V lógica
 *    Divisor resistivo bateria: R1=100kΩ / R2=10kΩ em série com GPIO34
 *    Tensão nominal banco: 9,0V (6× 1,5V)
 *    Fator divisor: (100k + 10k) / 10k = 11
 *
 * ==========================================================================
 */

// ─── Bibliotecas ──────────────────────────────────────────────────────────
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <AccelStepper.h>
#include <ESP32Servo.h>

#include "lookup_table.h"

// ─── UUIDs BLE ────────────────────────────────────────────────────────────
#define SERVICE_UUID     "12345678-1234-1234-1234-123456789abc"
#define CMD_CHAR_UUID    "12345678-1234-1234-1234-123456789ab1"
#define STATUS_CHAR_UUID "12345678-1234-1234-1234-123456789ab2"

// ─── Pinagem ──────────────────────────────────────────────────────────────
#define PIN_STEP     26
#define PIN_DIR      27
#define PIN_ENABLE   14
#define PIN_SERVO    13
#define PIN_ENDSTOP  25   // Fim de curso NC: LOW = acionado (motor em home)
#define PIN_ADC_BAT  34
#define PIN_LED       2

// ─── Configurações do motor ───────────────────────────────────────────────
#define MOTOR_MAX_SPEED    500.0f
#define MOTOR_ACCELERATION 200.0f
#define HOMING_SPEED       200.0f   // Velocidade reduzida durante homing
// Limite de passos para o homing (evita watchdog em busca infinita)
#define HOMING_MAX_STEPS   (STEPS_TABLE[TABLE_SIZE - 1] * 2)

// ─── Configurações do servo ───────────────────────────────────────────────
#define SERVO_POS_ARMED   90
#define SERVO_POS_FIRE     0
#define SERVO_FIRE_DELAY 500

// ─── Configurações de bateria ─────────────────────────────────────────────
#define BAT_R1_KOHM        100.0f
#define BAT_R2_KOHM         10.0f
#define BAT_DIVISOR_FACTOR ((BAT_R1_KOHM + BAT_R2_KOHM) / BAT_R2_KOHM)
#define BAT_NOMINAL_V        9.0f
#define BAT_LOW_THRESHOLD    0.85f
#define BAT_READ_INTERVAL_MS 5000

// ─── Configurações de temporização ───────────────────────────────────────
#define ARMED_TIMEOUT_MS  30000
#define LED_SLOW_PERIOD    1000
#define LED_FAST_PERIOD     200

// ─── Estados da FSM ───────────────────────────────────────────────────────
enum Estado {
    IDLE,
    HOMING,
    TENSIONING,
    ARMED,
    FIRING,
    RETURNING
};

// ─── Variáveis globais ────────────────────────────────────────────────────

volatile Estado estadoAtual = IDLE;

int   stepsRAM[TABLE_SIZE];
float distanciaAlvo_m    = 1.00f;
int   passosSelecionados = 255;

// Flag cross-task: BLE task sinaliza abort para o loop principal
volatile bool abortSolicitado = false;

// Timestamp para delay não-bloqueante no estado FIRING
unsigned long tempoDisparo = 0;

// BLE
BLEServer*         pServer          = nullptr;
BLECharacteristic* pCmdChar         = nullptr;
BLECharacteristic* pStatusChar      = nullptr;
bool               clienteConectado = false;
volatile bool      novoComando      = false;
String             bufferComando    = "";
SemaphoreHandle_t  cmdMutex         = nullptr;  // Protege bufferComando/novoComando

// Bateria
float         tensaoBateria_V   = 9.0f;
int           percentualBateria = 100;
unsigned long ultimaLeituraBat  = 0;

// Temporização / LED
unsigned long tempoEntradaARMED = 0;
unsigned long ultimoBlink       = 0;
bool          estadoLED         = false;
bool          bateriaCritica    = false;
int           sosPasso          = 0;
unsigned long ultimoSOS         = 0;

// Hardware
AccelStepper motor(AccelStepper::DRIVER, PIN_STEP, PIN_DIR);
Servo        servo;

// ─── Funções auxiliares ────────────────────────────────────────────────────

void enviarStatus(const String& mensagem) {
    if (!clienteConectado || pStatusChar == nullptr) return;
    pStatusChar->setValue(mensagem.c_str());
    pStatusChar->notify();
    Serial.print("[BLE TX] ");
    Serial.println(mensagem);
}

void habilitarMotor()   { digitalWrite(PIN_ENABLE, LOW);  }
void desabilitarMotor() { digitalWrite(PIN_ENABLE, HIGH); }

int distanciaParaIndice(float dist_m) {
    if (dist_m < DIST_MIN_M || dist_m > DIST_MAX_M) return -1;
    int idx = (int)round((dist_m - DIST_MIN_M) / DIST_STEP_M);
    if (idx < 0) idx = 0;
    if (idx >= TABLE_SIZE) idx = TABLE_SIZE - 1;
    return idx;
}

void lerBateria() {
    int adcBruto = analogRead(PIN_ADC_BAT);
    tensaoBateria_V = ((float)adcBruto / 4095.0f) * 3.3f * BAT_DIVISOR_FACTOR;
    float vMin = 6.0f, vMax = BAT_NOMINAL_V;
    percentualBateria = (int)(((tensaoBateria_V - vMin) / (vMax - vMin)) * 100.0f);
    if (percentualBateria < 0)   percentualBateria = 0;
    if (percentualBateria > 100) percentualBateria = 100;
    bateriaCritica = (tensaoBateria_V < BAT_NOMINAL_V * BAT_LOW_THRESHOLD);
    if (bateriaCritica) {
        enviarStatus("BATTERY_LOW:" + String(percentualBateria) + "%");
        Serial.println("[BATERIA] Nível crítico.");
    }
}

void atualizarLED() {
    unsigned long agora = millis();
    if (bateriaCritica) {
        static const int sosDuracoes[] = {
            100,100,100,100,100,300,
            300,100,300,100,300,300,
            100,100,100,100,100,700
        };
        static const int numSosPasos = sizeof(sosDuracoes) / sizeof(sosDuracoes[0]);
        if (agora - ultimoSOS >= (unsigned long)sosDuracoes[sosPasso]) {
            estadoLED = !estadoLED;
            digitalWrite(PIN_LED, estadoLED);
            ultimoSOS = agora;
            sosPasso  = (sosPasso + 1) % numSosPasos;
        }
        return;
    }
    switch (estadoAtual) {
        case IDLE:
            if (agora - ultimoBlink >= LED_SLOW_PERIOD / 2) {
                estadoLED = !estadoLED;
                digitalWrite(PIN_LED, estadoLED);
                ultimoBlink = agora;
            }
            break;
        case HOMING:
        case TENSIONING:
            if (agora - ultimoBlink >= LED_FAST_PERIOD / 2) {
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

/**
 * Inicia retorno ao zero de forma não-bloqueante.
 * O loop() executa motor.run() e transiciona para IDLE ao chegar em 0.
 */
void iniciarRetorno() {
    habilitarMotor();
    motor.moveTo(0);
    estadoAtual = RETURNING;
}

/**
 * Executa homing bloqueante (apenas chamado no setup() ou pelo comando HOME em IDLE).
 * Move o motor para trás até acionar o fim de curso e define posição = 0.
 * yield() dentro do loop previne reset pelo Task Watchdog Timer.
 */
void executarHoming() {
    Serial.println("[HOMING] Iniciando homing...");
    enviarStatus("HOMING");
    estadoAtual = HOMING;

    if (digitalRead(PIN_ENDSTOP) == LOW) {
        // Já está em home
        motor.setCurrentPosition(0);
        desabilitarMotor();
        Serial.println("[HOMING] Endstop já acionado — zero confirmado.");
        estadoAtual = IDLE;
        enviarStatus("HOME_OK");
        return;
    }

    habilitarMotor();
    motor.setMaxSpeed(HOMING_SPEED);
    motor.move(-HOMING_MAX_STEPS);

    while (digitalRead(PIN_ENDSTOP) == HIGH && motor.distanceToGo() != 0) {
        motor.run();
        yield();  // Previne watchdog reset e permite task BLE rodar
    }

    motor.stop();
    motor.setCurrentPosition(0);
    motor.setMaxSpeed(MOTOR_MAX_SPEED);
    desabilitarMotor();
    estadoAtual = IDLE;

    if (digitalRead(PIN_ENDSTOP) == LOW) {
        Serial.println("[HOMING] Posição zero estabelecida com sucesso.");
        enviarStatus("HOME_OK");
    } else {
        Serial.println("[HOMING] AVISO: endstop não detectado — verifique a fiação.");
        enviarStatus("HOME_WARN:ENDSTOP_NAO_DETECTADO");
    }
}

// ─── Callbacks BLE ────────────────────────────────────────────────────────

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pSvr) override {
        clienteConectado = true;
        Serial.println("[BLE] Cliente conectado.");
    }
    void onDisconnect(BLEServer* pSvr) override {
        clienteConectado = false;
        Serial.println("[BLE] Cliente desconectado.");
        if (estadoAtual != IDLE) {
            // Sinaliza abort para o loop() — não chama funções de motor aqui
            abortSolicitado = true;
        }
        BLEDevice::startAdvertising();
    }
};

class CmdCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pChar) override {
        std::string valor = pChar->getValue();
        if (valor.length() > 0 && cmdMutex != nullptr) {
            // Mutex com timeout curto: se não conseguir em 10 ms, descarta
            if (xSemaphoreTake(cmdMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                bufferComando = String(valor.c_str());
                novoComando   = true;
                xSemaphoreGive(cmdMutex);
            }
            Serial.print("[BLE RX] ");
            Serial.println(String(valor.c_str()));
        }
    }
};

// ─── Processamento de comandos ────────────────────────────────────────────

void processarComando(const String& cmd) {
    Serial.print("[CMD] ");
    Serial.println(cmd);

    // SET:X.XX — define distância alvo
    if (cmd.startsWith("SET:")) {
        if (estadoAtual != IDLE) { enviarStatus("ERROR:BUSY"); return; }
        float dist = cmd.substring(4).toFloat();
        int idx = distanciaParaIndice(dist);
        if (idx < 0) { enviarStatus("ERROR:DIST_INVALIDA"); return; }
        distanciaAlvo_m    = dist;
        passosSelecionados = stepsRAM[idx];
        enviarStatus("SET_OK:" + String(dist, 2) + "m:" + String(passosSelecionados) + "passos");
        Serial.printf("[CMD] Distância: %.2f m → %d passos\n", dist, passosSelecionados);
        return;
    }

    // ARM — inicia tensionamento (não-bloqueante: apenas configura alvo e retorna)
    if (cmd == "ARM") {
        if (estadoAtual != IDLE) { enviarStatus("ERROR:ESTADO_INVALIDO"); return; }
        habilitarMotor();
        motor.moveTo(passosSelecionados);
        estadoAtual = TENSIONING;
        enviarStatus("TENSIONING:" + String(passosSelecionados));
        Serial.printf("[FSM] TENSIONING → %d passos\n", passosSelecionados);
        return;
    }

    // FIRE — libera servo e agenda retorno via timestamp (não-bloqueante)
    if (cmd == "FIRE") {
        if (estadoAtual != ARMED) { enviarStatus("ERROR:NAO_ARMADO"); return; }
        servo.write(SERVO_POS_FIRE);
        tempoDisparo = millis();
        estadoAtual  = FIRING;
        Serial.println("[FSM] FIRING — servo liberado.");
        return;
    }

    // ABORT — interrompe qualquer operação e retorna ao zero
    if (cmd == "ABORT") {
        enviarStatus("ABORT:OK");
        iniciarRetorno();
        enviarStatus("RETURNING");
        Serial.println("[FSM] ABORT — retornando ao zero.");
        return;
    }

    // HOME — re-executa homing (apenas em IDLE; operação bloqueante intencional)
    if (cmd == "HOME") {
        if (estadoAtual != IDLE) { enviarStatus("ERROR:BUSY"); return; }
        executarHoming();
        return;
    }

    // STATUS — reporta estado atual
    if (cmd == "STATUS") {
        const char* nomes[] = {"IDLE","HOMING","TENSIONING","ARMED","FIRING","RETURNING"};
        String resp = "STATUS:" + String(nomes[estadoAtual]) +
                      ":BAT:" + String(percentualBateria) + "%" +
                      ":" + String(tensaoBateria_V, 2) + "V" +
                      ":DIST:" + String(distanciaAlvo_m, 2) + "m" +
                      ":POS:" + String(motor.currentPosition());
        enviarStatus(resp);
        return;
    }

    // CAL:X.XX:NNN — atualiza lookup table em RAM
    if (cmd.startsWith("CAL:")) {
        int p = cmd.indexOf(':', 4);
        if (p < 0) { enviarStatus("ERROR:CAL_FORMATO"); return; }
        float dist        = cmd.substring(4, p).toFloat();
        int   novosPassos = cmd.substring(p + 1).toInt();
        int   idx         = distanciaParaIndice(dist);
        if (idx < 0)                                { enviarStatus("ERROR:CAL_DIST_INVALIDA");    return; }
        if (novosPassos <= 0 || novosPassos > 9999) { enviarStatus("ERROR:CAL_PASSOS_INVALIDOS"); return; }
        stepsRAM[idx] = novosPassos;
        Serial.printf("[CAL] %.2fm → %d passos (idx %d)\n", dist, novosPassos, idx);
        enviarStatus("CAL_OK:" + String(dist, 2) + "m:" + String(novosPassos) + "passos");
        return;
    }

    enviarStatus("ERROR:CMD_DESCONHECIDO");
}

// ─── Setup ────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println("\n=====================================");
    Serial.println(" Projeto Hercules I — Equipe A2");
    Serial.println(" IPE I — IME 2026.1  v1.1.0");
    Serial.println("=====================================\n");

    pinMode(PIN_LED,     OUTPUT);
    pinMode(PIN_ENABLE,  OUTPUT);
    pinMode(PIN_ENDSTOP, INPUT_PULLUP);
    desabilitarMotor();

    for (int i = 0; i < TABLE_SIZE; i++) stepsRAM[i] = STEPS_TABLE[i];
    Serial.println("[INIT] Lookup table carregada.");

    motor.setMaxSpeed(MOTOR_MAX_SPEED);
    motor.setAcceleration(MOTOR_ACCELERATION);
    motor.setCurrentPosition(0);

    servo.attach(PIN_SERVO);
    servo.write(SERVO_POS_ARMED);
    Serial.printf("[INIT] Servo inicializado em %d°.\n", SERVO_POS_ARMED);

    analogReadResolution(12);
    lerBateria();
    Serial.printf("[INIT] Bateria: %.2fV (%d%%)\n", tensaoBateria_V, percentualBateria);

    cmdMutex = xSemaphoreCreateMutex();

    // Homing inicial — estabelece posição zero reprodutível
    executarHoming();

    // BLE
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

    Serial.println("[BLE] Servidor iniciado. Aguardando conexão...");
    Serial.println("[INIT] Sistema pronto — IDLE\n");
}

// ─── Loop principal ────────────────────────────────────────────────────────

void loop() {
    unsigned long agora = millis();

    atualizarLED();

    if (agora - ultimaLeituraBat >= BAT_READ_INTERVAL_MS) {
        ultimaLeituraBat = agora;
        lerBateria();
    }

    // Timeout de segurança no estado ARMED
    if (estadoAtual == ARMED && agora - tempoEntradaARMED >= ARMED_TIMEOUT_MS) {
        Serial.println("[FSM] Timeout ARMED — ABORT automático.");
        enviarStatus("ABORT:TIMEOUT");
        iniciarRetorno();
        enviarStatus("RETURNING");
    }

    // ── Controle não-bloqueante do motor ──────────────────────────────────
    if (estadoAtual == TENSIONING || estadoAtual == RETURNING) {
        if (motor.distanceToGo() != 0) {
            motor.run();
        } else {
            // Chegou ao destino — transição de estado
            if (estadoAtual == TENSIONING) {
                // Motor permanece HABILITADO em ARMED para evitar deriva pelo elástico.
                // Impacto: aquecimento do driver durante a espera (~30s máx pelo timeout).
                estadoAtual = ARMED;
                tempoEntradaARMED = agora;
                enviarStatus("ARMED");
                Serial.println("[FSM] ARMED — aguardando FIRE.");
            } else {  // RETURNING
                desabilitarMotor();
                servo.write(SERVO_POS_ARMED);
                estadoAtual = IDLE;
                enviarStatus("IDLE");
                Serial.println("[FSM] IDLE — pronto para próximo lançamento.");
            }
        }
    }

    // ── Delay não-bloqueante do FIRING ────────────────────────────────────
    if (estadoAtual == FIRING && agora - tempoDisparo >= SERVO_FIRE_DELAY) {
        enviarStatus("FIRED");
        iniciarRetorno();
        enviarStatus("RETURNING");
        Serial.println("[FSM] FIRED — iniciando retorno.");
    }

    // ── Processa flag de abort vinda da task BLE ──────────────────────────
    if (abortSolicitado) {
        abortSolicitado = false;
        Serial.println("[FSM] Abort solicitado pela task BLE.");
        iniciarRetorno();
        enviarStatus("ABORT:RETURNING");
    }

    // ── Processa novo comando BLE (com mutex) ─────────────────────────────
    if (novoComando) {
        String cmdCopy;
        if (xSemaphoreTake(cmdMutex, 0) == pdTRUE) {
            novoComando   = false;
            cmdCopy       = bufferComando;
            bufferComando = "";
            xSemaphoreGive(cmdMutex);
        }
        if (cmdCopy.length() > 0) {
            processarComando(cmdCopy);
        }
    }
}
