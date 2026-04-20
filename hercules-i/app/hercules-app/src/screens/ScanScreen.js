/**
 * Projeto Hércules I — Equipe A2
 * IPE I — Instituto Militar de Engenharia — 2026.1
 *
 * Arquivo: src/screens/ScanScreen.js
 * Descrição: Tela de scan e conexão BLE.
 *            Lista dispositivos encontrados e permite conectar ao ESP32.
 * Autor: Kalile (Gerente / Firmware)
 * Versão: 1.0.0
 * Data: Abril/2026
 */

import React, { useState, useEffect, useCallback } from 'react';
import {
  View,
  Text,
  FlatList,
  TouchableOpacity,
  StyleSheet,
  ActivityIndicator,
  Alert,
  SafeAreaView,
  Platform,
  PermissionsAndroid,
} from 'react-native';

import BLEService from '../services/BLEService';
import { SCAN_TIMEOUT_MS } from '../constants/ble';

export default function ScanScreen({ navigation }) {
  const [escaneando,   setEscaneando]   = useState(false);
  const [dispositivos, setDispositivos] = useState([]);
  const [conectando,   setConectando]   = useState(null);  // id do dispositivo conectando
  const [progresso,    setProgresso]    = useState(0);      // segundos do scan

  // ── Solicita permissões de BLE no Android ─────────────────────────────
  const solicitarPermissoes = useCallback(async () => {
    if (Platform.OS !== 'android') return true;

    const permissoes = [
      PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,
    ];

    // Android 12+ requer permissões adicionais de BLE
    if (Platform.Version >= 31) {
      permissoes.push(
        PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN,
        PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT
      );
    }

    const resultados = await PermissionsAndroid.requestMultiple(permissoes);
    return Object.values(resultados).every(
      (r) => r === PermissionsAndroid.RESULTS.GRANTED
    );
  }, []);

  // ── Inicia scan BLE ───────────────────────────────────────────────────
  const iniciarScan = useCallback(async () => {
    const permitido = await solicitarPermissoes();
    if (!permitido) {
      Alert.alert(
        'Permissão negada',
        'É necessário conceder permissões de Bluetooth e Localização para escanear dispositivos.'
      );
      return;
    }

    setDispositivos([]);
    setEscaneando(true);
    setProgresso(0);

    // Contador de progresso (atualiza a cada segundo)
    const intervalo = setInterval(() => {
      setProgresso((p) => p + 1);
    }, 1000);

    try {
      await BLEService.startScan(
        (device) => {
          // Adiciona dispositivo à lista (sem duplicatas)
          setDispositivos((prev) => {
            const existe = prev.some((d) => d.id === device.id);
            if (existe) return prev;
            return [...prev, { id: device.id, name: device.name, rssi: device.rssi }];
          });
        },
        SCAN_TIMEOUT_MS
      );
    } catch (erro) {
      Alert.alert('Erro no scan', erro.message);
    } finally {
      clearInterval(intervalo);
      setEscaneando(false);
      setProgresso(0);
    }
  }, [solicitarPermissoes]);

  // ── Conecta ao dispositivo selecionado ────────────────────────────────
  const conectar = useCallback(async (device) => {
    setConectando(device.id);

    try {
      // Para o scan antes de conectar
      BLEService.stopScan();

      await BLEService.connect(device.id, () => {
        // Desconexão inesperada — volta para scan
        Alert.alert('Desconectado', 'O dispositivo foi desconectado.');
        navigation.navigate('Home', { deviceId: null });
      });

      Alert.alert(
        'Conectado!',
        `Conectado a ${device.name} com sucesso.`,
        [{ text: 'OK', onPress: () => navigation.navigate('Home', {
          deviceId: device.id,
          deviceName: device.name,
        }) }]
      );
    } catch (erro) {
      Alert.alert('Falha na conexão', erro.message);
    } finally {
      setConectando(null);
    }
  }, [navigation]);

  // ── Inicia scan ao entrar na tela ────────────────────────────────────
  useEffect(() => {
    iniciarScan();
    return () => {
      BLEService.stopScan();
    };
  }, [iniciarScan]);

  // ── Renderiza item da lista ───────────────────────────────────────────
  const renderItem = ({ item }) => {
    const estáConectando = conectando === item.id;
    const rssiCor = item.rssi > -60 ? '#00ff88' : item.rssi > -80 ? '#ffbf00' : '#ff3333';

    return (
      <View style={styles.itemDispositivo}>
        <View style={styles.itemInfo}>
          <Text style={styles.itemNome}>{item.name}</Text>
          <Text style={styles.itemId}>{item.id}</Text>
          <Text style={[styles.itemRssi, { color: rssiCor }]}>
            RSSI: {item.rssi} dBm
          </Text>
        </View>
        <TouchableOpacity
          style={[styles.botaoConectar, estáConectando && styles.botaoConectarDesabilitado]}
          onPress={() => conectar(item)}
          disabled={estáConectando || conectando !== null}
        >
          {estáConectando ? (
            <ActivityIndicator color="#ffbf00" size="small" />
          ) : (
            <Text style={styles.textoBotaoConectar}>CONECTAR</Text>
          )}
        </TouchableOpacity>
      </View>
    );
  };

  // ─────────────────────────────────────────────────────────────────────

  return (
    <SafeAreaView style={styles.safeArea}>
      <View style={styles.container}>

        {/* Header */}
        <View style={styles.header}>
          <Text style={styles.titulo}>BUSCA DE DISPOSITIVOS</Text>
          <Text style={styles.subtitulo}>Procurando por "Hercules-I" via Bluetooth</Text>
        </View>

        {/* Botão de scan / indicador de progresso */}
        <View style={styles.scanContainer}>
          {escaneando ? (
            <View style={styles.scanAtivo}>
              <ActivityIndicator color="#ffbf00" size="large" />
              <Text style={styles.scanTexto}>
                Escaneando... {progresso}s / {SCAN_TIMEOUT_MS / 1000}s
              </Text>
              <View style={styles.progressBar}>
                <View
                  style={[
                    styles.progressFill,
                    { width: `${(progresso / (SCAN_TIMEOUT_MS / 1000)) * 100}%` },
                  ]}
                />
              </View>
            </View>
          ) : (
            <TouchableOpacity style={styles.botaoScan} onPress={iniciarScan}>
              <Text style={styles.textoBotaoScan}>▶ BUSCAR DISPOSITIVOS</Text>
            </TouchableOpacity>
          )}
        </View>

        {/* Lista de dispositivos */}
        <View style={styles.listaContainer}>
          <Text style={styles.labelLista}>
            DISPOSITIVOS ENCONTRADOS ({dispositivos.length})
          </Text>
          {dispositivos.length === 0 && !escaneando ? (
            <View style={styles.vazio}>
              <Text style={styles.vazioTexto}>
                Nenhum dispositivo "Hercules-I" encontrado.{'\n'}
                Verifique se o ESP32 está ligado e próximo.
              </Text>
            </View>
          ) : (
            <FlatList
              data={dispositivos}
              keyExtractor={(item) => item.id}
              renderItem={renderItem}
              style={styles.lista}
              ItemSeparatorComponent={() => <View style={styles.separador} />}
            />
          )}
        </View>

        {/* Voltar sem conectar */}
        <TouchableOpacity
          style={styles.botaoVoltar}
          onPress={() => navigation.goBack()}
        >
          <Text style={styles.textoBotaoVoltar}>← VOLTAR</Text>
        </TouchableOpacity>

      </View>
    </SafeAreaView>
  );
}

