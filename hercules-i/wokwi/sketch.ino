/**
 * Projeto Hércules I — Equipe A2
 * IPE I — Instituto Militar de Engenharia — 2026.1
 *
 * Arquivo: wokwi/sketch.ino
 * Descrição: Versão de simulação do firmware para o Wokwi.
 *            - Motor controlado com steps simplificados (delay + digitalWrite)
 *            - BLE substituído por Serial Monitor (comandos via teclado)
 *            - Todas as transições de estado exibidas via Serial.println
 *            - Permite testar a FSM completa sem hardware real
 *
 * Como usar no Wokwi:
 *   1. Acesse https://wokwi.com e crie um novo projeto ESP32
 *   2. Copie este arquivo para sketch.ino
 *   3. Copie diagram.json para a aba "Diagram"
 *   4. Crie libraries.txt com a linha: ESP32Servo
 *   5. Clique em "Start Simulation"
 *   6. Abra o Serial Monitor (9600 baud)
 *   7. Digite comandos (SET:1.50, ARM, FIRE, ABORT, STATUS, CAL:1.00:260)
 *
 * Autor: Kalile (Gerente / Firmware)
 * Versão: 1.0.0
 * Data: Abril/2026
 *
 * === PINAGEM WOKWI ========================================================
 *  GPIO 26  → bobina A- do motor virtual
 *  GPIO 27  → bobina A+ do motor virtual
 *  GPIO 14  → bobina B- do motor virtual
 *  GPIO 12  → bobina B+ do motor virtual
 *  GPIO 13  → Servo SG90 (PWM)
 *  GPIO 34  → Potenciômetro (ADC simulando bateria)
 *  GPIO  2  → LED de status
 * =========================================================================
 */

// ─── Flag de modo de simulação ────────────────────────────────────────────
#define SIMULATION_MODE

#include <ESP32Servo.h>

// ─── Pinagem ──────────────────────────────────────────────────────────────
#define PIN_IN1   26    // Motor virtual (bobina A-)
#define PIN_IN2   27    // Motor virtual (bobina A+)
#define PIN_IN3   14    // Motor virtual (bobina B-)
#define PIN_IN4   12    // Motor virtual (bobina B+)
#define PIN_SERVO 13
#define PIN_ADC   34
#define PIN_LED    2

// ─── Configurações do servo ───────────────────────────────────────────────
#define SERVO_ARMADO  90
#define SERVO_FIRE     0

// ─── Tabela de calibração (lookup table) ─────────────────────────────────
// Índices: distância de 0,50m a 4,00m em passos de 0,25m
#define TABLE_SIZE   15
#define DIST_MIN_M   0.50f
#define DIST_STEP_M  0.25f

int stepsTabela[TABLE_SIZE] = {
     120,  // 0,50 m
     185,  // 0,75 m
     255,  // 1,00 m
     330,  // 1,25 m
     410,  // 1,50 m
     495,  // 1,75 m
     585,  // 2,00 m
     680,  // 2,25 m
     780,  // 2,50 m
     885,  // 2,75 m
     995,  // 3,00 m
    1110,  // 3,25 m
    1230,  // 3,50 m
    1355,  // 3,75 m
    1485   // 4,00 m
};

// ─── Estados da FSM ───────────────────────────────────────────────────────
enum Estado {
    IDLE,
    TENSIONING,
    ARMED,
    FIRING,
    RETURNING
};

Estado estadoAtual = IDLE;

// ─── Variáveis de controle ────────────────────────────────────────────────
float distanciaAlvo   = 1.00f;
int   passosSelecionados = 255;
long  posicaoAtual    = 0;       // Posição atual do motor em passos
int   percentualBat   = 100;

// Temporização
unsigned long tempoEntradaARMED = 0;
unsigned long ultimoLED         = 0;
bool          estadoLED         = false;

Servo servo;

// Buffer de entrada serial
String bufferSerial = "";

// ─── Sequência de meios passos para o motor virtual do Wokwi ─────────────
// Tabela de 8 fases (meios passos) para torque suave
const byte FASES[8][4] = {
    {1, 0, 0, 0},
    {1, 1, 0, 0},
    {0, 1, 0, 0},
    {0, 1, 1, 0},
    {0, 0, 1, 0},
    {0, 0, 1, 1},
    {0, 0, 0, 1},
    {1, 0, 0, 1}
};
int indiceFase = 0;

// ─── Funções do motor (modo simulação) ────────────────────────────────────

/**
 * Executa um único meio passo no motor.
 * direcao: 1 = para frente (tensionar), -1 = para trás (retornar)
 */
void passoMotor(int direcao) {
    indiceFase = (indiceFase + direcao + 8) % 8;
    digitalWrite(PIN_IN1, FASES[indiceFase][0]);
    digitalWrite(PIN_IN2, FASES[indiceFase][1]);
    digitalWrite(PIN_IN3, FASES[indiceFase][2]);
    digitalWrite(PIN_IN4, FASES[indiceFase][3]);
    delayMicroseconds(2000);  // ~500 steps/s simulado
}

