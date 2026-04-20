/**
 * Projeto Hércules I — Equipe A2
 * IPE I — Instituto Militar de Engenharia — 2026.1
 *
 * Arquivo: hercules_firmware.ino
 * Descrição: Firmware principal da catapulta programável Hércules I.
 *            Controla motor de passo (A4988/DRV8825 + NEMA 17), servo de disparo
 *            e servidor BLE para receber comandos do aplicativo mobile.
 * Autor: Kalile (Gerente / Firmware)
 * Versão: 1.0.0
 * Data: Abril/2026
 *
 * === PINAGEM COMPLETA =====================================================
 *
 *  GPIO 26  → STEP   (driver A4988/DRV8825)
 *  GPIO 27  → DIR    (driver A4988/DRV8825)
 *  GPIO 14  → ENABLE (driver A4988/DRV8825) — ativo em LOW
 *  GPIO 13  → Sinal PWM do servo (SG90 / MG996R)
 *  GPIO 34  → ADC monitoramento de bateria (entrada analógica only)
 *  GPIO  2  → LED de status onboard
 *
 *  Alimentação:
 *    6× AA (9V bruto) → step-down MP1584 → 5V lógica / 12V motor
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

#include "lookup_table.h"  // Tabela de calibração

// ─── UUIDs BLE ────────────────────────────────────────────────────────────
#define SERVICE_UUID     "12345678-1234-1234-1234-123456789abc"
#define CMD_CHAR_UUID    "12345678-1234-1234-1234-123456789ab1"  // Write: recebe comandos
#define STATUS_CHAR_UUID "12345678-1234-1234-1234-123456789ab2"  // Notify: envia status

// ─── Pinagem ──────────────────────────────────────────────────────────────
#define PIN_STEP    26
#define PIN_DIR     27
#define PIN_ENABLE  14
#define PIN_SERVO   13
#define PIN_ADC_BAT 34
#define PIN_LED      2

// ─── Configurações do motor ───────────────────────────────────────────────
#define MOTOR_MAX_SPEED    500.0f   // passos/segundo
#define MOTOR_ACCELERATION 200.0f   // passos/segundo²

// ─── Configurações do servo ───────────────────────────────────────────────
#define SERVO_POS_ARMED   90   // graus — gatilho travado
#define SERVO_POS_FIRE     0   // graus — gatilho liberado
#define SERVO_FIRE_DELAY 500   // ms — tempo aguardado após liberar

// ─── Configurações de bateria ─────────────────────────────────────────────
#define BAT_R1_KOHM        100.0f   // Resistor superior do divisor
#define BAT_R2_KOHM         10.0f   // Resistor inferior do divisor (ligado ao GND)
#define BAT_DIVISOR_FACTOR ((BAT_R1_KOHM + BAT_R2_KOHM) / BAT_R2_KOHM)  // = 11
#define BAT_NOMINAL_V        9.0f   // Tensão nominal do banco de pilhas
#define BAT_LOW_THRESHOLD    0.85f  // 85% da tensão nominal
#define BAT_READ_INTERVAL_MS 5000   // Leitura a cada 5 segundos

// ─── Configurações de temporização ───────────────────────────────────────
#define ARMED_TIMEOUT_MS  30000   // 30 segundos até ABORT automático
#define LED_SLOW_PERIOD    1000   // ms — pisca lento (IDLE): 1 Hz
#define LED_FAST_PERIOD     200   // ms — pisca rápido (TENSIONING): 5 Hz

// ─── Estados da máquina de estados finitos (FSM) ─────────────────────────
enum Estado {
    IDLE,
    TENSIONING,
    ARMED,
    FIRING,
    RETURNING
};

// ─── Variáveis globais ────────────────────────────────────────────────────

// FSM
volatile Estado estadoAtual = IDLE;

// Motor — cópia em RAM da lookup table (permite atualização via BLE)
int stepsRAM[TABLE_SIZE];

// Distância e passos selecionados
float distanciaAlvo_m = 1.00f;
int   passosSelecionados = 255;

// Posição atual do motor (rastreada manualmente para retorno correto)
long posicaoMotor = 0;

// BLE
BLEServer*          pServer        = nullptr;
BLECharacteristic*  pCmdChar       = nullptr;
BLECharacteristic*  pStatusChar    = nullptr;
bool                clienteConectado = false;
bool                novoComando    = false;
String              bufferComando  = "";

// Bateria
float tensaoBateria_V   = 9.0f;
int   percentualBateria = 100;
unsigned long ultimaLeituraBat = 0;

// Temporização
unsigned long tempoEntradaARMED = 0;
unsigned long ultimoBlink       = 0;
bool          estadoLED         = false;

// SOS para bateria fraca
bool   bateriaCritica  = false;
int    sosPasso        = 0;
unsigned long ultimoSOS = 0;

// Objetos de hardware
AccelStepper motor(AccelStepper::DRIVER, PIN_STEP, PIN_DIR);
Servo        servo;

// ─── Funções auxiliares ────────────────────────────────────────────────────

/**
 * Envia uma string de status via BLE Notify para o cliente conectado.
 */
