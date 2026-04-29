/**
 * Hercules I - Simulacao Wokwi v2.0.0
 * Equipe A2 / IPE I / IME 2026.1
 *
 * Simula a logica do firmware real para dois motores 28BYJ-48.
 * No hardware real, cada motor deve ser ligado a um driver ULN2003.
 * No Wokwi, os fios aparecem diretamente no motor de passo para validar
 * comandos, estados e contagem de passos.
 */

#include <AccelStepper.h>

#define PIN_T_IN1 26
#define PIN_T_IN2 27
#define PIN_T_IN3 14
#define PIN_T_IN4 25

#define PIN_D_IN1 18
#define PIN_D_IN2 19
#define PIN_D_IN3 21
#define PIN_D_IN4 22

#define PIN_LED 2

#define T_VELOCIDADE_MAX  600.0f
#define T_ACELERACAO      250.0f
#define D_VELOCIDADE_MAX  700.0f
#define D_ACELERACAO      300.0f
#define DISPARO_PASSOS    512
#define DISPARO_DELAY_MS  300

#define ARMED_TIMEOUT_MS 30000

#define TABLE_SIZE  15
#define DIST_MIN_M  0.50f
#define DIST_MAX_M  4.00f
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

enum Estado { IDLE, TENSIONING, ARMED, FIRING, RETURNING };
Estado estadoAtual = IDLE;

float distanciaAlvo = 1.00f;
int passosSelecionados = 255;
bool autoDisparar = false;
bool retornoDisparoIniciado = false;
bool estadoLED = false;

unsigned long tempoEntradaARMED = 0;
unsigned long tempoDisparo = 0;
unsigned long ultimoBlink = 0;

AccelStepper motorTensao(AccelStepper::HALF4WIRE, PIN_T_IN1, PIN_T_IN3, PIN_T_IN2, PIN_T_IN4);
AccelStepper motorDisparo(AccelStepper::HALF4WIRE, PIN_D_IN1, PIN_D_IN3, PIN_D_IN2, PIN_D_IN4);
String bufferSerial = "";

const char* nomeEstado() {
    static const char* nomes[] = {"IDLE", "TENSIONING", "ARMED", "FIRING", "RETURNING"};
    return nomes[estadoAtual];
}

void printEstado() {
    Serial.print("[FSM] ");
    Serial.println(nomeEstado());
}

int distanciaParaIndice(float dist) {
    if (dist < DIST_MIN_M || dist > DIST_MAX_M) return -1;
    int idx = (int)round((dist - DIST_MIN_M) / DIST_STEP_M);
    if (idx < 0) idx = 0;
    if (idx >= TABLE_SIZE) idx = TABLE_SIZE - 1;
    return idx;
}

void atualizarLED() {
    unsigned long agora = millis();
    unsigned long intervalo = 500;

    if (estadoAtual == TENSIONING) intervalo = 100;
    if (estadoAtual == FIRING || estadoAtual == RETURNING) intervalo = 50;

    if (estadoAtual == ARMED) {
        digitalWrite(PIN_LED, HIGH);
        estadoLED = true;
        return;
    }

    if (agora - ultimoBlink >= intervalo) {
        estadoLED = !estadoLED;
        digitalWrite(PIN_LED, estadoLED);
        ultimoBlink = agora;
    }
}

void desenergizarMotores() {
    motorTensao.disableOutputs();
    motorDisparo.disableOutputs();
}

void iniciarRetorno() {
    motorTensao.enableOutputs();
    motorTensao.moveTo(0);
    estadoAtual = RETURNING;
    printEstado();
    Serial.printf("[SIM] Retornando tensionamento para zero. Posicao atual: %ld\n", motorTensao.currentPosition());
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
    Serial.println("[OK] HOME manual: posicoes zeradas. Garanta que o mecanismo esteja no zero fisico.");
    printEstado();
}

void iniciarDisparo() {
    motorDisparo.enableOutputs();
    motorDisparo.setCurrentPosition(0);
    motorDisparo.moveTo(DISPARO_PASSOS);
    tempoDisparo = millis();
    retornoDisparoIniciado = false;
    estadoAtual = FIRING;
    printEstado();
    Serial.printf("[SIM] Disparo acionado (%d passos).\n", DISPARO_PASSOS);
}

