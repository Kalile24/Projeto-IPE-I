#!/usr/bin/env python3
"""
Projeto Hércules I — Equipe A2
IPE I — Instituto Militar de Engenharia — 2026.1

Arquivo: tools/teste_ble.py
Descrição: Script de teste de comunicação BLE com o ESP32 a partir de Linux.
           Substitui o app mobile para testes rápidos de bancada.
           Usa a biblioteca 'bleak' (asyncio-based).
Autor: Kalile (Gerente / Firmware)
Versão: 1.0.0
Data: Abril/2026

Instalação:
    pip install bleak

Uso:
    python teste_ble.py --scan               # Lista dispositivos BLE
    python teste_ble.py --connect            # Conecta ao Hercules-I
    python teste_ble.py --cmd "SET:1.50"     # Envia um comando
    python teste_ble.py --cmd "ARM"          # Arma a catapulta
    python teste_ble.py --cmd "FIRE"         # Dispara
    python teste_ble.py --cmd "ABORT"        # Aborta
    python teste_ble.py --cmd "STATUS"       # Solicita status
    python teste_ble.py --monitor            # Monitora notificações por 30s
    python teste_ble.py --interativo         # Modo interativo (console de comandos)
"""

import asyncio
import argparse
import sys
import time
from datetime import datetime

try:
    from bleak import BleakScanner, BleakClient
    from bleak.exc import BleakError
except ImportError:
    print("[ERRO] Biblioteca 'bleak' não instalada.")
    print("       Execute: pip install bleak")
    sys.exit(1)

# ── Constantes BLE (devem ser idênticas ao firmware) ──────────────────────────

DEVICE_NAME      = "Hercules-I"
SERVICE_UUID     = "12345678-1234-1234-1234-123456789abc"
CMD_CHAR_UUID    = "12345678-1234-1234-1234-123456789ab1"
STATUS_CHAR_UUID = "12345678-1234-1234-1234-123456789ab2"

SCAN_TIMEOUT_S   = 10.0   # Duração do scan em segundos
MONITOR_TIMEOUT_S = 30.0  # Duração do monitoramento em segundos


# ── Utilitários ───────────────────────────────────────────────────────────────

def timestamp():
    """Retorna hora atual formatada."""
    return datetime.now().strftime('%H:%M:%S')


def log(msg, nivel='INFO'):
    """Imprime mensagem formatada com timestamp."""
    prefixos = {
        'INFO':  '\033[36m[INFO]\033[0m',
        'OK':    '\033[32m[ OK ]\033[0m',
        'ERRO':  '\033[31m[ERRO]\033[0m',
        'RX':    '\033[33m[ RX ]\033[0m',
        'TX':    '\033[35m[ TX ]\033[0m',
        'WARN':  '\033[33m[AVISO]\033[0m',
    }
    prefixo = prefixos.get(nivel, f'[{nivel}]')
    print(f"{timestamp()} {prefixo} {msg}")


# ── Scan de dispositivos ──────────────────────────────────────────────────────

async def cmd_scan():
    """Escaneia dispositivos BLE por SCAN_TIMEOUT_S segundos e exibe lista."""
    log(f"Iniciando scan BLE por {SCAN_TIMEOUT_S:.0f} segundos...", 'INFO')
    print("─" * 60)

    dispositivos_encontrados = []

    def callback_detectado(device, advertising_data):
        nome = device.name or '(sem nome)'
        rssi = advertising_data.rssi
        is_hercules = nome == DEVICE_NAME

        marker = " ◄ HÉRCULES I" if is_hercules else ""
        print(f"  {nome:30s} {device.address}  RSSI:{rssi:4d} dBm{marker}")

        if is_hercules:
            dispositivos_encontrados.append(device)

    async with BleakScanner(detection_callback=callback_detectado):
        await asyncio.sleep(SCAN_TIMEOUT_S)

    print("─" * 60)
    log(f"Scan finalizado. {len(dispositivos_encontrados)} dispositivo(s) 'Hercules-I' encontrado(s).", 'OK')

    if dispositivos_encontrados:
        log(f"Endereço para uso: {dispositivos_encontrados[0].address}", 'OK')

    return dispositivos_encontrados[0].address if dispositivos_encontrados else None


