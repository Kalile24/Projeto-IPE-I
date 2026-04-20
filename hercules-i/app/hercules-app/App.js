/**
 * Projeto Hércules I — Equipe A2
 * IPE I — Instituto Militar de Engenharia — 2026.1
 *
 * Arquivo: App.js
 * Descrição: Entry point do aplicativo. Configura a navegação entre telas.
 *            Ícone conceitual do app: silhueta de catapulta medieval
 *            (símbolo ASCII: /--\__O  representando o braço de lançamento)
 * Autor: Kalile (Gerente / Firmware)
 * Versão: 1.0.0
 * Data: Abril/2026
 */

import React from 'react';
import { StatusBar } from 'expo-status-bar';
import { NavigationContainer } from '@react-navigation/native';
import { createStackNavigator } from '@react-navigation/stack';
import { GestureHandlerRootView } from 'react-native-gesture-handler';

import HomeScreen        from './src/screens/HomeScreen';
import ScanScreen        from './src/screens/ScanScreen';
import CalibrationScreen from './src/screens/CalibrationScreen';

const Stack = createStackNavigator();

// Opções de estilo padrão para o header de navegação
const headerStyle = {
  backgroundColor: '#0d0d0d',
  borderBottomWidth: 1,
  borderBottomColor: '#1a3a5c',
  elevation: 0,
  shadowOpacity: 0,
};

export default function App() {
  return (
    <GestureHandlerRootView style={{ flex: 1 }}>
      {/* Barra de status com estilo claro para fundo escuro */}
      <StatusBar style="light" backgroundColor="#0d0d0d" />

      <NavigationContainer
        theme={{
          colors: {
            background: '#0d0d0d',
            card: '#0d0d0d',
            text: '#ffffff',
            border: '#1a3a5c',
            primary: '#ffbf00',
            notification: '#ff3333',
          },
        }}
      >
        <Stack.Navigator
          initialRouteName="Home"
          screenOptions={{
            headerStyle,
            headerTintColor: '#ffbf00',
            headerTitleStyle: {
              fontFamily: 'monospace',
              fontWeight: 'bold',
              fontSize: 14,
              letterSpacing: 2,
            },
            cardStyle: { backgroundColor: '#0d0d0d' },
          }}
        >
          {/* Tela principal de controle */}
          <Stack.Screen
            name="Home"
            component={HomeScreen}
            options={{
              title: '⚙ HÉRCULES I',
              headerRight: () => null,
            }}
          />

          {/* Tela de scan BLE */}
          <Stack.Screen
            name="Scan"
            component={ScanScreen}
            options={{ title: 'BUSCAR DISPOSITIVO' }}
          />

          {/* Tela de calibração */}
          <Stack.Screen
            name="Calibration"
            component={CalibrationScreen}
            options={{ title: 'CALIBRAÇÃO' }}
          />
        </Stack.Navigator>
      </NavigationContainer>
    </GestureHandlerRootView>
  );
}
