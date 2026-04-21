/**
 * Projeto Hércules I — Equipe A2
 * IPE I — Instituto Militar de Engenharia — 2026.1
 *
 * Arquivo: wokwi/sketch.ino
 * Versão: 1.1.0 — Abril/2026
 *
 * Simulação fiel ao firmware real:
 *   - Motor controlado via A4988 com AccelStepper no modo DRIVER (STEP/DIR/ENABLE)
 *   - Pinagem e FSM idênticas ao hercules_firmware.ino
 *   - Controle não-bloqueante: motor.run() chamado a cada ciclo do loop()
 *   - Homing via botão Endstop (GPIO 25) — pressione no diagrama quando solicitado
 *   - BLE substituído por Serial Monitor
 *
 * Diferenças inevitáveis em relação ao hardware real:
 *   - Sem stack BLE / sem mutex FreeRTOS (Serial é single-task)
 *   - VMOT do A4988 alimentado pelo 5V do ESP32 (real usa 9V)
 *   - Potenciômetro substitui o divisor resistivo R1/R2 no GPIO34
 *
 * Como usar:
 *   1. Inicie a simulação (F1 → Wokwi: Start Simulator)
 *   2. Abra o Serial Monitor (9600 baud)
 *   3. Durante o homing automático, pressione o botão ENDSTOP (verde) no diagrama
 *   4. Digite comandos: SET:1.50  ARM  FIRE  ABORT  HOME  STATUS  TABELA  CAL:X.XX:NNN
 *
 * === PINAGEM — idêntica ao firmware real =====================================
 *  GPIO 26  → A4988 STEP
 *  GPIO 27  → A4988 DIR
 *  GPIO 14  → A4988 ENABLE (ativo em LOW)
 *  GPIO 13  → Servo SG90 (PWM, via R3 220Ω)
 *  GPIO 25  → Botão endstop (NC simulado — LOW = home acionado)
 *  GPIO 34  → Potenciômetro (simula saída do divisor R1/R2 da bateria)
 *  GPIO  2  → LED de status
 * =============================================================================
 */

#include <AccelStepper.h>
#include <ESP32Servo.h>

// ─── Pinagem (idêntica ao firmware real) ─────────────────────────────────
#define PIN_STEP     26
#define PIN_DIR      27
#define PIN_ENABLE   14
#define PIN_SERVO    13
#define PIN_ENDSTOP  25
#define PIN_ADC      34
#define PIN_LED       2

// ─── Configurações do motor ───────────────────────────────────────────────
#define MOTOR_MAX_SPEED    500.0f
#define MOTOR_ACCELERATION 200.0f
#define HOMING_SPEED       200.0f
#define HOMING_MAX_STEPS   3000

// ─── Configurações do servo ───────────────────────────────────────────────
#define SERVO_ARMADO      90
#define SERVO_FIRE         0
#define SERVO_FIRE_DELAY 500

// ─── Tabela de calibração (espelho do lookup_table.h) ────────────────────
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

// ─── Estados da FSM (idênticos ao firmware real) ──────────────────────────
enum Estado { IDLE, HOMING, TENSIONING, ARMED, FIRING, RETURNING };
Estado estadoAtual = IDLE;

// ─── Variáveis de controle ────────────────────────────────────────────────
float         distanciaAlvo      = 1.00f;
int           passosSelecionados = 255;
int           percentualBat      = 100;
unsigned long tempoEntradaARMED  = 0;
unsigned long tempoDisparo       = 0;
unsigned long ultimoBlink        = 0;
bool          estadoLED          = false;

// ─── Hardware ─────────────────────────────────────────────────────────────
AccelStepper motor(AccelStepper::DRIVER, PIN_STEP, PIN_DIR);
Servo        servo;
String       bufferSerial = "";

// ─── Funções auxiliares ───────────────────────────────────────────────────

void habilitarMotor()   { digitalWrite(PIN_ENABLE, LOW);  }
void desabilitarMotor() { digitalWrite(PIN_ENABLE, HIGH); }

int distanciaParaIndice(float dist) {
    if (dist < DIST_MIN_M || dist > 4.00f) return -1;
    int idx = (int)round((dist - DIST_MIN_M) / DIST_STEP_M);
    return (idx >= TABLE_SIZE) ? TABLE_SIZE - 1 : idx;
}

void printEstado() {
    const char* nomes[] = {"IDLE","HOMING","TENSIONING","ARMED","FIRING","RETURNING"};
    Serial.print("[FSM] → ");
    Serial.println(nomes[estadoAtual]);
}

void atualizarLED() {
    unsigned long agora = millis();
    switch (estadoAtual) {
        case IDLE:
            if (agora - ultimoBlink >= 500) { estadoLED = !estadoLED; digitalWrite(PIN_LED, estadoLED); ultimoBlink = agora; }
            break;
        case HOMING:
        case TENSIONING:
            if (agora - ultimoBlink >= 100) { estadoLED = !estadoLED; digitalWrite(PIN_LED, estadoLED); ultimoBlink = agora; }
            break;
        case ARMED:
            digitalWrite(PIN_LED, HIGH);
            break;
        case FIRING:
        case RETURNING:
            if (agora - ultimoBlink >= 50) { estadoLED = !estadoLED; digitalWrite(PIN_LED, estadoLED); ultimoBlink = agora; }
            break;
    }
}