# ── Localiza dispositivo Hércules I ───────────────────────────────────────────

async def localizar_hercules():
    """Faz scan curto para encontrar o endereço do Hercules-I."""
    log(f"Procurando '{DEVICE_NAME}'...", 'INFO')
    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=SCAN_TIMEOUT_S)
    if device is None:
        log(f"Dispositivo '{DEVICE_NAME}' não encontrado. Verifique se o ESP32 está ligado e próximo.", 'ERRO')
        sys.exit(1)
    log(f"Encontrado: {device.name} ({device.address})", 'OK')
    return device.address


# ── Envio de comando único ────────────────────────────────────────────────────

async def cmd_enviar_comando(comando):
    """Conecta ao ESP32, envia um comando e aguarda resposta."""
    endereco = await localizar_hercules()

    async with BleakClient(endereco) as client:
        log(f"Conectado a {endereco}", 'OK')

        # Habilita notificações de status
        respostas = []

        def callback_notificacao(sender, data):
            msg = data.decode('utf-8', errors='replace')
            log(f"← {msg}", 'RX')
            respostas.append(msg)

        await client.start_notify(STATUS_CHAR_UUID, callback_notificacao)

        # Envia o comando
        log(f"→ {comando}", 'TX')
        await client.write_gatt_char(CMD_CHAR_UUID, comando.encode('utf-8'), response=True)

        # Aguarda resposta por até 5 segundos
        await asyncio.sleep(5.0)

        await client.stop_notify(STATUS_CHAR_UUID)
        log("Comando enviado e resposta recebida. Desconectando.", 'OK')


# ── Monitoramento contínuo ────────────────────────────────────────────────────

async def cmd_monitorar():
    """Conecta ao ESP32 e monitora todas as notificações por MONITOR_TIMEOUT_S segundos."""
    endereco = await localizar_hercules()

    log(f"Monitorando notificações por {MONITOR_TIMEOUT_S:.0f} segundos...", 'INFO')
    log("Pressione Ctrl+C para parar.", 'INFO')
    print("─" * 60)

    async with BleakClient(endereco) as client:
        log(f"Conectado a {endereco}", 'OK')

        def callback_notificacao(sender, data):
            msg = data.decode('utf-8', errors='replace')
            log(f"← {msg}", 'RX')

        await client.start_notify(STATUS_CHAR_UUID, callback_notificacao)

        try:
            await asyncio.sleep(MONITOR_TIMEOUT_S)
        except asyncio.CancelledError:
            pass

        await client.stop_notify(STATUS_CHAR_UUID)

    print("─" * 60)
    log("Monitoramento encerrado.", 'OK')


# ── Modo interativo ───────────────────────────────────────────────────────────

async def cmd_interativo():
    """
    Console interativo de comandos.
    Mantém a conexão BLE aberta e permite enviar múltiplos comandos.
    """
    endereco = await localizar_hercules()

    print("\n" + "=" * 60)
    print("  CONSOLE INTERATIVO — HÉRCULES I")
    print("  Comandos disponíveis:")
    print("    SET:X.XX   — Define distância (ex: SET:1.50)")
    print("    ARM        — Arma a catapulta")
    print("    FIRE       — Dispara")
    print("    ABORT      — Aborta operação")
    print("    STATUS     — Solicita status")
    print("    CAL:X.XX:N — Calibra distância")
    print("    quit/exit  — Encerra")
    print("=" * 60 + "\n")

    async with BleakClient(endereco, disconnected_callback=lambda c: log("Desconectado.", 'WARN')) as client:
        log(f"Conectado a {endereco}", 'OK')

        def callback_notificacao(sender, data):
            msg = data.decode('utf-8', errors='replace')
            log(f"← {msg}", 'RX')

        await client.start_notify(STATUS_CHAR_UUID, callback_notificacao)

        # Loop de leitura de comandos
        while True:
            try:
                cmd = await asyncio.get_event_loop().run_in_executor(None, input, "\n> Comando: ")
                cmd = cmd.strip()

                if not cmd:
                    continue
                if cmd.lower() in ('quit', 'exit', 'sair'):
                    break

                log(f"→ {cmd}", 'TX')
                await client.write_gatt_char(CMD_CHAR_UUID, cmd.encode('utf-8'), response=True)
                await asyncio.sleep(0.5)  # Aguarda possível notificação

            except (KeyboardInterrupt, EOFError):
                break
            except BleakError as e:
                log(f"Erro BLE: {e}", 'ERRO')
                break

        await client.stop_notify(STATUS_CHAR_UUID)

    log("Sessão encerrada.", 'OK')