void processarComando(const String& cmd) {
    Serial.println();
    Serial.print("[CMD] ");
    Serial.println(cmd);

    if (cmd.startsWith("SET:")) {
        if (estadoAtual != IDLE) { Serial.println("[ERRO] Sistema ocupado. Envie ABORT primeiro."); return; }
        float dist = cmd.substring(4).toFloat();
        int idx = distanciaParaIndice(dist);
        if (idx < 0) { Serial.println("[ERRO] Distancia fora do range (0.50 a 4.00 m)."); return; }
        distanciaAlvo = dist;
        passosSelecionados = stepsTabela[idx];
        Serial.printf("[OK] %.2f m -> %d passos\n", dist, passosSelecionados);
        return;
    }

    if (cmd.startsWith("LAUNCH:")) {
        if (estadoAtual != IDLE) { Serial.println("[ERRO] Sistema ocupado. Envie ABORT primeiro."); return; }
        float dist = cmd.substring(7).toFloat();
        int idx = distanciaParaIndice(dist);
        if (idx < 0) { Serial.println("[ERRO] Distancia fora do range (0.50 a 4.00 m)."); return; }
        distanciaAlvo = dist;
        passosSelecionados = stepsTabela[idx];
        autoDisparar = true;
        motorTensao.enableOutputs();
        motorTensao.moveTo(passosSelecionados);
        estadoAtual = TENSIONING;
        printEstado();
        Serial.printf("[SIM] LAUNCH %.2fm -> %d passos\n", dist, passosSelecionados);
        return;
    }

    if (cmd == "ARM") {
        if (estadoAtual != IDLE) { Serial.println("[ERRO] Estado invalido para ARM."); return; }
        motorTensao.enableOutputs();
        motorTensao.moveTo(passosSelecionados);
        estadoAtual = TENSIONING;
        printEstado();
        Serial.printf("[SIM] Tensionando %d passos.\n", passosSelecionados);
        return;
    }

    if (cmd == "FIRE") {
        if (estadoAtual != ARMED) { Serial.println("[ERRO] Catapulta nao esta ARMADA."); return; }
        iniciarDisparo();
        return;
    }

    if (cmd == "ABORT") {
        autoDisparar = false;
        retornoDisparoIniciado = false;
        motorDisparo.stop();
        motorDisparo.moveTo(0);
        Serial.println("[SIM] ABORT solicitado.");
        iniciarRetorno();
        return;
    }

    if (cmd == "HOME") {
        zerarPosicaoManual();
        return;
    }

    if (cmd == "STATUS") {
        Serial.printf("STATUS:%s:DIST:%.2fm:POS_T:%ld:POS_D:%ld\n",
                      nomeEstado(), distanciaAlvo,
                      motorTensao.currentPosition(),
                      motorDisparo.currentPosition());
        return;
    }

    if (cmd.startsWith("CAL:")) {
        int p = cmd.indexOf(':', 4);
        if (p < 0) { Serial.println("[ERRO] Formato: CAL:X.XX:NNN"); return; }
        float dist = cmd.substring(4, p).toFloat();
        int novosPassos = cmd.substring(p + 1).toInt();
        int idx = distanciaParaIndice(dist);
        if (idx < 0) { Serial.println("[ERRO] Distancia invalida."); return; }
        if (novosPassos <= 0 || novosPassos > 99999) { Serial.println("[ERRO] Passos invalidos."); return; }
        stepsTabela[idx] = novosPassos;
        Serial.printf("[CAL] %.2fm -> %d passos\n", dist, novosPassos);
        return;
    }

    if (cmd == "TABELA") {
        Serial.println("LOOKUP TABLE:");
        for (int i = 0; i < TABLE_SIZE; i++) {
            float d = DIST_MIN_M + i * DIST_STEP_M;
            Serial.printf("[%02d] %.2f m -> %d passos\n", i, d, stepsTabela[i]);
        }
        return;
    }

    Serial.println("[ERRO] Comandos: SET:X.XX  ARM  FIRE  ABORT  HOME  STATUS  CAL:X.XX:NNN  TABELA");
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n====================================================");
    Serial.println("  HERCULES I - SIMULACAO WOKWI v2.0.0");
    Serial.println("  2x 28BYJ-48 + ULN2003 | Equipe A2 / IME 2026.1");
    Serial.println("====================================================");
    Serial.println("  Antes de iniciar: posicione o mecanismo no zero e envie HOME.");
    Serial.println("  LAUNCH:X.XX  SET:X.XX  ARM  FIRE  ABORT  HOME  STATUS  TABELA");
    Serial.println("====================================================\n");

    pinMode(PIN_LED, OUTPUT);

    motorTensao.setMaxSpeed(T_VELOCIDADE_MAX);
    motorTensao.setAcceleration(T_ACELERACAO);
    motorTensao.setCurrentPosition(0);

    motorDisparo.setMaxSpeed(D_VELOCIDADE_MAX);
    motorDisparo.setAcceleration(D_ACELERACAO);
    motorDisparo.setCurrentPosition(0);

    desenergizarMotores();

    Serial.println("[INIT] Pronto. HOME apenas zera a posicao atual.");
    Serial.print("\n> ");
}

void loop() {
    unsigned long agora = millis();

    atualizarLED();

    if (estadoAtual == ARMED && agora - tempoEntradaARMED >= ARMED_TIMEOUT_MS) {
        Serial.println("\n[TIMEOUT] 30s em ARMED - retorno automatico.");
        autoDisparar = false;
        iniciarRetorno();
    }

    if (estadoAtual == TENSIONING || estadoAtual == RETURNING) {
        if (motorTensao.distanceToGo() != 0) {
            motorTensao.run();
        } else if (estadoAtual == TENSIONING) {
            estadoAtual = ARMED;
            tempoEntradaARMED = agora;
            printEstado();
            Serial.println("[SIM] ARMADO.");
            if (autoDisparar) {
                autoDisparar = false;
                iniciarDisparo();
            }
        } else {
            motorTensao.disableOutputs();
            estadoAtual = IDLE;
            printEstado();
            Serial.println("[SIM] Ciclo completo. Pronto para novo lancamento.");
            Serial.print("\n> ");
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
            Serial.println("[SIM] DISPARADO! Iniciando retorno do tensionamento.");
            iniciarRetorno();
        }
    }

    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            bufferSerial.trim();
            if (bufferSerial.length() > 0) {
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
