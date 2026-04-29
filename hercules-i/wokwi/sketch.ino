/**
 * Hércules I — Simulação Wokwi v1.4.0
 * Equipe A2 / IPE I / IME 2026.1
 *
 * Use esta simulação para validar a lógica antes de montar o hardware.
 * O diagram.json é a referência visual de montagem do circuito real.
 *
 * Diferenças em relação ao firmware real:
 *   - Bluetooth substituído por Serial Monitor (mesmos comandos)
 *   - Potenciômetro em GPIO 34 simula o divisor de tensão da bateria
 *   - Homing inicial ignorado — use HOME + botão ENDSTOP para testar
 *
 * PINAGEM — idêntica ao hardware real:
 *   Motor tensionamento:  GPIO 26 (STEP), 27 (DIR), 14 (ENABLE)
 *   Motor disparo:        GPIO 18 (STEP), 19 (DIR), 21 (ENABLE)
 *   Fim de curso:         GPIO 25 — botão verde no diagrama
 *   Bateria (simulada):   GPIO 34 — potenciômetro no diagrama
 *   LED status:           GPIO 2
 *
 * SEQUÊNCIA DE TESTE:
 *   STATUS          verifica estado inicial
 *   LAUNCH:1.50     tensiona e dispara em 1,50 m (sequência completa)
 *   ABORT           para tudo e retorna ao zero
 *   HOME            homing — pressione o botão verde quando solicitado
 */

#include <AccelStepper.h>

// ─── Pinagem ──────────────────────────────────────────────────────────────────
#define PIN_T_STEP    26
#define PIN_T_DIR     27
#define PIN_T_ENABLE  14

#define PIN_D_STEP    18
#define PIN_D_DIR     19
#define PIN_D_ENABLE  21

#define PIN_ENDSTOP   25
#define PIN_ADC       34
#define PIN_LED        2

// ─── Níveis lógicos ───────────────────────────────────────────────────────────
#define MOTOR_ENABLE_ON   LOW
#define MOTOR_ENABLE_OFF  HIGH
#define ENDSTOP_ATIVO     LOW

// ─── Parâmetros do motor de tensionamento ─────────────────────────────────────
#define T_VELOCIDADE_MAX  500.0f
#define T_ACELERACAO      200.0f
#define T_VELOCIDADE_HOME 200.0f
#define T_HOMING_MAX      3000

// ─── Parâmetros do motor de disparo ───────────────────────────────────────────
#define DISPARO_PASSOS      200
#define DISPARO_VELOCIDADE  800.0f
#define DISPARO_ACELERACAO  400.0f
#define DISPARO_DELAY_MS    300

// ─── Lookup table (espelho do lookup_table.h) ─────────────────────────────────
#define TABLE_SIZE  15
#define DIST_MIN_M  0.50f
#define DIST_STEP_M 0.25f

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

// ─── FSM ──────────────────────────────────────────────────────────────────────
enum Estado { IDLE, HOMING, TENSIONING, ARMED, FIRING, RETURNING };
Estado estadoAtual = IDLE;

// ─── Variáveis de controle ────────────────────────────────────────────────────
float         distanciaAlvo      = 1.00f;
int           passosSelecionados = 255;
unsigned long tempoEntradaARMED  = 0;
unsigned long tempoDisparo       = 0;
unsigned long ultimoBlink        = 0;
bool          estadoLED          = false;
bool          disparoCompleto    = false;
bool          autoDisparar      = false;

// ─── Hardware ─────────────────────────────────────────────────────────────────
AccelStepper motorTensao(AccelStepper::DRIVER,  PIN_T_STEP, PIN_T_DIR);
AccelStepper motorDisparo(AccelStepper::DRIVER, PIN_D_STEP, PIN_D_DIR);
String       bufferSerial = "";

// ─── Funções auxiliares ───────────────────────────────────────────────────────