/**
 * Desliga todas as bobinas (economiza energia, evita superaquecimento).
 */
void desligarBobinas() {
    digitalWrite(PIN_IN1, LOW);
    digitalWrite(PIN_IN2, LOW);
    digitalWrite(PIN_IN3, LOW);
    digitalWrite(PIN_IN4, LOW);
}

/**
 * Move o motor n passos em uma direção.
 * direcao: 1 = tensionar, -1 = retornar
 */
void moverMotor(int nPassos, int direcao) {
    for (int i = 0; i < nPassos; i++) {
        passoMotor(direcao);
        if (i % 50 == 0) {
            Serial.print(".");  // Indicador de progresso
        }
    }
    Serial.println();
    desligarBobinas();
}

// ─── Funções auxiliares ────────────────────────────────────────────────────

/**
 * Converte distância em metros para índice na tabela.
 */
int distanciaParaIndice(float dist) {
    if (dist < DIST_MIN_M || dist > 4.00f) return -1;
    return (int)round((dist - DIST_MIN_M) / DIST_STEP_M);
}

/**
 * Lê o ADC do potenciômetro e calcula percentual de "bateria".
 */
void lerBateria() {
    int adc = analogRead(PIN_ADC);
    percentualBat = (int)((adc / 4095.0f) * 100.0f);
}

/**
 * Imprime estado atual da FSM no Serial.
 */
void printEstado(const char* prefixo) {
    const char* nomes[] = {"IDLE", "TENSIONING", "ARMED", "FIRING", "RETURNING"};
    Serial.print("[FSM] ");
    Serial.print(prefixo);
    Serial.println(nomes[estadoAtual]);
}

/**
 * Executa ABORT: para motor e retorna ao zero.
 */
void executarAbort() {
    Serial.println("[ABORT] Retornando ao zero...");
    if (posicaoAtual > 0) {
        moverMotor(posicaoAtual, -1);
        posicaoAtual = 0;
    }
    servo.write(SERVO_ARMADO);
    estadoAtual = IDLE;
    printEstado("→ ");
    Serial.println("[SIM] Pronto para novo lançamento.");
}

// ─── Processamento de comandos (via Serial) ────────────────────────────────

void processarComandoSerial(const String& cmd) {
    Serial.println();
    Serial.print("[CMD] → ");
    Serial.println(cmd);

    // SET:X.XX
    if (cmd.startsWith("SET:")) {
        if (estadoAtual != IDLE) {
            Serial.println("[ERRO] Sistema ocupado. Envie ABORT primeiro.");
            return;
        }
        float dist = cmd.substring(4).toFloat();
        int idx = distanciaParaIndice(dist);
        if (idx < 0) {
            Serial.println("[ERRO] Distância fora do range (0.50 a 4.00 m).");
            return;
        }
        distanciaAlvo      = dist;
        passosSelecionados = stepsTabela[idx];
        Serial.printf("[OK] Distância: %.2f m → %d passos (índice %d)\n",
                      dist, passosSelecionados, idx);
        return;
    }

    // ARM
    if (cmd == "ARM") {
        if (estadoAtual != IDLE) {
            Serial.println("[ERRO] Estado inválido para ARM.");
            return;
        }
        estadoAtual = TENSIONING;
        printEstado("→ ");
        Serial.printf("[SIM] Tensionando: %d passos...\n", passosSelecionados);
        moverMotor(passosSelecionados, 1);
        posicaoAtual = passosSelecionados;
        estadoAtual = ARMED;
        tempoEntradaARMED = millis();
        printEstado("→ ");
        Serial.println("[SIM] Sistema ARMADO. Aguardando FIRE (timeout 30s).");
        return;
    }

    // FIRE
    if (cmd == "FIRE") {
        if (estadoAtual != ARMED) {
            Serial.println("[ERRO] Catapulta não está ARMED. Execute ARM primeiro.");
            return;
        }
        estadoAtual = FIRING;
        printEstado("→ ");
        Serial.println("[SIM] DISPARANDO! Servo liberando gatilho...");
        servo.write(SERVO_FIRE);
        delay(500);
        Serial.println("[SIM] DISPARADO!");

        // Retornar ao zero
        estadoAtual = RETURNING;
        printEstado("→ ");
        Serial.printf("[SIM] Retornando: %ld passos...\n", posicaoAtual);
        moverMotor(posicaoAtual, -1);
        posicaoAtual = 0;

        // Recolhe servo
        servo.write(SERVO_ARMADO);

        estadoAtual = IDLE;
        printEstado("→ ");
        Serial.println("[SIM] Ciclo completo. Pronto para novo lançamento.");
        return;
    }

    // ABORT
    if (cmd == "ABORT") {
        executarAbort();
        return;
    }

    // STATUS
    if (cmd == "STATUS") {
        lerBateria();
        const char* nomes[] = {"IDLE", "TENSIONING", "ARMED", "FIRING", "RETURNING"};
        Serial.println("─────────────────────────────────");
        Serial.printf("  Estado:    %s\n", nomes[estadoAtual]);
        Serial.printf("  Distância: %.2f m\n", distanciaAlvo);
        Serial.printf("  Passos:    %d\n", passosSelecionados);
        Serial.printf("  Posição:   %ld passos\n", posicaoAtual);
        Serial.printf("  Bateria:   %d%%\n", percentualBat);
        Serial.println("─────────────────────────────────");
        return;
    }

    // CAL:X.XX:NNN
    if (cmd.startsWith("CAL:")) {
        int p1 = cmd.indexOf(':', 4);
        if (p1 < 0) {
            Serial.println("[ERRO] Formato: CAL:X.XX:NNN");
            return;
        }
        float dist = cmd.substring(4, p1).toFloat();
        int novosPassos = cmd.substring(p1 + 1).toInt();
        int idx = distanciaParaIndice(dist);
        if (idx < 0) {
            Serial.println("[ERRO] Distância inválida.");
            return;
        }
        if (novosPassos <= 0 || novosPassos > 9999) {
            Serial.println("[ERRO] Passos inválidos (1-9999).");
            return;
        }
        stepsTabela[idx] = novosPassos;
        Serial.printf("[CAL] Atualizado: %.2fm → %d passos (idx %d)\n",
                      dist, novosPassos, idx);
        return;
    }

    // TABELA — exibe a lookup table completa
    if (cmd == "TABELA") {
        Serial.println("─────────────────────────────────");
        Serial.println("  LOOKUP TABLE ATUAL:");
        for (int i = 0; i < TABLE_SIZE; i++) {
            float d = DIST_MIN_M + i * DIST_STEP_M;
            Serial.printf("  [%02d] %.2f m → %4d passos\n", i, d, stepsTabela[i]);
        }
        Serial.println("─────────────────────────────────");
        return;
    }

    Serial.println("[ERRO] Comando desconhecido. Comandos: SET:X.XX ARM FIRE ABORT STATUS CAL:X.XX:NNN TABELA");
}

