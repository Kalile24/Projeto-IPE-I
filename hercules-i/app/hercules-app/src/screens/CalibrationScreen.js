/**
 * Projeto Hércules I — Equipe A2
 * IPE I — Instituto Militar de Engenharia — 2026.1
 *
 * Arquivo: src/screens/CalibrationScreen.js
 * Descrição: Tela de calibração em campo.
 *            Permite atualizar a lookup table via BLE e exportar CSV.
 * Autor: Kalile (Gerente / Firmware)
 * Versão: 1.0.0
 * Data: Abril/2026
 */

import React, { useState, useCallback } from 'react';
import {
  View,
  Text,
  TextInput,
  TouchableOpacity,
  ScrollView,
  StyleSheet,
  Alert,
  Share,
  SafeAreaView,
} from 'react-native';

import BLEService from '../services/BLEService';
import { DIST_MIN, DIST_MAX, DIST_STEP } from '../constants/ble';

// Gera array de distâncias de 0.50 a 4.00 em passos de 0.25
const DISTANCIAS = [];
for (let d = DIST_MIN; d <= DIST_MAX + 0.001; d += DIST_STEP) {
  DISTANCIAS.push(parseFloat(d.toFixed(2)));
}

// Tabela padrão em memória (espelha a do firmware)
const TABELA_PADRAO = [120, 185, 255, 330, 410, 495, 585, 680, 780, 885, 995, 1110, 1230, 1355, 1485];

