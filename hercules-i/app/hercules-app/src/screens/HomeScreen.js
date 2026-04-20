/**
 * Projeto Hércules I — Equipe A2
 * IPE I — Instituto Militar de Engenharia — 2026.1
 *
 * Arquivo: src/screens/HomeScreen.js
 * Descrição: Tela principal de controle da catapulta.
 *            Exibe status, seletor de distância e painel de controle.
 * Autor: Kalile (Gerente / Firmware)
 * Versão: 1.0.0
 * Data: Abril/2026
 */

import React, { useState, useEffect, useRef, useCallback } from 'react';
import {
  View,
  Text,
  TouchableOpacity,
  ScrollView,
  StyleSheet,
  Animated,
  Alert,
  ActivityIndicator,
  SafeAreaView,
} from 'react-native';

import BLEService from '../services/BLEService';
import BatteryIndicator from '../components/BatteryIndicator';
import ConnectionStatusBar from '../components/StatusBar';
import DistanceSlider from '../components/DistanceSlider';
import { ESTADOS } from '../constants/ble';

// Número máximo de mensagens no log
const MAX_LOG_MSGS = 5;

export default function HomeScreen({ navigation }) {
  // ── Estado da conexão ──────────────────────────────────────────────────
  const [conectado, setConectado]       = useState(false);
  const [nomeDevice, setNomeDevice]     = useState('');

  // ── Estado da FSM (espelho do ESP32) ──────────────────────────────────
  const [fsm, setFsm] = useState(ESTADOS.IDLE);

  // ── Distância selecionada ──────────────────────────────────────────────
  const [distancia, setDistancia] = useState(1.50);

  // ── Bateria ───────────────────────────────────────────────────────────
  const [batPercent, setBatPercent] = useState(null);
  const [batTensao,  setBatTensao]  = useState(null);

  // ── Log de mensagens ──────────────────────────────────────────────────
  const [logMsgs, setLogMsgs]   = useState([]);
  const scrollRef               = useRef(null);

  // ── Animação do botão DISPARAR (pulse) ────────────────────────────────
  const pulseAnim = useRef(new Animated.Value(1)).current;
  const pulseLoop = useRef(null);

  // ── Carregamento do botão PREPARAR ────────────────────────────────────
  const [preparando, setPreparando] = useState(false);

  // ─────────────────────────────────────────────────────────────────────

  /** Adiciona mensagem ao log com timestamp */
  const adicionarLog = useCallback((msg) => {
    const agora = new Date();
    const hora  = agora.toTimeString().slice(0, 8);
    setLogMsgs((prev) => {
      const nova = [...prev, `[${hora}] ${msg}`];
      return nova.slice(-MAX_LOG_MSGS);
    });
  }, []);

  /** Inicia animação de pulse no botão DISPARAR */
  const iniciarPulse = useCallback(() => {
    pulseLoop.current = Animated.loop(
      Animated.sequence([
        Animated.timing(pulseAnim, { toValue: 1.06, duration: 600, useNativeDriver: true }),
        Animated.timing(pulseAnim, { toValue: 1.00, duration: 600, useNativeDriver: true }),
      ])
    );
    pulseLoop.current.start();
  }, [pulseAnim]);

  /** Para animação de pulse */
  const pararPulse = useCallback(() => {
    pulseLoop.current?.stop();
    pulseAnim.setValue(1);
  }, [pulseAnim]);

  /** Processa mensagem de status recebida do ESP32 */
  const processarStatus = useCallback((msg) => {
    adicionarLog(msg);

    if (msg.startsWith('TENSIONING:')) {
      setFsm(ESTADOS.TENSIONING);
      setPreparando(true);
    } else if (msg === 'ARMED') {
      setFsm(ESTADOS.ARMED);
      setPreparando(false);
      iniciarPulse();
    } else if (msg === 'FIRED') {
      setFsm(ESTADOS.FIRING);
      pararPulse();
    } else if (msg === 'RETURNING') {
      setFsm(ESTADOS.RETURNING);
    } else if (msg === 'IDLE') {
      setFsm(ESTADOS.IDLE);
      setPreparando(false);
    } else if (msg.startsWith('BATTERY_LOW:') || msg.startsWith('STATUS:BAT:')) {
      // Extrai percentual de bateria de mensagens do tipo BATTERY_LOW:85%
      const match = msg.match(/(\d+)%/);
      if (match) setBatPercent(parseInt(match[1], 10));
      // Extrai tensão se presente
      const tensaoMatch = msg.match(/([\d.]+)V/);
      if (tensaoMatch) setBatTensao(tensaoMatch[1] + 'V');
    } else if (msg.startsWith('STATUS:')) {
      // STATUS:IDLE:BAT:92%:8.4V:DIST:1.50m
      const batMatch = msg.match(/BAT:(\d+)%:([\d.]+)V/);
      if (batMatch) {
        setBatPercent(parseInt(batMatch[1], 10));
        setBatTensao(batMatch[2] + 'V');
      }
    }
  }, [adicionarLog, iniciarPulse, pararPulse]);

  // ── Conecta ao BLE quando a tela recebe parâmetro de device ──────────
  useEffect(() => {
    const conectarDispositivo = async (deviceId, nome) => {
      try {
        await BLEService.connect(deviceId, () => {
          // Callback de desconexão
          setConectado(false);
          setNomeDevice('');
          setFsm(ESTADOS.IDLE);
          setPreparando(false);
          pararPulse();
          adicionarLog('Dispositivo desconectado.');
        });
        setConectado(true);
        setNomeDevice(nome);
        adicionarLog(`Conectado a ${nome}`);

        // Inscreve nas notificações de status
        BLEService.subscribeToStatus(processarStatus);

        // Solicita status inicial
        await BLEService.sendCommand('STATUS');
      } catch (erro) {
        Alert.alert('Erro de conexão', erro.message);
      }
    };

    // Verifica se há parâmetros de navegação (vindos de ScanScreen)
    const unsubscribe = navigation.addListener('focus', () => {
      const params = navigation.getState()?.routes?.find(r => r.name === 'Home')?.params;
      if (params?.deviceId && !BLEService.isConnected()) {
        conectarDispositivo(params.deviceId, params.deviceName || 'Hercules-I');
      }
    });

    return () => {
      unsubscribe();
    };
  }, [navigation, adicionarLog, processarStatus, pararPulse]);

  // ── Ações dos botões ──────────────────────────────────────────────────

  const handlePreparar = async () => {
    if (!conectado) return;
    try {
      setPreparando(true);
      // Envia distância e depois ARM
      const distStr = distancia.toFixed(2);
      await BLEService.sendCommand(`SET:${distStr}`);
      await BLEService.sendCommand('ARM');
    } catch (erro) {
      setPreparando(false);
      Alert.alert('Erro', erro.message);
    }
  };

  const handleDisparar = async () => {
    if (!conectado || fsm !== ESTADOS.ARMED) return;
    Alert.alert(
      'CONFIRMAR DISPARO',
      `Disparar para ${distancia.toFixed(2)} m?`,
      [
        { text: 'Cancelar', style: 'cancel' },
        {
          text: 'DISPARAR',
          style: 'destructive',
          onPress: async () => {
            try {
              await BLEService.sendCommand('FIRE');
            } catch (erro) {
              Alert.alert('Erro', erro.message);
            }
          },
        },
      ]
    );
  };

  const handleAbortar = async () => {
    if (!conectado) return;
    try {
      await BLEService.sendCommand('ABORT');
      pararPulse();
      setPreparando(false);
      adicionarLog('Comando ABORT enviado.');
    } catch (erro) {
      Alert.alert('Erro', erro.message);
    }
  };

  const handleIrParaScan = () => {
    navigation.navigate('Scan');
  };

  // ── Determina estado de habilitação dos botões ────────────────────────
  const btnPreparar = conectado && (fsm === ESTADOS.IDLE);
  const btnDisparar = conectado && (fsm === ESTADOS.ARMED);
  const btnAbortar  = conectado && (fsm === ESTADOS.ARMED || fsm === ESTADOS.TENSIONING);
  const sliderAtivo = conectado && (fsm === ESTADOS.IDLE);

  // ─────────────────────────────────────────────────────────────────────

  return (
    <SafeAreaView style={styles.safeArea}>
      <ScrollView
        style={styles.scroll}
        contentContainerStyle={styles.conteudo}
        ref={scrollRef}
      >

        {/* ── Header ─────────────────────────────────────────────────── */}
        <View style={styles.header}>
          <View style={styles.tituloRow}>
            <Text style={styles.titulo}>⚙ HÉRCULES I</Text>
            {batPercent !== null && (
              <BatteryIndicator percentual={batPercent} tensao={batTensao} />
            )}
          </View>
          <Text style={styles.subtitulo}>EQUIPE A2 — IPE I — IME 2026.1</Text>
        </View>

        {/* ── Barra de status BLE ────────────────────────────────────── */}
        <ConnectionStatusBar
          conectado={conectado}
          nomeDispositivo={nomeDevice}
          onPressConectar={handleIrParaScan}
        />

        {/* ── Seletor de distância ───────────────────────────────────── */}
        <View style={styles.secao}>
          <DistanceSlider
            valor={distancia}
            onChange={setDistancia}
            habilitado={sliderAtivo}
          />
        </View>

        {/* ── Display de status (log terminal) ─────────────────────────  */}
        <View style={styles.secao}>
          <Text style={styles.labelSecao}>LOG DO SISTEMA</Text>
          <View style={styles.terminal}>
            {logMsgs.length === 0 ? (
              <Text style={styles.terminalPlaceholder}>Aguardando mensagens...</Text>
            ) : (
              logMsgs.map((msg, i) => (
                <Text key={i} style={styles.terminalLinha}>{msg}</Text>
              ))
            )}
          </View>
        </View>

        {/* ── Painel de controle ─────────────────────────────────────── */}
        <View style={styles.secao}>
          <Text style={styles.labelSecao}>PAINEL DE CONTROLE</Text>

          {/* PREPARAR */}
          <TouchableOpacity
            style={[styles.botao, styles.botaoPreparar, !btnPreparar && styles.botaoDesabilitado]}
            onPress={handlePreparar}
            disabled={!btnPreparar}
            activeOpacity={0.7}
          >
            {preparando && fsm === ESTADOS.TENSIONING ? (
              <View style={styles.botaoConteudo}>
                <ActivityIndicator color="#fff" size="small" />
                <Text style={styles.textoBotao}> TENSIONANDO...</Text>
              </View>
            ) : (
              <Text style={styles.textoBotao}>[ PREPARAR ]</Text>
            )}
          </TouchableOpacity>

          {/* DISPARAR */}
          <Animated.View style={{ transform: [{ scale: pulseAnim }] }}>
            <TouchableOpacity
              style={[styles.botao, styles.botaoDisparar, !btnDisparar && styles.botaoDesabilitado]}
              onPress={handleDisparar}
              disabled={!btnDisparar}
              activeOpacity={0.7}
            >
              <Text style={[styles.textoBotao, styles.textoBotaoDisparar]}>[ DISPARAR ]</Text>
            </TouchableOpacity>
          </Animated.View>

          {/* ABORTAR */}
          <TouchableOpacity
            style={[styles.botao, styles.botaoAbortar, !btnAbortar && styles.botaoDesabilitado]}
            onPress={handleAbortar}
            disabled={!btnAbortar}
            activeOpacity={0.7}
          >
            <Text style={styles.textoBotao}>[ ABORTAR ]</Text>
          </TouchableOpacity>
        </View>

        {/* ── Indicador de estado FSM ───────────────────────────────── */}
        <View style={styles.fsmContainer}>
          {Object.values(ESTADOS).map((estado) => (
            <View
              key={estado}
              style={[styles.fsmEstado, fsm === estado && styles.fsmEstadoAtivo]}
            >
              <Text style={[styles.fsmTexto, fsm === estado && styles.fsmTextoAtivo]}>
                {estado}
              </Text>
            </View>
          ))}
        </View>

        {/* ── Link para calibração ──────────────────────────────────── */}
        <TouchableOpacity
          style={styles.linkCalibracao}
          onPress={() => navigation.navigate('Calibration')}
        >
          <Text style={styles.textoLinkCalibracao}>⚙ CALIBRAÇÃO AVANÇADA</Text>
        </TouchableOpacity>

      </ScrollView>
    </SafeAreaView>
  );
}

