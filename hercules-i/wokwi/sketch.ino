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

enum Estado { IDLE, TENSIONING, RETURNING, ARMED, FIRING };
Estado estadoAtual = IDLE;

float distanciaAlvo = 1.00f;
int passosSelecionados = 255;
bool autoDisparar = false;
bool retornoDisparoIniciado = false;
bool estadoLED = false;
bool armarAposRetorno = false;
bool timeoutArmadoAvisado = false;

unsigned long tempoEntradaARMED = 0;
unsigned long tempoDisparo = 0;
unsigned long ultimoBlink = 0;

// No Wokwi, FULL4WIRE faz o display "steps" bater 1:1 com os passos logicos.
// O sinal e invertido porque a ordem visual das bobinas gira no sentido oposto.
AccelStepper motorTensao(AccelStepper::FULL4WIRE, PIN_T_IN1, PIN_T_IN3, PIN_T_IN2, PIN_T_IN4);
AccelStepper motorDisparo(AccelStepper::FULL4WIRE, PIN_D_IN1, PIN_D_IN3, PIN_D_IN2, PIN_D_IN4);
String bufferSerial = "";

long alvoFisicoWokwi(long passosLogicos) {
    return -passosLogicos;
}

long posicaoLogicaTensao() {
    return -motorTensao.currentPosition();
}

long posicaoLogicaDisparo() {
    return -motorDisparo.currentPosition();
}

const char* nomeEstado() {
    static const char* nomes[] = {"IDLE", "TENSIONING", "RETURNING", "ARMED", "FIRING"};
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

void iniciarRetornoTensionamento(bool prepararDisparo) {
    armarAposRetorno = prepararDisparo;
    motorTensao.enableOutputs();
    motorTensao.moveTo(alvoFisicoWokwi(0));
    estadoAtual = RETURNING;
    printEstado();
    Serial.printf("[SIM] Retornando tensionamento para zero. Posicao logica atual: %ld\n", posicaoLogicaTensao());
}

void zerarPosicaoManual() {
    motorTensao.stop();
    motorDisparo.stop();
    motorTensao.setCurrentPosition(alvoFisicoWokwi(0));
    motorDisparo.setCurrentPosition(alvoFisicoWokwi(0));
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
    motorDisparo.setCurrentPosition(alvoFisicoWokwi(0));
    motorDisparo.moveTo(alvoFisicoWokwi(DISPARO_PASSOS));
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
        motorTensao.moveTo(alvoFisicoWokwi(passosSelecionados));
        estadoAtual = TENSIONING;
        printEstado();
        Serial.printf("[SIM] LAUNCH %.2fm -> %d passos\n", dist, passosSelecionados);
        return;
    }

    if (cmd == "ARM") {
        if (estadoAtual != IDLE) { Serial.println("[ERRO] Estado invalido para ARM."); return; }
        motorTensao.enableOutputs();
        motorTensao.moveTo(alvoFisicoWokwi(passosSelecionados));
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
        motorDisparo.moveTo(alvoFisicoWokwi(0));
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
                      posicaoLogicaTensao(),
                      posicaoLogicaDisparo());
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
    Serial.println("  HERCULES I - SIMULACAO WOKWI v2.0.0");
    Serial.println("  2x 28BYJ-48 + ULN2003 | Equipe A2 / IME 2026.1");
    Serial.println("====================================================");
    Serial.println("  Antes de iniciar: posicione o mecanismo no zero e envie HOME.");
    Serial.println("  LAUNCH:X.XX  SET:X.XX  ARM  FIRE  ABORT  HOME  STATUS  TABELA");
    Serial.println("====================================================\n");

    motorTensao.setMaxSpeed(T_VELOCIDADE_MAX);
    motorTensao.setAcceleration(T_ACELERACAO);
    motorTensao.setCurrentPosition(alvoFisicoWokwi(0));

    motorDisparo.setMaxSpeed(D_VELOCIDADE_MAX);
    motorDisparo.setAcceleration(D_ACELERACAO);
    motorDisparo.setCurrentPosition(alvoFisicoWokwi(0));

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
            Serial.println("[SIM] TRAVADO pela engrenagem. Retornando motor de tensionamento ao zero.");
            iniciarRetornoTensionamento(true);
        } else {
            motorTensao.disableOutputs();
            if (armarAposRetorno) {
                armarAposRetorno = false;
                estadoAtual = ARMED;
                tempoEntradaARMED = agora;
                timeoutArmadoAvisado = false;
                printEstado();
                Serial.println("[SIM] ARMADO: tensionamento voltou a zero e a engrenagem segura a carga.");
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

    if (estadoAtual == FIRING) {
        if (motorDisparo.distanceToGo() != 0) {
            motorDisparo.run();
        } else if (!retornoDisparoIniciado) {
            if (agora - tempoDisparo >= DISPARO_DELAY_MS) {
                motorDisparo.moveTo(alvoFisicoWokwi(0));
                retornoDisparoIniciado = true;
            }
        } else {
            motorDisparo.disableOutputs();
            estadoAtual = IDLE;
            Serial.println("[SIM] DISPARADO! Motor de disparo voltou ao zero. Ciclo completo.");
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