void habilitarMotorT()   { digitalWrite(PIN_T_ENABLE, MOTOR_ENABLE_ON);  }
void desabilitarMotorT() { digitalWrite(PIN_T_ENABLE, MOTOR_ENABLE_OFF); }
void habilitarMotorD()   { digitalWrite(PIN_D_ENABLE, MOTOR_ENABLE_ON);  }
void desabilitarMotorD() { digitalWrite(PIN_D_ENABLE, MOTOR_ENABLE_OFF); }

bool endstopAcionado() {
    return digitalRead(PIN_ENDSTOP) == ENDSTOP_ATIVO;
}

int distanciaParaIndice(float dist) {
    if (dist < DIST_MIN_M || dist > 4.00f) return -1;
    int idx = (int)round((dist - DIST_MIN_M) / DIST_STEP_M);
    return (idx >= TABLE_SIZE) ? TABLE_SIZE - 1 : idx;
}

const char* nomeEstado() {
    const char* nomes[] = {"IDLE","HOMING","TENSIONING","ARMED","FIRING","RETURNING"};
    return nomes[estadoAtual];
}

void printEstado() {
    Serial.print("[FSM] ");
    Serial.println(nomeEstado());
}

void atualizarLED() {
    unsigned long agora = millis();
    switch (estadoAtual) {
        case IDLE:
            if (agora - ultimoBlink >= 500) {
                estadoLED = !estadoLED;
                digitalWrite(PIN_LED, estadoLED);
                ultimoBlink = agora;
            }
            break;
        case HOMING:
        case TENSIONING:
            if (agora - ultimoBlink >= 100) {
                estadoLED = !estadoLED;
                digitalWrite(PIN_LED, estadoLED);
                ultimoBlink = agora;
            }
            break;
        case ARMED:
            digitalWrite(PIN_LED, HIGH);
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
    habilitarMotorT();
    motorTensao.moveTo(0);
    estadoAtual = RETURNING;
    printEstado();
    Serial.printf("[SIM] Retornando para zero (posição atual: %ld).\n", motorTensao.currentPosition());
}

void executarHoming() {
    estadoAtual = HOMING;
    printEstado();
    Serial.println("[HOMING] Recuando — pressione o botão ENDSTOP (verde) no diagrama.");

    if (endstopAcionado()) {
        motorTensao.setCurrentPosition(0);
        desabilitarMotorT();
        estadoAtual = IDLE;
        Serial.println("[HOMING] Endstop já ativo — zero confirmado.");
        printEstado();
        return;
    }

    habilitarMotorT();
    motorTensao.setMaxSpeed(T_VELOCIDADE_HOME);
    motorTensao.move(-T_HOMING_MAX);

    while (!endstopAcionado() && motorTensao.distanceToGo() != 0) {
        motorTensao.run();
        atualizarLED();
    }

    motorTensao.stop();
    motorTensao.setCurrentPosition(0);
    motorTensao.setMaxSpeed(T_VELOCIDADE_MAX);
    desabilitarMotorT();
    estadoAtual = IDLE;

    if (endstopAcionado()) {
        Serial.println("[HOMING] Posição zero estabelecida.");
    } else {
        Serial.println("[HOMING] AVISO: limite atingido sem detectar endstop. Verifique o botão.");
    }
    printEstado();
}

// ─── Processamento de comandos ────────────────────────────────────────────────

void processarComando(const String& cmd) {
    Serial.println();
    Serial.print("[CMD] ");
    Serial.println(cmd);

    // SET:X.XX
    if (cmd.startsWith("SET:")) {
        if (estadoAtual != IDLE) { Serial.println("[ERRO] Sistema ocupado. Envie ABORT primeiro."); return; }
        float dist = cmd.substring(4).toFloat();
        int idx = distanciaParaIndice(dist);
        if (idx < 0) { Serial.println("[ERRO] Distância fora do range (0.50 – 4.00 m)."); return; }
        distanciaAlvo      = dist;
        passosSelecionados = stepsTabela[idx];
        Serial.printf("[OK] %.2f m → %d passos (índice %d)\n", dist, passosSelecionados, idx);
        return;
    }

    // LAUNCH:X.XX — sequência completa automática
    if (cmd.startsWith("LAUNCH:")) {
        if (estadoAtual != IDLE) { Serial.println("[ERRO] Sistema ocupado. Envie ABORT primeiro."); return; }
        float dist = cmd.substring(7).toFloat();
        int idx = distanciaParaIndice(dist);
        if (idx < 0) { Serial.println("[ERRO] Distância fora do range (0.50 – 4.00 m)."); return; }
        distanciaAlvo      = dist;
        passosSelecionados = stepsTabela[idx];
        autoDisparar = true;
        habilitarMotorT();
        motorTensao.moveTo(passosSelecionados);
        estadoAtual = TENSIONING;
        printEstado();
        Serial.printf("[SIM] LAUNCH %.2fm → %d passos (auto-disparo)\n", dist, passosSelecionados);
        return;
    }

    // ARM
    if (cmd == "ARM") {
        if (estadoAtual != IDLE) { Serial.println("[ERRO] Estado inválido para ARM."); return; }
        habilitarMotorT();
        motorTensao.moveTo(passosSelecionados);
        estadoAtual = TENSIONING;
        printEstado();
        Serial.printf("[SIM] Tensionando %d passos...\n", passosSelecionados);
        return;
    }

    // FIRE
    if (cmd == "FIRE") {
        if (estadoAtual != ARMED) { Serial.println("[ERRO] Catapulta não está ARMADA."); return; }
        habilitarMotorD();
        motorDisparo.move(DISPARO_PASSOS);
        tempoDisparo   = millis();
        disparoCompleto = false;
        estadoAtual    = FIRING;
        printEstado();
        Serial.printf("[SIM] Motor de disparo acionado (%d passos).\n", DISPARO_PASSOS);
        return;
    }

    // ABORT
    if (cmd == "ABORT") {
        autoDisparar = false;
        Serial.println("[SIM] ABORT solicitado.");
        motorDisparo.stop();
        motorDisparo.setCurrentPosition(0);
        desabilitarMotorD();
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
        int bat = (int)((analogRead(PIN_ADC) / 4095.0f) * 100.0f);
        Serial.println("─────────────────────────────────────");
        Serial.printf("  Estado:          %s\n",   nomeEstado());
        Serial.printf("  Distância alvo:  %.2f m\n", distanciaAlvo);
        Serial.printf("  Passos:          %d\n",   passosSelecionados);
        Serial.printf("  Pos. tensão:     %ld\n",  motorTensao.currentPosition());
        Serial.printf("  Pos. disparo:    %ld\n",  motorDisparo.currentPosition());
        Serial.printf("  Bateria:         %d%%\n", bat);
        Serial.println("─────────────────────────────────────");
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
        Serial.printf("[CAL] %.2fm → %d passos (idx %d)\n", dist, novosPassos, idx);
        return;
    }

    // TABELA
    if (cmd == "TABELA") {
        Serial.println("─────────────────────────────────────");
        Serial.println("  LOOKUP TABLE:");
        for (int i = 0; i < TABLE_SIZE; i++) {
            float d = DIST_MIN_M + i * DIST_STEP_M;
            Serial.printf("  [%02d] %.2f m → %4d passos\n", i, d, stepsTabela[i]);
        }
        Serial.println("─────────────────────────────────────");
        return;
    }

    Serial.println("[ERRO] Comandos: SET:X.XX  ARM  FIRE  ABORT  HOME  STATUS  CAL:X.XX:NNN  TABELA");
}

// ─── Setup ────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(9600);
    delay(500);

    Serial.println("\n====================================================");
    Serial.println("  HERCULES I — SIMULAÇÃO WOKWI v1.2");
    Serial.println("  2x A4988 + AccelStepper | Equipe A2 / IME 2026.1");
    Serial.println("====================================================");
    Serial.println("  LAUNCH:X.XX  SET:X.XX  ARM  FIRE  ABORT  HOME  STATUS  TABELA");
    Serial.println("====================================================\n");

    pinMode(PIN_T_ENABLE, OUTPUT);
    pinMode(PIN_D_ENABLE, OUTPUT);
    pinMode(PIN_ENDSTOP,  INPUT_PULLUP);
    pinMode(PIN_LED,      OUTPUT);

    desabilitarMotorT();
    desabilitarMotorD();
    digitalWrite(PIN_LED, LOW);

    motorTensao.setMaxSpeed(T_VELOCIDADE_MAX);
    motorTensao.setAcceleration(T_ACELERACAO);
    motorTensao.setCurrentPosition(0);

    motorDisparo.setMaxSpeed(DISPARO_VELOCIDADE);
    motorDisparo.setAcceleration(DISPARO_ACELERACAO);
    motorDisparo.setCurrentPosition(0);

    analogReadResolution(12);

    Serial.println("[INIT] Pronto. Homing inicial ignorado na simulação.");
    Serial.println("[INIT] Use HOME para testar o fim de curso.");
    Serial.print("\n> ");
}

