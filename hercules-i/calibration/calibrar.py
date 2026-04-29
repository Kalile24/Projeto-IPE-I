#!/usr/bin/env python3
"""
Projeto Hércules I — Equipe A2
IPE I — Instituto Militar de Engenharia — 2026.1

Arquivo: calibration/calibrar.py
Descrição: Script de calibração da catapulta.
           Processa dados de testes, valida a precisão e gera a lookup table
           como header C++ pronto para uso no firmware.
Autor: Kalile (Gerente / Firmware)
Versão: 1.0.0
Data: Abril/2026

Uso:
    python calibrar.py --input dados_teste.csv --output lookup_table.h
    python calibrar.py --input dados_teste.csv --plot
"""

import argparse
import csv
import sys
import os
import math
from datetime import datetime
from collections import defaultdict

# ── Constantes de calibração ───────────────────────────────────────────────────

# Critério de aprovação: desvio padrão ≤ 15 cm = 0,15 m
DESVIO_MAX_ACEITAVEL_M = 0.15

# Distâncias alvo suportadas pelo sistema (0,50 m a 4,00 m, passo 0,25 m)
DISTANCIAS_ALVO = [round(0.50 + i * 0.25, 2) for i in range(15)]

# Tabela padrão de passos (usada como fallback para distâncias não testadas)
TABELA_PADRAO = [120, 185, 255, 330, 410, 495, 585, 680, 780, 885, 995, 1110, 1230, 1355, 1485]


# ── Funções utilitárias ────────────────────────────────────────────────────────

def media(valores):
    """Calcula a média aritmética de uma lista."""
    return sum(valores) / len(valores) if valores else 0.0


def desvio_padrao(valores):
    """Calcula o desvio padrão amostral de uma lista."""
    if len(valores) < 2:
        return 0.0
    m = media(valores)
    variancia = sum((x - m) ** 2 for x in valores) / (len(valores) - 1)
    return math.sqrt(variancia)


def interpolar_passos(dist_alvo, dist_real_media, passos_usados):
    """
    Sugere ajuste nos passos via interpolação linear simples.
    Assume relação aproximadamente linear entre passos e distância.

    dist_alvo      — distância desejada (m)
    dist_real_media — distância real medida com os passos atuais (m)
    passos_usados   — passos utilizados no teste

    Retorna sugestão de novos passos (inteiro).
    """
    if dist_real_media <= 0:
        return passos_usados
    fator = dist_alvo / dist_real_media
    return int(round(passos_usados * fator))


# ── Leitura do CSV ─────────────────────────────────────────────────────────────

