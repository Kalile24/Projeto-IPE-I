/**
 * Hércules I — Tabela de calibração
 * Mapeamento: distância (m) → passos do motor de tensionamento
 *
 * COMO ATUALIZAR:
 *   Script (recomendado): python calibration/calibrar.py --input dados.csv --output lookup_table.h
 *   Em campo (sem recompilar): envie "CAL:X.XX:NNN" por Serial ou Bluetooth
 *   Manual: edite STEPS_TABLE[] abaixo e recompile
 *
 * Índice 0 = 0,50 m | Índice 14 = 4,00 m | Passo = 0,25 m por índice
 * ATENÇÃO: valores abaixo são placeholder — substitua após calibração real.
 */

#ifndef LOOKUP_TABLE_H
#define LOOKUP_TABLE_H

#define TABLE_SIZE  15
#define DIST_MIN_M  0.50f
#define DIST_MAX_M  4.00f
#define DIST_STEP_M 0.25f

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