// ─── Loop principal ────────────────────────────────────────────────────────────

void loop() {
    unsigned long agora = millis();

    atualizarLED();

    // Timeout de segurança em ARMED (30s)
    if (estadoAtual == ARMED && agora - tempoEntradaARMED >= 30000) {
        Serial.println("\n[TIMEOUT] 30s em ARMED — ABORT automático.");
        motorDisparo.stop();
        motorDisparo.setCurrentPosition(0);
        desabilitarMotorD();
        iniciarRetorno();
    }

    // ── Motor de tensionamento ────────────────────────────────────────────────
    if (estadoAtual == TENSIONING || estadoAtual == RETURNING) {
        if (motorTensao.distanceToGo() != 0) {
            motorTensao.run();
        } else {
            if (estadoAtual == TENSIONING) {
                // Motor permanece habilitado para evitar deriva pelo elástico
                estadoAtual = ARMED;
                tempoEntradaARMED = agora;
                printEstado();
                Serial.println("[SIM] ARMADO.");
                if (autoDisparar) {
                    autoDisparar = false;
                    habilitarMotorD();
                    motorDisparo.move(DISPARO_PASSOS);
                    tempoDisparo    = agora;
                    disparoCompleto = false;
                    estadoAtual     = FIRING;
                    printEstado();
                    Serial.println("[SIM] Disparo automático acionado.");
                }
            } else {
                desabilitarMotorT();
                estadoAtual = IDLE;
                printEstado();
                Serial.println("[SIM] Ciclo completo. Pronto para novo lançamento.");
                Serial.print("\n> ");
            }
        }
    }

    // ── Motor de disparo ──────────────────────────────────────────────────────
    if (estadoAtual == FIRING) {
        if (motorDisparo.distanceToGo() != 0) {
            motorDisparo.run();
        } else if (!disparoCompleto) {
            // Rotação completa — aguarda delay antes de retornar
            disparoCompleto = true;
            tempoDisparo    = agora;
            Serial.println("[SIM] Motor de disparo completou rotação.");
        } else if (agora - tempoDisparo >= DISPARO_DELAY_MS) {
            // Retorna o motor de disparo ao zero
            motorDisparo.moveTo(0);
            while (motorDisparo.distanceToGo() != 0) motorDisparo.run();
            desabilitarMotorD();
            Serial.println("[SIM] DISPARADO! Iniciando retorno do tensionamento.");
            iniciarRetorno();
        }
    }

    // ── Leitura de comandos Serial ────────────────────────────────────────────
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (bufferSerial.length() > 0) {
                bufferSerial.trim();
                processarComando(bufferSerial);
                bufferSerial = "";
                if (estadoAtual == IDLE) Serial.print("\n> ");
            }
        } else {
            bufferSerial += c;
            Serial.print(c);
        }
    }
}