void enviarStatus(const String& mensagem) {
    if (!clienteConectado || pStatusChar == nullptr) return;
    pStatusChar->setValue(mensagem.c_str());
    pStatusChar->notify();
    Serial.print("[BLE TX] ");
    Serial.println(mensagem);
}

/**
 * Habilita o driver do motor (ENABLE ativo em LOW).
 */
void habilitarMotor() {
    digitalWrite(PIN_ENABLE, LOW);
}

/**
 * Desabilita o driver do motor para economizar energia.
 */
void desabilitarMotor() {
    digitalWrite(PIN_ENABLE, HIGH);
}

/**
 * Converte distância em metros para índice na lookup table.
 * Retorna -1 se a distância estiver fora do intervalo suportado.
 */
int distanciaParaIndice(float dist_m) {
    if (dist_m < DIST_MIN_M || dist_m > DIST_MAX_M) return -1;
    int idx = (int)round((dist_m - DIST_MIN_M) / DIST_STEP_M);
    if (idx < 0) idx = 0;
    if (idx >= TABLE_SIZE) idx = TABLE_SIZE - 1;
    return idx;
}

/**
 * Lê o ADC de bateria, calcula tensão e percentual.
 */
void lerBateria() {
    int adcBruto = analogRead(PIN_ADC_BAT);
    // Tensão real = (leitura / 4095) * Vref * fator_divisor
    tensaoBateria_V = ((float)adcBruto / 4095.0f) * 3.3f * BAT_DIVISOR_FACTOR;

    // Percentual: linearizado entre 6V (vazio) e 9V (cheio)
    float vMin = 6.0f;
    float vMax = BAT_NOMINAL_V;
    percentualBateria = (int)(((tensaoBateria_V - vMin) / (vMax - vMin)) * 100.0f);
    if (percentualBateria < 0)   percentualBateria = 0;
    if (percentualBateria > 100) percentualBateria = 100;

    float limiar = BAT_NOMINAL_V * BAT_LOW_THRESHOLD;
    bateriaCritica = (tensaoBateria_V < limiar);

    if (bateriaCritica) {
        String msg = "BATTERY_LOW:" + String(percentualBateria) + "%";
        enviarStatus(msg);
        Serial.println("[BATERIA] Nível crítico: " + msg);
    }
}

/**
 * Gerencia o piscar do LED de status conforme o estado atual.
 * Também implementa o padrão SOS quando a bateria está crítica.
 */