export default function CalibrationScreen({ navigation }) {
  // Distância selecionada para atualização
  const [distSelecionada, setDistSelecionada] = useState(DISTANCIAS[0]);
  // Passos inseridos pelo usuário
  const [passosInput, setPassosInput]         = useState('');
  // Cópia local da tabela (para exibição e exportação)
  const [tabela, setTabela]                   = useState([...TABELA_PADRAO]);
  // Índice selecionado no seletor de distância
  const [idxSelecionado, setIdxSelecionado]   = useState(0);

  /** Calcula índice correspondente à distância na tabela */
  const calcIdx = (dist) => {
    return Math.round((dist - DIST_MIN) / DIST_STEP);
  };

  /** Atualiza tabela local e envia comando CAL: ao ESP32 */
  const handleAtualizar = useCallback(async () => {
    const passos = parseInt(passosInput, 10);
    if (isNaN(passos) || passos <= 0 || passos > 9999) {
      Alert.alert('Valor inválido', 'Insira um número de passos entre 1 e 9999.');
      return;
    }

    if (!BLEService.isConnected()) {
      Alert.alert('Não conectado', 'Conecte ao ESP32 antes de atualizar a tabela.');
      return;
    }

    const distStr = distSelecionada.toFixed(2);
    const cmd     = `CAL:${distStr}:${passos}`;

    try {
      await BLEService.sendCommand(cmd);

      // Atualiza tabela local
      const idx = calcIdx(distSelecionada);
      const novaTabela = [...tabela];
      novaTabela[idx] = passos;
      setTabela(novaTabela);

      Alert.alert('Atualizado', `${distStr} m → ${passos} passos enviado ao ESP32.`);
      setPassosInput('');
    } catch (erro) {
      Alert.alert('Erro ao enviar', erro.message);
    }
  }, [distSelecionada, passosInput, tabela]);

  /** Exporta a tabela como CSV e abre o compartilhamento do sistema */
  const handleExportar = useCallback(async () => {
    let csv = 'distancia_m,passos\n';
    DISTANCIAS.forEach((dist, i) => {
      csv += `${dist.toFixed(2)},${tabela[i]}\n`;
    });

    // Gera também o conteúdo do header C++
    let headerCpp = '// Gerado pelo app Hércules I em ' + new Date().toLocaleString('pt-BR') + '\n';
    headerCpp    += '// Projeto Hércules I — Equipe A2 — IME 2026.1\n';
    headerCpp    += 'const float DISTANCES[] = {' + DISTANCIAS.map(d => d.toFixed(2) + 'f').join(', ') + '};\n';
    headerCpp    += 'const int STEPS[] = {' + tabela.join(', ') + '};\n';
    headerCpp    += 'const int TABLE_SIZE = 15;\n';

    const conteudo = `=== CSV ===\n${csv}\n=== C++ HEADER ===\n${headerCpp}`;

    try {
      await Share.share({
        title: 'Lookup Table — Hércules I',
        message: conteudo,
      });
    } catch (erro) {
      Alert.alert('Erro ao exportar', erro.message);
    }
  }, [tabela]);

  // ─────────────────────────────────────────────────────────────────────

  return (
    <SafeAreaView style={styles.safeArea}>
      <ScrollView style={styles.scroll} contentContainerStyle={styles.conteudo}>

        {/* Header */}
        <View style={styles.header}>
          <Text style={styles.titulo}>CALIBRAÇÃO</Text>
          <Text style={styles.subtitulo}>Atualização da Lookup Table em Campo</Text>
        </View>

        {/* Formulário de atualização */}
        <View style={styles.card}>
          <Text style={styles.labelCampo}>DISTÂNCIA ALVO</Text>

          {/* Seletor de distância (scroll horizontal) */}
          <ScrollView horizontal showsHorizontalScrollIndicator={false} style={styles.seletorScroll}>
            {DISTANCIAS.map((dist, i) => (
              <TouchableOpacity
                key={dist}
                style={[styles.chipDist, idxSelecionado === i && styles.chipDistAtivo]}
                onPress={() => {
                  setIdxSelecionado(i);
                  setDistSelecionada(dist);
                  setPassosInput(String(tabela[i]));
                }}
              >
                <Text style={[styles.chipDistTexto, idxSelecionado === i && styles.chipDistTextoAtivo]}>
                  {dist.toFixed(2)}m
                </Text>
              </TouchableOpacity>
            ))}
          </ScrollView>

          {/* Distância e campo de passos */}
          <View style={styles.inputRow}>
            <View style={styles.inputGroup}>
              <Text style={styles.labelCampo}>DISTÂNCIA</Text>
              <View style={styles.valorFixo}>
                <Text style={styles.valorFixoTexto}>{distSelecionada.toFixed(2)} m</Text>
              </View>
            </View>

            <Text style={styles.seta}>→</Text>

            <View style={styles.inputGroup}>
              <Text style={styles.labelCampo}>PASSOS</Text>
              <TextInput
                style={styles.inputPassos}
                value={passosInput}
                onChangeText={setPassosInput}
                keyboardType="numeric"
                placeholder="ex: 410"
                placeholderTextColor="#333"
                maxLength={4}
              />
            </View>
          </View>

          <TouchableOpacity style={styles.botaoAtualizar} onPress={handleAtualizar}>
            <Text style={styles.textoBotaoAtualizar}>▶ ATUALIZAR TABELA</Text>
          </TouchableOpacity>
        </View>

        {/* Tabela completa */}
        <View style={styles.card}>
          <View style={styles.tabelaHeader}>
            <Text style={styles.labelCampo}>LOOKUP TABLE ATUAL</Text>
            <TouchableOpacity onPress={handleExportar}>
              <Text style={styles.botaoExportar}>⬆ EXPORTAR CSV</Text>
            </TouchableOpacity>
          </View>

          {/* Cabeçalho da tabela */}
          <View style={styles.tabelaLinha}>
            <Text style={[styles.tabelaCelula, styles.tabelaCabecalho]}>#</Text>
            <Text style={[styles.tabelaCelula, styles.tabelaCabecalho, styles.celulaDistancia]}>DIST (m)</Text>
            <Text style={[styles.tabelaCelula, styles.tabelaCabecalho, styles.celulaPassos]}>PASSOS</Text>
          </View>

          {DISTANCIAS.map((dist, i) => (
            <TouchableOpacity
              key={dist}
              style={[styles.tabelaLinha, i % 2 === 0 ? styles.linhaEven : styles.linhaOdd, idxSelecionado === i && styles.linhaSelecionada]}
              onPress={() => {
                setIdxSelecionado(i);
                setDistSelecionada(dist);
                setPassosInput(String(tabela[i]));
              }}
            >
              <Text style={[styles.tabelaCelula, styles.textoTabela]}>{String(i).padStart(2, '0')}</Text>
              <Text style={[styles.tabelaCelula, styles.textoTabela, styles.celulaDistancia]}>{dist.toFixed(2)}</Text>
              <Text style={[styles.tabelaCelula, styles.textoTabela, styles.celulaPassos, styles.textoPassos]}>{tabela[i]}</Text>
            </TouchableOpacity>
          ))}
        </View>

        {/* Voltar */}
        <TouchableOpacity style={styles.botaoVoltar} onPress={() => navigation.goBack()}>
          <Text style={styles.textoBotaoVoltar}>← VOLTAR</Text>
        </TouchableOpacity>

      </ScrollView>
    </SafeAreaView>
  );
}