// ─── Estilos ──────────────────────────────────────────────────────────────────

const styles = StyleSheet.create({
  safeArea: {
    flex: 1,
    backgroundColor: '#0d0d0d',
  },
  container: {
    flex: 1,
    padding: 12,
    gap: 12,
  },

  // Header
  header: {
    paddingVertical: 8,
    borderBottomWidth: 1,
    borderBottomColor: '#1a1a2e',
  },
  titulo: {
    color: '#ffbf00',
    fontSize: 18,
    fontFamily: 'monospace',
    fontWeight: 'bold',
    letterSpacing: 2,
  },
  subtitulo: {
    color: '#555',
    fontSize: 11,
    fontFamily: 'monospace',
    marginTop: 2,
  },

  // Scan
  scanContainer: {
    alignItems: 'center',
    paddingVertical: 8,
  },
  scanAtivo: {
    alignItems: 'center',
    gap: 8,
    width: '100%',
  },
  scanTexto: {
    color: '#888',
    fontFamily: 'monospace',
    fontSize: 12,
  },
  progressBar: {
    width: '100%',
    height: 4,
    backgroundColor: '#1a1a2e',
    borderRadius: 2,
    overflow: 'hidden',
  },
  progressFill: {
    height: '100%',
    backgroundColor: '#ffbf00',
  },
  botaoScan: {
    backgroundColor: '#1a3a5c',
    borderWidth: 1,
    borderColor: '#ffbf00',
    paddingHorizontal: 24,
    paddingVertical: 14,
  },
  textoBotaoScan: {
    color: '#ffbf00',
    fontFamily: 'monospace',
    fontSize: 14,
    fontWeight: 'bold',
    letterSpacing: 2,
  },

  // Lista
  listaContainer: {
    flex: 1,
    gap: 6,
  },
  labelLista: {
    color: '#555',
    fontSize: 10,
    fontFamily: 'monospace',
    letterSpacing: 2,
  },
  lista: {
    flex: 1,
  },
  separador: {
    height: 1,
    backgroundColor: '#1a1a2e',
  },
  vazio: {
    flex: 1,
    justifyContent: 'center',
    alignItems: 'center',
  },
  vazioTexto: {
    color: '#333',
    fontFamily: 'monospace',
    fontSize: 13,
    textAlign: 'center',
    lineHeight: 22,
  },

  // Item de dispositivo
  itemDispositivo: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    backgroundColor: '#1a1a2e',
    padding: 12,
  },
  itemInfo: {
    flex: 1,
    gap: 2,
  },
  itemNome: {
    color: '#ffffff',
    fontFamily: 'monospace',
    fontSize: 14,
    fontWeight: 'bold',
  },
  itemId: {
    color: '#555',
    fontFamily: 'monospace',
    fontSize: 10,
  },
  itemRssi: {
    fontFamily: 'monospace',
    fontSize: 11,
  },
  botaoConectar: {
    backgroundColor: '#1a3a5c',
    borderWidth: 1,
    borderColor: '#ffbf00',
    paddingHorizontal: 14,
    paddingVertical: 8,
    minWidth: 90,
    alignItems: 'center',
  },
  botaoConectarDesabilitado: {
    opacity: 0.5,
  },
  textoBotaoConectar: {
    color: '#ffbf00',
    fontFamily: 'monospace',
    fontSize: 11,
    fontWeight: 'bold',
    letterSpacing: 1,
  },

  // Voltar
  botaoVoltar: {
    alignItems: 'center',
    paddingVertical: 12,
    borderTopWidth: 1,
    borderTopColor: '#1a1a2e',
  },
  textoBotaoVoltar: {
    color: '#555',
    fontFamily: 'monospace',
    fontSize: 12,
    letterSpacing: 2,
  },
});