// ─── Estilos ─────────────────────────────────────────────────────────────────

const styles = StyleSheet.create({
  safeArea: {
    flex: 1,
    backgroundColor: '#0d0d0d',
  },
  scroll: {
    flex: 1,
  },
  conteudo: {
    padding: 12,
    gap: 12,
    paddingBottom: 32,
  },

  // Header
  header: {
    paddingVertical: 8,
  },
  tituloRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  titulo: {
    color: '#ffbf00',
    fontSize: 24,
    fontFamily: 'monospace',
    fontWeight: 'bold',
    letterSpacing: 3,
  },
  subtitulo: {
    color: '#555',
    fontSize: 10,
    fontFamily: 'monospace',
    letterSpacing: 2,
    marginTop: 2,
  },

  // Seções
  secao: {
    gap: 6,
  },
  labelSecao: {
    color: '#555',
    fontSize: 10,
    fontFamily: 'monospace',
    letterSpacing: 2,
    textTransform: 'uppercase',
  },

  // Terminal de log
  terminal: {
    backgroundColor: '#0a0a0a',
    borderWidth: 1,
    borderColor: '#1a3a5c',
    padding: 10,
    minHeight: 90,
  },
  terminalPlaceholder: {
    color: '#333',
    fontFamily: 'monospace',
    fontSize: 12,
    fontStyle: 'italic',
  },
  terminalLinha: {
    color: '#00ff88',
    fontFamily: 'monospace',
    fontSize: 11,
    lineHeight: 18,
  },

  // Botões de controle
  botao: {
    padding: 18,
    alignItems: 'center',
    justifyContent: 'center',
    borderWidth: 2,
    marginVertical: 4,
  },
  botaoConteudo: {
    flexDirection: 'row',
    alignItems: 'center',
  },
  botaoPreparar: {
    backgroundColor: '#1a3a5c',
    borderColor: '#2a5a8c',
  },
  botaoDisparar: {
    backgroundColor: '#8b0000',
    borderColor: '#cc0000',
    paddingVertical: 24,
  },
  botaoAbortar: {
    backgroundColor: '#3d1f00',
    borderColor: '#cc6600',
  },
  botaoDesabilitado: {
    backgroundColor: '#1a1a1a',
    borderColor: '#2a2a2a',
    opacity: 0.5,
  },
  textoBotao: {
    color: '#ffffff',
    fontFamily: 'monospace',
    fontSize: 16,
    fontWeight: 'bold',
    letterSpacing: 3,
  },
  textoBotaoDisparar: {
    fontSize: 20,
    letterSpacing: 4,
  },

  // FSM indicator
  fsmContainer: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    borderWidth: 1,
    borderColor: '#1a1a2e',
    padding: 8,
    gap: 4,
  },
  fsmEstado: {
    flex: 1,
    alignItems: 'center',
    paddingVertical: 4,
    borderWidth: 1,
    borderColor: '#1a1a2e',
  },
  fsmEstadoAtivo: {
    borderColor: '#ffbf00',
    backgroundColor: '#1a1a2e',
  },
  fsmTexto: {
    color: '#333',
    fontSize: 7,
    fontFamily: 'monospace',
    textAlign: 'center',
  },
  fsmTextoAtivo: {
    color: '#ffbf00',
    fontWeight: 'bold',
  },

  // Link de calibração
  linkCalibracao: {
    alignItems: 'center',
    paddingVertical: 8,
  },
  textoLinkCalibracao: {
    color: '#555',
    fontFamily: 'monospace',
    fontSize: 11,
    letterSpacing: 2,
    textDecorationLine: 'underline',
  },
});
