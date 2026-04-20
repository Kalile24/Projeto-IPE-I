/**
 * Projeto Hércules I — Equipe A2
 * IPE I — Instituto Militar de Engenharia — 2026.1
 *
 * Arquivo: src/components/StatusBar.js
 * Descrição: Barra de status da conexão BLE.
 * Autor: Kalile (Gerente / Firmware)
 * Versão: 1.0.0
 * Data: Abril/2026
 */

import React from 'react';
import { View, Text, TouchableOpacity, StyleSheet } from 'react-native';

/**
 * StatusBar
 * Props:
 *   conectado    {boolean}  — true se BLE conectado
 *   nomeDispositivo {string} — nome do ESP32
 *   onPressConectar {Function} — callback ao pressionar "Conectar"
 */
export default function StatusBar({ conectado, nomeDispositivo, onPressConectar }) {
  return (
    <View style={styles.container}>
      {/* Indicador de estado */}
      <View style={styles.statusRow}>
        <View style={[styles.ledIndicador, conectado ? styles.ledVerde : styles.ledVermelho]} />
        <Text style={[styles.textoStatus, conectado ? styles.textoConectado : styles.textoDesconectado]}>
          {conectado ? `● CONECTADO` : '○ DESCONECTADO'}
        </Text>
        {conectado && nomeDispositivo && (
          <Text style={styles.nomeDispositivo}> — {nomeDispositivo}</Text>
        )}
      </View>

      {/* Botão de conexão quando desconectado */}
      {!conectado && onPressConectar && (
        <TouchableOpacity style={styles.botaoConectar} onPress={onPressConectar}>
          <Text style={styles.textoBotao}>CONECTAR</Text>
        </TouchableOpacity>
      )}
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    backgroundColor: '#1a1a2e',
    paddingHorizontal: 12,
    paddingVertical: 8,
    borderWidth: 1,
    borderColor: '#2a2a4e',
  },
  statusRow: {
    flexDirection: 'row',
    alignItems: 'center',
  },
  ledIndicador: {
    width: 8,
    height: 8,
    borderRadius: 4,
    marginRight: 6,
  },
  ledVerde: {
    backgroundColor: '#00ff88',
    shadowColor: '#00ff88',
    shadowRadius: 4,
    shadowOpacity: 0.8,
  },
  ledVermelho: {
    backgroundColor: '#ff3333',
  },
  textoStatus: {
    fontFamily: 'monospace',
    fontSize: 12,
    fontWeight: 'bold',
    letterSpacing: 1,
  },
  textoConectado: {
    color: '#00ff88',
  },
  textoDesconectado: {
    color: '#ff3333',
  },
  nomeDispositivo: {
    color: '#888',
    fontSize: 11,
    fontFamily: 'monospace',
  },
  botaoConectar: {
    backgroundColor: '#1a3a5c',
    paddingHorizontal: 12,
    paddingVertical: 4,
    borderWidth: 1,
    borderColor: '#ffbf00',
  },
  textoBotao: {
    color: '#ffbf00',
    fontSize: 11,
    fontFamily: 'monospace',
    fontWeight: 'bold',
    letterSpacing: 2,
  },
});