/**
 * Inicia retorno ao zero (não-bloqueante).
 * O loop() executa motor.run() e faz a transição para IDLE ao chegar em 0.
 */
void iniciarRetorno() {
    habilitarMotor();
    motor.moveTo(0);
    estadoAtual = RETURNING;
    printEstado();
    Serial.printf("[SIM] Retornando de %ld para 0 passos...\n", motor.currentPosition());
}

/**
 * Homing bloqueante: recua o motor até o botão Endstop ser pressionado.
 * No Wokwi, pressione o botão VERDE no diagrama durante a sequência.
 */
void executarHoming() {
    estadoAtual = HOMING;
    printEstado();
    Serial.println("[HOMING] Recuando — pressione o botão ENDSTOP (verde) no diagrama.");

    if (digitalRead(PIN_ENDSTOP) == LOW) {
        motor.setCurrentPosition(0);
        desabilitarMotor();
        estadoAtual = IDLE;
        Serial.println("[HOMING] Endstop já acionado — zero confirmado.");
        printEstado();
        return;
    }

    habilitarMotor();
    motor.setMaxSpeed(HOMING_SPEED);
    motor.move(-HOMING_MAX_STEPS);

    while (digitalRead(PIN_ENDSTOP) == HIGH && motor.distanceToGo() != 0) {
        motor.run();
        atualizarLED();
    }

    motor.stop();
    motor.setCurrentPosition(0);
    motor.setMaxSpeed(MOTOR_MAX_SPEED);
    desabilitarMotor();
    estadoAtual = IDLE;

    if (digitalRead(PIN_ENDSTOP) == LOW) {
        Serial.println("[HOMING] Posição zero estabelecida com sucesso.");
    } else {
        Serial.println("[HOMING] AVISO: limite atingido sem detectar endstop. Verifique o botão.");
    }
    printEstado();
}

// ─── Processamento de comandos (via Serial) ───────────────────────────────

void processarComandoSerial(const String& cmd) {
    Serial.println();
    Serial.print("[CMD] → ");
    Serial.println(cmd);

    // SET:X.XX
    if (cmd.startsWith("SET:")) {
        if (estadoAtual != IDLE) { Serial.println("[ERRO] Sistema ocupado. Envie ABORT primeiro."); return; }
        float dist = cmd.substring(4).toFloat();
        int idx = distanciaParaIndice(dist);
        if (idx < 0) { Serial.println("[ERRO] Distância fora do range (0.50 – 4.00 m)."); return; }
        distanciaAlvo      = dist;
        passosSelecionados = stepsTabela[idx];
        Serial.printf("[OK] Distância: %.2f m → %d passos (índice %d)\n", dist, passosSelecionados, idx);
        return;
    }

    // ARM — inicia tensionamento (não-bloqueante)
    if (cmd == "ARM") {
        if (estadoAtual != IDLE) { Serial.println("[ERRO] Estado inválido para ARM."); return; }
        habilitarMotor();
        motor.moveTo(passosSelecionados);
        estadoAtual = TENSIONING;
        printEstado();
        Serial.printf("[SIM] Tensionando %d passos... (motor movendo)\n", passosSelecionados);
        return;
    }

    // FIRE — libera servo e agenda retorno via timestamp (não-bloqueante)
    if (cmd == "FIRE") {
        if (estadoAtual != ARMED) { Serial.println("[ERRO] Catapulta não está ARMED."); return; }
        servo.write(SERVO_FIRE);
        tempoDisparo = millis();
        estadoAtual  = FIRING;
        printEstado();
        Serial.println("[SIM] DISPARANDO — servo liberado.");
        return;
    }

    // ABORT
    if (cmd == "ABORT") {
        Serial.println("[SIM] ABORT solicitado.");
        iniciarRetorno();
        return;
    }

    // HOME
    if (cmd == "HOME") {
        if (estadoAtual != IDLE) { Serial.println("[ERRO] Sistema ocupado. Envie ABORT primeiro."); return; }
        executarHoming();
        return;
    }

    // STATUS
    if (cmd == "STATUS") {
        percentualBat = (int)((analogRead(PIN_ADC) / 4095.0f) * 100.0f);
        const char* nomes[] = {"IDLE","HOMING","TENSIONING","ARMED","FIRING","RETURNING"};
        Serial.println("─────────────────────────────────");
        Serial.printf("  Estado:    %s\n",   nomes[estadoAtual]);
        Serial.printf("  Distância: %.2f m\n", distanciaAlvo);
        Serial.printf("  Passos:    %d\n",   passosSelecionados);
        Serial.printf("  Posição:   %ld\n",  motor.currentPosition());
        Serial.printf("  Restante:  %ld\n",  motor.distanceToGo());
        Serial.printf("  Bateria:   %d%%\n", percentualBat);
        Serial.println("─────────────────────────────────");
        return;
    }

    // CAL:X.XX:NNN
    if (cmd.startsWith("CAL:")) {
        int p = cmd.indexOf(':', 4);
        if (p < 0) { Serial.println("[ERRO] Formato: CAL:X.XX:NNN"); return; }
        float dist        = cmd.substring(4, p).toFloat();
        int   novosPassos = cmd.substring(p + 1).toInt();
        int   idx         = distanciaParaIndice(dist);
        if (idx < 0)                                { Serial.println("[ERRO] Distância inválida."); return; }
        if (novosPassos <= 0 || novosPassos > 9999) { Serial.println("[ERRO] Passos inválidos (1–9999)."); return; }
        stepsTabela[idx] = novosPassos;
        Serial.printf("[CAL] Atualizado: %.2fm → %d passos (idx %d)\n", dist, novosPassos, idx);
        return;
    }

    // TABELA
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

    Serial.println("[ERRO] Comandos: SET:X.XX  ARM  FIRE  ABORT  HOME  STATUS  CAL:X.XX:NNN  TABELA");
}