void atualizarLED() {
    unsigned long agora = millis();

    // Padrão SOS: · · · — — — · · ·  (intervalos em ms)
    // Representado como array de durações [on, off, on, off, ...]
    if (bateriaCritica) {
        // Padrão SOS simplificado: 3 curtos, 3 longos, 3 curtos
        static const int sosDuracoes[] = {
            100, 100, 100, 100, 100, 300,   // S: · · ·
            300, 100, 300, 100, 300, 300,   // O: — — —
            100, 100, 100, 100, 100, 700    // S: · · · (pausa longa)
        };
        static const int numSosPasos = sizeof(sosDuracoes) / sizeof(sosDuracoes[0]);

        if (agora - ultimoSOS >= (unsigned long)sosDuracoes[sosPasso]) {
            estadoLED = !estadoLED;
            digitalWrite(PIN_LED, estadoLED ? HIGH : LOW);
            ultimoSOS = agora;
            sosPasso = (sosPasso + 1) % numSosPasos;
        }
        return;
    }

    // LED conforme estado da FSM
    switch (estadoAtual) {
        case IDLE:
            // Pisca lento: 1 Hz (500ms on, 500ms off)
            if (agora - ultimoBlink >= LED_SLOW_PERIOD / 2) {
                estadoLED = !estadoLED;
                digitalWrite(PIN_LED, estadoLED ? HIGH : LOW);
                ultimoBlink = agora;
            }
            break;

        case TENSIONING:
            // Pisca rápido: 5 Hz (100ms on, 100ms off)
            if (agora - ultimoBlink >= LED_FAST_PERIOD / 2) {
                estadoLED = !estadoLED;
                digitalWrite(PIN_LED, estadoLED ? HIGH : LOW);
                ultimoBlink = agora;
            }
            break;

        case ARMED:
            // LED fixo aceso
            digitalWrite(PIN_LED, HIGH);
            estadoLED = true;
            break;

        case FIRING:
        case RETURNING:
            // LED piscando muito rápido para indicar atividade
            if (agora - ultimoBlink >= 50) {
                estadoLED = !estadoLED;
                digitalWrite(PIN_LED, estadoLED ? HIGH : LOW);
                ultimoBlink = agora;
            }
            break;
    }
}

/**
 * Retorna o motor à posição zero de forma segura.
 */
void retornarZero() {
    habilitarMotor();
    motor.moveTo(0);
    while (motor.distanceToGo() != 0) {
        motor.run();
    }
    posicaoMotor = 0;
    desabilitarMotor();
}

/**
 * Executa a sequência de ABORT: para o motor e retorna ao zero.
 */
void executarAbort() {
    Serial.println("[FSM] ABORT executado.");
    motor.stop();
    estadoAtual = RETURNING;
    enviarStatus("RETURNING");
    retornarZero();
    servo.write(SERVO_POS_ARMED);  // Recoloca servo na posição de travado
    estadoAtual = IDLE;
    enviarStatus("IDLE");
}

// ─── Callbacks BLE ────────────────────────────────────────────────────────

/**
 * Callback de conexão/desconexão de cliente BLE.
 */
class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pSvr) override {
        clienteConectado = true;
        Serial.println("[BLE] Cliente conectado.");
    }

    void onDisconnect(BLEServer* pSvr) override {
        clienteConectado = false;
        Serial.println("[BLE] Cliente desconectado. Executando ABORT de segurança.");
        // Segurança: se cliente desconectar durante operação, abortar
        if (estadoAtual != IDLE) {
            executarAbort();
        }
        // Reinicia advertising para aceitar nova conexão
        BLEDevice::startAdvertising();
    }
};

/**
 * Callback de recepção de comando na characteristic de escrita.
 */
class CmdCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pChar) override {
        std::string valor = pChar->getValue();
        if (valor.length() > 0) {
            bufferComando = String(valor.c_str());
            novoComando   = true;
            Serial.print("[BLE RX] Comando recebido: ");
            Serial.println(bufferComando);
        }
    }
};

// ─── Processamento de comandos BLE ────────────────────────────────────────

/**
 * Processa um comando recebido via BLE e executa a ação correspondente.
 *
 * Comandos suportados:
 *   SET:X.XX  — Define distância alvo em metros
 *   ARM       — Inicia tensionamento do elástico
 *   FIRE      — Dispara (apenas no estado ARMED)
 *   ABORT     — Cancela operação e retorna ao zero
 *   STATUS    — Responde com status atual e nível de bateria
 *   CAL:X.XX:NNN — Atualiza lookup table em RAM
 */