def ler_dados(caminho_csv):
    """
    Lê o arquivo CSV de dados de teste.

    Formato esperado:
        distancia_m,passos,distancia_real_m,desvio_lateral_cm

    Retorna dicionário:
        {distancia_m: [(passos, distancia_real_m, desvio_lateral_cm), ...]}
    """
    dados = defaultdict(list)

    if not os.path.exists(caminho_csv):
        print(f"[ERRO] Arquivo não encontrado: {caminho_csv}")
        sys.exit(1)

    with open(caminho_csv, newline='', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        linhas_validas = 0

        for i, linha in enumerate(reader, start=2):  # linha 1 é o cabeçalho
            try:
                dist_alvo    = float(linha['distancia_m'])
                passos       = int(linha['passos'])
                dist_real    = float(linha['distancia_real_m'])
                desv_lateral = float(linha['desvio_lateral_cm'])
                dados[dist_alvo].append((passos, dist_real, desv_lateral))
                linhas_validas += 1
            except (KeyError, ValueError) as e:
                print(f"[AVISO] Linha {i} inválida (ignorada): {e}")

    print(f"[INFO] {linhas_validas} registros lidos de '{caminho_csv}'")
    return dados


# ── Processamento ──────────────────────────────────────────────────────────────

def processar(dados):
    """
    Processa os dados de teste para cada distância.

    Retorna lista de dicionários com os resultados por distância.
    """
    resultados = []

    for dist_alvo in sorted(dados.keys()):
        medicoes = dados[dist_alvo]

        passos_lista    = [m[0] for m in medicoes]
        real_lista      = [m[1] for m in medicoes]
        lateral_lista   = [m[2] for m in medicoes]

        passos_usados   = passos_lista[0]  # Todos devem ter o mesmo valor no CSV
        media_real      = media(real_lista)
        dp_real         = desvio_padrao(real_lista)
        media_lateral   = media(lateral_lista)
        erro_medio_m    = media_real - dist_alvo
        n_medicoes      = len(medicoes)
        aprovado        = dp_real <= DESVIO_MAX_ACEITAVEL_M

        # Sugestão de ajuste de passos se necessário
        passos_sugeridos = passos_usados
        if not aprovado or abs(erro_medio_m) > 0.05:
            passos_sugeridos = interpolar_passos(dist_alvo, media_real, passos_usados)

        resultados.append({
            'dist_alvo':       dist_alvo,
            'passos':          passos_usados,
            'n':               n_medicoes,
            'media_real':      media_real,
            'dp_real':         dp_real,
            'erro_medio':      erro_medio_m,
            'media_lateral':   media_lateral,
            'aprovado':        aprovado,
            'passos_sugeridos': passos_sugeridos,
        })

    return resultados


# ── Relatório no terminal ──────────────────────────────────────────────────────

def imprimir_relatorio(resultados):
    """Exibe tabela formatada no terminal com os resultados de calibração."""

    linha_sep = "─" * 100

    print("\n" + "=" * 100)
    print("  RELATÓRIO DE CALIBRAÇÃO — PROJETO HÉRCULES I — Equipe A2 — IME 2026.1")
    print("  Gerado em: " + datetime.now().strftime('%d/%m/%Y %H:%M:%S'))
    print("=" * 100)
    print(f"  Critério de aprovação: desvio padrão ≤ {DESVIO_MAX_ACEITAVEL_M * 100:.0f} cm")
    print(linha_sep)

    # Cabeçalho da tabela
    print(f"  {'DIST':>6} {'PASSOS':>7} {'N':>3} {'MEDIA_REAL':>11} {'DP':>8} {'ERRO':>8} "
          f"{'LAT_MED':>8} {'STATUS':>8} {'SUGESTÃO':>10}")
    print(f"  {'(m)':>6} {'(steps)':>7} {'':>3} {'(m)':>11} {'(cm)':>8} {'(cm)':>8} "
          f"{'(cm)':>8} {'':>8} {'(steps)':>10}")
    print(linha_sep)

    total   = len(resultados)
    aprovados = 0

    for r in resultados:
        status = "✓ OK" if r['aprovado'] else "✗ AJUSTE"
        if r['aprovado']:
            aprovados += 1

        # Indica se passos precisam de ajuste
        sug_str = str(r['passos_sugeridos']) if r['passos_sugeridos'] != r['passos'] else "—"

        print(
            f"  {r['dist_alvo']:>6.2f} "
            f"{r['passos']:>7d} "
            f"{r['n']:>3d} "
            f"{r['media_real']:>11.4f} "
            f"{r['dp_real'] * 100:>8.2f} "
            f"{r['erro_medio'] * 100:>8.2f} "
            f"{r['media_lateral']:>8.2f} "
            f"{status:>8} "
            f"{sug_str:>10}"
        )

    print(linha_sep)
    print(f"  RESULTADO: {aprovados}/{total} distâncias aprovadas no critério de precisão.")

    # Verifica distâncias não testadas
    testadas = {r['dist_alvo'] for r in resultados}
    nao_testadas = [d for d in DISTANCIAS_ALVO if d not in testadas]
    if nao_testadas:
        print(f"\n  [AVISO] Distâncias não testadas (usando tabela padrão): "
              f"{', '.join(f'{d:.2f}m' for d in nao_testadas)}")

    print("=" * 100 + "\n")


# ── Montagem da lookup table final ────────────────────────────────────────────

def montar_tabela_final(resultados):
    """
    Constrói a tabela final de passos para todas as 15 distâncias.
    Usa valores calibrados onde disponíveis; valores padrão onde não testado.
    """
    mapa = {r['dist_alvo']: r['passos_sugeridos'] for r in resultados}
    tabela = []
    for i, dist in enumerate(DISTANCIAS_ALVO):
        passos = mapa.get(dist, TABELA_PADRAO[i])
        tabela.append(passos)
    return tabela


# ── Geração do header C++ ─────────────────────────────────────────────────────

def gerar_header_cpp(tabela, caminho_saida):
    """Gera o arquivo lookup_table.h com a tabela calibrada."""
    agora = datetime.now().strftime('%d/%m/%Y %H:%M')

    linhas_distances = ', '.join(f'{d:.2f}f' for d in DISTANCIAS_ALVO)
    linhas_steps     = ', '.join(f'{s:4d}' for s in tabela)

    # Gera comentário com a tabela legível
    tabela_comentario = ''
    for i, (dist, passos) in enumerate(zip(DISTANCIAS_ALVO, tabela)):
        tabela_comentario += f' *   [{i:02d}]  {dist:.2f} m  →  {passos:4d} passos\n'

    conteudo = f"""/**
 * Projeto Hércules I — Equipe A2
 * IPE I — Instituto Militar de Engenharia — 2026.1
 *
 * Arquivo: lookup_table.h
 * Descrição: Tabela de calibração gerada automaticamente.
 *            SUBSTITUI o arquivo lookup_table.h do firmware.
 * Gerado em: {agora}
 * Script:    calibrar.py v1.0.0
 *
 * Em campo: envie CAL:X.XX:N por Serial ou Bluetooth para testar
 * novos valores na RAM antes de gravar esta tabela no firmware.
 *
 * Tabela de calibração:
{tabela_comentario} */

#ifndef LOOKUP_TABLE_H
#define LOOKUP_TABLE_H

#define TABLE_SIZE   15
#define DIST_MIN_M   0.50f
#define DIST_MAX_M   4.00f
#define DIST_STEP_M  0.25f

const float DISTANCES_TABLE[TABLE_SIZE] = {{
    {linhas_distances}
}};

const int STEPS_TABLE[TABLE_SIZE] = {{
    {linhas_steps}
}};

#endif // LOOKUP_TABLE_H
"""

    with open(caminho_saida, 'w', encoding='utf-8') as f:
        f.write(conteudo)

    print(f"[OK] Header C++ gerado: '{caminho_saida}'")


# ── Geração do CSV de relatório ───────────────────────────────────────────────

def gerar_csv_relatorio(resultados, caminho_saida):
    """Salva todos os dados processados em CSV."""
    campos = [
        'dist_alvo_m', 'passos_originais', 'n_medicoes',
        'media_real_m', 'dp_real_cm', 'erro_medio_cm',
        'media_lateral_cm', 'aprovado', 'passos_sugeridos'
    ]

    with open(caminho_saida, 'w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=campos)
        writer.writeheader()
        for r in resultados:
            writer.writerow({
                'dist_alvo_m':      f"{r['dist_alvo']:.2f}",
                'passos_originais':  r['passos'],
                'n_medicoes':        r['n'],
                'media_real_m':      f"{r['media_real']:.4f}",
                'dp_real_cm':        f"{r['dp_real'] * 100:.2f}",
                'erro_medio_cm':     f"{r['erro_medio'] * 100:.2f}",
                'media_lateral_cm':  f"{r['media_lateral']:.2f}",
                'aprovado':          'SIM' if r['aprovado'] else 'NAO',
                'passos_sugeridos':  r['passos_sugeridos'],
            })

    print(f"[OK] Relatório CSV salvo: '{caminho_saida}'")


# ── Geração de gráfico (opcional) ─────────────────────────────────────────────

def gerar_grafico(resultados, dados_brutos):
    """Gera gráfico de calibração com matplotlib."""
    try:
        import matplotlib.pyplot as plt
        import matplotlib.patches as mpatches
    except ImportError:
        print("[ERRO] matplotlib não instalado. Execute: pip install matplotlib")
        return

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))
    fig.suptitle('Calibração — Projeto Hércules I\nEquipe A2 — IME 2026.1',
                 fontsize=13, fontweight='bold')

    # ── Gráfico 1: Distância alvo vs. distância real média ─────────────
    dists_alvo  = [r['dist_alvo']   for r in resultados]
    medias_real = [r['media_real']  for r in resultados]
    dps         = [r['dp_real']     for r in resultados]
    cores       = ['green' if r['aprovado'] else 'red' for r in resultados]

    ax1.errorbar(dists_alvo, medias_real, yerr=dps, fmt='none', ecolor='gray',
                 capsize=4, linewidth=1.5, label='Desvio padrão')
    ax1.scatter(dists_alvo, medias_real, c=cores, s=60, zorder=5)
    ax1.plot([0.5, 4.0], [0.5, 4.0], 'b--', linewidth=1, alpha=0.5, label='Ideal')
    ax1.set_xlabel('Distância Alvo (m)')
    ax1.set_ylabel('Distância Real Média (m)')
    ax1.set_title('Distância Alvo × Distância Real')
    ax1.grid(True, alpha=0.3)
    ax1.legend(handles=[
        mpatches.Patch(color='green', label='Aprovado'),
        mpatches.Patch(color='red',   label='Requer ajuste'),
    ])

    # ── Gráfico 2: Passos vs. distância (com sugestão de ajuste) ───────
    passos_orig = [r['passos']            for r in resultados]
    passos_sug  = [r['passos_sugeridos']  for r in resultados]

    ax2.plot(dists_alvo, passos_orig, 'b-o', linewidth=2, markersize=6, label='Passos atuais')
    ax2.plot(dists_alvo, passos_sug,  'r--s', linewidth=1, markersize=6, alpha=0.7, label='Passos sugeridos')
    ax2.set_xlabel('Distância (m)')
    ax2.set_ylabel('Passos do Motor')
    ax2.set_title('Lookup Table: Distância × Passos')
    ax2.legend()
    ax2.grid(True, alpha=0.3)

    plt.tight_layout()
    caminho_png = 'relatorio_calibracao.png'
    plt.savefig(caminho_png, dpi=150, bbox_inches='tight')
    print(f"[OK] Gráfico salvo: '{caminho_png}'")
    plt.show()


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description='Calibração da catapulta Hércules I — Equipe A2 — IME 2026.1'
    )
    parser.add_argument('--input',  '-i', required=True,
                        help='Arquivo CSV com dados de teste')
    parser.add_argument('--output', '-o', default='lookup_table.h',
                        help='Arquivo de saída do header C++ (padrão: lookup_table.h)')
    parser.add_argument('--plot',   '-p', action='store_true',
                        help='Gerar gráfico de calibração (requer matplotlib)')
    parser.add_argument('--csv-relatorio', default='relatorio_calibracao.csv',
                        help='Arquivo CSV de saída do relatório (padrão: relatorio_calibracao.csv)')
    args = parser.parse_args()

    print("\n====================================================")
    print("  CALIBRADOR — PROJETO HÉRCULES I")
    print("  Equipe A2 — IPE I — IME 2026.1")
    print("====================================================")

    # Lê os dados brutos
    dados = ler_dados(args.input)
    if not dados:
        print("[ERRO] Nenhum dado válido encontrado no arquivo.")
        sys.exit(1)

    # Processa e calcula estatísticas
    resultados = processar(dados)

    # Imprime relatório no terminal
    imprimir_relatorio(resultados)

    # Monta tabela final (com valores sugeridos)
    tabela_final = montar_tabela_final(resultados)

    # Gera header C++
    gerar_header_cpp(tabela_final, args.output)

    # Gera CSV de relatório
    gerar_csv_relatorio(resultados, args.csv_relatorio)

    # Gera gráfico se solicitado
    if args.plot:
        gerar_grafico(resultados, dados)

    print("\n[CONCLUÍDO] Calibração processada com sucesso.")
    print(f"  → Copie '{args.output}' para a pasta firmware/hercules_firmware/")
    print(f"  → Recompile e faça o upload do firmware para o ESP32.\n")


if __name__ == '__main__':
    main()