# ── Teste de conectividade básica ─────────────────────────────────────────────

async def cmd_conectar():
    """Testa conexão básica: conecta, lê serviços e desconecta."""
    endereco = await localizar_hercules()

    async with BleakClient(endereco) as client:
        log(f"Conectado com sucesso a {endereco}", 'OK')

        # Lista serviços e características
        print("\n  Serviços e características encontrados:")
        print("─" * 60)
        for service in client.services:
            print(f"  Serviço: {service.uuid}")
            for char in service.characteristics:
                props = ', '.join(char.properties)
                print(f"    └─ Char: {char.uuid}  [{props}]")
        print("─" * 60)

        # Verifica se os UUIDs do Hércules I estão presentes
        uuids = [str(c.uuid) for s in client.services for c in s.characteristics]

        if CMD_CHAR_UUID in uuids:
            log(f"✓ CMD characteristic encontrada ({CMD_CHAR_UUID})", 'OK')
        else:
            log(f"✗ CMD characteristic NÃO encontrada!", 'ERRO')

        if STATUS_CHAR_UUID in uuids:
            log(f"✓ STATUS characteristic encontrada ({STATUS_CHAR_UUID})", 'OK')
        else:
            log(f"✗ STATUS characteristic NÃO encontrada!", 'ERRO')

    log("Desconectado.", 'OK')


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description='Teste BLE — Projeto Hércules I — Equipe A2 — IME 2026.1'
    )
    grupo = parser.add_mutually_exclusive_group(required=True)
    grupo.add_argument('--scan',       action='store_true',
                       help='Escaneia dispositivos BLE próximos')
    grupo.add_argument('--connect',    action='store_true',
                       help='Conecta e lista serviços do Hercules-I')
    grupo.add_argument('--cmd',        metavar='COMANDO',
                       help='Envia um comando e exibe a resposta (ex: --cmd "SET:1.50")')
    grupo.add_argument('--monitor',    action='store_true',
                       help=f'Monitora notificações por {MONITOR_TIMEOUT_S:.0f}s')
    grupo.add_argument('--interativo', action='store_true',
                       help='Abre console interativo de comandos')

    args = parser.parse_args()

    print("\n====================================================")
    print("  TESTE BLE — PROJETO HÉRCULES I — Equipe A2")
    print("  IPE I — Instituto Militar de Engenharia — 2026.1")
    print("====================================================\n")

    try:
        if args.scan:
            asyncio.run(cmd_scan())
        elif args.connect:
            asyncio.run(cmd_conectar())
        elif args.cmd:
            asyncio.run(cmd_enviar_comando(args.cmd))
        elif args.monitor:
            asyncio.run(cmd_monitorar())
        elif args.interativo:
            asyncio.run(cmd_interativo())
    except KeyboardInterrupt:
        print("\n[INFO] Interrompido pelo usuário.")
    except Exception as e:
        log(f"Erro fatal: {e}", 'ERRO')
        sys.exit(1)


if __name__ == '__main__':
    main()