void processarComando(const String& cmd) {
    Serial.print("[CMD] Processando: ");
    Serial.println(cmd);

    // ── SET:X.XX — Define distância alvo ──────────────────────────────────
    if (cmd.startsWith("SET:")) {
        if (estadoAtual != IDLE) {
            enviarStatus("ERROR:BUSY");
            return;
        }
        String valorStr = cmd.substring(4);
        float dist = valorStr.toFloat();
        int idx = distanciaParaIndice(dist);
        if (idx < 0) {
            enviarStatus("ERROR:DIST_INVALIDA");
            return;
        }
        distanciaAlvo_m      = dist;
        passosSelecionados   = stepsRAM[idx];
        enviarStatus("SET_OK:" + String(dist, 2) + "m:" + String(passosSelecionados) + "passos");
        Serial.printf("[CMD] Distância definida: %.2f m → %d passos\n", dist, passosSelecionados);
        return;
    }

    // ── ARM — Inicia tensionamento ─────────────────────────────────────────
    if (cmd == "ARM") {
        if (estadoAtual != IDLE) {
            enviarStatus("ERROR:ESTADO_INVALIDO");
            return;
        }
        estadoAtual = TENSIONING;
        String notif = "TENSIONING:" + String(passosSelecionados);
        enviarStatus(notif);
        Serial.printf("[FSM] TENSIONING → %d passos\n", passosSelecionados);

        // Tensiona o elástico movendo o motor
        habilitarMotor();
        motor.moveTo(passosSelecionados);
        while (motor.distanceToGo() != 0) {
            motor.run();
            atualizarLED();
            // Verifica desconexão BLE durante o tensionamento
            if (!clienteConectado) {
                executarAbort();
                return;
            }
        }
        posicaoMotor = passosSelecionados;
        desabilitarMotor();

        estadoAtual = ARMED;
        tempoEntradaARMED = millis();
        enviarStatus("ARMED");
        Serial.println("[FSM] ARMED — aguardando FIRE.");
        return;
    }

    // ── FIRE — Dispara ─────────────────────────────────────────────────────
    if (cmd == "FIRE") {
        if (estadoAtual != ARMED) {
            enviarStatus("ERROR:NAO_ARMADO");
            return;
        }
        estadoAtual = FIRING;
        Serial.println("[FSM] FIRING — liberando servo.");

        // Rotaciona o servo para liberar o gatilho
        servo.write(SERVO_POS_FIRE);
        delay(SERVO_FIRE_DELAY);
        enviarStatus("FIRED");
        Serial.println("[FSM] FIRED — servo liberado.");

        // Retorna ao zero
        estadoAtual = RETURNING;
        enviarStatus("RETURNING");
        Serial.println("[FSM] RETURNING — motor retornando ao zero.");
        retornarZero();

        // Reposiciona servo para posição de travado
        servo.write(SERVO_POS_ARMED);

        estadoAtual = IDLE;
        enviarStatus("IDLE");
        Serial.println("[FSM] IDLE — pronto para próximo lançamento.");
        return;
    }

    // ── ABORT — Cancela e retorna ao zero ────────────────────────────────
    if (cmd == "ABORT") {
        executarAbort();
        return;
    }

    // ── STATUS — Reporta estado atual e bateria ───────────────────────────
    if (cmd == "STATUS") {
        String nomeEstado;
        switch (estadoAtual) {
            case IDLE:       nomeEstado = "IDLE";       break;
            case TENSIONING: nomeEstado = "TENSIONING"; break;
            case ARMED:      nomeEstado = "ARMED";      break;
            case FIRING:     nomeEstado = "FIRING";     break;
            case RETURNING:  nomeEstado = "RETURNING";  break;
        }
        String resp = "STATUS:" + nomeEstado +
                      ":BAT:" + String(percentualBateria) + "%" +
                      ":" + String(tensaoBateria_V, 2) + "V" +
                      ":DIST:" + String(distanciaAlvo_m, 2) + "m";
        enviarStatus(resp);
        return;
    }

    // ── CAL:X.XX:NNN — Atualiza lookup table em RAM ───────────────────────
    if (cmd.startsWith("CAL:")) {
        // Formato: CAL:1.50:395
        int primeiroDoisPontos = cmd.indexOf(':', 4);
        if (primeiroDoisPontos < 0) {
            enviarStatus("ERROR:CAL_FORMATO");
            return;
        }
        float  dist      = cmd.substring(4, primeiroDoisPontos).toFloat();
        int    novosPassos = cmd.substring(primeiroDoisPontos + 1).toInt();
        int    idx       = distanciaParaIndice(dist);

        if (idx < 0) {
            enviarStatus("ERROR:CAL_DIST_INVALIDA");
            return;
        }
        if (novosPassos <= 0 || novosPassos > 9999) {
            enviarStatus("ERROR:CAL_PASSOS_INVALIDOS");
            return;
        }

        stepsRAM[idx] = novosPassos;
        Serial.printf("[CAL] Atualizado: %.2fm → %d passos (índice %d)\n", dist, novosPassos, idx);
        enviarStatus("CAL_OK:" + String(dist, 2) + "m:" + String(novosPassos) + "passos");
        return;
    }

    // Comando desconhecido
    enviarStatus("ERROR:CMD_DESCONHECIDO");
}

