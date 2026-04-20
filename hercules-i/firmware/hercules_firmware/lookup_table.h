/**
 * Projeto Hércules I — Equipe A2
 * IPE I — Instituto Militar de Engenharia — 2026.1
 *
 * Arquivo: lookup_table.h
 * Descrição: Tabela de calibração — mapeamento distância (m) → passos do motor.
 *            Este arquivo pode ser gerado automaticamente pelo script calibrar.py
 *            ou atualizado em campo via comando BLE "CAL:X.XX:NNN".
 * Autor: Kalile (Gerente / Firmware)
 * Versão: 1.0.0
 * Data: Abril/2026
 */

#ifndef LOOKUP_TABLE_H
#define LOOKUP_TABLE_H

// === CALIBRAÇÃO ============================================================
//
// COMO ATUALIZAR ESTA TABELA:
//
// OPÇÃO 1 — Via script Python (recomendado após testes):
//   python calibration/calibrar.py --input dados_teste.csv --output lookup_table.h
//   Copie o arquivo gerado para esta pasta e recompile o firmware.
//
// OPÇÃO 2 — Via comando BLE em campo (sem recompilar):
//   Envie o comando: CAL:X.XX:NNN
//   Exemplo: CAL:1.50:395   → define 1,50 m = 395 passos
//   O valor é atualizado em RAM (persiste até reinicialização).
//
// OPÇÃO 3 — Edição manual deste arquivo:
//   Altere os valores de STEPS_TABLE[] abaixo e recompile.
//   Índice 0 = 0,50 m, índice 1 = 0,75 m, ..., índice 14 = 4,00 m
//   Incremento de 0,25 m por índice.
//
// ===========================================================================

// Número de entradas na tabela (0,50 m a 4,00 m, passo de 0,25 m)
#define TABLE_SIZE 15

// Distância mínima e máxima suportadas (em metros)
#define DIST_MIN_M   0.50f
#define DIST_MAX_M   4.00f
#define DIST_STEP_M  0.25f

// Distâncias correspondentes a cada índice (apenas para referência/documentação)
const float DISTANCES_TABLE[TABLE_SIZE] = {
    0.50f,  // índice  0
    0.75f,  // índice  1
    1.00f,  // índice  2
    1.25f,  // índice  3
    1.50f,  // índice  4
    1.75f,  // índice  5
    2.00f,  // índice  6
    2.25f,  // índice  7
    2.50f,  // índice  8
    2.75f,  // índice  9
    3.00f,  // índice 10
    3.25f,  // índice 11
    3.50f,  // índice 12
    3.75f,  // índice 13
    4.00f   // índice 14
};

// Número de passos do motor para cada distância.
// ATENÇÃO: valores placeholder — substituir após calibração real.
// Gerado em: 20/04/2026 (placeholder inicial)
const int STEPS_TABLE[TABLE_SIZE] = {
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

#endif // LOOKUP_TABLE_H