// ─── Estilos ──────────────────────────────────────────────────────────────────

const styles = StyleSheet.create({
  safeArea: { flex: 1, backgroundColor: '#0d0d0d' },
  scroll: { flex: 1 },
  conteudo: { padding: 12, gap: 12, paddingBottom: 32 },

  header: { paddingVertical: 8, borderBottomWidth: 1, borderBottomColor: '#1a1a2e' },
  titulo: { color: '#ffbf00', fontSize: 18, fontFamily: 'monospace', fontWeight: 'bold', letterSpacing: 2 },
  subtitulo: { color: '#555', fontSize: 11, fontFamily: 'monospace', marginTop: 2 },

  card: { backgroundColor: '#1a1a2e', padding: 12, gap: 10, borderWidth: 1, borderColor: '#2a2a4e' },

  labelCampo: { color: '#555', fontSize: 10, fontFamily: 'monospace', letterSpacing: 2, marginBottom: 4 },

  seletorScroll: { flexGrow: 0 },
  chipDist: { paddingHorizontal: 10, paddingVertical: 5, borderWidth: 1, borderColor: '#2a2a4e', marginRight: 6 },
  chipDistAtivo: { borderColor: '#ffbf00', backgroundColor: '#1a3a5c' },
  chipDistTexto: { color: '#555', fontFamily: 'monospace', fontSize: 11 },
  chipDistTextoAtivo: { color: '#ffbf00', fontWeight: 'bold' },

  inputRow: { flexDirection: 'row', alignItems: 'flex-end', gap: 8 },
  inputGroup: { flex: 1 },
  valorFixo: { backgroundColor: '#0d0d0d', padding: 10, borderWidth: 1, borderColor: '#2a2a4e' },
  valorFixoTexto: { color: '#888', fontFamily: 'monospace', fontSize: 18 },
  seta: { color: '#ffbf00', fontSize: 20, marginBottom: 10 },
  inputPassos: {
    backgroundColor: '#0d0d0d',
    borderWidth: 1,
    borderColor: '#ffbf00',
    padding: 10,
    color: '#ffbf00',
    fontFamily: 'monospace',
    fontSize: 18,
  },

  botaoAtualizar: { backgroundColor: '#1a3a5c', borderWidth: 1, borderColor: '#ffbf00', padding: 14, alignItems: 'center' },
  textoBotaoAtualizar: { color: '#ffbf00', fontFamily: 'monospace', fontSize: 14, fontWeight: 'bold', letterSpacing: 2 },

  // Tabela
  tabelaHeader: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center' },
  botaoExportar: { color: '#00ff88', fontFamily: 'monospace', fontSize: 11, letterSpacing: 1 },

  tabelaLinha: { flexDirection: 'row', paddingVertical: 6 },
  tabelaCabecalho: { color: '#555', fontWeight: 'bold' },
  linhaEven: { backgroundColor: '#0d0d0d' },
  linhaOdd: { backgroundColor: '#111' },
  linhaSelecionada: { backgroundColor: '#1a3a5c' },

  tabelaCelula: { fontFamily: 'monospace', fontSize: 12, paddingHorizontal: 4 },
  celulaDistancia: { flex: 2 },
  celulaPassos: { flex: 2, textAlign: 'right' },
  textoTabela: { color: '#aaa' },
  textoPassos: { color: '#ffbf00', fontWeight: 'bold' },

  botaoVoltar: { alignItems: 'center', paddingVertical: 12, borderTopWidth: 1, borderTopColor: '#1a1a2e' },
  textoBotaoVoltar: { color: '#555', fontFamily: 'monospace', fontSize: 12, letterSpacing: 2 },
});