// ─── Setup ────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(9600);
    delay(500);

    Serial.println("\n====================================================");
    Serial.println("  PROJETO HERCULES I — MODO SIMULAÇÃO WOKWI");
    Serial.println("  Equipe A2 — IPE I — IME 2026.1");
    Serial.println("====================================================");
    Serial.println("  Comandos disponíveis:");
    Serial.println("    SET:X.XX  — Define distância (ex: SET:2.00)");
    Serial.println("    ARM       — Arma a catapulta");
    Serial.println("    FIRE      — Dispara");
    Serial.println("    ABORT     — Aborta operação");
    Serial.println("    STATUS    — Exibe status atual");
    Serial.println("    CAL:X.XX:N — Calibra distância");
    Serial.println("    TABELA    — Exibe lookup table completa");
    Serial.println("====================================================\n");

    // Pinos do motor
    pinMode(PIN_IN1, OUTPUT);
    pinMode(PIN_IN2, OUTPUT);
    pinMode(PIN_IN3, OUTPUT);
    pinMode(PIN_IN4, OUTPUT);
    desligarBobinas();

    // LED
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

    // Servo
    servo.attach(PIN_SERVO);
    servo.write(SERVO_ARMADO);

    // ADC
    analogReadResolution(12);

    Serial.println("[INIT] Sistema pronto. Estado: IDLE");
    Serial.print("> Comando: ");
}

// ─── Loop principal ────────────────────────────────────────────────────────

void loop() {
    unsigned long agora = millis();

    // ── Timeout automático no estado ARMED (30s) ──────────────────────
    if (estadoAtual == ARMED) {
        if (agora - tempoEntradaARMED >= 30000) {
            Serial.println("\n[TIMEOUT] 30s no estado ARMED — executando ABORT automático.");
            executarAbort();
        }
    }

    // ── LED de status ─────────────────────────────────────────────────
    unsigned long periodLED = (estadoAtual == IDLE) ? 500 : 100;
    if (estadoAtual == ARMED) {
        digitalWrite(PIN_LED, HIGH);
    } else if (agora - ultimoLED >= periodLED) {
        estadoLED = !estadoLED;
        digitalWrite(PIN_LED, estadoLED ? HIGH : LOW);
        ultimoLED = agora;
    }

    // ── Leitura de comandos via Serial ────────────────────────────────
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (bufferSerial.length() > 0) {
                bufferSerial.trim();
                processarComandoSerial(bufferSerial);
                bufferSerial = "";
                Serial.print("\n> Comando: ");
            }
        } else {
            bufferSerial += c;
            Serial.print(c);  // Eco local
        }
    }
}