// ─── Setup ────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println("\n====================================");
    Serial.println(" Projeto Hercules I — Equipe A2");
    Serial.println(" IPE I — IME 2026.1");
    Serial.println("====================================\n");

    // ── Inicializa pinos de saída ──────────────────────────────────────────
    pinMode(PIN_LED,    OUTPUT);
    pinMode(PIN_ENABLE, OUTPUT);
    desabilitarMotor();  // Motor inicia desabilitado

    // ── Copia lookup table do flash para RAM ───────────────────────────────
    // (Permite atualização em campo via comando CAL:)
    for (int i = 0; i < TABLE_SIZE; i++) {
        stepsRAM[i] = STEPS_TABLE[i];
    }
    Serial.println("[INIT] Lookup table carregada para RAM.");

    // ── Configura motor de passo ───────────────────────────────────────────
    motor.setMaxSpeed(MOTOR_MAX_SPEED);
    motor.setAcceleration(MOTOR_ACCELERATION);
    motor.setCurrentPosition(0);
    Serial.printf("[INIT] Motor configurado: max=%d steps/s, accel=%d steps/s²\n",
                  (int)MOTOR_MAX_SPEED, (int)MOTOR_ACCELERATION);

    // ── Configura servo ────────────────────────────────────────────────────
    servo.attach(PIN_SERVO);
    servo.write(SERVO_POS_ARMED);  // Inicia na posição de travado
    Serial.printf("[INIT] Servo inicializado na posição %d°.\n", SERVO_POS_ARMED);

    // ── Inicializa ADC de bateria ──────────────────────────────────────────
    analogReadResolution(12);  // 12 bits: 0-4095
    lerBateria();
    Serial.printf("[INIT] Bateria: %.2fV (%d%%)\n", tensaoBateria_V, percentualBateria);

    // ── Inicializa BLE ────────────────────────────────────────────────────
    BLEDevice::init("Hercules-I");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    BLEService* pService = pServer->createService(SERVICE_UUID);

    // Characteristic de comando (Write)
    pCmdChar = pService->createCharacteristic(
        CMD_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    pCmdChar->setCallbacks(new CmdCallbacks());

    // Characteristic de status (Notify)
    pStatusChar = pService->createCharacteristic(
        STATUS_CHAR_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pStatusChar->addDescriptor(new BLE2902());

    pService->start();

    // Advertising BLE
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);  // Ajuda compatibilidade com iPhones
    BLEDevice::startAdvertising();

    Serial.println("[BLE] Servidor iniciado. Aguardando conexão...");
    Serial.println("[INIT] Sistema pronto — estado: IDLE\n");
}

// ─── Loop principal ────────────────────────────────────────────────────────

void loop() {
    unsigned long agora = millis();

    // ── Atualiza LED de status ─────────────────────────────────────────────
    atualizarLED();

    // ── Leitura periódica da bateria ───────────────────────────────────────
    if (agora - ultimaLeituraBat >= BAT_READ_INTERVAL_MS) {
        ultimaLeituraBat = agora;
        lerBateria();
    }

    // ── Timeout de segurança no estado ARMED ──────────────────────────────
    if (estadoAtual == ARMED) {
        if (agora - tempoEntradaARMED >= ARMED_TIMEOUT_MS) {
            Serial.println("[FSM] Timeout ARMED — executando ABORT automático.");
            enviarStatus("ABORT:TIMEOUT");
            executarAbort();
        }
    }

    // ── Processa novo comando BLE ──────────────────────────────────────────
    if (novoComando) {
        novoComando = false;
        String cmdCopy = bufferComando;
        bufferComando  = "";
        processarComando(cmdCopy);
    }

    // ── Mantém motor em run (caso em movimento) ───────────────────────────
    // Nota: durante TENSIONING/RETURNING, o motor é controlado por blocos
    // síncronos dentro de processarComando(). Esta chamada é para garantia.
    if (estadoAtual == TENSIONING || estadoAtual == RETURNING) {
        motor.run();
    }
}