// ─── Setup ────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(9600);
    delay(500);

    Serial.println("\n====================================================");
    Serial.println("  HERCULES I — SIMULAÇÃO WOKWI v1.1");
    Serial.println("  A4988 + AccelStepper (modo DRIVER)");
    Serial.println("  Equipe A2 — IPE I — IME 2026.1");
    Serial.println("====================================================");
    Serial.println("  Comandos disponíveis:");
    Serial.println("    SET:X.XX     — Define distância (ex: SET:2.00)");
    Serial.println("    ARM          — Arma a catapulta (não-bloqueante)");
    Serial.println("    FIRE         — Dispara");
    Serial.println("    ABORT        — Aborta e retorna ao zero");
    Serial.println("    HOME         — Re-executa homing");
    Serial.println("    STATUS       — Exibe status atual");
    Serial.println("    CAL:X.XX:N   — Calibra distância");
    Serial.println("    TABELA       — Exibe lookup table");
    Serial.println("====================================================\n");

    pinMode(PIN_ENABLE,  OUTPUT);
    pinMode(PIN_ENDSTOP, INPUT_PULLUP);
    pinMode(PIN_LED,     OUTPUT);
    desabilitarMotor();
    digitalWrite(PIN_LED, LOW);

    motor.setMaxSpeed(MOTOR_MAX_SPEED);
    motor.setAcceleration(MOTOR_ACCELERATION);
    motor.setCurrentPosition(0);

    servo.attach(PIN_SERVO);
    servo.write(SERVO_ARMADO);

    analogReadResolution(12);

    // Homing inicial — pressione o botão ENDSTOP quando solicitado
    executarHoming();

    Serial.println("[INIT] Sistema pronto.");
    Serial.print("> Comando: ");
}

// ─── Loop principal ────────────────────────────────────────────────────────

void loop() {
    unsigned long agora = millis();

    atualizarLED();

    // Timeout de segurança no estado ARMED (30 s)
    if (estadoAtual == ARMED && agora - tempoEntradaARMED >= 30000) {
        Serial.println("\n[TIMEOUT] 30s em ARMED — ABORT automático.");
        iniciarRetorno();
    }

    // ── Controle não-bloqueante do motor ──────────────────────────────────
    if (estadoAtual == TENSIONING || estadoAtual == RETURNING) {
        if (motor.distanceToGo() != 0) {
            motor.run();
        } else {
            if (estadoAtual == TENSIONING) {
                // Motor permanece HABILITADO em ARMED (evita deriva pelo elástico)
                estadoAtual = ARMED;
                tempoEntradaARMED = agora;
                printEstado();
                Serial.println("[SIM] ARMADO — aguardando FIRE (timeout 30 s).");
            } else {  // RETURNING
                desabilitarMotor();
                servo.write(SERVO_ARMADO);
                estadoAtual = IDLE;
                printEstado();
                Serial.println("[SIM] Ciclo completo. Pronto para novo lançamento.");
                Serial.print("> Comando: ");
            }
        }
    }

    // ── Delay não-bloqueante do FIRING ────────────────────────────────────
    if (estadoAtual == FIRING && agora - tempoDisparo >= SERVO_FIRE_DELAY) {
        Serial.println("[SIM] DISPARADO!");
        iniciarRetorno();
    }

    // ── Leitura de comandos via Serial ────────────────────────────────────
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (bufferSerial.length() > 0) {
                bufferSerial.trim();
                processarComandoSerial(bufferSerial);
                bufferSerial = "";
                if (estadoAtual == IDLE) Serial.print("\n> Comando: ");
            }
        } else {
            bufferSerial += c;
            Serial.print(c);
        }
    }
}
