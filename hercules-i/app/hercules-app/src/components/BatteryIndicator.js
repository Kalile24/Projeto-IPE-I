/**
 * Projeto Hércules I — Equipe A2
 * IPE I — Instituto Militar de Engenharia — 2026.1
 *
 * Arquivo: src/components/BatteryIndicator.js
 * Descrição: Componente visual de nível de bateria.
 * Autor: Kalile (Gerente / Firmware)
 * Versão: 1.0.0
 * Data: Abril/2026
 */

import React from 'react';
import { View, Text, StyleSheet } from 'react-native';

/**
 * Retorna a cor do indicador conforme o percentual da bateria.
 */
function corBateria(percentual) {
  if (percentual > 60) return '#00ff88';   // Verde
  if (percentual > 30) return '#ffbf00';   // Âmbar
  return '#ff3333';                        // Vermelho
}

/**
 * BatteryIndicator
 * Props:
 *   percentual {number} — 0 a 100
 *   tensao     {string} — Ex: "8.4V" (opcional)
 */
export default function BatteryIndicator({ percentual = 0, tensao = null }) {
  const cor = corBateria(percentual);
  const larguraBarra = `${Math.max(0, Math.min(100, percentual))}%`;

  return (
    <View style={styles.container}>
      {/* Ícone de bateria */}
      <View style={styles.batteryCasing}>
        <View style={[styles.batteryFill, { width: larguraBarra, backgroundColor: cor }]} />
        <View style={styles.batteryPolo} />
      </View>

      {/* Percentual e tensão */}
      <View style={styles.textoContainer}>
        <Text style={[styles.percentual, { color: cor }]}>
          {percentual}%
        </Text>
        {tensao && (
          <Text style={styles.tensao}>{tensao}</Text>
        )}
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 6,
  },
  batteryCasing: {
    width: 36,
    height: 16,
    borderWidth: 1.5,
    borderColor: '#aaa',
    borderRadius: 2,
    flexDirection: 'row',
    alignItems: 'center',
    overflow: 'hidden',
    position: 'relative',
  },
  batteryFill: {
    height: '100%',
    borderRadius: 1,
  },
  batteryPolo: {
    position: 'absolute',
    right: -5,
    width: 4,
    height: 8,
    backgroundColor: '#aaa',
    borderRadius: 1,
  },
  textoContainer: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 4,
  },
  percentual: {
    fontSize: 12,
    fontFamily: 'monospace',
    fontWeight: 'bold',
  },
  tensao: {
    fontSize: 10,
    color: '#888',
    fontFamily: 'monospace',
  },
});
