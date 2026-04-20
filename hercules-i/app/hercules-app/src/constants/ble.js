/**
 * Projeto Hércules I — Equipe A2
 * IPE I — Instituto Militar de Engenharia — 2026.1
 *
 * Arquivo: src/constants/ble.js
 * Descrição: Constantes BLE — UUIDs e nome do dispositivo.
 *            Devem ser idênticos aos definidos no firmware.
 * Autor: Kalile (Gerente / Firmware)
 * Versão: 1.0.0
 * Data: Abril/2026
 */

// Nome BLE do ESP32 (conforme BLEDevice::init() no firmware)
export const DEVICE_NAME = 'Hercules-I';

// UUID do serviço BLE principal
export const SERVICE_UUID = '12345678-1234-1234-1234-123456789abc';

// Characteristic de comando (Write) — app → ESP32
export const CMD_CHAR_UUID = '12345678-1234-1234-1234-123456789ab1';

// Characteristic de status (Notify) — ESP32 → app
export const STATUS_CHAR_UUID = '12345678-1234-1234-1234-123456789ab2';

// Duração do scan BLE em milissegundos
export const SCAN_TIMEOUT_MS = 10000;

// Distâncias suportadas (em metros)
export const DIST_MIN = 0.5;
export const DIST_MAX = 4.0;
export const DIST_STEP = 0.25;

// Estados da FSM (espelham os estados do firmware)
export const ESTADOS = {
  IDLE:       'IDLE',
  TENSIONING: 'TENSIONING',
  ARMED:      'ARMED',
  FIRING:     'FIRING',
  RETURNING:  'RETURNING',
};

// Botões de distância rápida
export const DISTANCIAS_RAPIDAS = [0.5, 1.0, 2.0, 3.0, 4.0];
