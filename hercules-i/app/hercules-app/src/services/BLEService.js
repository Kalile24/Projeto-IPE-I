/**
 * Projeto Hércules I — Equipe A2
 * IPE I — Instituto Militar de Engenharia — 2026.1
 *
 * Arquivo: src/services/BLEService.js
 * Descrição: Serviço singleton de comunicação BLE com o ESP32.
 *            Encapsula toda a lógica de scan, conexão, escrita e notificações.
 * Autor: Kalile (Gerente / Firmware)
 * Versão: 1.0.0
 * Data: Abril/2026
 */

import { BleManager, State } from 'react-native-ble-plx';
import { encode as btoa } from 'base-64';
import {
  DEVICE_NAME,
  SERVICE_UUID,
  CMD_CHAR_UUID,
  STATUS_CHAR_UUID,
  SCAN_TIMEOUT_MS,
} from '../constants/ble';

class BLEService {
  constructor() {
    // Instância única do gerenciador BLE
    this._manager       = new BleManager();
    this._device        = null;       // Dispositivo conectado
    this._connected     = false;
    this._statusSub     = null;       // Subscription de notificações
    this._disconnectSub = null;       // Subscription de desconexão
    this._onDisconnectCb = null;      // Callback externo de desconexão
  }

  /**
   * Aguarda o Bluetooth estar ligado no dispositivo.
   * Rejeita após 15 segundos se o BT não estiver disponível.
   */
  _aguardarBluetooth() {
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        reject(new Error('Bluetooth não disponível após 15s.'));
      }, 15000);

      this._manager.onStateChange((state) => {
        if (state === State.PoweredOn) {
          clearTimeout(timeout);
          resolve();
        }
      }, true);
    });
  }

  /**
   * Escaneia dispositivos BLE por SCAN_TIMEOUT_MS milissegundos.
   * Chama onDeviceFound para cada dispositivo encontrado.
   * Filtra automaticamente pelo nome "Hercules-I".
   *
   * @param {Function} onDeviceFound - Callback (device) => void
   * @param {number}   timeout       - Duração do scan em ms (padrão: SCAN_TIMEOUT_MS)
   */
  async startScan(onDeviceFound, timeout = SCAN_TIMEOUT_MS) {
    await this._aguardarBluetooth();

    const dispositivosVistos = new Set();

    this._manager.startDeviceScan(null, { allowDuplicates: false }, (erro, device) => {
      if (erro) {
        console.error('[BLE] Erro no scan:', erro.message);
        return;
      }

      if (!device || !device.name) return;

      // Filtra pelo nome do dispositivo Hércules
      if (device.name !== DEVICE_NAME) return;

      // Evita duplicatas no callback
      if (dispositivosVistos.has(device.id)) return;
      dispositivosVistos.add(device.id);

      console.log(`[BLE] Dispositivo encontrado: ${device.name} (${device.id}) RSSI:${device.rssi}`);
      onDeviceFound(device);
    });

    // Para o scan após o timeout
    setTimeout(() => {
      this._manager.stopDeviceScan();
      console.log('[BLE] Scan finalizado.');
    }, timeout);
  }

  /**
   * Para o scan BLE manualmente.
   */
  stopScan() {
    this._manager.stopDeviceScan();
  }

  /**
   * Conecta ao dispositivo ESP32, descobre serviços e características.
   *
   * @param {string}   deviceId      - ID do dispositivo BLE
   * @param {Function} onDisconnect  - Callback chamado ao desconectar
   * @returns {Promise<Device>}
   */
  async connect(deviceId, onDisconnect = null) {
    try {
      console.log(`[BLE] Conectando a: ${deviceId}`);
      this._onDisconnectCb = onDisconnect;

      // Conecta com timeout de 10 segundos
      this._device = await this._manager.connectToDevice(deviceId, {
        timeout: 10000,
        autoConnect: false,
      });

      // Descobre todos os serviços e características
      await this._device.discoverAllServicesAndCharacteristics();
      this._connected = true;

      console.log('[BLE] Conectado e serviços descobertos.');

      // Monitora desconexão inesperada
      this._disconnectSub = this._device.onDisconnected((erro, dev) => {
        console.log('[BLE] Dispositivo desconectado.');
        this._connected = false;
        this._device    = null;
        if (this._statusSub) {
          this._statusSub.remove();
          this._statusSub = null;
        }
        if (this._onDisconnectCb) this._onDisconnectCb();
      });

      return this._device;
    } catch (erro) {
      this._connected = false;
      throw new Error(`Falha na conexão: ${erro.message}`);
    }
  }

  /**
   * Desconecta do dispositivo ESP32 e limpa recursos.
   */
  async disconnect() {
    try {
      if (this._statusSub) {
        this._statusSub.remove();
        this._statusSub = null;
      }
      if (this._disconnectSub) {
        this._disconnectSub.remove();
        this._disconnectSub = null;
      }
      if (this._device) {
        await this._device.cancelConnection();
        this._device = null;
      }
      this._connected = false;
      console.log('[BLE] Desconectado.');
    } catch (erro) {
      console.warn('[BLE] Erro ao desconectar:', erro.message);
    }
  }

  /**
   * Envia um comando string para a characteristic de escrita do ESP32.
   *
   * @param {string} comando - Ex: "SET:1.50", "ARM", "FIRE"
   */
  async sendCommand(comando) {
    if (!this._connected || !this._device) {
      throw new Error('BLE não conectado.');
    }
    try {
      // Codifica string em Base64 (requisito da biblioteca react-native-ble-plx)
      const base64Cmd = btoa(comando);
      await this._device.writeCharacteristicWithResponseForService(
        SERVICE_UUID,
        CMD_CHAR_UUID,
        base64Cmd
      );
      console.log(`[BLE TX] Enviado: ${comando}`);
    } catch (erro) {
      throw new Error(`Falha ao enviar comando "${comando}": ${erro.message}`);
    }
  }

  /**
   * Inscreve para receber notificações da characteristic de status.
   * Cada notificação chama o callback com a string decodificada.
   *
   * @param {Function} callback - (mensagem: string) => void
   */
  subscribeToStatus(callback) {
    if (!this._connected || !this._device) {
      throw new Error('BLE não conectado.');
    }

    // Cancela subscription anterior se houver
    if (this._statusSub) {
      this._statusSub.remove();
    }

    this._statusSub = this._device.monitorCharacteristicForService(
      SERVICE_UUID,
      STATUS_CHAR_UUID,
      (erro, characteristic) => {
        if (erro) {
          // Ignora erro de desconexão (código -1 é normal ao desconectar)
          if (erro.errorCode !== -1) {
            console.warn('[BLE] Erro na notificação:', erro.message);
          }
          return;
        }
        if (characteristic?.value) {
          // Decodifica Base64
          const mensagem = atob(characteristic.value);
          console.log(`[BLE RX] ${mensagem}`);
          callback(mensagem);
        }
      }
    );
  }

  /**
   * Retorna true se há um cliente BLE conectado.
   */
  isConnected() {
    return this._connected && this._device !== null;
  }

  /**
   * Retorna o nome do dispositivo conectado (ou null).
   */
  getDeviceName() {
    return this._device?.name ?? null;
  }
}

// Exporta instância singleton
export default new BLEService();
