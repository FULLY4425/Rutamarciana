#!/bin/sh
# Pruebas de host para los núcleos puros (BITACORA + presupuesto EVA)
# Uso: ./run.sh   (o g++ directo; no necesita Arduino ni ESP32)
set -e
cd "$(dirname "$0")"
g++ -std=c++17 -Wall -Wextra -O1 -o /tmp/t_eva test_eva_budget.cpp && /tmp/t_eva
g++ -std=c++17 -Wall -Wextra -O1 -o /tmp/t_btx test_bitacora_core.cpp && /tmp/t_btx
g++ -std=c++17 -Wall -Wextra -O1 -o /tmp/t_pov test_pov_core.cpp && /tmp/t_pov
