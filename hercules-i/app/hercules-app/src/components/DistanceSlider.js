/**
 * Projeto Hércules I — Equipe A2
 * IPE I — Instituto Militar de Engenharia — 2026.1
 *
 * Arquivo: src/components/DistanceSlider.js
 * Descrição: Slider de seleção de distância de lançamento (0,5 m a 4,0 m).
 * Autor: Kalile (Gerente / Firmware)
 * Versão: 1.0.0
 * Data: Abril/2026
 */

import React from 'react';
import { View, Text, TouchableOpacity, StyleSheet } from 'react-native';
import Slider from '@react-native-community/slider';
import { DIST_MIN, DIST_MAX, DIST_STEP, DISTANCIAS_RAPIDAS } from '../constants/ble';

/**
 * DistanceSlider
 * Props:
 *   valor       {number}   — distância atual selecionada em metros
 *   onChange    {Function} — callback (novoValor: number) => void
 *   habilitado  {boolean}  — se false, desabilita interação
 */
export default function DistanceSlider({ valor, onChange, habilitado = true }) {

  // Arredonda para o múltiplo mais próximo de DIST_STEP
  const arredondar = (v) => {
    return Math.round(v / DIST_STEP) * DIST_STEP;
  };

  return (
    <View style={styles.container}>
      {/* Label e valor atual */}
      <View style={styles.cabecalho}>
        <Text style={styles.label}>DISTÂNCIA ALVO</Text>
        <Text style={styles.valorDestaque}>
          {valor.toFixed(2)} <Text style={styles.unidade}>m</Text>
        </Text>
      </View>

      {/* Slider */}
      <Slider
        style={styles.slider}
        minimumValue={DIST_MIN}
        maximumValue={DIST_MAX}
        step={DIST_STEP}
        value={valor}
        onValueChange={(v) => habilitado && onChange(arredondar(v))}
        minimumTrackTintColor="#ffbf00"
        maximumTrackTintColor="#333"
        thumbTintColor={habilitado ? '#ffbf00' : '#555'}
        disabled={!habilitado}
      />

      {/* Marcadores de limite */}
      <View style={styles.marcadores}>
        <Text style={styles.marcadorTexto}>{DIST_MIN.toFixed(1)}m</Text>
        <Text style={styles.marcadorTexto}>{DIST_MAX.toFixed(1)}m</Text>
      </View>

      {/* Botões de atalho */}
      <View style={styles.botoesRapidos}>
        {DISTANCIAS_RAPIDAS.map((dist) => (
          <TouchableOpacity
            key={dist}
            style={[
              styles.botaoRapido,
              valor === dist && styles.botaoRapidoAtivo,
              !habilitado && styles.botaoRapidoDesabilitado,
            ]}
            onPress={() => habilitado && onChange(dist)}
            disabled={!habilitado}
          >
            <Text style={[
              styles.textoBotaoRapido,
              valor === dist && styles.textoBotaoRapidoAtivo,
            ]}>
              {dist.toFixed(1)}m
            </Text>
          </TouchableOpacity>
        ))}
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    backgroundColor: '#1a1a2e',
    padding: 16,
    borderWidth: 1,
    borderColor: '#2a2a4e',
  },
  cabecalho: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'flex-end',
    marginBottom: 8,
  },
  label: {
    color: '#888',
    fontSize: 11,
    fontFamily: 'monospace',
    letterSpacing: 2,
    textTransform: 'uppercase',
  },
  valorDestaque: {
    color: '#ffbf00',
    fontSize: 36,
    fontFamily: 'monospace',
    fontWeight: 'bold',
    letterSpacing: -1,
  },
  unidade: {
    fontSize: 18,
    color: '#ffbf00',
  },
  slider: {
    width: '100%',
    height: 40,
  },
  marcadores: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    marginTop: -4,
    marginBottom: 12,
  },
  marcadorTexto: {
    color: '#555',
    fontSize: 10,
    fontFamily: 'monospace',
  },
  botoesRapidos: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    gap: 4,
  },
  botaoRapido: {
    flex: 1,
    paddingVertical: 6,
    borderWidth: 1,
    borderColor: '#2a2a4e',
    alignItems: 'center',
  },
  botaoRapidoAtivo: {
    borderColor: '#ffbf00',
    backgroundColor: '#1a3a5c',
  },
  botaoRapidoDesabilitado: {
    opacity: 0.4,
  },
  textoBotaoRapido: {
    color: '#555',
    fontSize: 11,
    fontFamily: 'monospace',
  },
  textoBotaoRapidoAtivo: {
    color: '#ffbf00',
    fontWeight: 'bold',
  },
});
