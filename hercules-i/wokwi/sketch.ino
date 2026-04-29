/**
 * Hercules I - Simulacao Wokwi v2.1.0
 * Equipe A2 / IPE I / IME 2026.1
 *
 * ATENCAO:
 *   O firmware real usa 28BYJ-48 + ULN2003 em 4 fios.
 *   Esta simulacao usa drivers STEP/DIR apenas como adaptador visual do Wokwi,
 *   porque o componente visual de motor de passo volta ao zero corretamente
 *   nesse modo. A FSM e o protocolo de comandos sao os mesmos do firmware real.
 */

#include <AccelStepper.h>

// Pinos STEP/DIR usados somente na simulacao visual Wokwi.
#define PIN_T_STEP 26
#define PIN_T_DIR  27
#define PIN_D_STEP 18
#define PIN_D_DIR  19

#define PIN_LED 2

#define T_VELOCIDADE_MAX  600.0f
#define T_ACELERACAO      250.0f
#define D_VELOCIDADE_MAX  700.0f
#define D_ACELERACAO      300.0f
#define DISPARO_PASSOS    512
#define DISPARO_DELAY_MS 1000
#define LOCK_SETTLE_MS   1000

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

enum Estado { IDLE, TENSIONING, LOCK_SETTLING, RETURNING, ARMED, FIRING };
Estado estadoAtual = IDLE;

float distanciaAlvo = 1.00f;
int passosSelecionados = 255;
bool autoDisparar = false;
bool retornoDisparoIniciado = false;
bool armarAposRetorno = false;
bool timeoutArmadoAvisado = false;
bool estadoLED = false;

unsigned long tempoEntradaARMED = 0;
unsigned long tempoDisparo = 0;
unsigned long tempoTrava = 0;
unsigned long ultimoBlink = 0;

AccelStepper motorTensao(AccelStepper::DRIVER, PIN_T_STEP, PIN_T_DIR);
AccelStepper motorDisparo(AccelStepper::DRIVER, PIN_D_STEP, PIN_D_DIR);
String bufferSerial = "";

const char* nomeEstado() {
    static const char* nomes[] = {"IDLE", "TENSIONING", "LOCK_SETTLING", "RETURNING", "ARMED", "FIRING"};
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

    if (estadoAtual == LOCK_SETTLING) {
        digitalWrite(PIN_LED, HIGH);
        estadoLED = true;
        return;
    }

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

void iniciarRetornoTensionamento(bool prepararDisparo) {
    armarAposRetorno = prepararDisparo;
    motorTensao.enableOutputs();
    motorTensao.moveTo(0);
    estadoAtual = RETURNING;
    printEstado();
    Serial.printf("[SIM] Retornando motor 1 para zero. Posicao atual: %ld\n", motorTensao.currentPosition());
}

void zerarPosicaoManual() {
    motorTensao.stop();
    motorDisparo.stop();
    motorTensao.setCurrentPosition(0);
    motorDisparo.setCurrentPosition(0);
    autoDisparar = false;
    retornoDisparoIniciado = false;
    armarAposRetorno = false;
    timeoutArmadoAvisado = false;
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
    Serial.printf("[SIM] Motor 2 liberando trava (%d passos).\n", DISPARO_PASSOS);
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
        Serial.printf("[SIM] LAUNCH %.2fm -> motor 1 tensiona %d passos\n", dist, passosSelecionados);
        return;
    }

    if (cmd == "ARM") {
        if (estadoAtual != IDLE) { Serial.println("[ERRO] Estado invalido para ARM."); return; }
        motorTensao.enableOutputs();
        motorTensao.moveTo(passosSelecionados);
        estadoAtual = TENSIONING;
        printEstado();
        Serial.printf("[SIM] Motor 1 tensionando %d passos.\n", passosSelecionados);
        return;
    }

    if (cmd == "FIRE") {
        if (estadoAtual != ARMED) { Serial.println("[ERRO] Catapulta nao esta ARMADA."); return; }
        iniciarDisparo();
        return;
    }

    if (cmd == "ABORT") {
        autoDisparar = false;
        armarAposRetorno = false;
        timeoutArmadoAvisado = false;
        if (estadoAtual == ARMED) {
            Serial.println("[SIM] ABORT: sistema travado pela engrenagem. Use FIRE ou libere manualmente.");
            return;
        }
        if (estadoAtual == FIRING) {
            Serial.println("[SIM] ABORT: disparo ja liberado; aguardando motor 2 voltar ao zero.");
            return;
        }
        retornoDisparoIniciado = false;
        motorDisparo.stop();
        motorDisparo.moveTo(0);
        Serial.println("[SIM] ABORT solicitado.");
        iniciarRetornoTensionamento(false);
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

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);
    estadoLED = true;
    ultimoBlink = millis();

    Serial.println("\n====================================================");
    Serial.println("  HERCULES I - SIMULACAO WOKWI v2.1.0");
    Serial.println("  FSM real + drivers STEP/DIR visuais para Wokwi");
    Serial.println("====================================================");
    Serial.println("  Ciclo: motor 1 tensiona -> trava -> motor 1 volta -> motor 2 libera.");
    Serial.println("  LAUNCH:X.XX  SET:X.XX  ARM  FIRE  ABORT  HOME  STATUS  TABELA");
    Serial.println("====================================================\n");

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
        if (!timeoutArmadoAvisado) {
            Serial.println("\n[TIMEOUT] 30s em ARMED - sistema segue travado pela engrenagem.");
            Serial.println("[SIM] Use FIRE ou libere manualmente.");
            timeoutArmadoAvisado = true;
        }
    }

    if (estadoAtual == TENSIONING || estadoAtual == RETURNING) {
        if (motorTensao.distanceToGo() != 0) {
            motorTensao.run();
        } else if (estadoAtual == TENSIONING) {
            printEstado();
            Serial.printf("[SIM] TRAVADO pela engrenagem. Aguardando %d ms antes do retorno.\n", LOCK_SETTLE_MS);
            tempoTrava = agora;
            estadoAtual = LOCK_SETTLING;
        } else {
            motorTensao.disableOutputs();
            if (armarAposRetorno) {
                armarAposRetorno = false;
                estadoAtual = ARMED;
                tempoEntradaARMED = agora;
                timeoutArmadoAvisado = false;
                printEstado();
                Serial.println("[SIM] ARMADO: motor 1 esta em zero e a engrenagem segura a carga.");
                if (autoDisparar) {
                    autoDisparar = false;
                    iniciarDisparo();
                }
            } else {
                estadoAtual = IDLE;
                printEstado();
                Serial.println("[SIM] Retorno concluido. Pronto para novo comando.");
                Serial.print("\n> ");
            }
        }
    }

    if (estadoAtual == LOCK_SETTLING && agora - tempoTrava >= LOCK_SETTLE_MS) {
        Serial.println("[SIM] Pausa concluida. Motor 1 voltando ao zero.");
        iniciarRetornoTensionamento(true);
    }

    if (estadoAtual == FIRING) {
        if (motorDisparo.distanceToGo() != 0) {
            motorDisparo.run();
        } else if (!retornoDisparoIniciado) {
            if (agora - tempoDisparo >= DISPARO_DELAY_MS) {
                motorDisparo.moveTo(0);
                retornoDisparoIniciado = true;
                Serial.println("[SIM] Motor 2 liberou a trava. Pausa concluida; voltando ao zero.");
            }
        } else {
            motorDisparo.disableOutputs();
            estadoAtual = IDLE;
            Serial.println("[SIM] DISPARADO! Ambos motores em zero. Ciclo completo.");
            Serial.print("\n> ");
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
